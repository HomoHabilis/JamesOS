/* The small UI changes of this release: the monitor switch and version badge, ARP on the kit track, the
 * OCT hint, LFO DEPTHS, the hold cues and the full-word names. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void check_header_badge(uint32_t global)
{
    uint16_t pixels[20u * 30u];
    for (uint32_t y = 0u; y < 20u; ++y)
        memcpy(&pixels[y * 30u], &fb[(y + 2u) * 240u + 6u], 30u * sizeof(uint16_t));
    cv_begin(240u, 24u, BR_BACKGROUND);
    if (global) br_global_badge(6, 2); else br_track_badge(6, 2, br_ui.part, 0u);
    cv_blit(0u, 0u);
    for (uint32_t y = 0u; y < 20u; ++y)
        assert(!memcmp(&pixels[y * 30u], &fb[(y + 2u) * 240u + 6u], 30u * sizeof(uint16_t)));
}

int main(void)
{
    br_control_t c;
    /* Preserve specimen geometry including descenders and fallback symbols. */
    assert(james_font_width('A') == 5u && james_font_width('I') == 5u);
    assert(james_font_width(' ') == 3u && james_font_width('%') == 8u);
    assert(james_font_row('Q', 7u) != 0u && james_font_row('g', 7u) != 0u);
    assert(james_font_row('J', 0u) == 14u && james_font_row('J', 6u) == 17u &&
           james_font_row('J', 7u) == 14u);
    assert(james_font_row('/', 0u) != 0u && james_font_row('#', 2u) != 0u);
    assert(br_widget_text_width("AI") == 11);
    {
        static const char names[] = "BGPLY";
        static const uint8_t masks[5][8] = {
            {0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0,0x00},
            {0x70,0x88,0x80,0x80,0x98,0x88,0x78,0x08},
            {0xF0,0x88,0x88,0xF0,0x80,0x80,0xC0,0x00},
            {0xC0,0x40,0x40,0x40,0x40,0x40,0xF8,0x00},
            {0x88,0x88,0x48,0x38,0x08,0x88,0x70,0x00}
        };
        for (uint32_t i = 0; i < 5u; ++i) {
            assert(james_font_width((uint8_t)names[i]) == 5u);
            for (uint32_t row = 0; row < 8u; ++row)
                assert(james_font_row((uint8_t)names[i], row) == masks[i][row] >> 3);
        }
    }
    /* One pendulum traverse per beat, alternating smoothly at boundaries. */
    assert(br_metronome_swing(0u, 0u) == -32);
    assert(br_metronome_swing(0u, 1u) == 32);
    assert(br_metronome_swing(64u, 0u) == 0);
    for (uint32_t phase = 0; phase < 127u; ++phase) {
        assert(br_metronome_swing(phase, 0u) <= br_metronome_swing(phase + 1u, 0u));
        assert(br_metronome_swing(phase, 1u) == -br_metronome_swing(phase, 0u));
    }
    br_app_init(); fm1_ms = 5000u;
    br_playing = 1u;
    for (uint32_t beat = 0; beat < 2u; ++beat) for (uint32_t phase = 0; phase < 128u; phase += 8u) {
        br_transport_phase = phase; br_global_beat = beat;
        uint32_t encoded = br_widget_phase(BR_WIDGET_BPM) - 1u;
        assert(br_metronome_swing((encoded & 15u) * 8u, encoded >> 4) == br_metronome_swing(phase, beat));
    }
    br_playing = 0u; assert(br_widget_phase(BR_WIDGET_BPM) == 0u);
    /* Settings page: MONITOR is knob 4, an ON/OFF switch drawn with the standard toggle; it is not saved. */
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 2u; br_ui.part = 0u;
    c = br_control(3u);
    assert(!strcmp(c.label, "MONITOR") && c.value == 0 && !strcmp(c.text, "OFF"));
    assert(br_control_widget(3u, &c) == BR_WIDGET_TOGGLE);
    br_control_turn(3u, 1);
    assert(br_mon.on == 1u);
    c = br_control(3u); assert(!strcmp(c.text, "ON") && br_control_widget(3u, &c) == BR_WIDGET_TOGGLE && c.value == 1);
    fm1_ms = 1000u; br_mon.at = 1000u; br_ui.last_draw = 1000u; br_ui.redraw = 0u;
    for (uint32_t elapsed = 1u; elapsed <= 500u; ++elapsed) {
        fm1_ms = 1000u + elapsed; br_app_tick();
        assert(br_mon.at == 1000u + (elapsed / 100u) * 100u);
    }
    br_control_turn(3u, -1); assert(br_mon.on == 0u);
    assert(br_version_text()[0] == 'V');
    assert(!strcmp(br_header_title(), "SETTINGS"));
    /* GLO navigation order changes without changing the internal control IDs. */
    {
        static const uint8_t order[7] = {0u, 1u, 3u, 6u, 4u, 5u, 2u};
        static const char *const titles[7] = {"MIXER", "GLOBAL / FILTER", "GLOBAL / PAN", "GLOBAL / HEAT", "COMPRESSOR", "COMP KNOBS", "SETTINGS"};
        br_ui.page = BR_PAGE_MIX; br_ui.part = 0u; br_ui.screen[BR_PAGE_MIX] = 0u;
        for (uint32_t i = 0u; i < 7u; ++i) {
            assert(br_ui.screen[BR_PAGE_MIX] == order[i]);
            assert(!strcmp(br_header_title(), titles[i]));
            br_ui.redraw = 1u; br_draw();
            check_header_badge(i != 0u);
            br_select_screen(1);
        }
        assert(br_ui.screen[BR_PAGE_MIX] == 0u);
        br_select_screen(-1); assert(br_ui.screen[BR_PAGE_MIX] == 2u);
    }
    /* The kit may open ARP, which has no editable parameters for that track. */
    br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_HOME;
    br_buttons(1u << BTN_ARP);
    assert(br_ui.page == BR_PAGE_PATTERN);
    {
        br_part_t before = br_part[BR_DRUM_PART];
        for (uint32_t k = 0; k < 4u; ++k) br_control_turn(k, 1);
        assert(!memcmp(&before, &br_part[BR_DRUM_PART], sizeof before));
    }
    br_draw(); assert(br_ui.page == BR_PAGE_PATTERN);
    br_ui.part = 0u; br_draw(); assert(br_ui.page == BR_PAGE_PATTERN);
    br_ui.part = BR_DRUM_PART; br_draw(); assert(br_ui.page == BR_PAGE_PATTERN);
    br_ui.page = BR_PAGE_HOME; assert(!*br_oct_hint());
    /* Each kit mixer knob edits its own drum and preserves the other lanes. */
    br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 3u;
    for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) br_part[BR_DRUM_PART].lane_level[lane] = 50u;
    for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) {
        br_control_turn(lane, 1);
        for (uint32_t j = 0; j < BR_DRUM_LANES; ++j)
            assert(br_part[BR_DRUM_PART].lane_level[j] == (j <= lane ? 51u : 50u));
        assert(br_ui.lane == lane);
    }
    /* PRESETS changes only the selected lane's voice on KIT LEVEL. */
    {
        const uint32_t kits[] = { BR_KIT_BRAIDS, BR_KIT_PLAITS, BR_KIT_808, BR_KIT_909 };
        for (uint32_t kit = 0; kit < 4u; ++kit) {
            br_part[BR_DRUM_PART].sound.preset = kits[kit];
            for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) {
                uint32_t mask, old, level = br_part[BR_DRUM_PART].lane_level[lane];
                br_gate_select_lane(lane);
                mask = br_part[BR_DRUM_PART].sound.drum_voice_mask;
                old = br_drum_lane_choice(mask, lane);
                br_preset_encoder_turn(1);
                assert(br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, lane) == (old + 1u) % 3u);
                for (uint32_t j = 0; j < BR_DRUM_LANES; ++j)
                    if (j != lane) assert(br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, j) == br_drum_lane_choice(mask, j));
                assert(br_part[BR_DRUM_PART].lane_level[lane] == level);
                br_preset_encoder_turn(-1);
                assert(br_part[BR_DRUM_PART].sound.drum_voice_mask == mask);
            }
        }
        br_ui.cursor = 7u; br_ui.step_bank = 1u; br_ui.redraw = 0u;
        br_algorithm_encoder_turn(-1);
        assert(br_ui.part == BR_DRUM_PART && br_ui.cursor == 7u && br_ui.step_bank == 1u && !br_ui.redraw);
        br_ui.part = 0u; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        br_algorithm_encoder_turn(-1); assert(br_ui.part == 2u);
        br_algorithm_encoder_turn(1); assert(br_ui.part == 0u);
        br_ui.page = BR_PAGE_HOME; br_ui.part = 2u;
        br_algorithm_encoder_turn(1); assert(br_ui.part == BR_DRUM_PART);
        br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    }
    /* Audio-owned peak publication retains a short hit and clamps safely. */
    {
        memset(br_audio_kit_partial, 0, sizeof br_audio_kit_partial);
        br_audio_kit_meter_enable(1u);
        br_audio_kit_partial[0][0][0] = 1234;
        br_audio_kit_partial[0][1][0] = -32768;
        br_audio_kit_partial[0][2][0] = INT32_MIN;
        br_audio_kit_meter_capture(BR_BLOCK, 1u);
        assert(br_audio_kit_meter_read(0u) == 1234u);
        assert(br_audio_kit_meter_read(1u) == 32768u && br_audio_kit_meter_read(2u) == 32768u);
        assert(br_audio_kit_meter_read(3u) == 0u);
        memset(br_audio_kit_partial, 0, sizeof br_audio_kit_partial);
        br_audio_kit_meter_capture(BR_BLOCK, 4u);
        assert(br_audio_kit_meter_read(0u) > 0u && br_audio_kit_meter_read(0u) < 1234u);
        br_audio_kit_meter_enable(0u);
    }
    { uint32_t bank = br_ui.gate_bank;
      br_buttons(1u << BTN_OCT_DOWN); br_buttons(1u << BTN_OCT_UP);
      assert(br_ui.gate_bank == bank); } /* Drum-mixer key banks were retired in 1616. */
    /* the OCT hint: sequence pages, punch, and the kit's sound page */
    br_ui.part = 0u; br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0u;
    assert(!strcmp(br_oct_hint(), "OCT-/+: STEP BANK"));
    br_ui.screen[BR_PAGE_SEQ] = 1u; assert(!*br_oct_hint());
    { int8_t old_octave = br_ui.octave; uint32_t old_bank = br_ui.step_bank;
      br_buttons(1u << BTN_OCT_DOWN); br_buttons(1u << BTN_OCT_UP);
      assert(br_ui.screen[BR_PAGE_SEQ] == 1u && br_ui.octave == old_octave && br_ui.step_bank == old_bank);
      br_leds(); assert(host_led_level_state[BTN_OCT_DOWN] == FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] == FM1_LED_OFF); }
    br_ui.page = BR_PAGE_PUNCH; assert(!strcmp(br_oct_hint(), "OCT-/+: PAGES"));
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0u; assert(!*br_oct_hint());      /* octave works here */
    /* LFO DEPTHS: named for what it does, with a hint */
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 2u;
    assert(!strcmp(br_header_title(), "LFO DEPTHS") && !strcmp(br_oct_hint(), "HOW FAR LFO 1 MOVES EACH"));
    {
        uint16_t top[60u * 20u];
        c = br_control(1u);
        assert(!strcmp(c.label, "PITCH"));
        assert(br_control_widget(1u, &c) == BR_WIDGET_MOD_ATTEN);
        assert(br_control_widget_variant(1u, &c, BR_WIDGET_MOD_ATTEN) == 1u);
        for (uint32_t variant = 1u; variant <= 3u; variant += 2u) {
            const char *expected = variant == 1u ? "P" : "F";
            cv_begin(60u, 60u, BR_BACKGROUND); cv_oy = 0;
            bw_mod_atten(0, 0, 60u, 64u, variant, 0u);
            memcpy(top, cv_px, sizeof top);
            cv_begin(60u, 60u, BR_BACKGROUND); cv_oy = 0;
            br_widget_text(30 - br_widget_text_width(expected) / 2, 7, expected, BR_ACC);
            assert(!memcmp(top, cv_px, sizeof top));
        }
    }
    /* Shared compact drum names and full FX header names. */
    assert(!strcmp(br_drum_voice_name[0], "KICK1") && !strcmp(br_drum_voice_name[4], "OHAT"));
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_MOD; assert(!strcmp(br_header_title(), "FX / MOD"));
    /* Every allowed engine and every page title fits the full-size header. */
    {
        char title[BR_HEADER_TITLE_MAX + 1u];
        br_header_short_title("SAMPLE KIT A", title); assert(!strcmp(title, "SAMPLE A"));
        br_header_short_title("SAMPLE KIT B", title); assert(!strcmp(title, "SAMPLE B"));
        br_header_short_title("WAVE TERRAIN", title); assert(!strcmp(title, "TERRAIN"));
        br_header_short_title("FX / MOD", title); assert(!strcmp(title, "FX/MOD"));
        br_header_short_title("STRING MACH", title); assert(!strcmp(title, "STR MACH"));
        br_header_short_title("CHOOSE SLOT", title); assert(!strcmp(title, "PICK SLOT"));
        br_header_short_title("SAVE FIRST?", title); assert(!strcmp(title, "SAVE FIRST"));
        br_header_short_title("ENV / FILTER", title); assert(!strcmp(title, "ENV/FILTER"));
        br_header_short_title("FX / REVERB", title); assert(!strcmp(title, "FX/REVERB"));
        for (uint32_t part = 0u; part < BR_PARTS; ++part) {
            br_ui.part = part;
            for (uint32_t preset = 0u; preset < BR_PRESET_COUNT; ++preset) {
                if (!br_preset_allowed_on_part(part, preset)) continue;
                br_ui.page = BR_PAGE_HOME; br_part[part].sound.preset = preset;
                br_header_short_title(br_header_title(), title);
                assert(strlen(title) <= BR_HEADER_TITLE_MAX && br_widget_text_width(title) * 2 <= 126);
            }
            for (uint32_t page = 0u; page < BR_PAGE_COUNT; ++page) {
                br_ui.page = page;
                for (uint32_t screen = 0u; screen < br_screen_count(); ++screen) {
                    br_ui.screen[page] = screen;
                    br_header_short_title(br_header_title(), title);
                    assert(strlen(title) <= BR_HEADER_TITLE_MAX && br_widget_text_width(title) * 2 <= 126);
                }
            }
        }
    }
    for (uint32_t i = 0u; i < BR_DRUM_VOICE_COUNT; ++i)
        assert(strlen(br_drum_voice_name[i]) <= 5u);
    /* Kit HOME exposes the requested controls without changing preset IDs. */
    {
        static const uint32_t kits[] = {BR_KIT_BRAIDS, BR_KIT_PLAITS, BR_KIT_808, BR_KIT_909};
        static const char *const names[] = {"BRAIDS KIT", "PLAITS KIT", "80S KIT", "90S KIT"};
        static const char *const labels[][4] = {
            {"TIMBRE", "COLOR", "", "GATE"}, {"TIMBRE", "HARMONICS", "MORPH", "GATE"},
            {"PITCH", "", "", "GATE"}, {"PITCH", "", "", "GATE"}};
        br_app_init(); br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0;
        for (uint32_t i = 0u; i < 4u; ++i) {
            br_load_preset(BR_DRUM_PART, kits[i]);
            assert(!strcmp(br_sound_name(kits[i]), names[i]));
            assert(br_next_kit_preset(kits[i], 1) == kits[(i + 1u) % 4u]);
            for (uint32_t k = 0u; k < 4u; ++k) assert(!strcmp(br_control(k).label, labels[i][k]));
            br_part[BR_DRUM_PART].sound.gate = BR_GATE_OPEN;
            assert(br_control(3).value == BR_GATE_OPEN);
            br_control_turn(3u, -1); assert(br_part[BR_DRUM_PART].sound.gate == BR_GATE_OPEN - 1u);
            if (i >= 2u) { int32_t old = br_part[BR_DRUM_PART].sound.tune; br_control_turn(0u, 1); assert(br_part[BR_DRUM_PART].sound.tune == old + 1); }
            br_select_screen(1); assert(br_ui.screen[BR_PAGE_HOME] == 1u && br_home_filter_page());
            br_select_screen(1); assert(br_ui.screen[BR_PAGE_HOME] == 0u);
        }
        br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        assert(!*br_oct_hint());
    }
    /* Settings SNAP always edits the kit, independently of selected track. */
    {
        br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 2u;
        for (uint32_t p = 0u; p < BR_PARTS; ++p) {
            br_ui.part = p; br_part[BR_DRUM_PART].sound.drum_snap = 50u;
            assert(!strcmp(br_control(0u).label, "DRUM SNAP"));
            br_control_turn(0u, 1); assert(br_part[BR_DRUM_PART].sound.drum_snap == 51u);
        }
    }
    /* Gate limits preserve natural tails at OPEN and take the shorter step limit. */
    {
        static br_session_t before, after;
        const uint32_t p = BR_DRUM_PART;
        br_app_init(); br_ui.part = p; br_ui.page = BR_PAGE_HOME;
        br_load_preset(p, BR_KIT_BRAIDS);
        assert(br_capture(&before));
        br_part[p].sound.gate = 32u; assert(br_capture(&after));
        assert(!memcmp(&before, &after, sizeof before)); /* transient: session format remains unchanged */
        for (uint32_t i = 0; i < BR_PARTS; ++i) br_mode[i] = 0u;
        br_mode[p] = 1u; br_effective_sound[p] = br_part[p].sound;
        br_seq_lane_gate_frames[p][0] = 0u;
        br_seq_lane_gate_frames[p][1] = 100u;
        br_seq_lane_gate_frames[p][2] = br_gate_samples(32u) + 200u;
        br_seq_lane_gate_frames[p][3] = 0u;
        br_prepare_kit_source_controls();
        assert(br_audio_kit_source_controls[p].gate_frames[0] == br_gate_samples(32u));
        assert(br_audio_kit_source_controls[p].gate_frames[1] == 100u);
        assert(br_audio_kit_source_controls[p].gate_frames[2] == br_gate_samples(32u));
        br_effective_sound[p].gate = BR_GATE_OPEN; br_prepare_kit_source_controls();
        assert(!br_audio_kit_source_controls[p].gate_frames[0]);
        assert(br_audio_kit_source_controls[p].gate_frames[1] == 100u);
        assert(br_audio_kit_source_controls[p].gate_frames[2] == br_gate_samples(32u) + 200u);
    }
    /* A finite gate actually shortens a kick in every kit engine. */
    {
        static union { uint64_t align; uint8_t bytes[BR_DRUM_STATE_BYTES]; } state;
        static int32_t out[256];
        static const uint32_t kits[] = {BR_KIT_BRAIDS, BR_KIT_PLAITS, BR_KIT_808, BR_KIT_909};
        const uint32_t levels[4] = {127u, 127u, 127u, 127u};
        uint32_t limits[4] = {0u, 0u, 0u, 0u};
        for (uint32_t i = 0u; i < 4u; ++i) {
            br_sound_t sound; uint64_t energy[2] = {0u, 0u};
            br_preset_sound(kits[i], &sound);
            for (uint32_t gated = 0u; gated < 2u; ++gated) {
                limits[0] = gated ? br_gate_samples(0u) : 0u;
                if (kits[i] == BR_KIT_PLAITS) { plaits_kit_init(state.bytes, 123u); plaits_kit_note_on(state.bytes, 36u, 127u); }
                else { br_drum_init(state.bytes, 123u, kits[i]); br_drum_note_on(state.bytes, 36u, 127u); }
                for (uint32_t block = 0u; block < 8u; ++block) {
                    memset(out, 0, sizeof out);
                    if (kits[i] == BR_KIT_PLAITS) plaits_kit_render_gated(state.bytes, &sound, levels, limits, out, 256u);
                    else br_drum_render_gated(state.bytes, &sound, levels, limits, out, 256u);
                    if (block >= 4u) for (uint32_t j = 0u; j < 256u; ++j) energy[gated] += out[j] < 0 ? -(int64_t)out[j] : out[j];
                }
            }
            assert(energy[0] > 0u && energy[1] < energy[0]);
        }
    }
    printf("ui polish: ok\n");
    return 0;
}
