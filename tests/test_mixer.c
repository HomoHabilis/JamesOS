/* Mixer controls exercise the real UI, session and audio snapshot paths. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void tick(void) { ++fm1_ms;br_app_tick(); }
static void tap(uint32_t button) {
    fm1_in.buttons|=1u<<button;host_pressed|=1u<<button;tick();
    fm1_in.buttons&=~(1u<<button);host_released|=1u<<button;tick();
}
static void refresh_audio(void) {
    for(uint32_t p=0;p<BR_PARTS;p++){br_snapshot(p);br_build_effective(p);}
}
int main(void) {
    br_session_t saved;uint16_t strip[240*8],clock_strip[240*8],cue[240*24],frame[240*24];
    int32_t audio[2u*HALF_FRAMES];
    br_app_init();fm1_ms=2000;tap(BTN_GLO);
    assert(br_ui.page==BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]);
    assert(!strcmp(br_header_title(),"MIXER"));
    assert(host_led_level_state[BTN_OCT_DOWN]==FM1_LED_BRIGHT && host_led_level_state[BTN_OCT_UP]==FM1_LED_BRIGHT);
    tap(BTN_OCT_DOWN);assert(br_part[0].sound.mute==1u);
    br_ui.part=1;tap(BTN_OCT_UP);refresh_audio();
    assert(br_part[0].sound.mute==1u && br_effective_sound[0].mute==1u && !br_effective_sound[1].mute);
    tap(BTN_HOME);assert(br_solo_mask==2u);tap(BTN_GLO);assert(br_solo_mask==2u);
    tap(BTN_OCT_UP);refresh_audio();assert(br_part[0].sound.mute==1u && br_effective_sound[0].mute==1u && !br_effective_sound[1].mute);
    br_ui.part=0;
    tap(BTN_OCT_UP);assert(br_solo_mask==1u);refresh_audio();
    assert(br_effective_sound[0].mute==1u); /* Explicit mute still wins. */
    for(uint32_t p=1;p<BR_PARTS;p++)assert(br_effective_sound[p].mute==1u);
    tap(BTN_OCT_DOWN);refresh_audio();assert(br_effective_sound[0].mute==0u);
    br_ui.part=1;tap(BTN_OCT_UP);assert(br_solo_mask==3u);refresh_audio();
    assert(!br_effective_sound[0].mute && !br_effective_sound[1].mute && br_effective_sound[2].mute);
    tap(BTN_OCT_UP);br_ui.part=0;tap(BTN_OCT_UP);refresh_audio();assert(br_solo_mask==0u);
    for(uint32_t p=0;p<BR_PARTS;p++)assert(!br_effective_sound[p].mute);
    tap(BTN_OCT_DOWN);tap(BTN_OCT_UP);assert(br_capture(&saved));
    assert(br_restore(&saved) && br_solo_mask==0u && br_part[0].sound.mute==1u);
    br_ui.redraw=1;br_draw();memcpy(strip,fb+240*24,sizeof strip);
    br_part[0].step[0]^=BR_STEP_ON;br_ui.cursor=3;br_ui.redraw=0;br_draw();
    assert(!memcmp(strip,fb+240*24,sizeof strip));
    br_playing=1;br_global_beat=1;br_transport_phase=0;br_draw();memcpy(clock_strip,fb+240*24,sizeof clock_strip);
    br_global_beat=2;br_transport_phase=64;br_draw();assert(memcmp(clock_strip,fb+240*24,sizeof clock_strip));
    br_playing=0;memset(audio,0,sizeof audio);app_audio(audio,HALF_FRAMES);
    br_playing=1;
    for(uint32_t i=0;i<FS*60u/(br_bpm*HALF_FRAMES)+3u;i++) {
        memset(audio,0,sizeof audio);app_audio(audio,HALF_FRAMES);
    }
    assert(br_global_beat==1u);
    br_playing=0;br_ui.message[0]=0;br_ui.message_until=0;
    br_draw_cue_bar(1,1);memcpy(cue,fb+240*216,sizeof cue);
    br_message("PROJECT SAVED");fm1_ms+=80;br_draw_cue_bar(1,1);
    memcpy(frame,fb+240*216,sizeof frame);assert(memcmp(frame,cue,sizeof cue));
    fm1_ms+=240;br_draw_cue_bar(1,1);assert(memcmp(frame,fb+240*216,sizeof frame));
    memcpy(frame,fb+240*216,sizeof frame);
    br_bpm=40;fm1_ms=br_ui.message_until-240u;br_draw_cue_bar(1,1);
    assert(memcmp(frame,fb+240*216,sizeof frame)); /* Hold duration was fixed when the message began. */
    fm1_ms=br_ui.message_until;br_draw_cue_bar(1,1);assert(!memcmp(cue,fb+240*216,sizeof cue));
    br_ui.screen[BR_PAGE_MIX]=2u;
    assert(!strcmp(br_control(0).label,"DRUM SNAP"));
    assert(!strcmp(br_control(1).label,"BPM"));
    assert(!strcmp(br_control(2).label,"SWING"));
    assert(!strcmp(br_control(3).label,"MONITOR"));
    uint32_t mute_before=br_part[br_ui.part].sound.mute;
    uint32_t target_before=br_master.select_target;
    br_control_turn(0,1);
    assert(br_master.select_target==target_before);
    br_control_turn(3,1);
    assert(br_part[br_ui.part].sound.mute==mute_before);
    /* Old project assignments are accepted but SELECT always edits the filter. */
    assert(br_capture(&saved));
    saved.select_target=BR_SELECT_TEMPO;
    assert(br_restore(&saved));
    assert(br_master.select_target==BR_SELECT_MASTER_FILTER);
    for (uint32_t target=0;target<BR_SELECT_COUNT;++target) {
        uint32_t bpm=br_bpm, heat=br_master.fx.heat_type, level=br_part[br_ui.part].sound.level;
        br_master.select_target=target;
        br_master.fx.filter=64u;
        br_select_turn(1);
        assert(br_master.fx.filter==65u && br_bpm==bpm && br_master.fx.heat_type==heat);
        assert(br_part[br_ui.part].sound.level==level);
    }
    /* Pan marker reaches left, center and right without changing track colors. */
    {
        const uint32_t value[3]={0u,64u,127u}, position[3]={12u,30u,48u};
        for (uint32_t i=0;i<3u;++i) {
            cv_begin(60,60,BR_BACKGROUND);cv_oy=0;bw_track_pan(0,0,60,value[i],0);
            assert(cv_px[30u*cv_w+position[i]]==swap16(C_WHITE));
        }
    }
    puts("PASS: mixer mute, multiple solos, saved mute, LEDs and global clock strip");return 0;
}
