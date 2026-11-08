/* SEL sound bank through the real app: table, browse, audition, load/undo, leave. */
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
    br_load_preset(0u, BR_FIRST_PLAITS + 3u);
    br_part[0].sound.timbre = 33u; br_part[0].sound.reverb = 9u; br_part[0].sound.pan = 20u;
    br_part[0].sound.level = 99u; br_part[0].sound.tune = 5;
    br_part[0].mod.lfo[1].depth = 7u;
}

static double rms_block(void)
{
    int32_t audio[BR_BLOCK * 2u] = {0};
    double sum = 0.0;
    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    for (uint32_t i = 0u; i < BR_BLOCK * 2u; ++i) sum += (double)audio[i] * audio[i];
    return sqrt(sum / (BR_BLOCK * 2u));
}

static int same_sound(uint32_t part, const br_bank_state_t *want)
{
    br_bank_state_t have;
    br_bank_take(&have, &br_part[part].sound, &br_part[part].mod);
    return !memcmp(&have, want, sizeof have);
}

static void tick(uint32_t buttons) { br_sel_housekeeping(buttons); }

int main(void)
{
    /* Old project scale settings do not alter pitch; sound tuning and pitch
     * modulation still work. The stored fields remain format-compatible. */
    br_cached_mod[0].scale = 1u; br_cached_mod[0].root = 4u;
    br_cached_mod[0].quantize = 1u; br_cached_mod[0].transpose = 12;
    br_cached_sound[0].tune = 2; br_pitch_mod_q7[0] = 32;
    assert(br_map_pitch_q7(0u, 60u) == 62u * 128u + 32u);
    br_pitch_mod_q7[0] = 0;
    /* The table: 64 distinct short names, valid models, and the cutoff mapping. */
    for (uint32_t i = 0u; i < BR_BANK_COUNT; ++i) {
        assert(br_bank[i].name[0] && strlen(br_bank[i].name) <= 10u);
        assert(br_bank[i].model < BR_PLAITS_MODEL_COUNT);
        assert(br_bank[i].lfo_target < BR_TARGET_COUNT && br_bank[i].env_target < BR_TARGET_COUNT);
        for (uint32_t j = 0u; j < i; ++j) assert(strcmp(br_bank[i].name, br_bank[j].name));
        br_bank_state_t s;
        br_bank_state(&br_bank[i], &s);
        assert(br_preset_allowed_on_part(0u, s.preset) && s.filter <= 64u);
        assert(s.sustain <= 127u && s.reverb <= 127u && s.delay <= 127u);
    }
    assert(!strcmp(br_bank[0].name, "REESE") && !strcmp(br_bank[63].name, "SPACE BELL"));
    { br_bank_state_t s; br_bank_state(&br_bank[1], &s);       /* SUB DEEP: F 50 */
      assert(s.filter == 25u && s.preset == BR_FIRST_PLAITS + 8u); }
    { br_bank_state_t s; br_bank_state(&br_bank[7], &s);       /* 808 BOOM: F 127 is open */
      assert(s.filter == 64u); }
    assert(br_sel_wrap(0u, -1) == 63u && br_sel_wrap(63u, 1) == 0u && br_sel_wrap(5u, 200) == (5u + 200u) % 64u);

    /* SEL opens the browser on a synth track; nothing is applied until used. */
    setup();
    br_bank_state_t before; br_bank_take(&before, &br_part[0].sound, &br_part[0].mod);
    br_buttons(1u << BTN_SEL);
    assert(br_ui.page == BR_PAGE_SCALE && br_sel_browse() && !br_sel.active);
    assert(same_sound(0u, &before));
    assert(!strcmp(br_header_title(), "SOUNDS"));
    /* The encoder browses, wraps, and puts the entry on the track. */
    br_sel_move(-1);
    assert(br_sel.cursor[0] == 63u && br_sel.active && br_sel.shown);
    { br_bank_state_t want; br_bank_state(&br_bank[63], &want); assert(same_sound(0u, &want)); }
    assert(br_part[0].sound.pan == 20u && br_part[0].sound.level == 99u && br_part[0].sound.tune == 5);
    br_sel_move(65);
    assert(br_sel.cursor[0] == 0u);
    { br_bank_state_t want; br_bank_state(&br_bank[0], &want); assert(same_sound(0u, &want)); }
    assert(br_part[0].mod.lfo[0].target == BR_TARGET_TIMBRE && br_part[0].mod.lfo[1].depth == 0u);
    /* REESE values match the percentages and LFO frequency shown on the unit. */
    assert(br_part[0].sound.preset == BR_FIRST_PLAITS + 8u);
    assert(br_part[0].sound.timbre == 84u && br_part[0].sound.plaits_harmonics == 30u);
    assert(br_part[0].sound.color == 105u && br_part[0].sound.plaits_aux == 8u);
    assert(br_part[0].mod.lfo[0].wave == BR_WAVE_SINE && br_part[0].mod.lfo[0].rate == 2u);
    assert(br_lfo_rate_millihz(2u) == 344u && br_part[0].mod.lfo[0].depth == 4u);
    for (uint32_t i=1u;i<BR_BANK_COUNT;++i) assert(br_bank[i].aux == 64u);
    /* Five visible rows scroll only when the cursor crosses an edge. */
    assert(BR_SEL_ROWS == 5u && br_sel.top[0] == 0u);
    for (uint32_t i = 1u; i <= 4u; ++i) { br_sel_move(1); assert(br_sel.cursor[0] == i && br_sel.top[0] == 0u); }
    br_sel_move(1); assert(br_sel.cursor[0] == 5u && br_sel.top[0] == 1u);
    br_sel_move(1); assert(br_sel.cursor[0] == 6u && br_sel.top[0] == 2u);
    br_sel_move(-3); assert(br_sel.cursor[0] == 3u && br_sel.top[0] == 2u);
    br_sel_move(-1); assert(br_sel.cursor[0] == 2u && br_sel.top[0] == 2u);
    br_sel_move(-1); assert(br_sel.cursor[0] == 1u && br_sel.top[0] == 1u);
    br_sel_move(-1); br_sel_move(-1);
    assert(br_sel.cursor[0] == 63u && br_sel.top[0] == 59u);
    br_sel_move(1); assert(br_sel.cursor[0] == 0u && br_sel.top[0] == 0u);
    for (uint32_t c = 0u; c < 64u; ++c)
        for (uint32_t t = 0u; t < 60u; ++t) {
            uint32_t n = br_sel_top(t, c);
            assert(n <= 59u && c >= n && c < n + 5u);
        }
    /* The screen draws at the top, the middle and the bottom. */
    for (uint32_t at = 0u; at < 64u; at += 21u) {
        br_sel.cursor[0] = (uint8_t)at; br_sel.top[0] = (uint8_t)br_sel_top(0u, at);
        br_ui.redraw = 1u; br_draw();
    }
    br_sel.cursor[0] = 0u; br_sel.top[0] = 0u; br_ui.redraw = 1u;
    /* Knobs do not edit the hidden scale settings from the browser. */
    { uint32_t scale = br_part[0].mod.scale; br_control_turn(1u, 1); br_control_turn(2u, 1); br_control_turn(3u, 1);
      assert(br_part[0].mod.scale == scale); }
    /* Knob 1 moves the cursor like the preset knob does. */
    { uint32_t at = br_sel.cursor[0]; br_control_turn(0u, 1); assert(br_sel.cursor[0] == br_sel_wrap(at, 1)); br_control_turn(0u, -1); assert(br_sel.cursor[0] == at); }

    /* Leaving without loading puts the original back, exactly. */
    br_buttons(1u << BTN_HOME); tick(0u);
    assert(br_ui.page == BR_PAGE_HOME && !br_sel.active && same_sound(0u, &before));

    /* OCT- and OCT+ move the keyboard octave as everywhere else; they never load. SEL again keeps the sound. */
    br_buttons(1u << BTN_SEL); br_sel_move(9);
    br_bank_state_t nine; br_bank_state(&br_bank[9], &nine);
    br_ui.octave = 0;
    br_buttons(1u << BTN_OCT_UP); assert(br_ui.octave == 1 && !br_sel.loaded && same_sound(0u, &nine));
    br_buttons(1u << BTN_OCT_DOWN); br_buttons(1u << BTN_OCT_DOWN); assert(br_ui.octave == -1 && !br_sel.undo_ready);
    br_ui.octave = 0;
    br_buttons(1u << BTN_SEL);
    assert(br_sel_browse() && br_sel.loaded && br_sel.undo_ready && same_sound(0u, &nine));
    br_sel_move(1);                                                     /* browse on, then leave: back to the load */
    assert(!br_sel.loaded);
    br_buttons(1u << BTN_ENV); tick(0u);
    assert(!br_sel.active && same_sound(0u, &nine));

    /* SEL loads the highlighted preset; once loaded, SEL returns to SOUND. */
    br_buttons(1u << BTN_SEL); assert(br_sel_browse() && !br_sel.shown);
    br_buttons(1u << BTN_SEL); assert(br_sel_browse() && br_sel.shown && br_sel.loaded);
    br_buttons(1u << BTN_SEL); tick(0u); assert(!br_sel_browse() && br_ui.page == BR_PAGE_HOME);
    br_buttons(1u << BTN_SEL); assert(br_sel_browse());
    br_sel_move(3); br_buttons(1u << BTN_SEL); assert(br_sel_browse() && br_sel.loaded);
    br_bank_state_t kept; br_bank_state(&br_bank[br_sel.cursor[0]], &kept);
    br_buttons(1u << BTN_SEL); tick(0u); assert(br_ui.page == BR_PAGE_HOME);
    br_buttons(1u << BTN_SEL);
    assert(same_sound(0u, &kept));
    br_buttons(1u << BTN_HOME); tick(0u);
    assert(same_sound(0u, &kept));
    /* Undo (not on a button now) still returns to the sound before the last load. */
    br_buttons(1u << BTN_SEL); br_sel_move(1); br_buttons(1u << BTN_SEL);
    assert(br_sel.undo_ready && br_sel_undo(&br_sel, &br_part[0].sound, &br_part[0].mod));
    assert(same_sound(0u, &kept));
    br_buttons(1u << BTN_HOME); tick(0u);
    br_load_preset(0u, BR_FIRST_PLAITS + 3u);
    br_bank_take(&before, &br_part[0].sound, &br_part[0].mod);

    /* Changing track or pressing SAVE also ends the visit and restores. */
    setup(); br_bank_take(&before, &br_part[0].sound, &br_part[0].mod);
    br_buttons(1u << BTN_SEL); br_sel_move(20);
    br_ui.part = 1u; tick(0u);
    assert(!br_sel.active && same_sound(0u, &before));
    br_ui.part = 0u; br_sel_move(4); tick(0u);
    assert(br_sel.active);
    tick(1u << BTN_SAVE);
    assert(!br_sel.active && same_sound(0u, &before));

    /* SEL keeps the audition; the next SEL exits the browser to SOUND. */
    br_buttons(1u << BTN_HOME); tick(0u);
    br_buttons(1u << BTN_SEL); br_sel_move(2);
    br_bank_state_t two; br_bank_state(&br_bank[br_sel.cursor[0]], &two);
    br_buttons(1u << BTN_SEL); assert(br_sel_browse() && br_sel.loaded);
    br_buttons(1u << BTN_SEL);
    assert(br_ui.page == BR_PAGE_HOME && !br_sel_browse());
    tick(0u); assert(!br_sel.active && same_sound(0u, &two));
    br_buttons(1u << BTN_SEL); assert(br_sel_browse());
    br_buttons(1u << BTN_FX); tick(0u); assert(same_sound(0u, &two));

    /* The kit has its own twelve-entry browser and independent cursor. */
    setup(); br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_HOME;
    br_kit_bank_state_t original, got, kept_kit;
    br_kit_bank_take(&original, &br_part[BR_DRUM_PART].sound);
    uint32_t mask = br_part[BR_DRUM_PART].sound.drum_voice_mask;
    uint32_t level_before = br_part[BR_DRUM_PART].lane_level[0];
    uint8_t note_before = br_seq_kit_note[0][0];
    br_buttons(1u << BTN_SEL);
    assert(br_sel_browse() && !br_sel.active && !strcmp(br_header_title(), "DRUM KITS"));
    br_sel_move(-1); assert(br_sel.cursor[3] == 11u && br_sel.top[3] == 7u);
    br_draw(); br_sel_move(1); assert(br_sel.cursor[3] == 0u && br_sel.top[3] == 0u);
    for (uint32_t i = 0; i < BR_KIT_BANK_COUNT; ++i) {
        assert(br_preset_allowed_on_part(3u, br_kit_bank[i].preset));
        if (i % 3u == 0u) {
            br_sound_t factory; br_preset_sound(br_kit_bank[i].preset, &factory);
            assert(br_kit_bank[i].timbre == factory.timbre && br_kit_bank[i].color == factory.color);
            assert(br_kit_bank[i].harmonics == factory.plaits_harmonics && br_kit_bank[i].decay == factory.decay);
            assert(br_kit_bank[i].snap == factory.drum_snap && br_kit_bank[i].tune == factory.tune);
        }
        assert(strlen(br_kit_bank[i].name) <= 10u);
        for (uint32_t j = 0; j < i; ++j) assert(strcmp(br_kit_bank[i].name, br_kit_bank[j].name));
        br_sel.cursor[3] = (uint8_t)i; br_sel_show_now();
        assert(br_part[3].sound.preset == br_kit_bank[i].preset);
        assert(br_part[3].sound.timbre == br_kit_bank[i].timbre);
        assert(br_part[3].sound.tune == br_kit_bank[i].tune);
        assert(br_part[3].sound.drum_voice_mask == mask && br_part[3].lane_level[0] == level_before);
        assert(br_seq_kit_note[0][0] == note_before);
    }
    tick(1u << BTN_SAVE);
    br_kit_bank_take(&got, &br_part[3].sound); assert(!memcmp(&got, &original, sizeof got));
    br_sel_move(3); br_buttons(1u << BTN_SEL); assert(br_sel.loaded);
    br_kit_bank_take(&kept_kit, &br_part[3].sound);
    br_sel_move(1); br_buttons(1u << BTN_HOME); tick(0u);
    br_kit_bank_take(&got, &br_part[3].sound); assert(!memcmp(&got, &kept_kit, sizeof got));
    { br_session_t saved; assert(br_capture(&saved));
      br_part[3].sound.timbre = 1u; assert(br_restore(&saved));
      br_kit_bank_take(&got, &br_part[3].sound); assert(!memcmp(&got, &kept_kit, sizeof got)); }
    br_ui.part = 0u; br_ui.page = BR_PAGE_HOME;

    /* Keys audition the highlighted sound: applied on first press, and it sounds. */
    setup();
    br_buttons(1u << BTN_SEL);
    br_key_down(5u, 1);
    assert(br_sel.active && br_sel.shown && br_ui.held_note[5]);
    { br_bank_state_t want; br_bank_state(&br_bank[br_sel.cursor[0]], &want); assert(same_sound(0u, &want)); }
    double level = 0.0, x;
    for (uint32_t i = 0u; i < 40u; ++i) { x = rms_block(); if (x > level) level = x; }
    assert(level > 50.0);
    br_key_up(5u);
    br_buttons(1u << BTN_HOME); tick(0u);

    /* The loaded sound survives a session round trip like any other. */
    setup(); br_buttons(1u << BTN_SEL); br_sel_move(11);
    br_buttons(1u << BTN_SEL);
    br_buttons(1u << BTN_HOME); tick(0u);
    {
        br_session_t saved; br_bank_state_t kept, now;
        br_bank_take(&kept, &br_part[0].sound, &br_part[0].mod);
        assert(br_capture(&saved) && saved.v3.version == 16u && br_session_v16_valid(&saved));
        br_load_preset(0u, BR_FIRST_PLAITS); br_part[0].sound.timbre = 1u;
        assert(br_restore(&saved));
        br_bank_take(&now, &br_part[0].sound, &br_part[0].mod);
        assert(!memcmp(&kept, &now, sizeof now));
    }
        /* The keyboard: both rows swell together, and the black (top) keys peak one step below full. */
    {
        static const uint8_t white[4] = {FM1_LED_DIM, FM1_LED_MID, FM1_LED_BRIGHT, FM1_LED_MID};
        static const uint8_t black[4] = {FM1_LED_DIM, FM1_LED_DIM, FM1_LED_MID, FM1_LED_DIM};
        setup(); br_buttons(1u << BTN_SEL);
        for (uint32_t step = 0u; step < 4u; ++step) {
            fm1_ms = 1000u + step * 450u; br_leds();
            for (uint32_t k = 0u; k < 27u; ++k)
                assert(host_led_level_state[KEY_LED(k)] == (br_black_key(k) ? black : white)[(fm1_ms / 450u) & 3u]);
        }
        /* and the octave buttons are lit by the octave, as on the sound page */
        br_ui.octave = 1; br_leds();
        assert(host_led_level_state[BTN_OCT_UP] == FM1_LED_BRIGHT);
    }
    /* Each kit entry must produce its own non-silent audio, not just a new label. */
    {
        uint64_t hashes[BR_KIT_BANK_COUNT];
        for (uint32_t k = 0u; k < BR_KIT_BANK_COUNT; ++k) {
            int32_t audio[BR_BLOCK * 2u]; uint64_t hash = 1469598103934665603ULL;
            uint32_t nonzero = 0u;
            setup(); br_ui.part = BR_DRUM_PART;
            br_kit_bank_apply(&br_kit_bank[k], &br_part[BR_DRUM_PART].sound);
            br_part[BR_DRUM_PART].revision += 2u;
            br_engine_note_on(BR_DRUM_PART, 36u, 100u);
            for (uint32_t b = 0u; b < 24u; ++b) {
                memset(audio, 0, sizeof audio);
                assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
                for (uint32_t x = 0u; x < BR_BLOCK * 2u; ++x) {
                    nonzero |= (uint32_t)audio[x];
                    hash = (hash ^ (uint32_t)audio[x]) * 1099511628211ULL;
                }
            }
            assert(nonzero); hashes[k] = hash;
            for (uint32_t j = 0; j < k; ++j) assert(hashes[k] != hashes[j]);
        }
    }
    printf("sound bank table, browse, audition, load, confirm, octave and leave checks passed\n");
    return 0;
}
