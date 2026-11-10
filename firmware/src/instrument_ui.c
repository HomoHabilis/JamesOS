/* SPDX-License-Identifier: GPL-3.0-only */
/* Repeated module buttons browse pages; SELECT controls the global filter. */
#include "instrument_midi_routes.h"
static br_midi_routes_t br_midi_routes;
/* Remember which local key presses actually sent MIDI so OFF can release
 * outstanding notes without sending offs for keys that never sent an on. */
static uint8_t br_midi_out_held[27], br_midi_out_sustain[27];
static const char *const br_page_name[BR_PAGE_COUNT] = {"HOME", "ENVELOPE", "FX", "GLOBAL", "SEQUENCE", "PATTERNS", "SCALE", "LFO", "PATCH", "PUNCH"
#ifdef BR_PATTERN_SONG_EXPERIMENT
, "PATTERNS"
#endif
};
static const char *const br_target_name[BR_TARGET_COUNT] = {"OFF", "PITCH", "TIMBRE", "COLOR", "FILTER", "TRACK LEVEL", "AMP DECAY", "RETIRED", "DELAY MIX", "REVERB SEND", "MOD FX MIX", "HARMONICS", "OSC MIX", "RETIRED", "AMP ATTACK", "AMP SUSTAIN", "AMP RELEASE", "RESONANCE", "LFO 1 RATE", "LFO 2 RATE"};
static const char *const br_source_name[BR_SOURCE_COUNT] = {"OFF", "OSC AVG", "MIX AVG", "AMP ENV", "FILTER ENV", "LFO 1", "LFO 2", "RETIRED", "VELOCITY", "NOTE", "RANDOM",
    "GATE", "LFO 1 TRI", "LFO 1 SQU", "LFO 2 TRI", "LFO 2 SQU"};
static const char *const br_wave_name[BR_WAVE_COUNT] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "SAMPLE HOLD"};
static const char *const br_scale_name[BR_SCALE_COUNT] = {"CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "MAJOR PENT", "MINOR PENT", "BLUES"};
static const char *const br_delay_name[BR_DELAY_COUNT] = {"DIGITAL", "TAPE ECHO", "MULTITAP", "GLITCH"};
static const char *const br_reverb_name[BR_REVERB_COUNT] = {"ROOM", "HALL", "SPRING", "SPACE", "ETHEREAL"};
/* Keep saved engine IDs stable; only these three engines are offered. */
static const uint8_t br_reverb_ui_id[] = {BR_REVERB_ROOM, BR_REVERB_HALL, BR_REVERB_SPACE};
static const char *const br_mod_name[BR_MOD_COUNT] = {"CHORUS", "FLANGE", "PHASER", "VIBRATO"};
static const char *const br_delay_note_name[11] = {
    "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4",
    "1", "1/2", "2", "3", "4"
};
static const char *const br_root_name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* Saved target IDs 2 and 3 now address the HEAT type and amount controls. */
static const char *const br_heat_name[BR_HEAT_COUNT] = {"BITCRUSH", "SATURATE", "TAPE WOW"};
/* Saved IDs stay as they were; only OFF, ALL, the three tracks and the kick drum (the first drum
 * source) are offered. Drum sources 2 to 4 from older projects still read and play as they did. */
static const char *const br_comp_source_name[BR_COMP_SOURCE_COUNT] = {
    "OFF", "ALL", "TRACK 1", "TRACK 2", "TRACK 3", "KICK DRUM", "DRUM 2", "DRUM 3", "DRUM 4"
};
static const char *const br_comp_ratio_name[8] = {"1:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1"};

/* These UI ordinals intentionally omit retired saved enum values. Keep the
 * serialized IDs stable while making DRIVE, master DISTORTION, and LFO3
 * unreachable through the controls. Targets follow the SOUND controls, then
 * filter/level/envelope and FX. Saved IDs never depend on this menu order. */
static const uint8_t br_target_ui_id[] = {
    BR_TARGET_OFF, BR_TARGET_PITCH, BR_TARGET_TIMBRE, BR_TARGET_HARMONICS,
    BR_TARGET_COLOR, BR_TARGET_OUTPUT, BR_TARGET_FILTER, BR_TARGET_RESONANCE, BR_TARGET_LEVEL,
    BR_TARGET_ATTACK, BR_TARGET_DECAY, BR_TARGET_SUSTAIN, BR_TARGET_RELEASE,
    BR_TARGET_DELAY, BR_TARGET_REVERB, BR_TARGET_MOD, BR_TARGET_LFO1_RATE, BR_TARGET_LFO2_RATE
};
static const uint8_t br_source_ui_id[] = {
    BR_SOURCE_OFF, BR_SOURCE_OSC, BR_SOURCE_MIX, BR_SOURCE_AMP, BR_SOURCE_ENV,
    BR_SOURCE_LFO1, BR_SOURCE_LFO1_TRI, BR_SOURCE_LFO1_SQU,
    BR_SOURCE_LFO2, BR_SOURCE_LFO2_TRI, BR_SOURCE_LFO2_SQU,
    BR_SOURCE_VELOCITY, BR_SOURCE_KEY, BR_SOURCE_GATE, BR_SOURCE_RANDOM
};
#define BR_REVERB_UI_COUNT (sizeof br_reverb_ui_id / sizeof br_reverb_ui_id[0])
static uint32_t br_reverb_ui_ordinal(uint32_t engine)
{
    uint32_t i;
    for (i = 0u; i < BR_REVERB_UI_COUNT; ++i)
        if (br_reverb_ui_id[i] == engine) return i;
    return 0u;
}
#define BR_TARGET_UI_COUNT (sizeof br_target_ui_id / sizeof br_target_ui_id[0])
#define BR_SOURCE_UI_COUNT (sizeof br_source_ui_id / sizeof br_source_ui_id[0])

/* Some saved destinations have no consumer in a particular kit. Hide those
 * choices without rewriting a saved route when the sound changes. */
static uint32_t br_target_ui_available(uint32_t target)
{
    uint32_t preset = br_part[br_ui.part].sound.preset;
    /* Sample TIMBRE is a hidden filter, not an editable panel parameter.
     * Keep old stored routes working, but do not offer it for new routes. */
    if (target == BR_TARGET_TIMBRE && (preset == BR_KIT_808 || preset == BR_KIT_909)) return 0u;
    return target == BR_TARGET_OFF || br_mod_target_supported(preset, target);
}
static uint32_t br_target_ui_count(void)
{
    uint32_t i, count = 0u;
    for (i = 0u; i < BR_TARGET_UI_COUNT; ++i)
        count += br_target_ui_available(br_target_ui_id[i]);
    return count;
}
static uint32_t br_target_ui_value(uint32_t ordinal)
{
    uint32_t i;
    for (i = 0u; i < BR_TARGET_UI_COUNT; ++i) {
        uint32_t target = br_target_ui_id[i];
        if (!br_target_ui_available(target)) continue;
        if (!ordinal) return target;
        --ordinal;
    }
    return BR_TARGET_OFF;
}
static uint32_t br_target_ui_ordinal(uint32_t target)
{
    uint32_t i, ordinal = 0u;
    for (i = 0u; i < BR_TARGET_UI_COUNT; ++i) {
        if (!br_target_ui_available(br_target_ui_id[i])) continue;
        if (br_target_ui_id[i] == target) return ordinal;
        ++ordinal;
    }
    return 0u;
}
static uint32_t br_source_ui_value(uint32_t ordinal)
{
    if (ordinal >= BR_SOURCE_UI_COUNT) ordinal = (uint32_t)BR_SOURCE_UI_COUNT - 1u;
    return br_source_ui_id[ordinal];
}
static uint32_t br_source_ui_ordinal(uint32_t source)
{
    uint32_t i;
    for (i = 0; i < BR_SOURCE_UI_COUNT; ++i)
        if (br_source_ui_id[i] == source) return i;
    return 0u;
}
static const char *br_source_ui_name(uint32_t source)
{ return br_source_name[br_source_ui_value(br_source_ui_ordinal(source))]; }
static const char *br_target_ui_name(uint32_t target)
{
    uint32_t preset = br_part[br_ui.part].sound.preset;
    if (!br_target_ui_available(target)) return "N/A";
    if (target == BR_TARGET_COLOR &&
        (br_is_plaits(preset) || preset == BR_KIT_PLAITS)) return "MORPH";
    return br_target_name[target];
}
static uint32_t br_retrig_page(void)
{
    return br_ui.page == BR_PAGE_PATTERN && br_ui.part == BR_DRUM_PART;
}
static uint32_t br_retrig_editing(void)
{
    return br_retrig_page() && br_ui.retrig_key && br_ui.retrig_key <= 27u &&
        br_ui.retrig_part == br_ui.part && br_ui.retrig_lane == br_ui.lane &&
        br_ui.retrig_step < br_part[BR_DRUM_PART].length &&
        (fm1_in.notes & (1u << (br_ui.retrig_key - 1u))) &&
        (BR_STEP_HIT(br_part[BR_DRUM_PART].step[br_ui.retrig_step]) & (1u << br_ui.lane));
}
static uint32_t br_gate_page(void)
{
    return br_ui.page == BR_PAGE_ROUTE && br_ui.screen[BR_PAGE_ROUTE] == 3u;
}
static uint32_t br_home_filter_page(void)
{
    return br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] == 1u;
}
static uint32_t br_sound_mod_page(void)
{
    return br_ui.page == BR_PAGE_ROUTE &&
        !br_is_kit(br_part[br_ui.part].sound.preset) &&
        br_ui.screen[BR_PAGE_ROUTE] == 2u;
}
static void br_gate_select_lane(uint32_t lane)
{
    if (lane >= BR_DRUM_LANES) lane = BR_DRUM_LANES - 1u;
    if (br_ui.lane != lane) br_ui.edit_key = 0xFFFFFFFFu;
    br_ui.lane = lane;
    br_ui.gate_bank = (uint32_t)(br_clip((int32_t)br_part[BR_DRUM_PART].lane_level[lane], 0, 127)) / 16u;
    br_ui.redraw = 1;
}
static void br_clamp_ui_lane(void)
{
    if (br_ui.lane >= BR_DRUM_LANES) {
        br_ui.lane = 0u;
        br_ui.edit_key = 0xFFFFFFFFu;
    }
}
static void br_step_trigger_clear_all(uint32_t part, uint32_t step)
{
    uint32_t lane;
    if (part == BR_DRUM_PART) {
        for (lane = 0; lane < BR_DRUM_LANES; ++lane)
            br_step_trigger_clear(part, step, lane);
    } else br_step_trigger_clear(part, step, 0u);
}
static void br_step_trigger_init_word(uint32_t part, uint32_t step, uint32_t word)
{
    uint32_t lane;
    if (part == BR_DRUM_PART) {
        for (lane = 0; lane < BR_DRUM_LANES; ++lane) {
            br_step_micro_set(part, step, lane, 0);
            if (word & (1u << lane)) {
                br_step_trigger_init(part, step, lane);
                br_step_velocity_set(part, step, lane,
                    (word & (1u << (lane + 8u))) ? 127u : 100u);
            }
        }
    } else {
        br_step_micro_set(part, step, 0u, 0);
        if (word & BR_STEP_ON) br_step_trigger_init(part, step, 0u);
    }
}
static uint32_t br_patch_slot(void)
{
    return br_ui.screen[BR_PAGE_ROUTE];
}

/* FX screens: 0 SENDS, 1 FILTER, 2 DELAY, 3 MODUL8N, 4 REVERB. The three effect
 * pages keep their old 0/1/2 (delay/reverb/mod) numbering internally. */
enum { BR_FX_SENDS, BR_FX_DRIVE, BR_FX_DELAY, BR_FX_MOD, BR_FX_REVERB };
static uint32_t br_fx_sends_page(void)
{
    return br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == BR_FX_SENDS;
}

static uint32_t br_fx_effect(uint32_t screen)
{
    return screen == BR_FX_DELAY ? 0u : screen == BR_FX_REVERB ? 1u : 2u;
}

/* A held snap gesture belongs to the page and track where it began. */
static uint32_t br_fx_snap_context(void)
{
    return br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == br_ui.fx_snap_screen &&
        br_ui.part == br_ui.fx_snap_part;
}
static uint32_t br_fx_snap_knob(uint32_t k)
{
    return br_ui.page == BR_PAGE_FX && (br_fx_sends_page() || k == 3u);
}
static uint32_t br_screen_count(void)
{
    switch (br_ui.page) {
    case BR_PAGE_HOME: return 2u;
    case BR_PAGE_ENV: return 3u;
    case BR_PAGE_FX: return 5u;
    case BR_PAGE_PUNCH: return 2u;
    case BR_PAGE_MIX: return br_ui.screen[BR_PAGE_MIX] >= 7u ? 2u : 7u;
    case BR_PAGE_SEQ: return 2u;
    case BR_PAGE_PATTERN: return 1u;
    case BR_PAGE_SCALE: return 1u; /* sounds/kit browser only */
    case BR_PAGE_LFO: return BR_LFOS;
    case BR_PAGE_ROUTE: return br_gate_page() ? 1u : 3u;
    default: return 1u;
    }
}
static void br_select_screen(int32_t turn)
{
    int32_t count = (int32_t)br_screen_count();
    int32_t at = (int32_t)br_ui.screen[br_ui.page];
    if (!br_is_kit(br_part[br_ui.part].sound.preset) &&
        (br_ui.page == BR_PAGE_LFO || (br_ui.page == BR_PAGE_ROUTE && !br_gate_page()))) {
        count = (int32_t)BR_LFOS + 3;
        at = br_ui.page == BR_PAGE_ROUTE ? at + (int32_t)BR_LFOS : at;
        at = (at + turn % count) % count;
        if (at < 0) at += count;
        if (at < (int32_t)BR_LFOS) {
            br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = (uint32_t)at;
        } else {
            br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = (uint32_t)at - BR_LFOS;
        }
    } else if (br_ui.page == BR_PAGE_MIX && at >= 7) {
        int32_t midi = (at - 7 + turn) % 2;
        br_ui.screen[BR_PAGE_MIX] = 7u + (uint32_t)(midi < 0 ? midi + 2 : midi);
    } else if (br_ui.page == BR_PAGE_MIX) {
        /* Keep the existing page IDs for controls and shortcuts; only navigation changes. */
        static const uint8_t order[7] = {0u, 1u, 3u, 6u, 4u, 5u, 2u};
        uint32_t i;
        for (i = 0u; i < 7u && order[i] != (uint32_t)at; ++i) {}
        at = i < 7u ? (int32_t)i : 0;
        at = (at + turn) % count;
        br_ui.screen[br_ui.page] = order[at < 0 ? at + count : at];
    } else {
        at = (at + turn) % count;
        br_ui.screen[br_ui.page] = (uint32_t)(at < 0 ? at + count : at);
    }
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
    br_ui.edit_key = 0xFFFFFFFFu;
    br_ui.redraw = 1;
}
static void br_open_module(uint32_t module)
{
    if (br_ui.page == module) br_select_screen(1);
    else { br_ui.page = module; br_ui.screen[module] = 0; br_ui.edit_key = 0xFFFFFFFFu; }
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
    br_ui.redraw = 1;
}
/* The MIDI pages are a separate two-page Global shortcut, not extra stops in
 * the ordinary Global cycle. Repeating the chord returns to the mixer. */
static void br_open_midi_settings(void)
{
    uint32_t already = br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] >= 7u;
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = already ? 0u : 7u;
    br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
}
static void br_mixer_octave_action(uint32_t button)
{
    uint32_t p=br_gate_page() ? BR_DRUM_PART : br_ui.part;
    if (br_gate_page()) {
        if (button == BTN_OCT_DOWN) br_remember();
        br_part[p].revision++;
        if (button == BTN_OCT_DOWN) br_kit_mute_mask ^= 1u << br_ui.lane;
        else br_kit_solo_mask ^= 1u << br_ui.lane;
        RING_PUBLISH(); br_part[p].revision++;
        br_ui.redraw = 1u;
        return;
    }
    if(button==BTN_OCT_DOWN) {
        br_part[p].revision++;
        br_part[p].sound.mute ^= 1u;
        RING_PUBLISH();br_part[p].revision++;
    } else {
        br_solo_mask ^= 1u << p;
        /* Each cached sound incorporates the solo mask. */
        for(p=0;p<BR_PARTS;p++) {
            br_part[p].revision++;RING_PUBLISH();br_part[p].revision++;
        }
    }
    br_ui.redraw=1;
}
static const char *const br_drum_voice_name[BR_DRUM_VOICE_COUNT] = {
    "KICK1", "KICK2", "SNARE", "CLAP", "OHAT", "CHAT", "CYMB", "PERC",
    "KICK3", "RIM", "PHAT", "TOM"
};
static const char *const br_arp_mode_name[BR_ARP_MODES] = {"UP", "DOWN", "UP AND DOWN", "RANDOM", "MANUAL"};
static const char *const br_arp_rate_name[BR_ARP_RATES] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const br_off_on_name[2] = {"OFF", "ON"};
static const char *const br_speed_name[3] = {"1/8", "1/16", "1/32"};
static const char *const br_time_quant_name[BR_TIME_QUANT_COUNT] = {
    "STEP", "OFF", "1/8", "1/16", "1/32", "1/64", "1/128"
};
/* Track and global accents live together so animated icon highlights cannot
 * retain a stale hue when the base palette changes. Shades are precomputed
 * RGB565 values; display redraws do not blend color channels at runtime. */
static uint16_t br_palette_color(uint32_t part, uint32_t shade)
{
    static const uint16_t colors[BR_PARTS + 1u][3] = {
        {RGB(8, 184, 104), RGB(104, 216, 164), RGB(184, 240, 212)},
        {RGB(160, 72, 224), RGB(202, 152, 240), RGB(232, 208, 252)},
        {RGB(32, 128, 240), RGB(120, 184, 248), RGB(192, 224, 255)},
        {RGB(240, 72, 40), RGB(248, 152, 128), RGB(255, 208, 192)},
        {RGB(232, 168, 16), RGB(248, 208, 112), RGB(255, 236, 192)}
    };
    return colors[part <= BR_PARTS ? part : BR_DRUM_PART][shade < 3u ? shade : 0u];
}
static uint16_t br_track_color(uint32_t part) { return br_palette_color(part, 0u); }
/* Dark RGB565 shades keep the screen legible while carrying each track hue. */
static uint16_t br_track_background(uint32_t part)
{
    static const uint16_t colors[BR_PARTS] = {
        RGB(0, 8, 0), RGB(8, 0, 8), RGB(0, 4, 8), RGB(8, 0, 0)
    };
    return colors[part < BR_PARTS ? part : BR_DRUM_PART];
}
#define BR_BACKGROUND br_track_background(br_ui.part)
#define BR_GLOBAL_ACC br_palette_color(BR_PARTS, 0u)
/* Resolve the page accent once per redraw. A scoped card color keeps SWING
 * and MUTE tied to their track without branching at every drawn pixel. */
#define BR_ACCENT_DEFAULT RGB(8, 184, 104)
static uint16_t br_ui_accent = BR_ACCENT_DEFAULT;
#define BR_ACC br_ui_accent
#define BR_DIM RGB(150, 158, 162)
#define BR_CARD RGB(16, 20, 24)
#define BR_LINE RGB(48, 56, 58)
#define BR_LENGTH_BG RGB(32, 40, 42)

/* Fixed, irregular 2x2-pixel speckles give the RGB565 panel a subtle fade
 * from the track tint to black without a repeating ordered-dither grid. */
static uint32_t br_gradient_component(uint32_t component, uint32_t y, uint32_t threshold, uint32_t end)
{
    uint32_t remaining = end - y, denominator = 2u * end * end;
    uint32_t n = component * remaining * (remaining + end);
    uint32_t q = n / denominator, rem = n % denominator;
    return q + (rem * 32u > (threshold * 2u + 1u) * denominator);
}
static uint16_t br_gradient_color(uint16_t color, uint32_t y, uint32_t threshold)
{
    uint32_t end = color == br_track_background(0u) ? 92u : 96u;
    if (y >= end) return C_BLACK;
    return (uint16_t)((br_gradient_component((color >> 11) & 31u, y, threshold, end) << 11) |
        (br_gradient_component((color >> 5) & 63u, y, threshold, end) << 5) |
        br_gradient_component(color & 31u, y, threshold, end));
}
static uint32_t br_mon_dirty;      /* something else was drawn over the monitor's bottom rows */
static void br_canvas_blit(uint32_t x, uint32_t y)
{
    uint16_t bg = BR_BACKGROUND, target = swap16(bg);
    if (cv_background == bg) {
        for (uint32_t row = 0; row < cv_h; ++row) {
            uint16_t colors[16];
            uint32_t cell = 0xffffffffu, threshold = 0u;
            if (y + row >= (bg == br_track_background(0u) ? 92u : 96u)) {
                for (uint32_t col = 0; col < cv_w; ++col)
                    if (cv_px[row * cv_w + col] == target) cv_px[row * cv_w + col] = 0;
                continue;
            }
            for (uint32_t t = 0; t < 16u; ++t)
                colors[t] = swap16(br_gradient_color(bg, y + row, t));
            for (uint32_t col = 0; col < cv_w; ++col) {
                uint16_t *pixel = &cv_px[row * cv_w + col];
                if (*pixel == target) {
                    uint32_t next = (x + col) >> 1;
                    if (next != cell) {
                        uint32_t hash = next * 0x9e3779b1u ^ ((y + row) >> 1) * 0x85ebca6bu;
                        hash ^= hash >> 16; hash *= 0x7feb352du;
                        hash ^= hash >> 15; hash *= 0x846ca68bu;
                        hash ^= hash >> 16;
                        threshold = hash & 15u; cell = next;
                    }
                    *pixel = colors[threshold];
                }
            }
        }
    }
    if (y + cv_h > 238u) br_mon_dirty = 1u;
    lcd_blit(x, y, cv_w, cv_h, cv_px);
    BR_UI_CLOCK_SERVICE();
}
#define cv_blit(x, y) br_canvas_blit((x), (y))

static void br_background_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h) return;
    while (h) {
        uint32_t rows = CV_MAX / w;
        if (rows > h) rows = h;
        cv_begin(w, rows, BR_BACKGROUND);
        cv_blit(x, y);
        y += rows; h -= rows;
    }
}

/* ------------------------------------------------------------------------------------------------ CPU monitor */
/* The bottom two pixel rows of the screen become one cyan meter of the audio CPU. At 10 Hz it is refreshed
 * from the last 100 ms of audio: the mean render time of a 256-frame half as a share of its deadline. Past
 * 90 % red comes in from the right, reaching the full width at 100 %. */
#define BR_MON_CPU RGB(0, 200, 230)
#define BR_MON_RED RGB(240, 60, 48)
#define BR_MON_TRACK RGB(24, 30, 32)
static struct { uint8_t on; uint32_t at, cpu, painted; } br_mon;      /* the share in tenths of a percent */
static void br_monitor_sample(void)
{
#if defined(FM1_DEVICE)
    uint32_t w[2];
    fm1_audio_window_take(w);
    if (w[0]) br_mon.cpu = w[1] * 1000u / FM1_AUDIO_HALF_TICKS;
#endif
}
static void br_monitor_set(uint32_t on)
{
    if (on == br_mon.on) return;
    br_mon.on = (uint8_t)on; br_mon.at = fm1_ms - 100u; br_mon.painted = 0xffffffffu;
    if (!on) { br_background_rect(0, 238, 240, 2); br_ui.redraw = 1u; }
}
static void br_monitor_overlay(void)
{
    uint32_t cpu;
    int32_t red;
    if (!br_mon.on) return;
    if (fm1_ms - br_mon.at >= 100u) { br_mon.at = fm1_ms; br_monitor_sample(); }
    if (br_mon.painted == br_mon.at && !br_mon_dirty) return;
    br_mon.painted = br_mon.at; cpu = br_mon.cpu;
    cv_begin(240, 2, C_BLACK);
    cv_rect(0, 0, 240, 2, BR_MON_TRACK);
    if (cpu) cv_rect(0, 0, (int32_t)(cpu > 1000u ? 1000u : cpu) * 240 / 1000, 2, BR_MON_CPU);
    if (cpu > 900u) {
        red = (int32_t)(cpu - 900u) * 240 / 100; if (red > 240) red = 240;
        if (red > 0) cv_rect(240 - red, 0, red, 2, BR_MON_RED);
    }
    cv_blit(0, 238);
    br_mon_dirty = 0u;
}

/* The version in the settings badge: "V0.1 BUILD 1560" from the package version and identity. */
static const char *br_version_text(void)
{
    static char text[24];
#if defined(FM1_DEVICE) || defined(FELUCCA_ID)
    const char *ver = APP_VER, *id = FELUCCA_ID;
    uint32_t i = 0, n = 0;
    br_text_copy(text, "V", sizeof text);
    while (ver[i] && ver[i] != '-' && n < 8u) { char c[2] = {ver[i++], 0}; br_text_append(text, c, sizeof text); ++n; }
    /* identity FM-1_926NNNN: the digits after the three-digit prefix are the build number */
    if (id[0] == 'F' && id[1] == 'M' && id[2] == '-' && id[3] == '1' && id[4] == '_' && id[5] && id[6] && id[7]) {
        br_text_append(text, " BUILD ", sizeof text); br_text_append(text, id + 8, sizeof text);
    }
#else
    br_text_copy(text, "V0.1 HOST", sizeof text);
#endif
    return text;
}


typedef struct { const char *label; int32_t value, low, high; const char *text; } br_control_t;
static void br_key_up(uint32_t key);
static void br_arp_release_unheld(uint32_t p);
static uint32_t br_seq_step_edit(void);
static void br_step_micro_shift(int32_t direction);
static uint32_t br_step_active(uint32_t part, uint32_t step, uint32_t lane, uint32_t kit);
static void br_record_note_cancel_all(void);
static void br_record_note_cancel_part(uint32_t part, uint32_t clear_stale);

static const char *br_drum_voice_label(uint32_t mask, uint32_t lane)
{
    return br_drum_voice_name[br_drum_voice_id(mask, lane)];
}

static void br_comp_source_text(uint32_t source, char *text, uint32_t capacity)
{
    if (source >= BR_COMP_SOURCE_COUNT) source = BR_COMP_OFF;
    br_text_copy(text, br_comp_source_name[source], capacity);
}

static void br_comp_route_text(uint32_t source, char *text, uint32_t capacity)
{
    uint32_t part, target;
    char number[8];
    if (source >= BR_COMP_SOURCE_COUNT) source = BR_COMP_OFF;
    if (source == BR_COMP_OFF) { br_text_copy(text, "COMPRESSOR OFF", capacity); return; }
    if (source == BR_COMP_ALL) { br_text_copy(text, "ALL TRACKS", capacity); return; }
    part = br_comp_source_part(source);
    if (part >= BR_PARTS) { br_text_copy(text, "COMPRESSOR OFF", capacity); return; }
    if (source >= BR_COMP_DRUM_1) {
        br_text_copy(text, "DK", capacity);
    } else {
        if (part == BR_DRUM_PART) br_text_copy(text, "DK", capacity);
        else { br_text_copy(text, "T", capacity); br_number(number, (int32_t)part + 1); br_text_append(text, number, capacity); }
    }
    br_text_append(text, " >", capacity);
    for (target = 0; target < BR_PARTS; ++target) if (target != part) {
        if (target == BR_DRUM_PART) br_text_append(text, " DK", capacity);
        else { br_text_append(text, " T", capacity); br_number(number, (int32_t)target + 1); br_text_append(text, number, capacity); }
    }
}

static void br_note_text(char *out, int32_t note)
{
    static const char *const name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char number[8];
    if (note < 0) { br_text_copy(out, "REST", 16); return; }
    br_text_copy(out, name[(uint32_t)note % 12u], 16);
    br_number(number, note / 12 - 1);
    br_text_append(out, number, 16);
}

