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
    br_part[0].sound.drive = 31u;
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

/* To HOME from wherever. On the SMK-37 the HOME button (KNOB BANK) is "next category" on the synth
 * SOUNDS list, so there the list is left by a page button first. */
static void leave_home(void)
{
#if BR_PRODUCT_SMK37
    if (br_sel_browse() && br_ui.part != BR_DRUM_PART) { br_buttons(1u << BTN_ENV); tick(0u); }
#endif
    br_buttons(1u << BTN_HOME); tick(0u);
}

int main(void)
{
    assert(host_init_resources());
    /* Sound tuning and pitch modulation work (the old scale fields are gone). */
    br_cached_sound[0].tune = 2; br_pitch_mod_q7[0] = 32;
    assert(br_map_pitch_q7(0u, 60u) == 62u * 128u + 32u);
    br_pitch_mod_q7[0] = 0;
    /* The table: 128 distinct short names, valid models, and the cutoff mapping. */
    for (uint32_t i = 0u; i < BR_BANK_COUNT; ++i) {
        assert(br_bank[i].name[0] && strlen(br_bank[i].name) <= 10u);
        assert(br_bank[i].model < BR_PLAITS_MODEL_COUNT);
        assert(br_bank[i].voice_mode == 1u || br_bank[i].voice_mode == 2u);
        assert(br_bank[i].lfo_target < BR_TARGET_COUNT && br_bank[i].env_target < BR_TARGET_COUNT);
        for (uint32_t j = 0u; j < i; ++j) assert(strcmp(br_bank[i].name, br_bank[j].name));
        br_bank_state_t s;
        br_bank_state(&br_bank[i], &s);
        assert(br_preset_allowed_on_part(0u, s.preset) && s.filter <= 64u);
        assert(s.sustain <= 127u && s.reverb <= 127u && s.delay <= 127u);
    }
    assert(!strcmp(br_bank[0].name, "LADDER SEQ") && !strcmp(br_bank[127].name, "ENDLS LITE"));
    assert(br_bank[0].voice_mode == 1u && br_bank[23].voice_mode == 2u);
    assert(!strcmp(br_bank_category(31u), "FAVORITES") && !strcmp(br_bank_category(32u), "BASS"));
    assert(br_sel_wrap(0u, -1) == 127u && br_sel_wrap(127u, 1) == 0u && br_sel_wrap(5u, 200) == (5u + 200u) % 128u);
    { char line[16]; br_ui.part = 0u; br_sel_row_text(line, 99u); assert(!strncmp(line, "100 ", 4));
      br_sel_row_text(line, 127u); assert(!strncmp(line, "128 ", 4)); }

    /* The Presets encoder changes only the model, preserving the sound and modulation. */
    setup();
    br_part[0].sound.pan = 20u; br_part[0].sound.level = 99u; br_part[0].sound.tune = 5;
    for (uint32_t model = 0u; model < BR_PLAITS_MODEL_COUNT; ++model) {
        br_sound_t expected;
        br_mod_config_t expected_mod;
        br_part[0].sound.preset = BR_FIRST_PLAITS + (model + BR_PLAITS_MODEL_COUNT - 1u) % BR_PLAITS_MODEL_COUNT;
        br_part[0].sound.timbre = 1u; br_part[0].sound.plaits_aux = 127u;
        br_part[0].sound.filter = 0u; br_part[0].sound.reverb = 80u;
        br_part[0].mod.lfo[0].depth = 90u;
        expected = br_part[0].sound; expected.preset = BR_FIRST_PLAITS + model;
        expected_mod = br_part[0].mod;
        br_preset_encoder_turn(1);
        assert(!memcmp((const void *)&br_part[0].sound, &expected, sizeof expected));
        assert(!memcmp((const void *)&br_part[0].mod, &expected_mod, sizeof expected_mod));
    }

    /* SEL opens the browser on a synth track; nothing is applied until used. */
    setup();
    br_bank_state_t before; br_bank_take(&before, &br_part[0].sound, &br_part[0].mod);
    br_buttons(1u << BTN_SEL);
    assert(br_ui.page == BR_PAGE_SCALE && br_sel_browse() && !br_sel.active);
    assert(same_sound(0u, &before));
    assert(!strcmp(br_header_title(), "SOUNDS"));
    /* The encoder browses, wraps, and puts the entry on the track. */
    br_sel_move(-1);
    assert(br_sel.cursor[0] == 127u && br_sel.active && br_sel.shown);
    { br_bank_state_t want; br_bank_state(&br_bank[127], &want); assert(same_sound(0u, &want)); }
    assert(br_part[0].sound.pan == 20u && br_part[0].sound.level == 99u && br_part[0].sound.tune == 5);
    br_sel_move(129);
    assert(br_sel.cursor[0] == 0u);
    { br_bank_state_t want; br_bank_state(&br_bank[0], &want); assert(same_sound(0u, &want)); }
    assert(br_part[0].sound.preset == BR_FIRST_PLAITS && br_part[0].sound.drive == 0u);
    assert(br_part[0].sound.timbre == 57u && br_part[0].sound.plaits_harmonics == 64u);
    assert(br_part[0].mod.target == BR_TARGET_TIMBRE && br_part[0].mod.amount == 48);
    assert(br_part[0].mod.lfo[1].depth == 0u);
    /* Five visible rows scroll only when the cursor crosses an edge. */
    assert(BR_SEL_ROWS == 5u && br_sel.top[0] == 0u);
    for (uint32_t i = 1u; i <= 4u; ++i) { br_sel_move(1); assert(br_sel.cursor[0] == i && br_sel.top[0] == 0u); }
    br_sel_move(1); assert(br_sel.cursor[0] == 5u && br_sel.top[0] == 1u);
    br_sel_move(1); assert(br_sel.cursor[0] == 6u && br_sel.top[0] == 2u);
    br_sel_move(-3); assert(br_sel.cursor[0] == 3u && br_sel.top[0] == 2u);
    br_sel_move(-1); assert(br_sel.cursor[0] == 2u && br_sel.top[0] == 2u);
    br_sel_move(-1); assert(br_sel.cursor[0] == 1u && br_sel.top[0] == 1u);
    br_sel_move(-1); br_sel_move(-1);
    assert(br_sel.cursor[0] == 127u && br_sel.top[0] == 123u);
    br_sel_move(1); assert(br_sel.cursor[0] == 0u && br_sel.top[0] == 0u);
    for (uint32_t c = 0u; c < 128u; ++c)
        for (uint32_t t = 0u; t < 124u; ++t) {
            uint32_t n = br_sel_top(t, c);
            assert(n <= 123u && c >= n && c < n + 5u);
        }
    /* The screen draws at the top, the middle and the bottom. */
    for (uint32_t at = 0u; at < 128u; at += 21u) {
        br_sel.cursor[0] = (uint8_t)at; br_sel.top[0] = (uint8_t)br_sel_top(0u, at);
        br_ui.redraw = 1u; br_draw();
    }
    br_sel.cursor[0] = 0u; br_sel.top[0] = 0u; br_ui.redraw = 1u;
    /* Knobs 2-4 do not edit the sound from the browser. */
    { br_mod_config_t m = br_part[0].mod; br_control_turn(1u, 1); br_control_turn(2u, 1); br_control_turn(3u, 1);
      assert(!memcmp(&m, (const void *)&br_part[0].mod, sizeof m)); }
    /* Knob 1 moves the cursor like the preset knob does. */
    { uint32_t at = br_sel.cursor[0]; br_control_turn(0u, 1); assert(br_sel.cursor[0] == br_sel_wrap(at, 1)); br_control_turn(0u, -1); assert(br_sel.cursor[0] == at); }

    /* Leaving without loading puts the original back, exactly. */
    leave_home();
    assert(br_ui.page == BR_PAGE_HOME && !br_sel.active && same_sound(0u, &before));
    assert(br_part[0].sound.drive == 31u);

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
    leave_home();
    assert(same_sound(0u, &kept));
    /* Undo (not on a button now) still returns to the sound before the last load. */
    br_buttons(1u << BTN_SEL); br_sel_move(1); br_buttons(1u << BTN_SEL);
    assert(br_sel.undo_ready && br_sel_undo(&br_sel, &br_part[0].sound, &br_part[0].mod));
    assert(same_sound(0u, &kept));
    leave_home();
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
    leave_home();
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
        if (i == 0u || i == 3u) {
            br_sound_t factory; br_preset_sound(br_kit_bank[i].preset, &factory);
            assert(br_kit_bank[i].timbre == factory.timbre && br_kit_bank[i].color == factory.color);
            assert(br_kit_bank[i].harmonics == factory.plaits_harmonics && br_kit_bank[i].decay == factory.decay);
            assert(br_kit_bank[i].snap == factory.drum_snap && br_kit_bank[i].tune == factory.tune);
        }
        if (i >= 6u) assert(br_kit_bank[i].preset == BR_KIT_PLAITS);
        assert(strlen(br_kit_bank[i].name) <= 10u);
        for (uint32_t j = 0; j < i; ++j) assert(strcmp(br_kit_bank[i].name, br_kit_bank[j].name));
        br_sel.cursor[3] = (uint8_t)i; br_sel_show_now();
        assert(br_part[3].sound.preset == br_kit_bank[i].preset);
        assert(br_part[3].sound.timbre == br_kit_bank[i].timbre);
        assert(br_part[3].sound.tune == br_kit_bank[i].tune);
        assert(br_part[3].sound.drum_voice_mask == mask && br_part[3].lane_level[0] == level_before);
        assert(br_seq_kit_note[0][0] == note_before);
    }
    assert(!br_preset_allowed_on_part(BR_DRUM_PART, BR_KIT_808));
    assert(!br_preset_allowed_on_part(BR_DRUM_PART, BR_KIT_909));
    /* Old sample-kit projects migrate to the corresponding synthesized kit. */
    assert(br_session_preset_role_valid(BR_DRUM_PART, BR_KIT_808));
    assert(br_session_preset_role_valid(BR_DRUM_PART, BR_KIT_909));
    assert(br_session_preset_role_valid(BR_DRUM_PART, BR_KIT_PLAITS));
    {
        br_session_t saved;
        const uint32_t kit_preset_offset = 76u + 112u * BR_DRUM_PART;
        const uint32_t ids[] = {BR_KIT_808, BR_KIT_909};
        for (uint32_t i = 0; i < 2u; ++i) {
            setup(); br_part[0].step[0] = BR_NOTE_STEP(60u, 100u);
            assert(br_capture(&saved));
            saved.b[kit_preset_offset] = (uint8_t)ids[i];
            assert(br_restore(&saved));
            assert(br_part[0].step[0] == BR_NOTE_STEP(60u, 100u));
            assert(br_part[BR_DRUM_PART].sound.preset == BR_KIT_PLAITS);
            assert(br_part[BR_DRUM_PART].sound.timbre == br_kit_bank[i ? 9u : 6u].timbre);
            assert(strstr(br_ui.message, "NOW SYNTH") != NULL);
        }
        setup(); br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_HOME;
        br_buttons(1u << BTN_SEL);
        br_kit_bank_take(&original, &br_part[BR_DRUM_PART].sound);
    }
    tick(1u << BTN_SAVE);
    br_kit_bank_take(&got, &br_part[3].sound); assert(!memcmp(&got, &original, sizeof got));
    br_sel_move(3); br_buttons(1u << BTN_SEL); assert(br_sel.loaded);
    br_kit_bank_take(&kept_kit, &br_part[3].sound);
    br_sel_move(1); leave_home();
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
    leave_home();

    /* The loaded sound survives a session round trip like any other. */
    setup(); br_buttons(1u << BTN_SEL); br_sel_move(11);
    br_buttons(1u << BTN_SEL);
    leave_home();
    {
        br_session_t saved; br_bank_state_t kept, now;
        br_bank_take(&kept, &br_part[0].sound, &br_part[0].mod);
        assert(br_capture(&saved) && saved.b[4] == BR_V17_VERSION && br_session_valid(&saved));
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
    /* Every bank entry sounds through the real product audio path. */
    for (uint32_t i = 0u; i < BR_BANK_COUNT; ++i) {
        br_bank_state_t state; double peak = 0.0;
        setup(); br_bank_state(&br_bank[i], &state);
        br_bank_put(&state, &br_part[0].sound, &br_part[0].mod);
        br_part[0].revision += 2u;
        (void)rms_block();
        br_engine_note_on(0u, i < 12u || (i >= 32u && i < 52u) ? 40u : 60u, 110u);
        for (uint32_t block = 0; block < 100u; ++block) {
            double v = rms_block(); if (v > peak) peak = v;
        }
        if (peak <= 20.0) fprintf(stderr, "silent preset %u %s: %f\n", i + 1, br_bank[i].name, peak);
        assert(peak > 20.0);
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
    /* Both replacement kits must sound on each of the four drum lanes. */
    {
        const uint8_t notes[] = {36u, 38u, 46u, 49u};
        for (uint32_t kit = 6u; kit <= 9u; kit += 3u) {
            for (uint32_t lane = 0u; lane < 4u; ++lane) {
                int32_t audio[BR_BLOCK * 2u]; uint32_t nonzero = 0u;
                setup(); br_ui.part = BR_DRUM_PART;
                br_kit_bank_apply(&br_kit_bank[kit], &br_part[BR_DRUM_PART].sound);
                br_part[BR_DRUM_PART].revision += 2u;
                br_engine_note_on(BR_DRUM_PART, notes[lane], 100u);
                for (uint32_t b = 0; b < 24u; ++b) {
                    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
                    for (uint32_t x = 0; x < BR_BLOCK * 2u; ++x) nonzero |= (uint32_t)audio[x];
                }
                assert(nonzero);
            }
        }
    }
    printf("sound bank table, browse, audition, load, confirm, octave and leave checks passed\n");
    return 0;
}
