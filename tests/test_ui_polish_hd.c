/* SPDX-License-Identifier: GPL-3.0-only */
/* HD release polish: the font data
 * and the HD type's measured widths matching what it draws, the metronome swing, the MONITOR switch,
 * GLO navigation order with the header badges equal to a direct draw, ARP on the kit track, the kit
 * mixer knobs and lane voices, the OCT hints, LFO DEPTHS, the hold cues, the PITCH/FM attenuator
 * letters and the full-word names. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

static void check_header_badge(uint32_t global)
{
    uint16_t pixels[20u * 30u];
    for (uint32_t y = 0u; y < 20u; ++y)
        memcpy(&pixels[y * 30u], &fb[(y + 2u) * 240u + 6u], 30u * sizeof(uint16_t));
    /* The header uses the selected track tint, while the body is black. */
    cv_begin(240u, 24u, br_track_background(br_ui.part));
    if (global) br_global_badge(6, 2); else br_track_badge(6, 2, br_ui.part, 0u);
    cv_blit(0u, 0u);
    for (uint32_t y = 0u; y < 20u; ++y)
        HD_CHECK(!memcmp(&pixels[y * 30u], &fb[(y + 2u) * 240u + 6u], 30u * sizeof(uint16_t)), "!memcmp(&pixels[y * 30u], &fb[(y + 2u) * 240u + 6u], 30u * sizeof(uint16_t))");
}