static br_control_t br_control(uint32_t k)
{
    static const char *const env_label[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
    static const char *const mix_label[4] = {"TRACK 1", "TRACK 2", "TRACK 3", "DRUM KIT"};
    uint32_t p = br_gate_page() ? BR_DRUM_PART : br_seq_step_edit() ? br_ui.step_press_part : br_ui.part;
    uint32_t at = br_seq_step_edit() ? br_ui.step_press_step : br_ui.cursor;
    uint32_t lane = br_seq_step_edit() ? br_ui.step_press_lane : br_ui.lane;
    const volatile br_sound_t *v = &br_part[p].sound;
    const volatile br_mod_config_t *m = &br_part[p].mod;
    const volatile br_fx_config_t *f = &br_part[p].fx;
    uint32_t page = br_ui.screen[br_ui.page], step = br_part[p].step[at];
    uint32_t kit = br_seq_step_edit() ? br_ui.step_press_kit : br_is_kit(v->preset);
    br_control_t c = {"", 0, 0, 127, 0};
    br_clamp_ui_lane();
    if (!kit) lane = 0u;
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) return c;
#endif
    if (br_ui.page == BR_PAGE_HOME) {
        static const char *const label[4] = {"TIMBRE", "COLOR", "", ""};
        if (!page && kit && v->preset == BR_KIT_PLAITS) {
            static const char *const plaits_kit[4] = {"TIMBRE", "HARMONICS", "MORPH", "GATE"};
            c.label = plaits_kit[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->plaits_harmonics :
                k == 2u ? (int32_t)v->color : (int32_t)v->gate;
            if (k == 3u) c.high = BR_GATE_OPEN;
        } else if (!page && kit && (v->preset == BR_KIT_808 || v->preset == BR_KIT_909)) {
            static const char *const sample_kit[4] = {"PITCH", "", "", "GATE"};
            c.label = sample_kit[k]; c.value = !k ? v->tune : k == 3u ? (int32_t)v->gate : 0;
            if (!k) { c.low = -12; c.high = 12; }
            else if (k == 3u) c.high = BR_GATE_OPEN;
        } else if (!page && kit) {
            static const char *const braids_kit[4] = {"TIMBRE", "COLOR", "", "GATE"};
            c.label = braids_kit[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->color : k == 3u ? (int32_t)v->gate : 0;
            if (k == 3u) c.high = BR_GATE_OPEN;
        } else if (!page && br_is_plaits(v->preset) && !kit) {
            static const char *const plaits[4] = {"TIMBRE", "HARMONICS", "MORPH", "OSC MIX"};
            c.label = plaits[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->plaits_harmonics :
                      k == 2u ? (int32_t)v->color : (int32_t)v->plaits_aux;
        } else if (br_home_filter_page()) {
            static const char *const filter[4] = {"FILTER", "RESONANCE", "ENV DEPTH", "KEY TRACK"};
            c.label = filter[k];
            /* The depth is the mod envelope's direct amount; it counts only while that envelope is aimed at FILTER. */
            c.value = !k ? (int32_t)v->filter : k == 1u ? (int32_t)v->filter_resonance :
                      k == 3u ? (int32_t)v->filter_keytrack : m->target == BR_TARGET_FILTER ? m->amount : 0;
            if (k == 3u) { c.high = 1; c.text = c.value ? "ON" : "OFF"; }
            if (k == 2u) c.low = -127;
        } else if (br_sound_mod_page()) {
            static const char *const mod_braids[4] = {"TIMBRE", "PITCH", "", ""};
            static const char *const mod_plaits[4] = {"TIMBRE", "PITCH", "MORPH", ""};
            c.label = br_is_plaits(v->preset) ? mod_plaits[k] : mod_braids[k];
            c.value = !k ? (int32_t)v->timbre_mod : k == 1u ? (int32_t)v->fm_mod :
                      k == 2u ? (int32_t)v->morph_mod : 64;
        } else {
            c.label = label[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->color : 0;
        }
    } else if (br_ui.page == BR_PAGE_ENV) {
        if (page < 2u) {
            c.label = env_label[k];
            c.value = !page ? (k == 0u ? v->attack : k == 1u ? v->decay : k == 2u ? v->sustain : v->release) :
                             (k == 0u ? m->attack : k == 1u ? m->decay : k == 2u ? m->sustain : m->release);
        } else {
            static const char *const label[4] = {"INTENSITY", "TARGET", "LOOP", "RETRIGGER"};
            c.label = label[k]; c.value = k == 0u ? m->amount : k == 1u ? (int32_t)br_target_ui_ordinal(m->target) : k == 2u ? (int32_t)m->loop : (int32_t)m->retrigger;
            if (!k) c.low = -127;
            else if (k == 1u) { c.high = (int32_t)br_target_ui_count() - 1; c.text = br_target_ui_name(m->target); }
            else { c.high = 1; c.text = c.value ? "ON" : "OFF"; }
        }
    } else if (br_ui.page == BR_PAGE_FX) {
        static const char *const label[3][4] = {{"ENGINE", "TIME", "FEEDBACK", "MIX"},
                                               {"ENGINE", "SIZE", "DECAY", "SEND"},
                                               {"ENGINE", "RATE", "DEPTH", "MIX"}};
        static const char *const sends[4] = {"DRIVE", "DELAY", "MODULATION", "REVERB"};
        static const char *const drive[4] = {"MODE", "GAIN", "TONE", "SEND"};
        static const char *const drive_name[3] = {"OVERDRIVE", "DISTORT", "FUZZ"};
        uint32_t fx = br_fx_effect(page);
        if (page == BR_FX_SENDS) {
            c.label = sends[k];
            c.value = k == 0u ? (int32_t)v->drive : k == 1u ? (int32_t)v->delay :
                      k == 2u ? (int32_t)f->mod_mix : (int32_t)v->reverb;
        } else if (page == BR_FX_DRIVE) {
            /* Stored in the fields the retired DRIVE page used: mode, gain (drive_gain),
             * tone (drive_tone, default open) and the send (sound.drive, default zero). */
            c.label = drive[k];
            c.value = !k ? (int32_t)(f->drive_mode > 2u ? 0u : f->drive_mode) :
                      k == 1u ? (int32_t)f->drive_gain : k == 2u ? (int32_t)f->drive_tone : (int32_t)v->drive;
            if (!k) { c.high = 2; c.text = drive_name[c.value]; }
        } else {
        c.label = label[fx][k];
        if (fx == 0u) {
            c.value = k == 0u ? f->delay_engine : k == 1u ?
                (int32_t)br_fx_delay_note_index(f->delay_time) :
                k == 2u ? f->delay_feedback : v->delay;
            if (!k) { c.high = BR_DELAY_COUNT - 1; c.text = br_delay_name[f->delay_engine]; }
            else if (k == 1u) { c.high = 10; c.text = br_delay_note_name[c.value]; }
        } else if (fx == 1u) {
            /* One reverb for every track: its type, size and decay are track 1's
             * and edits reach all four; only the send is the track's own. */
            c.value = k == 0u ? (int32_t)br_reverb_ui_ordinal(br_part[0].fx.reverb_engine) :
                k == 1u ? br_part[0].sound.space : k == 2u ? br_part[0].fx.reverb_tone : v->reverb;
            if (!k) { c.high = BR_REVERB_UI_COUNT - 1u; c.text = br_reverb_name[br_reverb_ui_id[c.value]]; }
        } else {
            c.value = k == 0u ? f->mod_engine : k == 1u ? f->mod_rate : k == 2u ? f->mod_depth : f->mod_mix;
            if (!k) { c.high = BR_MOD_COUNT - 1; c.text = br_mod_name[f->mod_engine]; }
        }
        }
    } else if (br_ui.page == BR_PAGE_MIX) {
        if (page >= 7u && page <= 8u) {
            static const char *const channel_labels[7] = {"T1 CH", "T2 CH", "T3 CH", "D1 CH", "D2 CH", "D3 CH", "D4 CH"};
            if (page == 7u && k == 3u) {
                static const char *const clock_text[3] = {"OFF", "RECEIVE", "SEND"};
                c.label = "MIDI CLOCK"; c.value = br_midi_clock_enabled ? 1 : br_midi_clock_out ? 2 : 0; c.high = 2;
                c.text = clock_text[c.value];
            } else {
                uint32_t channel = page == 7u ? k : 3u + k;
                c.label = channel_labels[channel]; c.value = br_midi_channels[channel]; c.low = 0; c.high = 16;
                if (!c.value) c.text = "OFF";
            }
        } else if (!page) { c.label = mix_label[k]; c.value = br_part[k].sound.level; }
        else if (page == 1u) {
            static const char *const label[4] = {"FILTER", "RESONANCE", "ENV DEPTH", "SMOOTH"};
            c.label = label[k];
            c.value = !k ? br_master.fx.filter : k == 1u ? br_master.fx.resonance :
                k == 2u ? br_master.fx.env_depth : (int32_t)br_master.fx.smooth;
            if (k == 2u) c.low = -127;
            else if (k == 3u) c.high = 100;
        }
        else if (page == 6u) {
            static const char *const label[4] = {"HEAT TYPE", "AMOUNT", "", ""};
            c.label = label[k];
            c.value = !k ? br_master.fx.heat_type : k == 1u ? br_master.fx.heat_amount : 0;
            if (!k) {
                uint32_t type = br_master.fx.heat_type < BR_HEAT_COUNT ?
                    br_master.fx.heat_type : BR_HEAT_BIT;
                c.high = BR_HEAT_COUNT - 1u;
                c.value = (int32_t)type;
                c.text = br_heat_name[type];
            }
        }
        else if (page == 2u) {
            static const char *const label[4] = {"DRUM SNAP", "BPM", "SWING", "MONITOR"};
            c.label = label[k]; c.value = !k ? br_part[BR_DRUM_PART].sound.drum_snap : k == 1u ? br_bpm : k == 2u ? br_part[0].swing : br_mon.on;
            if (k == 1u) {
                c.low = BR_BPM_MIN; c.high = BR_BPM_MAX;
                if (br_midi_clock_active) { c.label = "MIDI BPM"; c.value = (int32_t)br_midi_clock_bpm; }
            }
            else if (k == 2u) c.high = BR_SWING_MAX;
            else if (k == 3u) { c.high = 1; c.text = c.value ? "ON" : "OFF"; }
        } else if (page == 3u) {
            c.label = mix_label[k]; c.value = br_part[k].sound.pan;
        } else if (page == 4u) {
            static const char *const label[4] = {"SOURCE", "THRESHOLD", "RATIO", "MAKEUP"};
            c.label = label[k];
            c.value = !k ? br_master.comp.source : k == 1u ? br_master.comp.threshold : k == 2u ? br_master.comp.ratio : br_master.comp.makeup;
            if (!k) {
                uint32_t source = br_master.comp.source < BR_COMP_SOURCE_COUNT ?
                    br_master.comp.source : BR_COMP_OFF;
                c.high = BR_COMP_DRUM_1;                      /* OFF, ALL, TRACK 1-3, KICK DRUM */
                c.value = (int32_t)source;
                c.text = br_comp_source_name[source];
            }
            else if (k == 1u) c.high = 48;
            else if (k == 2u) { c.high = 7; c.text = br_comp_ratio_name[br_master.comp.ratio]; }
            else c.high = 24;
        } else {
            static const char *const label[4] = {"ATTACK", "HOLD", "RELEASE", "MIX"};
            c.label = label[k];
            c.value = !k ? br_master.comp.attack : k == 1u ? br_master.comp.hold : k == 2u ? br_master.comp.release : br_master.comp.mix;
        }
    } else if (br_ui.page == BR_PAGE_LFO) {
        {
            const volatile br_lfo_config_t *l = &m->lfo[page];
            static const char *const label[4] = {"WAVE", "RATE", "TARGET", "DEPTH"};
            c.label = label[k]; c.value = k == 0u ? l->wave : k == 1u ? l->rate : k == 2u ? l->target : l->depth;
            if (!k) { c.high = BR_WAVE_COUNT - 1; c.text = br_wave_name[l->wave]; }
            if (k == 2u) { c.high = (int32_t)br_target_ui_count() - 1; c.value = (int32_t)br_target_ui_ordinal(l->target); c.text = br_target_ui_name(l->target); }
        }
    } else if (br_ui.page == BR_PAGE_ROUTE) {
        if (br_sound_mod_page()) {
            static const char *const mod_plaits[4] = {"TIMBRE", "PITCH", "MORPH", ""};
            c.label = mod_plaits[k];
            c.value = !k ? (int32_t)v->timbre_mod : k == 1u ?
                (int32_t)v->fm_mod : k == 2u ? (int32_t)v->morph_mod : 64;
        } else if (br_gate_page()) {
            c.label = br_drum_voice_label(v->drum_voice_mask, k);
            c.value = br_part[BR_DRUM_PART].lane_level[k];
            c.high = 127;
        } else {
            const volatile br_route_config_t *r = &m->route[br_patch_slot()];
            static const char *const label[4] = {"SOURCE", "TARGET", "AMOUNT", "ENABLE"};
            c.label = label[k]; c.value = k == 0u ? (int32_t)r->source : k == 1u ? (int32_t)r->target : k == 2u ? r->amount : (int32_t)r->enabled;
            if (!k) { c.high = (int32_t)BR_SOURCE_UI_COUNT - 1; c.value = (int32_t)br_source_ui_ordinal(r->source); c.text = br_source_ui_name(r->source); }
            else if (k == 1u) { c.high = (int32_t)br_target_ui_count() - 1; c.value = (int32_t)br_target_ui_ordinal(r->target); c.text = br_target_ui_name(r->target); }
            else if (k == 2u) c.low = -127;
            else { c.high = 1; c.text = c.value ? "ON" : "OFF"; }
        }
    } else if (br_seq_step_edit() || (br_ui.page == BR_PAGE_SEQ && !page)) {
        static const char *const label[4] = {"NOTE", "LENGTH", "VELOCITY", "CONDITION"};
        c.label = label[k];
        uint32_t lock = br_seq_step_edit();
        uint32_t default_lane = kit ? lane : 0u;
        if (!k) {
            c.value = lock ? (int32_t)br_step_note_get(p, at, lane) :
                (int32_t)br_ui.seq_default[p][default_lane].note;
            if (kit) { c.low = 36; c.high = 84; }
        } else if (k == 1u) {
            c.value = lock ? (int32_t)br_step_length_get(p, at, lane) :
                (int32_t)br_ui.seq_default[p][default_lane].length;
            c.low = kit ? 0 : 1; c.high = BR_STEP_LENGTH_MAX;
        } else if (k == 2u) {
            c.value = lock ? (int32_t)br_step_velocity_get(p, at, lane) :
                (int32_t)br_ui.seq_default[p][default_lane].velocity;
            c.low = 0; c.high = 127;
        } else {
            c.value = lock ? (int32_t)br_step_condition_get(p, at, lane) :
                (int32_t)br_ui.seq_default[p][default_lane].condition;
            c.low = 0; c.high = BR_STEP_COND_COUNT - 1;
        }
    } else if (br_ui.page == BR_PAGE_SEQ && page == 1u) {
        static const char *const label[4] = {"STEPS", "SPEED", "QUANTIZE", ""};
        c.label = label[k];
        c.value = k == 0u ? br_part[br_ui.part].length : k == 1u ?
            br_part[br_ui.part].division : k == 2u ? br_seq_record_quantize[br_ui.part] : 0;
        c.low = k == 0u ? 1 : 0;
        c.high = k == 0u ? BR_STEPS : k == 1u ? 2 : k == 2u ? BR_TIME_QUANT_COUNT - 1 : 0;
        if (k == 1u) c.text = br_speed_name[br_part[br_ui.part].division];
        else if (k == 2u) c.text = br_time_quant_name[br_seq_record_quantize[br_ui.part]];
    } else if (br_ui.page == BR_PAGE_PUNCH) {
        static const char *const first[4] = {"LOOP", "ECHO TIME", "FILTER", "TREMOLO"};
        static const char *const second[4] = {"", "FEEDBACK", "AMOUNT", "DEPTH"};
        static const char *const loop[4] = {"1 BAR", "1/2 BAR", "1/4 BAR", "1/8 BAR"};
        static const char *const echo[4] = {"1/32", "1/16", "1/8", "DOT 1/8"};
        static const char *const shape[4] = {"LOW PASS", "HIGH PASS", "LP SWEEP", "HP SWEEP"};
        static const char *const trem[4] = {"1/4", "1/8", "1/16", "1/32"};
        const br_punch_cfg_t *u = &br_punch_cfg;
        c.label = page ? second[k] : first[k];
        if (!page) {
            c.high = 3;
            c.value = k == 0u ? u->loop_len : k == 1u ? u->echo_time : k == 2u ? u->filter_shape : u->trem_rate;
            c.text = k == 0u ? loop[u->loop_len & 3u] : k == 1u ? echo[u->echo_time & 3u] :
                     k == 2u ? shape[u->filter_shape & 3u] : trem[u->trem_rate & 3u];
        } else if (k) {
            c.high = 31;
            c.value = k == 1u ? u->echo_fb : k == 2u ? u->filter_amount : u->trem_depth;
        }
    } else if (br_retrig_page()) {
        static const char *const rates[8] = {"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
        uint32_t step = br_retrig_editing() ? br_ui.retrig_step : br_ui.cursor;
        uint32_t packed = step < BR_STEPS ? br_seq_kit_retrig[step][br_ui.lane] : 0u;
        c.label = k == 0u ? "RETRIGS" : k == 1u ? "SPACING" : "";
        c.low = 0; c.high = 7;
        c.value = !k ? packed & 7u : (packed >> 3u) & 7u;
        if (!k && !c.value) c.text = "OFF";
        if (k == 1u) c.text = rates[c.value];
    } else if (kit) {
    } else {
        static const char *const label[4] = {"MODE", "RATE", "OCTAVES", "GATE"};
        const volatile br_part_t *a = &br_part[p];
        c.label = label[k];
        c.value = !k ? a->arp_mode : k == 1u ? a->arp_rate : k == 2u ? a->arp_octaves : a->arp_gate;
        c.low = k >= 2u ? 1 : 0;
        c.high = !k ? BR_ARP_MODES - 1 : k == 1u ? BR_ARP_RATES - 1 : k == 2u ? BR_ARP_MAX_OCTAVES : 127;
        if (!k) c.text = br_arp_mode_name[a->arp_mode < BR_ARP_MODES ? a->arp_mode : 0u];
        else if (k == 1u) c.text = br_arp_rate_name[a->arp_rate < BR_ARP_RATES ? a->arp_rate : 0u];
    }
    return c;
}

#include "instrument_encoder.h"

/* Only relative, continuously adjustable values gain speed. Navigation and
 * musically discrete pitch/step choices keep one value per detent. */
static br_encoder_accel_t br_encoder_speed[5]; /* four controls plus SELECT */
static uint32_t br_encoder_context = 0xFFFFFFFFu;
static uint32_t br_encoder_edit_mode = 0xFFFFFFFFu;

static void br_encoder_reset(void)
{
    memset(br_encoder_speed, 0, sizeof br_encoder_speed);
    br_encoder_context = 0xFFFFFFFFu;
}

static void br_encoder_sync(void)
{
    uint32_t context = br_ui.page | br_ui.screen[br_ui.page] << 4 |
        br_ui.part << 8 | br_part[br_ui.part].sound.preset << 10 |
        br_ui.lane << 17 | br_ui.cursor << 20 | br_ui.step_bank << 26 |
        ((fm1_in.buttons >> BTN_SEQ) & 1u) << 31;
    if (context != br_encoder_context || br_ui.step_edit_active != br_encoder_edit_mode) {
        memset(br_encoder_speed, 0, sizeof br_encoder_speed);
        br_encoder_context = context;
        br_encoder_edit_mode = br_ui.step_edit_active;
    }
}

static int32_t br_control_input(uint32_t k, int32_t turn)
{
    /* An earlier knob in this same scan may have selected a new lane/step. */
    br_encoder_sync();
    br_control_t c = br_control(k);
    uint32_t screen = br_ui.screen[br_ui.page];
    uint32_t precise = c.text || c.high - c.low < 32 || br_ui.page == BR_PAGE_SCALE ||
        (br_seq_step_edit() && (k == 0u || k == 3u)) ||
        (br_ui.page == BR_PAGE_SEQ && (!screen ? (k == 0u || k == 3u) : !k));
    if (precise) {
        memset(&br_encoder_speed[k], 0, sizeof br_encoder_speed[k]);
        return turn;
    }
    return br_encoder_accel_step(&br_encoder_speed[k], turn, fm1_ms);
}

static void br_load_preset(uint32_t p, uint32_t preset)
{
    if (p >= BR_PARTS || !br_preset_allowed_on_part(p, preset)) {
        br_message("PRESET ROLE LOCKED");
        return;
    }
    br_part[p].revision++;
    br_part[p].sound.preset = preset;
    RING_PUBLISH(); br_part[p].revision++;
    if (br_ui.screen[br_ui.page] >= br_screen_count()) br_ui.screen[br_ui.page] = 0;
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
    br_ui.redraw = 1;
}

/* SEL sound bank (synth tracks). Browsing puts the highlighted entry on the
 * track so the keys and a running sequence play it; leaving restores the
 * track unless an entry was loaded. */
/* In the spare pool, so the audio path's RAM layout is unchanged. */
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static br_sel_t br_sel __attribute__((section(".pool"), aligned(4)));
#else
static br_sel_t br_sel;
#endif
static uint32_t br_sel_browse(void)
{
    return br_ui.page == BR_PAGE_SCALE && !br_ui.screen[BR_PAGE_SCALE] && br_ui.part < BR_PARTS;
}
/* The keyboard plays the selected track on these pages, and the octave buttons move it (on the mixer
 * screen they are MUTE and SOLO). The drum mixer ignores the physical keyboard. */
static uint32_t br_page_plays_keys(void)
{
    uint32_t page = br_ui.page;
    if (br_retrig_page()) return 0u;
    if (br_sel_browse()) return 1u;
    if (br_gate_page()) return 0u;
    return page == BR_PAGE_HOME || page == BR_PAGE_PATTERN || page == BR_PAGE_FX || page == BR_PAGE_ENV ||
           page == BR_PAGE_LFO || page == BR_PAGE_ROUTE || page == BR_PAGE_MIX;
}
static void br_sel_show_now(void)
{
    uint32_t p = br_ui.part;
    br_part[p].revision++;
    br_sel_show(&br_sel, p, &br_part[p].sound, &br_part[p].mod);
    RING_PUBLISH(); br_part[p].revision++;
    br_ui.redraw = 1;
}
static void br_sel_move(int32_t turn)
{
    uint32_t p = br_ui.part;
    if (!turn || p >= BR_PARTS) return;
    if (p == BR_DRUM_PART) {
        int32_t at = ((int32_t)br_sel.cursor[p] + turn % (int32_t)BR_KIT_BANK_COUNT) % (int32_t)BR_KIT_BANK_COUNT;
        br_sel.cursor[p] = (uint8_t)(at < 0 ? at + (int32_t)BR_KIT_BANK_COUNT : at);
        if (br_sel.cursor[p] < br_sel.top[p]) br_sel.top[p] = br_sel.cursor[p];
        else if (br_sel.cursor[p] >= br_sel.top[p] + BR_SEL_ROWS)
            br_sel.top[p] = br_sel.cursor[p] - (BR_SEL_ROWS - 1u);
    } else {
        br_sel.cursor[p] = (uint8_t)br_sel_wrap(br_sel.cursor[p], turn);
        br_sel.top[p] = (uint8_t)br_sel_top(br_sel.top[p], br_sel.cursor[p]);
    }
    br_sel_show_now();
}
static void br_sel_close(void)
{
    uint32_t p = br_sel.part;
    if (!br_sel.active) return;
    br_part[p].revision++;
    br_sel_leave(&br_sel, &br_part[p].sound, &br_part[p].mod);
    RING_PUBLISH(); br_part[p].revision++;
    br_ui.redraw = 1;
}
/* SEL pressed again while browsing loads the highlighted preset (putting it on the track first if it
 * has not been auditioned yet). Once it is loaded, SEL returns to SOUND. */
static uint32_t br_sel_confirm(void)
{
    uint32_t p = br_ui.part;
    char text[25];
    if (!br_sel_browse() || br_sel.loaded) return 0u;
    if (!br_sel.shown) br_sel_show_now();
    if (!br_sel_load(&br_sel, &br_part[p].sound, &br_part[p].mod)) return 0u;
    br_text_copy(text, "LOADED ", sizeof text);
    br_text_append(text, br_sel_entry_name(p, br_sel.cursor[p]), sizeof text);
    br_message(text);
    br_ui.redraw = 1;
    return 1u;
}
/* Called every tick. A visit ends, and the track goes back, when the page or
 * track changes, or SAVE is pressed so a project never stores an audition. */
static void br_sel_housekeeping(uint32_t buttons)
{
    if (!br_sel.active) return;
    if ((buttons & (1u << BTN_SAVE)) || br_ui.page != BR_PAGE_SCALE || br_ui.part != br_sel.part)
        BR_FOREGROUND_MUTATE(br_sel_close());
}

static uint32_t br_melodic_preset_count(void)
{
    uint32_t preset, count = 0;
    for (preset = 0; preset < BR_PRESET_COUNT; ++preset)
        count += br_preset_allowed_on_part(0u, preset) != 0;
    return count;
}

static uint32_t br_melodic_preset_at(uint32_t index)
{
    uint32_t preset;
    for (preset = 0; preset < BR_PRESET_COUNT; ++preset) {
        if (!br_preset_allowed_on_part(0u, preset)) continue;
        if (!index--) return preset;
    }
    return br_default_part_preset(0u);
}

static uint32_t br_next_melodic_preset(uint32_t current, int32_t turn)
{
    uint32_t preset, count = br_melodic_preset_count(), index = 0;
    int32_t at = 0;
    if (!count) return br_default_part_preset(0u);
    for (preset = 0; preset < BR_PRESET_COUNT; ++preset) {
        if (!br_preset_allowed_on_part(0u, preset)) continue;
        if (preset == current) { index = (uint32_t)at; break; }
        ++at;
    }
    at = ((int32_t)index + turn % (int32_t)count) % (int32_t)count;
    if (at < 0) at += (int32_t)count;
    return br_melodic_preset_at((uint32_t)at);
}

static uint32_t br_next_kit_preset(uint32_t current, int32_t turn)
{
    static const uint32_t kit_preset[] = {
        BR_KIT_BRAIDS,
#if BR_ENABLE_PLAITS
        BR_KIT_PLAITS,
#endif
        BR_KIT_808, BR_KIT_909,
    };
    uint32_t i, count = (uint32_t)(sizeof kit_preset / sizeof kit_preset[0]);
    int32_t index = 0;
    for (i = 0; i < count; ++i) if (kit_preset[i] == current) { index = (int32_t)i; break; }
    index = (index + turn % (int32_t)count) % (int32_t)count;
    if (index < 0) index += (int32_t)count;
    return kit_preset[index];
}

/* ALGORITHM selects tracks, except that KIT LEVEL keeps its lane focus and
 * synth PATCH pages cycle only among the three synth tracks. */
static void br_algorithm_encoder_turn(int32_t turn)
{
    if (!turn || br_gate_page()) return;
    if (br_ui.page == BR_PAGE_ROUTE && br_ui.part < BR_SYNTH_PARTS) {
        int32_t part = ((int32_t)br_ui.part + turn % (int32_t)BR_SYNTH_PARTS) % (int32_t)BR_SYNTH_PARTS;
        if (part < 0) part += BR_SYNTH_PARTS;
        br_ui.part = (uint32_t)part;
    } else {
        br_ui.part = (uint32_t)br_clip((int32_t)br_ui.part + turn, 0, BR_PARTS - 1);
    }
    br_ui.cursor = br_ui.step_bank = 0; br_ui.redraw = 1;
    if (br_ui.screen[br_ui.page] >= br_screen_count()) br_ui.screen[br_ui.page] = 0;
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
}

/* Called by dinghy's PRESETS encoder dispatcher. The kit selector lives on
 * HOME; SEQ cycles the active lane's three sound choices. */
static void br_preset_encoder_turn(int32_t turn)
{
    uint32_t p = br_gate_page() ? BR_DRUM_PART : br_ui.part;
    if (!turn || p >= BR_PARTS) return;
    if (br_sel_browse()) { br_sel_move(br_clip(turn, -4, 4)); return; }
    br_clamp_ui_lane();
    if (p != BR_DRUM_PART) {
        uint32_t preset = br_next_melodic_preset(br_part[p].sound.preset, turn);
        if (!br_preset_allowed_on_part(p, preset)) return;
        br_remember();
        br_load_preset(p, preset);
        return;
    }
    if (br_ui.page == BR_PAGE_SEQ || br_gate_page() || br_retrig_page()) {
        if (br_retrig_editing()) return;
        uint32_t lane = br_ui.lane;
        uint32_t old_choice;
        int32_t next_choice;
        if (lane >= BR_DRUM_LANES) lane = br_ui.lane = 0u;
        old_choice = br_drum_lane_choice(br_part[p].sound.drum_voice_mask, lane);
        if (old_choice >= 3u) old_choice = 0u;
        next_choice = ((int32_t)old_choice + turn % 3) % 3;
        if (next_choice < 0) next_choice += 3;
        if ((uint32_t)next_choice == old_choice) return;
        br_remember();
        br_part[p].revision++;
        br_part[p].sound.drum_voice_mask = br_drum_set_lane_choice(
            br_part[p].sound.drum_voice_mask, lane, (uint32_t)next_choice);
        RING_PUBLISH(); br_part[p].revision++;
        br_ui.edit_key = 0xFFFFFFFFu;
        br_ui.redraw = 1;
        br_message(br_drum_voice_label(br_part[p].sound.drum_voice_mask, lane));
        return;
    }
    if (br_ui.page == BR_PAGE_HOME) {
        uint32_t preset = br_next_kit_preset(br_part[p].sound.preset, turn);
        br_remember();
        br_load_preset(p, preset);
        return;
    }
    br_message("KIT: HOME OR SEQ");
}

static void br_midi_channel_set(uint32_t destination, uint8_t channel);

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void br_control_turn(uint32_t k, int32_t turn)
{
    uint32_t page = br_ui.screen[br_ui.page], selected = br_ui.part;
    br_control_t c = br_control(k);
    uint32_t global_swing = br_ui.page == BR_PAGE_MIX && page == 2u && k == 2u;
    uint32_t at = br_gate_page() ? BR_DRUM_PART : (br_ui.page == BR_PAGE_MIX && page == 2u && k == 0u) ? BR_DRUM_PART : global_swing ? 0u : br_ui.page == BR_PAGE_MIX && (!page || page == 3u) ? k : selected;
    br_part_t *p = &br_part[at];
    volatile br_sound_t *s = &p->sound;
    volatile br_mod_config_t *m = &p->mod;
    volatile br_fx_config_t *f = &p->fx;
    int32_t v = br_clip(c.value + turn, c.low, c.high);
    uint32_t key = br_ui.page * 256u + page * 16u + selected * 4u + k;
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) return;
#endif
    if (br_retrig_page()) {
        if (k >= 2u || !br_retrig_editing() || v == c.value) return;
        uint32_t step = br_ui.retrig_step, lane = br_ui.retrig_lane;
        uint32_t packed = br_seq_kit_retrig[step][lane];
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        p->revision++;
        br_seq_kit_retrig[step][lane] = (uint8_t)(!k ? (packed & 56u) | (uint32_t)v : (packed & 7u) | ((uint32_t)v << 3u));
        RING_PUBLISH(); p->revision++;
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms; br_ui.redraw = 1u;
        return;
    }
    if (br_sel_browse()) {                    /* on the sounds list knob 1 moves the cursor too */
        if (!k) { br_sel_move(br_clip(turn, -4, 4)); br_sel.knob_ms = fm1_ms | 1u; }
        return;
    }
    if (!c.label[0]) return;
    if (br_seq_step_edit()) {
        uint32_t part = br_ui.step_press_part, step = br_ui.step_press_step;
        uint32_t lane = br_ui.step_press_lane, kit = br_ui.step_press_kit;
        br_part_t *target = &br_part[part];
        if (!br_step_active(part, step, lane, kit) || v == c.value) return;
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
        target->revision++;
        if (!k) {
            br_step_note_set(part, step, lane, (uint32_t)v);
        } else if (k == 1u) br_step_length_set(part, step, lane, (uint32_t)v);
        else if (k == 2u) br_step_velocity_set(part, step, lane, (uint32_t)v);
        else br_step_condition_set(part, step, lane, (uint32_t)v);
        RING_PUBLISH(); target->revision++;
        br_ui.redraw = 1u;
        return;
    }
    if (br_ui.page == BR_PAGE_SEQ && !page) {
        uint32_t part = br_ui.part;
        uint32_t lane = br_is_kit(br_part[part].sound.preset) ? br_ui.lane : 0u;
        if (v == c.value) return;
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
        if (!k) {
            br_ui.seq_default[part][lane].note = (uint8_t)v;
            if (part != BR_DRUM_PART) br_ui.note[part] = (uint32_t)v;
        } else if (k == 1u) br_ui.seq_default[part][lane].length = (uint16_t)v;
        else if (k == 2u) br_ui.seq_default[part][lane].velocity = (uint8_t)v;
        else br_ui.seq_default[part][lane].condition = (uint8_t)v;
        br_ui.redraw = 1u;
        return;
    }
    if (br_ui.page == BR_PAGE_MIX && page == 2u && k == 1u && br_midi_clock_active) return;
    if (br_ui.page == BR_PAGE_MIX && (page == 7u || page == 8u)) {
        if (v == c.value) return;
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        if (page == 7u && k == 3u) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT && defined(FM1_DEVICE)
            if ((v == 1) != (br_midi_clock_enabled != 0u)) br_engine_clock_source_changed(fm1_ticks());
#endif
            br_midi_clock_enabled = (uint8_t)(v == 1);
            br_midi_clock_out = (uint8_t)(v == 2);
        }
        else br_midi_channel_set(page == 7u ? k : 3u + k, (uint8_t)v);
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms; br_ui.redraw = 1u;
        return;
    }
    if (br_gate_page() && br_ui.lane != k) br_gate_select_lane(k);
    if (br_ui.page == BR_PAGE_MIX && !page && br_ui.part != k) {
        br_ui.part = k; br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
    }
    if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
    br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
    p->revision++;
    if (br_ui.page == BR_PAGE_MIX && page != 0u && page != 3u) br_master.revision++;
    if (br_ui.page == BR_PAGE_HOME) {
        if (!page && s->preset == BR_KIT_PLAITS) {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->plaits_harmonics = (uint32_t)v;
            else if (k == 2u) s->color = (uint32_t)v;
            else s->gate = (uint32_t)v;
        } else if (!page && (s->preset == BR_KIT_808 || s->preset == BR_KIT_909)) {
            if (!k) s->tune = v;
            else if (k == 3u) s->gate = (uint32_t)v;
        } else if (!page && s->preset == BR_KIT_BRAIDS) {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->color = (uint32_t)v;
            else if (k == 3u) s->gate = (uint32_t)v;
        } else if (!page && br_is_plaits(s->preset) && !br_is_kit(s->preset)) {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->plaits_harmonics = (uint32_t)v;
            else if (k == 2u) s->color = (uint32_t)v;
            else s->plaits_aux = (uint32_t)v;
        } else if (br_home_filter_page()) {
            if (!k) s->filter = (uint32_t)v;
            else if (k == 1u) s->filter_resonance = (uint32_t)v;
            else if (k == 3u) s->filter_keytrack = (uint32_t)v;
            else { m->target = BR_TARGET_FILTER; m->amount = v; }
        } else if (br_sound_mod_page()) {
            if (!k) s->timbre_mod = (uint32_t)v;
            else if (k == 1u) s->fm_mod = (uint32_t)v;
            else if (k == 2u && br_is_plaits(s->preset)) s->morph_mod = (uint32_t)v;
        } else {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->color = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_ENV) {
        if (!page) {
            if (!k) s->attack = (uint32_t)v; else if (k == 1u) s->decay = (uint32_t)v;
            else if (k == 2u) s->sustain = (uint32_t)v; else s->release = (uint32_t)v;
        } else if (page == 1u) {
            if (!k) m->attack = (uint32_t)v; else if (k == 1u) m->decay = (uint32_t)v;
            else if (k == 2u) m->sustain = (uint32_t)v; else m->release = (uint32_t)v;
        } else {
            if (!k) m->amount = v; else if (k == 1u) m->target = br_target_ui_value((uint32_t)v);
            else if (k == 2u) m->loop = (uint32_t)v; else m->retrigger = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_FX) {
        if (page == BR_FX_SENDS) {
            if (!k) s->drive = (uint32_t)v; else if (k == 1u) s->delay = (uint32_t)v;
            else if (k == 2u) f->mod_mix = (uint32_t)v; else s->reverb = (uint32_t)v;
        } else if (page == BR_FX_DRIVE) {
            if (!k) f->drive_mode = (uint32_t)v; else if (k == 1u) f->drive_gain = (uint32_t)v;
            else if (k == 2u) f->drive_tone = (uint32_t)v; else s->drive = (uint32_t)v;
        } else if (page == BR_FX_DELAY) {
            if (!k) f->delay_engine = (uint32_t)v;
            else if (k == 1u) f->delay_time = br_fx_delay_note_store((uint32_t)v);
            else if (k == 2u) f->delay_feedback = (uint32_t)v; else s->delay = (uint32_t)v;
        } else if (page == BR_FX_REVERB) {
            if (k == 3u) s->reverb = (uint32_t)v;
            else {
                uint32_t i;
                for (i = 0u; i < BR_PARTS; ++i) {
                    if (i != at) br_part[i].revision++;
                    if (!k) br_part[i].fx.reverb_engine = br_reverb_ui_id[(uint32_t)v];
                    else if (k == 1u) br_part[i].sound.space = (uint32_t)v;
                    else br_part[i].fx.reverb_tone = (uint32_t)v;
                    if (i != at) { RING_PUBLISH(); br_part[i].revision++; }
                }
            }
        } else {
            if (!k) f->mod_engine = (uint32_t)v; else if (k == 1u) f->mod_rate = (uint32_t)v;
            else if (k == 2u) f->mod_depth = (uint32_t)v; else f->mod_mix = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_MIX) {
        if (!page) s->level = (uint32_t)v;
        else if (page == 1u) {
            if (!k) br_master.fx.filter = (uint32_t)v;
            else if (k == 1u) br_master.fx.resonance = (uint32_t)v;
            else if (k == 2u) br_master.fx.env_depth = v;
            else br_master.fx.smooth = (uint32_t)v;
        } else if (page == 6u) {
            if (!k) br_master.fx.heat_type = (uint32_t)v;
            else if (k == 1u) br_master.fx.heat_amount = (uint32_t)v;
        } else if (page == 2u) {
            if (!k) s->drum_snap = (uint32_t)v;
            else if (k == 1u) br_bpm = (uint32_t)v;
            else if (k == 2u) p->swing = (uint32_t)v;
            else if (k == 3u) br_monitor_set((uint32_t)v);
        } else if (page == 3u) s->pan = (uint32_t)v;
        else if (page == 4u) {
            if (!k) br_master.comp.source = (uint32_t)v;
            else if (k == 1u) br_master.comp.threshold = (uint32_t)v;
            else if (k == 2u) br_master.comp.ratio = (uint32_t)v;
            else br_master.comp.makeup = (uint32_t)v;
        } else {
            if (!k) br_master.comp.attack = (uint32_t)v;
            else if (k == 1u) br_master.comp.hold = (uint32_t)v;
            else if (k == 2u) br_master.comp.release = (uint32_t)v;
            else br_master.comp.mix = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_LFO) {
        {
            volatile br_lfo_config_t *l = &m->lfo[page];
            if (!k) l->wave = (uint32_t)v; else if (k == 1u) l->rate = (uint32_t)v;
            else if (k == 2u) l->target = br_target_ui_value((uint32_t)v); else l->depth = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_ROUTE) {
        if (br_sound_mod_page()) {
            if (!k) s->timbre_mod = (uint32_t)v;
            else if (k == 1u) s->fm_mod = (uint32_t)v;
            else if (k == 2u) s->morph_mod = (uint32_t)v;
        } else if (br_gate_page()) {
            p->lane_level[k] = (uint32_t)v;
            br_ui.gate_bank = (uint32_t)v / 16u;
        } else {
            volatile br_route_config_t *r = &m->route[br_patch_slot()];
            if (!k) r->source = br_source_ui_value((uint32_t)v); else if (k == 1u) r->target = br_target_ui_value((uint32_t)v);
            else if (k == 2u) r->amount = v; else r->enabled = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_SEQ && page == 1u) {
        if (!k) { p->length = (uint32_t)v; if (br_ui.cursor >= (uint32_t)v) { br_ui.cursor = (uint32_t)v - 1u; br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS; } }
        else if (k == 1u) p->division = (uint32_t)v;
        else if (k == 2u) br_seq_record_quantize[selected] = (uint8_t)v;
    } else if (br_ui.page == BR_PAGE_PUNCH) {
        br_punch_cfg_t *u = &br_punch_cfg;
        if (!page) {
            if (!k) {
                u->loop_len = (uint8_t)v;
                /* Publish the edit while LOOP is held as well as on key changes. */
                RING_PUBLISH();
                br_punch_loop_len = u->loop_len & 3u;
            } else if (k == 1u) u->echo_time = (uint8_t)v;
            else if (k == 2u) u->filter_shape = (uint8_t)v; else u->trem_rate = (uint8_t)v;
        } else if (k == 1u) u->echo_fb = (uint8_t)v;
        else if (k == 2u) u->filter_amount = (uint8_t)v;
        else if (k == 3u) u->trem_depth = (uint8_t)v;
    } else {
        if (!k) p->arp_mode = (uint32_t)v; else if (k == 1u) p->arp_rate = (uint32_t)v;
        else if (k == 2u) p->arp_octaves = (uint32_t)v; else p->arp_gate = (uint32_t)v;
    }
    RING_PUBLISH(); p->revision++;
    if (br_ui.page == BR_PAGE_MIX && page != 0u && page != 3u) {
        RING_PUBLISH(); br_master.revision++;
    }
    br_ui.redraw = 1;
}


static void br_seq_press(void)
{
    br_ui.seq_pressed = 1; br_ui.seq_turned = 0;
    br_ui.seq_was_active = br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ];
    br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0;
    br_ui.redraw = 1;
}
static void br_seq_release(void)
{
    if (!br_ui.seq_turned && br_ui.seq_was_active && br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ]) {
        /* OCT−/+ now navigate banks; a second SEQ press opens settings. */
        br_ui.screen[BR_PAGE_SEQ] = 1u;
        br_ui.edit_key = 0xffffffffu;
    }
    br_ui.seq_pressed = 0;
    br_ui.redraw = 1;
}
static void br_length_turn(int32_t turn)
{
    br_part_t *p = &br_part[br_ui.part];
    char text[25], value[12];
    if (!br_ui.seq_turned) br_remember();
    br_ui.seq_turned = 1;
    p->revision++;
    p->length = (uint32_t)br_clip((int32_t)p->length + turn, 1, BR_STEPS);
    RING_PUBLISH(); p->revision++;
    if (br_ui.cursor >= p->length) br_ui.cursor = p->length - 1u;
    br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS;
    br_text_copy(text, "LENGTH ", sizeof text); br_number(value, (int32_t)p->length);
    br_text_append(text, value, sizeof text); br_text_append(text, " STEPS", sizeof text);
    br_message(text);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void br_save_button_action(void)
{
    if (br_session_save_blocked()) {
        br_message(br_session_legacy_migration_pending() ? "MIGRATION REQUIRED" : "SESSION INVALID");
        return;
    }
    if (br_playing) {
        br_message("STOP BEFORE SAVE");
        return;
    }
    if (!br_quiescent_transaction_begin()) {
        br_message("SAVE BUSY");
        return;
    }
    if (!br_save_capture_copy() || br_engine_pipeline_uncontained_fault_get()) {
        br_quiescent_transaction_end();
        if (!br_engine_pipeline_uncontained_fault_get()) br_message("SESSION INVALID");
        return;
    }
    br_save_pending = 1u;
    br_quiescent_transaction_end();
    br_message("SAVING WHEN SILENT");
}

static void br_buttons(uint32_t buttons)
{
    uint32_t b;
    for (b = 0; b < 14u; b++) if ((buttons >> b) & 1u) {
        switch (b) {
        case BTN_HOME: br_open_module(BR_PAGE_HOME); break;
        case BTN_ENV: br_open_module(BR_PAGE_ENV); break;
        case BTN_FX: br_open_module(BR_PAGE_FX); break;
        case BTN_GLO: br_open_module(BR_PAGE_MIX); break;
        case BTN_SEQ:
#ifdef BR_PATTERN_SONG_EXPERIMENT
            if (br_ui.page == BR_PAGE_PS) {
                br_ui.ps_held_key = 0u;
                br_message("HOLD KEY: REC/PLAY"); break;
            }
#endif
            br_seq_press(); break;
        case BTN_ARP: br_open_module(BR_PAGE_PATTERN); break;
        case BTN_SEL:
#ifdef BR_PATTERN_SONG_EXPERIMENT

#endif
            if (br_sel_browse() && br_sel.loaded) br_open_module(BR_PAGE_HOME);
            else if (!br_sel_confirm()) br_open_module(BR_PAGE_SCALE);
            break;
        case BTN_LFO:
            if (br_ui.page == BR_PAGE_ROUTE && !br_gate_page() && !br_is_kit(br_part[br_ui.part].sound.preset))
                br_select_screen(1);
            else br_open_module(BR_PAGE_LFO);
            break;
        case BTN_EDIT:
            br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 3u;
            br_ui.edit_key = 0xffffffffu;
            br_gate_select_lane(br_ui.lane); br_ui.redraw = 1u;
            break;
        case BTN_PLAY:
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
            if (br_midi_clock_enabled) {
                br_engine_midi_transport(0xFCu);
                if (!br_playing) br_message("WAIT MIDI START");
                break;
            }
#endif
            br_playing ^= 1u; break;
        case BTN_REC:
            br_recording ^= 1u;
            if (!br_recording) br_record_note_cancel_all();
            br_message(br_recording ? "RECORD ARMED" : "RECORD OFF");
            break;
        case BTN_SAVE: /* handled in its own muted transaction */
            break;
        case BTN_OCT_DOWN: case BTN_OCT_UP:
#ifdef BR_PATTERN_SONG_EXPERIMENT
            if (br_ui.page == BR_PAGE_PS) break;
#endif
            if (br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]) {
                br_mixer_octave_action(b);
            } else if ((br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ]) || br_retrig_page()) {
                if (br_retrig_editing()) break;
                uint32_t bank = br_ui.step_bank;
                if (b == BTN_OCT_DOWN && bank) --bank;
                if (b == BTN_OCT_UP && bank + 1u < BR_BANKS) ++bank;
                if (bank != br_ui.step_bank) {
                    br_ui.step_bank = bank;
                    br_ui.cursor = bank * BR_BANK_STEPS + br_ui.cursor % BR_BANK_STEPS;
                    br_ui.edit_key = 0xffffffffu;
                    br_ui.redraw = 1u;
                }
            } else if (br_gate_page()) {
                br_mixer_octave_action(b);
            } else if (br_ui.page == BR_PAGE_SEQ) {
                /* The sequencer settings page has no OCT action. */
            } else if (br_ui.page == BR_PAGE_PUNCH) {
                if (b == BTN_OCT_UP) br_select_screen(1); else br_ui.screen[BR_PAGE_PUNCH] = 0u;
            } else if (br_is_kit(br_part[br_ui.part].sound.preset))
                br_message("KIT: HOME PAGE 2");
            else br_ui.octave = (int8_t)br_clip(br_ui.octave + (b == BTN_OCT_DOWN ? -1 : 1), -2, 2);
            break;
        }
        br_ui.redraw = 1;
    }
}

/* The 27 physical keys cover a little over two octaves. Keep their keyboard
 * classification and position in ROM tables so the repeated UI scan does
 * not divide and walk all preceding keys for every key. Values beyond the
 * physical range retain the historical periodic behavior for callers that
 * probe invalid indices. */
static const uint8_t br_key_black[27] = {
    0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0
};
static const uint8_t br_key_position[27] = {
    0, 0, 1, 1, 2, 2, 3, 4, 3, 5, 4, 6, 7, 5, 8, 6, 9, 7, 10, 11, 8, 12, 9, 13, 14, 10, 15
};

static int br_black_key(uint32_t k)
{
    if (k < 27u) return br_key_black[k];
    return (0x54Au >> ((53u + k) % 12u)) & 1u;
}
static uint32_t br_key_place(uint32_t k)
{
    if (k < 27u) return br_key_position[k];
    uint32_t i, place = 0, black = br_black_key(k);
    for (i = 0; i < k; i++) if (br_black_key(i) == (int)black) place++;
    return place;
}

#define BR_STEP_EDIT_HOLD_MS 350u
#define BR_STEP_NO_PRESS 0xFFFFFFFFu

static uint32_t br_step_press_pending(void)
{
    return br_ui.step_press_key != BR_STEP_NO_PRESS;
}

static uint32_t br_seq_grid_page(void)
{
    return br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ];
}

static uint32_t br_step_active(uint32_t part, uint32_t step, uint32_t lane, uint32_t kit)
{
    uint32_t word = br_part[part].step[step];
    return kit ? ((BR_STEP_HIT(word) >> lane) & 1u) : ((word & BR_STEP_ON) != 0u);
}

static void br_step_edit_end(void)
{
    if (br_ui.step_edit_active) br_ui.edit_key = 0xffffffffu;
    br_ui.step_edit_active = 0u;
    br_ui.step_press_key = BR_STEP_NO_PRESS;
    br_ui.step_press_active = 0u;
    br_fill_active = 0u;
    br_ui.redraw = 1u;
}

static void br_step_trigger_toggle(uint32_t part, uint32_t step, uint32_t lane,
                                   uint32_t kit, uint32_t was_active);
/* A copied step is independent of its source, so later edits cannot alter it. */
typedef struct {
    uint32_t word;
    uint16_t length;
    uint8_t valid, kit, note, velocity, condition, retrig, kit_note, kit_pitch;
    int8_t micro;
} br_step_clipboard_t;
static br_step_clipboard_t br_step_clipboard;

static void br_step_clipboard_action(uint32_t paste)
{
    uint32_t p = br_ui.step_press_part, step = br_ui.step_press_step;
    uint32_t lane = br_ui.step_press_lane, kit = br_ui.step_press_kit;
    br_step_clipboard_t *c = &br_step_clipboard;
    /* Suppress the usual release toggle, including for empty targets. */
    br_ui.step_edit_active = 1u;
    if (!paste) {
        if (!br_step_active(p, step, lane, kit)) { br_message("EMPTY STEP"); return; }
        c->word = br_part[p].step[step]; c->kit = (uint8_t)kit;
        c->length = (uint16_t)br_step_length_get(p, step, lane);
        c->note = (uint8_t)br_step_note_get(p, step, lane);
        c->velocity = (uint8_t)br_step_velocity_get(p, step, lane);
        c->condition = (uint8_t)br_step_condition_get(p, step, lane);
        c->micro = (int8_t)br_step_micro_get(p, step, lane);
        if (kit) {
            c->retrig = br_seq_kit_retrig[step][lane];
            c->kit_note = br_seq_kit_note[step][lane];
            c->kit_pitch = br_seq_kit_pitch[step][lane];
            c->word = (c->word >> lane & 1u) | ((c->word >> (lane + 8u) & 1u) << 8);
        }
        c->valid = 1u; br_message("NOTE COPIED"); return;
    }
    if (!c->valid) { br_message("NOTHING COPIED"); return; }
    if (c->kit != kit) { br_message("INCOMPATIBLE NOTE"); return; }
    br_remember(); br_part[p].revision++;
    if (kit) {
        uint32_t word = br_part[p].step[step];
        word &= ~((1u << lane) | (1u << (lane + 8u)));
        word |= (c->word & 1u) << lane | ((c->word >> 8) & 1u) << (lane + 8u);
        br_part[p].step[step] = word;
        br_seq_kit_retrig[step][lane] = c->retrig;
        br_seq_kit_note[step][lane] = c->kit_note;
        br_seq_kit_pitch[step][lane] = c->kit_pitch;
    } else br_part[p].step[step] = c->word;
    br_step_length_set(p, step, lane, c->length);
    if (!kit) br_step_note_set(p, step, lane, c->note);
    br_step_velocity_set(p, step, lane, c->velocity);
    br_step_condition_set(p, step, lane, c->condition);
    br_step_micro_set(p, step, lane, c->micro);
    RING_PUBLISH(); br_part[p].revision++;
    br_ui.step_press_active = 1u; br_message("NOTE PASTED");
}
static void br_step_edit_begin(void)
{
    if (br_ui.step_press_key == BR_STEP_NO_PRESS) return;
    if (!br_ui.step_press_active) {
        br_step_trigger_toggle(br_ui.step_press_part, br_ui.step_press_step,
                               br_ui.step_press_lane, br_ui.step_press_kit, 0u);
        if (!br_step_active(br_ui.step_press_part, br_ui.step_press_step,
                            br_ui.step_press_lane, br_ui.step_press_kit)) return;
        br_ui.step_press_active = 1u;
    }
    br_ui.step_edit_active = 1u;
    if (br_ui.seq_pressed) br_ui.seq_turned = 1u;
    br_ui.redraw = 1u;
}

static void br_step_trigger_toggle(uint32_t part, uint32_t step, uint32_t lane,
                                   uint32_t kit, uint32_t was_active)
{
    br_part_t *track;
    uint32_t word, hit_bit, accent_bit;
    if (part >= BR_PARTS) return;
    track = &br_part[part];
    if (step >= track->length) return;
    br_remember();
    track->revision++;
    word = track->step[step];
    if (kit) {
        hit_bit = 1u << lane;
        accent_bit = 1u << (lane + 8u);
        if (was_active) {
            br_step_trigger_clear(part, step, lane);
            track->step[step] = word & ~(hit_bit | accent_bit);
        } else {
            br_step_trigger_init(part, step, lane);
            track->step[step] = word | hit_bit;
            br_step_note_set(part, step, lane, br_ui.seq_default[part][lane].note);
            br_step_length_set(part, step, lane, br_ui.seq_default[part][lane].length);
            br_step_velocity_set(part, step, lane, br_ui.seq_default[part][lane].velocity);
            br_step_condition_set(part, step, lane, br_ui.seq_default[part][lane].condition);
        }
    } else if (was_active) {
        br_step_trigger_clear(part, step, 0u);
        track->step[step] = 0u;
    } else {
        br_step_trigger_init(part, step, 0u);
        track->step[step] = BR_NOTE_STEP(br_ui.seq_default[part][0].note,
                                        br_ui.seq_default[part][0].velocity);
        br_step_length_set(part, step, 0u, br_ui.seq_default[part][0].length);
        br_step_velocity_set(part, step, 0u, br_ui.seq_default[part][0].velocity);
        br_step_condition_set(part, step, 0u, br_ui.seq_default[part][0].condition);
    }
    RING_PUBLISH();
    track->revision++;
    br_ui.redraw = 1u;
}

static void br_step_press_begin(uint32_t key, int held, uint32_t kit)
{
    uint32_t step = br_key_place(key) + br_ui.step_bank * BR_BANK_STEPS;
    uint32_t part = br_ui.part, lane = kit ? br_ui.lane : 0u;
    if (step >= br_part[part].length) return;
    if (br_step_press_pending()) return;
    br_ui.redraw = 1u;
    if (!held) {
        br_step_trigger_toggle(part, step, lane, kit,
                               br_step_active(part, step, lane, kit));
        return;
    }
    if (br_ui.step_press_key != BR_STEP_NO_PRESS) return;
    br_ui.step_press_key = key;
    br_ui.step_press_at = fm1_ms;
    br_ui.step_press_part = part;
    br_ui.step_press_step = step;
    br_ui.step_press_lane = lane;
    br_ui.step_press_kit = kit;
    br_ui.step_press_active = br_step_active(part, step, lane, kit);
    br_ui.step_edit_active = 0u;
}

static void br_step_press_update(uint32_t keys)
{
    uint32_t key = br_ui.step_press_key;
    if (key == BR_STEP_NO_PRESS) return;
    if (!(keys & (1u << key))) {
        if (!br_ui.step_edit_active && br_ui.step_press_active &&
            fm1_ms - br_ui.step_press_at >= BR_STEP_EDIT_HOLD_MS)
            br_step_edit_begin();
        if (!br_ui.step_edit_active)
            br_step_trigger_toggle(br_ui.step_press_part, br_ui.step_press_step,
                                   br_ui.step_press_lane, br_ui.step_press_kit,
                                   br_ui.step_press_active);
        br_step_edit_end();
        return;
    }
    if (!br_ui.step_edit_active && br_ui.step_press_active &&
        fm1_ms - br_ui.step_press_at >= BR_STEP_EDIT_HOLD_MS)
        br_step_edit_begin();
}

static uint32_t br_seq_step_edit(void)
{
    return br_seq_grid_page() && br_ui.step_edit_active;
}

/* Keep a note within its neighboring midpoints, accounting for swing. */
static void br_micro_bounds(uint32_t part, uint32_t step, int32_t *minimum, int32_t *maximum)
{
    uint32_t division = br_part[part].division > 2u ? 2u : br_part[part].division;
    uint32_t ticks = 16u >> division, length = br_part[part].length;
    uint32_t prev = step ? step - 1u : (length ? length - 1u : 0u);
    uint32_t swing = br_part[0].swing > BR_SWING_MAX ? BR_SWING_MAX : br_part[0].swing;
    uint32_t prev_ratio = (prev & 1u) ? 128u - swing : 128u + swing;
    uint32_t this_ratio = (step & 1u) ? 128u - swing : 128u + swing;
    *minimum = -(int32_t)((ticks * prev_ratio + 255u) / 256u);
    *maximum = (int32_t)((ticks * this_ratio + 255u) / 256u) - 1;
    if (*minimum < -16) *minimum = -16;
    if (*maximum > 15) *maximum = 15;
}
static void br_step_micro_shift(int32_t direction)
{
    uint32_t part, step, lane;
    int32_t old, value, minimum, maximum;
    char msg[25], amount[8];
    if (!br_step_press_pending() || !direction) return;
    if (!br_ui.step_edit_active) br_step_edit_begin();
    if (!br_ui.step_edit_active) return;
    part = br_ui.step_press_part; step = br_ui.step_press_step;
    lane = br_ui.step_press_kit ? br_ui.step_press_lane : 0u;
    br_micro_bounds(part, step, &minimum, &maximum);
    old = br_step_micro_get(part, step, lane);
    value = old;
    if ((direction < 0 && old > minimum) || (direction > 0 && old < maximum))
        value = old + (direction < 0 ? -1 : 1);
    if (value != old) {
        br_remember(); br_part[part].revision++;
        br_step_micro_set(part, step, lane, value);
        RING_PUBLISH(); br_part[part].revision++;
    }
    br_text_copy(msg, "TIMING ", sizeof msg);
    if (value > 0) br_text_append(msg, "+", sizeof msg);
    br_number(amount, value); br_text_append(msg, amount, sizeof msg);
    br_text_append(msg, "/128", sizeof msg); br_message(msg);
    br_ui.redraw = 1u;
}

static uint32_t br_step_length_visual_quanta(uint32_t part, uint32_t step,
                                             uint32_t lane)
{
    uint32_t length = br_step_length_get(part, step, lane);
    /* Kit zero preserves its natural tail; pitched zero resolves to one step. */
    if (part == BR_DRUM_PART && !length) return 0u;
    return length > BR_STEP_LENGTH_MAX ? BR_STEP_LENGTH_MAX : length;
}

static uint32_t br_step_length_coverage(uint32_t part, uint32_t step,
                                        uint32_t lane, uint32_t kit)
{
    uint32_t start = step, coverage = 0u;
    /* Recent notes cover this cell most often. A full cell is already the
     * maximum union, so older notes cannot change its pixels. */
    do {
        uint32_t active = br_step_active(part, start, lane, kit);
        uint32_t length, elapsed, remaining;
        if (active) {
            length = br_step_length_visual_quanta(part, start, lane);
            elapsed = (step - start) * 16u;
            if (length > elapsed) {
                remaining = length - elapsed;
                if (remaining >= 16u) return 16u;
                if (remaining > coverage) coverage = remaining;
            }
        }
    } while (start--);
    return coverage;
}

static void br_step_condition_text(char *text, uint32_t value)
{
    static const char *const special[] = {
        "FIRST PASS", "REPEATS", "AFTER HIT", "AFTER MISS", "NEIGHBOR", "ALONE", "FILL", "NO FILL"
    };
    if (value == BR_STEP_COND_ALWAYS) {
        br_text_copy(text, "ALWAYS", 24u);
    } else if (value <= 100u) {
        br_number(text, (int32_t)value);
        br_text_append(text, "%", 24u);
    } else if (value <= 108u) {
        br_text_copy(text, special[value - 101u], 24u);
    } else {
        uint32_t denominator, base = 109u, numerator = 1u;
        char denominator_text[8];
        for (denominator = 1u; denominator <= 8u; ++denominator) {
            if (value < base + denominator) {
                numerator = value - base + 1u;
                break;
            }
            base += denominator;
        }
        if (denominator > 8u) { br_text_copy(text, "ALWAYS", 24u); return; }
        br_number(text, (int32_t)numerator);
        br_text_append(text, ":", 24u);
        br_number(denominator_text, (int32_t)denominator);
        br_text_append(text, denominator_text, 24u);
    }
}

typedef struct {
    uint32_t start_clock;
    uint16_t generation;
    uint16_t step_frames;
    uint8_t step;
    uint8_t active;
    uint8_t midi_source;
    uint8_t midi_stale_offs;
} br_record_hold_t;
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static br_record_hold_t br_record_hold[BR_PARTS][128]
    __attribute__((section(".pool"), aligned(4)));
#else
static br_record_hold_t br_record_hold[BR_PARTS][128];
#endif
static uint16_t br_record_generation;
static uint16_t br_record_key_generation[27];

static void br_record_note_finish_at(uint32_t part, uint32_t note,
                                     uint16_t generation, const uint32_t *clock);
static void br_record_note_finish(uint32_t part, uint32_t note,
                                  uint16_t generation)
{
    br_record_note_finish_at(part, note, generation, 0);
}

/* clock, when given, is the record clock at the note's end (arp notes are
 * reported by the audio engine a little after they happen). */
static void br_record_note_finish_at(uint32_t part, uint32_t note,
                                     uint16_t generation, const uint32_t *clock)
{
    br_record_hold_t *hold;
    uint32_t step, lane = 0u, units, duration, elapsed;
    if (part >= BR_PARTS || note >= 128u) return;
    if (part == BR_DRUM_PART)
        note = br_drum_lane_note(br_drum_lane(note));
    hold = &br_record_hold[part][note];
    if (!hold->active || (generation && hold->generation != generation)) return;
    hold->active = 0u;
    hold->midi_source = 0u;
    if (hold->step >= BR_STEPS) return;
    step = hold->step;
    duration = hold->step_frames ? hold->step_frames : 1u;
    elapsed = (clock ? *clock : br_engine_record_clock(part)) - hold->start_clock;
    if (elapsed >= duration * BR_STEPS) units = BR_STEP_LENGTH_MAX;
    else {
        units = (elapsed * 16u + duration / 2u) / duration;
        if (!units) units = 1u;
        if (units > BR_STEP_LENGTH_MAX) units = BR_STEP_LENGTH_MAX;
    }
    if (part == BR_DRUM_PART) {
        uint32_t word;
        lane = br_drum_lane(note);
        word = br_part[part].step[step];
        if (!(word & (1u << lane))) return;
    } else {
        uint32_t word = br_part[part].step[step];
        if (!(word & BR_STEP_ON) || BR_STEP_NOTE(word) != note) return;
    }
    br_part[part].revision++;
    br_step_length_set(part, step, lane, units);
    RING_PUBLISH();
    br_part[part].revision++;
    br_ui.redraw = 1u;
}

/* Quantize against the time grid, then store the nearest step plus a signed
 * 1/128-note offset. Compare both sides of the loop for wraparound. */
static void br_record_quantize_event(uint32_t part, uint32_t step, uint32_t elapsed,
                                     uint32_t duration, uint32_t unit, uint32_t nominal, uint32_t mode,
                                     uint32_t *result_step, int32_t *result_micro)
{
    uint32_t length = br_part[part].length, division, ticks, even, odd, loop;
    int32_t position, target, best_delta = 0;
    uint32_t best_distance = UINT32_MAX;
    uint32_t best = step;
    static const uint8_t quantum[BR_TIME_QUANT_COUNT] = {0u, 1u, 16u, 8u, 4u, 2u, 1u};
    if (!length || length > BR_STEPS) length = BR_STEPS;
    if (step >= length) step = 0u;
    if (mode == BR_TIME_QUANT_STEP) {
        *result_step = elapsed * 2u < duration ? step : (step + 1u) % length;
        *result_micro = 0; return;
    }
    if (!unit) unit = 1u;
    if (mode >= BR_TIME_QUANT_COUNT) mode = BR_TIME_QUANT_OFF;
    division = br_part[part].division > 2u ? 2u : br_part[part].division;
    ticks = 16u >> division;
    if (!nominal) nominal = unit * ticks;
    even = nominal * (128u + (br_part[0].swing > BR_SWING_MAX ? BR_SWING_MAX : br_part[0].swing)) / 128u;
    odd = nominal * (128u - (br_part[0].swing > BR_SWING_MAX ? BR_SWING_MAX : br_part[0].swing)) / 128u;
    loop = (length / 2u) * (even + odd) + (length & 1u ? even : 0u);
    if (!loop) loop = 1u;
    position = (int32_t)((step / 2u) * (even + odd) + (step & 1u ? even : 0u) + elapsed);
    {
        uint32_t grid = quantum[mode];
        uint32_t grid_index = ((uint32_t)position * ticks + nominal * grid / 2u) /
                              (nominal * grid);
        target = (int32_t)((grid_index * grid * nominal + ticks / 2u) / ticks);
    }
    for (uint32_t candidate = 0u; candidate < length; ++candidate) {
        int32_t boundary = (int32_t)((candidate / 2u) * (even + odd) + (candidate & 1u ? even : 0u));
        for (int32_t wrap = -1; wrap <= 1; ++wrap) {
            int32_t delta = target - (boundary + wrap * (int32_t)loop);
            uint32_t distance = (uint32_t)(delta < 0 ? -delta : delta);
            if (distance < best_distance || (distance == best_distance && delta < best_delta)) {
                best_distance = distance; best_delta = delta; best = candidate;
            }
        }
    }
    *result_micro = (int32_t)(best_delta < 0 ?
        -((-best_delta * ticks + nominal / 2u) / nominal) :
          (best_delta * ticks + nominal / 2u) / nominal);
    {
        int32_t minimum, maximum;
        br_micro_bounds(part, best, &minimum, &maximum);
        if (*result_micro > maximum) {
            best_delta -= (int32_t)((best & 1u) ? odd : even);
            best = (best + 1u) % length;
        } else if (*result_micro < minimum) {
            best = best ? best - 1u : length - 1u;
            best_delta += (int32_t)((best & 1u) ? odd : even);
        }
        *result_micro = (int32_t)(best_delta < 0 ?
            -((-best_delta * ticks + nominal / 2u) / nominal) :
              (best_delta * ticks + nominal / 2u) / nominal);
        br_micro_bounds(part, best, &minimum, &maximum);
        *result_micro = br_clip(*result_micro, minimum, maximum);
    }
    *result_step = best;
}

static uint16_t br_record_note_begin_at(uint32_t part, uint32_t note,
                                        uint32_t velocity, uint32_t kit,
                                        uint32_t lane, uint32_t midi_source,
                                        const br_arp_rec_t *at);
static uint16_t br_record_note_begin(uint32_t part, uint32_t note,
                                     uint32_t velocity, uint32_t kit,
                                     uint32_t lane, uint32_t midi_source)
{
    return br_record_note_begin_at(part, note, velocity, kit, lane, midi_source, 0);
}

/* at, when given, is the sequencer position and clock captured by the audio
 * engine when an arp note started, instead of the position now. */
static uint16_t br_record_note_begin_at(uint32_t part, uint32_t note,
                                        uint32_t velocity, uint32_t kit,
                                        uint32_t lane, uint32_t midi_source,
                                        const br_arp_rec_t *at)
{
    br_record_hold_t *hold;
    br_part_t *track;
    uint32_t step, word, active;
    int32_t micro = 0;
    uint16_t token;
    if (!br_recording || part >= BR_PARTS || note >= 128u) return 0u;
    if (kit) {
        if (lane >= BR_DRUM_LANES) return 0u;
        note = br_drum_lane_note(lane);
    }
    step = br_playing ? br_step_pos[part] : br_ui.cursor;
    if (br_playing) {
        uint32_t elapsed, duration, unit;
        if (at) {
            step = at->step; elapsed = at->elapsed;
            duration = at->duration ? at->duration : 1u; unit = at->unit ? at->unit : 1u;
        } else br_engine_record_position(part, &step, &elapsed, &duration, &unit);
        br_record_quantize_event(part, step, elapsed, duration, unit,
                                 br_engine_step_duration(part, step),
                                 br_seq_record_quantize[part], &step, &micro);
    }
    if (step >= br_part[part].length || step >= BR_STEPS) return 0u;
    hold = &br_record_hold[part][note];
    if (hold->active) {
        if (hold->midi_source && hold->midi_stale_offs != 255u)
            ++hold->midi_stale_offs;
        br_record_note_finish(part, note, hold->generation);
    }
    track = &br_part[part];
    word = track->step[step];
    active = kit ? !!(word & (1u << lane)) : !!(word & BR_STEP_ON);
    br_remember();
    track->revision++;
    if (kit) {
        br_step_trigger_init(part, step, lane);
        word |= 1u << lane;
        word &= ~(1u << (lane + 8u));
        if (velocity >= 110u) word |= 1u << (lane + 8u);
        track->step[step] = word;
        br_seq_kit_velocity[step][lane] = (uint8_t)(velocity > 127u ? 127u : velocity);
    } else {
        if (!active) br_step_trigger_init(part, step, 0u);
        track->step[step] = BR_NOTE_STEP(note, velocity);
        br_seq_synth_note[part][step] = 255u;
        br_seq_synth_velocity[part][step] = 255u;
    }
    br_step_micro_set(part, step, kit ? lane : 0u, micro);
    RING_PUBLISH();
    track->revision++;
    token = ++br_record_generation;
    if (!token) token = ++br_record_generation;
    hold->start_clock = at ? at->clock : br_engine_record_clock(part);
    hold->generation = token;
    hold->step_frames = (uint16_t)br_engine_step_duration(part, step);
    hold->step = (uint8_t)step;
    hold->active = 1u;
    hold->midi_source = (uint8_t)(midi_source != 0u);
    br_ui.redraw = 1u;
    return token;
}

static void br_record_note_cancel_all(void)
{
    uint32_t part, note;
    for (part = 0u; part < BR_PARTS; ++part)
        for (note = 0u; note < 128u; ++note) {
            br_record_hold_t *hold = &br_record_hold[part][note];
            if (hold->active && hold->midi_source &&
                hold->midi_stale_offs != 255u) ++hold->midi_stale_offs;
            hold->active = 0u;
            hold->midi_source = 0u;
        }
    memset(br_record_key_generation, 0, sizeof br_record_key_generation);
}

static void br_record_note_cancel_part(uint32_t part, uint32_t clear_stale)
{
    uint32_t note;
    if (part >= BR_PARTS) return;
    for (note = 0u; note < 128u; ++note) {
        br_record_hold_t *hold = &br_record_hold[part][note];
        if (hold->active && hold->midi_source &&
            hold->midi_stale_offs != 255u) ++hold->midi_stale_offs;
        hold->active = 0u;
        hold->midi_source = 0u;
        if (clear_stale) hold->midi_stale_offs = 0u;
    }
}

void br_ui_recording_state_reset(void)
{
    memset(br_record_hold, 0, sizeof br_record_hold);
    memset(br_record_key_generation, 0, sizeof br_record_key_generation);
    br_record_generation = 0u;
}

static void br_record_note_midi_off(uint32_t part, uint32_t note)
{
    br_record_hold_t *hold;
    if (part >= BR_PARTS || note >= 128u) return;
    if (part == BR_DRUM_PART)
        note = br_drum_lane_note(br_drum_lane(note));
    hold = &br_record_hold[part][note];
    /* The MIDI routing ledger has already suppressed older note-offs. */
    hold->midi_stale_offs = 0u;
    if (hold->midi_source) br_record_note_finish(part, note, 0u);
}

/* ---- Arpeggiator and HOLD, main-loop side. The audio engine plays the
 * arp from br_arp_keys; this file decides which keys are in each set. */
static uint16_t br_arp_rec_gen[BR_SYNTH_PARTS][128];

static uint32_t br_arp_latched(uint32_t p)
{
    (void)p;
    return br_hold_on;
}

/* Physical keys currently held for track p, routed to the arp or not. */
static uint32_t br_keys_held(uint32_t p, uint32_t arp)
{
    uint32_t k, count = 0u;
    for (k = 0u; k < 27u; ++k)
        if (br_ui.held_note[k] && br_ui.held_part[k] == p && !br_ui.held_arp[k] == !arp)
            ++count;
    return count;
}

static void br_arp_publish(uint32_t p, const uint8_t *notes, uint32_t count)
{
    br_arp_keys_t *keys = &br_arp_keys[p];
    uint32_t i;
    keys->seq++;
    RING_PUBLISH();
    keys->count = (uint8_t)count;
    for (i = 0u; i < count; ++i) keys->notes[i] = notes[i];
    RING_PUBLISH();
    keys->seq++;
}

static void br_arp_add(uint32_t p, uint32_t note, uint32_t fresh)
{
    uint8_t notes[BR_ARP_MAX_NOTES];
    uint32_t count = fresh ? 0u : br_arp_keys[p].count, i;
    for (i = 0u; i < count; ++i) {
        notes[i] = br_arp_keys[p].notes[i];
        if (notes[i] == note) return;
    }
    if (count == BR_ARP_MAX_NOTES) {            /* drop the oldest */
        br_message("ARP: 8 MAX, OLDEST OUT");
        for (i = 1u; i < count; ++i) notes[i - 1u] = notes[i];
        --count;
    }
    notes[count++] = (uint8_t)note;
    br_arp_publish(p, notes, count);
}

static void br_arp_remove(uint32_t p, uint32_t note)
{
    uint8_t notes[BR_ARP_MAX_NOTES];
    uint32_t count = 0u, i;
    for (i = 0u; i < br_arp_keys[p].count; ++i)
        if (br_arp_keys[p].notes[i] != note) notes[count++] = br_arp_keys[p].notes[i];
    br_arp_publish(p, notes, count);
}

/* Latch released: keep only the keys still physically held, in press order. */
static void br_arp_release_unheld(uint32_t p)
{
    uint8_t notes[BR_ARP_MAX_NOTES];
    uint32_t count = 0u, i, k;
    if (p >= BR_SYNTH_PARTS) return;
    for (i = 0u; i < br_arp_keys[p].count; ++i)
        for (k = 0u; k < 27u; ++k)
            if (br_ui.held_arp[k] && br_ui.held_part[k] == p &&
                br_ui.held_note[k] == br_arp_keys[p].notes[i] + 1u) {
                notes[count++] = br_arp_keys[p].notes[i]; break;
            }
    br_arp_publish(p, notes, count);
}

static void br_arp_toggle(void)
{
    uint32_t p;
    if (br_ui.part == BR_DRUM_PART) { br_ui.page = BR_PAGE_PATTERN; br_ui.redraw = 1u; return; }
    br_arp_on ^= 1u;
    if (!br_arp_on) for (p = 0u; p < BR_SYNTH_PARTS; ++p) br_arp_publish(p, 0, 0u);
    br_message(br_arp_on ? "ARP ON" : "ARP OFF");
}

static uint32_t br_midi_output_destination(uint32_t part, uint32_t note)
{
    return part < BR_SYNTH_PARTS ? part : BR_SYNTH_PARTS + br_drum_lane(note);
}

/* A key's note to MIDI out on its destination's channel; velocity 0 is a note off. */
static void br_midi_output_note(uint32_t part, uint32_t note, uint32_t velocity)
{
    uint32_t channel = br_midi_channels[br_midi_output_destination(part, note)];
    if (channel) midi_send((velocity ? 0x90u : 0x80u) | (channel - 1u), note, velocity);
}

static void br_midi_output_release_destination(uint32_t destination)
{
    for (uint32_t k = 0u; k < 27u; ++k) {
        uint32_t note = br_ui.held_note[k], part = br_ui.held_part[k];
        if (br_midi_out_held[k] && note && br_midi_output_destination(part, note - 1u) == destination) {
            br_midi_output_note(part, note - 1u, 0u); br_midi_out_held[k] = 0u;
        }
        note = br_ui.sustain_note[k]; part = br_ui.sustain_part[k];
        if (br_midi_out_sustain[k] && note && br_midi_output_destination(part, note - 1u) == destination) {
            br_midi_output_note(part, note - 1u, 0u); br_midi_out_sustain[k] = 0u;
        }
    }
}

static void br_hold_release_all(void)
{
    uint32_t k;
    for (k = 0u; k < 27u; ++k) {
        uint32_t note = br_ui.sustain_note[k], p = br_ui.sustain_part[k];
        if (!note) continue;
        if (br_ui.sustain_gen[k]) br_record_note_finish(p, note - 1u, br_ui.sustain_gen[k]);
        br_engine_note_off(p, note - 1u);
        if (br_midi_out_sustain[k]) br_midi_output_note(p, note - 1u, 0u);
        br_midi_out_sustain[k] = 0u;
        br_ui.sustain_note[k] = 0u; br_ui.sustain_gen[k] = 0u;
    }
}

static void br_hold_toggle(void)
{
    uint32_t p;
    br_hold_on ^= 1u;
    if (!br_hold_on) {
        br_hold_release_all();
        for (p = 0u; p < BR_SYNTH_PARTS; ++p)
            br_arp_release_unheld(p);
    }
    br_message(br_hold_on ? "HOLD ON" : "HOLD OFF");
}

/* Arp notes reported by the audio engine while the transport runs. With REC
 * armed they are written at the position the engine captured. */
static void br_arp_record_drain(void)
{
    while (br_arp_rec_read != br_arp_rec_write) {
        uint32_t r = br_arp_rec_read;
        br_arp_rec_t e;
        e.part = br_arp_rec[r % BR_ARP_REC_SLOTS].part;
        e.note = br_arp_rec[r % BR_ARP_REC_SLOTS].note;
        e.on = br_arp_rec[r % BR_ARP_REC_SLOTS].on;
        e.clock = br_arp_rec[r % BR_ARP_REC_SLOTS].clock;
        e.step = br_arp_rec[r % BR_ARP_REC_SLOTS].step;
        e.elapsed = br_arp_rec[r % BR_ARP_REC_SLOTS].elapsed;
        e.duration = br_arp_rec[r % BR_ARP_REC_SLOTS].duration;
        e.unit = br_arp_rec[r % BR_ARP_REC_SLOTS].unit;
        RING_PUBLISH();
        br_arp_rec_read = r + 1u;
        if (!br_recording || e.part >= BR_SYNTH_PARTS || e.note >= 128u) continue;
        if (e.on)
            br_arp_rec_gen[e.part][e.note] =
                br_record_note_begin_at(e.part, e.note, 100u, 0u, 0u, 0u, &e);
        else if (br_arp_rec_gen[e.part][e.note]) {
            br_record_note_finish_at(e.part, e.note, br_arp_rec_gen[e.part][e.note], &e.clock);
            br_arp_rec_gen[e.part][e.note] = 0u;
        }
    }
}

/* Shift key held: show the punch settings; released: put the old page back.
 * The effect itself is a single word the audio path reads. */
static void br_punch_changed(void)
{
    if (br_punch_ks.shift && br_ui.page != BR_PAGE_PUNCH) {
        br_punch_return_page = br_ui.page; br_punch_return_screen = br_ui.screen[br_ui.page];
        br_ui.page = BR_PAGE_PUNCH; br_ui.screen[BR_PAGE_PUNCH] = 0u;
    } else if (!br_punch_ks.shift && br_ui.page == BR_PAGE_PUNCH) {
        br_ui.page = br_punch_return_page; br_ui.screen[br_ui.page] = br_punch_return_screen;
    }
    if (br_ui.page == BR_PAGE_PUNCH && br_punch_ks.active == BR_PUNCH_LOOP)
        br_ui.screen[BR_PAGE_PUNCH] = 0u; /* LOOP has no second-page parameter. */
    RING_PUBLISH();
    br_punch_loop_len = br_punch_cfg.loop_len & 3u;
    br_punch_effect = br_punch_ks.active;
    br_ui.edit_key = 0xFFFFFFFFu; br_ui.redraw = 1u;
}

/* Preserve the sequencer, browser and drum mixer's distinct key roles. */
static uint32_t br_punch_page(void)
{
    return br_ui.page == BR_PAGE_PUNCH ||
        (br_ui.page != BR_PAGE_SCALE && br_page_plays_keys());
}

static void br_key_down(uint32_t key, int held)
{
    if (br_retrig_page()) {
        uint32_t place = br_key_place(key);
        if (br_retrig_editing()) return;
        if (br_black_key(key)) {
            if (place < BR_DRUM_LANES) { br_ui.lane = place; br_ui.redraw = 1u; }
        } else {
            uint32_t step = br_ui.step_bank * BR_BANK_STEPS + place;
            if (held && step < br_part[BR_DRUM_PART].length &&
                (BR_STEP_HIT(br_part[BR_DRUM_PART].step[step]) & (1u << br_ui.lane))) {
                br_ui.retrig_key = key + 1u; br_ui.retrig_step = br_ui.cursor = step;
                br_ui.retrig_lane = br_ui.lane; br_ui.retrig_part = br_ui.part; br_ui.redraw = 1u;
            }
        }
        return;
    }
    if (br_gate_page()) return; /* Mixer keys never select lanes, change levels or audition. */
    if (br_punch_page() && br_punch_key(&br_punch_ks, key, 1u)) {
        br_punch_changed();
        if (!held && br_punch_key(&br_punch_ks, key, 0u)) br_punch_changed();
        return;
    }
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) {
        uint32_t place = br_key_place(key);
        if (br_black_key(key)) {
            br_ui.ps_held_key = key + 1u; br_ui.ps_held_used = 0u;
            if (!held) { if (!br_ps_select(place)) br_message("PATTERN FAILED"); br_ui.ps_held_key = 0u; }
        }
        br_ui.redraw = 1u; return;
    }
#endif
    br_clamp_ui_lane();
    uint32_t p = br_ui.part, kit = br_is_kit(br_part[p].sound.preset);
    uint32_t note = (uint32_t)(53 + (int32_t)key + 12 * br_ui.octave), place = br_key_place(key);
    if (kit) note = br_drum_lane_note(key % BR_DRUM_LANES);
    if (br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && kit) {
        if (!br_black_key(key)) {
            br_step_press_begin(key, held, 1u);
            br_ui.redraw = 1; return;
        }
        if (place < BR_DRUM_LANES && !br_step_press_pending()) { br_ui.lane = place; br_ui.redraw = 1; }
        return;
    }
    if (br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && !kit) {
        if (!br_black_key(key)) {
            br_step_press_begin(key, held, 0u);
            br_ui.redraw = 1; return;
        }
        br_ui.note[p] = note;
        br_ui.seq_default[p][0].note = (uint8_t)br_clip((int32_t)note, 0, 127);
        br_ui.redraw = 1u;
    }
    /* Sequencer keys still edit their targets; only HOME and
     * ARP may trigger notes from the physical keyboard. MIDI is separate. */
    if (br_sel_browse()) {
        if (!br_sel.shown) br_sel_show_now();
    } else if (!br_page_plays_keys()) return;
    if (br_arp_on && !kit && p < BR_SYNTH_PARTS && key < 27u) {
        /* A latched set is replaced by the first key of a new gesture. */
        br_arp_add(p, note, br_arp_latched(p) && !br_keys_held(p, 1u));
        br_ui.held_note[key] = note + 1u; br_ui.held_part[key] = p;
        br_ui.held_arp[key] = 1u;
        if (!held) br_key_up(key);
        return;
    }
    /* HOLD sustains until the next key pressed with no other key down. */
    if (br_hold_on && !br_keys_held(p, 0u)) br_hold_release_all();
    br_ui.held_note[key] = note + 1u; br_ui.held_part[key] = p;
    br_engine_note_on(p, note, 100);
    br_midi_out_held[key] = br_midi_channels[br_midi_output_destination(p, note)] != 0u;
    if (br_midi_out_held[key]) br_midi_output_note(p, note, 100u);
    if (key < 27u)
        br_record_key_generation[key] = br_record_note_begin(
            p, note, 100u, kit, kit ? br_drum_lane(note) : 0u, 0u);
    if (!held) br_key_up(key);
}
static void br_key_up(uint32_t key)
{
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.ps_held_key == key + 1u) {
        if (br_ui.page == BR_PAGE_PS && !br_ui.ps_held_used && !br_ps_select(br_key_place(key))) br_message("PATTERN FAILED");
        br_ui.ps_held_key = br_ui.ps_held_used = 0u; br_ui.redraw = 1u;
    }
#endif
    if (br_ui.retrig_key == key + 1u) { br_ui.retrig_key = 0u; br_ui.redraw = 1u; }
    if (br_punch_key(&br_punch_ks, key, 0u)) { br_punch_changed(); return; }
    if (br_ui.step_press_key == key) br_step_press_update(0u);
    uint32_t note = br_ui.held_note[key], p = br_ui.held_part[key];
    if (!note) return;
    if (key < 27u && br_ui.held_arp[key]) {
        br_ui.held_arp[key] = 0u;
        br_ui.held_note[key] = 0u;
        if (!br_arp_latched(p)) br_arp_remove(p, note - 1u);
        br_ui.redraw = 1;
        return;
    }
    if (br_hold_on && key < 27u) {
        if (br_ui.sustain_note[key] && (br_ui.sustain_note[key] != note || br_ui.sustain_part[key] != p)) {
            uint32_t old = br_ui.sustain_note[key] - 1u, op = br_ui.sustain_part[key];
            if (br_ui.sustain_gen[key]) br_record_note_finish(op, old, br_ui.sustain_gen[key]);
            br_engine_note_off(op, old);
            if (br_midi_out_sustain[key]) br_midi_output_note(op, old, 0u);
        }
        br_ui.sustain_note[key] = note; br_ui.sustain_part[key] = p;
        br_midi_out_sustain[key] = br_midi_out_held[key]; br_midi_out_held[key] = 0u;
        br_ui.sustain_gen[key] = br_record_key_generation[key];
        br_record_key_generation[key] = 0u;
        br_ui.held_note[key] = 0u;
        br_ui.redraw = 1;
        return;
    }
    if (key < 27u && br_record_key_generation[key])
        br_record_note_finish(p, note - 1u, br_record_key_generation[key]);
    if (key < 27u) br_record_key_generation[key] = 0u;
    br_engine_note_off(p, note - 1u);
    if (br_midi_out_held[key]) br_midi_output_note(p, note - 1u, 0u);
    br_midi_out_held[key] = 0u;
    br_ui.held_note[key] = 0;
    if (br_recording && !br_playing && br_ui.page == BR_PAGE_SEQ && p == br_ui.part &&
        !br_is_kit(br_part[p].sound.preset))
        br_ui.cursor = (br_ui.cursor + 1u) % br_part[p].length;
    br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS;
    br_ui.redraw = 1;
}

static void br_midi_channel_set(uint32_t destination, uint8_t channel)
{
    /* Held keys are released on the channel they were sent on. */
    if (destination < 7u && channel != br_midi_channels[destination] && br_midi_channels[destination] != 0u)
        br_midi_output_release_destination(destination);
    uint32_t affected = br_midi_route_set(&br_midi_routes, br_midi_channels, destination, channel);
    for (uint32_t p = 0u; p < BR_PARTS; ++p) if (affected & (1u << p)) {
        br_engine_part_off(p);
        br_record_note_cancel_part(p, 1u);
    }
}

static void br_midi_before_session_restore(void)
{
    uint32_t parts = 0u;
    for (uint32_t i = 0u; i < 7u; ++i) br_midi_output_release_destination(i);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT && defined(FM1_DEVICE)
    br_engine_clock_source_changed(fm1_ticks());
#endif
    for (uint32_t i = 0u; i < BR_MIDI_ROUTE_NOTES; ++i) {
        uint8_t targets = br_midi_routes.note[i].targets;
        parts |= targets & 7u;
        if (targets & BR_MIDI_DRUM_BITS) parts |= 8u;
    }
    br_midi_route_invalidate(&br_midi_routes, 15u);
    for (uint32_t p = 0u; p < BR_PARTS; ++p) if (parts & (1u << p)) {
        br_engine_part_off(p);
        br_record_note_cancel_part(p, 1u);
    }
}

static void br_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint32_t channel = (status & 15u) + 1u, cmd = status & 0xF0u;
    uint8_t targets;
#if defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE


#endif
#if defined(FM1_DEVICE) && defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (status == 0xF2u || status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu) {
        br_engine_clock_midi_event(status, midi_last_timestamp, midi_last_generation);
        if (status == 0xF2u && br_midi_clock_enabled) br_message("SPP NOT SUPPORTED");
        return;
    }
#endif
    if (status == 0xFAu || status == 0xFBu || status == 0xFCu) {
        br_engine_midi_transport(status); return;
    }
    if (status >= 0xF0u) return;
    if (cmd == 0x90u && d2) targets = br_midi_route_on(&br_midi_routes, br_midi_channels, channel, d1);
    else if (cmd == 0x80u || cmd == 0x90u) targets = br_midi_route_off(&br_midi_routes, channel, d1);
    else targets = br_midi_route_targets(br_midi_channels, channel);
    if (cmd == 0x90u || cmd == 0x80u) {
        for (uint32_t i = 0u; i < 7u; ++i) if (targets & (1u << i)) {
            uint32_t p = i < BR_SYNTH_PARTS ? i : BR_DRUM_PART;
            uint32_t note = i < BR_SYNTH_PARTS ? d1 : br_drum_lane_note(i - BR_SYNTH_PARTS);
            if (p == BR_DRUM_PART && !br_is_kit(br_part[p].sound.preset)) continue;
            if (cmd == 0x90u && d2) {
                br_engine_note_on(p, note, d2);
                if (br_recording) (void)br_record_note_begin(p, note, d2, p == BR_DRUM_PART,
                                                             p == BR_DRUM_PART ? i - BR_SYNTH_PARTS : 0u, 1u);
            } else {
                br_record_note_midi_off(p, note);
                br_engine_note_off(p, note);
            }
        }
    } else if (cmd == 0xC0u && d1 < BR_PRESET_COUNT) {
        for (uint32_t p = 0u; p < BR_PARTS; ++p) {
            uint32_t selected = p == BR_DRUM_PART ? (targets & BR_MIDI_DRUM_BITS) : (targets & (1u << p));
            if (!selected) continue;
            if (br_preset_allowed_on_part(p, d1)) { br_remember(); br_load_preset(p, d1); }
            else br_message("PRESET ROLE LOCKED");
        }
    } else if (cmd == 0xB0u) {
        if (d1 == 123u || d1 == 120u) {
            uint32_t parts = (targets & 7u) | ((targets & BR_MIDI_DRUM_BITS) ? 8u : 0u);
            br_midi_route_invalidate(&br_midi_routes, (uint8_t)parts);
            br_midi_route_forget_channel(&br_midi_routes, (uint8_t)channel);
            for (uint32_t p = 0u; p < BR_PARTS; ++p) if (parts & (1u << p)) {
                br_engine_part_off(p); br_record_note_cancel_part(p, 1u);
            }
        } else if (d1 == 74u || d1 == 71u || d1 == 7u) {
            for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) if (targets & (1u << p)) {
                br_part[p].revision++;
                if (d1 == 74u) br_part[p].sound.timbre = d2;
                else if (d1 == 71u) br_part[p].sound.color = d2;
                else br_part[p].sound.level = d2;
                RING_PUBLISH(); br_part[p].revision++;
            }
            if (targets & BR_MIDI_DRUM_BITS) {
                br_part[BR_DRUM_PART].revision++;
                if (d1 == 7u) {
                    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
                        if (targets & (1u << (lane + BR_SYNTH_PARTS))) br_part[BR_DRUM_PART].lane_level[lane] = d2;
                } else if (d1 == 74u) br_part[BR_DRUM_PART].sound.timbre = d2;
                else br_part[BR_DRUM_PART].sound.color = d2;
                RING_PUBLISH(); br_part[BR_DRUM_PART].revision++;
            }
        }
    }
    br_ui.redraw = 1;
}

