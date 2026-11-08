/* Punch-in FX through the real app: keys, page switch, persistence, trem. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>

static void setup(void)
{
    br_app_init(); fm1_ms = 1000u;
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u;
    br_load_preset(0u, BR_FIRST_PLAITS);
}

static double rms_block(void)
{
    int32_t audio[BR_BLOCK * 2u] = {0};
    double sum = 0.0;
    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    for (uint32_t i = 0u; i < BR_BLOCK * 2u; ++i) sum += (double)audio[i] * audio[i];
    return sqrt(sum / (BR_BLOCK * 2u));
}

int main(void)
{
    /* Keys and pages. */
    setup();
    br_key_down(25u, 1);
    assert(br_ui.page == BR_PAGE_PUNCH && br_punch_effect == BR_PUNCH_NONE && !br_ui.held_note[25]);
    br_key_down(8u, 1);  assert(br_punch_effect == BR_PUNCH_ECHO && !br_ui.held_note[8]);
    br_key_up(8u);       assert(br_punch_effect == BR_PUNCH_NONE);
    br_key_down(13u, 1); br_key_down(15u, 1); br_key_up(13u);
    assert(br_punch_effect == BR_PUNCH_FILTER);
    br_key_down(20u, 1); assert(br_punch_effect == BR_PUNCH_FILTER);   /* other group ignored */
    br_key_up(20u); br_key_up(15u);
    assert(br_punch_effect == BR_PUNCH_NONE);
    br_key_down(1u, 1);
    br_key_up(25u);
    assert(br_ui.page == BR_PAGE_HOME && br_punch_effect == BR_PUNCH_NONE);
    br_key_up(1u); assert(!br_ui.held_note[1]);                         /* no stray note-off state */
    /* Outside the mode a black key still plays its note. */
    br_key_down(8u, 1);
    assert(br_ui.held_note[8] && br_punch_effect == BR_PUNCH_NONE);
    br_key_up(8u);
    /* On other pages the shift key is an ordinary key with its old role. */
    setup(); br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0u;
    br_key_down(25u, 1);
    assert(br_ui.page == BR_PAGE_SEQ && !br_punch_ks.shift);
    br_key_up(25u);

    /* Playback hint, held-mode contrast, and a left-to-right active-group chase. */
    setup(); br_playing=1u; br_leds();
    assert(host_led_level_state[KEY_LED(25)]==FM1_LED_MID);
    assert(host_led_level_state[KEY_LED(1)]==FM1_LED_DIM);
    br_playing=0u; br_leds(); assert(host_led_level_state[KEY_LED(25)]==FM1_LED_DIM);
    br_key_down(25u,1); br_leds();
    for(uint32_t k=0;k<27u;++k)
        assert(host_led_level_state[KEY_LED(k)]==(br_black_key(k)?FM1_LED_BRIGHT:FM1_LED_DIM));
    br_key_down(1u,1); br_leds();
    assert(host_led_level_state[KEY_LED(1)]==FM1_LED_BRIGHT);
    assert(host_led_level_state[KEY_LED(3)]==FM1_LED_MID);
    assert(host_led_level_state[KEY_LED(5)]==FM1_LED_DIM);
    fm1_ms+=100u; br_leds();
    assert(host_led_level_state[KEY_LED(1)]==FM1_LED_DIM);
    assert(host_led_level_state[KEY_LED(3)]==FM1_LED_BRIGHT);
    assert(host_led_level_state[KEY_LED(5)]==FM1_LED_MID);
    fm1_ms+=100u; br_leds(); assert(host_led_level_state[KEY_LED(5)]==FM1_LED_BRIGHT);
    br_key_down(8u,1); br_leds(); assert(host_led_level_state[KEY_LED(8)]==FM1_LED_BRIGHT);
    br_key_up(8u); br_key_up(1u); br_leds();
    assert(host_led_level_state[KEY_LED(1)]==FM1_LED_BRIGHT);
    br_key_down(8u,1); br_leds();
    assert(host_led_level_state[KEY_LED(8)]==FM1_LED_BRIGHT && host_led_level_state[KEY_LED(10)]==FM1_LED_MID);
    fm1_ms+=100u; br_leds();
    assert(host_led_level_state[KEY_LED(8)]==FM1_LED_DIM && host_led_level_state[KEY_LED(10)]==FM1_LED_BRIGHT);
    br_leds_mode(1u);
    for(uint32_t k=0;k<27u;++k) assert(host_led_level_state[KEY_LED(k)]==FM1_LED_OFF);
    br_key_up(8u); br_key_up(25u); br_leds();
    assert(host_led_level_state[KEY_LED(25)]==FM1_LED_DIM && host_led_level_state[KEY_LED(0)]==FM1_LED_BRIGHT);

    /* Active effect owns the parameter highlight, including LOOP from page two. */
    setup(); br_key_down(25u,1); br_draw();
    for(uint32_t group=BR_PUNCH_LOOP;group<=BR_PUNCH_TREM;++group) {
        static const uint8_t keys[5]={0,1,8,13,20};
        static uint16_t badge[240*20];
        br_ui.screen[BR_PAGE_PUNCH]=1u;
        br_key_down(keys[group],1); br_draw();
        assert(br_active_knob()==group-1u);
        assert(br_ui.screen[BR_PAGE_PUNCH]==(group==BR_PUNCH_LOOP?0u:1u));
        memcpy(badge,fb+46u*240u,sizeof badge);
        br_key_up(keys[group]); br_draw();
        assert(br_active_knob()==4u);
        assert(memcmp(badge,fb+46u*240u,sizeof badge));
    }
    br_key_up(25u);

    /* Settings page edits the saved settings, and OCT+ opens its second screen. */
    setup(); br_key_down(25u, 1);
    br_control_turn(0u, 1); br_control_turn(1u, 1); br_control_turn(2u, 3); br_control_turn(3u, 1);
    assert(br_punch_cfg.loop_len == 3u && br_punch_cfg.echo_time == 3u &&
           br_punch_cfg.filter_shape == 3u && br_punch_cfg.trem_rate == 3u);
    br_buttons(1u << BTN_OCT_UP);
    assert(br_ui.screen[BR_PAGE_PUNCH] == 1u);
    br_control_turn(1u, 5); br_control_turn(2u, -4); br_control_turn(3u, 9);
    assert(br_punch_cfg.echo_fb == 17u && br_punch_cfg.filter_amount == 16u && br_punch_cfg.trem_depth == 31u);
    br_key_up(25u);

    /* LOOP edits reach the sequencer while held, including a shorter slice. */
    setup(); br_part[0].length = 16u; br_part[0].division = 1u;
    br_playing = 1u; br_key_down(25u, 1); br_key_down(1u, 1);
    assert(br_punch_loop_len == 2u && br_punch_cfg.loop_len == 2u);
    br_loop_on[0] = 0u; br_step_pos[0] = 0u; br_seq_next[0] = 1u;
    br_seq_step(0u); assert(br_step_pos[0] == 1u);
    br_seq_step(0u); assert(br_step_pos[0] == 2u);
    /* The next index is 3 in the old four-step slice. */
    br_control_turn(0u, 1);
    assert(br_control(0u).value == 3 && br_punch_cfg.loop_len == 3u);
    assert(br_punch_loop_len == 3u);
    for (uint32_t i = 0u; i < 8u; ++i) {
        br_seq_step(0u);
        assert(br_step_pos[0] == (i + 1u) % 2u);
    }
    br_buttons(1u << BTN_OCT_UP);
    assert(br_ui.screen[BR_PAGE_PUNCH] == 1u && br_active_knob() == 4u);
    assert(br_control(0u).label[0] == '\0');
    assert(!strcmp(br_oct_hint(), "OCT-/+: PAGES"));
    br_key_up(1u);
    br_key_down(1u, 1);
    assert(br_ui.screen[BR_PAGE_PUNCH] == 0u);
    br_key_up(1u); br_key_up(25u);

    /* Session round trip, and a session from before punch-in loads with defaults. */
    {
        br_session_t saved; br_punch_cfg_t keep = br_punch_cfg;
        assert(br_capture(&saved) && br_session_v16_valid(&saved));
        assert(saved.v3.part[BR_DRUM_PART].lane_level[BR_DRUM_LANES] == BR_PUNCH_SAVE_MARK);
        { static const br_punch_cfg_t d = BR_PUNCH_CFG_DEFAULTS; br_punch_cfg = d; }
        assert(br_restore(&saved) && !memcmp(&keep, &br_punch_cfg, sizeof keep));
        saved.v3.part[BR_DRUM_PART].lane_level[BR_DRUM_LANES] = 0x77u;
        assert(!br_session_v16_valid(&saved));
        { static const br_punch_cfg_t d = BR_PUNCH_CFG_DEFAULTS; br_punch_cfg = d; }
        assert(br_capture(&saved));
        for (uint32_t i = BR_DRUM_LANES; i < 8u; ++i) assert(!saved.v3.part[BR_DRUM_PART].lane_level[i]);
    }

    /* Trem chops the output while held and stops when released. */
    setup(); br_punch_cfg.trem_rate = 0u; br_punch_cfg.trem_depth = 31u;
    br_engine_note_on(0u, 60u, 100u);
    for (uint32_t i = 0u; i < 20u; ++i) (void)rms_block();
    double loud = 0.0, quiet = 1e18, level;
    for (uint32_t i = 0u; i < 20u; ++i) { level = rms_block(); if (level > loud) loud = level; }
    br_key_down(25u, 1); br_key_down(20u, 1);
    assert(br_punch_effect == BR_PUNCH_TREM);
    for (uint32_t i = 0u; i < 100u; ++i) { level = rms_block(); if (level < quiet) quiet = level; }
    assert(loud > 100.0 && quiet < 0.2 * loud);
    br_key_up(20u); br_key_up(25u);
    for (uint32_t i = 0u; i < 60u; ++i) level = rms_block();
    assert(level > 0.5 * loud);
    /* Loop: repeat a slice of the bar while held, then resume in sync. */
    setup();
    br_part[0].length = 16u; br_part[0].division = 1u;
    for (uint32_t i = 0u; i < 16u; ++i) br_part[0].step[i] = BR_NOTE_STEP(48u + i, 100u);
    br_midi(0xFAu, 0u, 0u);
    uint32_t seen[64], count = 0u, previous = 99u, steps_total = 0u, start_pos = 0u;
    (void)steps_total;
    for (uint32_t b = 0u; b < 200u; ++b) {            /* about 9 steps of normal playback */
        (void)rms_block();
        if (br_step_pos[0] != previous) { previous = br_step_pos[0]; ++steps_total; }
    }
    start_pos = previous;
    br_punch_loop_len = 3u;                              /* 1/8 bar: two steps */
    br_punch_effect = BR_PUNCH_LOOP;
    for (uint32_t b = 0u; b < 400u; ++b) {
        (void)rms_block();
        if (br_step_pos[0] != previous) { previous = br_step_pos[0]; ++steps_total; if (count < 64u) seen[count++] = previous; }
    }
    assert(count >= 12u);
    for (uint32_t i = 1u; i < count; ++i)                /* alternates between two neighbouring steps */
        assert(seen[i] == seen[i - 1u] ? 0 : (seen[i] == seen[0] || seen[i] == (seen[0] + 1u) % 16u || seen[i] == (seen[0] + 15u) % 16u));
    for (uint32_t i = 2u; i < count; ++i) assert(seen[i] == seen[i - 2u]);
    br_punch_effect = BR_PUNCH_NONE;
    {   /* run until the position changes: it must be where the song would be */
        uint32_t resumed = 99u, guard = 0u;
        while (guard++ < 100u) { (void)rms_block(); if (br_step_pos[0] != previous) { resumed = br_step_pos[0]; ++steps_total; break; } }
        assert(resumed != 99u);
        /* Each loop step stood for one step of the song, so the song is now at
         * the step after the one in flight at the punch, plus the steps looped. */
        assert(resumed == (start_pos + 1u + count) % 16u);
    }
    printf("punch-in key, page, settings, session, trem and loop checks passed\n");
    return 0;
}