int main(int argc, char **argv)
{
    if (argc > 1) hd_crop_dir = argv[1];
    br_control_t c;
    /* Preserve specimen geometry including descenders and fallback symbols. */
    HD_CHECK(james_font_width('A') == 5u && james_font_width('I') == 5u, "james_font_width('A') == 5u && james_font_width('I') == 5u");
    HD_CHECK(james_font_width(' ') == 3u && james_font_width('%') == 8u, "james_font_width(' ') == 3u && james_font_width('%%') == 8u");
    HD_CHECK(james_font_row('Q', 7u) != 0u && james_font_row('g', 7u) != 0u, "james_font_row('Q', 7u) != 0u && james_font_row('g', 7u) != 0u");
    HD_CHECK(james_font_row('J', 0u) == 14u && james_font_row('J', 6u) == 17u &&
           james_font_row('J', 7u) == 14u, "james_font_row('J', 0u) == 14u && james_font_row('J', 6u) == 17u && james_font_row('J', 7u) == 14u");
    HD_CHECK(james_font_row('/', 0u) != 0u && james_font_row('#', 2u) != 0u, "james_font_row('/', 0u) != 0u && james_font_row('#', 2u) != 0u");
    /* HD type is proportional: the measured width is exactly the drawn ink (layout depends on it). */
    {
        static const char *const words[] = {"AI", "SAV", "NEXT", "+", "LOAD PRESET", "AUDITION", "MONITOR", "J/g%#"};
        for (uint32_t i = 0; i < sizeof words / sizeof words[0]; ++i) {
            cv_begin(240u, 40u, BR_BACKGROUND); cv_oy = 0; br_widget_text(10, 10, words[i], C_WHITE);
            hd_box_t b = hd_box(cv_px, 240, 40, BR_BACKGROUND, 1);
            HD_CHECK(b.left == 10 && b.right - b.left + 1 == br_widget_text_width(words[i]),
                     "'%s': measured width %d, drawn ink x %d..%d", words[i], br_widget_text_width(words[i]),
                     b.left - 10, b.right - 10);
        }
    }
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
            HD_CHECK(james_font_width((uint8_t)names[i]) == 5u, "james_font_width((uint8_t)names[i]) == 5u");
            for (uint32_t row = 0; row < 8u; ++row)
                HD_CHECK(james_font_row((uint8_t)names[i], row) == masks[i][row] >> 3, "james_font_row((uint8_t)names[i], row) == masks[i][row] >> 3");
        }
    }
    /* One pendulum traverse per beat, alternating smoothly at boundaries. */
    HD_CHECK(br_metronome_swing(0u, 0u) == -32, "br_metronome_swing(0u, 0u) == -32");
    HD_CHECK(br_metronome_swing(0u, 1u) == 32, "br_metronome_swing(0u, 1u) == 32");
    HD_CHECK(br_metronome_swing(64u, 0u) == 0, "br_metronome_swing(64u, 0u) == 0");
    for (uint32_t phase = 0; phase < 127u; ++phase) {
        HD_CHECK(br_metronome_swing(phase, 0u) <= br_metronome_swing(phase + 1u, 0u), "br_metronome_swing(phase, 0u) <= br_metronome_swing(phase + 1u, 0u)");
        HD_CHECK(br_metronome_swing(phase, 1u) == -br_metronome_swing(phase, 0u), "br_metronome_swing(phase, 1u) == -br_metronome_swing(phase, 0u)");
    }
    br_app_init(); host_after_intro(); fm1_ms = 5000u;
    br_message("CPU USAGE HIGH. MONITOR ENABLED.");
    HD_CHECK(!strcmp(br_ui.message, "CPU USAGE HIGH. MONITOR ENABLED."), "CPU warning text is intact");
    HD_CHECK(br_widget_text_width(br_ui.message) <= 240, "CPU warning fits the screen");
    br_ui.message[0] = 0;
    br_playing = 1u;
    for (uint32_t beat = 0; beat < 2u; ++beat) for (uint32_t phase = 0; phase < 128u; phase += 8u) {
        br_transport_phase = phase; br_global_beat = beat;
        uint32_t encoded = br_widget_phase(BR_WIDGET_BPM) - 1u;
        HD_CHECK(br_metronome_swing((encoded & 15u) * 8u, encoded >> 4) == br_metronome_swing(phase, beat), "br_metronome_swing((encoded & 15u) * 8u, encoded >> 4) == br_metronome_swing(phase, beat)");
    }
    br_playing = 0u; HD_CHECK(br_widget_phase(BR_WIDGET_BPM) == 0u, "br_widget_phase(BR_WIDGET_BPM) == 0u");
    /* Settings page: MONITOR is knob 4, an ON/OFF switch drawn with the standard toggle. */
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 2u; br_ui.part = 0u;
    c = br_control(3u);
    HD_CHECK(!strcmp(c.label, "MONITOR") && c.value == 0 && !strcmp(c.text, "OFF"), "!strcmp(c.label, 'MONITOR') && c.value == 0 && !strcmp(c.text, 'OFF')");
    HD_CHECK(br_control_widget(3u, &c) == BR_WIDGET_MONITOR, "MONITOR is the HD monitor switch (D15)");
    br_control_turn(3u, 1);
    HD_CHECK(br_mon.on == 1u, "br_mon.on == 1u");
    { br_session_t saved;
      HD_CHECK(br_capture(&saved), "capture MONITOR on");
      br_monitor_set(0u);
      HD_CHECK(br_restore(&saved) && br_mon.on == 1u, "MONITOR survives the session round trip"); }
    c = br_control(3u); HD_CHECK(!strcmp(c.text, "AVG 0% MAX 0% OVR 0") && br_control_widget(3u, &c) == BR_WIDGET_MONITOR && c.value == 1, "MONITOR on: its figures (AVG/MAX/OVR), the monitor switch, value 1");
    fm1_ms = 1000u; br_mon.at = 1000u; br_ui.last_draw = 1000u; br_ui.redraw = 0u;
    for (uint32_t elapsed = 1u; elapsed <= 500u; ++elapsed) {
        fm1_ms = 1000u + elapsed; br_app_tick();
        HD_CHECK(br_mon.at == 1000u + (elapsed / BR_MON_MS) * BR_MON_MS, "br_mon.at == 1000u + (elapsed / BR_MON_MS) * BR_MON_MS (every 200 ms)");
    }
    br_control_turn(3u, -1); HD_CHECK(br_mon.on == 0u, "br_mon.on == 0u");
    HD_CHECK(br_version_text()[0] == 'V', "br_version_text()[0] == 'V'");
    HD_CHECK(!strcmp(br_header_title(), "TEMPO"), "!strcmp(br_header_title(), 'TEMPO')");
    /* GLO navigation order changes without changing the internal control IDs. */
    {
#if BR_PRODUCT_SMK37
        /* SMK-37: GLO's ring is five screens from PAN; the mixer (FADER BANK) is outside it but still wears the globe. */
        enum { GLO_N = 5, GLO_LAST = 5 };
        static const uint8_t order[5] = {3u, 1u, 6u, 4u, 5u};
        static const char *const titles[5] = {"GLOBAL / PAN", "GLOBAL / FILTER", "GLOBAL / HEAT", "COMPRESSOR", "COMP TIME"};
        br_ui.page = BR_PAGE_MIX; br_ui.part = 0u; br_ui.screen[BR_PAGE_MIX] = 0u;
        HD_CHECK(!strcmp(br_header_title(), "MIXER"), "MIXER (FADER BANK) title");
        br_ui.redraw = 1u; br_draw(); check_header_badge(1u);
        br_ui.screen[BR_PAGE_MIX] = order[0];
#else
        enum { GLO_N = 6, GLO_LAST = 5 };
        static const uint8_t order[6] = {0u, 3u, 1u, 6u, 4u, 5u};
        static const char *const titles[6] = {"MIXER", "GLOBAL / PAN", "GLOBAL / FILTER", "GLOBAL / HEAT", "COMPRESSOR", "COMP TIME"};
        br_ui.page = BR_PAGE_MIX; br_ui.part = 0u; br_ui.screen[BR_PAGE_MIX] = 0u;
#endif
        for (uint32_t i = 0u; i < GLO_N; ++i) {
            HD_CHECK(br_ui.screen[BR_PAGE_MIX] == order[i], "br_ui.screen[BR_PAGE_MIX] == order[i]");
            HD_CHECK(!strcmp(br_header_title(), titles[i]), "!strcmp(br_header_title(), titles[i])");
            br_ui.redraw = 1u; br_draw();
            check_header_badge(1u);   /* the mixer wears the globe too */
            br_select_screen(1);
        }
        HD_CHECK(br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == order[0], "GLO returns to its first screen (MIXER on the FM-1, PAN on the SMK-37)");
        br_select_screen(-1); HD_CHECK(br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == GLO_LAST, "GLO returns to COMP TIME");
    }
    /* Track LFOs keep the track badge; the shared LFO wears the globe. */
    br_ui.part = 0u; br_ui.page = BR_PAGE_LFO;
    for (uint32_t screen = 0u; screen <= BR_LFOS; ++screen) {
        br_ui.screen[BR_PAGE_LFO] = screen; br_ui.redraw = 1u; br_draw();
        check_header_badge(screen == BR_LFOS);
    }
    br_ui.page = BR_PAGE_FX;
    br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.redraw = 1u; br_draw();
    check_header_badge(1u);
    /* The kit may open ARP, which has no editable parameters for that track. */
    br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_HOME;
    br_buttons(1u << BTN_ARP);
    HD_CHECK(br_ui.page == BR_PAGE_PATTERN, "br_ui.page == BR_PAGE_PATTERN");
    {
        br_part_t before = br_part[BR_DRUM_PART];
        for (uint32_t k = 0; k < 4u; ++k) br_control_turn(k, 1);
        HD_CHECK(!memcmp(&before, &br_part[BR_DRUM_PART], sizeof before), "!memcmp(&before, &br_part[BR_DRUM_PART], sizeof before)");
    }
    br_draw(); HD_CHECK(br_ui.page == BR_PAGE_PATTERN, "br_ui.page == BR_PAGE_PATTERN");
    br_ui.part = 0u; br_draw(); HD_CHECK(br_ui.page == BR_PAGE_PATTERN, "br_ui.page == BR_PAGE_PATTERN");
    br_ui.part = BR_DRUM_PART; br_draw(); HD_CHECK(br_ui.page == BR_PAGE_PATTERN, "br_ui.page == BR_PAGE_PATTERN");
    br_ui.page = BR_PAGE_HOME; HD_CHECK(!*br_oct_hint(), "!*br_oct_hint()");
    /* Each kit mixer knob edits its own drum and preserves the other lanes. */
    br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 3u;
    for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) br_part[BR_DRUM_PART].lane_level[lane] = 50u;
    for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) {
        br_control_turn(lane, 1);
        for (uint32_t j = 0; j < BR_DRUM_LANES; ++j)
            HD_CHECK(br_part[BR_DRUM_PART].lane_level[j] == (j <= lane ? 51u : 50u), "br_part[BR_DRUM_PART].lane_level[j] == (j <= lane ? 51u : 50u)");
        HD_CHECK(br_ui.lane == lane, "br_ui.lane == lane");
    }
    /* PRESETS changes only the selected lane's voice on KIT LEVEL. */
    {
        const uint32_t kits[] = { BR_KIT_BRAIDS, BR_KIT_PLAITS };
        for (uint32_t kit = 0; kit < 2u; ++kit) {
            br_part[BR_DRUM_PART].sound.preset = kits[kit];
            for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) {
                uint32_t mask, old, level = br_part[BR_DRUM_PART].lane_level[lane];
                br_gate_select_lane(lane);
                mask = br_part[BR_DRUM_PART].sound.drum_voice_mask;
                old = br_drum_lane_choice(mask, lane);
                br_preset_encoder_turn(1);
                HD_CHECK(br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, lane) == (old + 1u) % 3u, "br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, lane) == (old + 1u) %% 3u");
                for (uint32_t j = 0; j < BR_DRUM_LANES; ++j)
                    if (j != lane) HD_CHECK(br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, j) == br_drum_lane_choice(mask, j), "br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, j) == br_drum_lane_choice(mask, j)");
                HD_CHECK(br_part[BR_DRUM_PART].lane_level[lane] == level, "br_part[BR_DRUM_PART].lane_level[lane] == level");
                br_preset_encoder_turn(-1);
                HD_CHECK(br_part[BR_DRUM_PART].sound.drum_voice_mask == mask, "br_part[BR_DRUM_PART].sound.drum_voice_mask == mask");
            }
        }
        br_ui.cursor = 7u; br_ui.step_bank = 1u; br_ui.redraw = 0u;
        br_algorithm_encoder_turn(-1);
        HD_CHECK(br_ui.part == BR_DRUM_PART && br_ui.cursor == 7u && br_ui.step_bank == 1u && !br_ui.redraw, "br_ui.part == BR_DRUM_PART && br_ui.cursor == 7u && br_ui.step_bank == 1u && !br_ui.redraw");
        br_ui.part = 0u; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        br_algorithm_encoder_turn(-1); HD_CHECK(br_ui.part == 0u, "track selection stops at the first track");
        br_algorithm_encoder_turn(1); HD_CHECK(br_ui.part == 1u, "track selection can change while patches remain global");
        br_ui.page = BR_PAGE_HOME; br_ui.part = 2u;
        br_algorithm_encoder_turn(1); HD_CHECK(br_ui.part == BR_DRUM_PART, "br_ui.part == BR_DRUM_PART");
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
        HD_CHECK(br_audio_kit_meter_read(0u) == 1234u, "br_audio_kit_meter_read(0u) == 1234u");
        HD_CHECK(br_audio_kit_meter_read(1u) == 32768u && br_audio_kit_meter_read(2u) == 32768u, "br_audio_kit_meter_read(1u) == 32768u && br_audio_kit_meter_read(2u) == 32768u");
        HD_CHECK(br_audio_kit_meter_read(3u) == 0u, "br_audio_kit_meter_read(3u) == 0u");
        memset(br_audio_kit_partial, 0, sizeof br_audio_kit_partial);
        br_audio_kit_meter_capture(BR_BLOCK, 4u);
        HD_CHECK(br_audio_kit_meter_read(0u) > 0u && br_audio_kit_meter_read(0u) < 1234u, "br_audio_kit_meter_read(0u) > 0u && br_audio_kit_meter_read(0u) < 1234u");
        br_audio_kit_meter_enable(0u);
    }
    { uint32_t bank = br_ui.gate_bank;
      br_buttons(1u << BTN_OCT_DOWN); br_buttons(1u << BTN_OCT_UP);
      HD_CHECK(br_ui.gate_bank == bank, "br_ui.gate_bank == bank"); } /* Drum-mixer key banks were retired in 1616. */
    /* the OCT hint: sequence pages, punch, and the kit's sound page */
    br_ui.part = 0u; br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0u;