static void br_leds_mode(uint32_t save_mode)
{
    static const uint8_t button[BR_PAGE_COUNT] = {BTN_HOME, BTN_ENV, BTN_FX, BTN_GLO, BTN_SEQ, BTN_ARP, BTN_SEL, BTN_LFO, BTN_EDIT, BTN_HOME
#ifdef BR_PATTERN_SONG_EXPERIMENT
, BTN_SEQ
#endif
};
    /* UI-only chase: restart at the left edge when a new effect becomes active. */
    static uint32_t punch_previous, punch_started;
    uint32_t punch_active = br_punch_ks.shift ? br_punch_ks.active : BR_PUNCH_NONE;
    if (punch_active != punch_previous) { punch_previous = punch_active; punch_started = fm1_ms; }
    uint32_t punch_phase = ((fm1_ms - punch_started) / 100u) % 3u;
    uint32_t k, kit = br_is_kit(br_part[br_ui.part].sound.preset);
    uint32_t grid = (br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ]) || br_retrig_page();
    br_clamp_ui_lane();
    for (k = 0; k < 14u; k++) {
        uint32_t page_button = br_ui.page == BR_PAGE_ROUTE && !br_gate_page() ? BTN_LFO : button[br_ui.page];
        fm1_led_level_t level = k == page_button ? FM1_LED_BRIGHT : FM1_LED_DIM;
        if ((k == BTN_OCT_DOWN || k == BTN_OCT_UP) &&
            ((br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]) || br_gate_page())) level = FM1_LED_BRIGHT;
        else if ((k == BTN_OCT_DOWN || k == BTN_OCT_UP) && grid) {
            if (br_step_press_pending()) {
                uint32_t part = br_ui.step_press_part;
                uint32_t lane = br_ui.step_press_kit ? br_ui.step_press_lane : 0u;
                int32_t micro = br_step_micro_get(part, br_ui.step_press_step, lane), minimum, maximum;
                br_micro_bounds(part, br_ui.step_press_step, &minimum, &maximum);
                level = (k == BTN_OCT_DOWN ? micro > minimum : micro < maximum) ? FM1_LED_BRIGHT : FM1_LED_OFF;
            } else level = (k == BTN_OCT_DOWN ? br_ui.step_bank > 0u : br_ui.step_bank + 1u < BR_BANKS) ?
                FM1_LED_BRIGHT : FM1_LED_OFF;
        }
        else if ((k == BTN_OCT_DOWN || k == BTN_OCT_UP) &&
                 br_ui.page == BR_PAGE_SEQ && br_ui.screen[BR_PAGE_SEQ]) level = FM1_LED_OFF;
