/* Kit mute and solo are lane-local and preserve level and track solo state. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static void publish_masks(uint32_t mute, uint32_t solo) {
    br_part[BR_DRUM_PART].revision++;
    br_kit_mute_mask=mute; br_kit_solo_mask=solo;
    RING_PUBLISH(); br_part[BR_DRUM_PART].revision++;
}
static void update_all(void) {
    for (uint32_t p=0;p<BR_PARTS;p++) br_snapshot(p);
}
static void check_lanes(uint32_t audible) {
    for (uint32_t lane=0;lane<BR_DRUM_LANES;lane++) {
        assert(br_part[BR_DRUM_PART].lane_level[lane]==30u+20u*lane);
        assert(br_cached_lane[BR_DRUM_PART][lane]==
               ((audible & (1u<<lane)) ? 30u+20u*lane : 0u));
    }
}
static void set_track_solo(uint32_t mask) {
    br_solo_mask=mask;
    for (uint32_t p=0;p<BR_PARTS;p++) {
        br_part[p].revision++; RING_PUBLISH(); br_part[p].revision++;
    }
}
static uint64_t render_hit(uint32_t preset, uint32_t lane, uint32_t mode) {
    uint64_t energy=0;
    br_app_init(); fm1_ms=2000u;
    br_load_preset(BR_DRUM_PART,preset);
    br_part[BR_DRUM_PART].step[0]=1u<<lane;
    br_seq_kit_velocity[0][lane]=120u;
    br_seq_kit_length[0][lane]=0u;
    br_seq_kit_condition[0][lane]=BR_STEP_COND_ALWAYS;
    br_seq_kit_note[0][lane]=(uint8_t)br_drum_voice_id(br_part[BR_DRUM_PART].sound.drum_voice_mask,lane);
    br_seq_kit_pitch[0][lane]=BR_STEP_PITCH_DEFAULT;
    br_seq_kit_micro[0][lane]=0; br_seq_kit_retrig[0][lane]=0;
    publish_masks(mode==1u ? 1u<<lane : 0u,
                  mode==2u ? 1u<<((lane+1u)%BR_DRUM_LANES) : 0u);
    br_playing=1u;
    for (uint32_t b=0;b<8u;b++) {
        int32_t audio[BR_BLOCK*2u]={0};
        assert(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
        for (uint32_t i=0;i<BR_BLOCK*2u;i++)
            energy += audio[i]<0 ? (uint64_t)(-(int64_t)audio[i]) : (uint64_t)audio[i];
    }
    return energy;
}
static void tick(void) { ++fm1_ms; br_app_tick(); }
static void tap_button(uint32_t button) {
    fm1_in.buttons |= 1u<<button; host_pressed |= 1u<<button; tick();
    fm1_in.buttons &= ~(1u<<button); host_released |= 1u<<button; tick();
}
int main(void) {
    br_app_init(); fm1_ms=2000u;
    br_part[BR_DRUM_PART].revision++;
    for (uint32_t lane=0;lane<BR_DRUM_LANES;lane++)
        br_part[BR_DRUM_PART].lane_level[lane]=30u+20u*lane;
    RING_PUBLISH(); br_part[BR_DRUM_PART].revision++;
    set_track_solo(0u); publish_masks(0u,0u); update_all(); check_lanes(15u);
    publish_masks(5u,0u); update_all(); check_lanes(10u);
    publish_masks(0u,6u); update_all(); check_lanes(6u);
    assert(!br_cached_sound[0].mute && !br_cached_sound[1].mute && !br_cached_sound[2].mute);
    publish_masks(2u,6u); update_all(); check_lanes(4u);
    publish_masks(0u,0u); update_all(); check_lanes(15u);
    set_track_solo(1u); update_all(); assert(br_cached_sound[BR_DRUM_PART].mute);
    publish_masks(0u,2u); update_all(); check_lanes(2u);
    assert(br_cached_sound[BR_DRUM_PART].mute && !br_cached_sound[0].mute);
    assert(br_solo_mask==1u);
    set_track_solo(1u<<BR_DRUM_PART); update_all();
    assert(!br_cached_sound[BR_DRUM_PART].mute && br_cached_sound[0].mute);
    check_lanes(2u);
    /* Real knobs select a lane; OCT buttons operate on it, keyboard remains inert. */
    br_app_init(); fm1_ms=3000u; br_ui.part=BR_DRUM_PART;
    br_ui.page=BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE]=3u; tick();
    host_encoder[ENC_KNOB1+2u]++; tick(); assert(br_ui.lane==2u);
    uint32_t level=br_part[BR_DRUM_PART].lane_level[2];
    tap_button(BTN_OCT_DOWN); assert(br_kit_mute_mask==4u);
    tap_button(BTN_OCT_UP); assert(br_kit_solo_mask==4u && !br_solo_mask);
    assert(br_part[BR_DRUM_PART].lane_level[2]==level);
    fm1_in.notes|=1u; host_note_pressed|=1u; tick();
    assert(!br_ui.held_note[0] && br_ui.lane==2u);
    fm1_in.notes&=~1u; tick();
    tap_button(BTN_OCT_DOWN); tap_button(BTN_OCT_UP);
    assert(!br_kit_mute_mask && !br_kit_solo_mask);

    /* Actual fresh-hit audio through all four kit engines and every lane. */
    static const uint32_t kits[]={BR_KIT_BRAIDS,BR_KIT_PLAITS,BR_KIT_808,BR_KIT_909};
    for (uint32_t k=0;k<4u;k++) for (uint32_t lane=0;lane<BR_DRUM_LANES;lane++) {
        uint64_t active=render_hit(kits[k],lane,0u);
        uint64_t muted=render_hit(kits[k],lane,1u);
        uint64_t excluded=render_hit(kits[k],lane,2u);
        assert(active>0u && muted==0u && excluded==0u);
    }
    puts("PASS: kit mixer controls, audio, lane isolation and level preservation");
    return 0;
}