#if BR_PRODUCT_SMK37
    HD_CHECK(!strcmp(br_oct_hint(), "OCT-/+ + REC SHIFT NOTES"), "!strcmp(br_oct_hint(), 'OCT-/+ + REC SHIFT NOTES')");      /* SMK-37: the pads are the steps */
#else
    HD_CHECK(!strcmp(br_oct_hint(), "OCT-/+ + REC SHIFT NOTES"), "!strcmp(br_oct_hint(), 'OCT-/+ + REC SHIFT NOTES')");
#endif
    br_ui.screen[BR_PAGE_SEQ] = 1u; HD_CHECK(!*br_oct_hint(), "!*br_oct_hint()");
    { int8_t old_octave = br_ui.octave; uint32_t old_bank = br_ui.step_bank;
      br_buttons(1u << BTN_OCT_DOWN); br_buttons(1u << BTN_OCT_UP);
      HD_CHECK(br_ui.screen[BR_PAGE_SEQ] == 1u && br_ui.octave == old_octave && br_ui.step_bank == old_bank, "br_ui.screen[BR_PAGE_SEQ] == 1u && br_ui.octave == old_octave && br_ui.step_bank == old_bank");
      br_leds(); HD_CHECK(host_led_level_state[BTN_OCT_DOWN] == FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] == FM1_LED_OFF, "host_led_level_state[BTN_OCT_DOWN] == FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] == FM1_LED_OFF"); }
    br_ui.page = BR_PAGE_PUNCH; HD_CHECK(!strcmp(br_oct_hint(), "OCT-/+: PAGES"), "!strcmp(br_oct_hint(), 'OCT-/+: PAGES')");
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0u; HD_CHECK(!*br_oct_hint(), "!*br_oct_hint()");      /* octave works here */
    /* LFO DEPTHS: named for what it does, with a hint */
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 2u;
    HD_CHECK(!strcmp(br_header_title(), "MOD DEPTHS") && !strcmp(br_oct_hint(), "SCALES ALL MODULATION"), "!strcmp(br_header_title(), 'MOD DEPTHS') && !strcmp(br_oct_hint(), 'SCALES ALL MODULATION')");
    {
        c = br_control(1u);
        HD_CHECK(!strcmp(c.label, "HARMONICS"), "!strcmp(c.label, 'HARMONICS')");
        HD_CHECK(br_control_widget(1u, &c) == BR_WIDGET_MOD_ATTEN, "br_control_widget(1u, &c) == BR_WIDGET_MOD_ATTEN");
        HD_CHECK(br_control_widget_variant(1u, &c, BR_WIDGET_MOD_ATTEN) == 1u, "br_control_widget_variant(1u, &c, BR_WIDGET_MOD_ATTEN) == 1u");
        /* The miniature illustrations distinguish timbre, harmonics, morph and pitch. */
        {
            static uint16_t pic[4][60u * 20u];
            for (uint32_t kind = 0; kind < 4u; ++kind) {
                cv_begin(60u, 60u, BR_BACKGROUND); cv_oy = 0;
                bw_mod_atten(0, 0, 60u, 96u, kind, 0u);
                memcpy(pic[kind], cv_px, sizeof pic[kind]);
            }
            for (uint32_t a = 0; a < 4u; ++a) for (uint32_t b = a + 1u; b < 4u; ++b) {
                int differ = memcmp(pic[a], pic[b], sizeof pic[a]) != 0;
                if (!differ) {
                    char path[512];
                    snprintf(path, sizeof path, "%s/mod_atten_kind%u_and_%u_x6.ppm", hd_crop_dir, a, b);
                    hd_write_crop(path, pic[a], 60, 1, 0, 0, 60, 20, 6);
                }
                HD_CHECK(differ, "attenuator pictures for targets %u and %u differ (0 TIMBRE, 1 PITCH, 2 MORPH, 3 FM)", a, b);
            }
        }
    }
    /* Shared compact drum names and full FX header names. */
    HD_CHECK(!strcmp(br_drum_voice_name[0], "KICK1") && !strcmp(br_drum_voice_name[4], "OHAT"), "!strcmp(br_drum_voice_name[0], 'KICK1') && !strcmp(br_drum_voice_name[4], 'OHAT')");
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_MOD; HD_CHECK(!strcmp(br_header_title(), "FX / MOD"), "!strcmp(br_header_title(), 'FX / MOD')");
    /* Every allowed engine and every page title fits the full-size header. */
    {
        char title[BR_HEADER_TITLE_MAX + 1u];
        br_header_short_title("SAMPLE KIT A", title); HD_CHECK(!strcmp(title, "SAMPLE A"), "!strcmp(title, 'SAMPLE A')");
        br_header_short_title("SAMPLE KIT B", title); HD_CHECK(!strcmp(title, "SAMPLE B"), "!strcmp(title, 'SAMPLE B')");
        br_header_short_title("WAVE TERRAIN", title); HD_CHECK(!strcmp(title, "TERRAIN"), "!strcmp(title, 'TERRAIN')");
        br_header_short_title("FX / MOD", title); HD_CHECK(!strcmp(title, "FX/MOD"), "!strcmp(title, 'FX/MOD')");
        br_header_short_title("STRING MACH", title); HD_CHECK(!strcmp(title, "STR MACH"), "!strcmp(title, 'STR MACH')");
        br_header_short_title("CHOOSE SLOT", title); HD_CHECK(!strcmp(title, "PICK SLOT"), "!strcmp(title, 'PICK SLOT')");
        br_header_short_title("SAVE FIRST?", title); HD_CHECK(!strcmp(title, "SAVE FIRST"), "!strcmp(title, 'SAVE FIRST')");
        br_header_short_title("ENV / FILTER", title); HD_CHECK(!strcmp(title, "ENV/FILTER"), "!strcmp(title, 'ENV/FILTER')");
        br_header_short_title("FX / REVERB", title); HD_CHECK(!strcmp(title, "FX/REVERB"), "!strcmp(title, 'FX/REVERB')");
        for (uint32_t part = 0u; part < BR_PARTS; ++part) {
            br_ui.part = part;
            for (uint32_t preset = 0u; preset < BR_PRESET_COUNT; ++preset) {
                if (!br_preset_allowed_on_part(part, preset)) continue;
                br_ui.page = BR_PAGE_HOME; br_part[part].sound.preset = preset;
                br_header_short_title(br_header_title(), title);
                HD_CHECK(strlen(title) <= BR_HEADER_TITLE_MAX && br_widget_text_width(title) * 2 <= 126, "strlen(title) <= BR_HEADER_TITLE_MAX && br_widget_text_width(title) * 2 <= 126");
            }
            for (uint32_t page = 0u; page < BR_PAGE_COUNT; ++page) {
                br_ui.page = page;
                for (uint32_t screen = 0u; screen < br_screen_count(); ++screen) {
                    br_ui.screen[page] = screen;
                    br_header_short_title(br_header_title(), title);
                    HD_CHECK(strlen(title) <= BR_HEADER_TITLE_MAX && br_widget_text_width(title) * 2 <= 126, "strlen(title) <= BR_HEADER_TITLE_MAX && br_widget_text_width(title) * 2 <= 126");
                }
            }
        }
    }
    for (uint32_t i = 0u; i < BR_DRUM_VOICE_COUNT; ++i)
        HD_CHECK(strlen(br_drum_voice_name[i]) <= 5u, "strlen(br_drum_voice_name[i]) <= 5u");
    /* Kit HOME exposes the requested controls without changing preset IDs. */
    {
        static const uint32_t kits[] = {BR_KIT_BRAIDS, BR_KIT_PLAITS};
        static const char *const names[] = {"BRAIDS KIT", "PLAITS KIT"};
        static const char *const labels[][4] = {
            {"TIMBRE", "COLOR", "SNAP", "GATE"}, {"TIMBRE", "HARMONICS", "MORPH", "GATE"}};
        br_app_init(); host_after_intro(); br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0;
        for (uint32_t i = 0u; i < 2u; ++i) {
            br_load_preset(BR_DRUM_PART, kits[i]);
            HD_CHECK(!strcmp(br_sound_name(kits[i]), names[i]), "!strcmp(br_sound_name(kits[i]), names[i])");
            HD_CHECK(br_next_kit_preset(kits[i], 1) == kits[(i + 1u) % 2u], "kit preset cycle");
            for (uint32_t k = 0u; k < 4u; ++k) HD_CHECK(!strcmp(br_control(k).label, labels[i][k]), "!strcmp(br_control(k).label, labels[i][k])");
            br_part[BR_DRUM_PART].sound.gate = BR_GATE_OPEN;
            HD_CHECK(br_control(3).value == BR_GATE_OPEN, "br_control(3).value == BR_GATE_OPEN");
            br_control_turn(3u, -1); HD_CHECK(br_part[BR_DRUM_PART].sound.gate == BR_GATE_OPEN - 1u, "br_part[BR_DRUM_PART].sound.gate == BR_GATE_OPEN - 1u");
            br_select_screen(1); HD_CHECK(br_ui.screen[BR_PAGE_HOME] == 1u && br_home_filter_page(), "br_ui.screen[BR_PAGE_HOME] == 1u && br_home_filter_page()");
            br_select_screen(1); HD_CHECK(br_ui.screen[BR_PAGE_HOME] == 0u, "HOME sound pages wrap");
            br_open_home_settings(); HD_CHECK(br_ui.screen[BR_PAGE_HOME] == 2u && br_sound_mod_page(), "HOME deep page: MOD DEPTHS");
            br_open_home_settings(); HD_CHECK(br_ui.screen[BR_PAGE_HOME] == 0u, "HOME deep menu returns");
        }
        br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        HD_CHECK(!*br_oct_hint(), "!*br_oct_hint()");
    }
    /* SNAP lives on the kit's HOME (Braids and sample kits, knob 3); TEMPO no longer edits it. */
    {
        br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 2u;
        for (uint32_t p = 0u; p < BR_PARTS; ++p) {
            br_ui.part = p; br_part[BR_DRUM_PART].sound.drum_snap = 50u;
            HD_CHECK(strcmp(br_control(2u).label, "DRUM SNAP") && strcmp(br_control(2u).label, "SNAP"), "TEMPO knob 3 is not SNAP");
            br_control_turn(2u, 1); HD_CHECK(br_part[BR_DRUM_PART].sound.drum_snap == 50u, "TEMPO knob 3 leaves drum_snap");
        }
    }
    /* Gate limits preserve natural tails at OPEN and take the shorter step limit. */
    {
        static br_session_t before, after;
        const uint32_t p = BR_DRUM_PART;
        br_app_init(); host_after_intro(); br_ui.part = p; br_ui.page = BR_PAGE_HOME;
        br_load_preset(p, BR_KIT_BRAIDS);
        HD_CHECK(br_capture(&before), "br_capture(&before)");
        br_part[p].sound.gate = 32u; HD_CHECK(br_capture(&after), "br_capture(&after)");
        HD_CHECK(memcmp(&before, &after, sizeof before), "kit GATE is saved (session v17)");
        br_part[p].sound.gate = BR_GATE_OPEN; HD_CHECK(br_restore(&after) && br_part[p].sound.gate == 32u, "kit GATE restores");
        for (uint32_t i = 0; i < BR_PARTS; ++i) br_mode[i] = 0u;
        br_mode[p] = 1u; br_effective_sound[p] = br_part[p].sound;
        br_seq_lane_gate_frames[p][0] = 0u;
        br_seq_lane_gate_frames[p][1] = 100u;
        br_seq_lane_gate_frames[p][2] = br_gate_samples(32u) + 200u;
        br_seq_lane_gate_frames[p][3] = 0u;
        br_prepare_kit_source_controls();
        HD_CHECK(br_audio_kit_source_controls[p].gate_frames[0] == br_gate_samples(32u), "br_audio_kit_source_controls[p].gate_frames[0] == br_gate_samples(32u)");
        HD_CHECK(br_audio_kit_source_controls[p].gate_frames[1] == 100u, "br_audio_kit_source_controls[p].gate_frames[1] == 100u");
        HD_CHECK(br_audio_kit_source_controls[p].gate_frames[2] == br_gate_samples(32u), "br_audio_kit_source_controls[p].gate_frames[2] == br_gate_samples(32u)");
        br_effective_sound[p].gate = BR_GATE_OPEN; br_prepare_kit_source_controls();
        HD_CHECK(!br_audio_kit_source_controls[p].gate_frames[0], "!br_audio_kit_source_controls[p].gate_frames[0]");
        HD_CHECK(br_audio_kit_source_controls[p].gate_frames[1] == 100u, "br_audio_kit_source_controls[p].gate_frames[1] == 100u");
        HD_CHECK(br_audio_kit_source_controls[p].gate_frames[2] == br_gate_samples(32u) + 200u, "br_audio_kit_source_controls[p].gate_frames[2] == br_gate_samples(32u) + 200u");
    }
    /* A finite gate actually shortens a kick in every kit engine. */
    {
        static union { uint64_t align; uint8_t bytes[BR_DRUM_STATE_BYTES]; } state;
        static int32_t out[256];
        static const uint32_t kits[] = {BR_KIT_BRAIDS, BR_KIT_PLAITS};
        const uint32_t levels[4] = {127u, 127u, 127u, 127u};
        uint32_t limits[4] = {0u, 0u, 0u, 0u};
        for (uint32_t i = 0u; i < 2u; ++i) {
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
            HD_CHECK(energy[0] > 0u && energy[1] < energy[0], "energy[0] > 0u && energy[1] < energy[0]");
        }
    }
    return hd_finish("HD release polish: fonts, metronome, monitor, GLO order and badges, kit, hints, names");
}