#ifdef BR_PATTERN_SONG_EXPERIMENT
        else if ((k == BTN_OCT_DOWN || k == BTN_OCT_UP) && br_ui.page == BR_PAGE_PS)
            level = FM1_LED_OFF;
#endif
        else if ((k == BTN_OCT_DOWN || k == BTN_OCT_UP) && !br_page_plays_keys()) level = FM1_LED_OFF;
        else if (k == BTN_PLAY) level = br_playing ? FM1_LED_MID : FM1_LED_OFF;
        else if (k == BTN_REC) level = br_recording ? FM1_LED_BRIGHT : FM1_LED_DIM;
        else if (k == BTN_SAVE) level = br_save_pending ? FM1_LED_BRIGHT : FM1_LED_DIM;
        else if (!kit && k == BTN_OCT_UP && br_ui.octave > 0) level = FM1_LED_BRIGHT;
        else if (!kit && k == BTN_OCT_DOWN && br_ui.octave < 0) level = FM1_LED_BRIGHT;
        else if (!kit && k == BTN_OCT_DOWN && br_ui.octave == 2) level = FM1_LED_OFF;
        else if (!kit && k == BTN_OCT_UP && br_ui.octave == -2) level = FM1_LED_OFF;
        if (save_mode) {
            if (k == BTN_SAVE) level = FM1_LED_BRIGHT;
            else if (k == BTN_OCT_UP) level = FM1_LED_OFF;
            else if (k == BTN_HOME) level = FM1_LED_OFF;
            else if (k == BTN_OCT_DOWN) level = save_mode == 1u ? FM1_LED_OFF : FM1_LED_BRIGHT;
        }
        if (!save_mode && ((k == BTN_ARP && br_arp_on && br_ui.part != BR_DRUM_PART) || (k == BTN_HOME && br_hold_on)))
            level = (fm1_ms / 250u) & 1u ? FM1_LED_BRIGHT : FM1_LED_DIM;
        fm1_led_level(k, level);
    }
    fm1_led_level(FM1_LED_PLAY_GREEN, br_playing ? FM1_LED_OFF : FM1_LED_MID);
    for (k = 0; k < 27u; k++) {
        uint32_t level;
        if (save_mode) {
            level = FM1_LED_OFF;
#ifdef BR_PATTERN_SONG_EXPERIMENT
        } else if (br_ui.page == BR_PAGE_PS) {
            uint32_t place = br_key_place(k);
            level = br_black_key(k) ? ((br_ps_state.used & (1u << place)) ? FM1_LED_BRIGHT : FM1_LED_DIM) : FM1_LED_OFF;
            if (br_black_key(k) && place == br_ps_state.active) level = (fm1_ms / 250u) & 1u ? FM1_LED_BRIGHT : FM1_LED_MID;
#endif
        } else if (grid) {
            uint32_t place = br_key_place(k);
            if (br_black_key(k)) {
                level = kit ? (place < BR_DRUM_LANES && place == br_ui.lane ? FM1_LED_BRIGHT : FM1_LED_OFF) :
                    ((fm1_in.notes >> k) & 1u ? FM1_LED_BRIGHT : FM1_LED_OFF);
            } else {
                place += br_ui.step_bank * BR_BANK_STEPS;
                uint32_t active = place < br_part[br_ui.part].length && (kit ?
                    ((BR_STEP_HIT(br_part[br_ui.part].step[place]) >> br_ui.lane) & 1u) :
                    (br_part[br_ui.part].step[place] & BR_STEP_ON) != 0u);
                level = active ? (br_retrig_page() ? FM1_LED_MID : FM1_LED_BRIGHT) : FM1_LED_OFF;
                if (br_retrig_page() && active && ((br_retrig_editing() && place == br_ui.retrig_step) ||
                    (br_playing && place == br_step_pos[br_ui.part]))) level = FM1_LED_BRIGHT;
            }
        } else if (br_punch_ks.shift) {
            /* The three lit levels are UI brightness steps 2/4, 3/4 and 4/4. */
            level = br_black_key(k) ? FM1_LED_BRIGHT : FM1_LED_DIM;
            uint32_t group = br_punch_group(k);
            if (group && group == punch_active) {
                static const uint8_t first[5] = {0u, 1u, 8u, 13u, 20u};
                uint32_t ordinal = (k - first[group]) / 2u;
                level = FM1_LED_DIM + (5u + punch_phase - ordinal) % 3u;
            }
        } else if (br_sel_browse()) {
            /* A slow swell, both rows together. The black keys (the top row) peak one step below full. */
            static const uint8_t white[4] = {FM1_LED_DIM, FM1_LED_MID, FM1_LED_BRIGHT, FM1_LED_MID};
            static const uint8_t black[4] = {FM1_LED_DIM, FM1_LED_DIM, FM1_LED_MID, FM1_LED_DIM};
            uint32_t phase = (fm1_ms / 450u) & 3u;
            level = (fm1_in.notes >> k) & 1u ? FM1_LED_BRIGHT : br_black_key(k) ? black[phase] : white[phase];
        } else if (!br_page_plays_keys()) {
            level = FM1_LED_OFF;
        } else if (br_black_key(k)) {
            level = (fm1_in.notes >> k) & 1u ? FM1_LED_BRIGHT :
                br_playing && br_punch_page() && k == BR_PUNCH_SHIFT_KEY ? FM1_LED_MID : FM1_LED_DIM;
        } else {
            level = FM1_LED_BRIGHT;
        }
        fm1_led_level(KEY_LED(k), (fm1_led_level_t)level);
    }
}

static void br_leds(void) { br_leds_mode(0u); }

static void br_time_text(char *text, uint32_t samples)
{
    uint32_t ms = samples * 1000u / FS;
    if (ms < 1000u) { br_number(text, (int32_t)ms); br_text_append(text, "MS", 24); }
    else {
        uint32_t tenths = (ms + 50u) / 100u;
        char digit[2] = {(char)('0' + tenths % 10u), 0};
        br_number(text, (int32_t)(tenths / 10u)); br_text_append(text, ".", 24);
        br_text_append(text, digit, 24); br_text_append(text, "S", 24);
    }
}
static void br_rate_text(char *text, uint32_t millihz)
{
    char frac[3];
    uint32_t hundredths = (millihz + 5u) / 10u;
    br_number(text, (int32_t)(hundredths / 100u)); br_text_append(text, ".", 24);
    frac[0] = (char)('0' + hundredths / 10u % 10u); frac[1] = (char)('0' + hundredths % 10u); frac[2] = 0;
    br_text_append(text, frac, 24); br_text_append(text, "HZ", 24);
}
static void br_filter_text(char *text, int32_t value)
{
    if (value == 63 || value == 64) br_text_copy(text, "OPEN", 24);
    else {
        char amount[16]; br_text_copy(text, value < 63 ? "LP " : "HP ", 24);
        br_number(amount, ((value < 63 ? 63 - value : value - 64) * 100 + 31) / 63);
        br_text_append(text, amount, 24); br_text_append(text, "%", 24);
    }
}
static void br_attenuverter_text(char *text, int32_t value)
{
    int32_t amount;
    if (value < 64) amount = -((64 - value) * 100 + 32) / 64;
    else amount = ((value - 64) * 100 + 31) / 63;
    br_number(text, amount);
    br_text_append(text, "%", 24u);
}
/* Move to the next 0/50/100 landmark in the direction of the turn. */
static int32_t br_snap_delta(int32_t value, int32_t turn)
{
    value = br_clip(value, 0, 127);
    int32_t target = turn > 0 ? (value < 64 ? 64 : 127) : (value > 64 ? 64 : 0);
    return turn ? target - value : 0;
}
static int32_t br_filter_snap_delta(int32_t turn)
{
    return br_snap_delta((int32_t)br_master.fx.filter, turn);
}
static void br_select_turn(int32_t turn)
{
    int32_t old = (int32_t)br_master.fx.filter;
    int32_t value = br_clip(old + turn, 0, 127);
    uint32_t key = 4096u + br_ui.part;
    char message[25], text[24];
    if (value != old) {
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
        br_master.revision++;
        br_master.fx.filter = (uint32_t)value;
        RING_PUBLISH(); br_master.revision++;
    }
    br_filter_text(text, value);
    br_text_copy(message, "MASTER FILTER ", sizeof message);
    br_text_append(message, text, sizeof message);
    br_message(message);
}

static void br_step_length_text(char *text, uint32_t length)
{
    uint32_t whole, fraction;
    char number[8];
    if (!length) { br_text_copy(text, "1 STEP", 24u); return; }
    whole = length / 16u;
    fraction = length % 16u;
    if (whole) {
        br_number(text, (int32_t)whole);
        if (fraction) {
            br_text_append(text, "+", 24u);
            br_number(number, (int32_t)fraction);
            br_text_append(text, number, 24u);
            br_text_append(text, "/16", 24u);
            br_text_append(text, " STEP", 24u);
        } else br_text_append(text, " STEP", 24u);
    } else {
        br_number(text, (int32_t)fraction);
        br_text_append(text, "/16 STEP", 24u);
    }
}

static int br_widget_is(const char *a, const char *b);
static void br_control_text(uint32_t k, br_control_t *c, char *text)
{
    uint32_t page = br_ui.screen[br_ui.page];
    uint32_t kit = br_seq_step_edit() ? br_ui.step_press_kit : br_is_kit(br_part[br_ui.part].sound.preset);
    if (br_retrig_page() && !k) {
        if (c->value) br_number(text, c->value); else br_text_copy(text, "OFF", 24u);
    } else if (br_seq_step_edit() || (br_ui.page == BR_PAGE_SEQ && !page)) {
        if (!k) {
            br_note_text(text, c->value);
        } else if (k == 1u) {
            if (kit && !c->value) br_text_copy(text, "TAIL", 24u);
            else br_step_length_text(text, (uint32_t)c->value);
        }
        else if (k == 2u) br_number(text, c->value);
        else br_step_condition_text(text, (uint32_t)c->value);
    } else if (br_ui.page == BR_PAGE_HOME && !page && kit && k == 3u) {
        if (c->value >= (int32_t)BR_GATE_OPEN) br_text_copy(text, "OPEN", 24);
        else br_time_text(text, br_gate_samples((uint32_t)c->value));
    } else if (br_ui.page == BR_PAGE_HOME && !page && kit && !k &&
               (br_part[br_ui.part].sound.preset == BR_KIT_808 || br_part[br_ui.part].sound.preset == BR_KIT_909)) {
        br_number(text, c->value); br_text_append(text, " ST", 24);
    } else if (br_gate_page() && (k == 1u || k == 2u)) {
        if (k == 1u) {
            br_number(text, (c->value * 100 + 63) / 127); br_text_append(text, "%", 24);
        } else {
            char high[8];
            br_number(text, c->value * 16); br_text_append(text, "-", 24);
            br_number(high, c->value * 16 + 15 > 127 ? 127 : c->value * 16 + 15);
            br_text_append(text, high, 24);
        }
    } else if (br_ui.page == BR_PAGE_MIX && page == 3u) {
        if (c->value == 64) br_text_copy(text, "CENTER", 24);
        else {
            br_number(text, c->value < 64 ? (64 - c->value) * 100 / 64 : (c->value - 64) * 100 / 63);
            br_text_append(text, c->value < 64 ? "% L" : "% R", 24);
        }
    } else if (br_home_filter_page() && k == 0u) {
        br_filter_text(text, c->value);
    } else if (br_home_filter_page() && k == 1u) {
        br_number(text, (c->value * 100 + 63) / 127); br_text_append(text, "%", 24);
    } else if (br_sound_mod_page() && k < 3u) {
        br_attenuverter_text(text, c->value);
    } else if (br_widget_is(c->label, "OSC MIX")) {
        uint32_t preset = br_part[br_ui.part].sound.preset;
        uint32_t plaits = br_is_plaits(preset);
        if (!c->value) {
            br_text_copy(text, plaits ? "MAIN" : preset <= 8u ? "OSC1" : "BASE", 24);
        } else if (c->value == 127) {
            br_text_copy(text, plaits ? "AUX" : preset <= 8u ? "OSC2" : "PAIR", 24);
        } else {
            br_number(text, (c->value * 100 + 63) / 127);
            br_text_append(text, plaits ? "% AUX" : "%", 24);
        }
    } else if (br_ui.page == BR_PAGE_MIX && page == 4u && k == 1u) {
        br_number(text, br_comp_threshold_db((uint32_t)c->value)); br_text_append(text, " dB", 24);
    } else if (br_ui.page == BR_PAGE_MIX && page == 4u && k == 3u) {
        br_number(text, c->value); br_text_append(text, " dB", 24);
    } else if (br_ui.page == BR_PAGE_MIX && page == 4u && !k) {
        br_comp_source_text((uint32_t)c->value, text, 24u);
    } else if (br_ui.page == BR_PAGE_MIX && page == 5u && k != 3u) {
        uint32_t samples = !k ? br_comp_attack_samples((uint32_t)c->value) :
                           k == 1u ? br_comp_hold_samples((uint32_t)c->value) :
                                     br_comp_release_samples((uint32_t)c->value);
        if (samples && samples < FS / 100u) {
            uint32_t tenths = (samples * 10000u + FS / 2u) / FS;
            char digit[2] = {(char)('0' + tenths % 10u), 0};
            br_number(text, (int32_t)(tenths / 10u)); br_text_append(text, ".", 24);
            br_text_append(text, digit, 24); br_text_append(text, "MS", 24);
        } else br_time_text(text, samples);
    } else if (br_ui.page == BR_PAGE_MIX && page == 2u && k == 2u) {
        /* Position of the delayed note within a pair, in whole percent. */
        br_number(text, ((128 + c->value) * 100 + 128) / 256);
        br_text_append(text, "%", 24);
    } else if (c->text) br_text_copy(text, c->text, 24);
    else if (br_ui.page == BR_PAGE_MIX && page == 1u && !k) br_filter_text(text, c->value);
    else if (br_ui.page == BR_PAGE_MIX && page == 1u && k == 3u) { br_number(text, c->value); br_text_append(text, "%", 24); }
    else if (br_ui.page == BR_PAGE_ENV && page < 2u && k != 2u) {
        uint32_t samples = br_time_samples((uint32_t)c->value, !k ? 3u : 8u);
        br_time_text(text, samples);
    } else if (br_ui.page == BR_PAGE_FX && page == BR_FX_DELAY && k == 1u) br_time_text(text, br_fx_delay_samples((uint32_t)c->value));
    else if (br_ui.page == BR_PAGE_PUNCH && page) {
        br_number(text, (c->value * 100 + 15) / 31); br_text_append(text, "%", 24);
    }
    else if (br_ui.page == BR_PAGE_LFO && !br_sound_mod_page() && k == 1u) br_rate_text(text, br_lfo_rate_millihz((uint32_t)c->value));
    else if (br_ui.page == BR_PAGE_SCALE && k == 3u) {
        br_number(text, c->value); br_text_append(text, " ST", 24);
    } else if ((br_ui.page == BR_PAGE_MIX && ((page == 2u && k == 1u) || page == 7u || page == 8u)) ||
               (br_ui.page == BR_PAGE_PATTERN && k == 2u) ||
               (br_ui.page == BR_PAGE_SEQ && ((!page && (!k || k == 3u)) ||
                                              (page == 1u && !k) ||
                                              (page == 2u && k == 0u)))) br_number(text, c->value);
    else {
        int32_t amount = c->value * 100;
        amount = amount >= 0 ? (amount + 63) / 127 : (amount - 63) / 127;
        br_number(text, amount); br_text_append(text, "%", 24);
    }
}

#include "instrument_widgets.c"

/* Drawings describe the control's role; the input and sound mappings stay in
 * br_control/br_control_turn. Envelope pages share one graph; other controls
 * use square icons with their labels below. */
static int br_widget_is(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* Resolve destination IDs from the stored route, not the filtered menu ordinal. */
static uint32_t br_target_control_id(uint32_t k)
{
    const volatile br_mod_config_t *m = &br_part[br_ui.part].mod;
    uint32_t page = br_ui.screen[br_ui.page];
    if (br_ui.page == BR_PAGE_LFO && k == 2u) return m->lfo[page].target;
    if (br_ui.page == BR_PAGE_ROUTE && k == 1u && !br_gate_page() && !br_sound_mod_page())
        return m->route[br_patch_slot()].target;
    return BR_TARGET_COUNT;
}

/* Patch sources use the same artwork as the controls that produce them. */
static uint32_t br_source_control_id(uint32_t k)
{
    if (br_ui.page != BR_PAGE_ROUTE || k != 0u || br_gate_page() || br_sound_mod_page())
        return BR_SOURCE_COUNT;
    uint32_t source = br_part[br_ui.part].mod.route[br_patch_slot()].source;
    return br_source_ui_value(br_source_ui_ordinal(source));
}

static uint32_t br_source_widget(uint32_t source)
{
    switch (source) {
    case BR_SOURCE_OFF: return BR_WIDGET_TOGGLE;
    case BR_SOURCE_OSC: case BR_SOURCE_LFO1: case BR_SOURCE_LFO2:
    case BR_SOURCE_RANDOM:
    case BR_SOURCE_LFO1_TRI: case BR_SOURCE_LFO1_SQU:
    case BR_SOURCE_LFO2_TRI: case BR_SOURCE_LFO2_SQU: return BR_WIDGET_WAVE;
    case BR_SOURCE_GATE: return BR_WIDGET_GATE;
    case BR_SOURCE_MIX: return BR_WIDGET_MIX;
    case BR_SOURCE_AMP: case BR_SOURCE_ENV: return BR_WIDGET_ADSR;
    case BR_SOURCE_VELOCITY: return BR_WIDGET_FADER;
    case BR_SOURCE_KEY: return BR_WIDGET_POT;
    default: return BR_WIDGET_TOGGLE;
    }
}

static uint32_t br_target_widget(uint32_t target)
{
    if (!br_target_ui_available(target)) return BR_WIDGET_TOGGLE;
    switch (target) {
    case BR_TARGET_OFF: return BR_WIDGET_TOGGLE;
    case BR_TARGET_TIMBRE: return BR_WIDGET_TIMBRE;
    case BR_TARGET_HARMONICS: return BR_WIDGET_HARMONICS;
    case BR_TARGET_COLOR: return BR_WIDGET_COLOR;
    case BR_TARGET_FILTER: return BR_WIDGET_FILTER_TARGET;
    case BR_TARGET_RESONANCE: return BR_WIDGET_RESONANCE_TARGET;
    case BR_TARGET_LEVEL: return BR_WIDGET_FADER;
    case BR_TARGET_ATTACK: case BR_TARGET_DECAY: case BR_TARGET_SUSTAIN:
    case BR_TARGET_RELEASE: return BR_WIDGET_ADSR;
    case BR_TARGET_OUTPUT: return BR_WIDGET_MIX;
    case BR_TARGET_DELAY: return BR_WIDGET_DELAY;
    case BR_TARGET_REVERB: return BR_WIDGET_REVERB;
    case BR_TARGET_MOD: return BR_WIDGET_FX_ENGINE;
    default: return BR_WIDGET_POT; /* Pitch uses the tuning control. */
    }
}

static uint32_t br_target_widget_value(uint32_t target)
{
    const volatile br_sound_t *s = &br_part[br_ui.part].sound;
    if (!br_target_ui_available(target)) return 0u;
    switch (target) {
    case BR_TARGET_PITCH: return (uint32_t)br_clip(64 + s->tune * 63 / 12, 0, 127);
    case BR_TARGET_TIMBRE: return s->timbre;
    case BR_TARGET_HARMONICS: return s->plaits_harmonics;
    case BR_TARGET_COLOR: return s->color;
    case BR_TARGET_OUTPUT: return s->plaits_aux;
    case BR_TARGET_FILTER: return s->filter;
    case BR_TARGET_RESONANCE: return s->filter_resonance;
    case BR_TARGET_ATTACK: return s->attack;
    case BR_TARGET_SUSTAIN: return s->sustain;
    case BR_TARGET_RELEASE: return s->release;
    case BR_TARGET_LEVEL: return s->level;
    case BR_TARGET_DECAY: return s->decay;
    case BR_TARGET_DELAY: return s->delay;
    case BR_TARGET_REVERB: return s->reverb;
    case BR_TARGET_MOD: return br_part[br_ui.part].fx.mod_mix;
    default: return 0u;
    }
}

static uint32_t br_control_widget(uint32_t k, const br_control_t *c)
{
    uint32_t page = br_ui.screen[br_ui.page];
    uint32_t target = br_target_control_id(k), source = br_source_control_id(k);
    /* OFF source and target use the prohibition symbol; ENABLE remains a switch. */
    if (br_ui.page == BR_PAGE_ROUTE && page < 2u && !br_gate_page() &&
        k < 2u && c->text && br_widget_is(c->text, "OFF"))
        return BR_WIDGET_PATCH_OFF;
    if (target != BR_TARGET_COUNT) return br_target_widget(target);
    if (source != BR_SOURCE_COUNT) return br_source_widget(source);
    if (br_seq_step_edit() || (br_ui.page == BR_PAGE_SEQ && !page)) {
        if (k == 1u) return BR_WIDGET_GATE;
        if (k == 2u) return BR_WIDGET_FADER;
        if (k == 3u) return BR_WIDGET_CHOICE;
        return BR_WIDGET_POT;
    }
    if (br_ui.page == BR_PAGE_ENV && page < 2u) return BR_WIDGET_ADSR;
    if (br_retrig_page()) return !k ? BR_WIDGET_CHOICE : BR_WIDGET_BPM;
    if (br_ui.page == BR_PAGE_PATTERN) {
        if (!k) return BR_WIDGET_ARP_MODE;
        if (k == 1u) return BR_WIDGET_ARP_RATE;
        if (k == 3u) return BR_WIDGET_GATE;
    }
    if (br_ui.page == BR_PAGE_MIX && page == 3u) return BR_WIDGET_TRACK_PAN;
    if (br_ui.page == BR_PAGE_MIX && page == 4u && !k) return BR_WIDGET_COMP_SOURCE;
    if (br_ui.page == BR_PAGE_MIX && page == 7u && k == 3u) return BR_WIDGET_CHOICE;
    if (br_ui.page == BR_PAGE_MIX && (page == 7u || page == 8u)) return BR_WIDGET_MIDI_CHANNEL;
    if (c->high == 1 && c->text && (br_widget_is(c->text, "ON") || br_widget_is(c->text, "OFF"))) return BR_WIDGET_TOGGLE;
    if (br_gate_page()) {
        if (!k) return BR_WIDGET_ROUTE;
        if (k == 1u) return BR_WIDGET_FADER;
        if (k == 2u) return BR_WIDGET_CHOICE;
    }
    if (br_widget_is(c->label, "RESONANCE")) return BR_WIDGET_RESONANCE;
    if (br_widget_is(c->label, "BPM") || br_widget_is(c->label, "MIDI BPM")) return BR_WIDGET_BPM;
    if (br_home_filter_page() && !k) return BR_WIDGET_FILTER;
    if (br_home_filter_page() && k == 2u) return BR_WIDGET_POT_BIPOLAR;   /* envelope depth */
    if (br_sound_mod_page() && k < 3u) return BR_WIDGET_MOD_ATTEN;
    if (br_widget_is(c->label, "OSC MIX")) return BR_WIDGET_MIX;
    if (br_widget_is(c->label, "HARMONICS")) return BR_WIDGET_HARMONICS;
    if (br_widget_is(c->label, "TIMBRE")) return BR_WIDGET_TIMBRE;
    if (br_widget_is(c->label, "COLOR") || br_widget_is(c->label, "MORPH")) return BR_WIDGET_COLOR;
    if (br_ui.page == BR_PAGE_MIX && page == 1u) {
        if (!k) return BR_WIDGET_FILTER;
        return k == 2u ? BR_WIDGET_POT_BIPOLAR : BR_WIDGET_POT;
    }
    if (br_ui.page == BR_PAGE_MIX && page == 6u) return !k ? BR_WIDGET_HEAT : BR_WIDGET_AMOUNT;
    if (br_ui.page == BR_PAGE_FX) {
        if (page == BR_FX_DRIVE) return !k ? BR_WIDGET_FX_ENGINE : k == 3u ? BR_WIDGET_MIX : BR_WIDGET_POT;
        if (page >= BR_FX_DELAY) {
            if (!k) return BR_WIDGET_FX_ENGINE;
            if (page == BR_FX_REVERB && k == 1u) return BR_WIDGET_REVERB;
            if (page == BR_FX_DELAY && k == 1u) return BR_WIDGET_DELAY;
            if (page == BR_FX_DELAY && k == 2u) return BR_WIDGET_DELAY_FEEDBACK;
            if (k == 3u) return BR_WIDGET_MIX;
        }
    }
    if (br_ui.page == BR_PAGE_LFO) {
        if (br_sound_mod_page()) return k < 3u ? BR_WIDGET_MOD_ATTEN : BR_WIDGET_POT;
        if (!k) return BR_WIDGET_WAVE;
        if (k == 2u) return BR_WIDGET_ROUTE;
        if (k == 3u) return BR_WIDGET_AMOUNT;
    }
    if (br_ui.page == BR_PAGE_ROUTE && k < 2u) return BR_WIDGET_ROUTE;
    if (c->text) return BR_WIDGET_CHOICE;
    if (br_widget_is(c->label, "LEVEL") || br_widget_is(c->label, "VELOCITY")) return BR_WIDGET_FADER;
    if (br_widget_is(c->label, "MIX")) return BR_WIDGET_MIX;
    if (br_widget_is(c->label, "AMOUNT") || br_widget_is(c->label, "INTENSITY")) return BR_WIDGET_AMOUNT;
    if (br_widget_is(c->label, "SWING")) return BR_WIDGET_SWING;
    if (br_widget_is(c->label, "GATE"))
        return br_ui.page == BR_PAGE_HOME && br_is_kit(br_part[br_ui.part].sound.preset) ? BR_WIDGET_GATE : BR_WIDGET_PERCENT;
    return BR_WIDGET_POT;
}

static uint32_t br_control_widget_variant(uint32_t k, const br_control_t *c,
                                          uint32_t kind)
{
    uint32_t page = br_ui.screen[br_ui.page];
    uint32_t target = br_target_control_id(k), source = br_source_control_id(k);
    if (source == BR_SOURCE_AMP || source == BR_SOURCE_ENV) return 4u;
    if (source == BR_SOURCE_LFO1 || source == BR_SOURCE_LFO2)
        return br_part[br_ui.part].mod.lfo[source - BR_SOURCE_LFO1].wave;
    if (source == BR_SOURCE_RANDOM) return BR_WAVE_RANDOM;
    if (source == BR_SOURCE_LFO1_TRI || source == BR_SOURCE_LFO2_TRI) return BR_WAVE_TRIANGLE;
    if (source == BR_SOURCE_LFO1_SQU || source == BR_SOURCE_LFO2_SQU) return BR_WAVE_SQUARE;
    if (source == BR_SOURCE_GATE) return 1u;                    /* drawn open */
    if (source == BR_SOURCE_OSC) return BR_WAVE_SINE;
    if (kind == BR_WIDGET_ADSR) {
        if (target == BR_TARGET_ATTACK) return 0u;
        if (target == BR_TARGET_DECAY || br_ui.page == BR_PAGE_HOME) return 1u;
        if (target == BR_TARGET_SUSTAIN) return 2u;
        if (target == BR_TARGET_RELEASE) return 3u;
    }
    if (target == BR_TARGET_DELAY)
        return br_fx_delay_note_index(br_part[br_ui.part].fx.delay_time);
    if (target == BR_TARGET_MOD) return (3u << 8) | BR_MOD_CHORUS;
    if (kind == BR_WIDGET_ARP_MODE || kind == BR_WIDGET_ARP_RATE) return (uint32_t)c->value;
    if (kind == BR_WIDGET_GATE && br_ui.page == BR_PAGE_PATTERN) return 0u;
    if (kind == BR_WIDGET_GATE) return br_seq_step_edit() ||
        (br_ui.page == BR_PAGE_SEQ && !page) ? 0u : c->value >= BR_GATE_OPEN;
    if (kind == BR_WIDGET_WAVE)
        return br_ui.page == BR_PAGE_LFO ? br_part[br_ui.part].mod.lfo[page].wave : BR_WAVE_SINE;
    if (kind == BR_WIDGET_CHOICE) return (uint32_t)(c->high - c->low + 1);
    if (kind == BR_WIDGET_MIDI_CHANNEL) return (uint32_t)c->value;
    if (kind == BR_WIDGET_ROUTE) return (uint32_t)c->value;
    if (kind == BR_WIDGET_FX_ENGINE && br_ui.page == BR_PAGE_FX && page == BR_FX_DRIVE)
        return (uint32_t)c->value;                       /* family 0: the drive modes */
    if (kind == BR_WIDGET_FX_ENGINE)
        return ((br_fx_effect(page) + 1u) << 8) |
            (page == BR_FX_REVERB ? br_reverb_ui_id[(uint32_t)c->value] : (uint32_t)c->value);
    if (kind == BR_WIDGET_MOD_ATTEN && (br_home_filter_page() || br_ui.page == BR_PAGE_MIX)) return 3u;
    if (kind == BR_WIDGET_TRACK_PAN) return k;
    if (kind == BR_WIDGET_COMP_SOURCE) return (uint32_t)c->value;
    if (kind == BR_WIDGET_MONITOR) return br_mon.on;
    if (kind == BR_WIDGET_HEAT) return br_master.fx.heat_type;
    if (br_ui.page == BR_PAGE_FX && page == BR_FX_DELAY && k == 1u && kind == BR_WIDGET_DELAY)
        return (uint32_t)c->value;
    return k;
}

static uint32_t br_widget_evolving(uint32_t kind)
{
    return kind == BR_WIDGET_TIMBRE || kind == BR_WIDGET_COLOR ||
        kind == BR_WIDGET_HARMONICS || kind == BR_WIDGET_MIX ||
        kind == BR_WIDGET_AMOUNT || kind == BR_WIDGET_HEAT ||
        kind == BR_WIDGET_MOD_ATTEN;
}

static uint32_t br_widget_phase(uint32_t kind)
{
    /* Motion lives in the display loop; silent controls settle to a stable
     * picture. Eight updates per second limit LCD traffic while playing. */
    if (kind == BR_WIDGET_BPM) return br_playing ? 1u + (br_transport_phase >> 3) + ((br_global_beat & 1u) << 4) : 0u;
    if (br_widget_evolving(kind))
        return br_playing || !br_quiet_part[br_ui.part] ? (fm1_ms / 125u) & 127u : 0u;
    if (kind == BR_WIDGET_WAVE && br_ui.page == BR_PAGE_LFO) {
        return br_audio_lfo_phase(br_ui.part, br_ui.screen[BR_PAGE_LFO]);
    }
    return (fm1_ms / 100u) & 127u;
}

/* Quiet 2px outlines identify editable artwork without enclosing its labels.
 * Whole-cell edges stay aligned through clipped canvas-strip redraws. */
static void br_control_frame(int32_t x, int32_t y, int32_t w, int32_t h)
{
    br_art_rect(x, y, w, 2, BR_LINE);
    br_art_rect(x, y + h - 2, w, 2, BR_LINE);
    br_art_rect(x, y + 2, 2, h - 4, BR_LINE);
    br_art_rect(x + w - 2, y + 2, 2, h - 4, BR_LINE);
}

/* Four original-size 60px illustrations fill the 240px screen. Their outer
 * frames are omitted; knob numbers, values and labels share the same column.
 * Long values wrap at word boundaries without shrinking the pixel font. */
enum { BR_PARAM_BODY_Y = 74, BR_PARAM_ICON_Y = 12, BR_PARAM_VALUE_Y = 76 };
/* Wrap by actual glyph width; no text is silently clipped at a column edge. */
static int32_t br_param_rows(const char *text, int32_t center, int32_t y, uint16_t color)
{
    while (*text) {
        char line[64];
        uint32_t n = 0u, split = 0u;
        int32_t width = 0;
        while (text[n] && n < sizeof line - 1u) {
            int32_t advance = (int32_t)james_font_width((uint8_t)text[n]) + 1;
            if (n && width + advance - 1 > 58) break;
            width += advance;
            if (text[n] == ' ') split = n;
            ++n;
        }
        if (text[n] && split) n = split;
        for (uint32_t i = 0; i < n; ++i) line[i] = text[i];
        line[n] = 0;
        br_widget_text(center - br_widget_text_width(line) / 2, y, line, color);
        y += 10;
        text += n;
        while (*text == ' ') ++text;
    }
    return y;
}
static int32_t br_draw_param_text(int32_t center, int32_t value_y,
                                  const char *value, const char *label, uint16_t label_color)
{
    int32_t y = br_param_rows(value, center, value_y, C_WHITE);
    return br_param_rows(label, center, y, label_color) - 10;
}

static uint32_t br_active_knob(void)
{
    if (br_ui.page == BR_PAGE_PUNCH && br_punch_ks.shift && br_punch_ks.active) {
        /* LOOP has no parameter on Punch page 2; do not highlight its blank slot. */
        if (br_ui.screen[BR_PAGE_PUNCH] && br_punch_ks.active == BR_PUNCH_LOOP) return 4u;
        return br_punch_ks.active - 1u;
    }
    uint32_t key = br_ui.page * 256u + br_ui.screen[br_ui.page] * 16u + br_ui.part * 4u;
    return br_ui.edit_key >= key && br_ui.edit_key - key < 4u &&
        fm1_ms - br_ui.edit_time < 1200u ? br_ui.edit_key - key : 4u;
}

static void br_draw_knob_badge(uint32_t k, int32_t center, int32_t y, uint32_t active)
{
    static const char *const labels[4] = {"KNOB1", "KNOB2", "KNOB3", "KNOB4"};
    int32_t width = br_widget_text_width(labels[k]) + 12;
    int32_t x = center - width / 2;
    uint16_t background = active ? BR_ACC : RGB(12, 16, 18);
    cv_rect(x + 1, y, width - 2, 11, background);
    cv_rect(x, y + 1, width, 9, background);
    br_widget_text(x + 6, y + 2, labels[k], active ? C_BLACK : RGB(168, 177, 180));
}

/* Route badges describe assigned, usable connections, including zero-depth
 * assignments. Sending badges belong to a signal, not its component knobs. */
enum { BR_BADGE_LFO1 = 1u, BR_BADGE_LFO2 = 2u,
       BR_BADGE_PATCH1 = 4u, BR_BADGE_PATCH2 = 8u,
       BR_BADGE_SEND1 = 16u, BR_BADGE_SEND2 = 32u };
static uint32_t br_route_valid(uint32_t part, uint32_t slot)
{
    const volatile br_route_config_t *r = &br_part[part].mod.route[slot];
    return r->enabled && br_mod_source_active(r->source) &&
        br_mod_target_supported(br_part[part].sound.preset, r->target);
}
/* Match the audio matrix: any valid ENV patch, even at zero amount and
 * including a saved hidden slot, replaces the entire direct envelope route. */
static uint32_t br_env_direct_overridden(uint32_t part)
{
    for (uint32_t i = 0u; i < BR_ROUTES; ++i)
        if (br_part[part].mod.route[i].source == BR_SOURCE_ENV && br_route_valid(part, i))
            return 1u;
    return 0u;
}
static uint32_t br_receive_badges(uint32_t part, uint32_t target)
{
    const volatile br_mod_config_t *m = &br_part[part].mod;
    const volatile br_sound_t *s = &br_part[part].sound;
    uint32_t bits = 0u, i;
    if (!br_mod_target_supported(s->preset, target)) return 0u;
    for (i = 0; i < BR_LFOS; ++i)
        if (m->lfo[i].target == target) bits |= BR_BADGE_LFO1 << i;
    for (i = 0; i < 2u; ++i)
        if (br_route_valid(part, i) && m->route[i].target == target)
            bits |= BR_BADGE_PATCH1 << i;
    if ((target == BR_TARGET_TIMBRE && s->timbre_mod != 64u) ||
        (target == BR_TARGET_PITCH && s->fm_mod != 64u) ||
        (target == BR_TARGET_COLOR && br_is_plaits(s->preset) && s->morph_mod != 64u))
        bits |= BR_BADGE_LFO1;
    return bits;
}
static uint32_t br_send_badges(uint32_t part, uint32_t source)
{
    uint32_t bits = 0u, i;
    for (i = 0; i < 2u; ++i)
        if (br_route_valid(part, i) && br_part[part].mod.route[i].source == source)
            bits |= BR_BADGE_SEND1 << i;
    return bits;
}
static uint32_t br_control_route_badges(uint32_t k)
{
    uint32_t p = br_ui.part, page = br_ui.screen[br_ui.page], target = BR_TARGET_OFF;
    uint32_t preset = br_part[p].sound.preset, kit = br_is_kit(preset);
    if (br_ui.page == BR_PAGE_HOME) {
        if (br_home_filter_page())
            target = k == 0u ? BR_TARGET_FILTER : k == 1u ? BR_TARGET_RESONANCE : BR_TARGET_OFF;
        else if (!page) {
            if (kit && (preset == BR_KIT_808 || preset == BR_KIT_909))
                target = !k ? BR_TARGET_PITCH : BR_TARGET_OFF;
            else if (!k) target = BR_TARGET_TIMBRE;
            else if (br_is_plaits(preset) || preset == BR_KIT_PLAITS)
                target = k == 1u ? BR_TARGET_HARMONICS : k == 2u ? BR_TARGET_COLOR :
                    k == 3u && !kit ? BR_TARGET_OUTPUT : BR_TARGET_OFF;
            else if (k == 1u) target = BR_TARGET_COLOR;
        } else if (kit && k == 2u) target = BR_TARGET_PITCH;
    } else if (br_ui.page == BR_PAGE_LFO && k == 1u) {
        target = BR_TARGET_LFO1_RATE + page;              /* the LFO's own RATE input */
    } else if (br_ui.page == BR_PAGE_ENV && !page) {
        static const uint8_t targets[4] = {BR_TARGET_ATTACK, BR_TARGET_DECAY, BR_TARGET_SUSTAIN, BR_TARGET_RELEASE};
        target = targets[k];
    } else if (br_ui.page == BR_PAGE_FX && page == BR_FX_SENDS) {
        static const uint8_t targets[4] = {BR_TARGET_OFF, BR_TARGET_DELAY, BR_TARGET_MOD, BR_TARGET_REVERB};
        target = targets[k];
    } else if (br_ui.page == BR_PAGE_FX && k == 3u && page >= BR_FX_DELAY) {
        static const uint8_t targets[3] = {BR_TARGET_DELAY, BR_TARGET_REVERB, BR_TARGET_MOD};
        target = targets[br_fx_effect(page)];
    }
    return br_receive_badges(p, target);
}
static uint32_t br_badge_count(uint32_t bits)
{
    uint32_t n = 0u;
    while (bits) { n += bits & 1u; bits >>= 1; }
    return n;
}
/* Sample once per UI frame so every clipped stripe uses the same phase. */
static uint8_t br_badge_lfo_phase[BR_PARTS][BR_LFOS];
static uint8_t br_badge_lfo_depth[BR_PARTS][BR_LFOS];
static int8_t br_badge_patch_value[BR_PARTS][2];
static uint32_t br_badge_pulse_key(uint32_t part, uint32_t bits)
{
    uint32_t key = 0u;
    for (uint32_t i = 0; i < BR_LFOS; ++i) {
        if (bits & (BR_BADGE_LFO1 << i)) {
            uint32_t phase = br_badge_lfo_phase[part][i] >> 4;
            uint32_t level = phase <= 4u ? phase : 8u - phase;
            uint32_t brightness = 127u - br_badge_lfo_depth[part][i] * (4u - level) * 38u / (127u * 4u);
            key |= brightness << (i * 7u);
        }
    }
    for (uint32_t i = 0; i < 2u; ++i) {
        if (bits & ((BR_BADGE_PATCH1 << i) | (BR_BADGE_SEND1 << i))) {
            /* Neutral is halfway through the same gentle 70–100% range as LFO badges. */
            int32_t value = br_badge_patch_value[part][i];
            uint32_t brightness = (uint32_t)(108 + value * 19 / 127);
            key |= brightness << ((i + 2u) * 7u);
        }
    }
    return key;
}
/* Blend directly to the surrounding surface, including selected mixer cards. */
static uint16_t br_badge_blend(uint16_t background, uint16_t color, uint32_t amount)
{
    uint32_t inverse = 127u - amount;
    uint32_t r = (((background >> 11) & 31u) * inverse + ((color >> 11) & 31u) * amount) / 127u;
    uint32_t g = (((background >> 5) & 63u) * inverse + ((color >> 5) & 63u) * amount) / 127u;
    uint32_t b = ((background & 31u) * inverse + (color & 31u) * amount) / 127u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
/* The same route keeps its color on every track and at both endpoints. */
static uint16_t br_route_badge_color(uint32_t part, uint32_t bit)
{
    static const uint16_t colors[4] = {
        RGB(240, 72, 64), RGB(72, 160, 248),
        RGB(248, 152, 40), RGB(184, 112, 240)
    };
    (void)part;
    return colors[bit < 2u ? bit : 2u + (bit - 2u) % 2u];
}
static void br_draw_named_badge_scaled(int32_t center, int32_t y, const char *name, uint16_t color, int32_t scale)
{
    int32_t width = (br_widget_text_width(name) + 10) * scale;
    int32_t x = center - width / 2;
    cv_rect(x + scale, y, width - 2 * scale, 9 * scale, color);
    cv_rect(x, y + scale, width, 7 * scale, color);
    if (scale == 2) br_widget_text_2x(x + 5 * scale, y + scale, name, C_BLACK);
    else br_widget_text(x + 5, y + 1, name, C_BLACK);
}
static const char *const br_route_badge_name[6] = {"LFO1", "LFO2", "PATCH1", "PATCH2", "PATCH1", "PATCH2"};
static uint16_t br_route_badge_tint(uint32_t bit, uint32_t part, uint16_t background)
{
    uint16_t color = br_route_badge_color(part, bit);
    uint32_t brightness;
    if (bit < BR_LFOS)
        brightness = (br_badge_pulse_key(part, BR_BADGE_LFO1 << bit) >> (bit * 7u)) & 127u;
    else {
        uint32_t slot = (bit - 2u) % 2u;
        brightness = (br_badge_pulse_key(part, 1u << bit) >> ((slot + 2u) * 7u)) & 127u;
    }
    return br_badge_blend(background, color, brightness);
}
/* Route names identify modulation assignments without a direction icon. */
static void br_draw_route_badge_scaled(int32_t center, int32_t y, uint32_t bit, uint32_t part, uint16_t background, int32_t scale)
{
    br_draw_named_badge_scaled(center, y, br_route_badge_name[bit], br_route_badge_tint(bit, part, background), scale);
}
static void br_draw_route_badge(int32_t center, int32_t y, uint32_t bit, uint32_t part, uint16_t background)
{
    br_draw_route_badge_scaled(center, y, bit, part, background, 1);
}
static void br_draw_route_badges(int32_t center, int32_t y, uint32_t bits, uint32_t part, uint16_t background)
{
    for (uint32_t bit = 0; bit < 6u; ++bit)
        if (bits & (1u << bit)) { br_draw_route_badge(center, y, bit, part, background); y += 10; }
}

/* An unused knob has an icon slot, but no knob badge or value. */
static void br_draw_empty_param(int32_t x, int32_t y, int32_t size)
{
    for (int32_t at = 0; at < size; at += 4) {
        cv_rect(x + at, y, 2, 2, BR_LINE);
        cv_rect(x + at, y + size - 2, 2, 2, BR_LINE);
        cv_rect(x, y + at, 2, 2, BR_LINE);
        cv_rect(x + size - 2, y + at, 2, 2, BR_LINE);
    }
    int32_t center = size / 2 - 1;
    for (int32_t at = -6; at <= 6; at += 2) {
        cv_rect(x + center + at, y + center + at, 2, 2, BR_DIM);
        cv_rect(x + center + at, y + center - at, 2, 2, BR_DIM);
    }
}

static void br_draw_control(uint32_t k, const br_control_t *c, uint32_t kind,
                            const char *value, uint32_t phase, uint32_t route_badges)
{
    uint32_t offset, env[4], q, variant = k;
    uint32_t body_y = BR_PARAM_BODY_Y;
    uint32_t screen = br_ui.screen[br_ui.page];
    uint16_t previous_accent = br_ui_accent;
    int32_t range = c->high - c->low;
    const volatile br_sound_t *s = &br_part[br_ui.part].sound;
    const volatile br_mod_config_t *m = &br_part[br_ui.part].mod;
    if (!c->label[0]) {
        for (offset = 0; offset < 134u; offset += 60u) {
            cv_begin(60, 134u - offset < 60u ? 134u - offset : 60u, BR_BACKGROUND);
            cv_oy = -(int32_t)offset;
            br_draw_empty_param(10, 22, 40);
            cv_oy = 0;
            cv_blit(k * 60u, body_y + offset);
        }
        return;
    }
    q = range > 0 ? (uint32_t)(br_clip(c->value - c->low, 0, range) * 127 / range) : 0u;
    if (br_target_control_id(k) != BR_TARGET_COUNT)
        q = br_target_widget_value(br_target_control_id(k));
    if (br_source_control_id(k) != BR_SOURCE_COUNT)
        q = br_source_control_id(k) == BR_SOURCE_OFF ? 0u : 96u;
    variant = br_control_widget_variant(k, c, kind);
    if (br_ui.page == BR_PAGE_MIX && screen == 2u && k == 1u && br_midi_clock_active) br_ui_accent = BR_DIM;
    if ((br_ui.page == BR_PAGE_ENV && screen == 1u) || br_source_control_id(k) == BR_SOURCE_ENV) {
        env[0] = m->attack; env[1] = m->decay; env[2] = m->sustain; env[3] = m->release;
    } else {
        env[0] = s->attack; env[1] = s->decay; env[2] = s->sustain; env[3] = s->release;
    }
    if (kind == BR_WIDGET_WAVE) {
        if (br_ui.page == BR_PAGE_LFO && !k) q = 96u;
        if (br_source_control_id(k) == BR_SOURCE_LFO1 || br_source_control_id(k) == BR_SOURCE_LFO2)
            phase = br_audio_lfo_phase(br_ui.part, br_source_control_id(k) - BR_SOURCE_LFO1);
    }
    for (offset = 0; offset < 134u; offset += 60u) {
        cv_begin(60, 134u - offset < 60u ? 134u - offset : 60u, BR_BACKGROUND);
        cv_oy = -(int32_t)offset;
        br_widget_draw(kind, 0, BR_PARAM_ICON_Y, 60, q, variant, env, phase);
        br_draw_knob_badge(k, 30, 0, k == br_active_knob());
        int32_t label_y = br_draw_param_text(30, BR_PARAM_VALUE_Y, value, c->label,
            br_ui.page == BR_PAGE_PUNCH && br_punch_ks.active == k + 1u ? BR_ACC : BR_DIM);
        int32_t badge_y = label_y + 10;
        int32_t max_badge_y = 135 - (int32_t)br_badge_count(route_badges) * 10;
        if (route_badges && badge_y > max_badge_y) badge_y = max_badge_y;
        br_draw_route_badges(30, badge_y, route_badges, br_ui.part, BR_BACKGROUND);
        cv_oy = 0;
        cv_blit(k * 60u, body_y + offset);
    }
    br_ui_accent = previous_accent;
}

static uint32_t br_hash_text(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static const char *br_engine_name(uint32_t preset)
{
    if (preset == BR_KIT_808) return "80S";
    if (preset == BR_KIT_909) return "90S";
    if (br_is_plaits(preset) || preset == BR_KIT_PLAITS) return "PLAITS";
    return "BRAIDS";
}
static const char *br_sound_name(uint32_t preset)
{
    if (preset == BR_KIT_PLAITS) return "PLAITS KIT";
    const char *name = br_preset_name(preset);
    return br_is_plaits(preset) && name[0] == 'P' && name[1] == 'L' && name[2] == ' ' ? name + 3 : name;
}
static const char *br_header_title(void)
{
    uint32_t page=br_ui.page, screen=br_ui.screen[page];
    switch(page){
    case BR_PAGE_HOME: return br_sound_name(br_part[br_ui.part].sound.preset);
    case BR_PAGE_ENV: return screen==0u ? "ENV / AMP" : screen==1u ? "ENV / FILTER" : "ENV / SETUP";
    case BR_PAGE_FX: {
        static const char *const titles[]={"FX / SENDS","FX / DRIVE","FX / DELAY","FX / MOD","FX / REVERB"};
        return screen<5u?titles[screen]:"FX";
    }
    case BR_PAGE_MIX: {
        static const char *const titles[]={"MIXER","GLOBAL / FILTER","SETTINGS","GLOBAL / PAN","COMPRESSOR","COMP KNOBS","GLOBAL / HEAT"};
        return screen<7u?titles[screen]:screen==7u?"MIDI / 1":screen==8u?"MIDI / 2":"";
    }
    case BR_PAGE_SEQ: return "SEQUENCER";
#ifdef BR_PATTERN_SONG_EXPERIMENT
    case BR_PAGE_PS: return "PATTERNS";
#endif
    case BR_PAGE_PATTERN: return br_retrig_page() ? "RETRIGGER" : "ARP";
    case BR_PAGE_SCALE: return br_ui.part == BR_DRUM_PART ? "DRUM KITS" : "SOUNDS";
    case BR_PAGE_PUNCH: return "PUNCH FX";
    case BR_PAGE_LFO: return screen?"LFO 2":"LFO 1";
    case BR_PAGE_ROUTE:
        if(br_gate_page())return "KIT LEVEL";
        return screen==0u?"PATCH 1":screen==1u?"PATCH 2":"LFO DEPTHS";
    default: return "";
    }
}
/* The title area fits ten characters in the 2x widget font. Keep the kit
 * variants distinct; truncate other long names only at the end. */
#define BR_HEADER_TITLE_MAX 10u
static uint32_t br_header_title_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void br_header_short_title(const char *name, char out[BR_HEADER_TITLE_MAX + 1u])
{
    uint32_t i, n = 0u;
    if (br_header_title_equal(name, "SAMPLE KIT A")) name = "SAMPLE A";
    else if (br_header_title_equal(name, "SAMPLE KIT B")) name = "SAMPLE B";
    else if (br_header_title_equal(name, "WAVE TERRAIN")) name = "TERRAIN";
    else if (br_header_title_equal(name, "STRING MACH")) name = "STR MACH";
    else if (br_header_title_equal(name, "CHOOSE SLOT")) name = "PICK SLOT";
    for (i = 0u; name[i] && n < BR_HEADER_TITLE_MAX; ++i) {
        if (name[i] == ' ' && (name[i + 1u] == '/' || (i && name[i - 1u] == '/'))) continue;
        out[n++] = name[i];
    }
    out[n] = 0;
}
static uint32_t br_header_has_prefix(const char *name, const char *prefix)
{
    while (*prefix && *name == *prefix) { ++name; ++prefix; }
    return !*prefix;
}
static void br_header_draw_title(const char *name)
{
    char title[BR_HEADER_TITLE_MAX + 1u];
    if (br_header_has_prefix(name, "GLOBAL / ")) {
        br_widget_text(40, 9, "GLOBAL /", C_WHITE);
        br_header_short_title(name + 9, title);
        br_widget_text_2x(92, 5, title, C_WHITE);
    } else if (br_header_has_prefix(name, "FX / ")) {
        br_widget_text(40, 9, "FX /", C_WHITE);
        br_header_short_title(name + 5, title);
        title[8] = 0; /* eight large characters fit after the prefix */
        br_widget_text_2x(68, 5, title, C_WHITE);
    } else if (br_header_has_prefix(name, "ENV / ")) {
        br_widget_text(40, 9, "ENV /", C_WHITE);
        br_header_short_title(name + 6, title);
        title[7] = 0;
        br_widget_text_2x(74, 5, title, C_WHITE);
    } else {
        br_header_short_title(name, title);
        br_widget_text_2x(40, 5, title, C_WHITE);
    }
}
static void br_track_badge(int32_t x, int32_t y, uint32_t part, uint32_t compact)
{
    char text[8], number[8];
    uint16_t color = br_track_color(part);
    if (part == BR_DRUM_PART) br_text_copy(text, "DK", sizeof text);
    else { br_text_copy(text, compact ? "" : "T", sizeof text);
        br_number(number, (int32_t)part + 1); br_text_append(text, number, sizeof text); }
    /* Include the bold overprint, then leave three pixels on either side. */
    int32_t w = compact && part != BR_DRUM_PART ? 18 : 30;
    br_art_rect(x + 2, y, w - 4, 20, color);
    br_art_rect(x, y + 2, w, 16, color);
    if (part == BR_DRUM_PART) {
        /* Preserve both original glyphs; trim only K's rightmost bold cell. */
        br_widget_text_2x(x + 3, y + 3, "D", C_BLACK);
        br_widget_text_2x(x + 5, y + 3, "D", C_BLACK);
        br_widget_text_2x(x + 17, y + 3, "K", C_BLACK);
        for (uint32_t row=0;row<8u;++row)
            for (uint32_t col=0;col+1u<james_font_width('K');++col)
                if (james_font_row('K', row) & (1u << (james_font_width('K') - 1u - col)))
                    cv_rect(x + 19 + (int32_t)col * 2, y + 3 + (int32_t)row * 2, 2, 2, C_BLACK);
    } else {
        br_widget_text_2x(x + 3, y + 3, text, C_BLACK);
        br_widget_text_2x(x + 5, y + 3, text, C_BLACK);
    }
}

static void br_global_badge(int32_t x, int32_t y)
{
    /* Same footprint as the track badge, with a globe instead of T1..T4. */
    br_art_rect(x + 2, y, 26, 20, BR_GLOBAL_ACC);
    br_art_rect(x, y + 2, 30, 16, BR_GLOBAL_ACC);
    /* A simple circle, drawn entirely in 2x2 pixel cells. */
    static const uint8_t circle[7] = {0x1cu, 0x22u, 0x41u, 0x41u, 0x41u, 0x22u, 0x1cu};
    for (uint32_t row = 0u; row < 7u; ++row)
        for (uint32_t col = 0u; col < 7u; ++col)
            if (circle[row] & (1u << (6u - col)))
                cv_rect(x + 8 + (int32_t)col * 2, y + 3 + (int32_t)row * 2, 2, 2, C_BLACK);
}

static void br_metronome_icon(int32_t x)
{
    x &= ~1;
    uint32_t phase = br_playing ? br_transport_phase : 64u;
    int32_t swing = br_playing ? br_metronome_swing(phase & ~7u, br_global_beat) : 0;
    int32_t arm = x + 7 + swing * 5 / 32;
    br_art_line(x + 2, 18, x + 12, 18, BR_GLOBAL_ACC);
    br_art_line(x + 2, 18, x + 5, 2, BR_GLOBAL_ACC);
    br_art_line(x + 5, 2, x + 8, 2, BR_GLOBAL_ACC);
    br_art_line(x + 8, 2, x + 12, 18, BR_GLOBAL_ACC);
    br_art_line(x + 7, 13, arm, 1, br_playing ? C_WHITE : BR_DIM);
    br_art_rect(arm - 1, 4, 3, 3, br_playing ? C_WHITE : BR_DIM);
}

/* Three small bars share the header with the BPM and metronome. An unknown
 * reading is deliberately distinct from an empty battery. */
static void br_battery_icon(int32_t level)
{
    uint16_t outline = level < 0 ? BR_LINE : level == 0 ? RGB(190, 62, 48) : BR_DIM;
    uint16_t fill = level <= 1 ? RGB(226, 92, 48) : BR_GLOBAL_ACC;
    uint32_t bars, i;
    br_art_rect(216, 0, 18, 10, outline);
    br_art_rect(218, 2, 14, 6, C_BLACK);
    cv_rect(234, 3, 3, 4, outline);
    if (level < 0) {
        br_art_rect(222, 4, 4, 2, BR_DIM);
        return;
    }
    bars = level > 3 ? 3u : (uint32_t)level;
    for (i = 0; i < bars; ++i)
        br_art_rect(220 + (int32_t)i * 4, 2, 2, 6, fill);
}

/* Round the corners of physical button cues on the native pixel grid. */
static void br_cue_round_button(int32_t x, int32_t y, int32_t w, int32_t h)
{
    cv_rect(x + 2, y, w - 4, h, BR_LINE);
    cv_rect(x + 1, y + 1, w - 2, h - 2, BR_LINE);
    cv_rect(x, y + 2, w, h - 4, BR_LINE);
}

/* Show the paired octave buttons as one button whenever they are referenced. */
static void br_draw_oct_cue(const char *text, int32_t y, uint32_t compact, uint16_t ink)
{
    const char *rest = text + 7; /* OCT-/+ followed by a space or colon */
    int32_t bw = br_widget_text_width("OCT-/+") + 6;
    int32_t w, x;
    while (*rest == ' ' || *rest == ':') ++rest;
    w = bw + (*rest ? 5 + br_widget_text_width(rest) : 0);
    x = (240 - w) / 2;
    if (compact) {
        br_cue_round_button(x, y, bw, 9);
        br_widget_text(x + 3, y + 1, "OCT-/+", C_WHITE);
        if (*rest) br_widget_text(x + bw + 5, y + 1, rest, ink);
    } else {
        br_cue_round_button(x, y + 4, bw, 16);
        br_widget_text(x + 3, y + 8, "OCT-/+", C_WHITE);
        if (*rest) br_widget_text(x + bw + 5, y + 8, rest, ink);
    }
}

/* The kit navigation messages reference the HOME and SEQ buttons explicitly. */
static uint32_t br_draw_kit_button_message(const char *text, int32_t y, uint32_t compact)
{
    uint32_t h = br_hash_text(text);
    uint32_t both = h == br_hash_text("KIT: HOME OR SEQ");
    int32_t x, by = y + (compact ? 0 : 4), ty = y + (compact ? 1 : 8);
    int32_t bh = compact ? 9 : 16;
    if (!both && h != br_hash_text("KIT: HOME PAGE 2")) return 0u;
    x = (240 - (br_widget_text_width("KIT:") + 5 + br_widget_text_width("HOME") + 6 + 5 +
        br_widget_text_width(both ? "OR" : "PAGE 2") + (both ? 5 + br_widget_text_width("SEQ") + 6 : 0))) / 2;
    br_widget_text(x, ty, "KIT:", BR_GLOBAL_ACC); x += br_widget_text_width("KIT:") + 5;
    br_cue_round_button(x, by, br_widget_text_width("HOME") + 6, bh);
    br_widget_text(x + 3, ty, "HOME", C_WHITE); x += br_widget_text_width("HOME") + 11;
    br_widget_text(x, ty, both ? "OR" : "PAGE 2", BR_GLOBAL_ACC);
    if (both) {
        x += br_widget_text_width("OR") + 5;
        br_cue_round_button(x, by, br_widget_text_width("SEQ") + 6, bh);
        br_widget_text(x + 3, ty, "SEQ", C_WHITE);
    }
    return 1u;
}

/* Shared 24-pixel cue bar: two centered groups, with brief vertical slides
 * between cues and contextual messages. Each movement takes 160 ms. */
static void br_draw_cue_bar(uint32_t kind, uint32_t force)
{
    static uint32_t previous;
    char timing[25], number[8];
    const char *step_notice = 0;
    uint32_t back_save=kind==9u || kind==10u;
    const char *button1=(kind==2u || (kind>=4u && !back_save))?"SAV":"OCT-";
    const char *label1=kind==1u?"MUTE":kind==2u?"NEXT":(kind==3u || back_save)?"BACK":kind==5u?"YES":"CONFIRM";
    const char *button2=back_save?"SAV":kind==2u || kind>=4u?"PLAY":kind==3u?"SAV":"OCT+";
    const char *label2=kind==10u?"CHOOSE SLOT":kind==9u?"CONFIRM":kind==1u?"SOLO":kind==2u || kind==4u || kind==5u?(br_playing?"STOP":"START"):"NEXT";
    uint32_t b1=(kind==2u || (kind>=4u && !back_save))?22u:30u, b2=back_save?22u:kind==2u || kind>=4u?26u:kind==3u?22u:30u, gap=20u;
    uint32_t w1=b1+5u+(uint32_t)br_widget_text_width(label1), w2=b2+5u+(uint32_t)br_widget_text_width(label2);
    int32_t x1=(240-(int32_t)(w1+w2+gap))/2, x2=x1+(int32_t)(w1+gap);
    uint32_t compact = 0u;
    int32_t cue_y=0, msg_y=24;
    uint32_t active=br_ui.message[0] && (int32_t)(br_ui.message_until-fm1_ms)>0;
    uint32_t elapsed=fm1_ms-br_ui.message_start, beat=br_ui.message_until-br_ui.message_start-640u, h;
    if (kind == 6u && br_seq_step_edit()) {
        int32_t micro = br_step_micro_get(br_ui.step_press_part, br_ui.step_press_step, br_ui.step_press_lane);
        br_text_copy(timing, "OCT-/+ ", sizeof timing);
        if (micro > 0) br_text_append(timing, "+", sizeof timing);
        br_number(number, micro); br_text_append(timing, number, sizeof timing);
        br_text_append(timing, "/128", sizeof timing);
        step_notice = timing;
    }
    if (kind == 8u && br_session_legacy_migration_pending()) step_notice = "LEGACY SESSION / MIGRATE";
    if(active) {
        if(elapsed<160u) cue_y=(int32_t)(elapsed*24u/160u);
        else if(elapsed<320u){cue_y=24;msg_y=24-(int32_t)((elapsed-160u)*24u/160u);}
        else if(elapsed<320u+beat){cue_y=24;msg_y=0;}
        else if(elapsed<480u+beat){cue_y=24;msg_y=(int32_t)((elapsed-320u-beat)*24u/160u);}
        else {msg_y=24;cue_y=24-(int32_t)((elapsed-480u-beat)*24u/160u);}
    }
    h=(kind + compact * 16u)*131u+(uint32_t)cue_y*31u+(uint32_t)msg_y;
    if(kind==7u)h=h*31u+br_sel.loaded;
    if(kind==4u || kind==5u)h=h*31u+br_playing;
    if(active)h=h*31u+br_hash_text(br_ui.message);
    if(step_notice)h=h*31u+br_hash_text(step_notice);
    if(!force && h==previous)return;
    cv_begin(240, compact ? 14u : 24u, BR_BACKGROUND);
    if (compact) { cue_y -= 5; msg_y -= 5; }
    if (kind == 8u && step_notice) {
        br_widget_text((240 - br_widget_text_width(step_notice)) / 2, 8 + cue_y, step_notice, BR_GLOBAL_ACC);
    } else if (kind == 8u) {
        int32_t w = 30 + 6 + br_widget_text_width("+") + 6 + 12 + 6 + br_widget_text_width("SNAP");
        int32_t x = (240 - w) / 2;
        br_cue_round_button(x, (compact ? 6 : 4) + cue_y, 30, compact ? 12 : 16);
        br_widget_text(x + (30 - br_widget_text_width("OCT-")) / 2, 8 + cue_y, "OCT-", C_WHITE);
        x += 36; br_widget_text(x, 8 + cue_y, "+", C_WHITE);
        x += br_widget_text_width("+") + 6;
        br_shape_circle_grid(bw_q8(x + 5), bw_q8(11 + cue_y), 5u * 256u, 256u, C_WHITE, 0u, 1u);
        cv_rect(x + 5, 7 + cue_y, 1, 4, C_WHITE);
        br_widget_text(x + 18, 8 + cue_y, "SNAP", C_WHITE);
    } else if (kind == 6u) {
        if (step_notice) br_draw_oct_cue(step_notice, cue_y, 0u, C_WHITE);
        else {
            int32_t x=(240-(12+4+br_widget_text_width("SELECT")+6+br_widget_text_width("DRUM")))/2;
            br_shape_circle_grid(bw_q8(x+5),bw_q8(11+cue_y),5u*256u,256u,C_WHITE,0u,1u);
            cv_rect(x+5,8+cue_y,1,4,C_WHITE);
            br_widget_text(x+16,8+cue_y,"SELECT",C_WHITE);
            br_widget_text(x+22+br_widget_text_width("SELECT"),8+cue_y,"DRUM",C_WHITE);
        }
    } else if (kind == 7u) {             /* SEL (the square button) loads; the keys audition */
        const char *label = br_sel.loaded ? "DONE" : "LOAD PRESET";
        int32_t w1 = 22 + 6 + br_widget_text_width(label), w2 = 24 + 6 + br_widget_text_width("AUDITION");
        int32_t x = ((240 - (w1 + 16 + w2)) / 2) & ~1, k = (x + w1 + 16) & ~1;
        br_cue_round_button(x, 1 + cue_y, 22, 22);
        br_widget_text(x + (22 - br_widget_text_width("SEL")) / 2, 8 + cue_y, "SEL", C_WHITE);
        br_widget_text(x + 28, 8 + cue_y, label, C_WHITE);
        /* Five distinct white keys and a group of two plus one black key. */
        cv_rect(k, 3 + cue_y, 24, 18, BR_DIM);
        for (int32_t key = 0; key < 5; ++key)
            cv_rect(k + 2 + key * 4, 5 + cue_y, 3, 14, C_WHITE);
        cv_rect(k + 4, 5 + cue_y, 2, 8, C_BLACK);
        cv_rect(k + 8, 5 + cue_y, 2, 8, C_BLACK);
        cv_rect(k + 16, 5 + cue_y, 2, 8, C_BLACK);
        br_widget_text(k + 30, 8 + cue_y, "AUDITION", C_WHITE);
    } else if (kind == 2u) {
        int32_t x=(240-(22+5+br_widget_text_width("NEXT")))/2;
        br_cue_round_button(x,1+cue_y,22,22);
        br_widget_text(x+(22-br_widget_text_width("SAV"))/2,8+cue_y,"SAV",C_WHITE);
        br_widget_text(x+27,8+cue_y,"NEXT",C_WHITE);
    } else {
        if (kind == 2u || (kind >= 4u && !back_save)) br_cue_round_button(x1, 1 + cue_y, (int32_t)b1, 22);
        else br_cue_round_button(x1, 4 + cue_y, (int32_t)b1, 16);
        br_widget_text(x1+((int32_t)b1-br_widget_text_width(button1))/2,8+cue_y,button1,C_WHITE);
        br_widget_text(x1+(int32_t)b1+5,8+cue_y,label1,C_WHITE);
        br_cue_round_button(x2, (back_save?1:4) + cue_y, (int32_t)b2, back_save?22:16);
        br_widget_text(x2+((int32_t)b2-br_widget_text_width(button2))/2,8+cue_y,button2,C_WHITE);
        br_widget_text(x2+(int32_t)b2+5,8+cue_y,label2,C_WHITE);
    }
    if(active && !br_draw_kit_button_message(br_ui.message, msg_y + (compact ? 8 : 0), compact))
        br_widget_text((240-br_widget_text_width(br_ui.message))/2,8+msg_y,br_ui.message,BR_GLOBAL_ACC);
    cv_blit(0, compact ? 226u : 216u);previous=h;
}

/* The meaning of OCT-/OCT+ on pages where they are not the keyboard octave. Empty where they are. */
static const char *br_oct_hint(void)
{
    uint32_t kit = br_is_kit(br_part[br_ui.part].sound.preset);
    if (br_retrig_page()) return "HOLD STEP: EDIT";
    if (br_sound_mod_page()) return "HOW FAR LFO 1 MOVES EACH";   /* LFO DEPTHS: what the page is for */
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) return "HOLD KEY: REC COPY PLAY PASTE";
#endif
    if (br_ui.page == BR_PAGE_SEQ) return br_ui.screen[BR_PAGE_SEQ] ? "" : "OCT-/+: STEP BANK";
    if (br_ui.page == BR_PAGE_PUNCH) return "OCT-/+: PAGES";
    if (kit && !br_page_plays_keys()) return "";
    return "";
}

/* Other pages retain their compact status area when no cue is present. */
static void br_draw_status(uint32_t force)
{
    static uint32_t previous;
    if((br_ui.page==BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]) || br_gate_page()) {br_draw_cue_bar(1u,force);return;}
    if(br_sel_browse()) {br_draw_cue_bar(7u,force);return;}
    if(br_fx_sends_page()) {br_draw_cue_bar(8u,force);return;}
    if(br_ui.page==BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && br_is_kit(br_part[br_ui.part].sound.preset)) {br_draw_cue_bar(6u,force);return;}
    char timing[25], number[8];
    const char *text = br_session_legacy_migration_pending() ? "LEGACY SESSION / MIGRATE" :
        (int32_t)(br_ui.message_until - fm1_ms) > 0 ? br_ui.message : "";
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (!*text && br_ui.page == BR_PAGE_PS) {
        br_text_copy(timing, "ACTIVE ", sizeof timing); br_number(number, br_ps_state.active + 1);
        br_text_append(timing, number, sizeof timing);
        if (br_ps_state.queued < FM1_PATTERN_COUNT) {
            br_text_append(timing, " QUEUED ", sizeof timing); br_number(number, br_ps_state.queued + 1);
            br_text_append(timing, number, sizeof timing);
        }
        text = timing;
    }
#endif
    if (!*text && br_seq_step_edit()) {
        uint32_t lane = br_ui.step_press_kit ? br_ui.step_press_lane : 0u;
        int32_t micro = br_step_micro_get(br_ui.step_press_part, br_ui.step_press_step, lane);
        br_text_copy(timing, "OCT-/+ ", sizeof timing);
        if (micro > 0) br_text_append(timing, "+", sizeof timing);
        br_number(number, micro); br_text_append(timing, number, sizeof timing);
        br_text_append(timing, "/128", sizeof timing);
        text = timing;
    }
    uint32_t dim = 0u;
    if (!*text) {
        /* What OCT-/OCT+ do here, when it is not the octave. */
        text = br_oct_hint(); dim = *text != 0;
    }
    uint32_t h = br_hash_text(text) ^ (dim << 31);
    if (force || h != previous) {
        cv_begin(240, 24, BR_BACKGROUND);
        if (*text) {
            if (text[0] == 'O' && text[1] == 'C' && text[2] == 'T' && text[3] == '-')
                br_draw_oct_cue(text, 0, 0u, dim ? BR_DIM : BR_GLOBAL_ACC);
            else if (!br_draw_kit_button_message(text, 0, 0u))
                br_widget_text((240 - br_widget_text_width(text)) / 2, 8, text, dim ? BR_DIM : BR_GLOBAL_ACC);
        }
        cv_blit(0, 216); previous = h;
    }
}

/* One envelope, with its four knob values together below the graph. The same
 * strip canvas serves the full-width drawing; no full-screen buffer is needed. */
static void br_draw_envelope(uint32_t force)
{
    static uint32_t previous;
    br_control_t controls[4];
    uint32_t env[4], screen = br_ui.screen[BR_PAGE_ENV], p = br_ui.part;
    uint32_t focus = br_active_knob();
    uint32_t source_badges = br_send_badges(p, screen ? BR_SOURCE_ENV : BR_SOURCE_AMP);
    uint32_t route_badges[4];
    uint32_t hash = screen + p * 4u, offset;
    for (uint32_t k = 0; k < 4u; ++k) {
        controls[k] = br_control(k);
        env[k] = (uint32_t)controls[k].value;
        hash = hash * 131u + env[k];
        route_badges[k] = br_control_route_badges(k);
        hash = (hash * 67u + route_badges[k]) * 67u + br_badge_pulse_key(p, route_badges[k]);
    }
    hash = (hash * 67u + source_badges) * 5u + focus;
    if (force || hash != previous) {
        for (offset = 0; offset < 168u; offset += 32u) {
            uint32_t rows = 168u - offset;
            cv_begin(240, rows < 32u ? rows : 32u, BR_BACKGROUND);
            cv_oy = -(int32_t)offset;
            bw_adsr_graph(9, 48, 222, 56, env, focus);
            for (uint32_t k = 0; k < 4u; ++k) {
                char value[24];
                int32_t center = 30 + (int32_t)k * 60;
                br_control_text(k, &controls[k], value);
                br_draw_knob_badge(k, center, BR_PARAM_BODY_Y - 40, k == focus);
                br_draw_param_text(center, BR_PARAM_BODY_Y + BR_PARAM_VALUE_Y - 40,
                                   value, controls[k].label, k == focus ? BR_ACC : BR_DIM);
                br_draw_route_badges(center, br_badge_count(route_badges[k]) == 4u ? 127 : 130, route_badges[k], p, BR_BACKGROUND);
            }
            if (source_badges & BR_BADGE_SEND1) br_draw_route_badge(94, 159, 4u, p, BR_BACKGROUND);
            if (source_badges & BR_BADGE_SEND2) br_draw_route_badge(146, 159, 5u, p, BR_BACKGROUND);
            cv_oy = 0;
            cv_blit(0, 40u + offset);
        }
        previous = hash;
    }
    br_draw_status(force);
}

/* Each PERFORM graph combines the two neighboring knob controls. */
static void br_perform_filter_graph(int32_t x, int32_t y, uint32_t position, uint32_t resonance)
{
    int32_t left=x+5, right=x+107, top=y+5, base=y+55;
    int32_t px=left, py=top;
    int32_t cutoff=position<63u ? (int32_t)position*127/62 :
        ((int32_t)position-65)*127/62;
    bw_line(left,base,right,base,512u,BR_LINE);
    for (int32_t i=0;i<=64;++i) {
        int32_t frequency=i*127/64, level;
        if(position==63u || position==64u) level=90;
        else {
            int32_t distance=frequency-cutoff, magnitude=distance<0?-distance:distance;
            int32_t response=distance*96/(magnitude+14);
            int32_t strength=position<63u ? 63-(int32_t)position : (int32_t)position-64;
            int32_t filtered=(response+96)*90/192;
            if(position<63u)filtered=90-filtered;
            /* Fade smoothly from bypass while the cutoff enters the graph. */
            if(strength>8)strength=8;
            filtered+=(int32_t)resonance*32/(127*(1+magnitude*magnitude/90));
            level=90+(filtered-90)*strength/8;
        }
        if(level>127)level=127;
        int32_t nx=left+i*(right-left)/64, ny=base-level*(base-top)/127;
        if(i)bw_line(px,py,nx,ny,512u,BR_ACC);
        px=nx;py=ny;
    }
}

static void br_perform_heat_graph(int32_t x, int32_t y, uint32_t type, uint32_t amount)
{
    int32_t left=x+5, right=x+107, top=y+5, base=y+55, mid=(top+base)/2;
    int32_t px=left, py=mid;
    bw_line(left,mid,right,mid,512u,BR_LINE);
    for (int32_t i=0;i<=64;++i) {
        int32_t input=i*254/64-127, output=input;
        if(type==BR_HEAT_BIT && amount) {
            int32_t step=2+(int32_t)amount*30/127;
            output=(input/step)*step;
        } else if(type==BR_HEAT_SAT || type==BR_HEAT_WOW) {
            if(type==BR_HEAT_WOW)
                input=bw_sine((uint32_t)i*(1536u+amount*4u))*127/32767;
            int32_t magnitude=input<0?-input:input;
            int32_t drive=(int32_t)amount*3;
            output=input*(127+drive)/(127+magnitude*drive/127);
        }
        output=br_clip(output,-127,127);
        int32_t nx=left+i*(right-left)/64, ny=mid-output*(base-top)/254;
        if(i)bw_line(px,py,nx,ny,512u,BR_ACC);
        px=nx;py=ny;
    }
}

static void br_draw_page_route_badge(uint32_t force, uint32_t part);
/* Two-column filter graph, then two knobs. Used for the global filter (GLO FILTER, RESONANCE, ENV DEPTH,
 * SMOOTH) and for each track's filter page (FILTER, RESONANCE, ENV DEPTH, KEY TRACK). */
static void br_draw_perform(uint32_t force)
{
    static uint32_t previous;
    uint32_t focus=br_active_knob(), offset, k, p=br_ui.part, track=br_ui.page!=BR_PAGE_MIX;
    uint32_t body_y = BR_PARAM_BODY_Y;
    br_control_t controls[4];
    uint32_t route_badges[4]={0,0,0,0};
    uint32_t hash=track+p*2u;
    for(k=0;k<4u;++k) {
        controls[k]=br_control(k);
        hash=hash*131u+(uint32_t)controls[k].value;
        if(track) {
            route_badges[k]=br_control_route_badges(k);
            hash=(hash*67u+route_badges[k])*67u+br_badge_pulse_key(p,route_badges[k]);
        }
    }
    hash=hash*5u+focus;
    if(track) br_draw_page_route_badge(force,p);
    if(force || hash!=previous) {
        for(offset=0;offset<120u;offset+=32u) {
            uint32_t rows=120u-offset;
            cv_begin(240,rows<32u?rows:32u, BR_BACKGROUND);cv_oy=-(int32_t)offset;
            br_perform_filter_graph(4,BR_PARAM_ICON_Y,(uint32_t)controls[0].value,(uint32_t)controls[1].value);
            for(k=0;k<4u;++k) {
                br_control_t *c=&controls[k];char value[24];
                int32_t center=30+(int32_t)k*60;
                br_control_text(k,c,value);
                if(k>=2u) {                                  /* the two right-hand controls are knobs */
                    uint32_t kind=br_control_widget(k,c);
                    int32_t range=c->high-c->low;
                    uint32_t q=range>0?(uint32_t)(br_clip(c->value-c->low,0,range)*127/range):0u;
                    br_widget_draw(kind,(int32_t)k*60,BR_PARAM_ICON_Y,60,q,br_control_widget_variant(k,c,kind),0,0u);
                }
                br_draw_knob_badge(k,center,0,k==focus);
                br_draw_param_text(center, BR_PARAM_VALUE_Y, value, c->label, BR_DIM);
                if(route_badges[k]) br_draw_route_badges(center,98,route_badges[k],p,BR_BACKGROUND);
            }
            cv_oy=0;cv_blit(0,body_y+offset);
        }
        previous=hash;
    }
    br_draw_status(force);
}

/* Four logical lanes share the drawing workspace. Their row labels show the
 * selected one of eight available voices; no hidden legacy lane rows remain. */
/* Compact sequencer icons share a centered, symmetric 2px grid. */
static void br_draw_seq_control(uint32_t k, const br_control_t *c, const char *value)
{
    int32_t range = c->high - c->low;
    uint32_t q = range > 0 ? (uint32_t)(br_clip(c->value - c->low, 0, range) * 127 / range) : 0u;
    uint32_t focus = br_active_knob();
    cv_begin(54, 52, BR_BACKGROUND);
    if (!c->label[0]) {
        br_draw_empty_param(11, 0, 32);
        return;
    }
    br_control_frame(10, 0, 34, 32);
    if (br_retrig_page()) {
        uint32_t count = !k ? (uint32_t)c->value + 1u : 4u;
        int32_t gap = !k ? 3 : 3 + (int32_t)c->value;
        if (count > 8u) count = 8u;
        if (k && gap > 8) gap = 8;
        int32_t left = 27 - ((int32_t)count - 1) * gap / 2;
        for (uint32_t i = 0; i < count; ++i) {
            int32_t x = left + (int32_t)i * gap;
            cv_rect(x, 8, 2, 16, i ? BR_ACC : C_WHITE);
        }
        br_widget_text((54 - br_widget_text_width(value)) / 2, 36, value, C_WHITE);
        br_widget_text((54 - br_widget_text_width(c->label)) / 2, 45, c->label, BR_DIM);
        return;
    }
    if (k == 0u) {
        int32_t ax, ay, bx, by;
        int32_t cx = bw_q8(27), cy = bw_q8(16);
        uint32_t angle = 40960u + q * 49152u / 127u;
        bw_circle(27, 16, 10, BR_LINE, 0u);
        bw_point(cx, cy, 3u * 256u, angle, &ax, &ay);
        bw_point(cx, cy, 8u * 256u, angle, &bx, &by);
        bw_qline(ax, ay, bx, by, 512u, C_WHITE);
    } else if (k == 1u) {
        uint32_t length = (uint32_t)c->value;
        uint32_t first = length > 16u ? 16u : length;
        uint32_t remaining = length > 16u ? length - 16u : 0u;
        uint32_t tail_max = BR_STEP_LENGTH_MAX - 16u;
        int32_t first_width = (int32_t)((first * 24u + 15u) / 16u);
        int32_t tail_width = (int32_t)((remaining * 24u + tail_max - 1u) / tail_max);
        br_art_rect(15, 9, 24, 6, BR_LINE);
        br_art_rect(15, 18, 24, 6, BR_LINE);
        if (first_width) br_art_rect(15, 10, first_width, 4, BR_ACC);
        if (tail_width) br_art_rect(15, 19, tail_width, 4, BR_ACC);
    } else if (k == 2u) {
        int32_t handle = 23 - (int32_t)((q * 14u + 63u) / 127u);
        br_art_rect(26, 8, 2, 16, BR_LINE);
        if (handle < 23) br_art_rect(26, handle, 2, 23 - handle, BR_ACC);
        for (int32_t i = 0; i < 3; ++i) {
            br_art_rect(17, 8 + i * 7, 4, 2, BR_LINE);
            br_art_rect(33, 8 + i * 7, 4, 2, BR_LINE);
        }
        br_art_rect(22, handle - 2, 10, 4, C_WHITE);
    } else {
        uint32_t selected = (q * 8u + 63u) / 127u;
        for (uint32_t row = 0; row < 3u; ++row)
            for (uint32_t col = 0; col < 3u; ++col)
                br_art_rect(17 + (int32_t)col * 8, 6 + (int32_t)row * 8, 4, 4,
                            row * 3u + col == selected ? BR_ACC : BR_LINE);
    }
    if (k != focus) {
        br_widget_text((54 - br_widget_text_width(c->label)) / 2, 37, c->label, BR_DIM);
    } else {
        char rows[2][9] = {{0}};
        uint32_t count = 0u;
        const char *at = value;
        while (*at && count < 2u) {
            uint32_t n = 0u, split = 0u;
            while (n < 8u && at[n]) { if (at[n] == ' ') split = n; ++n; }
            if (at[n] && split) n = split;
            for (uint32_t i = 0u; i < n; ++i) rows[count][i] = at[i];
            at += n;
            while (*at == ' ') ++at;
            ++count;
        }
        for (uint32_t row = 0u; row < count; ++row)
            br_widget_text((54 - br_widget_text_width(rows[row])) / 2,
                           37 + (int32_t)row * 8, rows[row], C_WHITE);
    }
}

static void br_draw_drum_grid(uint32_t force)
{
    static uint32_t ruler_old, row_old[BR_DRUM_LANES];
    uint32_t p = br_ui.part, length = br_part[p].length, lane, step, cell, h;
    uint32_t base = br_ui.step_bank * BR_BANK_STEPS;
    uint32_t retrig = br_retrig_page();
    uint32_t editing = retrig ? br_retrig_editing() : br_ui.step_edit_active;
    uint32_t edit_step = retrig ? br_ui.retrig_step : br_ui.step_press_step;
    h = edit_step ^ editing << 7 ^ length << 8 ^ br_playing << 16 ^ br_step_pos[p] << 20 ^ base << 24;
    if (force || h != ruler_old) {
        cv_begin(240, 14, BR_BACKGROUND);
        br_widget_text(6, 2, "STEP", BR_DIM);
        for (cell = 0; cell < BR_BANK_STEPS; cell++) {
            step = base + cell;
            int32_t x = 42 + (int32_t)cell * 12;
            uint32_t value = step + 1u;
            uint16_t color = step < length ? BR_DIM : BR_LINE;
            char number[8];
            if (editing && step == edit_step) color = BR_ACC;
            br_number(number, (int32_t)value);
            br_widget_text(x + (12 - br_widget_text_width(number)) / 2, 0, number, color);
            if (br_playing && step == br_step_pos[p]) br_art_rect(x, 10, 12, 2, C_WHITE);
            if (editing && step == edit_step) br_art_rect(x, 12, 12, 2, BR_ACC);
        }
        cv_blit(0, 36); ruler_old = h;
    }
    for (lane = 0; lane < BR_DRUM_LANES; lane++) {
        uint32_t selected = lane == br_ui.lane;
        uint16_t background = selected ? BR_CARD : BR_BACKGROUND;
        h = (p + 1u) * 31u + br_part[p].sound.preset;
        h = h * 31u + length; h = h * 31u + selected; h = h * 31u + base;
        h = h * 31u + br_drum_voice_id(br_part[p].sound.drum_voice_mask, lane);
        if (selected) h = (h * 31u + edit_step) * 3u + editing;
        for (cell = 0; cell < BR_BANK_STEPS; cell++) {
            step = base + cell;
            uint32_t word = br_part[p].step[step];
            h = h * 31u + ((BR_STEP_HIT(word) >> lane) & 1u) +
                (((BR_STEP_ACC(word) >> lane) & 1u) << 1);
            if (retrig) h = h * 67u + br_seq_kit_retrig[step][lane];
            h = h * 31u + br_step_length_get(p, step, lane);
            h = h * 31u + br_step_condition_get(p, step, lane);
            h = h * 31u + br_step_note_get(p, step, lane);
            h = h * 31u + br_step_velocity_get(p, step, lane);
            h = h * 31u + br_step_length_coverage(p, step, lane, 1u);
        }
        if (!force && h == row_old[lane]) continue;
        cv_begin(240, 18, background);
        if (selected) br_art_rect(0, 0, 2, 18, BR_ACC);
        br_widget_text(6, 5,
                       br_drum_voice_label(br_part[p].sound.drum_voice_mask, lane),
                       selected ? C_WHITE : BR_DIM);
        for (cell = 0; cell < BR_BANK_STEPS; cell++) {
            step = base + cell;
            int32_t x = 44 + (int32_t)cell * 12;
            uint32_t word = br_part[p].step[step];
            uint32_t hit = (BR_STEP_HIT(word) >> lane) & 1u;
            uint32_t coverage = step < length ? br_step_length_coverage(p, step, lane, 1u) : 0u;
            uint16_t color = step >= length ? RGB(16, 19, 20) :
                hit && !coverage ? BR_ACC :
                selected && editing && step == edit_step ? background : BR_LINE;
            if (selected && editing && step == edit_step) {
                br_art_rect(x - 2, 0, 12, 18, C_WHITE);
                br_art_rect(x, 2, 8, 14, background);
            }
            br_art_rect(x, 4, 8, 10, color);
            if (coverage) {
                uint32_t width = (coverage * 8u + 15u) / 16u;
                cv_rect(x, 4, (int32_t)width, 10, hit ? BR_ACC : BR_LENGTH_BG);
            }
            if (step < length && hit && ((BR_STEP_ACC(word) >> lane) & 1u))
                br_art_rect(x + 2, 8, 4, 4, C_WHITE);
            if (retrig && step < length && hit && (br_seq_kit_retrig[step][lane] & 7u)) {
                cv_rect(x + 1, 5, 2, 8, C_WHITE);
                cv_rect(x + 5, 5, 2, 8, C_WHITE);
            }
            if (step < length) {
                if (hit) {
                    uint32_t note = br_step_note_get(p, step, lane);
                    uint32_t condition = br_step_condition_get(p, step, lane);
                    if (note != 60u) br_art_rect(x + 1, 2, 2, 2, C_WHITE);
                    if (condition != BR_STEP_COND_ALWAYS) br_art_rect(x + 6, 2, 2, 2, BR_ACC);
                }
            }
        }
        cv_blit(0, 50 + lane * 18u); row_old[lane] = h;
    }
    {
        static uint32_t controls_old[4];
        uint32_t k;
        for (k = 0; k < 4u; ++k) {
            br_control_t c = br_control(k);
            char value[24];
            uint32_t hash;
            br_control_text(k, &c, value);
            hash = br_hash_text(value) ^ br_hash_text(c.label) ^ br_ui.part ^
                br_ui.cursor << 8 ^ br_ui.lane << 16 ^ (k == br_active_knob() ? 0x80000000u : 0u);
            if (force || hash != controls_old[k]) {
                br_draw_seq_control(k, &c, value);
                cv_blit(4u + k * 60u, 162u);
                controls_old[k] = hash;
            }
        }
    }
    br_draw_status(force);
}

/* The four kit lanes share the kit colour and line up with the four knobs. */
static void br_mixer_name(const char *name, char first[7], char second[7]);
static uint32_t br_meter_segments(uint32_t peak, uint32_t count);
static void br_draw_drum_levels(uint32_t force)
{
    static uint32_t strip_old[BR_DRUM_LANES];
    static uint8_t meter_hold[BR_DRUM_LANES];
    static uint32_t meter_old[BR_DRUM_LANES] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    static uint32_t meter_epoch;
    uint32_t epoch = br_audio_kit_meter_epoch(), peaks[BR_DRUM_LANES];
    uint32_t p = BR_DRUM_PART, redrawn = 0u;
    /* Discard a snapshot interrupted by a new audio publication. */
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
        peaks[lane] = br_audio_kit_meter_read(lane);
    if (epoch != br_audio_kit_meter_epoch()) epoch = meter_epoch;
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane) {
        uint32_t value = (uint32_t)br_clip((int32_t)br_part[p].lane_level[lane], 0, 127);
        uint32_t selected = lane == br_ui.lane;
        uint32_t id = br_drum_voice_id(br_part[p].sound.drum_voice_mask, lane);
        uint32_t muted = (br_kit_mute_mask >> lane) & 1u, solo = (br_kit_solo_mask >> lane) & 1u;
        uint32_t dim = muted || (br_kit_solo_mask && !solo) || br_part[p].sound.mute ||
            (br_solo_mask && !((br_solo_mask >> p) & 1u));
        uint32_t h = (((((value * 31u + selected) * 31u + id) * 31u + p) * 3u + muted) * 3u + solo) * 3u + dim;
        uint16_t bg = selected ? BR_CARD : BR_BACKGROUND, color = dim ? BR_DIM : br_track_color(p);
        int32_t cap = br_clip((103 - (int32_t)(value * 104u / 127u)) & ~1, 0, 100);
        const char *name = br_drum_voice_name[id];
        char badge[3] = {'D', (char)('1' + lane), 0};
        if (!force && h == strip_old[lane]) continue;
        for (uint32_t offset = 0; offset < 176u; offset += 40u) {
            uint32_t rows = 176u - offset;
            cv_begin(54, rows < 40u ? rows : 40u, bg);
            cv_oy = -(int32_t)offset;
            if (selected) {
                br_art_line(0, 0, 53, 0, BR_LINE); br_art_line(0, 175, 53, 175, BR_LINE);
                br_art_line(0, 0, 0, 175, BR_LINE); br_art_line(53, 0, 53, 175, BR_LINE);
            }
            br_art_rect(14, 6, 26, 20, color);
            br_art_rect(12, 8, 30, 16, color);
            br_widget_text_2x(15, 9, badge, C_BLACK);
            br_widget_text_2x(17, 9, badge, C_BLACK);
            br_control_frame(8, 42, 38, 114);
            br_art_rect(26, 46, 2, 106, BR_LINE);
            for (uint32_t tick = 0; tick <= 4u; ++tick) {
                int32_t y = 46 + (int32_t)tick * 104 / 4;
                br_art_line(10, y, 13, y, BR_LINE); br_art_line(41, y, 44, y, BR_LINE);
            }
            br_art_rect(16, 46 + cap, 22, 6, dim ? BR_DIM : selected ? C_WHITE : color);
            br_widget_text(27 - br_widget_text_width(name) / 2, 30, name, selected ? C_WHITE : BR_DIM);
            if (muted) {
                cv_rect(9, 158, 13, 12, RGB(16,144,132));
                cv_rect(8, 159, 15, 10, RGB(16,144,132));
            }
            if (solo) {
                cv_rect(33, 158, 13, 12, BR_GLOBAL_ACC);
                cv_rect(32, 159, 15, 10, BR_GLOBAL_ACC);
            }
            br_widget_text(8 + (15 - br_widget_text_width("M")) / 2, 161, "M", muted ? C_BLACK : BR_DIM);
            br_widget_text(32 + (15 - br_widget_text_width("S")) / 2, 161, "S", solo ? C_BLACK : BR_DIM);
            cv_oy = 0;
            cv_blit(4u + lane * 60u, 34u + offset);
        }
        strip_old[lane] = h;
        redrawn |= 1u << lane;
    }
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane) {
        uint32_t value = (uint32_t)br_clip((int32_t)br_part[p].lane_level[lane], 0, 127);
        uint32_t now = epoch != meter_epoch ? br_meter_segments(peaks[lane], 28u) : 0u;
        uint32_t lit;
        if (now >= meter_hold[lane]) meter_hold[lane] = (uint8_t)now;
        else if (meter_hold[lane]) --meter_hold[lane];
        lit = (uint32_t)meter_hold[lane] * 106u / 28u;
        int32_t cap = br_clip((103 - (int32_t)(value * 104u / 127u)) & ~1, 0, 100);
        uint32_t dim = ((br_kit_mute_mask >> lane) & 1u) ||
            (br_kit_solo_mask && !((br_kit_solo_mask >> lane) & 1u)) || br_part[p].sound.mute || (br_solo_mask && !((br_solo_mask >> p) & 1u));
        uint16_t color = dim ? BR_DIM : br_track_color(p);
        uint32_t key = (lit << 24) | (value << 17) | ((uint32_t)color << 1) | (lane == br_ui.lane);
        if (!force && !((redrawn >> lane) & 1u) && key == meter_old[lane]) continue;
        meter_old[lane] = key;
        cv_begin(2, 106, BR_LINE);
        if (lit) cv_rect(0, 106 - (int32_t)lit, 2, (int32_t)lit, color);
        if (meter_hold[lane] == 28u && !dim) cv_rect(0, 0, 2, 4, RGB(240, 60, 48));
        cv_rect(0, cap, 2, 6, dim ? BR_DIM : lane == br_ui.lane ? C_WHITE : color);
        cv_blit(4u + lane * 60u + 26u, 80u);
    }
    meter_epoch = epoch;
    br_draw_status(force);
}

static void br_mixer_name(const char *name, char first[7], char second[7])
{
    uint32_t n = 0, split, i;
    while (name[n] && n < 6u) n++;
    if (name[n] && name[n] != ' ') {
        split = n;
        while (split && name[split] != ' ') split--;
        if (split) n = split;
        else {
            /* Abbreviate an overlong word instead of orphaning one letter. */
            for (i = 0; i < 5u; i++) first[i] = name[i];
            first[5] = '.'; first[6] = 0;
            while (name[n] && name[n] != ' ') n++;
            while (name[n] == ' ') n++;
            br_text_copy(second, name + n, 7);
            return;
        }
    }
    for (i = 0; i < n; i++) first[i] = name[i];
    first[n] = 0;
    while (name[n] == ' ') n++;
    br_text_copy(second, name + n, 7);
}

/* Four fixed channel strips line up with knobs 1..4. Reuse the small canvas
 * for each clipped strip, and repaint only channels whose state has changed. */

/* ---------------------------------------------------------------------------------------------- mixer meters */
/* Each track's signal level: the peak of the last block it rendered (the tracks are mono until the effects),
 * shown as a column on each side of its fader, spaced evenly in dB from -60 to 0 dBFS. The columns fall one
 * step per frame and jump up at once. */
static void br_meter_peak(uint32_t p, uint32_t *left, uint32_t *right)
{
#if BR_AUDIO_FUSION_ONLY_BUILD
    *left = *right = br_fx_meter_read(p);
#else
    uint32_t peak = 0u;
    for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
        int32_t v = bus[p][i];
        uint32_t magnitude = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
        if (magnitude > peak) peak = magnitude;
    }
    *left = *right = peak > 32768u ? 32768u : peak;
#endif
}
/* How many of 28 steps a peak (0..32768) lights: each step is -60/28 dB, so full scale lights all 28 and
 * anything under -60 dBFS lights none. `count` is the number of steps to report (28 here). */
static uint32_t br_meter_segments(uint32_t peak, uint32_t count)
{
    float threshold = 32768.0f;
    uint32_t n = count;
    while (n) {
        if ((float)peak >= threshold) return n;
        threshold *= 0.7813f; --n;                            /* 10^(-60/20/28) */
    }
    return 0u;
}
#define BR_METER_STEPS 28u
static uint8_t br_meter_hold[BR_PARTS];                        /* the lit steps now shown, falling */
static uint8_t br_meter_drawn[BR_PARTS] = {255, 255, 255, 255};
/* The fader's own centre line is the level meter: lit from the bottom by the track's signal (-60 to 0 dBFS
 * over the fader's travel), red in the top step. The fader cap is drawn back over it. */
static void br_draw_meter_line(uint32_t p, uint32_t force)
{
    const volatile br_sound_t *s = &br_part[p].sound;
    uint32_t left, right, now;
    int32_t rows = 114, travel = 112;
    int32_t fader = (int32_t)((uint32_t)br_clip((int32_t)s->level, 0, 127) * (uint32_t)travel / 127u);
    uint32_t selected = p == br_ui.part, dim = s->mute || (br_solo_mask && !((br_solo_mask >> p) & 1u));
    uint16_t color = dim ? BR_DIM : br_track_color(p);
    int32_t lit, red, cap;
    br_meter_peak(p, &left, &right);
    now = br_meter_segments(left > right ? left : right, BR_METER_STEPS);
    if (now >= br_meter_hold[p]) br_meter_hold[p] = (uint8_t)now;
    else if (br_meter_hold[p]) --br_meter_hold[p];
    if (!force && br_meter_hold[p] == br_meter_drawn[p]) return;
    br_meter_drawn[p] = br_meter_hold[p];
    lit = (int32_t)br_meter_hold[p] * rows / (int32_t)BR_METER_STEPS;
    red = br_meter_hold[p] >= BR_METER_STEPS ? rows / (int32_t)BR_METER_STEPS : 0;
    cap = br_clip((111 - fader) & ~1, 0, rows - 6);                               /* the cap's first row, in line rows */
    cv_begin(2, (uint32_t)rows, BR_LINE);
    if (lit > 0) cv_rect(0, rows - lit, 2, lit, color);
    if (red > 0) cv_rect(0, rows - lit, 2, red, RGB(240, 60, 48));
    cv_rect(0, cap, 2, 6, dim ? BR_DIM : selected ? C_WHITE : br_track_color(p));
    cv_blit(4u + p * 60u + 26u, 34u + 36u);
}

static void br_draw_mixer(uint32_t force)
{
    static uint32_t channel_old[BR_PARTS];
    uint32_t p, h, redrawn = 0u;
    for (p = 0; p < BR_PARTS; ++p) {
        const volatile br_sound_t *s = &br_part[p].sound;
        uint32_t level = (uint32_t)br_clip((int32_t)s->level, 0, 127);
        uint32_t selected = p == br_ui.part, muted = s->mute != 0, solo = (br_solo_mask >> p) & 1u, offset;
        uint32_t excluded = br_solo_mask && !solo;
        int32_t travel = 112;
        uint16_t bg = selected ? BR_CARD : BR_BACKGROUND;
        uint16_t track_color = br_track_color(p);
        int32_t height = (int32_t)(level * (uint32_t)travel / 127u);
        int32_t cap = br_clip((111 - height) & ~1, 0, 108);
        h = (((level * 31u + muted) * 31u + selected) * 31u + solo) * 31u + excluded;
        if (!force && h == channel_old[p]) continue;
        for (offset = 0; offset < 176u; offset += 40u) {
            uint32_t tick;
            uint32_t rows=176u-offset;cv_begin(54, rows<40u?rows:40u, bg);
            cv_oy = -(int32_t)offset;
            if (selected) {
                br_art_line(0, 0, 53, 0, BR_LINE); br_art_line(0, 175, 53, 175, BR_LINE);
                br_art_line(0, 0, 0, 175, BR_LINE); br_art_line(53, 0, 53, 175, BR_LINE);
            }
            br_track_badge(12, 6, p, 0);
            br_control_frame(8, 32, 38, 124);
            br_art_rect(26, 36, 2, 114, BR_LINE);
            for (tick = 0; tick <= 4u; ++tick) {
                int32_t y = 36 + (int32_t)tick * travel / 4;
                br_art_line(15, y, 18, y, BR_LINE); br_art_line(36, y, 39, y, BR_LINE);
            }
            br_art_rect(16, 36 + cap, 22, 6, muted || excluded ? BR_DIM : selected ? C_WHITE : track_color);
            if (muted) {
                cv_rect(9,158,13,12,RGB(16,144,132));
                cv_rect(8,159,15,10,RGB(16,144,132));
            }
            if (solo) {
                cv_rect(33,158,13,12,BR_GLOBAL_ACC);
                cv_rect(32,159,15,10,BR_GLOBAL_ACC);
            }
            br_widget_text(8 + (15 - br_widget_text_width("M")) / 2, 161, "M", muted ? C_BLACK : BR_DIM);
            br_widget_text(32 + (15 - br_widget_text_width("S")) / 2, 161, "S", solo ? C_BLACK : BR_DIM);
            cv_oy = 0;
            cv_blit(4u + p * 60u, 34u + offset);
        }
        channel_old[p] = h;
        redrawn |= 1u << p;
    }
    for (p = 0; p < BR_PARTS; ++p) br_draw_meter_line(p, force || ((redrawn >> p) & 1u));
    br_draw_status(force);
}

/* FX send screen: the mixer's four strips, with a colour bar for each of the
 * current track's drive, delay, modulation and reverb levels. Each strip has an
 * effect icon where the mixer shows T1..DK and its name under the bar, where the mixer has M and S.
 * Drive, delay and modulation take the current track's colour; reverb is global and keeps the
 * global (goldenrod) colour. */
/* Kit strips retain the full height; FX sends leave room for the shortcut cue. */
#define BR_FX_STRIP_H 192u
#define BR_FX_SEND_STRIP_H 176u
static void br_fx_icon(int32_t x, int32_t y, uint32_t k, uint16_t color)
{
    uint32_t i;
    br_art_rect(x + 2, y, 26, 20, color);
    br_art_rect(x, y + 2, 30, 16, color);
    if (k == 0u) {                      /* drive: a lightning bolt */
        br_art_line(x + 18, y + 2, x + 10, y + 11, C_BLACK);
        br_art_line(x + 10, y + 11, x + 16, y + 11, C_BLACK);
        br_art_line(x + 16, y + 11, x + 12, y + 18, C_BLACK);
        br_art_line(x + 19, y + 2, x + 11, y + 11, C_BLACK);
        br_art_line(x + 17, y + 11, x + 13, y + 18, C_BLACK);
        br_art_line(x + 12, y + 18, x + 21, y + 8, C_BLACK);
        br_art_line(x + 21, y + 8, x + 16, y + 8, C_BLACK);
    } else if (k == 1u) {               /* delay: a bar graph */
        static const uint8_t height[5] = {6, 10, 14, 10, 6};          /* mirrored about the icon's centre */
        for (i = 0u; i < 5u; ++i) br_art_rect(x + 6 + (int32_t)i * 4, y + 18 - height[i], 2, height[i], C_BLACK);
    } else if (k == 2u) {               /* modulation: a wave */
        static const int8_t wave[7][2] = {{4, 10}, {8, 4}, {12, 10}, {16, 16}, {20, 10}, {24, 4}, {26, 7}};
        for (i = 0u; i < 6u; ++i)
            br_art_line(x + wave[i][0], y + wave[i][1], x + wave[i + 1u][0], y + wave[i + 1u][1], C_BLACK);
    } else {                            /* reverb: nested arches, a hall receding */
        static const int8_t arch[3][2] = {{11, 2}, {7, 6}, {3, 10}};   /* half width, top */
        int32_t cx = x + 15, base = y + 17;
        for (i = 0u; i < 3u; ++i) {
            int32_t w = arch[i][0], top = y + arch[i][1];
            int32_t l = cx - w, r = cx + w - 1;        /* the icon is 30 wide: mirror pixels sum to 2 * cx - 1 */
            br_art_line(l, base, l, top + 3, C_BLACK);
            br_art_line(l, top + 3, l + 3, top, C_BLACK);
            br_art_line(l + 3, top, r - 3, top, C_BLACK);
            br_art_line(r - 3, top, r, top + 3, C_BLACK);
            br_art_line(r, top + 3, r, base, C_BLACK);
        }
    }
}

static void br_draw_fx_send(uint32_t force)
{
    /* Top to bottom: the bar, its name (as far below the bar as the mixer's M and S), the icon. */
    /* Match the mixer badge's top at strip row 6; retain the old bottom,
     * label and icon positions so the fader is shortened from the top only. */
    const int32_t top = 6, frame_bottom = 134;
    const int32_t label_y = frame_bottom + 5, icon_y = label_y + 15;
    uint32_t k, p = br_ui.part;
    static uint32_t strip_old[4];
    for (k = 0; k < 4u; ++k) {
        br_control_t c = br_control(k);
        uint32_t routes = br_control_route_badges(k);
        int32_t badge_height = (int32_t)br_badge_count(routes) * 10;
        int32_t frame_h = frame_bottom - top - badge_height, track_h = frame_h - 10, travel = frame_h - 12;
        uint32_t value = (uint32_t)br_clip(c.value, 0, 127), active = k == br_active_knob();
        uint16_t bg = active ? BR_CARD : BR_BACKGROUND;
        uint16_t color = k == 3u ? BR_GLOBAL_ACC : br_track_color(p);
        int32_t height = (int32_t)(value * (uint32_t)travel / 127u);
        uint32_t offset, h = ((value * 31u + active) * 31u + p) * 31u + k;
        h = (h * 67u + routes) * 67u + br_badge_pulse_key(p, routes);
        if (!force && h == strip_old[k]) continue;
        for (offset = 0; offset < BR_FX_SEND_STRIP_H; offset += 40u) {
            uint32_t tick, rows = BR_FX_SEND_STRIP_H - offset;
            cv_begin(54, rows < 40u ? rows : 40u, bg);
            cv_oy = -(int32_t)offset;
            if (active) {
                br_art_line(0, 0, 53, 0, BR_LINE); br_art_line(0, (int32_t)BR_FX_SEND_STRIP_H - 1, 53, (int32_t)BR_FX_SEND_STRIP_H - 1, BR_LINE);
                br_art_line(0, 0, 0, (int32_t)BR_FX_SEND_STRIP_H - 1, BR_LINE); br_art_line(53, 0, 53, (int32_t)BR_FX_SEND_STRIP_H - 1, BR_LINE);
            }
            br_control_frame(8, top, 38, frame_h);
            br_art_rect(16, top + 4, 22, track_h, BR_LINE);
            if (height) br_art_rect(16, top + 4 + track_h - height, 22, height, color);
            for (tick = 0; tick <= 4u; ++tick) {
                int32_t y = top + 4 + (int32_t)tick * travel / 4;
                br_art_line(10, y, 13, y, BR_LINE); br_art_line(41, y, 44, y, BR_LINE);
            }
            br_art_rect(14, top + 1 + track_h - height, 26, 5, C_WHITE);
            br_draw_route_badges(27, top + frame_h, routes, p, bg);
            {
                static const char *const send_name[4] = {"DRIVE", "DELAY", "MOD", "REVERB"};
                br_widget_text(27 - br_widget_text_width(send_name[k]) / 2, label_y, send_name[k], active ? C_WHITE : BR_DIM);
            }
            br_fx_icon(12, icon_y, k, color);
            cv_oy = 0;
            cv_blit(4u + k * 60u, 34u + offset);
        }
        strip_old[k] = h;
    }
    br_draw_status(force);
}

static void br_draw_tonal_steps(uint32_t force)
{
    static uint32_t old_cell[BR_BANK_STEPS], old_control[4];
    uint32_t p = br_ui.part, base = br_ui.step_bank * BR_BANK_STEPS, cell, k;
    for (cell = 0; cell < BR_BANK_STEPS; ++cell) {
        uint32_t at = base + cell, word = br_part[p].step[at];
        uint32_t enabled = at < br_part[p].length, active = enabled && (word & BR_STEP_ON);
        uint32_t selected = br_ui.step_edit_active && at == br_ui.step_press_step,
                 playhead = br_playing && at == br_step_pos[p];
        uint32_t coverage = br_step_length_coverage(p, at, 0u, 0u);
        uint32_t condition = br_step_condition_get(p, at, 0u);
        uint32_t h = word ^ at << 8 ^ selected << 16 ^ playhead << 17 ^ enabled << 18 ^ p << 20;
        uint32_t width;
        h = h * 31u + br_step_length_get(p, at, 0u);
        h = h * 31u + condition;
        h = h * 31u + coverage;
        char number[8], note[16];
        uint16_t bg = active ? BR_CARD : BR_BACKGROUND;
        if (!force && h == old_cell[cell]) continue;
        cv_begin(54, 28, bg);
        if (coverage) {
            width = (coverage * 52u + 15u) / 16u;
            cv_rect(1, 1, (int32_t)width, 25, BR_LENGTH_BG);
        }
        br_number(number, (int32_t)at + 1);
        br_widget_text(3, 2, number, enabled ? BR_DIM : BR_LINE);
        br_note_text(note, active ? (int32_t)BR_STEP_NOTE(word) : -1);
        if (active) {
            br_widget_text((54 - br_widget_text_width(note)) / 2, 13, note, C_WHITE);
            br_art_rect(3, 25, (int32_t)(BR_STEP_VEL(word) * 48u / 127u), 1, BR_DIM);
            if (condition != BR_STEP_COND_ALWAYS) br_art_rect(46, 3, 3, 3, BR_ACC);
            if (word & BR_STEP_TIE) br_art_line(43, 4, 50, 4, BR_ACC);
        } else br_widget_text(24, 16, "-", enabled ? BR_DIM : BR_LINE);
        br_art_line(0, 27, 53, 27, BR_LINE);
        if (selected || playhead) {
            uint16_t edge = playhead ? BR_ACC : C_WHITE;
            br_art_line(0, 0, 53, 0, edge); br_art_line(0, 26, 53, 26, edge);
            br_art_line(0, 0, 0, 26, edge); br_art_line(52, 0, 52, 26, edge);
        }
        cv_blit(4u + (cell & 3u) * 60u, 42u + (cell >> 2) * 32u);
        old_cell[cell] = h;
    }
    for (k = 0; k < 4u; ++k) {
        br_control_t c = br_control(k);
        char value[24];
        uint32_t h;
        br_control_text(k, &c, value);
        h = br_hash_text(value) ^ br_hash_text(c.label) ^ p ^
            br_ui.cursor << 8 ^ br_ui.lane << 16 ^ (k == br_active_knob() ? 0x80000000u : 0u);
        if (!force && h == old_control[k]) continue;
        br_draw_seq_control(k, &c, value);
        cv_blit(4u + k * 60u, 170);
        old_control[k] = h;
    }
    br_draw_status(force);
}

/* The label above the controls names the LFO or patch being edited. */
static uint32_t br_page_route_badge(void)
{
    if (br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] < 2u)
        return br_ui.screen[BR_PAGE_LFO];
    if (br_ui.page == BR_PAGE_ROUTE && !br_gate_page() && !br_sound_mod_page() && br_patch_slot() < 2u)
        return 2u + br_patch_slot();
    return 6u;
}
/* Shared page badge geometry for FX, LFO and patch headers. */
static void br_draw_page_badge(const char *label, uint16_t color)
{
    int32_t w = (br_widget_text_width(label) + 14) & ~1, x = (240 - w) / 2;
    br_art_rect(x + 2, 0, w - 4, 16, color);
    br_art_rect(x, 2, w, 12, color);
    br_widget_text(x + (w - br_widget_text_width(label)) / 2, 4, label, C_BLACK);
}
static void br_draw_page_route_badge(uint32_t force, uint32_t part)
{
    static uint32_t previous = 0xffffffffu;
    uint32_t bit = br_page_route_badge();
    uint32_t bits = bit < 6u ? 1u << bit : 0u;
    uint32_t screen = br_ui.screen[br_ui.page];
    uint32_t punch = br_ui.page == BR_PAGE_PUNCH && br_punch_ks.shift ? br_punch_ks.active : 0u;
    uint32_t override = br_env_direct_overridden(part) &&
        ((br_home_filter_page() && br_part[part].mod.target == BR_TARGET_FILTER) ||
         (br_ui.page == BR_PAGE_ENV && screen == 2u &&
          br_mod_target_supported(br_part[part].sound.preset, br_part[part].mod.target)));
    uint32_t scope = br_ui.page == BR_PAGE_FX && screen == BR_FX_REVERB && br_active_knob() == 3u;
    uint32_t hash = (((bit + part * 7u) * 131u + br_ui.page) * 31u + screen) ^ br_badge_pulse_key(part, bits);
    hash = ((hash * 5u + punch) * 2u + override) * 2u + scope;
    if (br_ui.page == BR_PAGE_MIX && screen >= 7u)
        hash = ((hash * 2u + br_midi_clock_enabled) * 2u + br_midi_clock_active) * 257u + br_midi_clock_bpm;
    if (br_retrig_page()) hash = ((hash * 67u + br_ui.cursor) * 5u + br_ui.lane) * 2u + br_retrig_editing();
    if (!force && hash == previous) return;
    cv_begin(240, 20, BR_BACKGROUND);
    if (override) br_draw_page_badge("PATCH OVERRIDE", BR_GLOBAL_ACC);
    if (bits) br_draw_named_badge_scaled(120, 1, br_route_badge_name[bit], br_route_badge_tint(bit, part, BR_BACKGROUND), 2);
    if (br_ui.page == BR_PAGE_FX && screen != BR_FX_SENDS) {
        const char *label = screen == BR_FX_REVERB && !scope ? "GLOBAL" : "TRACK";
        uint16_t color = screen == BR_FX_REVERB && !scope ? BR_GLOBAL_ACC : br_track_color(part);
        br_draw_page_badge(label, color);
    }
    if (br_ui.page == BR_PAGE_MIX && screen >= 7u) {
        char clock[24], number[12];
        if (br_midi_clock_active) {
            br_text_copy(clock, "MIDI ", sizeof clock); br_number(number, (int32_t)br_midi_clock_bpm);
            br_text_append(clock, number, sizeof clock); br_text_append(clock, " BPM", sizeof clock);
        } else br_text_copy(clock, br_midi_clock_enabled ? "CLOCK WAIT" : "CLOCK OFF", sizeof clock);
        br_draw_page_badge(clock, BR_GLOBAL_ACC);
    }
    if (br_ui.page == BR_PAGE_MIX && screen == 2u) {
        const char *label = br_version_text();
        int32_t w = (br_widget_text_width(label) + 14) & ~1, x = (240 - w) / 2;
        br_art_rect(x + 2, 0, w - 4, 16, BR_LINE);
        br_art_rect(x, 2, w, 12, BR_LINE);
        br_widget_text(x + (w - br_widget_text_width(label)) / 2, 4, label, C_WHITE);
    }
    if (punch) {
        static const char *const names[5] = {"", "LOOP", "ECHO", "FILTER", "TREMOLO"};
        br_draw_named_badge_scaled(120, 1, names[punch], BR_ACC, 2);
    }
    if (br_retrig_page()) {
        char label[16], number[8];
        br_text_copy(label, "D", sizeof label); br_number(number, (int32_t)br_ui.lane + 1); br_text_append(label, number, sizeof label);
        br_text_append(label, " / ", sizeof label); br_number(number, (int32_t)br_ui.cursor + 1); br_text_append(label, number, sizeof label);
        br_draw_named_badge_scaled(120, 1, label, br_retrig_editing() ? BR_ACC : BR_LINE, 2);
    }
    cv_blit(0, 46);
    previous = hash;
}

/* Page position follows navigation order, including the reordered Global pages. */
static void br_draw_page_dots(uint32_t force)
{
    static uint32_t previous = 0xffffffffu;
    static const uint8_t global_order[7] = {0u, 1u, 3u, 6u, 4u, 5u, 2u};
    uint32_t count = br_screen_count(), at = br_ui.screen[br_ui.page], h;
    if (count < 2u) return;
    if (!br_is_kit(br_part[br_ui.part].sound.preset) &&
        (br_ui.page == BR_PAGE_LFO || br_ui.page == BR_PAGE_ROUTE)) {
        count = BR_LFOS + 3u;
        if (br_ui.page == BR_PAGE_ROUTE) at += BR_LFOS;
    }
    if (br_ui.page == BR_PAGE_MIX && at >= 7u) at -= 7u;
    else if (br_ui.page == BR_PAGE_MIX) {
        uint32_t i;
        for (i = 0u; i < 7u && global_order[i] != at; ++i) {}
        at = i < 7u ? i : 0u;
    }
    h = ((br_ui.page * 31u + at) * 31u + count) * 31u + br_ui_accent;
    if (!force && h == previous) return;
    cv_begin(240, 8, BR_BACKGROUND);
    for (uint32_t i = 0; i < count; ++i)
        cv_rect(120 - (int32_t)(count * 8u - 4u) / 2 + (int32_t)i * 8, 2, 4, 4,
                i == at ? br_ui_accent : BR_LINE);
    cv_blit(0, 208);
    previous = h;
}

static uint32_t br_draw_layout_old = 0xFFFFFFFFu;
/* Repaint only the incoming layout's uncovered background.
 * The header, controls and status are replaced by their normal canvas blits,
 * so they remain visible until their new pixels are ready. */
/* SEL sound bank: five rows; the highlighted sound is what the keys play. SEL again keeps it. */
static void br_sel_row_text(char *out, uint32_t n)
{
    out[0] = (char)('0' + (n + 1u) / 10u); out[1] = (char)('0' + (n + 1u) % 10u); out[2] = ' ';
    br_text_copy(out + 3, br_sel_entry_name(br_ui.part, n), 12u);
}
/* A small dial: the chip that tells you which knob drives a list. Native 1x1 grid. */
static void br_knob_icon(int x, int y, uint16_t color)
{
    br_shape_circle_grid(bw_q8(x + 5), bw_q8(y + 5), 5u * 256u, 256u, color, 0u, 1u);
    cv_rect(x + 5, y + 2, 1, 4, color);
}
static void br_draw_sel(uint32_t force)
{
    static uint32_t old;
    uint32_t p = br_ui.part, cur = br_sel.cursor[p], top = br_sel.top[p], h, y, row;
    uint32_t count = p == BR_DRUM_PART ? BR_KIT_BANK_COUNT : BR_BANK_COUNT;
    char text[16], head[24];
    uint32_t chip;
    h = (((cur * 64u + top) * 2u + (br_sel.shown && !br_sel.loaded)) * 2u + br_sel.undo_ready) * 4u + p;
    h = h * 31u + br_part[p].sound.preset;
    chip = br_sel.knob_ms && fm1_ms - br_sel.knob_ms < 400u;
    h = h * 3u + chip;
    if (!force && h == old) { br_draw_status(0u); return; }
    old = h;
    for (y = 34u; y < 214u; y += 40u) {
        uint32_t rows = 214u - y < 40u ? 214u - y : 40u;
        cv_begin(240, rows, BR_BACKGROUND);
        cv_oy = -(int32_t)y;
        if (p == BR_DRUM_PART) br_text_copy(head, "DRUM KITS", sizeof head);
        else br_text_copy(head, br_bank_name[cur / BR_BANK_SIZE], sizeof head);
        /* The SAVE menu's knob chip: it says knob 1 scrolls this list, and fills while it does. */
        cv_line(5, 73, 64, 73, C_WHITE); cv_line(3, 75, 3, 203, C_WHITE);
        cv_pset(4, 74, C_WHITE); cv_pset(65, 74, C_WHITE); cv_line(66, 75, 66, 87, C_WHITE);
        cv_pset(65, 88, C_WHITE); cv_line(3, 89, 64, 89, C_WHITE);
        if (chip) cv_rect(4, 74, 62, 15, br_track_color(p));
        br_knob_icon(8, 76, chip ? C_BLACK : C_WHITE);
        br_widget_text(24, 78, "KNOB1", chip ? C_BLACK : C_WHITE);
        br_widget_text(92, 78, head, BR_DIM);
        br_number(text, (int32_t)cur + 1); br_text_append(text, p == BR_DRUM_PART ? (BR_KIT_BANK_COUNT == 12u ? "/12" : "/9") : "/64", sizeof text);
        br_widget_text(232 - br_widget_text_width(text), 78, text, BR_DIM);
        for (row = 0u; row < BR_SEL_ROWS; ++row) {
            uint32_t n = top + row, selected = n == cur;
            int32_t at = 96 + (int32_t)row * 22;
            char line[16];
            if (selected) {
                br_art_rect(4, at - 3, 218, 20, BR_ACC);
                br_art_rect(222, at - 1, 2, 16, BR_ACC);
            }
            br_sel_row_text(line, n);
            br_widget_text_2x(12, at, line, selected ? C_BLACK : C_WHITE);
        }
        cv_oy = 0;
        cv_blit(0, y);
    }
    br_draw_status(1u);
}

static void br_repaint_layout_background(uint32_t layout)
{
    uint32_t k;
    if (layout == 9u) {
        br_background_rect(0, 32, 240, 198);
    } else if (layout == 0u || layout == 6u) {
        br_background_rect(0, 32, 240, 48);
        if (layout == 6u) br_background_rect(0, 194, 240, 22);
    } else if (layout == 5u) {
        br_background_rect(0, 32, 240, 8);
    } else if (layout == 1u || layout == 11u) {
        uint32_t rows_end = 50u + BR_DRUM_LANES * 18u;
        br_background_rect(0, 32, 240, 4);
        br_background_rect(0, rows_end, 240, 162u - rows_end);
        br_background_rect(0, 214, 240, layout == 11u ? 26 : 16);
        for (k = 0; k < 4u; ++k) {
            br_background_rect(k * 60u, 162, 4, 52);
            br_background_rect(k * 60u + 58u, 162, 2, 52);
        }
    } else if (layout == 2u || layout == 8u || layout == 10u) {   /* the mixer and the FX send bars share this frame */
        uint32_t strip_h = 176u, bottom = 34u + strip_h;
        br_background_rect(0, 32, 240, 2);
        br_background_rect(0, bottom, 240, bottom < 216u ? 216u - bottom : 230u - bottom);
        for (k = 0; k < 4u; ++k) {
            br_background_rect(k * 60u, 34, 4, strip_h);
            br_background_rect(k * 60u + 58u, 34, 2, strip_h);
        }
    } else if (layout == 7u) {                   /* SEL list: header gap and rows above cue */
        br_background_rect(0, 32, 240, 2);
        br_background_rect(0, 214, 240, 2);
    } else if (layout == 3u) {
        br_background_rect(0, 32, 240, 10);
        br_background_rect(0, 222, 240, 8);
        for (k = 0; k < 4u; ++k) {
            br_background_rect(0, 70u + k * 32u, 240, 4);
            br_background_rect(k * 60u, 42, 4, 180);
            br_background_rect(k * 60u + 58u, 42, 2, 180);
        }
    } else if (layout == 4u) {
        uint32_t rows_end = 54u + BR_DRUM_LANES * 18u;
        br_background_rect(0, 32, 240, 4);
        br_background_rect(0, 50, 240, 4);
        br_background_rect(0, rows_end, 240, 204u - rows_end);
        br_background_rect(0, 218, 240, 12);
    }
}


/* ------------------------------------------------------------------------------------------------ boot intro */
#define BR_INTRO_MS 2000u
#include "../gen/jamesos_start_rle.inc"

/* Expand the native-size RGB565 art into six 40-row canvases.
 * Runs cross strip boundaries; all decode state stays on the stack. */
static void br_intro_frame(void)
{
    uint32_t strip, pixel, offset = 0u, remaining = 0u;
    uint16_t color = 0u;
    for (strip = 0u; strip < 6u; ++strip) {
        cv_begin(240u, 40u, C_BLACK);
        for (pixel = 0u; pixel < 240u * 40u; ++pixel) {
#if BR_STARTUP_ART_CROPPED
            uint32_t row = strip * 40u + pixel / 240u;
            if (row < BR_STARTUP_ART_TOP || row >= BR_STARTUP_ART_BOTTOM) continue;
#endif
            if (!remaining) {
#if BR_STARTUP_ART_CROPPED
                uint32_t run = br_startup_art_rle[offset++];
                remaining = run & 15u;
                if (!remaining) remaining = br_startup_art_rle[offset++];
                color = br_startup_art_palette[run >> 4];
#elif BR_STARTUP_ART_SHORT_RUNS
                uint32_t run = br_startup_art_rle[offset++];
                remaining = run & 7u;
                if (!remaining) remaining = br_startup_art_rle[offset++];
                color = br_startup_art_palette[run >> 3];
#else
                remaining = br_startup_art_rle[offset++];
                color = br_startup_art_palette[br_startup_art_rle[offset++]];
#endif
            }
            --remaining;
            cv_px[pixel] = swap16(color);
        }
        lcd_blit(0u, strip * 40u, 240u, 40u, cv_px);
    }
}

static void br_draw_body(void)
{
    static uint32_t top_old, card_old[4], strip_old, comp_old;
    static uint32_t background_part = 0xffffffffu;
    static uint8_t compressor_visible, dots_visible, snap_cue_visible;
    uint32_t k, h, force = br_ui.redraw, p = br_ui.part, dots_force = !dots_visible || background_part != br_ui.part || br_draw_layout_old == 0xffffffffu;
    int32_t battery_level;
    uint32_t matrix = br_retrig_page() || (br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && br_is_kit(br_part[p].sound.preset));
    uint32_t fxsend = br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == BR_FX_SENDS;
    uint32_t mixer = (br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]) || fxsend;
    uint32_t perform = (br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u) || br_home_filter_page();
    uint32_t compressor = br_ui.page == BR_PAGE_MIX && (br_ui.screen[BR_PAGE_MIX] == 4u || br_ui.screen[BR_PAGE_MIX] == 5u);
    uint32_t tonal = br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && !br_is_kit(br_part[p].sound.preset);
    uint32_t gates = br_gate_page();
    br_audio_kit_meter_enable(gates);
    br_fx_meter_enable(br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]);
    uint32_t envelope = br_ui.page == BR_PAGE_ENV && br_ui.screen[BR_PAGE_ENV] < 2u;
    uint32_t sel = br_sel_browse();
    uint32_t reverb_screen;
    uint32_t layout = sel ? 7u : matrix ? (br_retrig_page() ? 11u : 1u) : fxsend ? 8u : mixer ? 2u : tonal ? 3u : gates ? 10u : envelope ? 5u : perform ? 6u : 0u;
    uint32_t layout_changed = br_draw_layout_old != layout || background_part != p;
    char text[64], number[16];
    const char *name = br_header_title();
    br_clamp_ui_lane();
    for (uint32_t part = 0; part < BR_PARTS; ++part) {
        for (uint32_t lfo = 0; lfo < BR_LFOS; ++lfo) {
            br_badge_lfo_phase[part][lfo] = (uint8_t)br_audio_lfo_phase(part, lfo);
            br_badge_lfo_depth[part][lfo] = (uint8_t)br_clip((int32_t)br_part[part].mod.lfo[lfo].depth, 0, 127);
        }
        for (uint32_t slot = 0; slot < 2u; ++slot)
            br_badge_patch_value[part][slot] = (int8_t)br_audio_patch_value(part, slot);
    }
    /* Reverb is one global effect, so its screen takes the global (goldenrod) colour. */
    reverb_screen = br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == BR_FX_REVERB;
    br_ui_accent = br_ui.page == BR_PAGE_MIX || br_ui.page == BR_PAGE_PUNCH || reverb_screen ? BR_GLOBAL_ACC : br_track_color(p);
    if (layout_changed) {
        /* The mixer and the drum grid keep a MUTE/SOLO-style cue bar in the last 24 rows; no other layout
         * paints there, so wipe it when leaving them. */
        uint32_t cue_left = (br_draw_layout_old == 1u || br_draw_layout_old == 2u || br_draw_layout_old == 7u || br_draw_layout_old == 8u || br_draw_layout_old == 10u) &&
                            layout != 1u && layout != 2u && layout != 7u && layout != 8u && layout != 10u;
        if (br_draw_layout_old == 9u || br_draw_layout_old == 10u) br_background_rect(0, 32, 240, 2);
        br_repaint_layout_background(layout);
        if (layout == 0u) br_background_rect(0, 208, 240, 8);
        if (cue_left) br_background_rect(0, 216, 240, 24);
        br_draw_layout_old = layout; background_part = p; force = 1;
    } else if (layout) force = 0;
    if (snap_cue_visible && !br_fx_sends_page()) {
        br_background_rect(0, 216, 240, 24); force = 1u;
    }
    snap_cue_visible = br_fx_sends_page();
    /* Only centered parameter layouts have an unobstructed eight-pixel strip here. */
    {
        uint32_t dots = (layout == 0u || layout == 5u || layout == 6u) && br_screen_count() > 1u;
        if (!dots && dots_visible) br_background_rect(0, 208, 240, 8);
        if (dots && (!dots_visible || layout_changed)) br_background_rect(0, 208, 240, 8);
        dots_visible = (uint8_t)dots;
    }
    /* The compressor readout occupies a strip shared by ordinary pages.
     * Clear it when leaving either compressor screen, including to Settings. */
    if (compressor_visible != compressor) {
        br_background_rect(0, 38, 240, 28);
        force = 1u;
    }
    compressor_visible = (uint8_t)compressor;
    battery_level = fm1_battery_level();
    h = br_hash_text(name) ^ (p << 20) ^ (br_part[p].sound.preset << 24) ^
        br_bpm ^ (br_playing << 31) ^
        (br_playing ? ((br_transport_phase >> 3) << 12) ^ ((br_global_beat & 1u) << 19) : 0u) ^
        ((uint32_t)(battery_level + 1) << 8);
    if (force || h != top_old) {
        cv_begin(240, 24, BR_BACKGROUND);
        if ((br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] != 0u) || br_ui.page == BR_PAGE_PUNCH)
            br_global_badge(6, 2);
        else br_track_badge(6, 2, p, 0);
        br_header_draw_title(name);
        br_number(number, (int32_t)br_bpm);
        br_metronome_icon(197);
        br_widget_text(223 - br_widget_text_width(number) / 2, 13, number, C_WHITE);
        br_battery_icon(battery_level);
        cv_blit(0, 0); top_old = h;
    }
    if (br_ui.page == BR_PAGE_MIX)
        h = 0x47504c4fu ^ (br_playing << 31) ^ (br_global_beat << 4) ^ (br_transport_phase >> 5);
    else {
        h = br_part[p].sound.preset ^ p << 24 ^ br_ui.cursor << 8 ^ br_ui.lane << 16 ^ br_playing << 31;
        h = h * 31u + br_ui.page;
        h = h * 31u + br_ui.step_bank;
        h = (h * 31u + br_ui.step_press_step) * 3u + br_ui.step_edit_active;
        h = h * 31u + br_part[p].length;
        for (k = 0; k < BR_STEPS; k++) h = h * 31u + br_part[p].step[k];
        h = h * 31u + br_step_pos[p];
    }
    if (force || h != strip_old) {
        cv_begin(240, 8, BR_BACKGROUND);
        for (k = 0; k < BR_BANK_STEPS; k++) {
            int32_t x = 8 + (int32_t)k * 14;
            if (br_ui.page == BR_PAGE_MIX) {
                uint32_t current=br_global_beat*4u+(br_transport_phase >> 5);
                br_art_rect(x, 2, 11, 4, br_playing && k==current ? RGB(100,110,112) : BR_LINE);
                continue;
            }
            uint32_t at = br_ui.step_bank * BR_BANK_STEPS + k;
            uint32_t step = br_part[p].step[at], active = br_is_kit(br_part[p].sound.preset) ?
                (BR_STEP_HIT(step) >> br_ui.lane) & 1u : (step & BR_STEP_ON) != 0;
            br_art_rect(x, 2, 11, 4, at >= br_part[p].length ? RGB(16, 19, 20) : active ? BR_ACC : BR_LINE);
            if (br_seq_grid_page() ? (br_ui.step_edit_active && at == br_ui.step_press_step) :
                at == br_ui.cursor) br_art_rect(x, 7, 11, 1, C_WHITE);
            if (br_playing && at == br_step_pos[p]) br_art_rect(x + 3, 0, 5, 1, C_WHITE);
        }
        cv_blit(0, 24); strip_old = h;
    }
    if (sel) {
        br_draw_sel(force);
        br_ui.redraw = 0;
        return;
    }
    if (mixer) {
        if (fxsend) br_draw_fx_send(force);
        else br_draw_mixer(force);
        br_ui.redraw = 0;
        return;
    }
    if (perform) {
        br_draw_perform(force);
        br_draw_page_dots(dots_force);
        br_ui.redraw = 0;
        return;
    }
    if (matrix) {
        br_draw_drum_grid(force);
        br_ui.redraw = 0;
        return;
    }
    if (gates) {
        br_draw_drum_levels(force);
        br_ui.redraw = 0;
        return;
    }
    if (tonal) {
        br_draw_tonal_steps(force);
        br_ui.redraw = 0;
        return;
    }
    if (compressor) {
        uint32_t source = br_master.comp.source, reduction = br_comp_reduction_q15;
        char route[40];
        br_comp_route_text(source, route, sizeof route);
        reduction = (reduction > 32768u ? 32768u : reduction) * 100u / 32768u;
        h = br_hash_text(route) ^ (reduction << 16) ^ (br_ui.screen[BR_PAGE_MIX] << 24);
        if (force || h != comp_old) {
            cv_begin(240, 12, BR_BACKGROUND);
            br_control_frame(80, 0, 80, 12);
            /* Compact segmented reduction meter, centered like the other
             * global readouts; a partial cell preserves smooth movement. */
            uint32_t fill = (reduction * 48u + 50u) / 100u;
            for (uint32_t cell = 0u; cell < 12u; ++cell) {
                int32_t x = 85 + (int32_t)cell * 6;
                uint32_t amount = fill > cell * 4u ? fill - cell * 4u : 0u;
                if (amount > 4u) amount = 4u;
                cv_rect(x, 4, 4, 4, BR_LINE);
                if (amount) {
                    cv_rect(x, 4, (int32_t)amount, 4, BR_GLOBAL_ACC);
                    cv_rect(x, 4, (int32_t)amount, 1, br_palette_color(BR_PARTS, 1u));
                }
            }
            cv_blit(0, 48); comp_old = h;
        }
    }
    if (envelope) {
        br_draw_envelope(force);
        br_draw_page_dots(dots_force);
        br_ui.redraw = 0;
        return;
    }
    if (!compressor) br_draw_page_route_badge(force, p);
    for (k = 0; k < 4u; ++k) {
        br_control_t c = br_control(k);
        uint32_t kind = br_control_widget(k, &c), phase = br_widget_phase(kind);
        uint32_t route_badges = br_control_route_badges(k);
        uint32_t animated = br_widget_evolving(kind) || kind == BR_WIDGET_WAVE || kind == BR_WIDGET_BPM ||
            (kind == BR_WIDGET_DELAY && br_part[p].sound.delay && (br_playing || !br_engine_quiet()));
        br_control_text(k, &c, text);
        h = br_hash_text(text) ^ br_hash_text(c.label) ^ br_ui.page ^ kind << 24;
        h = (h * 67u + route_badges) * 67u + br_badge_pulse_key(p, route_badges);
        if (k == br_active_knob()) h ^= 0x80000000u;
        h = h * 31u + br_ui_accent;
        /* Mix dust follows each actual knob step, even when rounded text is unchanged. */
        if (kind == BR_WIDGET_MIX) h = h * 131u + (uint32_t)c.value;
        if (br_target_control_id(k) != BR_TARGET_COUNT) {
            h = h * 31u + br_target_control_id(k);
            h = h * 31u + br_target_widget_value(br_target_control_id(k));
            h = h * 31u + br_control_widget_variant(k, &c, kind);
            if (kind == BR_WIDGET_ADSR) {
                h = h * 31u + br_part[p].sound.attack;
                h = h * 31u + br_part[p].sound.sustain;
                h = h * 31u + br_part[p].sound.release;
            }
        }
        if (br_source_control_id(k) != BR_SOURCE_COUNT) {
            uint32_t source = br_source_control_id(k);
            h = h * 31u + source;
            h = h * 31u + br_control_widget_variant(k, &c, kind);
            if (kind == BR_WIDGET_ADSR) {
                const volatile br_mod_config_t *m = &br_part[p].mod;
                const volatile br_sound_t *sound = &br_part[p].sound;
                h = h * 31u + (source == BR_SOURCE_ENV ? m->attack : sound->attack);
                h = h * 31u + (source == BR_SOURCE_ENV ? m->decay : sound->decay);
                h = h * 31u + (source == BR_SOURCE_ENV ? m->sustain : sound->sustain);
                h = h * 31u + (source == BR_SOURCE_ENV ? m->release : sound->release);
            }
            if (source == BR_SOURCE_LFO1 || source == BR_SOURCE_LFO2)
                phase = br_audio_lfo_phase(p, source - BR_SOURCE_LFO1);
            else if (source >= BR_SOURCE_LFO1_TRI && source <= BR_SOURCE_LFO2_SQU)
                phase = br_audio_lfo_phase(p, (source - BR_SOURCE_LFO1_TRI) >> 1);
        }
        if (animated) h ^= phase << 16;
        if (force || h != card_old[k]) {
            br_draw_control(k, &c, kind, text, phase, route_badges);
            card_old[k] = h;
        }
    }
    br_draw_status(force);
    br_draw_page_dots(dots_force);
    br_ui.redraw = 0;
}

static uint32_t br_intro_until;
static void br_draw(void)
{
    if (br_intro_until) {                      /* the first two seconds: the name, then the instrument */
        uint32_t left = br_intro_until - fm1_ms;
        if ((int32_t)left > 0) {
            if (br_ui.redraw) { br_intro_frame(); br_ui.redraw = 0u; }
            return;
        }
        br_intro_until = 0u; br_ui.redraw = 1u;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    }
    br_draw_body();
    br_monitor_overlay();
}
