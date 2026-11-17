/* SPDX-License-Identifier: GPL-3.0-only */
/* Repeated module buttons browse pages; SELECT controls the global filter. */
#include "../product.h"
#include "instrument_midi_routes.h"
#include "instrument_plaits_icons.h"
#include "instrument_level_smoothing.h"   /* br_level_law: the LEVEL read-out shows the engine's gain */
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
#define BR_POLY_NOTE_ON(p,n,v,t) br_engine_note_on_tagged((p),(n),(v),(t))
#define BR_POLY_NOTE_OFF(p,n,t) br_engine_note_off_tagged((p),(n),(t))
#else
#define BR_POLY_NOTE_ON(p,n,v,t) br_engine_note_on((p),(n),(v))
#define BR_POLY_NOTE_OFF(p,n,t) br_engine_note_off((p),(n))
#endif
/* BR_POLY_UI: polyphony can become active, so its cue, punch rules and audition guard are compiled in.
 * Device candidates with the polyphony capability, and split host builds (dinghy.c's host model of the
 * foreground transition). Without polyphony it is 0 and none of that code exists. */
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT && \
    ((defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT) || \
     (defined(DINGHY_HOST) && defined(BR_SPLIT_AUDIO)))
#define BR_POLY_UI 1
#else
#define BR_POLY_UI 0
#define br_poly_user_off 0u   /* no POLY switch without polyphony */
#endif
/* BR_SEQ_2X8: the HD sequencer screens as two rows of eight steps (tonal) and an open drum lane; the header
 * strip as a map of every bank. On by default; -DBR_SEQ_2X8=0 keeps the earlier HD sequencer screens. */
#ifndef BR_SEQ_2X8
#define BR_SEQ_2X8 1
#endif
#if BR_PRODUCT_SMK37 && !BR_SEQ_2X8
#error "SMK-37 Pro HD draws its step grid and pattern slots as two rows of eight (pads): BR_SEQ_2X8 must be 1"
#endif
/* BR_ARR_UI: the pattern arranger's PATTERNS / ARRANGE views (pattern_arranger_ui.c), with the arranger engine. */
#if defined(BR_ARRANGER) && BR_SEQ_2X8
#define BR_ARR_UI 1
#else
#define BR_ARR_UI 0
#endif
static br_midi_routes_t br_midi_routes;
/* Remember which local key presses actually sent MIDI so OFF can release
 * outstanding notes without sending offs for keys that never sent an on. */
/* Panel button names as printed on each product (SMK-37 Pro: JamesOS-K labels). */
#if BR_PRODUCT_SMK37
#define BR_PANEL_SELECT "K5"
#define BR_PANEL_SAVE "BT"
#define BR_PANEL_HOME "KNOB BANK"
#define BR_PANEL_GLOBAL "GLOBE"
#define BR_PANEL_BROWSER "PATCH"
#define BR_PANEL_KNOB1 "K1"
#define BR_PANEL_KNOB2 "K2"
#define BR_PANEL_KNOB3 "K3"
#define BR_PANEL_KNOB4 "K4"
#define BR_HOLD_BUTTON BTN_SEL     /* the printed PATCH: tap = SOUNDS list, hold 0.8 s = HOLD */
#else
#define BR_PANEL_SELECT "SELECT"
#define BR_PANEL_SAVE "SAV"
#define BR_PANEL_HOME "HOME"
#define BR_PANEL_GLOBAL "GLO"
#define BR_PANEL_BROWSER "SEL"
#define BR_PANEL_KNOB1 "KNOB1"
#define BR_PANEL_KNOB2 "KNOB2"
#define BR_PANEL_KNOB3 "KNOB3"
#define BR_PANEL_KNOB4 "KNOB4"
#define BR_HOLD_BUTTON BTN_HOME
#endif
static uint8_t br_midi_out_held[BR_PLAY_KEYS], br_midi_out_sustain[BR_PLAY_KEYS];
static const char *const br_page_name[BR_PAGE_COUNT] = {"HOME", "ENVELOPE", "FX", "GLOBAL", "SEQUENCE", "PATTERNS", "SCALE", "LFO", "PATCH", "PUNCH"
#ifdef BR_PATTERN_SONG_EXPERIMENT
, "PATTERNS"
#endif
};
static const char *const br_target_name[BR_TARGET_COUNT] = {"OFF", "PITCH", "TIMBRE", "COLOR", "FILTER FREQUENCY", "TRACK LEVEL", "AMP DECAY", "RETIRED", "DELAY SEND", "REVERB SEND", "MOD FX SEND", "HARMONICS", "OSC MIX", "RETIRED", "AMP ATTACK", "AMP SUSTAIN", "AMP RELEASE", "FILTER RESONANCE", "LFO 1 RATE", "LFO 2 RATE", "GATE", "TRACK PAN", "DRIVE SEND", "ARP MODE", "ARP RATE", "ARP OCTAVES", "ARP GATE"
#if BR_MODEL_MOD_EXPERIMENT
    , "MODEL"
#endif
};
/* IDs are independent of the track destinations and preserve old project IDs. */
static const uint8_t br_global_target_id[] = {BR_GLOBAL_TARGET_OFF,
    BR_GLOBAL_TARGET_FILTER, BR_GLOBAL_TARGET_RESONANCE, BR_GLOBAL_TARGET_HEAT_AMOUNT,
    BR_GLOBAL_TARGET_HEAT_TYPE, BR_GLOBAL_TARGET_COMP_MIX, BR_GLOBAL_TARGET_COMP_THRESHOLD,
    BR_GLOBAL_TARGET_COMP_MAKEUP, BR_GLOBAL_TARGET_COMP_ATTACK, BR_GLOBAL_TARGET_COMP_RELEASE,
    BR_GLOBAL_TARGET_COMP_HOLD, BR_GLOBAL_TARGET_REVERB_SIZE, BR_GLOBAL_TARGET_REVERB_DECAY};
static const char *const br_global_target_name[] = {"OFF", "GLOBAL FILTER FREQUENCY", "GLOBAL FILTER RESONANCE",
    "HEAT AMOUNT", "HEAT TYPE", "COMP MIX", "COMP THRESHOLD", "COMP MAKEUP",
    "COMP ATTACK", "COMP RELEASE", "COMP HOLD", "REVERB SIZE", "REVERB DECAY"};
#define BR_GLOBAL_TARGET_UI_COUNT (sizeof br_global_target_id / sizeof br_global_target_id[0])
static uint32_t br_global_target_ordinal(uint32_t target)
{
    uint32_t i;
    for (i = 0; i < BR_GLOBAL_TARGET_UI_COUNT; ++i)
        if (br_global_target_id[i] == target) return i;
    return 0u;
}
static const char *br_global_target_label(uint32_t target)
{
    uint32_t ordinal = br_global_target_ordinal(target);
    return !ordinal && target != BR_GLOBAL_TARGET_OFF ? "LEGACY OFF" : br_global_target_name[ordinal];
}
static const char *const br_source_name[BR_SOURCE_COUNT] = {"OFF", "OSC AVG", "MIX AVG", "AMP ENV", "FILTER ENV", "LFO 1", "LFO 2", "RETIRED", "VELOCITY", "NOTE", "RANDOM",
    "GATE", "LFO 1 TRI", "LFO 1 SQU", "LFO 2 TRI", "LFO 2 SQU", "GLOBAL LFO", "RANDOM PER NOTE"};
static const char *const br_wave_name[BR_WAVE_COUNT] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "SAMPLE HOLD"};
static const char *const br_scale_name[BR_SCALE_COUNT] = {"CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "MAJOR PENT", "MINOR PENT", "BLUES"};
static const char *const br_delay_name[BR_DELAY_COUNT] = {"DIGITAL", "TAPE ECHO", "MULTITAP", "GLITCH"};
static const char *const br_reverb_name[BR_REVERB_COUNT] = {"ROOM", "HALL", "SPRING", "SPACE", "ETHEREAL"};
/* Keep saved engine IDs stable; only these three engines are offered. */
static const uint8_t br_reverb_ui_id[] = {BR_REVERB_ROOM, BR_REVERB_HALL, BR_REVERB_SPACE};
static const char *const br_mod_name[BR_MOD_COUNT] = {"CHORUS", "FLANGE", "PHASER", "VIBRATO"};
static const char *const br_delay_note_name[16] = {
    "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8",
    "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 BAR"
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
    BR_TARGET_COLOR, BR_TARGET_OUTPUT,
#if BR_MODEL_MOD_EXPERIMENT
    BR_TARGET_MODEL,
#endif
    BR_TARGET_FILTER, BR_TARGET_RESONANCE, BR_TARGET_LEVEL, BR_TARGET_PAN,
    BR_TARGET_ATTACK, BR_TARGET_DECAY, BR_TARGET_SUSTAIN, BR_TARGET_RELEASE,
    BR_TARGET_DIST_SEND, BR_TARGET_DELAY, BR_TARGET_MOD, BR_TARGET_REVERB,
    BR_TARGET_LFO1_RATE, BR_TARGET_LFO2_RATE, BR_TARGET_KIT_GATE,
    BR_TARGET_ARP_MODE, BR_TARGET_ARP_RATE, BR_TARGET_ARP_OCTAVES, BR_TARGET_ARP_GATE
};
static const uint8_t br_source_ui_id[] = {
    BR_SOURCE_OFF, BR_SOURCE_ENV, BR_SOURCE_AMP, BR_SOURCE_LFO1, BR_SOURCE_LFO2,
    BR_SOURCE_GLOBAL_LFO, BR_SOURCE_VELOCITY, BR_SOURCE_KEY, BR_SOURCE_GATE,
    BR_SOURCE_RANDOM, BR_SOURCE_NOTE_RANDOM, BR_SOURCE_LFO1_TRI, BR_SOURCE_LFO1_SQU,
    BR_SOURCE_LFO2_TRI, BR_SOURCE_LFO2_SQU, BR_SOURCE_OSC, BR_SOURCE_MIX
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
    if (br_target_is_arp(target))
        return ((br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] < BR_LFOS) ||
                (br_ui.page == BR_PAGE_ROUTE && br_ui.screen[BR_PAGE_ROUTE] < 2u)) && br_is_plaits(preset);
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
#if BR_PRODUCT_SMK37
/* SMK-37: the drum track's PATTERN page is RETRIGGER only when NOTE REPEAT opened it (smk37_fader_logic.h);
 * opened from the ARP button (or by a track arrow onto the drum track) it is a cue, br_arp_cue_page(). The
 * page it was opened from is kept so that a second NOTE REPEAT tap returns to it. */
typedef struct { uint8_t opened, page, screen; } smk_retrig_t;
static smk_retrig_t br_smk_retrig;
static uint32_t br_retrig_page(void)
{
    return br_ui.page == BR_PAGE_PATTERN && br_ui.part == BR_DRUM_PART && br_smk_retrig.opened;
}
static uint32_t br_arp_cue_page(void)
{
    return br_ui.page == BR_PAGE_PATTERN && br_ui.part == BR_DRUM_PART && !br_smk_retrig.opened;
}
#else
static uint32_t br_retrig_page(void)
{
    return br_ui.page == BR_PAGE_PATTERN && br_ui.part == BR_DRUM_PART;
}
#endif
static uint32_t br_retrig_editing(void)
{
    return br_retrig_page() && br_ui.retrig_key && br_ui.retrig_key <= 27u &&
        br_ui.retrig_part == br_ui.part && br_ui.retrig_lane == br_ui.lane &&
        br_ui.retrig_step < br_part[BR_DRUM_PART].length &&
#if BR_PRODUCT_SMK37
        ((fm1_in.notes & (1u << (br_ui.retrig_key - 1u)))
#if defined(FM1_DEVICE)
         || smk_pad_holds_key(br_ui.retrig_key - 1u)
#endif
        ) &&
#else
        (fm1_in.notes & (1u << (br_ui.retrig_key - 1u))) &&
#endif
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
    return br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] == 2u;
}
static uint32_t br_home_voice_page(void)
{ return br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] == 3u; }
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

/* OCT- held turns knobs in 0 / 50 / 100 % steps: the FX pages and GLOBAL / PAN (100 % L, CENTER, 100 % R). */
static uint32_t br_pan_page(void)
{
    return br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 3u;
}
static uint32_t br_fx_snap_page(void)
{
    return br_ui.page == BR_PAGE_FX || br_pan_page() || br_home_filter_page() ||
        (br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u);
}
/* A held snap gesture belongs to the page and track where it began. */
static uint32_t br_fx_snap_context(void)
{
    return br_fx_snap_page() && (br_ui.page << 4 | br_ui.screen[br_ui.page]) == br_ui.fx_snap_screen &&
        br_ui.part == br_ui.fx_snap_part;
}
static uint32_t br_fx_snap_knob(uint32_t k)
{
    return br_pan_page() || (!k && (br_home_filter_page() ||
        (br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u))) ||
        (br_ui.page == BR_PAGE_FX && br_fx_sends_page());
}
static uint32_t br_screen_count(void)
{
    switch (br_ui.page) {
    case BR_PAGE_HOME: return 4u; /* two sound pages and two deep pages */
    case BR_PAGE_ENV: return 3u;
    case BR_PAGE_FX: return 5u;
    case BR_PAGE_PUNCH: return 2u;
#if BR_ARR_UI
    case BR_PAGE_PS: return 1u;    /* performance mode is set by knob 4 */
#endif
    case BR_PAGE_MIX: return 9u;   /* screen IDs; br_nav_ring() gives the two GLO groups */
    case BR_PAGE_SEQ: return 2u;
    case BR_PAGE_PATTERN: return 1u;
    case BR_PAGE_SCALE: return 1u; /* sounds/kit browser only */
    case BR_PAGE_LFO: return BR_LFOS + 1u;
    case BR_PAGE_ROUTE: return 4u;
    default: return 1u;
    }
}
/* The screens one page button steps through, in order, as page << 4 | screen; 0 when the
 * page's screens are simply 0..count-1. GLO: six sound screens on a tap, TEMPO and the
 * MIDI pages (screen IDs 2, 7, 8) behind a long press. Screen IDs stay as they were. */
#define BR_NAV(page, screen) (uint8_t)((page) << 4 | (screen))
/* GLO's first screen. JamesOS-K (SMK-37): the mixer is not in GLO's ring (FADER BANK opens the
 * mixers there), so GLO starts on PAN. */
#if BR_PRODUCT_SMK37
#define BR_GLO_FIRST 3u
#else
#define BR_GLO_FIRST 0u
#endif
static uint32_t br_nav_ring(const uint8_t **ring)
{
    static const uint8_t home_sound[2] = {BR_NAV(BR_PAGE_HOME, 0), BR_NAV(BR_PAGE_HOME, 1)};
    static const uint8_t home_deep[2] = {BR_NAV(BR_PAGE_HOME, 2), BR_NAV(BR_PAGE_HOME, 3)};
    static const uint8_t glo_sound[6] = {BR_NAV(BR_PAGE_MIX, 0), BR_NAV(BR_PAGE_MIX, 3), BR_NAV(BR_PAGE_MIX, 1),
                                         BR_NAV(BR_PAGE_MIX, 6), BR_NAV(BR_PAGE_MIX, 4), BR_NAV(BR_PAGE_MIX, 5)};
    static const uint8_t glo_setup[3] = {BR_NAV(BR_PAGE_MIX, 2), BR_NAV(BR_PAGE_MIX, 7), BR_NAV(BR_PAGE_MIX, 8)};
    static const uint8_t edit[3] = {BR_NAV(BR_PAGE_ROUTE, 3), BR_NAV(BR_PAGE_ROUTE, 0), BR_NAV(BR_PAGE_ROUTE, 1)};
    uint32_t s = br_ui.screen[br_ui.page];
    if (br_ui.page == BR_PAGE_HOME) {
        *ring = s >= 2u ? home_deep : home_sound;
        return 2u;
    }
    if (br_ui.page == BR_PAGE_MIX) {
        if (s == 2u || s >= 7u) { *ring = glo_setup; return 3u; }
#if BR_PRODUCT_SMK37
        *ring = glo_sound + 1; return 5u;      /* PAN, FILTER, HEAT, COMPRESSOR, COMP TIME */
#else
        *ring = glo_sound; return 6u;
#endif
    }
#if BR_PRODUCT_SMK37
    if (br_ui.page == BR_PAGE_ROUTE) { *ring = edit + 1; return 2u; }   /* PATCH 1, PATCH 2 */
#else
    if (br_ui.page == BR_PAGE_ROUTE) { *ring = edit; return 3u; }
#endif
    return 0u;
}
static void br_open_home_settings(void)
{
    uint32_t already = br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] >= 2u;
    br_ui.page = BR_PAGE_HOME;
    br_ui.screen[BR_PAGE_HOME] = already ? 0u : 2u;
    br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
}
static uint32_t br_nav_at(const uint8_t *ring, uint32_t n)
{
    uint32_t i, here = BR_NAV(br_ui.page, br_ui.screen[br_ui.page]);
    for (i = 0u; i < n && ring[i] != here; ++i) {}
    return i < n ? i : 0u;
}
static void br_select_screen(int32_t turn)
{
    const uint8_t *ring = 0;
    uint32_t n = br_nav_ring(&ring);
    int32_t count = (int32_t)(n ? n : br_screen_count());
    int32_t at = n ? (int32_t)br_nav_at(ring, n) : (int32_t)br_ui.screen[br_ui.page];
    at = (at + turn % count) % count;
    if (at < 0) at += count;
    if (n) { br_ui.page = ring[at] >> 4u; br_ui.screen[br_ui.page] = ring[at] & 15u; }
    else br_ui.screen[br_ui.page] = (uint32_t)at;
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
    br_ui.edit_key = 0xFFFFFFFFu;
    br_ui.redraw = 1;
}
static void br_open_module(uint32_t module)
{
#if BR_PRODUCT_SMK37
    br_smk_retrig.opened = 0u;      /* a page button leaves RETRIGGER; the ARP button never opens it (drum track: the cue) */
    if (module == BR_PAGE_MIX && (br_ui.page != module || !br_ui.screen[module])) {   /* from elsewhere or from the mixer */
        br_ui.page = module; br_ui.screen[module] = BR_GLO_FIRST; br_ui.edit_key = 0xFFFFFFFFu;
    } else
#endif
    if (br_ui.page == module) br_select_screen(1);
    else { br_ui.page = module; br_ui.screen[module] = 0; br_ui.edit_key = 0xFFFFFFFFu; }
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
    br_ui.redraw = 1;
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
        RGB(0, 12, 0), RGB(16, 0, 16), RGB(0, 8, 16), RGB(16, 0, 0)
    };
    return colors[part < BR_PARTS ? part : BR_DRUM_PART];
}
#define BR_BACKGROUND C_BLACK
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

/* Periodic redraws (animation, playhead, meters) run at 24 frames/s: one every 42 ms (owner's decision,
 * Nov 2026; it was 32 ms). A knob or button redraws at once (br_ui.redraw). Every animation takes its speed
 * from fm1_ms or the audio clock, never from the frame count, so none runs slower at this rate. */
#ifndef BR_FRAME_MS
#define BR_FRAME_MS 42u
#endif
/* Meters fall one of their 28 steps per 32 ms whatever the frame rate: the steps due since *at. */
static uint32_t br_meter_fall(uint32_t *at)
{
    uint32_t n = (fm1_ms - *at) >> 5;
    *at += n << 5;
    return n;
}
static uint32_t br_mon_dirty;      /* something else was drawn over the monitor's bottom rows */
/* Sliced drawing. The main loop calls br_draw_pass_with: one pass sends at most BR_PASS_BYTES of pixels
 * (7,200 B, 4.8 ms at 12 MHz: under the 6 ms that played notes are allowed) and returns, so keys, knobs and MIDI are serviced between the parts of a
 * frame. Every region (a canvas, or a picture drawn as several canvases) asks br_region / br_band before
 * it draws: once the pass is full they say no and the region, its hash not taken, is drawn by a later
 * pass from the state of that moment (a region whose state changed after it was drawn differs from its
 * hash and is drawn again). Regions go out in drawing order. A canvas bigger than the room left is sent in
 * part; the rest of its rows wait in the canvas (cv_held) for the next pass. A forced redraw (a new
 * layout) changes br_pass.epoch, which every region's hash includes, so each region is then drawn once.
 * Outside br_draw_pass_with (br_pass.on == 0: tests, br_draw) nothing is deferred.
 * A pass is also bounded in TIME: once BR_PASS_TICKS have gone since it began, or as soon as a played
 * note or MIDI message waits (BR_INPUT_WAITING), the next region waits for a later pass (the audio
 * interrupt takes most of the CPU while a pattern plays, so a pass's bytes say little about how long it
 * holds the loop). The first region of a pass always goes ahead, so every pass makes progress. BR_DRAW_NOW: the 24 MHz timer (a host probe may supply its own clock). */
#define BR_PASS_BYTES 7200u
#ifndef BR_PASS_TICKS
#define BR_PASS_TICKS (8u * 24000u)            /* 8 ms; a pass ends sooner when a note waits (BR_INPUT_WAITING) */
#endif
/* A played note or a MIDI message waiting for the loop: the pass ends at the next region boundary. */
#if defined(FM1_DEVICE)
#define BR_INPUT_WAITING() (mi_r != mi_w || fm1_in.notes_pressed)
#elif defined(DINGHY_HOST)
#define BR_INPUT_WAITING() (mq_r != mq_w || host_note_pressed)
#else
#define BR_INPUT_WAITING() 0
#endif
#ifndef BR_DRAW_NOW
#ifdef BR_NOTE_NOW
#define BR_DRAW_NOW() BR_NOTE_NOW()
#else
#define BR_DRAW_NOW() 0u                       /* no timer here: bytes alone bound a pass */
#endif
#endif
static struct { uint32_t epoch, sent, t0; uint8_t on, more, drawn; } br_pass;
static uint8_t br_draw_more;                   /* the main loop: a frame is still going out */
static uint8_t br_badge_lfo_phase[BR_PARTS][BR_LFOS];   /* the LFO phases when this frame began */
static uint8_t br_badge_global_lfo_phase;
static uint8_t br_badge_global_lfo_value;
static uint32_t br_badge_global_aux; /* heat type and compressor envelope at the start of this frame */
#if BR_MODEL_MOD_EXPERIMENT
static uint8_t br_badge_model_target;
#endif
static uint32_t br_open(void)                 /* is there room left in this pass? (sent: 0 outside passes) */
{
    BR_UIT_REGION_ASK();
    if (br_pass.sent < BR_PASS_BYTES && (!br_pass.drawn || !br_pass.on ||
        (!BR_INPUT_WAITING() && (uint32_t)(BR_DRAW_NOW() - br_pass.t0) < BR_PASS_TICKS))) {
        br_pass.drawn = 1u;
        if (br_pass.on) BR_UIT_REGION_OPEN();
        return 1u;
    }
    if (br_pass.sent < BR_PASS_BYTES) BR_UIT_PASS_TIMED();
    br_pass.more = 1u;
    return 0u;
}
/* Band n of a picture in n bands (n <= 15) to draw now, or n (all drawn, or no room in this pass). The bands
 * go round from a start (*next: start << 4 | bands drawn): a change of h while the picture is half drawn
 * (a knob turning) goes on from the band it reached, so every band soon shows a recent value instead of
 * the first ones being drawn over and over; those drawn before the change come again last. */
static uint32_t br_roll(uint32_t *old, uint8_t *next, uint32_t h, uint32_t n)
{
    uint32_t start = *next >> 4, done = *next & 15u;
    if (*old != (h ^ br_pass.epoch)) {
        start = done && done < n ? (start + done) % n : 0u;
        *old = h ^ br_pass.epoch; done = 0u;
        *next = (uint8_t)(start << 4);
    }
    if (done >= n || !br_open()) return n;
    *next = (uint8_t)(start << 4 | (done + 1u));
    return (start + done) % n;
}
/* A one-canvas region whose picture hashes to h: draw it now? (force: draw it whatever its hash) */
static uint32_t br_region(uint32_t *old, uint32_t h, uint32_t force)
{
    h ^= br_pass.epoch;
    if ((!force && *old == h) || !br_open()) return 0u;
    *old = h;
    return 1u;
}
/* Canvas `band` (0, 1, ... in order) of a picture drawn as several canvases; *next is the first canvas
 * not yet sent for the hash in *old. */
static uint32_t br_band(uint32_t *old, uint8_t *next, uint32_t h, uint32_t band, uint32_t force)
{
    h ^= br_pass.epoch;
    if (*old != h || (force && !band)) { *old = h; *next = 0u; }
    if (band < *next || !br_open()) return 0u;
    *next = (uint8_t)(band + 1u);
    return 1u;
}
/* Screen content uses a solid black background. */
static void br_canvas_shade(uint32_t x, uint32_t y)
{
    (void)x; (void)y;
}
static uint32_t br_dots_old = 0xffffffffu;
static void br_canvas_send(uint32_t x, uint32_t y)
{
    uint32_t rows = cv_h;
    /* Content bands can overlap the raised carousel row. If one is sent
     * after the dots during a sliced draw, the dots must be painted again. */
    if (y < 212u && y + cv_h > 200u &&
        !(y == 200u && cv_h == 12u))
        br_dots_old = 0xffffffffu;
    if (y + cv_h > 238u) br_mon_dirty = 1u;
    if (br_pass.on) {
        uint32_t room = br_pass.sent < BR_PASS_BYTES ? BR_PASS_BYTES - br_pass.sent : 0u;
        if (room / (cv_w * 2u) < rows) {       /* the rest of the rows go out in the next pass */
            rows = room / (cv_w * 2u); br_pass.sent = BR_PASS_BYTES; br_pass.more = 1u;
        } else br_pass.sent += rows * cv_w * 2u;
    }
    cv_held = (uint16_t)cv_h; cv_held_x = (uint16_t)x; cv_held_y = (uint16_t)y;
    cv_send_held(rows);
    BR_UI_CLOCK_SERVICE();
}
static void br_canvas_blit(uint32_t x, uint32_t y)
{
    br_canvas_shade(x, y);
    br_canvas_send(x, y);
}
#define cv_blit(x, y) br_canvas_blit((x), (y))

/* Bands whose pixels may well be what the screen already shows (a moving picture's still rows, a list's
 * unchanged rows): *crc holds the checksum of what was last sent there, mixed with the draw epoch, and an
 * equal band is not sent. One table serves the screen on show: the four HOME cards (8 bands each), the SEL
 * list or the SAVE screens (never on screen together; a change between them is a new epoch). */
static uint32_t br_band_crc[40];
static void br_crc_blit(uint32_t x, uint32_t y, uint32_t *crc)
{
    uint32_t v = br_pass.epoch, n = cv_w * cv_h;
    for (uint32_t i = 0; i < n; ++i) v = (v ^ cv_px[i]) * 16777619u;
    if (v == *crc) return;
    *crc = v;
    cv_blit(x, y);
}

/* The wipes of a layout change (br_draw_body), numbered in drawing order while br_wipe.on: `done` of them
 * are on screen, `ord` counts them in this pass, `sig` names the change being painted. */
static struct { uint32_t sig; uint16_t done, ord; uint8_t on; } br_wipe = {0xffffffffu, 0u, 0u, 0u};
static void br_background_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h) return;
    while (h) {
        uint32_t rows = BR_PASS_BYTES / 2u / w;           /* one pass's worth at most */
        if (rows > h) rows = h;
        if (!br_wipe.on || (br_wipe.ord++ == br_wipe.done && br_open())) {
            cv_begin(w, rows, BR_BACKGROUND);
            cv_blit(x, y);
            if (br_wipe.on) ++br_wipe.done;
        }
        y += rows; h -= rows;
    }
}

/* ------------------------------------------------------------------------------------------------ CPU monitor */
/* The bottom two pixel rows of the screen become one cyan meter of the audio CPU, refreshed at 5 Hz (every
 * BR_MON_MS; it was 10 Hz until the owner halved it, Nov 2026) from
 * the audio counters that every build keeps (platform.c fm1_audio_metrics_t): the average render time of a
 * 256-frame half (exponential, about 0.4 s) as a share of its deadline. Past 90 % red comes in from the
 * right, reaching the full width at 100 %. A white tick marks the worst half since the monitor was switched
 * on; any overrun since then turns the empty track red. The MONITOR card on TEMPO shows the same figures:
 * AVG and MAX in percent and OVR, the overrun count (its switch turns red once that is not zero). */
#define BR_MON_MS 200u                         /* the monitor's refresh: every 200 ms */
#define BR_MON_CPU RGB(0, 200, 230)
#define BR_MON_RED RGB(240, 60, 48)
#define BR_MON_TRACK RGB(24, 30, 32)
#define BR_MON_ALERT_TRACK RGB(120, 24, 20)
static struct { uint8_t on; uint32_t at, cpu, worst, overruns, base, painted; } br_mon;   /* shares in 0.1 % */
static char br_mon_text[28];
#if defined(FM1_DEVICE)
static uint32_t br_monitor_share(uint32_t ticks)          /* tenths of a percent of the half's deadline */
{
    if (ticks > 4u * FM1_AUDIO_HALF_TICKS) ticks = 4u * FM1_AUDIO_HALF_TICKS;   /* no overflow; shows 400 % */
    return ticks * 1000u / FM1_AUDIO_HALF_TICKS;
}
#endif
static void br_monitor_sample(void)
{
#if defined(FM1_DEVICE)
    uint32_t w[3], worst;
    fm1_audio_window_take(w);
    br_mon.cpu = br_monitor_share(w[0]);
    worst = br_monitor_share(w[1]);
    if (worst > br_mon.worst) br_mon.worst = worst;
    br_mon.overruns = w[2] - br_mon.base;
#endif
}
static uint32_t br_monitor_on(void) { return br_mon.on; }
static void br_monitor_set(uint32_t on)
{
    if (on == br_mon.on) return;
    br_mon.on = (uint8_t)on; br_mon.at = fm1_ms - BR_MON_MS; br_mon.painted = 0xffffffffu;
    br_mon.cpu = br_mon.worst = br_mon.overruns = 0u;
#if defined(FM1_DEVICE)
    if (on) { uint32_t w[3]; fm1_audio_window_take(w); br_mon.base = w[2]; }   /* count from now */
#endif
    if (!on) { br_background_rect(0, 238, 240, 2); br_ui.redraw = 1u; br_pass.epoch += 0x9e3779b9u; }   /* repaint what it covered */
}
#if defined(FM1_DEVICE)
#include "cpu_overrun_warning.h"
static br_cpu_warning_t br_cpu_warning;
static uint32_t br_cpu_warning_at;
static void br_cpu_warning_poll(void)
{
    uint32_t w[FM1_AUDIO_METRICS_WORDS];
    uint32_t bpm = br_midi_clock_active && br_midi_clock_bpm ? br_midi_clock_bpm : br_bpm;
    if (!br_cpu_warning.initialized || fm1_ms - br_cpu_warning_at >= BR_MON_MS) {
        br_cpu_warning_at = fm1_ms;
        fm1_audio_metrics_snapshot(w);
        if (br_cpu_warning_step(&br_cpu_warning, fm1_ms, w[9], bpm))
            br_monitor_set(1u);
    }
    if (br_cpu_warning_take_notice(&br_cpu_warning,
                                  (int32_t)(br_ui.message_until - fm1_ms) > 0))
        br_message("CPU USAGE HIGH. MONITOR ENABLED.");
}
#endif
static void br_monitor_percent(const char *name, uint32_t share)
{
    char number[12];
    br_text_append(br_mon_text, name, sizeof br_mon_text);
    br_number(number, (int32_t)((share + 5u) / 10u));
    br_text_append(br_mon_text, number, sizeof br_mon_text);
    br_text_append(br_mon_text, "% ", sizeof br_mon_text);
}
/* The MONITOR card's value: "AVG 38% MAX 52% OVR 0" (three rows on the card), or OFF. */
static const char *br_monitor_text(void)
{
    char number[12];
    if (!br_mon.on) return "OFF";
    br_mon_text[0] = 0;
    br_monitor_percent("AVG ", br_mon.cpu);
    br_monitor_percent("MAX ", br_mon.worst);
    br_number(number, (int32_t)(br_mon.overruns > 99999u ? 99999u : br_mon.overruns));
    br_text_append(br_mon_text, "OVR ", sizeof br_mon_text);
    br_text_append(br_mon_text, number, sizeof br_mon_text);
    return br_mon_text;
}
static void br_monitor_overlay(void)
{
    uint32_t cpu, x;
    int32_t red;
    if (!br_mon.on) return;
    if (fm1_ms - br_mon.at >= BR_MON_MS) { br_mon.at = fm1_ms; br_monitor_sample(); }
    if (br_mon.painted == br_mon.at && !br_mon_dirty) return;
    br_mon.painted = br_mon.at; cpu = br_mon.cpu;
    cv_begin(240, 2, C_BLACK);
    cv_rect(0, 0, 240, 2, br_mon.overruns ? BR_MON_ALERT_TRACK : BR_MON_TRACK);
    if (cpu) cv_rect(0, 0, (int32_t)(cpu > 1000u ? 1000u : cpu) * 240 / 1000, 2, BR_MON_CPU);
    if (cpu > 900u) {
        red = (int32_t)(cpu - 900u) * 240 / 100; if (red > 240) red = 240;
        if (red > 0) cv_rect(240 - red, 0, red, 2, BR_MON_RED);
    }
    if (br_mon.worst) {                    /* the worst half since switched on */
        x = (br_mon.worst > 1000u ? 1000u : br_mon.worst) * 238u / 1000u;
        cv_rect((int32_t)x, 0, 2, 2, C_WHITE);
    }
    cv_blit(0, 238);
    br_mon_dirty = 0u;
}

/* The version in the settings badge: "V1.0B BUILD 1560" from the package version and identity. */
static const char *br_version_text(void)
{
    static char text[24];
#if defined(FM1_DEVICE) || defined(FELUCCA_ID)
    const char *ver = APP_VER, *id = FELUCCA_ID;
    uint32_t i = 0, n = 0;
    br_text_copy(text, "V", sizeof text);
    while (ver[i] && ver[i] != '-' && n < 8u) { char c[2] = {ver[i++], 0}; if (c[0] >= 'a' && c[0] <= 'z') c[0] -= 32; /* the HD font is capitals only */
        br_text_append(text, c, sizeof text); ++n; }
    /* identity FM-1_926NNNN: the digits after the three-digit prefix are the build number */
    if (id[0] == 'F' && id[1] == 'M' && id[2] == '-' && id[3] == '1' && id[4] == '_' && id[5] && id[6] && id[7]) {
        br_text_append(text, " BUILD ", sizeof text); br_text_append(text, id + 8, sizeof text);
    }
#else
    br_text_copy(text, "V1.0B HOST", sizeof text);
#endif
    return text;
}

static const char *br_build_text(void)
{
    static char text[24];
#if defined(FM1_DEVICE) || defined(FELUCCA_ID)
    const char *id = FELUCCA_ID;
    while (*id && *id != '_') ++id;
    br_text_copy(text, "BUILD ", sizeof text);
    if (*id == '_') {
        ++id;
        if (id[0] && id[1] && id[2] && id[3]) id += 3;
        br_text_append(text, id, sizeof text);
    }
#else
    br_text_copy(text, "BUILD HOST", sizeof text);
#endif
    return text;
}


typedef struct { const char *label; int32_t value, low, high; const char *text; } br_control_t;
#if BR_ARR_UI
#define BR_ARR_UI_PART 1
#include "pattern_arranger_ui.c"   /* part 1: state, knobs, keys */
#undef BR_ARR_UI_PART
#endif
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
    if (br_ui.page == BR_PAGE_PS) {
#if BR_ARR_UI
        br_arr_control(k, &c);
#endif
        return c;
    }
#endif
    if (br_home_voice_page()) {
        static const char *const labels[4] = {"PORTA TIME", "GLIDE MODE", "AMP MODE", "GATE LENGTH"};
        static const char *const glide[3] = {"OFF", "LEGATO", "ALWAYS"};
        if (!kit && (k < 3u || v->amp_mode)) {
            c.label = labels[k];
            c.value = !k ? (int32_t)v->portamento : k == 1u ? (int32_t)v->glide_mode : k == 2u ? (int32_t)v->amp_mode : (int32_t)v->lpg_gate;
            c.high = !k ? 100 : k == 1u ? 2 : k == 2u ? 1 : 31;
            if (k == 1u) c.text = glide[c.value <= 2 ? c.value : 0];
            if (k == 2u) c.text = c.value ? "LPG" : "ENVELOPE";
        }
    } else if (br_sound_mod_page()) {
        static const char *const labels[4] = {"TIMBRE", "HARMONICS", "MORPH", "PITCH"};
        c.label = labels[k];
        c.value = !k ? (int32_t)v->timbre_mod : k == 1u ?
            (int32_t)(v->harmonics_mod ? v->harmonics_mod - 1u : 64u) : k == 2u ? (int32_t)v->morph_mod : (int32_t)v->fm_mod;
    } else if (br_ui.page == BR_PAGE_HOME) {
        static const char *const label[4] = {"TIMBRE", "COLOR", "", ""};
        if (!page && kit && v->preset == BR_KIT_PLAITS) {
            static const char *const plaits_kit[4] = {"TIMBRE", "HARMONICS", "MORPH", "GATE"};
            c.label = plaits_kit[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->plaits_harmonics :
                k == 2u ? (int32_t)v->color : (int32_t)v->gate;
            if (k == 3u) c.high = BR_GATE_OPEN;
        } else if (!page && kit && (v->preset == BR_KIT_808 || v->preset == BR_KIT_909)) {
            static const char *const sample_kit[4] = {"PITCH", "", "SNAP", "GATE"};
            c.label = sample_kit[k]; c.value = !k ? v->tune : k == 3u ? (int32_t)v->gate : k == 2u ? (int32_t)v->drum_snap : 0;
            if (!k) { c.low = -12; c.high = 12; }
            else if (k == 3u) c.high = BR_GATE_OPEN;
        } else if (!page && kit) {
            /* SNAP (the kit's attack: sample kits' burst of highs, Braids snare / rim / clap noise) sits in
             * the free third slot of the Braids and sample kits; the Plaits kit has no free slot. */
            static const char *const braids_kit[4] = {"TIMBRE", "COLOR", "SNAP", "GATE"};
            c.label = braids_kit[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->color : k == 3u ? (int32_t)v->gate : (int32_t)v->drum_snap;
            if (k == 3u) c.high = BR_GATE_OPEN;
        } else if (!page && br_is_plaits(v->preset) && !kit) {
            static const char *const plaits[4] = {"TIMBRE", "HARMONICS", "MORPH", "OSC MIX"};
            c.label = plaits[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->plaits_harmonics :
                      k == 2u ? (int32_t)v->color : (int32_t)v->plaits_aux;
        } else if (br_home_filter_page()) {
            static const char *const filter[4] = {"FILTER", "RESONANCE", "ENV DEPTH", "KEY TRACK"};
            c.label = filter[k];
            /* ENV DEPTH is the filter's own depth from the mod envelope, added to whatever
             * that envelope's TARGET/DEPTH does. KEY TRACK is an amount (0..127, 127 =
             * 1 oct/oct); this knob switches it OFF/ON (0/127). */
            c.value = !k ? (int32_t)v->filter : k == 1u ? (int32_t)v->filter_resonance :
                      k == 3u ? (int32_t)(v->filter_keytrack != 0u) : v->filter_env_depth;
            if (k == 3u) { c.high = 1; c.text = c.value ? "ON" : "OFF"; }
            if (k == 2u) c.low = -127;
        } else {
            c.label = label[k];
            c.value = !k ? (int32_t)v->timbre : k == 1u ? (int32_t)v->color : 0;
        }
    } else if (br_ui.page == BR_PAGE_ENV) {
        if (page < 2u) {
            c.label = env_label[k];
            c.value = !page ? (k == 0u ? v->attack : k == 1u ? v->decay : k == 2u ? v->sustain : v->release) :
                             (k == 0u ? m->attack : k == 1u ? m->decay : k == 2u ? m->sustain : m->release);
            if (!page && k == 3u && !kit && v->amp_mode) { c.label = ""; c.text = ""; c.value = 0; }
        } else {
            static const char *const label[4] = {"DEPTH", "TARGET", "LOOP", "RETRIGGER"};
            c.label = label[k]; c.value = k == 0u ? m->amount : k == 1u ? (int32_t)br_target_ui_ordinal(m->target) : k == 2u ? (int32_t)m->loop : (int32_t)m->retrigger;
            if (!k) c.low = -127;
            else if (k == 1u) { c.high = (int32_t)br_target_ui_count() - 1; c.text = br_target_ui_name(m->target); }
            else { c.high = 1; c.text = c.value ? "ON" : "OFF"; }
        }
    } else if (br_ui.page == BR_PAGE_FX) {
        static const char *const label[3][4] = {{"ENGINE", "TIME", "FEEDBACK", "TONE"},
                                               {"ENGINE", "SIZE", "DECAY", "PRE DELAY"},
                                               {"ENGINE", "RATE", "DEPTH", "FEEDBACK"}};
        static const char *const sends[4] = {"DRIVE", "DELAY", "MODULATION", "REVERB"};
        static const char *const drive[4] = {"MODE", "GAIN", "TONE", "BIAS"};
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
                      k == 1u ? (int32_t)f->drive_gain : k == 2u ? (int32_t)f->drive_tone : (int32_t)br_part_pending[p].drive_bias;
            if (!k) { c.high = 2; c.text = drive_name[c.value]; }
        } else {
        c.label = label[fx][k];
        if (fx == 0u) {
            c.value = k == 0u ? f->delay_engine : k == 1u ?
                (int32_t)br_fx_delay_choice(f->delay_time, br_part_pending[p].delay_short) :
                k == 2u ? f->delay_feedback : br_part_pending[p].delay_tone;
            if (!k) { c.high = BR_DELAY_COUNT - 1; c.text = br_delay_name[f->delay_engine]; }
            else if (k == 1u) { c.high = 15; c.text = br_delay_note_name[c.value]; }
        } else if (fx == 1u) {
            /* One reverb for every track: type, size and decay are global;
             * only the send is the track's own. */
            c.value = k == 0u ? (int32_t)br_reverb_ui_ordinal(br_master.reverb_engine) :
                k == 1u ? (int32_t)br_master.reverb_size : k == 2u ? (int32_t)br_master.reverb_decay :
                ((br_master.reverb_manual & 32u) && br_master.reverb_extra[0] <= 11u ? br_master.reverb_extra[0] : 0);
            if (!k) { c.high = BR_REVERB_UI_COUNT - 1u; c.text = br_reverb_name[br_reverb_ui_id[c.value]]; }
            else if (k == 3u) {
                static const char *const notes[12] = {"AUTO", "1/64T", "1/64", "1/32T", "1/64D", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8"};
                c.high = 11; c.text = notes[c.value];
            }
        } else {
            c.value = k == 0u ? f->mod_engine : k == 1u ? f->mod_rate : k == 2u ? f->mod_depth : br_part_pending[p].mod_feedback;
            if (!k) { c.high = BR_MOD_COUNT - 1; c.text = br_mod_name[f->mod_engine]; }
        }
        }
    } else if (br_ui.page == BR_PAGE_MIX) {
        if (page >= 7u && page <= 8u) {
            static const char *const channel_labels[7] = {"T1 CH", "T2 CH", "T3 CH", "D1 CH", "D2 CH", "D3 CH", "D4 CH"};
            if (page == 7u && k == 3u) {
                c.label = ""; c.value = 0; c.high = 0;
            } else {
                uint32_t channel = page == 7u ? k : 3u + k;
                c.label = channel_labels[channel]; c.value = br_midi_channels[channel]; c.low = 0; c.high = 16;
                if (!c.value) c.text = "OFF";
            }
        } else if (!page) { c.label = mix_label[k]; c.value = br_part[k].sound.level; }
        else if (page == 1u) {
            static const char *const label[4] = {"FILTER", "RESONANCE", "ENV DEPTH", ""};
            c.label = label[k];
            c.value = !k ? br_master.fx.filter : k == 1u ? br_master.fx.resonance :
                k == 2u ? br_master.fx.env_depth : 0;
            if (k == 2u) c.low = -127;
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
            static const char *const label[4] = {"BPM", "SWING", "CLOCK SOURCE", "MONITOR"};
            c.label = label[k];
            c.value = !k ? (int32_t)br_bpm : k == 1u ? (int32_t)br_swing : (int32_t)br_mon.on;
            if (!k) {
                c.low = BR_BPM_MIN; c.high = BR_BPM_MAX;
                if (br_midi_clock_active) { c.label = "MIDI BPM"; c.value = (int32_t)br_midi_clock_bpm; }
            }
            else if (k == 1u) c.high = BR_SWING_MAX;
            else if (k == 2u) {
                c.high = 1; c.value = br_midi_clock_enabled;
                c.text = c.value ? "USB/MIDI" : "OFF";
            }
            else { c.high = 1; c.text = br_monitor_text(); }
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
            const volatile br_lfo_config_t *l = page == BR_LFOS ? &br_master.global_lfo : &m->lfo[page];
            static const char *const label[4] = {"WAVE", "RATE", "TARGET", "DEPTH"};
            c.label = label[k]; c.value = k == 0u ? l->wave : k == 1u ? l->rate : k == 2u ? l->target : l->depth;
            if (!k) { c.high = BR_WAVE_COUNT - 1; c.text = br_wave_name[l->wave]; }
            if (k == 2u && page == BR_LFOS) {
                c.high = (int32_t)BR_GLOBAL_TARGET_UI_COUNT - 1;
                c.value = (int32_t)br_global_target_ordinal(l->target);
                c.text = br_global_target_label(l->target);
            } else if (k == 2u) { c.high = (int32_t)br_target_ui_count() - 1; c.value = (int32_t)br_target_ui_ordinal(l->target); c.text = br_target_ui_name(l->target); }
        }
    } else if (br_ui.page == BR_PAGE_ROUTE) {
        if (br_gate_page()) {
            c.label = br_drum_voice_label(v->drum_voice_mask, k);
            c.value = br_part[BR_DRUM_PART].lane_level[k];
            c.high = 127;
        } else {
            const volatile br_route_config_t *r = &br_part[br_ui.part].mod.route[BR_ROUTES + br_patch_slot()];
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
            c.low = lock; c.high = 127; /* a held step never reaches silent velocity 0 */
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
#if BR_POLY_UI
        /* Echo's delay memory is lent to polyphonic voices (models 15, 18, 19); its settings stay editable. */
        if (k == 1u && br_fx_poly_loan_active()) c.label = "NO ECHO IN POLY";
#endif
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
#if BR_PRODUCT_SMK37
    if (page == BR_PAGE_PUNCH) return 1u;
#endif
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
#if BR_PRODUCT_SMK37
static void br_smk_next_sound_category(void)
{
    uint32_t p = br_ui.part, start = br_sel.cursor[p], next = start;
    const char *category = br_bank_category(start);
    if (p >= BR_SYNTH_PARTS) return;
    do { next = (next + 1u) % BR_BANK_COUNT; }
    while (next != start && br_bank_category(next) == category);
    br_sel_move((int32_t)next - (int32_t)start);
    br_message(br_bank_category(next));
}
#endif
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
    br_ui.part = (uint32_t)br_clip((int32_t)br_ui.part + turn, 0, BR_PARTS - 1);
    br_ui.cursor = br_ui.step_bank = 0; br_ui.redraw = 1;
    if (br_ui.screen[br_ui.page] >= br_screen_count()) br_ui.screen[br_ui.page] = 0;
    if (br_gate_page()) br_gate_select_lane(br_ui.lane);
}

#if BR_PRODUCT_SMK37
/* SMK arrows select any track, including from the patch and kit pages. */
static void br_smk_track_turn(int32_t direction)
{
    uint32_t old = br_ui.part;
    uint32_t next = (uint32_t)br_clip((int32_t)old + direction, 0, BR_PARTS - 1);
    if (!direction || next == old) return;
    if (br_sel.active) br_sel_close();
    br_ui.part = next;
    br_smk_retrig.opened = 0u;          /* a track change ends RETRIGGER (the drum track's ARP page is then the cue) */
    br_ui.cursor = br_ui.step_bank = 0u;
    br_ui.edit_key = 0xffffffffu;
    br_ui.redraw = 1u;
}
#endif

#if BR_PRODUCT_SMK37
#include "smk37/smk37_panel_logic.h"
static uint32_t br_step_press_pending(void);
/* A display-only mixer peek: never changes the active page, track or key roles. */
static struct {
    uint32_t active, until, page, screen, part, bank;
} br_smk_fader_peek;
static void br_smk_fader_peek_cancel(void)
{
    if (br_smk_fader_peek.active) { br_smk_fader_peek.active = 0u; br_ui.redraw = 1u; }
}
static void br_smk_fader_peek_tick(void)
{
    if (br_smk_fader_peek.active &&
        ((int32_t)(fm1_ms - br_smk_fader_peek.until) >= 0 ||
         br_ui.page != br_smk_fader_peek.page || br_ui.part != br_smk_fader_peek.part ||
         br_ui.screen[br_ui.page] != br_smk_fader_peek.screen))
        br_smk_fader_peek_cancel();
}
static uint32_t br_smk_on_mixer_page(void)
{
    return br_gate_page() || (br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]);
}
static void br_smk_fader_moved(uint32_t bank)
{
    if (!smk_fader_peek_wanted(br_step_press_pending() || br_ui.step_edit_active, br_smk_on_mixer_page())) return;
    br_smk_fader_peek.active = 1u;
    br_smk_fader_peek.bank = bank;
    br_smk_fader_peek.until = fm1_ms + 1600u;
    br_smk_fader_peek.page = br_ui.page;
    br_smk_fader_peek.screen = br_ui.screen[br_ui.page];
    br_smk_fader_peek.part = br_ui.part;
    br_ui.redraw = 1u;
}
/* FADER BANK: the bank's real mixer page, persistent (bank 1: KIT LEVEL, bank 0: MIXER). */
static uint32_t br_smk_fader_bank_select(uint32_t bank)
{
    br_smk_fader_peek_cancel();
    if (smk_bank_press_decide(br_step_press_pending() || br_ui.step_edit_active) == SMK_BANK_MESSAGE)
        br_message(bank ? "DRUM FADERS" : "TRACK FADERS");
    else {
        br_ui.page = bank ? BR_PAGE_ROUTE : BR_PAGE_MIX;
        br_ui.screen[br_ui.page] = bank ? 3u : 0u;
        br_ui.edit_key = 0xffffffffu;
        br_gate_select_lane(br_ui.lane); br_ui.redraw = 1u;
    }
    return 1u;
}
#endif

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
    uint32_t global_swing = br_ui.page == BR_PAGE_MIX && page == 2u && k == 1u;
    uint32_t at = br_gate_page() ? BR_DRUM_PART : global_swing ? 0u : br_ui.page == BR_PAGE_MIX && (!page || page == 3u) ? k : selected;
    br_part_t *p = &br_part[at];
    volatile br_sound_t *s = &p->sound;
    volatile br_mod_config_t *m = &p->mod;
    volatile br_fx_config_t *f = &p->fx;
    int32_t v = br_clip(c.value + turn, c.low, c.high);
    uint32_t key = br_ui.page * 256u + page * 16u + selected * 4u + k;
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) {
#if BR_ARR_UI
        br_arr_turn(k, turn);
#endif
        return;
    }
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
    if (br_ui.page == BR_PAGE_MIX && page == 2u && !k && br_midi_clock_active) return;
    if (br_ui.page == BR_PAGE_MIX && page == 2u && k == 2u) {
        if (v == c.value) return;
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT && defined(FM1_DEVICE)
        br_engine_clock_source_changed(fm1_ticks());
#endif
        br_midi_clock_enabled = (uint8_t)v;
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms; br_ui.redraw = 1u;
        return;
    }
    if (br_ui.page == BR_PAGE_MIX && (page == 7u || page == 8u)) {
        if ((page == 7u && k == 3u) || v == c.value) return;
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        br_midi_channel_set(page == 7u ? k : 3u + k, (uint8_t)v);
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms; br_ui.redraw = 1u;
        return;
    }
    if (br_ui.page == BR_PAGE_ROUTE && page < 2u) {
        volatile br_route_config_t *r = &br_part[br_ui.part].mod.route[BR_ROUTES + page];
        if (v == c.value) return;
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
        p->revision++;
        if (!k) r->source = br_source_ui_value((uint32_t)v);
        else if (k == 1u) r->target = br_target_ui_value((uint32_t)v);
        else if (k == 2u) r->amount = v; else r->enabled = (uint32_t)v;
        RING_PUBLISH();
        p->revision++;
        br_ui.redraw = 1u;
        return;
    }
    if (br_gate_page() && br_ui.lane != k) br_gate_select_lane(k);
    if (br_ui.page == BR_PAGE_MIX && !page && br_ui.part != k) {
        br_ui.part = k; br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
    }
    if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
    br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
    p->revision++;
    if ((br_ui.page == BR_PAGE_MIX && page != 0u && page != 3u) ||
        (br_ui.page == BR_PAGE_LFO && page == BR_LFOS) ||
        (br_ui.page == BR_PAGE_FX && page == BR_FX_REVERB)) br_master.revision++;
    if (br_home_voice_page()) {
        if (!k) s->portamento = (uint32_t)v;
        else if (k == 1u) s->glide_mode = (uint32_t)v;
        else if (k == 2u) s->amp_mode = (uint32_t)v;
        else s->lpg_gate = (uint32_t)v;
    } else if (br_sound_mod_page()) {
        if (!k) s->timbre_mod = (uint32_t)v;
        else if (k == 1u) s->harmonics_mod = (uint32_t)v + 1u;
        else if (k == 2u) s->morph_mod = (uint32_t)v;
        else s->fm_mod = (uint32_t)v;
    } else if (br_ui.page == BR_PAGE_HOME) {
        if (!page && s->preset == BR_KIT_PLAITS) {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->plaits_harmonics = (uint32_t)v;
            else if (k == 2u) s->color = (uint32_t)v;
            else s->gate = (uint32_t)v;
        } else if (!page && (s->preset == BR_KIT_808 || s->preset == BR_KIT_909)) {
            if (!k) s->tune = v;
            else if (k == 2u) s->drum_snap = (uint32_t)v;
            else if (k == 3u) s->gate = (uint32_t)v;
        } else if (!page && s->preset == BR_KIT_BRAIDS) {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->color = (uint32_t)v;
            else if (k == 2u) s->drum_snap = (uint32_t)v;
            else if (k == 3u) s->gate = (uint32_t)v;
        } else if (!page && br_is_plaits(s->preset) && !br_is_kit(s->preset)) {
            if (!k) s->timbre = (uint32_t)v;
            else if (k == 1u) s->plaits_harmonics = (uint32_t)v;
            else if (k == 2u) s->color = (uint32_t)v;
            else s->plaits_aux = (uint32_t)v;
        } else if (br_home_filter_page()) {
            if (!k) s->filter = (uint32_t)v;
            else if (k == 1u) s->filter_resonance = (uint32_t)v;
            else if (k == 3u) s->filter_keytrack = v ? 127u : 0u;
            else s->filter_env_depth = v;
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
            else if (k == 2u) f->drive_tone = (uint32_t)v; else br_part_pending[br_ui.part].drive_bias = (uint8_t)v;
        } else if (page == BR_FX_DELAY) {
            if (!k) f->delay_engine = (uint32_t)v;
            else if (k == 1u) {
                uint32_t stored; uint8_t short_note;
                br_fx_delay_choice_set((uint32_t)v, &stored, &short_note);
                f->delay_time = stored; br_part_pending[br_ui.part].delay_short = short_note;
            }
            else if (k == 2u) f->delay_feedback = (uint32_t)v; else br_part_pending[br_ui.part].delay_tone = (uint8_t)v;
        } else if (page == BR_FX_REVERB) {
            if (k == 3u) { br_master.reverb_extra[0] = (uint8_t)v; br_master.reverb_manual = v ? (br_master.reverb_manual | 32u) : (br_master.reverb_manual & ~33u); }
            else if (!k) br_master.reverb_engine = br_reverb_ui_id[(uint32_t)v];
            else if (k == 1u) br_master.reverb_size = (uint32_t)v;
            else br_master.reverb_decay = (uint32_t)v;
        } else {
            if (!k) f->mod_engine = (uint32_t)v; else if (k == 1u) f->mod_rate = (uint32_t)v;
            else if (k == 2u) f->mod_depth = (uint32_t)v; else br_part_pending[br_ui.part].mod_feedback = (uint8_t)v;
        }
    } else if (br_ui.page == BR_PAGE_MIX) {
        if (!page) s->level = (uint32_t)v;
        else if (page == 1u) {
            if (!k) {
                br_master.fx.filter = (uint32_t)v;
                if ((br_ui.glo_pending && br_ui.glo_snap) || (br_ui.fx_snap_pending && br_ui.fx_snap_used))
                    ++br_master.fx.filter_snap_generation;
            } else if (k == 1u) br_master.fx.resonance = (uint32_t)v;
            else if (k == 2u) br_master.fx.env_depth = v;
        } else if (page == 6u) {
            if (!k) br_master.fx.heat_type = (uint32_t)v;
            else if (k == 1u) br_master.fx.heat_amount = (uint32_t)v;
        } else if (page == 2u) {
            if (!k) br_bpm = (uint32_t)v;
            else if (k == 1u) br_swing = (uint32_t)v;
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
            volatile br_lfo_config_t *l = page == BR_LFOS ? &br_master.global_lfo : &m->lfo[page];
            if (!k) l->wave = (uint32_t)v; else if (k == 1u) l->rate = (uint32_t)v;
            else if (k == 2u) l->target = page == BR_LFOS ? br_global_target_id[(uint32_t)v] : br_target_ui_value((uint32_t)v); else l->depth = (uint32_t)v;
        }
    } else if (br_ui.page == BR_PAGE_ROUTE) {
        if (br_gate_page()) {
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
    if ((br_ui.page == BR_PAGE_MIX && page != 0u && page != 3u) ||
        (br_ui.page == BR_PAGE_LFO && page == BR_LFOS) ||
        (br_ui.page == BR_PAGE_FX && page == BR_FX_REVERB)) {
        RING_PUBLISH(); br_master.revision++;
    }
    br_ui.redraw = 1;
}


static void br_seq_press(void)
{
    br_ui.seq_pressed = 1; br_ui.seq_turned = 0;
    br_ui.seq_was_active = br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ];
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) br_ui.seq_was_active = 2u; /* SEQ-first chord origin */
#endif
    br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0;
    br_ui.redraw = 1;
}
static void br_seq_release(void)
{
    if (!br_ui.seq_turned && br_ui.seq_was_active == 1u && br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ]) {
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
        case BTN_HOME:
#if BR_PRODUCT_SMK37
            /* KNOB BANK: on the synth SOUNDS list it steps to the next category; elsewhere it is HOME. */
            if (br_sel_browse() && br_ui.part < BR_SYNTH_PARTS) br_smk_next_sound_category();
            else
#endif
            br_open_module(BR_PAGE_HOME);
            break;
        case BTN_ENV: br_open_module(BR_PAGE_ENV); break;
        case BTN_FX: br_open_module(BR_PAGE_FX); break;
        case BTN_GLO: br_open_module(BR_PAGE_MIX); break;
        case BTN_SEQ:
#ifdef BR_PATTERN_SONG_EXPERIMENT
            if (br_ui.page == BR_PAGE_PS) {
                br_ui.ps_held_key = 0u;
#if BR_ARR_UI
                br_message("KNOB4: PERFORM MODE"); break;
#endif
                break;
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
            /* LFO 1, LFO 2, and the global LFO. */
            if (br_ui.page == BR_PAGE_LFO) br_select_screen(1);
            else br_open_module(BR_PAGE_LFO);
            break;
        case BTN_EDIT:
            /* EDIT opens the kit mixer and cycles through modulation and patches. */
            if (br_ui.page == BR_PAGE_ROUTE
#if BR_PRODUCT_SMK37
                && !br_gate_page()            /* KIT LEVEL (from FADER BANK) goes to PATCH 1 */
#endif
                ) br_select_screen(1);
            else {
                br_ui.page = BR_PAGE_ROUTE;
#if BR_PRODUCT_SMK37
                br_ui.screen[BR_PAGE_ROUTE] = 0u;   /* no KIT LEVEL here: the faders set the kit levels */
#else
                br_ui.screen[BR_PAGE_ROUTE] = 3u;
#endif
                br_ui.edit_key = 0xffffffffu;
                br_gate_select_lane(br_ui.lane); br_ui.redraw = 1u;
            }
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
                uint32_t banks = (br_part[br_ui.part].length + BR_BANK_STEPS - 1u) / BR_BANK_STEPS;
                if (!banks) banks = 1u;
                if (b == BTN_OCT_DOWN) bank = bank ? bank - 1u : banks - 1u;
                else bank = (bank + 1u) % banks;
                if (bank != br_ui.step_bank) {
                    br_ui.step_bank = bank;
                    br_ui.cursor = bank * BR_BANK_STEPS + br_ui.cursor % BR_BANK_STEPS;
                    if (br_ui.cursor >= br_part[br_ui.part].length) br_ui.cursor = br_part[br_ui.part].length - 1u;
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
    br_ui.redraw = 1u;
    /* A second step pressed before the first became an edit is a plain tap. */
    if (!held || br_step_press_pending()) {
        br_step_trigger_toggle(part, step, lane, kit,
                               br_step_active(part, step, lane, kit));
        return;
    }
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

/* Keep a shifted note within the neighboring step boundaries, accounting for swing. */
static void br_micro_bounds(uint32_t part, uint32_t step, int32_t *minimum, int32_t *maximum)
{
    uint32_t division = br_part[part].division > 2u ? 2u : br_part[part].division;
    uint32_t ticks = 16u >> division, length = br_part[part].length;
    uint32_t prev = step ? step - 1u : (length ? length - 1u : 0u);
    uint32_t swing = br_swing > BR_SWING_MAX ? BR_SWING_MAX : br_swing;
    uint32_t prev_ratio = (prev & 1u) ? 128u - swing : 128u + swing;
    uint32_t this_ratio = (step & 1u) ? 128u - swing : 128u + swing;
    *minimum = -(int32_t)((ticks * prev_ratio - 1u) / 128u);
    *maximum = (int32_t)((ticks * this_ratio - 1u) / 128u);
    if (*minimum < -16) *minimum = -16;
    if (*maximum > 15) *maximum = 15;
}
/* The stored offset is in 1/128 notes. Reduce it to familiar note fractions. */
static void br_micro_text(char *text, uint32_t capacity, int32_t value)
{
    uint32_t numerator = (uint32_t)(value < 0 ? -value : value), denominator = 128u;
    char number[8];
    if (!numerator) { br_text_copy(text, "ON GRID", capacity); return; }
    while (!(numerator & 1u) && denominator > 1u) { numerator >>= 1; denominator >>= 1; }
    br_text_copy(text, value < 0 ? "-" : "+", capacity);
    br_number(number, (int32_t)numerator); br_text_append(text, number, capacity);
    br_text_append(text, "/", capacity); br_number(number, (int32_t)denominator);
    br_text_append(text, number, capacity);
}
static void br_step_micro_shift(int32_t direction)
{
    uint32_t part, step, lane;
    int32_t old, value, minimum, maximum;
    char msg[25], amount[16];
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
    br_micro_text(amount, sizeof amount, value); br_text_append(msg, amount, sizeof msg);
    br_message(msg);
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
    uint32_t length_steps = br_part[part].length, coverage = 0u, start = step;
    if (!length_steps || length_steps > BR_STEPS || step >= length_steps) return 0u;
    /* Recent notes cover this cell most often. A full cell is already the
     * maximum union, so older notes cannot change its pixels. */
    for (uint32_t distance = 0u; distance < length_steps; ++distance) {
        uint32_t active = br_step_active(part, start, lane, kit);
        uint32_t length, elapsed, remaining;
        if (active) {
            length = br_step_length_visual_quanta(part, start, lane);
            elapsed = distance * 16u;
            if (length > elapsed) {
                remaining = length - elapsed;
                if (remaining >= 16u) return 16u;
                if (remaining > coverage) coverage = remaining;
            }
        }
        start = start ? start - 1u : length_steps - 1u;
    }
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
static br_record_hold_t br_record_hold[BR_SYNTH_PARTS][128]
    __attribute__((section(".pool"), aligned(4)));
static br_record_hold_t br_record_kit_hold[BR_DRUM_LANES]
    __attribute__((section(".pool"), aligned(4)));
#else
static br_record_hold_t br_record_hold[BR_SYNTH_PARTS][128];
static br_record_hold_t br_record_kit_hold[BR_DRUM_LANES];
#endif
/* Drum MIDI aliases are canonicalized to one of four lanes before recording. */
static br_record_hold_t *br_record_hold_for(uint32_t part, uint32_t note)
{
    return part == BR_DRUM_PART ? &br_record_kit_hold[br_drum_lane(note)] :
                                  &br_record_hold[part][note];
}
static uint16_t br_record_generation;
#if BR_PRODUCT_SMK37 && defined(FM1_DEVICE) && defined(__ELF__)
static uint16_t br_record_key_generation[BR_PLAY_KEYS] __attribute__((section(".pool"), aligned(4)));
#else
static uint16_t br_record_key_generation[BR_PLAY_KEYS];
#endif

/* Played-note timing. dinghy.c brackets the dispatch of each played note (MIDI in, keys, pads) with
 * br_note_played(arrival tick, tick now) / br_note_played_end(). The engine then schedules its note
 * events at arrival + BR_NOTE_DELAY_MS; recording places the note where it was played: the sequencer
 * position and record clock now, less the time the foreground took to get to it (up to 200 ms). */
static uint32_t br_note_lag_frames;
static void br_note_played(uint32_t arrival, uint32_t now)
{
    uint32_t lag = now - arrival;
    BR_UIT_NOTE(arrival, now);
    if ((int32_t)lag < 0 || lag > 200u * 24000u) lag = 0u;   /* beyond 200 ms: a stale stamp, not a stall */
    br_note_lag_frames = lag * (FS / 100u) / 240000u;
    br_engine_note_arrival(arrival);
}
static void br_note_played_end(void)
{
    br_note_lag_frames = 0u;
    br_engine_note_arrival_end();
}

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
    hold = br_record_hold_for(part, note);
    if (!hold->active || (generation && hold->generation != generation)) return;
    hold->active = 0u;
    hold->midi_source = 0u;
    if (hold->step >= BR_STEPS) return;
    step = hold->step;
    duration = hold->step_frames ? hold->step_frames : 1u;
    elapsed = (clock ? *clock : br_engine_record_clock(part) - br_note_lag_frames) - hold->start_clock;
    if ((int32_t)elapsed < 0) elapsed = 0u;
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
    even = nominal * (128u + (br_swing > BR_SWING_MAX ? BR_SWING_MAX : br_swing)) / 128u;
    odd = nominal * (128u - (br_swing > BR_SWING_MAX ? BR_SWING_MAX : br_swing)) / 128u;
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
        } else {
            br_engine_record_position(part, &step, &elapsed, &duration, &unit);
            /* Back to where it was played (not at the very start of play: nothing came before). */
            if (br_note_lag_frames && (step || elapsed)) {
                uint32_t back = br_note_lag_frames, length = br_part[part].length, guard = BR_STEPS;
                if (!length || length > BR_STEPS) length = BR_STEPS;
                while (back > elapsed && guard--) {
                    back -= elapsed;
                    step = step ? step - 1u : length - 1u;
                    duration = br_engine_step_duration(part, step);
                    if (!duration) duration = 1u;
                    elapsed = duration;
                }
                elapsed -= back <= elapsed ? back : elapsed;
            }
        }
        br_record_quantize_event(part, step, elapsed, duration, unit,
                                 br_engine_step_duration(part, step),
                                 br_seq_record_quantize[part], &step, &micro);
    }
    if (step >= br_part[part].length || step >= BR_STEPS) return 0u;
    hold = br_record_hold_for(part, note);
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
    br_engine_record_skip(part, step, lane);
    token = ++br_record_generation;
    if (!token) token = ++br_record_generation;
    hold->start_clock = at ? at->clock : br_engine_record_clock(part) - br_note_lag_frames;
    hold->generation = token;
    hold->step_frames = (uint16_t)br_engine_step_duration(part, step);
    hold->step = (uint8_t)step;
    hold->active = 1u;
    hold->midi_source = (uint8_t)(midi_source != 0u);
    /* Stopped recording is step entry: move on to the next step. */
    if (!br_playing && part == br_ui.part) {
        br_ui.cursor = (step + 1u) % br_part[part].length;
        br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS;
    }
    br_ui.redraw = 1u;
    return token;
}

static void br_record_note_cancel_all(void)
{
    uint32_t part, note;
    for (part = 0u; part < BR_PARTS; ++part)
        for (note = 0u; note < (part == BR_DRUM_PART ? BR_DRUM_LANES : 128u); ++note) {
            br_record_hold_t *hold = part == BR_DRUM_PART ? &br_record_kit_hold[note] :
                                                            &br_record_hold[part][note];
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
    for (note = 0u; note < (part == BR_DRUM_PART ? BR_DRUM_LANES : 128u); ++note) {
        br_record_hold_t *hold = part == BR_DRUM_PART ? &br_record_kit_hold[note] :
                                                        &br_record_hold[part][note];
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
    memset(br_record_kit_hold, 0, sizeof br_record_kit_hold);
    memset(br_record_key_generation, 0, sizeof br_record_key_generation);
    br_record_generation = 0u;
}

static void br_record_note_midi_off(uint32_t part, uint32_t note)
{
    br_record_hold_t *hold;
    if (part >= BR_PARTS || note >= 128u) return;
    if (part == BR_DRUM_PART)
        note = br_drum_lane_note(br_drum_lane(note));
    hold = br_record_hold_for(part, note);
    /* The MIDI routing ledger has already suppressed older note-offs. */
    hold->midi_stale_offs = 0u;
    if (hold->midi_source) br_record_note_finish(part, note, 0u);
}

/* ---- Arpeggiator and HOLD, main-loop side. The audio engine plays the
 * arp from br_arp_keys; this file decides which keys are in each set. */
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static uint16_t br_arp_rec_gen[BR_SYNTH_PARTS][128] __attribute__((section(".pool"), aligned(4)));
#else
static uint16_t br_arp_rec_gen[BR_SYNTH_PARTS][128];
#endif

static uint32_t br_arp_latched(uint32_t p)
{
    (void)p;
    return br_hold_on;
}

/* Physical keys currently held for track p, routed to the arp or not. */
static uint32_t br_keys_held(uint32_t p, uint32_t arp)
{
    uint32_t k, count = 0u;
    for (k = 0u; k < BR_PLAY_KEYS; ++k)
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
        for (k = 0u; k < BR_PLAY_KEYS; ++k)
            if (br_ui.held_arp[k] && br_ui.held_part[k] == p &&
                br_ui.held_note[k] == br_arp_keys[p].notes[i] + 1u) {
                notes[count++] = br_arp_keys[p].notes[i]; break;
            }
    br_arp_publish(p, notes, count);
}

static void br_arp_toggle(void)
{
    uint32_t p;
    if (br_ui.part == BR_DRUM_PART) {
#if BR_PRODUCT_SMK37
        br_smk_retrig.opened = 0u;
#endif
        br_ui.page = BR_PAGE_PATTERN; br_ui.redraw = 1u; return;
    }
    br_arp_on ^= 1u;
    if (!br_arp_on) for (p = 0u; p < BR_SYNTH_PARTS; ++p) br_arp_publish(p, 0, 0u);
    br_message(br_arp_on ? "ARP ON" : "ARP OFF");
}

static uint32_t br_midi_output_destination(uint32_t part, uint32_t note)
{
    return part < BR_SYNTH_PARTS ? part : BR_SYNTH_PARTS + br_drum_lane(note);
}

static void br_midi_output_release_destination(uint32_t destination)
{
    for (uint32_t k = 0u; k < BR_PLAY_KEYS; ++k) {
        uint32_t note = br_ui.held_note[k], part = br_ui.held_part[k];
        if (br_midi_out_held[k] && note && br_midi_output_destination(part, note - 1u) == destination) {
            midi_send(0x80u | part, note - 1u, 0); br_midi_out_held[k] = 0u;
        }
        note = br_ui.sustain_note[k]; part = br_ui.sustain_part[k];
        if (br_midi_out_sustain[k] && note && br_midi_output_destination(part, note - 1u) == destination) {
            midi_send(0x80u | part, note - 1u, 0); br_midi_out_sustain[k] = 0u;
        }
    }
}

static void br_hold_release_all(void)
{
    uint32_t k;
    for (k = 0u; k < BR_PLAY_KEYS; ++k) {
        uint32_t note = br_ui.sustain_note[k], p = br_ui.sustain_part[k];
        if (!note) continue;
        if (br_ui.sustain_gen[k]) br_record_note_finish(p, note - 1u, br_ui.sustain_gen[k]);
        BR_POLY_NOTE_OFF(p, note - 1u, k + 1u);
        if (br_midi_out_sustain[k]) midi_send(0x80u | p, note - 1u, 0);
        br_midi_out_sustain[k] = 0u;
        br_ui.sustain_note[k] = 0u; br_ui.sustain_gen[k] = 0u;
    }
}

#if BR_PRODUCT_SMK37
static uint8_t br_smk_pedal_down;
static void br_smk_pedal_set(uint32_t down)
{
    down = !!down;
    if (down == br_smk_pedal_down) return;
    br_smk_pedal_down = (uint8_t)down;
    if (!down && !br_hold_on) br_hold_release_all();
}
#endif

static void br_hold_toggle(void)
{
    uint32_t p;
    br_hold_on ^= 1u;
    if (!br_hold_on) {
#if BR_PRODUCT_SMK37
        if (!br_smk_pedal_down)
#endif
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

#if BR_POLY_UI && !BR_PRODUCT_SMK37
/* The PUNCH overlay opened by the shift key keeps the key-playing status of the page it covers, so a
 * polyphonic chord keeps sounding and the punch effects reach it (the SMK-37 treats PUNCH as a
 * key-playing page). Only the polyphony suspend rule asks; the keys themselves behave as before. */
static uint8_t br_punch_poly_keys;
static uint32_t br_poly_keys_page(void)
{
    return br_ui.page == BR_PAGE_PUNCH ? br_punch_poly_keys : br_page_plays_keys();
}
#define BR_POLY_KEYS_PAGE() br_poly_keys_page()
#elif BR_PRODUCT_SMK37
#define BR_POLY_KEYS_PAGE() (br_page_plays_keys() || br_seq_grid_page())
#else
#define BR_POLY_KEYS_PAGE() br_page_plays_keys()
#endif
/* Shift key held: show the punch settings; released: put the old page back.
 * The effect itself is a single word the audio path reads. */
static void br_punch_changed(void)
{
    if (br_punch_ks.shift && br_ui.page != BR_PAGE_PUNCH) {
#if BR_POLY_UI && !BR_PRODUCT_SMK37
        br_punch_poly_keys = (uint8_t)br_page_plays_keys();  /* the FM-1 PUNCH page opens only here */
#endif
        br_punch_return_page = br_ui.page; br_punch_return_screen = br_ui.screen[br_ui.page];
        br_ui.page = BR_PAGE_PUNCH; br_ui.screen[BR_PAGE_PUNCH] = 0u;
    } else if (!br_punch_ks.shift && br_ui.page == BR_PAGE_PUNCH) {
        br_ui.page = br_punch_return_page; br_ui.screen[br_ui.page] = br_punch_return_screen;
    }
    if (br_ui.page == BR_PAGE_PUNCH && br_punch_ks.active == BR_PUNCH_LOOP)
        br_ui.screen[BR_PAGE_PUNCH] = 0u; /* LOOP has no second-page parameter. */
    RING_PUBLISH();
    br_punch_loop_len = br_punch_cfg.loop_len & 3u;
#if BR_POLY_UI && !BR_PRODUCT_SMK37
    /* The voices of models 15, 18 and 19 borrow the delay memory punch echo plays from (the SMK-37
     * refuses its echo pad the same way, in smk37_panel.c). */
    if (br_punch_ks.active == BR_PUNCH_ECHO && br_fx_poly_loan_active()) {
        br_punch_effect = BR_PUNCH_NONE;
        br_message("ECHO UNAVAILABLE IN POLY");
    } else
#endif
    br_punch_effect = br_punch_ks.active;
    br_ui.edit_key = 0xFFFFFFFFu; br_ui.redraw = 1u;
}

/* Preserve the sequencer, browser and drum mixer's distinct key roles. */
static uint32_t br_punch_page(void)
{
    return br_ui.page == BR_PAGE_PUNCH ||
        (br_ui.page != BR_PAGE_SCALE && br_page_plays_keys());
}

#if BR_PRODUCT_SMK37
static void br_play_key(uint32_t key, int held, uint32_t p, uint32_t note, uint32_t velocity)
{
    uint32_t kit = br_is_kit(br_part[p].sound.preset);
    if (br_sel_browse()) {
        if (!br_sel.shown) br_sel_show_now();
    } else if (!br_page_plays_keys() && !br_seq_grid_page()) return;
    if (br_arp_on && !kit && p < BR_SYNTH_PARTS) {
        /* A latched set is replaced by the first key of a new gesture. */
        br_arp_add(p, note, br_arp_latched(p) && !br_keys_held(p, 1u));
        br_ui.held_note[key] = note + 1u; br_ui.held_part[key] = p;
        br_ui.held_arp[key] = 1u;
        if (!held) br_key_up(key);
        return;
    }
    /* HOLD sustains until the next key pressed with no other key down. */
    if (br_hold_on && !br_smk_pedal_down && !br_keys_held(p, 0u)) br_hold_release_all();
    br_ui.held_note[key] = note + 1u; br_ui.held_part[key] = p;
    BR_POLY_NOTE_ON(p, note, velocity, key + 1u);
    br_midi_out_held[key] = br_midi_channels[br_midi_output_destination(p, note)] != 0u;
    if (br_midi_out_held[key]) midi_send(0x90u | p, note, velocity);
    br_record_key_generation[key] = br_record_note_begin(
        p, note, velocity, kit, kit ? br_drum_lane(note) : 0u, 0u);
    if (!held) br_key_up(key);
}

/* Physical keybed notes have slots separate from the pad-backed step grid. */
static void br_smk_seq_keys(uint32_t *previous, uint32_t held, uint32_t edges,
                            uint32_t active, const uint8_t *velocity)
{
    uint32_t p = br_ui.part;
    for (uint32_t k = 0; k < 27u; ++k) {
        uint32_t bit = 1u << k, slot = 37u + k;
        if ((*previous & bit) && (!active || !(held & bit) || (edges & bit))) br_key_up(slot);
        if (active && (edges & bit)) {
            uint32_t note = (uint32_t)(53 + (int32_t)k + 12 * br_ui.octave);
            br_play_key(slot, (held & bit) != 0u, p, note, velocity[k]);
        }
    }
    *previous = active ? held : 0u;
}
#endif
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
#if !BR_PRODUCT_SMK37 || !defined(FM1_DEVICE)
    if (br_punch_page() && br_punch_key(&br_punch_ks, key, 1u)) {
        br_punch_changed();
        if (!held && br_punch_key(&br_punch_ks, key, 0u)) br_punch_changed();
        return;
    }
#endif
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) {
        uint32_t place = br_key_place(key);
        if (br_black_key(key)) {
#if BR_ARR_UI
            if (br_arr_key(place)) { br_ui.redraw = 1u; return; }
            if (!BR_ARR_VIEW) {
                br_chain_down(key, place);
                if (!held && br_chain_up(key)) { br_ui.redraw = 1u; return; }
                if (br_chain.gesture) { br_ui.redraw = 1u; return; }
            }
#endif
            br_ui.ps_held_key = key + 1u; br_ui.ps_held_used = 0u;
            if (!held) { if (!br_ps_select(place)) br_message("PATTERN FAILED"); br_ui.ps_held_key = 0u; }
        }
        br_ui.redraw = 1u; return;
    }
#endif
    br_clamp_ui_lane();
    uint32_t p = br_ui.part, kit = br_is_kit(br_part[p].sound.preset);
#if BR_PRODUCT_SMK37
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
#else
    uint32_t note = (uint32_t)(53 + (int32_t)key + 12 * br_ui.octave);
    if (kit) note = br_drum_lane_note(key % BR_DRUM_LANES);
    /* Step grid: white keys are steps; black keys do nothing (SELECT picks a kit lane). */
    if (br_seq_grid_page()) {
        if (!br_black_key(key)) { br_step_press_begin(key, held, kit != 0u); br_ui.redraw = 1; }
        return;
    }
#endif
    /* Sequencer keys still edit their targets; only HOME and
     * ARP may trigger notes from the physical keyboard. MIDI is separate. */
#if BR_PRODUCT_SMK37
    uint32_t velocity = 100u;
#if defined(FM1_DEVICE)
    if (fm1_in.key_vel[SMK_KEY_F3 + key])
        velocity = fm1_in.key_vel[SMK_KEY_F3 + key];
#endif
    br_play_key(key, held, p, note, velocity);
#else
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
    BR_POLY_NOTE_ON(p, note, 100, key + 1u);
    br_midi_out_held[key] = br_midi_channels[br_midi_output_destination(p, note)] != 0u;
    if (br_midi_out_held[key]) midi_send(0x90u | p, note, 100);
    if (key < 27u)
        br_record_key_generation[key] = br_record_note_begin(
            p, note, 100u, kit, kit ? br_drum_lane(note) : 0u, 0u);
    if (!held) br_key_up(key);
#endif
}
#if !BR_PRODUCT_SMK37
/* Key whose press set a held step's pitch; it sounds until that key is released. */
static uint8_t br_step_pitch_key, br_step_pitch_part, br_step_pitch_note;
/* Another white key pressed while a step key is held. Until the hold has become
 * an edit it is an ordinary tap (A8); after that, on a tonal track, it sets the
 * held step's pitch and sounds it (A10). Kit edits and black keys ignore it. */
static void br_step_other_key(uint32_t key)
{
    uint32_t p = br_ui.step_press_part, step = br_ui.step_press_step, note;
    if (!br_step_press_pending() || key == br_ui.step_press_key || br_black_key(key)) return;
    if (!br_ui.step_edit_active) { br_key_down(key, 0); return; }
    if (br_ui.step_press_kit || !br_step_active(p, step, 0u, 0u)) return;
    note = (uint32_t)br_clip(53 + (int32_t)key + 12 * br_ui.octave, 0, 127);
    br_part[p].revision++;
    br_step_note_set(p, step, 0u, note);
    RING_PUBLISH(); br_part[p].revision++;
#if BR_POLY_UI
    /* The grid is not a key-playing page, so polyphony has normally ended before a step is held. While
     * a refused or pending exit keeps it active, the edit stands but is not auditioned: an untagged
     * audition must never become a polyphonic voice. */
    if (br_poly_mode_active()) { br_ui.redraw = 1u; return; }
#endif
    if (br_step_pitch_key) br_engine_note_off(br_step_pitch_part, br_step_pitch_note);
    br_engine_note_on(p, note, 100);
    br_step_pitch_key = (uint8_t)(key + 1u); br_step_pitch_part = (uint8_t)p; br_step_pitch_note = (uint8_t)note;
    br_ui.redraw = 1u;
}
#endif
static void br_key_up(uint32_t key)
{
#ifdef BR_PATTERN_SONG_EXPERIMENT
#if BR_ARR_UI
    if (br_chain_up(key)) {
        if (br_ui.ps_held_key == key + 1u) br_ui.ps_held_key = br_ui.ps_held_used = 0u;
        br_ui.redraw = 1u;
        return;
    }
#endif
    if (br_ui.ps_held_key == key + 1u) {
        if (br_ui.page == BR_PAGE_PS && !br_ui.ps_held_used && !br_ps_select(br_key_place(key))) br_message("PATTERN FAILED");
        br_ui.ps_held_key = br_ui.ps_held_used = 0u; br_ui.redraw = 1u;
    }
#endif
    if (br_ui.retrig_key == key + 1u) { br_ui.retrig_key = 0u; br_ui.redraw = 1u; }
#if !BR_PRODUCT_SMK37
    if (br_step_pitch_key == key + 1u) { br_engine_note_off(br_step_pitch_part, br_step_pitch_note); br_step_pitch_key = 0u; }
#endif
    if (
#if BR_PRODUCT_SMK37 && defined(FM1_DEVICE)
        key != BR_PUNCH_SHIFT_KEY &&
#endif
        br_punch_key(&br_punch_ks, key, 0u)) { br_punch_changed(); return; }
    if (br_ui.step_press_key == key) br_step_press_update(0u);
    uint32_t note = br_ui.held_note[key], p = br_ui.held_part[key];
    if (!note) return;
#if BR_PRODUCT_SMK37
    if (br_ui.held_arp[key]) {
#else
    if (key < 27u && br_ui.held_arp[key]) {
#endif
        br_ui.held_arp[key] = 0u;
        br_ui.held_note[key] = 0u;
        if (!br_arp_latched(p)) br_arp_remove(p, note - 1u);
        br_ui.redraw = 1;
        return;
    }
#if BR_PRODUCT_SMK37
    if (br_hold_on || br_smk_pedal_down) {
#else
    if (br_hold_on && key < 27u) {
#endif
        if (br_ui.sustain_note[key] && (br_ui.sustain_note[key] != note || br_ui.sustain_part[key] != p)) {
            uint32_t old = br_ui.sustain_note[key] - 1u, op = br_ui.sustain_part[key];
            if (br_ui.sustain_gen[key]) br_record_note_finish(op, old, br_ui.sustain_gen[key]);
            BR_POLY_NOTE_OFF(op, old, key + 1u);
            if (br_midi_out_sustain[key]) midi_send(0x80u | op, old, 0);
        }
        br_ui.sustain_note[key] = note; br_ui.sustain_part[key] = p;
        br_midi_out_sustain[key] = br_midi_out_held[key]; br_midi_out_held[key] = 0u;
        br_ui.sustain_gen[key] = br_record_key_generation[key];
        br_record_key_generation[key] = 0u;
        br_ui.held_note[key] = 0u;
        br_ui.redraw = 1;
        return;
    }
#if BR_PRODUCT_SMK37
    if (br_record_key_generation[key])
        br_record_note_finish(p, note - 1u, br_record_key_generation[key]);
    br_record_key_generation[key] = 0u;
#else
    if (key < 27u && br_record_key_generation[key])
        br_record_note_finish(p, note - 1u, br_record_key_generation[key]);
    if (key < 27u) br_record_key_generation[key] = 0u;
#endif
    BR_POLY_NOTE_OFF(p, note - 1u, key + 1u);
    if (br_midi_out_held[key]) midi_send(0x80u | p, note - 1u, 0);
    br_midi_out_held[key] = 0u;
    br_ui.held_note[key] = 0;
    br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS;
    br_ui.redraw = 1;
}

static void br_midi_channel_set(uint32_t destination, uint8_t channel)
{
    if (destination < 7u && channel == 0u && br_midi_channels[destination] != 0u)
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
                BR_POLY_NOTE_ON(p, note, d2, 256u + channel * 128u + d1);
                if (br_recording) (void)br_record_note_begin(p, note, d2, p == BR_DRUM_PART,
                                                             p == BR_DRUM_PART ? i - BR_SYNTH_PARTS : 0u, 1u);
            } else {
                br_record_note_midi_off(p, note);
                BR_POLY_NOTE_OFF(p, note, 256u + channel * 128u + d1);
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
        uint32_t page_button = button[br_ui.page];
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
            } else level = br_part[br_ui.part].length > BR_BANK_STEPS ?
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
        if (!save_mode && ((k == BTN_ARP && br_arp_on && br_ui.part != BR_DRUM_PART) || (k == BR_HOLD_BUTTON && br_hold_on)))
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
#if BR_PRODUCT_SMK37
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
#else
        } else if (grid) {
            uint32_t place = br_key_place(k), retrig = br_retrig_page();
            if (br_black_key(k)) {
                /* Step grid: black keys are unlit (and inert); RETRIG keeps its lane keys. */
                level = !retrig ? FM1_LED_OFF :
                    kit ? (place < BR_DRUM_LANES && place == br_ui.lane ? FM1_LED_BRIGHT : FM1_LED_OFF) :
                    ((fm1_in.notes >> k) & 1u ? FM1_LED_BRIGHT : FM1_LED_OFF);
            } else {
                place += br_ui.step_bank * BR_BANK_STEPS;
                uint32_t active = place < br_part[br_ui.part].length && (kit ?
                    ((BR_STEP_HIT(br_part[br_ui.part].step[place]) >> br_ui.lane) & 1u) :
                    (br_part[br_ui.part].step[place] & BR_STEP_ON) != 0u);
                /* Step grid: on BRIGHT, beat starts (1 5 9 13) MID, other steps DIM, past the length OFF. */
                level = active ? (retrig ? FM1_LED_MID : FM1_LED_BRIGHT) :
                    retrig || place >= br_part[br_ui.part].length ? FM1_LED_OFF :
                    place & 3u ? FM1_LED_DIM : FM1_LED_MID;
                if (retrig && active && ((br_retrig_editing() && place == br_ui.retrig_step) ||
                    (br_playing && place == br_step_pos[br_ui.part]))) level = FM1_LED_BRIGHT;
            }
#endif
        } else if (br_punch_ks.shift) {
            /* The three lit levels are UI brightness steps 2/4, 3/4 and 4/4. */
            level = br_black_key(k) ? FM1_LED_BRIGHT : FM1_LED_DIM;
            uint32_t group = br_punch_group(k);
            if (group && group == punch_active) {
                static const uint8_t first[5] = {0u, 1u, 8u, 13u, 20u};
                uint32_t ordinal = (k - first[group]) / 2u;
                level = FM1_LED_DIM + (5u + punch_phase - ordinal) % 3u;
            }
#if BR_POLY_UI && !BR_PRODUCT_SMK37
            if (group == BR_PUNCH_ECHO && br_fx_poly_loan_active()) level = FM1_LED_OFF;   /* as the SMK-37 dims its echo pad */
#endif
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
/* Envelope times to three significant digits (0.50MS, 47.3MS, 512MS, 2.34S,
 * 12.0S): within 1% of the engine's exact sample count. */
static void br_env_time_text(char *text, uint32_t samples)
{
    uint32_t h = (samples * 1000u + FS / 200u) / (FS / 100u);   /* 10 us */
    uint32_t decimals = 2u, scale = 100u;
    const char *unit = "MS";
    char digits[4];
    while (h >= 1000u) {
        if (!decimals) { decimals = 3u; scale = 1000u; unit = "S"; }
        else { h = (h + 5u) / 10u; --decimals; scale /= 10u; }
    }
    br_number(text, (int32_t)(h / scale));
    if (decimals) {
        h %= scale;
        digits[0] = '.'; digits[1] = (char)('0' + h / (scale / 10u) % 10u);
        digits[2] = (char)('0' + h % 10u); digits[decimals + 1u] = 0;
        br_text_append(text, digits, 24);
    }
    br_text_append(text, unit, 24);
}
/* value / 10^places as a signed decimal, then the unit. */
static void br_decimal_text(char *text, int32_t value, uint32_t places, const char *unit)
{
    char frac[4];
    uint32_t scale = places == 3u ? 1000u : places == 2u ? 100u : 10u, i = places;
    uint32_t v = value < 0 ? (uint32_t)-value : (uint32_t)value;
    text[0] = '-';
    br_number(text + (value < 0), (int32_t)(v / scale)); br_text_append(text, ".", 24);
    frac[i] = 0;
    while (i--) { frac[i] = (char)('0' + v % 10u); v /= 10u; }
    br_text_append(text, frac, 24); br_text_append(text, unit, 24);
}
/* A depth knob into PITCH (quadratic, 24 st at 127, as the engine applies
 * it): cents below one semitone, otherwise semitones to a tenth. */
static void br_pitch_depth_text(char *text, int32_t depth)
{
    int32_t cents = (2400 * depth * depth + 8064) / 16129;
    if (depth < 0) cents = -cents;
    if (cents > -100 && cents < 100) { br_number(text, cents); br_text_append(text, "CT", 24); }
    else br_decimal_text(text, (cents + (cents < 0 ? -5 : 5)) / 10, 1u, "ST");
}
/* LFO DEPTH or PATCH AMOUNT on a slot aimed at PITCH. */
static uint32_t br_pitch_depth_control(uint32_t k)
{
    const volatile br_mod_config_t *m = &br_part[br_ui.part].mod;
    if (br_sound_mod_page()) return 0u;
    if (br_ui.page == BR_PAGE_LFO && k == 3u && br_ui.screen[BR_PAGE_LFO] < BR_LFOS)
        return m->lfo[br_ui.screen[BR_PAGE_LFO]].target == BR_TARGET_PITCH;
    return br_ui.page == BR_PAGE_ROUTE && k == 2u && !br_gate_page() &&
           br_part[br_ui.part].mod.route[BR_ROUTES + br_patch_slot()].target == BR_TARGET_PITCH;
}
/* LFO rate (Hz Q20, the engine's own value): seconds per cycle below 0.1 Hz,
 * three decimals below 1 Hz, two above; every read-out is within 0.5 %. */
static void br_rate_text(char *text, uint32_t hz_q20)
{
    if (hz_q20 < 104858u) br_decimal_text(text, (int32_t)((10485760u + hz_q20 / 2u) / hz_q20), 1u, "S");
    else if (hz_q20 < 1048576u) br_decimal_text(text, (int32_t)((hz_q20 * 1000u + 524288u) >> 20), 3u, "HZ");
    else br_decimal_text(text, (int32_t)((hz_q20 * 100u + 524288u) >> 20), 2u, "HZ");
}
/* Filter cutoff read-out from the engine's own law (instrument_filter_ladder.h):
 * -3 dB at resonance 0 is 20 Hz * ratio^position. "LP 240", "LP 1.2K", "HP 12K". */
#define BR_FILTER_LAW_ONLY
#include "instrument_filter_ladder.h"
#undef BR_FILTER_LAW_ONLY
static void br_filter_text(char *text, int32_t value)
{
    if (value == 63 || value == 64) br_text_copy(text, "OPEN", 24);
    else {
        char amount[16];
        uint32_t steps = (uint32_t)(value < 63 ? value : value - 65), hz = 20u << 16;
        uint64_t ratio = value < 63 ? BR_FILTER_LP_RATIO_Q24 : BR_FILTER_HP_RATIO_Q24;
        while (steps--) hz = (uint32_t)((hz * ratio + (1u << 23)) >> 24);
        hz = (hz + 32768u) >> 16;
        br_text_copy(text, value < 63 ? "LP " : "HP ", 24);
        if (hz < 1000u) br_number(amount, (int32_t)hz);
        else if (hz < 9950u) {
            uint32_t tenths = (hz + 50u) / 100u;
            char digit[3] = {'.', (char)('0' + tenths % 10u), 0};
            br_number(amount, (int32_t)(tenths / 10u)); br_text_append(amount, digit, 16);
        } else br_number(amount, (int32_t)((hz + 500u) / 1000u));
        br_text_append(text, amount, 24);
        if (hz >= 1000u) br_text_append(text, "K", 24);
    }
}
static void br_attenuverter_text(char *text, int32_t value)
{
    int32_t amount;
    if (value < 64) amount = -((64 - value) * 100 + 32) / 64;
    else amount = ((value - 64) * 100 + 31) / 63;
    text[0] = '+';
    br_number(text + (amount > 0), amount);
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
    uint32_t snap = br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u &&
        br_ui.glo_pending && br_ui.glo_snap;
    if (value != old || snap) {
        if (key != br_ui.edit_key || fm1_ms - br_ui.edit_time > 600u) br_remember();
        br_ui.edit_key = key; br_ui.edit_time = fm1_ms;
        br_master.revision++;
        br_master.fx.filter = (uint32_t)value;
        if (snap) ++br_master.fx.filter_snap_generation;
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
            if (kit) {
                int32_t semitones = c->value - (int32_t)BR_STEP_PITCH_DEFAULT;
                char number[8];
                br_text_append(text, " (", 24u);
                if (semitones > 0) br_text_append(text, "+", 24u);
                br_number(number, semitones); br_text_append(text, number, 24u);
                br_text_append(text, ")", 24u);
            }
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
    } else if (br_home_voice_page() && !k) {
        br_number(text, c->value); br_text_append(text, "%", 24u);
    } else if (br_home_voice_page() && k == 3u) {
        if (!c->value) br_text_copy(text, "OFF", 24u);
        else br_time_text(text, br_lpg_gate_samples((uint32_t)c->value));
    } else if (br_sound_mod_page()) {
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
    } else if (br_ui.page == BR_PAGE_MIX && page == 2u && k == 1u) {
        /* Position of the delayed note within a pair, in whole percent. */
        br_number(text, ((128 + c->value) * 100 + 128) / 256);
        br_text_append(text, "%", 24);
    } else if (br_ui.page == BR_PAGE_FX && page == BR_FX_DELAY && k == 1u && c->text) {
        /* The note value, and under it its length at the current tempo. */
        char time[12] = "";
        br_time_text(time, br_fx_delay_choice_samples((uint32_t)c->value) + FS / 2000u);   /* rounded */
        br_text_copy(text, c->text, 24); br_text_append(text, " ", 24); br_text_append(text, time, 24);
    } else if (c->text) br_text_copy(text, c->text, 24);
    else if (br_ui.page == BR_PAGE_MIX && page == 1u && !k) br_filter_text(text, c->value);
    else if (br_ui.page == BR_PAGE_ENV && page < 2u && k != 2u) {
        br_env_time_text(text, br_time_samples((uint32_t)c->value, k));
    } else if (br_ui.page == BR_PAGE_PUNCH && page) {
        br_number(text, (c->value * 100 + 15) / 31); br_text_append(text, "%", 24);
    }
    else if (br_pitch_depth_control(k)) br_pitch_depth_text(text, c->value);
    else if (k == 1u && ((br_ui.page == BR_PAGE_LFO && !br_sound_mod_page()) ||
                         (br_ui.page == BR_PAGE_FX && page == BR_FX_MOD)))   /* LFO RATE, MOD RATE: Hz Q20 */
        br_rate_text(text, br_ui.page == BR_PAGE_LFO ? br_lfo_rate_hz_q20((uint32_t)c->value << 8) :
                     (uint32_t)((uint64_t)br_mod_rate_inc((uint32_t)c->value) * 11025u >> 10));
    else if (br_ui.page == BR_PAGE_FX && page == BR_FX_MOD && k == 2u &&   /* CHORUS / VIBRATO DEPTH: cents */
             (br_part[br_ui.part].fx.mod_engine == BR_MOD_CHORUS || br_part[br_ui.part].fx.mod_engine == BR_MOD_VIBRATO)) {
        br_number(text, (int32_t)br_mod_depth_cents(br_part[br_ui.part].fx.mod_engine, (uint32_t)c->value,
                                                    br_part[br_ui.part].fx.mod_rate));
        br_text_append(text, "CT", 24);
    }
    else if (br_ui.page == BR_PAGE_SCALE && k == 3u) {
        br_number(text, c->value); br_text_append(text, " ST", 24);
    } else if ((br_ui.page == BR_PAGE_MIX && ((page == 2u && !k) || page == 7u || page == 8u)) ||
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

static void br_fx_icon(int32_t x, int32_t y, uint32_t k, uint16_t color);   /* defined below; D02 SEND draws it */
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
    if (br_ui.page == BR_PAGE_LFO && k == 2u && page < BR_LFOS) return m->lfo[page].target;
    if (br_ui.page == BR_PAGE_ENV && page == 2u && k == 1u) return m->target;   /* ENV/SETUP TARGET (D36) */
    if (br_ui.page == BR_PAGE_ROUTE && k == 1u && !br_gate_page() && !br_sound_mod_page())
        return br_part[br_ui.part].mod.route[BR_ROUTES + br_patch_slot()].target;
    return BR_TARGET_COUNT;
}

/* Patch sources use the same artwork as the controls that produce them. */
static uint32_t br_source_control_id(uint32_t k)
{
    if (br_ui.page != BR_PAGE_ROUTE || k != 0u || br_gate_page() || br_sound_mod_page())
        return BR_SOURCE_COUNT;
    uint32_t source = br_part[br_ui.part].mod.route[BR_ROUTES + br_patch_slot()].source;
    return br_source_ui_value(br_source_ui_ordinal(source));
}

static uint32_t br_source_widget(uint32_t source)
{
    switch (source) {
    case BR_SOURCE_OFF: return BR_WIDGET_TOGGLE;
    case BR_SOURCE_OSC: case BR_SOURCE_LFO1: case BR_SOURCE_LFO2: case BR_SOURCE_GLOBAL_LFO:
    case BR_SOURCE_RANDOM: case BR_SOURCE_NOTE_RANDOM:
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
    case BR_TARGET_KIT_GATE: return BR_WIDGET_GATE;
    case BR_TARGET_PAN: return BR_WIDGET_TRACK_PAN;
    case BR_TARGET_DIST_SEND: return BR_WIDGET_SEND;
    case BR_TARGET_ARP_MODE: return BR_WIDGET_ARP_MODE;
    case BR_TARGET_ARP_RATE: return BR_WIDGET_ARP_RATE;
    case BR_TARGET_ARP_OCTAVES: return BR_WIDGET_OCTAVES;
    case BR_TARGET_ARP_GATE: return BR_WIDGET_ARP_GATE;
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
    case BR_TARGET_KIT_GATE: return s->gate;
    case BR_TARGET_PAN: return s->pan;
    case BR_TARGET_DIST_SEND: return s->drive;
    case BR_TARGET_ARP_MODE: return br_part[br_ui.part].arp_mode * 127u / (BR_ARP_MODES - 1u);
    case BR_TARGET_ARP_RATE: return br_part[br_ui.part].arp_rate * 127u / (BR_ARP_RATES - 1u);
    case BR_TARGET_ARP_OCTAVES: return (br_part[br_ui.part].arp_octaves - 1u) * 127u / (BR_ARP_MAX_OCTAVES - 1u);
    case BR_TARGET_ARP_GATE: return br_part[br_ui.part].arp_gate;
#if BR_MODEL_MOD_EXPERIMENT
    case BR_TARGET_MODEL: return br_is_plaits(s->preset) ? (s->preset - BR_FIRST_PLAITS) * 127u / (BR_PLAITS_MODEL_COUNT - 1u) : 0u;
#endif
    default: return 0u;
    }
}

/* One sample per display frame, so all bands of the illustration show the
 * same effective global value even while the LFO is moving. */
static uint32_t br_global_target_visual_value(void)
{
    uint32_t target = br_master.global_lfo.target, base = 0u, maximum = 127u;
    switch (target) {
    case BR_GLOBAL_TARGET_FILTER: base = br_master.fx.filter; break;
    case BR_GLOBAL_TARGET_RESONANCE: base = br_master.fx.resonance; break;
    case BR_GLOBAL_TARGET_HEAT_AMOUNT: base = br_master.fx.heat_amount; break;
    case BR_GLOBAL_TARGET_HEAT_TYPE: base = br_master.fx.heat_type; maximum = BR_HEAT_COUNT - 1u; break;
    case BR_GLOBAL_TARGET_COMP_MIX: base = br_master.comp.mix; break;
    case BR_GLOBAL_TARGET_COMP_THRESHOLD: base = br_master.comp.threshold; maximum = 48u; break;
    case BR_GLOBAL_TARGET_COMP_MAKEUP: base = br_master.comp.makeup; maximum = 24u; break;
    case BR_GLOBAL_TARGET_COMP_ATTACK: base = br_master.comp.attack; break;
    case BR_GLOBAL_TARGET_COMP_RELEASE: base = br_master.comp.release; break;
    case BR_GLOBAL_TARGET_COMP_HOLD: base = br_master.comp.hold; break;
    case BR_GLOBAL_TARGET_REVERB_SIZE: base = br_master.reverb_size; break;
    case BR_GLOBAL_TARGET_REVERB_DECAY: base = br_master.reverb_decay; break;
    default: return 0u;
    }
    if (base > maximum) base = maximum;
    return br_audio_global_lfo_target_value(target, base, maximum) * 127u / maximum;
}

/* Parameter graphics: the mapping halves of the batch files, then the
 * combiners the call sites use. Batches are asked in order a, b, c, d; the
 * first answer wins, and with none the existing mapping below applies. */
#define BR_PARAM_HOOKS 1
#include "instrument_widgets_params_a.c"
#include "instrument_widgets_params_b.c"
#include "instrument_widgets_params_c.c"
#include "instrument_widgets_params_d.c"
#undef BR_PARAM_HOOKS
static uint32_t br_param_widget(uint32_t k, const br_control_t *c)
{
    uint32_t kind = br_param_widget_a(k, c);
    if (kind == BR_WIDGET_NONE) kind = br_param_widget_b(k, c);
    if (kind == BR_WIDGET_NONE) kind = br_param_widget_c(k, c);
    if (kind == BR_WIDGET_NONE) kind = br_param_widget_d(k, c);
    return kind;
}
static uint32_t br_param_variant(uint32_t k, const br_control_t *c, uint32_t kind)
{
    uint32_t v = br_param_variant_a(k, c, kind);
    if (v == BR_PARAM_UNHANDLED) v = br_param_variant_b(k, c, kind);
    if (v == BR_PARAM_UNHANDLED) v = br_param_variant_c(k, c, kind);
    if (v == BR_PARAM_UNHANDLED) v = br_param_variant_d(k, c, kind);
    return v;
}
static uint32_t br_param_env(uint32_t k, const br_control_t *c, uint32_t kind, uint32_t env[4])
{
    return br_param_env_a(k, c, kind, env) || br_param_env_b(k, c, kind, env) ||
        br_param_env_c(k, c, kind, env) || br_param_env_d(k, c, kind, env);
}
static uint32_t br_param_animated(uint32_t k, uint32_t kind)
{
    uint32_t a = br_param_animated_a(k, kind);
    if (!a) a = br_param_animated_b(k, kind);
    if (!a) a = br_param_animated_c(k, kind);
    if (!a) a = br_param_animated_d(k, kind);
    return a;
}

static uint32_t br_control_widget(uint32_t k, const br_control_t *c)
{
    uint32_t page = br_ui.screen[br_ui.page];
    uint32_t target = br_target_control_id(k), source = br_source_control_id(k);
    uint32_t mapped = br_param_widget(k, c);
    if (mapped != BR_WIDGET_NONE) return mapped;
    if (target != BR_TARGET_COUNT) return br_target_widget(target);
    if (source != BR_SOURCE_COUNT) return br_source_widget(source);
    if (br_ui.page == BR_PAGE_PATTERN) {
        if (!k) return BR_WIDGET_ARP_MODE;
        if (k == 1u) return BR_WIDGET_ARP_RATE;
    }
    if (br_ui.page == BR_PAGE_MIX && page == 3u) return BR_WIDGET_TRACK_PAN;
    if (br_ui.page == BR_PAGE_MIX && page == 4u && !k) return BR_WIDGET_COMP_SOURCE;
    if (br_ui.page == BR_PAGE_MIX && (page == 7u || page == 8u))
        return page == 7u && k == 3u ? BR_WIDGET_PATCH_OFF : BR_WIDGET_MIDI_CHANNEL;
    if (br_widget_is(c->label, "BPM") || br_widget_is(c->label, "MIDI BPM")) return BR_WIDGET_BPM;
    if (br_sound_mod_page()) return BR_WIDGET_MOD_ATTEN;
    if (br_widget_is(c->label, "OSC MIX")) return BR_WIDGET_MIX;
    if (br_widget_is(c->label, "HARMONICS")) return BR_WIDGET_HARMONICS;
    if (br_widget_is(c->label, "TIMBRE")) return BR_WIDGET_TIMBRE;
    if (br_widget_is(c->label, "COLOR") || br_widget_is(c->label, "MORPH")) return BR_WIDGET_COLOR;
    if (br_ui.page == BR_PAGE_MIX && page == 6u) return !k ? BR_WIDGET_HEAT : BR_WIDGET_AMOUNT;
    if (br_ui.page == BR_PAGE_FX) {
        if (page == BR_FX_DRIVE) return !k ? BR_WIDGET_FX_ENGINE : BR_WIDGET_POT;
        if (page >= BR_FX_DELAY) {
            if (!k) return BR_WIDGET_FX_ENGINE;
            if (page == BR_FX_REVERB && k == 1u) return BR_WIDGET_REVERB;
            if (page == BR_FX_DELAY && k == 1u) return BR_WIDGET_DELAY;
        }
    }
    if (br_ui.page == BR_PAGE_LFO) {
        if (!k) return BR_WIDGET_WAVE;
    }
    if (br_widget_is(c->label, "MIX")) return BR_WIDGET_MIX;
    if (br_widget_is(c->label, "SWING")) return BR_WIDGET_SWING;
    if (br_widget_is(c->label, "GATE")) return BR_WIDGET_GATE;            /* the kit HOME GATE (ARP GATE: batch D) */
    return BR_WIDGET_POT;
}

static uint32_t br_control_widget_variant(uint32_t k, const br_control_t *c,
                                          uint32_t kind)
{
    uint32_t page = br_ui.screen[br_ui.page];
    uint32_t target = br_target_control_id(k), source = br_source_control_id(k);
    uint32_t mapped = br_param_variant(k, c, kind);
    if (mapped != BR_PARAM_UNHANDLED) return mapped;
    if (source == BR_SOURCE_AMP || source == BR_SOURCE_ENV) return 4u;
    if (source == BR_SOURCE_LFO1 || source == BR_SOURCE_LFO2)
        return br_part[br_ui.part].mod.lfo[source - BR_SOURCE_LFO1].wave;
    if (source == BR_SOURCE_GLOBAL_LFO) return br_master.global_lfo.wave;
    if (source == BR_SOURCE_RANDOM || source == BR_SOURCE_NOTE_RANDOM) return BR_WAVE_RANDOM;
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
        return br_fx_delay_choice(br_part[br_ui.part].fx.delay_time, br_part_pending[br_ui.part].delay_short);
    if (target == BR_TARGET_MOD) return (3u << 8) | BR_MOD_CHORUS;
    if (target == BR_TARGET_ARP_MODE) return br_part[br_ui.part].arp_mode;
    if (target == BR_TARGET_ARP_RATE) return br_part[br_ui.part].arp_rate;
    if (kind == BR_WIDGET_ARP_MODE || kind == BR_WIDGET_ARP_RATE) return (uint32_t)c->value;
    if (kind == BR_WIDGET_GATE) return c->value >= BR_GATE_OPEN;     /* the kit HOME GATE only */
    if (kind == BR_WIDGET_WAVE)
        return br_ui.page == BR_PAGE_LFO ? (page == BR_LFOS ? br_master.global_lfo.wave : br_part[br_ui.part].mod.lfo[page].wave) : BR_WAVE_SINE;
    if (kind == BR_WIDGET_MIDI_CHANNEL) return (uint32_t)c->value;
    if (kind == BR_WIDGET_FX_ENGINE && br_ui.page == BR_PAGE_FX && page == BR_FX_DRIVE)
        return (uint32_t)c->value;                       /* family 0: the drive modes */
    if (kind == BR_WIDGET_FX_ENGINE)
        return ((br_fx_effect(page) + 1u) << 8) |
            (page == BR_FX_REVERB ? br_reverb_ui_id[(uint32_t)c->value] : (uint32_t)c->value);
    if (kind == BR_WIDGET_TRACK_PAN) return k;
    if (kind == BR_WIDGET_COMP_SOURCE) return (uint32_t)c->value;
    if (kind == BR_WIDGET_MONITOR) {        /* MONITOR and POLY: the switch state; 2 = MONITOR saw an overrun */
        if (br_ui.page != BR_PAGE_MIX) return c->value ? 3u : 0u;               /* HOME KEY TRACK: off / on (3: in the track's colour), never overrun */
        return k == 3u && c->value && br_mon.overruns ? 2u : (uint32_t)c->value;
    }
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

/* Pictures whose phase moves only marks (BW_MARK): a phase step redraws the marks' boxes (br_anim). */
static uint32_t br_widget_marked(uint32_t kind)
{
    return br_widget_evolving(kind) || kind == BR_WIDGET_ORBIT || kind == BR_WIDGET_LFO_DEPTH || kind == BR_WIDGET_WAVE ||
        kind == BR_WIDGET_BPM;
}

static uint32_t br_widget_phase(uint32_t kind)
{
    /* Motion lives in the display loop; silent controls settle to a stable
     * picture. Eight updates per second limit LCD traffic while playing. */
    if (kind == BR_WIDGET_BPM) return br_playing ? 1u + (br_transport_phase >> 3) + ((br_global_beat & 1u) << 4) : 0u;
    if (br_widget_evolving(kind))
        return br_playing || !br_quiet_part[br_ui.part] ? (fm1_ms / 125u) & 127u : 0u;
    if (kind == BR_WIDGET_WAVE && br_ui.page == BR_PAGE_LFO) return 0u;   /* the LFO page's scope holds still (owner) */
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
enum { BR_PARAM_BODY_Y = 74, BR_PARAM_STANDARD_BODY_Y = 74, BR_PARAM_ICON_Y = 12, BR_PARAM_VALUE_Y = 76 };
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

/* HD chrome surfaces. Every caller draws onto a freshly cleared canvas, so
 * anti-aliased edges are blended exactly once per redraw and per strip. */
/* Raised chip: lighter top, darker bottom, bright top line, dark base line. */
static void hdc_chip(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t face)
{
    hd__rrect(x, y, w, h, r, hd_lighten(face, 40u), hd_darken(face, 44u),
              hd_lighten(face, 120u), hd_darken(face, 120u), 0);
}
/* Chip with a soft gloss over its upper half: white blended in, fading to
 * nothing by mid-height so it has no edge. */
static void hdc_gloss(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t face)
{
    int32_t gx[2] = {(x + 2) * 256, (x + w - 2) * 256}, gy[2] = {(y + 1) * 256, (y + 1) * 256};
    hdc_chip(x, y, w, h, r, face);
    bw_area(gx, gy, 0, (y + h / 2 + 2) * 256, 2u, C_WHITE, 104, 0);
}
/* Badge lettering at scale 2, bold, placed by the glyphs' ink with a fixed
 * 2 px gap and centred in the box, so "T1" sits as tight as "DK". */
static void hdp_badge_text(int32_t x, int32_t w, int32_t y, const char *text)
{
    const uint8_t *bits;
    const james_hd_glyph_t *g;
    const char *t;
    char one[2] = {0, 0};
    int32_t ink = -2;
    for (t = text; *t; ++t) if ((g = hd_text_glyph((uint8_t)*t, 2, &bits))) ink += g->w + 3;
    x += (w - ink) / 2;
    for (t = text; *t; ++t) if ((g = hd_text_glyph((uint8_t)*t, 2, &bits))) {
        one[0] = *t;
        br_widget_text_2x(x - g->x, y, one, C_BLACK);
        br_widget_text_2x(x - g->x + 1, y, one, C_BLACK);
        x += g->w + 3;
    }
}
/* The track badge family: glossy chip, tight bold lettering. */
static void hdp_badge(int32_t x, int32_t y, int32_t w, uint16_t color, const char *text)
{
    hdc_gloss(x, y, w, 20, 5, color);
    hdp_badge_text(x, w, y + 3, text);
}
/* A small globe in dark line work (q8 centre on a pixel centre, radius a
 * whole number of pixels, so the straight lines land on pixel rows and
 * columns and stay crisp): rim, equator, two latitudes, the central meridian
 * and an elliptical meridian either side. */
static void hdp_globe(int32_t cx, int32_t cy, int32_t r)
{
    int32_t px[17], py[17], c = r * 13 / 16, l = (r * 9 / 16) & ~255;
    for (int32_t i = 0; i <= 16; ++i) {
        px[i] = cx + bw_sine((uint32_t)i * 4096u) * (r * 5 / 8) / 16384;
        py[i] = cy - bw_sine((uint32_t)i * 4096u + 16384u) * r / 16384;
    }
    hd_circle_q8(cx, cy, r, 320, C_BLACK);
    bw_path(px, py, 17u, 224u, C_BLACK);
    hd_line_q8(cx, cy - r, cx, cy + r, 256u, C_BLACK);
    hd_line_q8(cx - r, cy, cx + r, cy, 256u, C_BLACK);
    hd_line_q8(cx - c, cy - l, cx + c, cy - l, 224u, C_BLACK);
    hd_line_q8(cx - c, cy + l, cx + c, cy + l, 224u, C_BLACK);
}
static void br_draw_knob_badge(uint32_t k, int32_t center, int32_t y, uint32_t active)
{
    static const char *const labels[4] = {BR_PANEL_KNOB1, BR_PANEL_KNOB2, BR_PANEL_KNOB3, BR_PANEL_KNOB4};
    int32_t width = br_widget_text_width(labels[k]) + 12;
    int32_t x = center - width / 2;
    if (active) hdc_chip(x, y, width, 11, 5, BR_ACC);
    else hd__rrect(x, y, width, 11, 5, RGB(34, 40, 44), RGB(14, 18, 20), RGB(56, 64, 68), RGB(8, 10, 12), 0);
    br_widget_text(x + 6, y + 2, labels[k], active ? C_BLACK : RGB(168, 177, 180));
    return;
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
        if (br_part[part].mod.route[i].source == BR_SOURCE_ENV &&
            !br_target_is_arp(br_part[part].mod.route[i].target) && br_route_valid(part, i))
            return 1u;
    for (uint32_t i = 0u; i < BR_GLOBAL_ROUTES; ++i)
        if (br_part[part].mod.route[BR_ROUTES + i].source == BR_SOURCE_ENV &&
            !br_target_is_arp(br_part[part].mod.route[BR_ROUTES + i].target) && br_route_valid(part, BR_ROUTES + i)) return 1u;
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
        if (br_route_valid(part, BR_ROUTES + i) && br_part[part].mod.route[BR_ROUTES + i].target == target)
            bits |= BR_BADGE_PATCH1 << i;
    return bits;
}
static uint32_t br_send_badges(uint32_t part, uint32_t source)
{
    uint32_t bits = 0u, i;
    for (i = 0; i < 2u; ++i)
        if (br_route_valid(part, BR_ROUTES + i) && br_part[part].mod.route[BR_ROUTES + i].source == source)
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
    } else if (br_ui.page == BR_PAGE_LFO && k == 1u && page < BR_LFOS) {
        target = BR_TARGET_LFO1_RATE + page;              /* the LFO's own RATE input */
    } else if (br_ui.page == BR_PAGE_ENV && !page) {
        static const uint8_t targets[4] = {BR_TARGET_ATTACK, BR_TARGET_DECAY, BR_TARGET_SUSTAIN, BR_TARGET_RELEASE};
        target = targets[k];
    } else if (br_ui.page == BR_PAGE_FX && page == BR_FX_SENDS) {
        static const uint8_t targets[4] = {BR_TARGET_OFF, BR_TARGET_DELAY, BR_TARGET_MOD, BR_TARGET_REVERB};
        target = targets[k];
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
    int32_t width = br_widget_text_width_scaled(name, scale) + 10 * scale;   /* hugs the HD text */
    int32_t x = center - width / 2;
    if (scale > 1) hdc_gloss(x, y, width, 9 * scale, 3 * scale, color);    /* large ones gloss like the header badge */
    else hdc_chip(x, y, width, 9 * scale, 3 * scale, color);
    br_widget_text_scaled(x + 5 * scale, y + scale, name, C_BLACK, scale);
    return;
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
    {   /* A quiet rounded outline and a thin cross centred where the cells' was. */
        int32_t cx = (x + size / 2) * 256, cy = (y + size / 2) * 256;
        hd_rrect_frame(x, y, size, size, size / 6, BR_LINE);
        hd_line_q8(cx - 1536, cy - 1536, cx + 1536, cy + 1536, 320u, RGB(96, 104, 108));
        hd_line_q8(cx - 1536, cy + 1536, cx + 1536, cy - 1536, 320u, RGB(96, 104, 108));
        return;
    }
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

/* A card goes out in nine bands: knob badge, the picture in six of 10 rows, then value, label and route badges in two.
 * A band whose pixels are what the screen already shows there is not sent (br_crc_blit), so a moving
 * picture costs only the rows it changes and a new value text only its own rows. */
#define BR_CARD_BANDS 10u
static const uint8_t br_card_band[BR_CARD_BANDS + 1u] = {0, 12, 22, 32, 42, 52, 62, 72, 86, 103, 134};
/* Moving pictures (the evolving card pictures, the metronome, the playhead): a region keeps its hash
 * without the phase, the phase, the boxes (x0, y0, x1, y1 in the region, inclusive; x0 == 0xff: none) of
 * its marks (BW_MARK) at that phase and the boxes still owed a redraw at that phase. The screen shows the
 * region at `phase` everywhere but in the owed boxes (and in what its full redraw has not sent yet). A
 * phase step owes the old marks' boxes and the new ones; br_anim_paint draws them, each a small canvas of
 * the region's art (br_art, drawn whole and clipped: cv_ox, cv_oy). Cards 0..3, the header, the strip. */
#define BR_DIRTY 3u
typedef struct { uint32_t hash; uint8_t phase, mark[HD_MARKS][4], dirty[BR_DIRTY][4]; } br_anim_t;
static br_anim_t br_anim[6];
static void (*br_art)(void);
/* Owe box b: merged into an owed box it touches, else in a free slot, else merged into the last one. */
static void br_anim_owe(br_anim_t *a, const uint8_t b[4])
{
    uint8_t *d = a->dirty[0];
    if (b[0] == 0xffu) return;
    for (uint32_t i = 0; i < BR_DIRTY; ++i) {
        d = a->dirty[i];
        if (d[0] == 0xffu) { memcpy(d, b, 4u); return; }
        if (b[0] <= d[2] + 1u && d[0] <= b[2] + 1u && b[1] <= d[3] + 1u && d[1] <= b[3] + 1u) break;
    }
    for (uint32_t j = 0; j < 4u; ++j) if ((j < 2u) == (b[j] < d[j])) d[j] = b[j];
}
/* The marks of br_art at this moment: the art drawn into an empty strip (nothing is written) while
 * hd__clip collects the marks' boxes. Every mark lies within its region (test_anim_marks, test_draw_load);
 * a box beyond the w x h region is taken as none. */
static void br_anim_marks(uint8_t box[HD_MARKS][4], int32_t w, int32_t h)
{
    uint32_t saved = cv_h, i, j;
    int32_t oy = cv_oy;
    for (i = 0; i < HD_MARKS; ++i) for (j = 0; j < 4u; ++j) hd_mark.b[i][j] = (j & 2u) ? -0x7fff : 0x7fff;
    hd_mark.n = 0u; cv_h = 0u; cv_oy = 0;
    br_art();
    cv_h = saved; cv_oy = oy;
    for (i = 0; i < HD_MARKS; ++i) {
        int32_t *m = hd_mark.b[i];
        box[i][0] = 0xffu;
        if (m[0] >= 0 && m[1] >= 0 && m[2] < w && m[3] < h && m[0] <= m[2] && m[1] <= m[3])
            for (j = 0; j < 4u; ++j) box[i][j] = (uint8_t)m[j];
    }
}
/* The region (w x h, art br_art) now has hash h and phase `phase`: 0 when a phase step is due and the pass
 * has no room for it. */
static uint32_t br_anim_update(br_anim_t *a, uint32_t hash, uint32_t phase, int32_t w, int32_t h)
{
    uint32_t i;
    if (a->hash != (hash ^ br_pass.epoch)) {          /* a new picture: drawn whole by its region */
        a->hash = hash ^ br_pass.epoch; a->phase = (uint8_t)phase;
        memset(a->dirty, 0xff, sizeof a->dirty);
        br_anim_marks(a->mark, w, h);
    } else if (a->phase != (uint8_t)phase) {
        if (!br_open()) return 0u;
        for (i = 0; i < HD_MARKS; ++i) br_anim_owe(a, a->mark[i]);
        br_anim_marks(a->mark, w, h);
        for (i = 0; i < HD_MARKS; ++i) br_anim_owe(a, a->mark[i]);
        a->phase = (uint8_t)phase;
    }
    return 1u;
}
/* Draw the owed boxes of a region at screen (sx, sy). The `units` sums (crc, br_crc_blit) of what the screen
 * shows there no longer hold. */
static uint32_t br_anim_paint(br_anim_t *a, uint32_t sx, uint32_t sy, uint32_t *crc, uint32_t units)
{
    uint32_t painted = 0u;
    for (uint32_t i = 0; i < BR_DIRTY; ++i) {
        uint8_t *b = a->dirty[i];
        while (b[0] != 0xffu && br_open()) {   /* in strips of about 1,500 px (at least 12 rows), the box shrinking */
            uint32_t rows = b[3] - b[1] + 1u, most = 1536u / (b[2] - b[0] + 1u);
            if (most < 12u) most = 12u;
            if (rows > most) rows = most;
            cv_begin(b[2] - b[0] + 1u, rows, sy == 0u ? br_track_background(br_ui.part) : BR_BACKGROUND);
            cv_oy = -(int32_t)b[1]; cv_ox = b[0];
            br_art();
            cv_oy = 0; cv_ox = 0;
            cv_blit(sx + b[0], sy + b[1]); painted = 1u;
            for (uint32_t j = 0; crc && j < units; ++j) crc[j] ^= 0x80000000u;
            if (b[1] + rows > b[3]) b[0] = 0xffu;
            else b[1] = (uint8_t)(b[1] + rows);
        }
    }
    return painted;
}
/* The card being drawn (br_draw_control) and its art. Rows 11..71 hold only the picture: a canvas within
 * them draws no badge or texts. */
/* What a page's art draws from (set where the page is drawn, on its stack, before the art runs). */
static const void *br_view;
struct br_card_view {
    const br_control_t *c; const char *value; uint32_t k, kind, q, variant, phase, route; uint32_t env[4]; uint16_t label;
};
#define br_cc (*(struct br_card_view *)br_view)
/* Match the 1.5 px stroke used by the existing parameter illustrations. */
static void br_param_icon_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    hd_line_q8(bw_q8(x0), bw_q8(y0), bw_q8(x1), bw_q8(y1), 384u, color);
}
/* VOICE controls use compact diagrams with no surrounding panel, like the
 * other parameter graphics. Each reflects its setting without animation. */
static void br_voice_icon(uint32_t k, const br_control_t *c, uint32_t q)
{
    uint16_t dim = RGB(83, 96, 99), accent = BR_ACC;
    if (k == 0u) { /* a pitch sliding between two note bars */
        int32_t px = 15, py = 52;
        hd_rrect(8, 50, 9, 4, 2, dim); hd_rrect(43, 26, 9, 4, 2, dim);
        for (int32_t i = 1; i <= 12; ++i) {
            int32_t x = 15 + i * 30 / 12, d = x - 15, y = 52 - d * d * 24 / 900;
            br_param_icon_line(px, py, x, y, accent); px = x; py = y;
        }
        int32_t d = (int32_t)(q * 30u / 127u), x = 15 + d, y = 52 - d * d * 24 / 900;
        hd_circle_q8(bw_q8(x), bw_q8(y), 2 * 256, 0, C_WHITE);
    } else if (k == 1u) { /* two keys, connected only for the selected glide mode */
        hd_rrect_frame(10, 32, 16, 22, 3, dim); hd_rrect_frame(34, 23, 16, 22, 3, dim);
        hd_rrect(14, 34, 5, 10, 1, dim); hd_rrect(38, 25, 5, 10, 1, dim);
        if (c->value) {
            br_param_icon_line(24, 46, 36, 33, accent);
            hd_circle_q8(bw_q8(30), bw_q8(39), 2 * 256, 0, C_WHITE);
            if (c->value == 2) { br_param_icon_line(12, 60, 48, 60, accent); br_param_icon_line(45, 57, 48, 60, accent); br_param_icon_line(45, 63, 48, 60, accent); }
        }
    } else if (k == 2u) { /* LPG: a pair of wrought-iron gates, no panel */
        br_param_icon_line(10, 59, 10, 22, dim); br_param_icon_line(50, 59, 50, 22, dim);
        hd_circle_q8(bw_q8(10), bw_q8(20), 2 * 256, 0, dim);
        hd_circle_q8(bw_q8(50), bw_q8(20), 2 * 256, 0, dim);
        br_param_icon_line(11, 27, 17, 23, accent); br_param_icon_line(17, 23, 23, 20, accent);
        br_param_icon_line(23, 20, 30, 19, accent); br_param_icon_line(30, 19, 37, 20, accent);
        br_param_icon_line(37, 20, 43, 23, accent); br_param_icon_line(43, 23, 49, 27, accent);
        br_param_icon_line(11, 55, 49, 55, accent); br_param_icon_line(11, 43, 49, 43, accent);
        for (int32_t x = 16; x <= 44; x += 7) {
            int32_t top = 20 + (x < 30 ? 30 - x : x - 30) / 2;
            br_param_icon_line(x, top, x, 55, accent);
        }
        br_param_icon_line(30, 19, 30, 55, C_WHITE);
        hd_circle_q8(bw_q8(32), bw_q8(41), 384, 0, C_WHITE);
    } else { /* a clock beside the note gate; pulse width is the chosen duration */
        int32_t end = 37 + (int32_t)(q * 13u / 127u);
        hd_circle_q8(bw_q8(19), bw_q8(40), 10 * 256, 384, dim);
        br_param_icon_line(19, 40, 19, 33, C_WHITE); br_param_icon_line(19, 40, 24, 43, accent);
        br_param_icon_line(32, 50, 35, 50, dim); br_param_icon_line(35, 50, 35, 30, accent);
        br_param_icon_line(35, 30, end, 30, accent); br_param_icon_line(end, 30, end, 50, accent);
        br_param_icon_line(end, 50, 53, 50, dim);
    }
}
/* Reuse each global parameter's established artwork with its effective
 * modulated value. MODEL has its own wave-bank drawing. */
static void br_lfo_target_icon(uint32_t target, uint32_t global, uint32_t q)
{
    if (!global) {
        uint32_t model = (q * (BR_PLAITS_MODEL_COUNT - 1u) + 63u) / 127u;
        for (uint32_t i = 0; i < BR_PLAITS_MODEL_COUNT; ++i) {
            int32_t x = 9 + (int32_t)(i % 4u) * 11, y = 19 + (int32_t)(i / 4u) * 8;
            uint16_t color = i == model ? BR_ACC : RGB(83, 96, 99);
            br_param_icon_line(x, y + 5, x + 3, y + 1 + (int32_t)(i % 3u), color);
            br_param_icon_line(x + 3, y + 1 + (int32_t)(i % 3u), x + 7, y + 5, color);
            if (i == model) hd_circle_q8(bw_q8(x + 3), bw_q8(y + 1 + (int32_t)(i % 3u)), 256, 0, C_WHITE);
        }
        return;
    }
    uint32_t kind, variant = 0u;
    switch (target) {
    case BR_GLOBAL_TARGET_HEAT_TYPE:
        kind = BR_WIDGET_HEAT; variant = (q * 2u + 63u) / 127u; break;
    case BR_GLOBAL_TARGET_HEAT_AMOUNT:
        kind = BR_WIDGET_HEAT_AMOUNT; variant = br_badge_global_aux & 3u; break;
    case BR_GLOBAL_TARGET_COMP_MIX: kind = BR_WIDGET_MIX; variant = BR_BLEND_OSC; break;
    case BR_GLOBAL_TARGET_COMP_THRESHOLD: kind = BR_WIDGET_COMP_CURVE; variant = 1u; break;
    case BR_GLOBAL_TARGET_COMP_MAKEUP: kind = BR_WIDGET_COMP_CURVE; variant = 3u; break;
    case BR_GLOBAL_TARGET_COMP_ATTACK: case BR_GLOBAL_TARGET_COMP_HOLD: case BR_GLOBAL_TARGET_COMP_RELEASE: {
        uint32_t a = (br_badge_global_aux >> 2) & 127u, h = (br_badge_global_aux >> 9) & 127u;
        uint32_t r = (br_badge_global_aux >> 16) & 127u;
        uint32_t which = target == BR_GLOBAL_TARGET_COMP_ATTACK ? 0u : target == BR_GLOBAL_TARGET_COMP_HOLD ? 1u : 2u;
        if (!which) a = q; else if (which == 1u) h = q; else r = q;
        kind = BR_WIDGET_COMP_TIME; variant = which | a << 2 | h << 9 | r << 16; break;
    }
    case BR_GLOBAL_TARGET_REVERB_SIZE: kind = BR_WIDGET_REVERB; break;
    case BR_GLOBAL_TARGET_REVERB_DECAY: kind = BR_WIDGET_TAIL; break;
    default: kind = BR_WIDGET_PATCH_OFF; break;
    }
    br_widget_draw(kind, 0, BR_PARAM_ICON_Y, 60u, q, variant, 0, 0u);
}
/* The same FILTER / RESONANCE graphic is used on track and global LFO pages;
 * a small marker moves with the displayed value. */
static void br_lfo_filter_icon(uint32_t resonance, uint32_t q)
{
    br_widget_draw(resonance ? BR_WIDGET_RESONANCE_TARGET : BR_WIDGET_FILTER_TARGET,
                   0, BR_PARAM_ICON_Y, 60, q, 0, 0, 0);
    hd_circle_q8(bw_q8(10 + (int32_t)(q * 40u / 127u)), bw_q8(64), 2 * 256, 0, C_WHITE);
}
static void br_card_art(void)
{
    int32_t top = -cv_oy;
    if (!br_cc.c->label[0]) { br_draw_empty_param(10, 22, 40); return; }
    if (br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] == BR_LFOS && br_cc.k == 2u) {
        uint32_t target = br_global_target_id[br_global_target_ordinal(br_master.global_lfo.target)];
        if (target == BR_GLOBAL_TARGET_FILTER || target == BR_GLOBAL_TARGET_RESONANCE)
            br_lfo_filter_icon(target == BR_GLOBAL_TARGET_RESONANCE, br_cc.q);
        else br_lfo_target_icon(target, 1u, br_cc.q);
    }
    else if (br_ui.page == BR_PAGE_LFO && br_cc.k == 2u &&
             br_target_ui_available(br_target_control_id(br_cc.k)) &&
             (br_target_control_id(br_cc.k) == BR_TARGET_FILTER || br_target_control_id(br_cc.k) == BR_TARGET_RESONANCE))
        br_lfo_filter_icon(br_target_control_id(br_cc.k) == BR_TARGET_RESONANCE, br_cc.q);
#if BR_MODEL_MOD_EXPERIMENT
    else if (br_target_control_id(br_cc.k) == BR_TARGET_MODEL && br_target_ui_available(BR_TARGET_MODEL))
        br_lfo_target_icon(BR_TARGET_MODEL, 0u, br_cc.q);
#endif
    else if (br_home_voice_page() && br_cc.k == 2u && !br_cc.c->value)
        br_widget_draw(BR_WIDGET_ADSR, 0, BR_PARAM_ICON_Y, 60, 96u, 4u, br_cc.env, 0u);
    else if (br_home_voice_page()) br_voice_icon(br_cc.k, br_cc.c, br_cc.q);
    else br_widget_draw(br_cc.kind, 0, BR_PARAM_ICON_Y, 60, br_cc.q, br_cc.variant, br_cc.env, br_cc.phase);
    if (cv_h && (top < 11 || top + (int32_t)cv_h > 72)) {
        uint32_t route_badges = br_cc.route;
        br_draw_knob_badge(br_cc.k, 30, 0, br_cc.k == br_active_knob());
        int32_t label_y = br_draw_param_text(30, BR_PARAM_VALUE_Y, br_cc.value, br_cc.c->label, br_cc.label);
        int32_t badge_y = label_y + 10;
        int32_t max_badge_y = 135 - (int32_t)br_badge_count(route_badges) * 10;
        if (route_badges && badge_y > max_badge_y) badge_y = max_badge_y;
        br_draw_route_badges(30 + (badge_y + (int32_t)br_badge_count(route_badges) * 10 > 126 ? (br_cc.k == 1u ? -8 : br_cc.k == 2u ? 8 : 0) : 0), badge_y, route_badges, br_ui.part, BR_BACKGROUND);
    }
}
/* The bottom card band crosses the carousel: composite its dots into each
 * band so sliced LCD updates never briefly erase them. */
static void br_card_dots(uint32_t k)
{
    const uint8_t *ring = 0;
    uint32_t n = br_nav_ring(&ring), count = n ? n : br_screen_count();
    uint32_t at = n ? br_nav_at(ring, n) : br_ui.screen[br_ui.page];
    if (count < 2u) return;
    for (uint32_t i = 0; i < count; ++i) {
        int32_t x = 120 - (int32_t)(count * 8u - 4u) / 2 + (int32_t)i * 8 - (int32_t)k * 60;
        int32_t cy = (200 - BR_PARAM_STANDARD_BODY_Y + 3) * 256;
        if (i == at) {
            hd__radial((x + 2) * 256, cy, 1024, -1, 0u, 0u, hd_mix(BR_BACKGROUND, br_ui_accent, 110u));
            hd_circle_q8((x + 2) * 256, cy, 608, 0, br_ui_accent);
            hd_circle_q8((x + 2) * 256 - 160, cy - 192, 192, 0, hd_lighten(br_ui_accent, 140u));
        } else hd_circle_q8((x + 2) * 256, cy, 512, 0, RGB(58, 66, 70));
    }
}
static uint32_t br_draw_control(uint32_t k, const br_control_t *c, uint32_t kind,
                                const char *value, uint32_t phase, uint32_t route_badges,
                                uint32_t *old, uint8_t *next, uint32_t h, uint32_t which)
{
    struct br_card_view view;
    uint32_t offset, q, variant = k, marked, *env = view.env;
    uint32_t screen = br_ui.screen[br_ui.page];
    uint16_t previous_accent = br_ui_accent;
    int32_t range = c->high - c->low;
    const volatile br_sound_t *s = &br_part[br_ui.part].sound;
    const volatile br_mod_config_t *m = &br_part[br_ui.part].mod;
    br_view = &view; br_cc.c = c; br_art = br_card_art; marked = 0u;
    if (!c->label[0]) goto bands;                  /* an unused knob: br_card_art draws the empty slot */
    q = range > 0 ? (uint32_t)(br_clip(c->value - c->low, 0, range) * 127 / range) : 0u;
    if (br_target_control_id(k) != BR_TARGET_COUNT)
        q = br_target_widget_value(br_target_control_id(k));
#if BR_MODEL_MOD_EXPERIMENT
    if (br_ui.page == BR_PAGE_LFO && screen < BR_LFOS && k == 2u &&
        br_target_control_id(k) == BR_TARGET_MODEL && br_target_ui_available(BR_TARGET_MODEL))
        q = br_badge_model_target * 127u / (BR_PLAITS_MODEL_COUNT - 1u);
#endif
    if (br_ui.page == BR_PAGE_LFO && screen == BR_LFOS && k == 2u)
        q = br_badge_global_lfo_value;
    if (br_source_control_id(k) != BR_SOURCE_COUNT)
        q = br_source_control_id(k) == BR_SOURCE_OFF ? 0u : 96u;
    variant = br_control_widget_variant(k, c, kind);
    if (br_ui.page == BR_PAGE_MIX && screen == 2u && !k && br_midi_clock_active) br_ui_accent = BR_DIM;
#if BR_POLY_UI
    if (br_ui.page == BR_PAGE_PUNCH && k == 1u && br_fx_poly_loan_active()) br_ui_accent = BR_DIM;
#endif
    if ((br_ui.page == BR_PAGE_ENV && screen == 1u) || br_source_control_id(k) == BR_SOURCE_ENV) {
        env[0] = m->attack; env[1] = m->decay; env[2] = m->sustain; env[3] = m->release;
    } else {
        env[0] = s->attack; env[1] = s->decay; env[2] = s->sustain; env[3] = s->release;
    }
    br_param_env(k, c, kind, env);                 /* companion values for a parameter graphic */
    if (br_ui.page == BR_PAGE_LFO && br_target_control_id(k) != BR_TARGET_COUNT && kind == BR_WIDGET_ADSR)
        env[2] = 72u; /* Keep the envelope stages distinct in the target illustration. */
    if (kind == BR_WIDGET_WAVE) {
        if (br_ui.page == BR_PAGE_LFO && !k) q = 96u;
        if (br_source_control_id(k) == BR_SOURCE_LFO1 || br_source_control_id(k) == BR_SOURCE_LFO2)
            phase = br_badge_lfo_phase[br_ui.part][br_source_control_id(k) - BR_SOURCE_LFO1];
        else if (br_source_control_id(k) == BR_SOURCE_GLOBAL_LFO) phase = br_badge_global_lfo_phase;
    }
#if BR_POLY_UI
    uint16_t label_color = br_ui.page == BR_PAGE_PUNCH && br_punch_effect == k + 1u ? BR_ACC : BR_DIM;  /* a refused echo is not lit */
#else
    uint16_t label_color = br_ui.page == BR_PAGE_PUNCH && br_punch_ks.active == k + 1u ? BR_ACC : BR_DIM;
#endif
    /* The card's hash (br_draw_body) gains everything the picture is drawn from: a knob step can move the
     * picture while its rounded text stays. An evolving picture's phase is not in it (br_anim). */
    marked = br_widget_marked(kind);
    h = ((((h * 131u + q) * 31u + variant) * 31u + (marked ? 0u : phase)) * 65599u + br_ui_accent) * 65599u + label_color;
    if (br_ui.page == BR_PAGE_LFO && screen == BR_LFOS && k == 2u)
        h = h * 131u + br_badge_global_aux;
    for (uint32_t i = 0; i < 4u; ++i) h = h * 131u + env[i];
    br_cc.value = value; br_cc.k = k; br_cc.kind = kind; br_cc.q = q; br_cc.variant = variant;
    br_cc.phase = phase; br_cc.route = route_badges; br_cc.label = label_color;
bands:
    /* Two parts with their own band rounds (old[which], next[which]): 0 the badge, value, label and route
     * badges (bands 7, 8, 9, 0: the value's rows a band of their own), 1 the picture (bands 1..6; a moving picture's boxes after them). br_draw_body draws
     * part 0 of every card before any part 1, so a page's values show first. */
    if (which == 0u || !marked || br_anim_update(&br_anim[k], h, phase, 60, 134)) {
        uint32_t band, n = which ? 6u : 4u;
        while ((band = br_roll(old + which, next + which, h, n)) < n) {
            band = which ? band + 1u : band == 3u ? 0u : band + 7u;
            offset = br_card_band[band];
            cv_begin(60, br_card_band[band + 1u] - offset, BR_BACKGROUND);
            cv_oy = -(int32_t)offset;
            br_card_art();
            if (band == 9u) br_card_dots(k);
            cv_oy = 0;
            br_crc_blit(k * 60u, BR_PARAM_STANDARD_BODY_Y + offset, &br_band_crc[k * BR_CARD_BANDS + band]);
        }
        if (which && marked && (next[1] & 15u) >= 6u)
            br_anim_paint(&br_anim[k], k * 60u, BR_PARAM_STANDARD_BODY_Y, &br_band_crc[k * BR_CARD_BANDS], BR_CARD_BANDS);
    }
    br_ui_accent = previous_accent;
    {   /* bit 0: this part is all out; bit 1: the other part was, as of its last round, and has no box owed */
        uint32_t other = 1u - which, done = 0u;
        if ((next[which] & 15u) >= (which ? 6u : 4u) && !br_pass.more) done = 1u;
        if (old[other] == (h ^ br_pass.epoch) && (next[other] & 15u) >= (other ? 6u : 4u)) {
            done |= 2u;
            if (other && marked) {                 /* (and the picture's phase is the one shown, no box owed) */
                if (br_anim[k].hash != (h ^ br_pass.epoch) || br_anim[k].phase != (uint8_t)phase) done &= 1u;
                for (uint32_t i = 0; i < BR_DIRTY; ++i) if (br_anim[k].dirty[i][0] != 0xffu) done &= 1u;
            }
        }
        return done;
    }
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
    case BR_PAGE_HOME: return br_sound_mod_page() ? "MOD DEPTHS" : br_home_voice_page() ? "VOICE" : br_sound_name(br_part[br_ui.part].sound.preset);
    case BR_PAGE_ENV: return screen==0u ? "ENV / AMP" : screen==1u ? "ENV / MOD" : "ENV / MOD SET";
    case BR_PAGE_FX: {
        static const char *const titles[]={"FX / SENDS","FX / DRIVE","FX / DELAY","FX / MOD","FX / REVERB"};
        return screen<5u?titles[screen]:"FX";
    }
    case BR_PAGE_MIX: {
        static const char *const titles[]={"MIXER","GLOBAL / FILTER","TEMPO","GLOBAL / PAN","COMPRESSOR","COMP TIME","GLOBAL / HEAT"};
#if !BR_CAP_DIN_MIDI
        return screen<7u?titles[screen]:screen==7u?"USB MIDI 1":screen==8u?"USB MIDI 2":"";
#else
        return screen<7u?titles[screen]:screen==7u?"MIDI 1":screen==8u?"MIDI 2":"";
#endif
    }
    case BR_PAGE_SEQ: return "SEQUENCER";
#ifdef BR_PATTERN_SONG_EXPERIMENT
    case BR_PAGE_PS: return BR_ARR_UI ? (br_ps_state.mode == FM1_SONG_MODE_SONG ? "ARRANGER" : "PATTERNS") : "PATTERNS";
#endif
    case BR_PAGE_PATTERN: return br_retrig_page() ? "RETRIGGER" : "ARP";
    case BR_PAGE_SCALE: return br_ui.part == BR_DRUM_PART ? "DRUM KITS" : "SOUNDS";
    case BR_PAGE_PUNCH: return "PUNCH FX";
    case BR_PAGE_LFO: return screen == 2u ? "GLOBAL LFO" : screen ? "LFO 2" : "LFO 1";
    case BR_PAGE_ROUTE:
        if(br_gate_page())return "KIT LEVEL";
        if(br_sound_mod_page())return "MOD DEPTHS";
        return screen==0u?"PATCH 1":"PATCH 2";
    default: return "";
    }
}
/* At most ten characters in the 2x widget font, then fitted to the pixels
 * left before the metronome. Keep the kit variants distinct; truncate other
 * long names only at the end. */
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
/* The Plaits model chip (20 px, the track badge's height) at x 40 in the header: the model's
 * vector icon stroked in white on its group colour. */
static void br_draw_plaits_icon(uint32_t preset)
{
    uint32_t model = preset - BR_FIRST_PLAITS;
    uint32_t icon = model < 8u ? model : model - 8u;
    uint16_t color = model < 8u ? RGB(210, 125, 35) :
                     model < 16u ? RGB(0, 145, 145) : RGB(205, 48, 78);
    const uint8_t *v = br_plaits_vec;
    int32_t px[12], py[12], x = 40 * 256 + 128;
    hdc_chip(40, 2, 20, 20, 4, color);
    while (1) {   /* Walk the vector list to this icon's items. */
        uint32_t op = *v++, n;
        if (!op) { if (!icon--) return; continue; }
        if (op < 32u) {
            for (n = 0; n < op; ++n, v += 2) { px[n] = x + v[0] * 128; py[n] = 2 * 256 + 128 + v[1] * 128; }
            if (!icon) bw_path(px, py, op, 352u, C_WHITE);
        } else {
            if (!icon) hd_circle_q8(x + v[0] * 128, 2 * 256 + 128 + v[1] * 128, (int32_t)(op & 63u) * 128,
                                    op & 0x80u ? 0 : 352, C_WHITE);
            v += 2;
        }
    }
}
/* A small prefix ("FX /") then the large title 7 px after it; on HOME screen 0 a Plaits title
 * follows the model chip and a 6 px gap instead. Every title ends by x 190, clear of the
 * metronome: characters go from the end until the measured width fits. */
static uint8_t br_header_chip;   /* set by br_draw_body for one call; other headers have none */
static void br_header_draw_title(const char *name)
{
    static const char *const prefixes[3] = {"GLOBAL /", "FX /", "ENV /"};
    char title[BR_HEADER_TITLE_MAX + 1u];
    int32_t x = 40;
    uint32_t n;
    for (n = 0u; n < 3u; ++n)
        if (br_header_has_prefix(name, prefixes[n])) {
            br_widget_text(40, 9, prefixes[n], C_WHITE);
            x += br_widget_text_width(prefixes[n]) + 7;
            while (*name++ != '/') {}
            ++name;                                  /* the space after the slash */
            break;
        }
    if (br_header_chip) { br_draw_plaits_icon(br_part[br_ui.part].sound.preset); x = 66; br_header_chip = 0u; }
    br_header_short_title(name, title);
    n = BR_HEADER_TITLE_MAX;
    while (x + br_widget_text_width_scaled(title, 2) > 190) title[--n] = 0;
    br_widget_text_2x(x, 5, title, C_WHITE);
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
    hdp_badge(x, y, w, color, text);
    return;
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
    hdc_gloss(x, y, 30, 20, 5, BR_GLOBAL_ACC);
    hdp_globe(bw_q8(x + 15), bw_q8(y + 10), 7 * 256);
    return;
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
    {   /* Smooth body, plinth and a sub-pixel arm that follows the swing. */
        static const int32_t px[4] = {2, 6, 9, 13}, py[4] = {17, 2, 2, 17};
        int32_t bx[4], by[4], i;
        uint16_t ink = br_playing ? C_WHITE : BR_DIM;
        int32_t tx = (x + 7) * 256 + 128 + swing * 40, ox = (x + 7) * 256 + 128;
        for (i = 0; i < 4; ++i) { bx[i] = x + px[i]; by[i] = py[i]; }
        if (cv_h) {                         /* (an empty strip only asks where the arm is: br_anim_marks) */
            hd_poly4(bx, by, hd_darken(BR_GLOBAL_ACC, 150u));
            for (i = 0; i < 3; ++i)
                hd_line_q8(bx[i] * 256 + 128, by[i] * 256 + 128, bx[i + 1] * 256 + 128, by[i + 1] * 256 + 128, 352u, BR_GLOBAL_ACC);
            hd_rrect_grad(x + 1, 16, 14, 4, 1, hd_lighten(BR_GLOBAL_ACC, 60u), hd_darken(BR_GLOBAL_ACC, 60u));
        }
        BW_MARK(hd_line_q8(ox, 14 * 256, tx, 2 * 256, 320u, ink);           /* the arm swings: a mark */
                hd_circle_q8(ox + (tx - ox) * 3 / 4, 5 * 256, 400, 0, ink));
        return;
    }
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
    hd_rrect(234, 3, 3, 4, 1, outline);
    hd__rrect(216, 0, 18, 10, 3, hd_lighten(outline, 40u), outline, hd_lighten(outline, 80u), hd_darken(outline, 60u), 0);
    hd_rrect(217, 1, 16, 8, 2, C_BLACK);
    if (level < 0) { hd_rrect(221, 4, 8, 2, 1, BR_DIM); return; }
    bars = level > 3 ? 3u : (uint32_t)level;
    for (i = 0; i < bars; ++i)
        hd__rrect(218 + (int32_t)i * 5, 2, 4, 6, 1, hd_lighten(fill, 70u), hd_darken(fill, 50u),
                  hd_lighten(fill, 120u), hd_darken(fill, 80u), 0);
    return;
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

/* A small raised key cap: darker lip below a beveled face with a top highlight. */
static void hdc_key(int32_t x, int32_t y, int32_t w, int32_t h)
{
    hd_rrect(x, y, w, h, 3, RGB(14, 17, 19));
    hd__rrect(x, y, w, h - 2, 3, RGB(74, 84, 88), RGB(40, 47, 50), RGB(120, 130, 134), RGB(34, 40, 42), 0);
}
static void hdc_key_text(int32_t x, int32_t y, const char *text, uint16_t ink)
{
    br_widget_text(x + 1, y + 1, text, RGB(8, 11, 12));
    br_widget_text(x, y, text, ink);
}
/* One physical octave key, shared by all shortcut and status cues. */
#define BR_OCT_KEY_W 30
#define BR_OCT_PAIR_W (br_widget_text_width("OCT-/+") + 6)
static void br_oct_key(int32_t x, int32_t y, const char *label)
{
    hdc_key(x, y, BR_OCT_KEY_W, 16);
    hdc_key_text(x + (BR_OCT_KEY_W - br_widget_text_width(label)) / 2, y + 4, label, C_WHITE);
}
static void br_oct_pair(int32_t x, int32_t y)
{
    int32_t w = BR_OCT_PAIR_W;
    hdc_key(x, y, w, 16);
    hdc_key_text(x + (w - br_widget_text_width("OCT-/+")) / 2, y + 4, "OCT-/+", C_WHITE);
}
static void br_draw_oct_cue(const char *text, int32_t y, uint32_t compact, uint16_t ink)
{
    const char *rest = text + 7;
    int32_t w, x;
    (void)compact;
    while (*rest == ' ' || *rest == ':') ++rest;
    w = BR_OCT_PAIR_W + (*rest ? 5 + br_widget_text_width(rest) : 0);
    x = (240 - w) / 2;
    br_oct_pair(x, y + 4);
    if (*rest) br_widget_text(x + BR_OCT_PAIR_W + 5, y + 8, rest, ink);
}

static void br_draw_shift_cue(int32_t y)
{
    int32_t plus_w = br_widget_text_width("+");
    int32_t x = (240 - (BR_OCT_PAIR_W + 5 + plus_w + 5 + 22 + 5 + br_widget_text_width("SHIFT NOTES"))) / 2;
    br_oct_pair(x, y + 4); x += BR_OCT_PAIR_W + 5;
    br_widget_text(x, y + 8, "+", C_WHITE); x += plus_w + 5;
    hdc_key(x, y + 1, 22, 22);
    hdc_key_text(x + (22 - br_widget_text_width("REC")) / 2, y + 8, "REC", RGB(240, 72, 64));
    br_widget_text(x + 27, y + 8, "SHIFT NOTES", C_WHITE);
}

#if BR_POLY_UI
static uint32_t br_is_poly_cue(const char *text)
{
    const char *prefix = "OCT- + PLAY: ";
    while (*prefix && *text == *prefix) { ++text; ++prefix; }
    return !*prefix;
}
/* HOME: shortcut buttons, including the PLAY/STOP key cap. */
static void br_draw_poly_cue(const char *text, uint16_t ink)
{
    const char *label = text + sizeof("OCT- + PLAY: ") - 1u;
    int32_t oct_w = BR_OCT_KEY_W;
    int32_t plus_w = br_widget_text_width("+");
    int32_t play_w = 22;
    int32_t x = (240 - (oct_w + 5 + plus_w + 5 + play_w + 5 + br_widget_text_width(label))) / 2;
    br_oct_key(x, 4, "OCT-"); x += oct_w + 5;
    br_widget_text(x, 8, "+", ink); x += plus_w + 5;
    hdc_key(x, 1, play_w, 22);
    {
        int32_t play_x = x + (play_w - br_widget_text_width("PLA")) / 2;
        int32_t stop_x = x + (play_w - br_widget_text_width("STP")) / 2;
        hdc_key_text(play_x, 3, "PLA", RGB(93, 220, 139));
        cv_rect(x + 3, 11, play_w - 6, 1, RGB(140, 145, 147));
        hdc_key_text(stop_x, 13, "STP", RGB(93, 220, 139));
    }
    br_widget_text(x + play_w + 5, 8, label, ink);
}
#endif

/* The kit navigation messages reference the HOME and SEQ buttons explicitly. */
static uint32_t br_draw_kit_button_message(const char *text, int32_t y, uint32_t compact)
{
    uint32_t h = br_hash_text(text);
    uint32_t both = h == br_hash_text("KIT: HOME OR SEQ");
    int32_t x, by = y + (compact ? 0 : 4), ty = y + (compact ? 1 : 8);
    int32_t bh = compact ? 9 : 16;
    if (!both && h != br_hash_text("KIT: HOME PAGE 2")) return 0u;
    x = (240 - (br_widget_text_width("KIT:") + 5 + br_widget_text_width(BR_PANEL_HOME) + 6 + 5 +
        br_widget_text_width(both ? "OR" : "PAGE 2") + (both ? 5 + br_widget_text_width("SEQ") + 6 : 0))) / 2;
    br_widget_text(x, ty, "KIT:", BR_GLOBAL_ACC); x += br_widget_text_width("KIT:") + 5;
    hdc_key(x, by, br_widget_text_width(BR_PANEL_HOME) + 6, bh);
    hdc_key_text(x + 3, ty, BR_PANEL_HOME, C_WHITE); x += br_widget_text_width(BR_PANEL_HOME) + 11;
    br_widget_text(x, ty, both ? "OR" : "PAGE 2", BR_GLOBAL_ACC);
    if (both) {
        x += br_widget_text_width("OR") + 5;
        hdc_key(x, by, br_widget_text_width("SEQ") + 6, bh);
        hdc_key_text(x + 3, ty, "SEQ", C_WHITE);
    }
    return 1u;
}

/* A small dial (ring and pointer) in the 11x11 box of br_knob_icon at (x, y). */
static void hdc_dial(int32_t x, int32_t y, uint16_t color)
{
    int32_t cx = x * 256 + 1408, cy = y * 256 + 1408;
    hd_circle_q8(cx, cy, 1152, 352, color);
    hd_line_q8(cx, cy - 1408 + 640, cx, cy, 352u, color);
}
/* Shared 24-pixel cue bar: two centered groups, with brief vertical slides
 * between cues and contextual messages. Each movement takes 160 ms. */
/* A source level 0-127 in dB, 20 log10(v / 127), without a log routine: below this, levels 41-127
 * hold tenths of a dB down (one decimal, above -10 dB), levels 1-40 whole dB down. The caller
 * passes br_level_law(knob), so the read-out is 40 log10(knob / 127) as the engine plays it. */
static const uint8_t br_level_db_down[128] = {
    0, 42, 36, 33, 30, 28, 27, 25, 24, 23, 22, 21, 20, 20, 19, 19,
    18, 17, 17, 17, 16, 16, 15, 15, 14, 14, 14, 13, 13, 13, 13, 12,
    12, 12, 11, 11, 11, 11, 10, 10, 10, 98, 96, 94, 92, 90, 88, 86,
    85, 83, 81, 79, 78, 76, 74, 73, 71, 70, 68, 67, 65, 64, 62, 61,
    60, 58, 57, 56, 54, 53, 52, 51, 49, 48, 47, 46, 45, 43, 42, 41,
    40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25,
    24, 23, 23, 22, 21, 20, 19, 18, 17, 17, 16, 15, 14, 13, 12, 12,
    11, 10, 9, 9, 8, 7, 6, 6, 5, 4, 3, 3, 2, 1, 1, 0,
};
static void br_level_db_text(char *out, uint32_t v)
{
    char digit[2] = {0, 0};
    uint32_t down = br_level_db_down[v & 127u];
    if (!v) { br_text_copy(out, "-INF", 12u); return; }
    br_text_copy(out, down ? "-" : "", 12u);
    if (v < 41u) br_number(out + (down ? 1 : 0), (int32_t)down);
    else {
        br_number(out + (down ? 1 : 0), (int32_t)(down / 10u));
        digit[0] = (char)('0' + down % 10u); br_text_append(out, ".", 12u); br_text_append(out, digit, 12u);
    }
    br_text_append(out, " dB", 12u);
}
/* The cue bars (rows 216..239) go out in 48-px chunks, one sum per chunk of what is shown there (the status
 * line, drawn whole, upsets them all). */
static uint32_t br_status_crc[5];
static void br_draw_cue_bar(uint32_t kind, uint32_t force)
{
    static uint32_t previous;
    static uint8_t next;
    char timing[25], number[8];
    const char *step_notice = 0;
    uint32_t back_save=kind==9u || kind==10u;
    const char *button1=(kind==2u || (kind>=4u && !back_save))?BR_PANEL_SAVE:"OCT-";
    const char *label1=kind==1u?"MUTE":kind==2u?"NEXT":(kind==3u || back_save)?"BACK":kind==5u?"YES":"CONFIRM";
    const char *button2=back_save?BR_PANEL_SAVE:kind==2u || kind>=4u?"PLAY":kind==3u?BR_PANEL_SAVE:"OCT+";
    const char *label2=kind==10u?"CHOOSE SLOT":kind==9u?"CONFIRM":kind==1u?"SOLO":kind==2u || kind==4u || kind==5u?(br_playing?"STOP":"START"):"NEXT";
    uint32_t b1=(kind==2u || (kind>=4u && !back_save))?22u:30u, b2=back_save?22u:kind==2u || kind>=4u?26u:kind==3u?22u:30u, gap=20u;
    uint32_t w1=b1+5u+(uint32_t)br_widget_text_width(label1), w2=b2+5u+(uint32_t)br_widget_text_width(label2);
    int32_t x1=kind==1u?6:(240-(int32_t)(w1+w2+gap))/2, x2=x1+(int32_t)(w1+gap);   /* MIXER / KIT LEVEL: dB on the right */
    uint32_t compact = 0u;
    int32_t cue_y=0, msg_y=24;
    uint32_t active=br_ui.message[0] && (int32_t)(br_ui.message_until-fm1_ms)>0;
    uint32_t elapsed=fm1_ms-br_ui.message_start, beat=br_ui.message_until-br_ui.message_start-640u, h;
    if (kind == 6u && br_seq_step_edit()) {
        int32_t micro = br_step_micro_get(br_ui.step_press_part, br_ui.step_press_step, br_ui.step_press_lane);
        br_text_copy(timing, BR_SEQ_2X8 ? "OCT-/+ NUDGE " : "OCT-/+ ", sizeof timing);
        br_micro_text(number, sizeof number, micro); br_text_append(timing, number, sizeof timing);
        step_notice = timing;
    }
    if ((kind == 8u || kind == 11u || kind == 12u) && br_session_legacy_migration_pending()) step_notice = "LEGACY SESSION / MIGRATE";
    if(active) {
        if(elapsed<160u) cue_y=(int32_t)(elapsed*24u/160u);
        else if(elapsed<320u){cue_y=24;msg_y=24-(int32_t)((elapsed-160u)*24u/160u);}
        else if(elapsed<320u+beat){cue_y=24;msg_y=0;}
        else if(elapsed<480u+beat){cue_y=24;msg_y=(int32_t)((elapsed-320u-beat)*24u/160u);}
        else {msg_y=24;cue_y=24-(int32_t)((elapsed-480u-beat)*24u/160u);}
    }
    h=(kind + compact * 16u)*131u+(uint32_t)cue_y*31u+(uint32_t)msg_y;
    if(kind==7u)h=h*31u+br_sel.loaded;
    if(kind==1u){   /* MIXER and KIT LEVEL: the selected strip's level in dB */
        uint32_t level=br_gate_page()?br_part[BR_DRUM_PART].lane_level[br_ui.lane & 3u]:br_part[br_ui.part].sound.level;
        br_level_db_text(timing,br_level_law(level));h=h*131u+level;   /* the engine's gain: (v/127)^2 */
#if BR_PRODUCT_SMK37
        h=h*3u+br_smk_fader_link;
#endif
    }
    if(kind==4u || kind==5u)h=h*31u+br_playing;
    if(active)h=h*31u+br_hash_text(br_ui.message);
    if(step_notice)h=h*31u+br_hash_text(step_notice);
    if (compact) { cue_y -= 5; msg_y -= 5; }
    for (uint32_t chunk = 0; chunk < 5u; ++chunk) {   /* 48-px chunks; one equal to what is shown is not sent */
    if (!br_band(&previous, &next, h, chunk, force)) continue;
    cv_begin(48, compact ? 14u : 24u, BR_BACKGROUND); cv_ox = (int32_t)(chunk * 48u);
    if ((kind == 8u || kind == 11u || kind == 12u) && step_notice) {
        br_widget_text((240 - br_widget_text_width(step_notice)) / 2, 8 + cue_y, step_notice, BR_GLOBAL_ACC);
    } else if (kind == 11u || kind == 12u) {
        const char *button = kind == 11u ? BR_PANEL_GLOBAL : "OCT-";
        int32_t key = kind == 12u ? BR_OCT_KEY_W : br_widget_text_width(button) + 8;
        int32_t w = key + 6 + br_widget_text_width("+") + 6 + 12 + 3 + br_widget_text_width(BR_PANEL_KNOB1) + 6 + br_widget_text_width("SNAP");
        int32_t x = (240 - w) / 2;
        if (kind == 12u) br_oct_key(x, 4 + cue_y, button);
        else { hdc_key(x, 4 + cue_y, key, 16); hdc_key_text(x + 4, 8 + cue_y, button, C_WHITE); }
        x += key + 6; br_widget_text(x, 8 + cue_y, "+", C_WHITE);
        x += br_widget_text_width("+") + 6; hdc_dial(x, 6 + cue_y, C_WHITE);
        x += 15; br_widget_text(x, 8 + cue_y, BR_PANEL_KNOB1, C_WHITE);
        x += br_widget_text_width(BR_PANEL_KNOB1) + 6; br_widget_text(x, 8 + cue_y, "SNAP", C_WHITE);
    } else if (kind == 8u) {
        int32_t w = 30 + 6 + br_widget_text_width("+") + 6 + 12 + 6 + br_widget_text_width("SNAP");
        int32_t x = (240 - w) / 2;
        br_oct_key(x, 4 + cue_y, "OCT-");
        x += 36; br_widget_text(x, 8 + cue_y, "+", C_WHITE);
        x += br_widget_text_width("+") + 6;
        hdc_dial(x, 6 + cue_y, C_WHITE);
        br_widget_text(x + 18, 8 + cue_y, "SNAP", C_WHITE);
    } else if (kind == 6u) {
        if (step_notice) br_draw_oct_cue(step_notice, cue_y, 0u, BR_SEQ_2X8 ? BR_GLOBAL_ACC : C_WHITE);
        else {
            int32_t x=(240-(12+4+br_widget_text_width(BR_PANEL_SELECT)+6+br_widget_text_width("DRUM")))/2;
            hdc_dial(x, 6 + cue_y, C_WHITE);
            br_widget_text(x+16,8+cue_y,BR_PANEL_SELECT,C_WHITE);
            br_widget_text(x+22+br_widget_text_width(BR_PANEL_SELECT),8+cue_y,"DRUM",C_WHITE);
        }
    } else if (kind == 7u) {             /* SEL (the square button) loads; the keys audition */
        const char *label = br_sel.loaded ? "DONE" : "LOAD PRESET";
#if BR_PRODUCT_SMK37
        int32_t button_width = br_widget_text_width(BR_PANEL_BROWSER) + 8;
        int32_t w1 = button_width + 6 + br_widget_text_width(label), w2 = 24 + 6 + br_widget_text_width("AUDITION");
#else
        int32_t w1 = 22 + 6 + br_widget_text_width(label), w2 = 24 + 6 + br_widget_text_width("AUDITION");
#endif
        int32_t x = ((240 - (w1 + 16 + w2)) / 2) & ~1, k = (x + w1 + 16) & ~1;
#if BR_PRODUCT_SMK37
        hdc_key(x, 1 + cue_y, button_width, 22);
        hdc_key_text(x + (button_width - br_widget_text_width(BR_PANEL_BROWSER)) / 2, 8 + cue_y, BR_PANEL_BROWSER, C_WHITE);
        br_widget_text(x + button_width + 6, 8 + cue_y, label, C_WHITE);
#else
        hdc_key(x, 1 + cue_y, 22, 22);
        hdc_key_text(x + (22 - br_widget_text_width("SEL")) / 2, 8 + cue_y, "SEL", C_WHITE);
        br_widget_text(x + 28, 8 + cue_y, label, C_WHITE);
#endif
        /* Five distinct white keys and a group of two plus one black key. */
        hd__rrect(k, 3 + cue_y, 24, 18, 3, RGB(176, 184, 188), RGB(84, 92, 96), RGB(220, 226, 228), RGB(48, 54, 56), 0);
        for (int32_t key = 0; key < 5; ++key)
            hd_rrect_grad(k + 2 + key * 4, 5 + cue_y, 3, 14, 1, C_WHITE, RGB(196, 202, 206));
        for (int32_t key = 4; key < 20; key += key == 8 ? 8 : 4)
            hd_rrect_grad(k + key, 5 + cue_y, 2, 8, 1, RGB(70, 76, 80), C_BLACK);
        br_widget_text(k + 30, 8 + cue_y, "AUDITION", C_WHITE);
    } else if (kind == 2u) {
        int32_t x=(240-(22+5+br_widget_text_width("NEXT")))/2;
        hdc_key(x,1+cue_y,22,22);
        hdc_key_text(x+(22-br_widget_text_width(BR_PANEL_SAVE))/2,8+cue_y,BR_PANEL_SAVE,C_WHITE);
        br_widget_text(x+27,8+cue_y,"NEXT",C_WHITE);
    } else {
        if (br_header_title_equal(button1, "OCT-")) br_oct_key(x1, 4 + cue_y, button1);
        else { hdc_key(x1, 1 + cue_y, (int32_t)b1, 22);
            hdc_key_text(x1+((int32_t)b1-br_widget_text_width(button1))/2,8+cue_y,button1,C_WHITE); }
        br_widget_text(x1+(int32_t)b1+5,8+cue_y,label1,C_WHITE);
        if (br_header_title_equal(button2, "OCT+")) br_oct_key(x2, 4 + cue_y, button2);
        else { hdc_key(x2, (back_save?1:4) + cue_y, (int32_t)b2, back_save?22:16);
            hdc_key_text(x2+((int32_t)b2-br_widget_text_width(button2))/2,8+cue_y,button2,C_WHITE); }
        br_widget_text(x2+(int32_t)b2+5,8+cue_y,label2,C_WHITE);
        if (kind == 1u) br_widget_text(237-br_widget_text_width(timing),8+cue_y,timing,BR_DIM);
#if BR_PRODUCT_SMK37
        if (kind == 1u && br_smk_fader_link)    /* fader link: fader 1 moves T1 and T2, or kit lanes 1 and 2 */
            br_widget_text(x2+(int32_t)b2+11+br_widget_text_width(label2),8+cue_y,br_gate_page()?"D1+D2":"T1+T2",BR_GLOBAL_ACC);
#endif
    }
    if(active && !br_draw_kit_button_message(br_ui.message, msg_y + (compact ? 8 : 0), compact))
        br_widget_text((240-br_widget_text_width(br_ui.message))/2,8+msg_y,br_ui.message,BR_GLOBAL_ACC);
    cv_ox = 0;
    br_crc_blit(chunk * 48u, compact ? 226u : 216u, &br_status_crc[chunk]);
    }
}

/* SEQUENCER with more than one bank of 16 steps: OCT-/+ pages the banks (br_buttons). */
static uint32_t br_seq_pages(void)
{
    return br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && br_part[br_ui.part].length > BR_BANK_STEPS;
}
/* The sequencer's bottom cue alternates every 3 s between SHIFT NOTES (0) and the bank paging (1) when it can page. */
#define BR_SEQ_CUE_MS 3000u
static uint32_t br_seq_cue_phase(void)
{
    return br_seq_pages() && !br_seq_step_edit() ? (fm1_ms / BR_SEQ_CUE_MS) & 1u : 0u;
}

/* The meaning of OCT-/OCT+ on pages where they are not the keyboard octave. Empty where they are. */
static const char *br_oct_hint(void)
{
    uint32_t kit = br_is_kit(br_part[br_ui.part].sound.preset);
#if BR_POLY_UI
    if (br_ui.page == BR_PAGE_HOME && br_ui.part < BR_SYNTH_PARTS && br_playing &&
        (br_part[br_ui.part].sound.voice_mode == 2u ||
         (br_part[br_ui.part].sound.voice_mode == 0u && !br_poly_user_off)))
        return "POLYPHONY OFF DURING PLAYBACK";
#endif
    if (br_retrig_page()) return "HOLD STEP: EDIT";
    if (br_sound_mod_page()) return "SCALES ALL MODULATION";   /* LFO DEPTHS: what the page is for */
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS)
#if BR_ARR_UI
        return br_arr_hint();
#else
        return "";
#endif
#endif
    if (br_ui.page == BR_PAGE_SEQ)
        return br_ui.screen[BR_PAGE_SEQ] ? "" : "OCT-/+ + REC SHIFT NOTES";
    if (br_ui.page == BR_PAGE_PUNCH) return "OCT-/+: PAGES";
    if (br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 2u) return br_build_text();
    if (kit && !br_page_plays_keys()) return "";
#if BR_POLY_UI
#if BR_PRODUCT_SMK37
    if (br_ui.page == BR_PAGE_HOME && br_ui.part < BR_SYNTH_PARTS &&
        (br_part[br_ui.part].sound.voice_mode == 2u ||
         (br_part[br_ui.part].sound.voice_mode == 0u && !br_poly_user_off))) {
        if (!br_is_plaits(br_part[br_ui.part].sound.preset)) return "MONO: MODEL LIMIT";
        if (br_playing) return "MONO: SEQUENCER RUNNING";
        if (br_arp_on) return "MONO: ARP ON";
    }
#endif
    if (br_ui.page == BR_PAGE_HOME && !br_ui.screen[BR_PAGE_HOME] && br_ui.part < BR_SYNTH_PARTS &&
        !(BR_PRODUCT_SMK37 && br_poly_mode_active()))
        return (br_part[br_ui.part].sound.voice_mode == 2u ||
                (br_part[br_ui.part].sound.voice_mode == 0u && !br_poly_user_off)) ?
               "OCT- + PLAY: POLYPHONY OFF" : "OCT- + PLAY: POLYPHONY ON";
#endif
    return "";
}

/* Other pages retain their compact status area when no cue is present. */
static void br_draw_status(uint32_t force)
{
    static uint32_t previous;
    if((br_ui.page==BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]) || br_gate_page()) {br_draw_cue_bar(1u,force);return;}
    if(br_sel_browse()) {br_draw_cue_bar(7u,force);return;}
    if(br_fx_sends_page() || br_pan_page()) {br_draw_cue_bar(8u,force);return;}
    if(br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u) {br_draw_cue_bar(12u,force);return;}
    if(br_home_filter_page()) {br_draw_cue_bar(12u,force);return;}
    char timing[25], number[8];
    const char *text = br_session_legacy_migration_pending() ? "LEGACY SESSION / MIGRATE" :
        (int32_t)(br_ui.message_until - fm1_ms) > 0 ? br_ui.message : "";
#if BR_POLY_UI
    /* The transport warning stays visible on HOME while the chosen track is in POLY. */
    if (br_ui.page == BR_PAGE_HOME && br_playing && !br_session_legacy_migration_pending()) {
        const char *hint = br_oct_hint();
        if (br_header_title_equal(hint, "POLYPHONY OFF DURING PLAYBACK")) text = hint;
    }
#endif
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (!*text && br_ui.page == BR_PAGE_PS && !(BR_ARR_UI && br_ps_state.mode == FM1_SONG_MODE_SONG)) {
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
        br_text_copy(timing, BR_SEQ_2X8 ? "OCT-/+ NUDGE " : "OCT-/+ ", sizeof timing);
        br_micro_text(number, sizeof number, micro); br_text_append(timing, number, sizeof timing);
        text = timing;
    }
    uint32_t dim = 0u;
    if (!*text) {
        /* What OCT-/OCT+ do here, when it is not the octave. */
        text = br_oct_hint(); dim = *text != 0;
    }
    uint32_t alt = dim ? br_seq_cue_phase() : 0u;   /* (the hint, not a message or the nudge, is on show) */
    uint32_t h = br_hash_text(text) ^ (dim << 31) ^ (alt << 30);
    if (br_region(&previous, h, force)) {
        cv_begin(240, 24, BR_BACKGROUND);
        if (*text) {
#if BR_POLY_UI
            if (dim && br_ui.page == BR_PAGE_HOME && br_ui.part < BR_SYNTH_PARTS &&
                br_is_poly_cue(text))
                br_draw_poly_cue(text, dim ? BR_DIM : BR_GLOBAL_ACC);
            else
#endif
            if (dim && br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ] && !br_seq_step_edit())
                { if (alt) br_draw_oct_cue("OCT-/+: STEP BANK", 0, 0u, BR_DIM); else br_draw_shift_cue(0); }
            else if (text[0] == 'O' && text[1] == 'C' && text[2] == 'T' && text[3] == '-')
                br_draw_oct_cue(text, 0, 0u, dim ? BR_DIM : BR_GLOBAL_ACC);
            else if (!br_draw_kit_button_message(text, 0, 0u))
                br_widget_text((240 - br_widget_text_width(text)) / 2, 8, text,
                               br_header_title_equal(text, "POLYPHONY OFF DURING PLAYBACK") ? RGB(248, 208, 64) :
                               dim ? BR_DIM : BR_GLOBAL_ACC);
        }
        cv_blit(0, 216);
        for (uint32_t i = 0; i < 5u; ++i) br_status_crc[i] ^= 0x80000000u;   /* (the cue bars' sums no longer hold) */
    }
}

/* HD screen bodies share the hds_ helpers below. A faint fill under the ADSR curve, on bw_adsr_graph's
 * geometry (same x, y, w, h): its decay and release quadratics have the control point's x at the
 * segment middle, so y follows from x directly. Drawn first; the curve strokes over its top edge. */
static void hds_env_fill(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t env[4], uint16_t color)
{
    int32_t left = x + 5, right = x + w - 5, top = y + 7, base = y + h - 7, p[5], py[5], i = 0, c, r;
    int32_t r0 = -cv_oy > top ? -cv_oy : top, r1 = -cv_oy + (int32_t)cv_h;
    uint16_t hi = hd_darken(color, 150u), lo = hd_darken(color, 228u);
    p[0] = left; p[1] = left + 3 + (int32_t)bw_clamp(env[0], 127u) * (w / 4 - 3) / 127;
    p[2] = p[1] + 3 + (int32_t)bw_clamp(env[1], 127u) * (w / 5 - 3) / 127;
    p[3] = right - 3 - (int32_t)bw_clamp(env[3], 127u) * (w / 4 - 3) / 127; p[4] = right;
    if (p[2] > p[3]) { p[2] = (p[1] + right) / 2; p[3] = (p[2] + right) / 2; }
    py[0] = py[4] = base; py[1] = top; py[2] = py[3] = base - (int32_t)bw_clamp(env[2], 127u) * (base - top) / 127;
    int16_t shade[64];                                   /* each row's place in the wash, worked out once */
    if (r1 > base) r1 = base;
    if (r1 > r0 + 64) r1 = r0 + 64;                      /* (ENV's regions are at most 31 rows) */
    for (r = r0; r < r1; ++r) shade[r - r0] = (int16_t)((r - top) * 256 / (base - top));
    for (c = left; c < right; ++c) {
        if (c < cv_ox) { c = cv_ox - 1; continue; }        /* (columns outside the canvas: nothing to fill) */
        if (c >= cv_ox + (int32_t)cv_w) break;
        int32_t t, u, yq;
        while (i < 3 && c >= p[i + 1]) ++i;
        t = (c - p[i]) * 256 / (p[i + 1] - p[i]); u = 256 - t;
        yq = i & 1 ? py[i + 1] * 256 + (py[i] - py[i + 1]) * u * u / 256 - 128 : py[i] * 256 + (py[i + 1] - py[i]) * t;
        for (r = r0 > (yq >> 8) ? r0 : yq >> 8; r < r1; ++r)   /* (rows above yq >> 8 have no coverage) */
            hd__px(c, r, hd__dmix(hi, lo, shade[r - r0], c, r), (r + 1) * 256 - yq);
    }
}
/* One envelope, with its four knob values together below the graph. Its regions (rows below row 32 of the
 * screen): the graph's two 120-px halves (rows 46..107, two bands each) drawn from the stages they show, and
 * each knob's 60-px column: its badge (rows 0..45) from the focus alone, its value, label and route badges
 * (rows 108..167) from that knob, so a knob turn redraws one half of the graph (both for SUSTAIN) and one
 * value. Bands whose pixels did not change are not sent. */
struct br_env_view {
    br_control_t c[4];
    char value[4][24];
    uint32_t env[4], route[4], focus, source, p;
};
static void hds_well(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r);
static void br_env_art(void)
{
    const struct br_env_view *v = br_view;
    int32_t top = -cv_oy, bottom = top + (int32_t)cv_h;
    if (top < 108 && bottom > 46) {
        hds_well(5, 46, 230, 62, 6);
        hds_env_fill(9, 48, 222, 56, v->env, BR_ACC);
        for (uint32_t k = 1u; k < 4u; ++k)
            hd_line(9 + (int32_t)k * 222 / 4, 52, 9 + (int32_t)k * 222 / 4, 100, RGB(22, 26, 28));
        hd_line(13, 76, 227, 76, RGB(22, 26, 28));
        bw_adsr_graph(9, 48, 222, 56, v->env, v->focus);
    }
    if (top >= 46 && bottom <= 108) return;            /* (rows 46..107 hold only the graph) */
    for (uint32_t k = 0; k < 4u; ++k) {
        int32_t center = 30 + (int32_t)k * 60;
        if (!v->c[k].label[0]) continue;
        br_draw_knob_badge(k, center, BR_PARAM_BODY_Y - 40, k == v->focus);
        br_draw_param_text(center, BR_PARAM_BODY_Y + BR_PARAM_VALUE_Y - 40,
                           v->value[k], v->c[k].label, k == v->focus ? BR_ACC : BR_DIM);
        br_draw_route_badges(center + (br_badge_count(v->route[k]) == 4u ? (k == 1u ? -8 : k == 2u ? 8 : 0) : 0), br_badge_count(v->route[k]) == 4u ? 127 : 130, v->route[k], v->p, BR_BACKGROUND);
    }
    if (v->source & BR_BADGE_SEND1) br_draw_route_badge(82, 159, 4u, v->p, BR_BACKGROUND);
    if (v->source & BR_BADGE_SEND2) br_draw_route_badge(158, 159, 5u, v->p, BR_BACKGROUND);
}
static void br_draw_envelope(uint32_t force)
{
    static uint32_t previous[10];
    static uint8_t next[10];
    struct br_env_view v;
    uint32_t screen = br_ui.screen[BR_PAGE_ENV], p = br_ui.part, region, band;
    uint32_t hash[10];
    v.p = p; v.focus = br_active_knob();
    v.source = br_send_badges(p, screen ? BR_SOURCE_ENV : BR_SOURCE_AMP);
    for (uint32_t k = 0; k < 4u; ++k) {
        v.c[k] = br_control(k);
        br_control_text(k, &v.c[k], v.value[k]);
        v.env[k] = (uint32_t)v.c[k].value;
        v.route[k] = br_control_route_badges(k);
        hash[k] = (k == v.focus) * 2u + 1u + br_ui_accent * 7u;
        hash[4u + k] = br_hash_text(v.value[k]) ^ br_hash_text(v.c[k].label) * 7u ^ (k == v.focus) << 30;
        hash[4u + k] = ((hash[4u + k] * 67u + v.route[k]) * 67u + br_badge_pulse_key(p, v.route[k])) * 67u + v.source;
        hash[4u + k] = (hash[4u + k] * 31u + screen + p * 4u) * 31u + br_ui_accent;
    }
    /* The graph's halves: attack and decay end by x 113 (bw_adsr_graph at w 222) and release starts at 171 or
     * later, so the left half is drawn from A, D, S and the right from S and R (each with its stage's white). */
    hash[8] = ((((v.focus < 3u ? v.focus : 3u) * 131u + v.env[0]) * 131u + v.env[1]) * 131u + v.env[2]) * 7u + br_ui_accent;
    hash[9] = ((((v.focus == 2u) + (v.focus == 3u) * 2u + 8u) * 131u + v.env[2]) * 131u + v.env[3]) * 7u + br_ui_accent;
    br_view = &v; br_art = br_env_art;
    for (uint32_t i = 0; i < 10u; ++i) {   /* the values first, then the badges, then the graph */
        region = i < 8u ? (i + 4u) & 7u : i;
        if (force) previous[region] = ~hash[region] ^ br_pass.epoch;   /* (drawn whole) */
        while ((band = br_roll(&previous[region], &next[region], hash[region], 2u)) < 2u) {
            uint32_t x = region < 8u ? (region & 3u) * 60u : (region - 8u) * 120u;
            uint32_t y0 = region < 4u ? band * 34u : region < 8u ? 108u + band * 13u : 46u + band * 31u;
            uint32_t rows = region < 4u ? (band ? 12u : 34u) : region < 8u ? (band ? 47u : 13u) : 31u;
            cv_begin(region < 8u ? 60u : 120u, rows, BR_BACKGROUND);
            cv_oy = -(int32_t)y0; cv_ox = (int32_t)x;
            br_env_art();
            cv_oy = 0; cv_ox = 0;
            br_crc_blit(x, 40u + y0, &br_band_crc[region * 2u + band]);   /* (only the bands that changed go out) */
        }
    }
    br_draw_status(force);
}

static void hds_well(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r);

/* COMP TIME shares one attack/hold/release curve; MIX remains the fourth
 * independent control. Draw in the same bounded regions as the envelopes. */
static void br_comp_time_art(void)
{
    const struct br_env_view *v = br_view;
    int32_t top = -cv_oy, bottom = top + (int32_t)cv_h;
    if (top < 108 && bottom > 46) {
        uint32_t a = 12u + v->env[0], hold = 8u + v->env[1], release = 20u + v->env[2];
        uint32_t total = a + hold + release;
        int32_t x[4] = {9, 0, 0, 171}, y[4] = {56, 96, 96, 56};
        int32_t qx[4], qy[4];
        x[1] = x[0] + (int32_t)(162u * a / total);
        x[2] = x[1] + (int32_t)(162u * hold / total);
        hds_well(5, 47, 171, 59, 6);
        for (uint32_t k = 1u; k < 4u; ++k)
            hd_line(9 + (int32_t)k * 162 / 4, 51, 9 + (int32_t)k * 162 / 4, 100, RGB(22,26,28));
        hd_line(9, 76, 171, 76, RGB(22,26,28));
        /* (no floor line: the well's lit bottom lip is the floor; a second line 4 rows above it read as a double edge) */
        for (uint32_t i = 0; i < 4u; ++i) { qx[i] = bw_q8(x[i]); qy[i] = bw_q8(y[i]); }
        bw_area(qx, qy, 0, bw_q8(101), 4u, BR_ACC, 52, 0);
        for (uint32_t i = 0; i < 3u; ++i) {
            uint16_t color = v->focus == i ? C_WHITE : BR_ACC;
            uint32_t width = v->focus == i ? 640u : 320u;
            hd_line_q8(qx[i], qy[i], qx[i+1], qy[i+1], width, color);
        }
        br_widget_draw(BR_WIDGET_MIX, 180, 46, 60, v->env[3], BR_BLEND_OSC, 0, 0u);
    }
    if (top >= 46 && bottom <= 108) return;
    for (uint32_t k = 0; k < 4u; ++k) {
        int32_t center = 30 + (int32_t)k * 60;
        br_draw_knob_badge(k, center, BR_PARAM_BODY_Y - 40, k == v->focus);
        br_draw_param_text(center, BR_PARAM_BODY_Y + BR_PARAM_VALUE_Y - 40,
                           v->value[k], v->c[k].label, k == v->focus ? BR_ACC : BR_DIM);
    }
}
static void br_draw_comp_time(uint32_t force)
{
    static uint32_t previous[10];
    static uint8_t next[10];
    struct br_env_view v;
    uint32_t hash[10], region, band;
    v.focus = br_active_knob();
    for (uint32_t k = 0; k < 4u; ++k) {
        v.c[k] = br_control(k); br_control_text(k, &v.c[k], v.value[k]);
        v.env[k] = (uint32_t)v.c[k].value;
        hash[k] = (k == v.focus) * 2u + 1u + br_ui_accent * 7u;
        hash[4u+k] = (br_hash_text(v.value[k]) ^ br_hash_text(v.c[k].label) * 7u ^ (k == v.focus) << 30) * 31u + br_ui_accent;
    }
    hash[8] = (((v.focus * 131u + v.env[0]) * 131u + v.env[1]) * 131u + v.env[2]) * 7u + br_ui_accent;
    hash[9] = hash[8] * 131u + v.env[3];
    br_view = &v; br_art = br_comp_time_art;
    for (uint32_t i = 0; i < 10u; ++i) {
        region = i < 8u ? (i + 4u) & 7u : i;
        if (force) previous[region] = ~hash[region] ^ br_pass.epoch;
        while ((band = br_roll(&previous[region], &next[region], hash[region], 2u)) < 2u) {
            uint32_t x = region < 8u ? (region & 3u) * 60u : (region - 8u) * 120u;
            uint32_t y0 = region < 4u ? band * 34u : region < 8u ? 108u + band * 13u : 46u + band * 31u;
            uint32_t rows = region < 4u ? (band ? 12u : 34u) : region < 8u ? (band ? 47u : 13u) : 31u;
            cv_begin(region < 8u ? 60u : 120u, rows, BR_BACKGROUND);
            cv_oy = -(int32_t)y0; cv_ox = (int32_t)x;
            br_comp_time_art();
            cv_oy = 0; cv_ox = 0;
            br_crc_blit(x, 40u + y0, &br_band_crc[region * 2u + band]);
        }
    }
    br_draw_status(force);
}

/* Each PERFORM graph combines the two neighboring knob controls. */
static void br_perform_filter_graph(int32_t x, int32_t y, uint32_t position, uint32_t resonance)
{
    int32_t left=x+5, right=x+107, top=y+5, base=y+57;
    int32_t cutoff=position<63u ? (int32_t)position*127/62 :
        ((int32_t)position-65)*127/62;
    /* The same response at sub-pixel precision, washed like the function graphs. */
    int32_t qx[33], qy[33];
    /* (no floor line: the chart's well has a lit bottom lip) */
    for (int32_t i=0;i<=32;++i) {
        int32_t frequency=i*127/32, level=90;
        if(position!=63u && position!=64u) {
            int32_t distance=frequency-cutoff, magnitude=distance<0?-distance:distance;
            int32_t strength=position<63u ? 63-(int32_t)position : (int32_t)position-64;
            int32_t filtered=(distance*96/(magnitude+5)+96)*90/192;
            if(position<63u)filtered=90-filtered;
            if(strength>8)strength=8;
            filtered+=(int32_t)resonance*32/(127*(1+magnitude*magnitude/90));
            level=90+(filtered-90)*strength/8;
        }
        if(level>127)level=127;
        qx[i]=bw_q8(left)+i*(right-left)*8; qy[i]=bw_q8(base)-level*(base-top)*256/127;
    }
    bw_area(qx,qy,0,bw_q8(base),33u,BR_ACC,52,0);           /* a lighter wash: this panel is large */
    bw_path(qx,qy,33u,448u,BR_ACC);
    return;
    int32_t px=left, py=top;
    bw_line(left,base,right,base,512u,BR_LINE);
    for (int32_t i=0;i<=64;++i) {
        int32_t frequency=i*127/64, level;
        if(position==63u || position==64u) level=90;
        else {
            int32_t distance=frequency-cutoff, magnitude=distance<0?-distance:distance;
            int32_t response=distance*96/(magnitude+5);
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


static void br_draw_page_route_badge(uint32_t force, uint32_t part);
/* A recessed slot: dark shaded top (inner shadow), lit bottom lip. */
static void hds_well(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)
{
    hd__rrect(x, y, w, h, r, RGB(10, 12, 13), RGB(10, 12, 13), C_BLACK, RGB(46, 53, 56), 0);   /* flat: no dither grain */
}
/* Two-column filter graph, then two knobs. Used for the global filter (GLO FILTER, RESONANCE, ENV DEPTH,
 * SMOOTH) and for each track's filter page (FILTER, RESONANCE, ENV DEPTH, KEY TRACK). */
/* The PERFORM screens (HOME filter, MIX 2) in four 60-px columns of five bands. Each column is three regions:
 * its knob badge (from the focus), its picture (from its knob; the filter graph's two columns from knobs 1
 * and 2 together) and its value, label and route badges (from its knob): a knob turn redraws its value and
 * its picture (or the graph's two), and only the bands whose pixels changed are sent. */
struct br_perform_view {
    br_control_t c[4];
    char value[4][24];
    uint32_t q[4], kind[4], variant[4], route[4], has_env[4], env[4][4], focus, p;
};
static void br_perform_art(void)
{
    const struct br_perform_view *v = br_view;
    int32_t top = -cv_oy, bottom = top + (int32_t)cv_h;
    uint32_t k;
    if (top < 74 && bottom > 12) {
        hds_well(5,BR_PARAM_ICON_Y,111,62,6);             /* same 62-row height as the ENV chart */
        for(k=1u;k<4u;++k) hd_line(9+(int32_t)k*102/4,BR_PARAM_ICON_Y+5,9+(int32_t)k*102/4,BR_PARAM_ICON_Y+57,RGB(22,26,28));
        hd_line(9,BR_PARAM_ICON_Y+31,111,BR_PARAM_ICON_Y+31,RGB(22,26,28));
        br_perform_filter_graph(4,BR_PARAM_ICON_Y,(uint32_t)v->c[0].value,(uint32_t)v->c[1].value);
        for(k=2u;k<4u;++k)                                  /* the two right-hand controls are knobs */
            if (v->c[k].label[0])
                br_widget_draw(v->kind[k],(int32_t)k*60,BR_PARAM_ICON_Y,60,v->q[k],v->variant[k],v->has_env[k]?v->env[k]:0,0u);
            else br_draw_empty_param((int32_t)k*60+10, BR_PARAM_ICON_Y+10, 40);
    }
    if (top >= 12 && bottom <= 74) return;              /* (rows 12..73 hold only the pictures) */
    for(k=0;k<4u;++k) {
        int32_t center=30+(int32_t)k*60;
        if (!v->c[k].label[0]) continue;
        br_draw_knob_badge(k,center,0,k==v->focus);
        br_draw_param_text(center, BR_PARAM_VALUE_Y, v->value[k], v->c[k].label, BR_DIM);
        if(v->route[k]) br_draw_route_badges(center,98,v->route[k],v->p,BR_BACKGROUND);
    }
}
static void br_draw_perform(uint32_t force)
{
    static const uint8_t band_y[6] = {0, 12, 43, 74, 97, 120};   /* badge; picture in two; value and badges in two */
    static uint32_t previous[12];
    static uint8_t next[12];
    struct br_perform_view v;
    uint32_t k, p=br_ui.part, track=br_ui.page!=BR_PAGE_MIX, band, hash[12];
    v.p=p; v.focus=br_active_knob();
    for(k=0;k<4u;++k) {
        br_control_t *c=&v.c[k];
        int32_t range;
        *c=br_control(k);
        br_control_text(k,c,v.value[k]);
        range=c->high-c->low;
        v.q[k]=range>0?(uint32_t)(br_clip(c->value-c->low,0,range)*127/range):0u;
        v.kind[k]=br_control_widget(k,c);
        v.variant[k]=br_control_widget_variant(k,c,v.kind[k]);
        v.has_env[k]=br_param_env(k,c,v.kind[k],v.env[k]);   /* companion values, when a parameter graphic asks */
        v.route[k]=track?br_control_route_badges(k):0u;
        hash[k]=((br_hash_text(v.value[k])*31u+br_hash_text(c->label))*67u+v.route[k])*67u+br_badge_pulse_key(p,v.route[k]);
        hash[k]=(hash[k]*31u+p)*31u+br_ui_accent;                       /* value, label, route badges */
        hash[4u+k]=(k==v.focus)*31u+br_ui_accent;                       /* the knob badge */
        hash[8u+k]=((br_ui_accent*131u+v.q[k])*31u+v.kind[k])*31u+v.variant[k];   /* the picture */
        if(v.has_env[k]) for(uint32_t i=0;i<4u;++i) hash[8u+k]=hash[8u+k]*131u+v.env[k][i];
    }
    for(k=0;k<2u;++k) hash[8u+k]=((br_ui_accent*131u+(uint32_t)v.c[0].value)*131u+(uint32_t)v.c[1].value)*3u+k;   /* (the graph) */
    br_draw_page_route_badge(force,p); /* global filter also clears the track badge when switching directly */
    br_view=&v; br_art=br_perform_art;
    for(uint32_t r=0;r<12u;++r) {   /* per 60-px column: value and badges first, then the knob badge, the picture */
        static const uint8_t first[3]={3u,0u,1u}, count[3]={2u,1u,2u};
        uint32_t part=r>>2, n=count[part];
        k=r&3u;
        if(force) previous[r]=~hash[r]^br_pass.epoch;   /* (drawn whole) */
        while((band=br_roll(&previous[r],&next[r],hash[r],n))<n) {
            band+=first[part];
            cv_begin(60,band_y[band+1u]-band_y[band],BR_BACKGROUND);cv_oy=-(int32_t)band_y[band];cv_ox=(int32_t)(k*60u);
            br_perform_art();
            cv_oy=0;cv_ox=0;br_crc_blit(k*60u,BR_PARAM_STANDARD_BODY_Y+band_y[band],&br_band_crc[k*5u+band]);
        }
    }
    br_draw_status(force);
}

/* Four logical lanes share the drawing workspace. Their row labels show the
 * selected one of eight available voices; no hidden legacy lane rows remain. */
/* Compact sequencer icons share a centered, symmetric 2px grid. */
/* Raised tile: fine rounded outline, shaded face, 1 px top highlight. */
static void hds_tile(int32_t x, int32_t y, int32_t w, int32_t h)
{
    hd_rrect_frame(x, y, w, h, 6, RGB(52, 60, 64));
    hd__rrect(x + 1, y + 1, w - 2, h - 2, 5, RGB(30, 36, 39), RGB(11, 14, 16), RGB(72, 82, 86), RGB(6, 8, 9), 0);
}
#if BR_SEQ_2X8
/* A step condition in at most four characters, for a 26 px drum cell or a tile ("" when ALWAYS). */
static void sq_cond_short(char *out, uint32_t cond)
{
    static const char code[8][5] = {"1ST", "RPT", "HIT", "MISS", "NEI", "ALN", "FILL", "NFIL"};
    br_step_condition_text(out, cond);
    if (cond == BR_STEP_COND_ALWAYS) out[0] = 0;
    else if (cond - 101u < 8u) br_text_copy(out, code[cond - 101u], 5u);
}
#endif
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
    hds_tile(10, 0, 34, 32);
#ifdef BR_PATTERN_SONG_EXPERIMENT
#if BR_ARR_UI
#define BR_PS_ICON_BARS br_arr_bars_shown()
#define BR_PS_ICON_ARRANGE BR_ARR_VIEW
#else                                   /* (a build with the pattern page but no arranger views: fixed pictures) */
#define BR_PS_ICON_BARS 4u
#define BR_PS_ICON_ARRANGE 1
#endif
    if (br_ui.page == BR_PAGE_PS) {
        if (k == 0u) {
            hd_line(17, 16, 37, 16, BR_ACC);
            hd_line(17, 16, 22, 11, BR_ACC); hd_line(17, 16, 22, 21, BR_ACC);
            hd_line(37, 16, 32, 11, BR_ACC); hd_line(37, 16, 32, 21, BR_ACC);
        } else if (k == 1u) {           /* BARS: 1..4 lit of four slots; 5 and more add a plus */
            uint32_t bars = BR_PS_ICON_BARS;
            for (int32_t i = 0; i < 4; ++i) hd_rrect(13 + i * 5, 9, 3, 14, 1, (uint32_t)i < bars ? BR_ACC : BR_LINE);
            if (bars > 4u) { hd_line(37, 12, 37, 20, C_WHITE); hd_line(34, 16, 40, 16, C_WHITE); }
        } else if (k == 2u) {           /* LOOP MODE: ON the loop closed, with arrowheads; OFF the same outline dim, struck through */
            static const uint8_t loop[] = {35, 10, 19, 10, 16, 13, 16, 17, 19, 20, 35, 20, 38, 17, 38, 13, 35, 10};
            uint16_t c = br_ps_state.end ? BR_ACC : BR_LINE;
            for (uint32_t i = 0; i < 16u; i += 2u) hd_line(loop[i], loop[i + 1], loop[i + 2], loop[i + 3], c);
            if (br_ps_state.end) {
                hd_line(31, 7, 35, 10, c); hd_line(35, 10, 31, 13, c);
                hd_line(23, 17, 19, 20, c); hd_line(19, 20, 23, 23, c);
            } else hd_line(16, 24, 38, 6, BR_DIM);
        } else if (!BR_PS_ICON_ARRANGE) {   /* PERFORM, PATTERN: one block that repeats */
            hd_rrect(14, 9, 15, 14, 2, BR_ACC);
            hd_line(33, 9, 33, 22, C_WHITE); hd_rrect(35, 12, 2, 2, 0, C_WHITE); hd_rrect(35, 18, 2, 2, 0, C_WHITE);
        } else {                        /* PERFORM, ARRANGE: a timeline of blocks of different lengths */
            static const uint8_t at[4][2] = {{14, 6}, {21, 10}, {32, 4}, {37, 5}};
            for (int32_t i = 0; i < 4; ++i) hd_rrect(at[i][0], 9, at[i][1], 12, 1, BR_ACC);
            hd_line(14, 24, 41, 24, C_WHITE);
        }
        br_widget_text((54 - br_widget_text_width(value)) / 2, 36, value, C_WHITE);
        br_widget_text((54 - br_widget_text_width(c->label)) / 2, 45, c->label, k == focus ? C_WHITE : BR_DIM);
        return;
    }
#endif
    if (br_retrig_page()) {
        uint32_t count = !k ? (uint32_t)c->value + 1u : 4u;
        int32_t gap = !k ? 3 : 3 + (int32_t)c->value;
        if (count > 8u) count = 8u;
        if (k && gap > 8) gap = 8;
        int32_t left = 27 - ((int32_t)count - 1) * gap / 2;
        for (uint32_t i = 0; i < count; ++i) {
            int32_t x = left + (int32_t)i * gap;
            hd_rrect(x, 8, 2, 16, 1, i ? BR_ACC : C_WHITE);
        }
        br_widget_text((54 - br_widget_text_width(value)) / 2, 36, value, C_WHITE);
        br_widget_text((54 - br_widget_text_width(c->label)) / 2, 45, c->label, BR_DIM);
        return;
    }
    pgd_seq_tile(k, c->value, q, c->low);   /* D21, instrument_widgets_params_d.c */
#if BR_SEQ_2X8
    {   /* Value over label on every tile, as the Retrigger page does; a value too wide for the tile drops
         * its unit word (LENGTH) or takes the short condition code. */
        char text[24];
        br_text_copy(text, value, sizeof text);
        if (br_widget_text_width(text) > 54) {
            char *cut = 0;
            for (char *at = text; *at; ++at) if (*at == ' ') cut = at;
            if (k == 3u) sq_cond_short(text, (uint32_t)c->value);
            else if (cut) *cut = 0;
        }
        br_widget_text((54 - br_widget_text_width(text)) / 2, 36, text, C_WHITE);
        br_widget_text((54 - br_widget_text_width(c->label)) / 2, 45, c->label, k == focus ? C_WHITE : BR_DIM);
        return;
    }
#endif
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

#if BR_SEQ_2X8
/* Two rows of eight 26 px cells, 2 px apart inside a beat and 8 px between the two beats of a row (the open
 * drum lane uses the same columns). Each cell's canvas runs to the next column, so it paints its own gap and
 * the length band or tie that crosses it. */
static int32_t sq_col_x(uint32_t col) { return 6 + (int32_t)col * 28 + (col >= 4u ? 6 : 0); }
static int32_t sq_col_w(uint32_t col) { return col == 3u ? 34 : col == 7u ? 32 : 28; }
#define SQ_EN 1u
#define SQ_ON 2u
#define SQ_HELD 4u
#define SQ_PLAY 8u
#define SQ_BRIDGE 16u
#define SQ_TIE 32u
#define SQ_COND 64u
#define SQ_ACC 128u
/* Cell body (x, 3, 26, h) on a fresh canvas w wide: flat fill (outline only past the pattern's end), the
 * velocity bar at row vy and the length band at row ty. The band starts at the cell's left edge when the step
 * is only covered by an earlier note, and runs on through the gap when the next step is covered too. */
static void sq_cell(int32_t x, int32_t w, int32_t h, uint16_t fill, uint32_t f, uint32_t vel, uint32_t cover,
                    int32_t vy, int32_t ty)
{
    if (!(f & SQ_EN)) { hd_rrect_frame(x, 3, 26, h, 4, RGB(28, 33, 36)); return; }
    hd_rrect(x, 3, 26, h, 4, fill);
    if (vel) hd_rrect(x + 2, vy, (int32_t)(vel * 21u / 127u) + 1, 2, 1, hd_lighten(BR_ACC, 120u));
    if (cover) {
        int32_t x0 = f & SQ_ON ? x + 2 : x;
        cv_rect(x0, ty, (f & SQ_BRIDGE ? w : x + 2 + (int32_t)(cover * 22u / 16u)) - x0, 3, BR_ACC);
    }
}
/* Held step: a 2 px white ring on the body. Playhead: a white cap above it. Both can show at once. */
static void sq_marks(int32_t x, int32_t h, uint32_t f)
{
    if (f & SQ_HELD) { hd_rrect_frame(x, 3, 26, h, 4, C_WHITE); hd_rrect_frame(x + 1, 4, 24, h - 2, 3, C_WHITE); }
    if (f & SQ_PLAY) hd_rrect(x, 0, 26, 2, 1, C_WHITE);
}
/* A step cell's (or a compact group's) rows: all of them when its hash h (everything but the playhead) has
 * changed, only its 2-row cap strip when just the playhead has moved (*cap: the playhead shown there), else
 * none. */
static uint8_t br_cell_cap[BR_BANK_STEPS], br_group_cap[BR_DRUM_LANES][4];
static uint32_t br_cell_rows(uint32_t *old, uint8_t *cap, uint32_t play, uint32_t h, uint32_t rows, uint32_t force)
{
    if (br_region(old, h, force)) { *cap = (uint8_t)play; return rows; }
    if (*cap == play || !br_open()) return 0u;
    *cap = (uint8_t)play; return 2u;
}
/* Chips centred on row y: up to three labels, each on its own face colour. */
static void sq_chips(int32_t y, const char *const text[3], const uint16_t face[3])
{
    int32_t i, w = -6, x;
    for (i = 0; i < 3 && text[i]; ++i) w += br_widget_text_width(text[i]) + 20;
    x = (240 - w) / 2;
    for (i = 0; i < 3 && text[i]; ++i) {
        int32_t cw = br_widget_text_width(text[i]) + 14;
        hdc_chip(x, y, cw, 16, 5, face[i]);
        br_widget_text(x + 7, y + 4, text[i], face[i] == BR_LINE ? C_WHITE : C_BLACK);
        x += cw + 6;
    }
}
#endif
static void br_draw_drum_grid(uint32_t force)
{
#if BR_SEQ_2X8
    /* No ruler. Lanes stack from row 36 in kit order; the selected lane opens in place as two rows of eight
     * square cells (70 rows), the others stay one row of sixteen small cells (10 rows), 2 rows apart. A lane change
     * moves lanes, so it repaints like a layout change. Compact rows repaint per beat (four steps). */
    static uint32_t name_old[BR_DRUM_LANES], group_old[BR_DRUM_LANES][4], cell_old[BR_BANK_STEPS];
    static uint32_t lane_old = 0xffffffffu, gaps_old;
    static uint8_t gaps_next;
    const uint16_t band = RGB(14, 17, 20);
    uint32_t p = br_ui.part, length = br_part[p].length, lane, cell, h;
    uint32_t base = br_ui.step_bank * BR_BANK_STEPS;
    uint32_t retrig = br_retrig_page();
    uint32_t editing = retrig ? br_retrig_editing() : br_ui.step_edit_active;
    uint32_t edit_step = retrig ? br_ui.retrig_step : br_ui.step_press_step;
    int32_t y = 36;
    if (lane_old != br_ui.lane) { br_pass.epoch += 0x9e3779b9u; lane_old = br_ui.lane; }   /* lanes move: draw all once */
    for (lane = 0; lane < BR_DRUM_LANES; lane++) {
        uint32_t open = lane == br_ui.lane, voice = br_drum_voice_id(br_part[p].sound.drum_voice_mask, lane);
        int32_t lh = open ? 70 : 10;
        const char *name = br_drum_voice_label(br_part[p].sound.drum_voice_mask, lane);
        /* the rows between lanes (and below the last one) */
        if (br_band(&gaps_old, &gaps_next, br_ui.lane, lane * 2u, force))     /* (they move with the open lane) */
            br_background_rect(0, (uint32_t)(y + lh), 240, lane + 1u < BR_DRUM_LANES ? 2u : 15u);
        if (!open && br_band(&gaps_old, &gaps_next, br_ui.lane, lane * 2u + 1u, force)) br_background_rect(236, (uint32_t)y, 4, 10);
        /* The final gap is 16 rows. Give its last row its own band so
         * a partially sent first band cannot be flushed by a second canvas. */
        if (lane + 1u == BR_DRUM_LANES && br_band(&gaps_old, &gaps_next, br_ui.lane, 8u, force))
            br_background_rect(0, (uint32_t)(y + lh + 15), 240, (uint32_t)(158 - y - lh - 15));
        uint32_t solo = (br_kit_solo_mask >> lane) & 1u;
        uint32_t muted = ((br_kit_mute_mask >> lane) & 1u) || (br_kit_solo_mask && !solo) || br_part[p].sound.mute;
        h = ((((voice * 2u + open) * 8u + p) * 131u + br_ui_accent) * 8u + solo * 4u + muted * 2u) + br_recording;
        if (br_region(&name_old[lane], h, force)) {
            if (open) {                          /* the band's top, with the lane accent at its left; tags at its right */
                int32_t x = 234;
                cv_begin(240, 8, BR_BACKGROUND);
                hd_rrect(0, 0, 240, lh, 5, band);
                hd_rrect(0, 2, 2, lh - 4, 1, BR_ACC);
                br_widget_text(6, 1, name, C_WHITE);
                if (br_recording) { x -= br_widget_text_width("REC"); br_widget_text(x, 1, "REC", RGB(240, 64, 64)); x -= 8; }
                if (muted) { x -= br_widget_text_width("MUTED"); br_widget_text(x, 1, "MUTED", C_WHITE); x -= 8; }
                if (solo) br_widget_text(x - br_widget_text_width("SOLO"), 1, "SOLO", BR_GLOBAL_ACC);
            } else {                             /* a silenced lane's name is struck through */
                cv_begin(44, 10, BR_BACKGROUND);
                br_widget_text(6, 1, name, muted ? BR_LINE : BR_DIM);
                if (muted) hd_rrect(4, 4, br_widget_text_width(name) + 4, 1, 0, BR_DIM);
            }
            cv_blit(0, (uint32_t)y);
        }
        if (open) {
            for (cell = 0; cell < BR_BANK_STEPS; cell++) {
                uint32_t col = cell & 7u, row = cell >> 3, step = base + cell, next = step + 1u;
                uint32_t word = br_part[p].step[step], enabled = step < length;
                uint32_t hit = enabled && ((BR_STEP_HIT(word) >> lane) & 1u);
                uint32_t cover = enabled ? br_step_length_coverage(p, step, lane, 1u) : 0u;
                uint32_t vel = hit ? br_step_velocity_get(p, step, lane) : 0u;
                uint32_t note = hit ? br_step_note_get(p, step, lane) : 60u;
                uint32_t cond = hit ? br_step_condition_get(p, step, lane) : BR_STEP_COND_ALWAYS;
                uint32_t count = hit ? br_seq_kit_retrig[step][lane] & 7u : 0u;
                uint32_t f = enabled | hit << 1 | (editing && step == edit_step) << 2 | (br_playing && step == br_step_pos[p]) << 3;
                int32_t x0 = col ? sq_col_x(col) : 0, x = sq_col_x(col) - x0, w = col ? sq_col_w(col) : 34, ly = 8 + (int32_t)row * 31;
                if (hit && ((BR_STEP_ACC(word) >> lane) & 1u)) f |= SQ_ACC;
                if (cover == 16u && col != 7u && next < length && !((BR_STEP_HIT(br_part[p].step[next]) >> lane) & 1u) &&
                    br_step_length_coverage(p, next, lane, 1u)) f |= SQ_BRIDGE;
                h = (((((step * 257u + (f & ~SQ_PLAY)) * 131u + vel) * 131u + note) * 149u + cond) * 9u + count) * 17u + cover;
                h = (h * 8u + p) * 5u + lane;
                uint32_t rows = br_cell_rows(&cell_old[cell], &br_cell_cap[cell], f & SQ_PLAY, h, 31u, force);
                if (!rows) continue;
                if (!col || col == 7u) {         /* the band's rounded ends; the left end carries the accent */
                    cv_begin((uint32_t)w, rows, BR_BACKGROUND);
                    hd_rrect(-x0, -ly, 240, lh, 5, band);
                    if (!col) hd_rrect(0, 2 - ly, 2, lh - 4, 1, BR_ACC);
                } else cv_begin((uint32_t)w, rows, band);
                sq_cell(x, w, 26, !hit ? RGB(32, 38, 41) : hd_mix(BR_CARD, BR_ACC, f & SQ_ACC ? 217u : 128u),
                        f, vel, cover, 26, 29);
                if (hit) {                       /* top: retrigger count and accent; then condition; then pitch offset */
                    char text[16];
                    int32_t tw;
                    if (count) {
                        text[0] = 'R'; br_number(text + 1, (int32_t)count);
                        br_widget_text(x + 3, 5, text, C_WHITE);
                    }
                    if (f & SQ_ACC) br_widget_text(x + 23 - br_widget_text_width(">"), 5, ">", C_WHITE);
                    sq_cond_short(text, cond);
                    tw = br_widget_text_width(text);
                    if (tw > 20 && cond == 100u) { text[3] = 0; tw = br_widget_text_width(text); }   /* 100% -> 100 */
                    br_widget_text(x + (26 - tw) / 2, 12, text, C_WHITE);
                    if (note != 60u) {
                        text[0] = note > 60u ? '+' : 0; text[1] = 0;
                        br_number(text + (note > 60u), (int32_t)note - 60);
                        br_widget_text(x + (26 - br_widget_text_width(text)) / 2, 19, text, C_WHITE);
                    }
                }
                sq_marks(x, 26, f);
                cv_blit((uint32_t)x0, (uint32_t)(y + ly));
            }
        } else {
            for (uint32_t g = 0; g < 4u; ++g) {
                uint32_t i, play = 0u, rows;
                h = (p * 5u + lane) * 31u + br_ui_accent;
                for (i = 0; i < 4u; ++i) {
                    uint32_t step = base + g * 4u + i, word = br_part[p].step[step];
                    h = h * 37u + (step < length) + ((BR_STEP_HIT(word) >> lane) & 1u) * 2u +
                        ((BR_STEP_ACC(word) >> lane) & 1u) * 4u + step * 16u;
                    if (br_playing && step == br_step_pos[p]) play = i + 1u;
                }
                if (!(rows = br_cell_rows(&group_old[lane][g], &br_group_cap[lane][g], play, h, 10u, force))) continue;
                cv_begin(48, rows, BR_BACKGROUND);
                for (i = 0; i < 4u; ++i) {
                    uint32_t step = base + g * 4u + i, word = br_part[p].step[step];
                    uint32_t hit = step < length && ((BR_STEP_HIT(word) >> lane) & 1u);
                    int32_t x = (int32_t)i * 11;
                    hd_rrect(x, 1, 9, 8, 2, step >= length ? RGB(16, 19, 20) : hit ? BR_ACC : BR_LINE);
                    if (hit && ((BR_STEP_ACC(word) >> lane) & 1u)) hd_rrect(x + 3, 4, 3, 2, 1, C_WHITE);
                    if (br_playing && step == br_step_pos[p]) hd_rrect(x, 0, 9, 2, 1, C_WHITE);
                }
                cv_blit(44u + g * 48u, (uint32_t)y);
            }
        }
        y += lh + 2;
    }
    {
        static uint32_t controls_old[4];
        uint32_t k;
        for (k = 0; k < 4u; ++k) {
            br_control_t c = br_control(k);
            char value[24];
            br_control_text(k, &c, value);
            h = br_hash_text(value) ^ br_hash_text(c.label) ^ br_ui.part ^
                br_ui.cursor << 8 ^ br_ui.lane << 16 ^ (k == br_active_knob() ? 0x80000000u : 0u);
            if (br_region(&controls_old[k], h * 131u + (uint32_t)c.value + ((uint32_t)c.low << 12) + ((uint32_t)c.high << 20), force)) {
                br_draw_seq_control(k, &c, value);
                cv_blit(4u + k * 60u, 158u);
            }
        }
    }
    br_draw_status(force);
#else
    static uint32_t ruler_old, row_old[BR_DRUM_LANES];
    uint32_t p = br_ui.part, length = br_part[p].length, lane, step, cell, h;
    uint32_t base = br_ui.step_bank * BR_BANK_STEPS;
    uint32_t retrig = br_retrig_page();
    uint32_t editing = retrig ? br_retrig_editing() : br_ui.step_edit_active;
    uint32_t edit_step = retrig ? br_ui.retrig_step : br_ui.step_press_step;
    h = edit_step ^ editing << 7 ^ length << 8 ^ br_playing << 16 ^ br_step_pos[p] << 20 ^ base << 24;
    if (br_region(&ruler_old, h, force)) {
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
            if (br_playing && step == br_step_pos[p]) hd_rrect(x + 1, 10, 10, 2, 1, C_WHITE);
            if (editing && step == edit_step) hd_rrect(x + 1, 12, 10, 2, 1, BR_ACC);
        }
        cv_blit(0, 36);
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
        if (!br_region(&row_old[lane], h, force)) continue;
        (void)background;
        cv_begin(240, 18, BR_BACKGROUND);
        if (selected) {                 /* a soft band, the lane accent as a small pill at its left */
            hd_rrect_grad(0, 1, 240, 16, 5, RGB(30, 36, 40), RGB(13, 16, 18));
            hd_rrect(0, 3, 2, 12, 1, BR_ACC);
        }
        br_widget_text(6, 5,
                       br_drum_voice_label(br_part[p].sound.drum_voice_mask, lane),
                       selected ? C_WHITE : BR_DIM);
        for (cell = 0; cell < BR_BANK_STEPS; cell++) {
            step = base + cell;
            int32_t x = 44 + (int32_t)cell * 12;
            uint32_t word = br_part[p].step[step];
            uint32_t hit = (BR_STEP_HIT(word) >> lane) & 1u;
            uint32_t coverage = step < length ? br_step_length_coverage(p, step, lane, 1u) : 0u;
            uint32_t edit = selected && editing && step == edit_step;
            if (edit) {                 /* the held step: a bright rounded ring with a softer inner edge */
                hd_rrect_frame(x - 2, 0, 12, 18, 3, C_WHITE);
                hd_rrect_frame(x - 1, 1, 10, 16, 2, RGB(176, 182, 184));
            }
            if (step >= length) hd_rrect(x, 4, 8, 10, 2, RGB(16, 19, 20));
            else if (hit && !coverage) hd_bevel(x, 4, 8, 10, 2, BR_ACC);
            else if (!edit || hit) hd_rrect_grad(x, 4, 8, 10, 2, RGB(54, 62, 66), RGB(36, 42, 45));
            if (coverage) hd_bevel(x, 4, (int32_t)((coverage * 8u + 15u) / 16u), 10, 2, hit ? BR_ACC : BR_LENGTH_BG);
            if (step < length && hit && ((BR_STEP_ACC(word) >> lane) & 1u))
                hd_circle_q8((x + 4) * 256, 9 * 256, 2 * 256, 0, C_WHITE);
            if (retrig && step < length && hit && (br_seq_kit_retrig[step][lane] & 7u)) {
                hd_rrect(x + 1, 5, 2, 8, 1, C_WHITE);
                hd_rrect(x + 5, 5, 2, 8, 1, C_WHITE);
            }
            if (step < length && hit) {
                if (br_step_note_get(p, step, lane) != 60u) hd_rrect(x + 1, 1, 2, 2, 1, C_WHITE);
                if (br_step_condition_get(p, step, lane) != BR_STEP_COND_ALWAYS) hd_rrect(x + 5, 1, 2, 2, 1, BR_ACC);
            }
        }
        cv_blit(0, 50 + lane * 18u);
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
            if (br_region(&controls_old[k], hash * 131u + (uint32_t)c.value + ((uint32_t)c.low << 12) + ((uint32_t)c.high << 20), force)) {
                br_draw_seq_control(k, &c, value);
                cv_blit(4u + k * 60u, 158u);
            }
        }
    }
    br_draw_status(force);
#endif
}

/* The four kit lanes share the kit colour and line up with the four knobs. */
static void br_mixer_name(const char *name, char first[7], char second[7]);
static uint32_t br_meter_segments(uint32_t peak, uint32_t count);
/* Channel strips (kit levels, mixer, FX sends). ox shifts the strip left so a narrow canvas can repaint just
 * the meter columns with the very same drawing calls; keep ox a multiple of 4 so the dither lines up. */
static void hds_panel(int32_t ox, int32_t h)            /* the selected strip: a softly lit rounded panel */
{
    hd_rrect_grad(-ox, 0, 54, h, 6, RGB(28, 33, 37), RGB(11, 14, 16));
    hd_rrect_frame(-ox, 0, 54, h, 6, RGB(52, 60, 64));
}
static void hds_housing(int32_t x, int32_t y, int32_t w, int32_t h)
{
    hd_rrect_frame(x, y, w, h, 6, RGB(40, 46, 49));
    hd_rrect_grad(x + 1, y + 1, w - 2, h - 2, 5, RGB(14, 17, 19), RGB(5, 6, 7));
}
/* Fine scale marks either side of a slot: majors at quarters, minors between. */
static void hds_ticks(int32_t left, int32_t right, int32_t top, int32_t span)
{
    for (int32_t i = 0; i <= 8; ++i) {
        int32_t y = top + i * span / 8, n = i & 1 ? 1 : 3;
        uint16_t c = i & 1 ? RGB(46, 53, 56) : RGB(88, 98, 102);
        hd_line(left - n, y, left, y, c);
        hd_line(right, y, right + n, y, c);
    }
}
/* Fader cap with a soft drop shadow and a centre groove. */
static void hds_cap(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t box[4] = {x, y, x + w - 1, y + h};      /* (the shadow's row too, for the mark: hd__clip records it) */
    hd__clip(box);
    for (int32_t i = 1; i < w - 1; ++i) hd_blend(x + i, y + h, C_BLACK, 130u);
    hd_bevel(x, y, w, h, 2, c);
    hd_line(x + 3, y + (h - 1) / 2, x + w - 4, y + (h - 1) / 2, hd_darken(c, 120u));
}
/* Level colour, t from 0 (bottom) to 256 (top): green, amber, then red. */
static uint16_t hds_meter_color(int32_t t)
{
    return t < 160 ? hd_mix(RGB(40, 200, 96), RGB(240, 184, 32), (uint32_t)(t * 255 / 160)) :
        hd_mix(RGB(240, 184, 32), RGB(240, 56, 44), (uint32_t)((t - 160) * 255 / 96));
}
/* Housing, ticks, recessed slot, meter (lit rows from the bottom, the rest a dark ghost of the scale) and
 * cap for a 54-wide strip; meter rows top .. top + len - 1, cap's first row at cap. */
static void hds_fader(int32_t ox, int32_t frame_y, int32_t frame_h, int32_t top, int32_t len, int32_t cap,
                      int32_t lit, uint32_t selected, uint32_t dim, uint16_t cap_color)
{
    if (selected) hds_panel(ox, 176);
    hds_housing(8 - ox, frame_y, 38, frame_h);
    hds_ticks(20 - ox, 33 - ox, top, len - 2);
    hds_well(24 - ox, top - 2, 6, len + 4, 3);
    int32_t y0 = top > -cv_oy ? top : -cv_oy, y1 = (int32_t)cv_h - cv_oy;      /* this strip's meter rows */
    if (y1 > top + len) y1 = top + len;
    for (int32_t y = y0; y < y1; ++y) {
        int32_t t = (top + len - 1 - y) * 256 / len;
        uint16_t c = dim ? hd_mix(RGB(64, 70, 74), RGB(150, 158, 162), (uint32_t)t) : hds_meter_color(t);
        if (y < top + len - lit) c = hd_darken(c, 218u);
        for (int32_t x = 25; x < 29; ++x) hd_blend(x - ox, y, x == 25 || x == 28 ? hd_darken(c, 64u) : c, 255u);
    }
    BW_MARK(hds_cap(16 - ox, cap, 22, 6, cap_color));   /* the cap moves with the level: a mark */
}
/* A strip's meter after its level moved from was (-1: unknown) to lit rows: only the 4 lit columns and only
 * the rows that changed, repainted with the strip's own drawing (from column 24 where the window meets the cap
 * or its shadow, so the cap's dither keeps the strip's 4-pixel phase). sx: the strip's left on screen. */
static void hds_meter(uint32_t sx, int32_t frame_y, int32_t frame_h, int32_t top, int32_t len, int32_t at,
                      int32_t was, int32_t lit, uint32_t selected, uint32_t dim, uint16_t cap_color)
{
    int32_t r0 = 0, r1 = len, ox;
    if (was >= 0) { r0 = len - (was > lit ? was : lit); r1 = len - (was < lit ? was : lit); }
    if (r1 <= r0) return;
    ox = r0 <= at + 6 && r1 > at ? 24 : 25;
    cv_begin((uint32_t)(29 - ox), (uint32_t)(r1 - r0), BR_BACKGROUND);
    cv_oy = -(top + r0);
    hds_fader(ox, frame_y, frame_h, top, len, top + at, lit, selected, dim, cap_color);
    cv_oy = 0;
    cv_blit(sx + (uint32_t)ox, 34u + (uint32_t)(top + r0));
}
/* MUTE and SOLO chips under a strip; their letters are drawn by the caller. */
static void hds_mute_solo(uint32_t muted, uint32_t solo)
{
    if (muted) hd_bevel(8, 158, 15, 12, 3, RGB(16, 144, 132));
    else hd_rrect_frame(8, 158, 15, 12, 3, RGB(36, 42, 45));
    if (solo) hd_bevel(32, 158, 15, 12, 3, BR_GLOBAL_ACC);
    else hd_rrect_frame(32, 158, 15, 12, 3, RGB(36, 42, 45));
}
/* A mixer or drum-mixer strip's art (54 x 176): the fader (its cap a mark: a level step redraws the cap's
 * old and new boxes, br_anim), mute and solo, and the badge. */
struct br_strip_view { uint32_t p, lane, muted, solo, dim, selected; int32_t cap; uint16_t color; };
static void br_level_strip_art(void)
{
    const struct br_strip_view *v = br_view;
    uint32_t drum = v->lane < BR_DRUM_LANES;
    hds_fader(0, drum ? 42 : 32, drum ? 114 : 124, drum ? 46 : 36, drum ? 106 : 114, (drum ? 46 : 36) + v->cap, 0,
              v->selected, v->dim, v->dim ? BR_DIM : v->selected ? C_WHITE : v->color);
    hds_mute_solo(v->muted, v->solo);
    if (drum) {
        const char *name = br_drum_voice_name[br_drum_voice_id(br_part[BR_DRUM_PART].sound.drum_voice_mask, v->lane)];
        char badge[3] = {'D', (char)('1' + v->lane), 0};
        hdp_badge(12, 6, 30, v->color, badge);   /* the header badge's family */
        br_widget_text(27 - br_widget_text_width(name) / 2, 30, name, v->selected ? C_WHITE : BR_DIM);
    } else br_track_badge(12, 6, v->p, 0);
    br_widget_text(8 + (15 - br_widget_text_width("M")) / 2, 161, "M", v->muted ? C_BLACK : BR_DIM);
    br_widget_text(32 + (15 - br_widget_text_width("S")) / 2, 161, "S", v->solo ? C_BLACK : BR_DIM);
}
/* Strip i (54 x 176 at x 4 + 60 i, row 34) from the view: in 40-row bands when its hash (all but the level)
 * changes, else the cap's boxes; 1 when the strip was drawn (its meter is then repainted whole). */
static uint32_t br_level_strip(uint32_t i, uint32_t *old, uint8_t *next, uint32_t h, uint32_t level, uint32_t force)
{
    uint32_t drawn = 0u;
    br_art = br_level_strip_art;
    if (!br_anim_update(&br_anim[i], h, level, 54, 176)) return 0u;
    for (uint32_t offset = 0; offset < 176u; offset += 40u) {
        uint32_t rows = 176u - offset;
        if (!br_band(old, next, h, offset / 40u, force)) continue;
        cv_begin(60, rows < 40u ? rows : 40u, BR_BACKGROUND);   /* (with the gaps either side of the strip) */
        cv_oy = -(int32_t)offset; cv_ox = -4;
        br_level_strip_art();   /* (the slot's meter is drawn unlit; the caller repaints it) */
        cv_oy = 0; cv_ox = 0;
        cv_blit(i * 60u, 34u + offset);
        drawn = 1u;
    }
    if (*next >= 5u) drawn |= br_anim_paint(&br_anim[i], 4u + i * 60u, 34u, 0, 0u);
    return drawn;
}
static void br_draw_drum_levels(uint32_t force)
{
    static uint32_t strip_old[BR_DRUM_LANES];
    static uint8_t strip_next[BR_DRUM_LANES];
    static uint8_t meter_hold[BR_DRUM_LANES];
    static uint32_t meter_old[BR_DRUM_LANES] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    static uint32_t meter_epoch, meter_ms;
    uint32_t epoch = br_audio_kit_meter_epoch(), peaks[BR_DRUM_LANES], fall = br_meter_fall(&meter_ms);
    uint32_t p = BR_DRUM_PART, redrawn = 0u;
    /* Discard a snapshot interrupted by a new audio publication. */
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
        peaks[lane] = br_audio_kit_meter_read(lane);
    if (epoch != br_audio_kit_meter_epoch()) epoch = meter_epoch;
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane) {
        uint32_t value = (uint32_t)br_clip((int32_t)br_part[p].lane_level[lane], 0, 127);
        struct br_strip_view v;
        v.p = p; v.lane = lane; v.selected = lane == br_ui.lane;
        v.muted = (br_kit_mute_mask >> lane) & 1u; v.solo = (br_kit_solo_mask >> lane) & 1u;
        v.dim = v.muted || (br_kit_solo_mask && !v.solo) || br_part[p].sound.mute || (br_solo_mask && !((br_solo_mask >> p) & 1u));
        v.color = v.dim ? BR_DIM : br_track_color(p);
        v.cap = br_clip(103 - (int32_t)(value * 104u / 127u), 0, 100);
        uint32_t h = (((((br_drum_voice_id(br_part[p].sound.drum_voice_mask, lane) * 31u + v.selected) * 31u + p) * 3u + v.muted) * 3u
                      + v.solo) * 3u + v.dim) * 31u + 0x4b4954u;
        br_view = &v;
        if (br_level_strip(lane, &strip_old[lane], &strip_next[lane], h, value, force)) redrawn |= 1u << lane;
    }
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane) {
        uint32_t value = (uint32_t)br_clip((int32_t)br_part[p].lane_level[lane], 0, 127);
        uint32_t now = epoch != meter_epoch ? br_meter_segments(peaks[lane], 28u) : 0u;
        uint32_t lit;
        if (now >= meter_hold[lane]) meter_hold[lane] = (uint8_t)now;
        else meter_hold[lane] = (uint8_t)(meter_hold[lane] > fall ? meter_hold[lane] - fall : 0u);
        lit = (uint32_t)meter_hold[lane] * 106u / 28u;
        int32_t cap = br_clip((103 - (int32_t)(value * 104u / 127u)) & ~1, 0, 100);
        uint32_t dim = ((br_kit_mute_mask >> lane) & 1u) ||
            (br_kit_solo_mask && !((br_kit_solo_mask >> lane) & 1u)) || br_part[p].sound.mute || (br_solo_mask && !((br_solo_mask >> p) & 1u));
        uint16_t color = dim ? BR_DIM : br_track_color(p);
        uint32_t key = (lit << 24) | (value << 17) | ((uint32_t)color << 1) | (lane == br_ui.lane);
        if ((redrawn >> lane) & 1u) meter_old[lane] = UINT32_MAX;   /* (UINT32_MAX: repaint it whole) */
        if (strip_next[lane] < 5u || (!force && key == meter_old[lane]) || !br_open()) continue;
        {   /* only the rows between the old and the new lit height (a strip just drawn shows it unlit) */
            int32_t was = force ? 0 :
                (key & 0xffffffu) == (meter_old[lane] & 0xffffffu) ? (int32_t)(meter_old[lane] >> 24) : -1;
            meter_old[lane] = key;
            hds_meter(4u + lane * 60u, 42, 114, 46, 106, br_clip(103 - (int32_t)(value * 104u / 127u), 0, 100), was,
                      (int32_t)lit, lane == br_ui.lane, dim, dim ? BR_DIM : lane == br_ui.lane ? C_WHITE : color);
            continue;
        }
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
static uint32_t br_meter_ms;                                   /* the mixer meters' fall clock */
/* The fader's own centre line is the level meter: lit from the bottom by the track's signal (-60 to 0 dBFS
 * over the fader's travel), red in the top step. The fader cap is drawn back over it. */
static void br_draw_meter_line(uint32_t p, uint32_t force, uint32_t fall)
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
    else {
        uint32_t decayed = br_meter_hold[p] > fall ? br_meter_hold[p] - fall : 0u;
        br_meter_hold[p] = (uint8_t)(decayed > now ? decayed : now);
    }
    if ((!force && br_meter_hold[p] == br_meter_drawn[p]) || !br_open()) return;
    /* force: br_draw_mixer has just drawn this strip, meter unlit; br_meter_drawn 255: repaint it all */
    int32_t was = force ? 0 : br_meter_drawn[p] <= BR_METER_STEPS ? (int32_t)br_meter_drawn[p] * rows / (int32_t)BR_METER_STEPS : -1;
    br_meter_drawn[p] = br_meter_hold[p];
    lit = (int32_t)br_meter_hold[p] * rows / (int32_t)BR_METER_STEPS;
    red = br_meter_hold[p] >= BR_METER_STEPS ? rows / (int32_t)BR_METER_STEPS : 0;
    cap = br_clip((111 - fader) & ~1, 0, rows - 6);                               /* the cap's first row, in line rows */
    (void)red; (void)cap;
    hds_meter(4u + p * 60u, 32, 124, 36, rows, br_clip(111 - fader, 0, rows - 6), was, lit, selected, dim,
              dim ? BR_DIM : selected ? C_WHITE : br_track_color(p));
    return;
    cv_begin(2, (uint32_t)rows, BR_LINE);
    if (lit > 0) cv_rect(0, rows - lit, 2, lit, color);
    if (red > 0) cv_rect(0, rows - lit, 2, red, RGB(240, 60, 48));
    cv_rect(0, cap, 2, 6, dim ? BR_DIM : selected ? C_WHITE : br_track_color(p));
    cv_blit(4u + p * 60u + 26u, 34u + 36u);
}

static void br_draw_mixer(uint32_t force)
{
    static uint32_t channel_old[BR_PARTS];
    static uint8_t channel_next[BR_PARTS];
    uint32_t p, h, redrawn = 0u;
    for (p = 0; p < BR_PARTS; ++p) {
        const volatile br_sound_t *s = &br_part[p].sound;
        uint32_t level = (uint32_t)br_clip((int32_t)s->level, 0, 127);
        struct br_strip_view v;
        v.p = p; v.lane = BR_DRUM_LANES; v.selected = p == br_ui.part; v.muted = s->mute != 0; v.solo = (br_solo_mask >> p) & 1u;
        v.dim = v.muted || (br_solo_mask && !v.solo); v.color = br_track_color(p);
        v.cap = br_clip(111 - (int32_t)(level * 112u / 127u), 0, 108);
        h = ((((v.muted * 31u + v.selected) * 31u + v.solo) * 31u + v.dim) * 31u + p) * 31u + 0x4d4958u;
        br_view = &v;
        if (br_level_strip(p, &channel_old[p], &channel_next[p], h, level, force)) {
            br_meter_drawn[p] = 255u;                          /* the meter is repainted whole over it */
            if (force) redrawn |= 1u << p;
        }
    }
    h = br_meter_fall(&br_meter_ms);
    for (p = 0; p < BR_PARTS; ++p)
        if (channel_next[p] >= 5u) br_draw_meter_line(p, (redrawn >> p) & 1u, h);   /* over a strip that is all out */
    br_draw_status(force);
}

/* FX send screen: the mixer's four strips, with a colour bar for each of the
 * current track's drive, delay, modulation and reverb levels. Each strip has an
 * effect icon where the mixer shows T1..DK and its name under the bar, where the mixer has M and S.
 * Drive, delay and modulation take the current track's colour; reverb is global and keeps the
 * global (goldenrod) colour. */
/* Kit strips retain the full height; FX sends leave room for the shortcut cue. */
#define BR_FX_STRIP_H 192u
#define BR_FX_SEND_STRIP_H 178u
/* A beveled tile with smooth black art: a filled bolt, rounded bars, a sine,
 * three round-topped arches. */
static void br_fx_icon(int32_t x, int32_t y, uint32_t k, uint16_t color)
{
    int32_t px[13], py[13];
    uint32_t i;
    hd_bevel(x, y, 30, 20, 5, color);
    if (k == 0u) {                      /* drive: a slanted band and a dart overlap into the bolt */
        int32_t ax[4] = {x + 15, x + 21, x + 15, x + 8}, ay[4] = {y + 2, y + 2, y + 11, y + 11};
        int32_t bx[4] = {x + 13, x + 22, x + 10, x + 10}, by[4] = {y + 8, y + 8, y + 18, y + 18};
        hd_poly4(ax, ay, C_BLACK);
        hd_poly4(bx, by, C_BLACK);
    } else if (k == 1u) {               /* delay: bars mirrored about the centre */
        static const uint8_t height[5] = {6, 10, 14, 10, 6};
        for (i = 0u; i < 5u; ++i) hd_rrect(x + 5 + (int32_t)i * 4, y + 17 - height[i], 3, height[i], 1, C_BLACK);
    } else if (k == 2u) {               /* modulation: one and a quarter sine cycles */
        for (i = 0u; i <= 12u; ++i) {
            px[i] = bw_q8(x + 4) + (int32_t)i * 22 * 256 / 12;
            py[i] = bw_q8(y + 10) - bw_sine(i * 6827u) * 6 / 64;
        }
        bw_path(px, py, 13u, 512u, C_BLACK);
    } else {                            /* reverb: nested round arches, a hall receding */
        for (i = 0u; i < 3u; ++i) {
            int32_t hw = (11 - 4 * (int32_t)i) * 256 - 128, top = 2 + 4 * (int32_t)i, r = i == 2u ? 2 : 4, n = 1;
            int32_t cx = (x + 15) * 256, cyq = bw_q8(y + top + r), side = hw - r * 256;
            px[0] = cx - hw; py[0] = bw_q8(y + 17);
            for (int32_t a = 0; a < 10; ++a) {   /* two rounded shoulders, flat between */
                uint32_t ph = 49152u + (uint32_t)a * 4096u - (a > 4 ? 4096u : 0u);
                px[n] = cx + (a < 5 ? -side : side) + bw_sine(ph) * r / 64;
                py[n++] = cyq - bw_sine(ph + 16384u) * r / 64;
            }
            px[n] = cx + hw; py[n++] = bw_q8(y + 17);
            bw_path(px, py, (uint32_t)n, 448u, C_BLACK);
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
    static uint8_t strip_next[4];
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
        for (offset = 0; offset < BR_FX_SEND_STRIP_H; offset += 40u) {
            uint32_t tick, rows = BR_FX_SEND_STRIP_H - offset;
            if (!br_band(&strip_old[k], &strip_next[k], h, offset / 40u, force)) continue;
            int32_t cap = top + 1 + track_h - height;
            (void)tick;
            cv_begin(54, rows < 40u ? rows : 40u, BR_BACKGROUND);
            cv_oy = -(int32_t)offset;
            if (active) {
                /* Shift only the selected background down: less space above
                 * the fader and more below the effect icon. */
                cv_oy += 2;
                hds_panel(0, (int32_t)BR_FX_SEND_STRIP_H - 2);
                cv_oy -= 2;
            }
            hds_housing(8, top, 38, frame_h);
            hds_well(16, top + 4, 22, track_h, 4);
            if (height > 1)              /* the level: a shaded column in the send's colour */
                hd_rrect_grad(17, top + 4 + track_h - height, 20, height - 1, 3, hd_lighten(color, 60u), hd_darken(color, 80u));
            hds_ticks(13, 41, top + 4, travel);
            hds_cap(14, cap, 26, 5, RGB(232, 236, 238));
            br_draw_route_badges(27, top + frame_h, routes, p, bg);
            {
                static const char *const send_name[4] = {"DRIVE", "DELAY", "MOD", "REVERB"};
                br_widget_text(27 - br_widget_text_width(send_name[k]) / 2, label_y, send_name[k], active ? C_WHITE : BR_DIM);
            }
            br_fx_icon(12, icon_y, k, color);
            cv_oy = 0;
            cv_blit(4u + k * 60u, 34u + offset);
            continue;
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
    }
    br_draw_status(force);
}

static void br_draw_tonal_steps(uint32_t force)
{
#if BR_SEQ_2X8
    static uint32_t old_cell[BR_BANK_STEPS], old_control[4], old_chip;
    uint32_t p = br_ui.part, base = br_ui.step_bank * BR_BANK_STEPS, length = br_part[p].length, cell, k, h;
    for (cell = 0; cell < BR_BANK_STEPS; ++cell) {
        uint32_t at = base + cell, col = cell & 7u, word = br_part[p].step[at], next = at + 1u;
        uint32_t enabled = at < length, on = enabled && (word & BR_STEP_ON);
        uint32_t cover = enabled ? br_step_length_coverage(p, at, 0u, 0u) : 0u;
        uint32_t f = enabled | on << 1 | (br_ui.step_edit_active && at == br_ui.step_press_step) << 2 |
                     (br_playing && at == br_step_pos[p]) << 3;
        int32_t w = sq_col_w(col);
        if (cover == 16u && col != 7u && next < length && !(br_part[p].step[next] & BR_STEP_ON) &&
            br_step_length_coverage(p, next, 0u, 0u)) f |= SQ_BRIDGE;
        if (on && (word & BR_STEP_TIE)) f |= SQ_TIE;
        if (on && br_step_condition_get(p, at, 0u) != BR_STEP_COND_ALWAYS) f |= SQ_COND;
        h = ((word * 31u + at) * 257u + (f & ~SQ_PLAY)) * 31u + cover;
        h = h * 31u + p;
        if (!(k = br_cell_rows(&old_cell[cell], &br_cell_cap[cell], f & SQ_PLAY, h, 40u, force))) continue;
        cv_begin((uint32_t)w, k, BR_BACKGROUND);
        sq_cell(0, w, 37, on ? hd_mix(BR_CARD, BR_ACC, 84u) : BR_CARD, f, on ? BR_STEP_VEL(word) : 0u, cover, 31, 34);
        if (enabled && !(cell & 3u)) {          /* beat starts only: 1, 5, 9, 13 (17, 21 ... in later banks) */
            char number[8];
            br_number(number, (int32_t)at + 1);
            br_widget_text(4, 6, number, BR_DIM);
        }
        if (on) {                                /* the letter large; sharp above the octave, small, beside it */
            char note[16], letter[2] = {0, 0};
            const char *octave;
            int32_t lw, sw, x;
            br_note_text(note, (int32_t)BR_STEP_NOTE(word));
            letter[0] = note[0];
            octave = note + 1 + (note[1] == '#');
            lw = 2 * br_widget_text_width(letter);
            sw = br_widget_text_width(octave);
            if (note[1] == '#' && br_widget_text_width("#") > sw) sw = br_widget_text_width("#");
            x = (26 - lw - 1 - sw) / 2;
            br_widget_text_scaled(x, 14, letter, C_WHITE, 2);
            if (note[1] == '#') br_widget_text(x + lw + 1, 14, "#", C_WHITE);
            br_widget_text(x + lw + 1, 21, octave, C_WHITE);
            if (f & SQ_COND) hd_rrect(19, 6, 4, 4, 1, C_WHITE);
            if (f & SQ_TIE) hd_rrect(20, 34, (col == 7u ? 26 : w) - 20, 3, 1, C_WHITE);
        }
        sq_marks(0, 37, f);
        cv_blit((uint32_t)sq_col_x(col), 36u + (cell >> 3) * 46u);
    }
    {   /* Caption: what the knobs edit (new steps, or the held step), and whether the track is muted or armed. */
        uint32_t held = br_ui.step_edit_active, muted = br_part[p].sound.mute != 0u;
        char step[12], number[8];
        const char *text[3] = {0, 0, 0};
        uint16_t face[3] = {BR_ACC, BR_LINE, BR_LINE};
        h = (((held ? br_ui.step_press_step + 1u : 0u) * 2u + muted) * 2u + br_recording) * 8u + p;
        if (br_region(&old_chip, h, force)) {
            k = 0u;
            if (held) {
                br_text_copy(step, "STEP ", sizeof step); br_number(number, (int32_t)br_ui.step_press_step + 1);
                br_text_append(step, number, sizeof step);
                text[k] = step; face[k++] = C_WHITE;
            } else text[k++] = "NEW STEPS";
            if (muted) { text[k] = "MUTED"; face[k++] = BR_LINE; }
            if (br_recording) { text[k] = "REC"; face[k] = RGB(220, 40, 40); }
            cv_begin(240, 36, BR_BACKGROUND);
#if BR_PRODUCT_SMK37
            {   /* SMK-37: the chips sit higher; under them, which controls set pitch and which set steps. */
                const char *hint = "KEYS SET PITCH   PADS SET STEPS";
                sq_chips(4, text, face);
                br_widget_text((240 - br_widget_text_width(hint)) / 2, 27, hint, BR_DIM);
            }
#else
            sq_chips(14, text, face);
#endif
            cv_blit(0, 122);
        }
    }
    for (k = 0; k < 4u; ++k) {
        br_control_t c = br_control(k);
        char value[24];
        br_control_text(k, &c, value);
        h = br_hash_text(value) ^ br_hash_text(c.label) ^ p ^
            br_ui.cursor << 8 ^ br_ui.lane << 16 ^ (k == br_active_knob() ? 0x80000000u : 0u);
        if (!br_region(&old_control[k], h * 131u + (uint32_t)c.value + ((uint32_t)c.low << 12) + ((uint32_t)c.high << 20), force)) continue;
        br_draw_seq_control(k, &c, value);
        cv_blit(4u + k * 60u, 158);
    }
    br_draw_status(force);
#else
    static uint32_t old_cell[BR_BANK_STEPS], old_control[4], old_hint;
    uint32_t p = br_ui.part, base = br_ui.step_bank * BR_BANK_STEPS, cell, k;
#if BR_PRODUCT_SMK37
    const uint32_t columns = 8u, card_width = 26u, pitch = 30u, row_pitch = 36u;
    if (br_region(&old_hint, 1u, force)) {
        cv_begin(240, 22, BR_BACKGROUND);
        const char *hint = "KEYS SET PITCH   PADS SET STEPS";
        br_widget_text((240 - br_widget_text_width(hint)) / 2, 8, hint, BR_DIM);
        cv_blit(0, 125);
    }
#endif
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
        if (!br_region(&old_cell[cell], h, force)) continue;
        /* Rounded cells: lit in a deep shade of the track colour when on, the held length as a brighter band,
         * quiet when empty, only an outline past the pattern's end; playhead and held step get a bright ring. */
        (void)bg;
        cv_begin(54, 28, BR_BACKGROUND);
        if (active) hd__rrect(0, 0, 53, 27, 5, hd_darken(BR_ACC, 170u), hd_darken(BR_ACC, 214u),
                              hd_darken(BR_ACC, 100u), hd_darken(BR_ACC, 226u), 0);
        else if (enabled) hd__rrect(0, 0, 53, 27, 5, RGB(14, 17, 19), RGB(14, 17, 19), RGB(30, 35, 38), RGB(8, 10, 11), 0);
        else hd_rrect_frame(0, 0, 53, 27, 5, RGB(24, 28, 30));
        if (coverage) {
            width = (coverage * 51u + 15u) / 16u;
            hd__rrect(1, 1, (int32_t)width, 25, 4, hd_darken(BR_ACC, 120u), hd_darken(BR_ACC, 176u),
                      hd_darken(BR_ACC, 60u), hd_darken(BR_ACC, 196u), 0);
        }
        br_number(number, (int32_t)at + 1);
        br_widget_text(3, 2, number, enabled ? BR_DIM : BR_LINE);
        br_note_text(note, active ? (int32_t)BR_STEP_NOTE(word) : -1);
        if (active) {
            br_widget_text((54 - br_widget_text_width(note)) / 2, 13, note, C_WHITE);
            hd_rrect(3, 23, (int32_t)(BR_STEP_VEL(word) * 47u / 127u) + 1, 2, 1, hd_lighten(BR_ACC, 80u));
            if (condition != BR_STEP_COND_ALWAYS) hd_circle_q8(48 * 256, 5 * 256, 2 * 256, 0, hd_lighten(BR_ACC, 60u));
            if (word & BR_STEP_TIE) hd_line(41, 4, 50, 4, hd_lighten(BR_ACC, 60u));
        } else br_widget_text(24, 16, "-", enabled ? BR_DIM : BR_LINE);
        if (selected || playhead) {
            uint16_t edge = playhead ? hd_lighten(BR_ACC, 40u) : C_WHITE;
            hd_rrect_frame(0, 0, 53, 27, 5, edge);
            hd_rrect_frame(1, 1, 51, 25, 4, hd_darken(edge, 110u));
        }
        cv_blit(4u + (cell & 3u) * 60u, 42u + (cell >> 2) * 32u);
        continue;
#if BR_PRODUCT_SMK37
        cv_begin(card_width, 28, bg);
        if (coverage) {
            width = (coverage * (card_width - 2u) + 15u) / 16u;
            cv_rect(1, 1, (int32_t)width, 25, BR_LENGTH_BG);
        }
        br_number(number, (int32_t)at + 1);
        br_widget_text(3, 2, number, enabled ? BR_DIM : BR_LINE);
        br_note_text(note, active ? (int32_t)BR_STEP_NOTE(word) : -1);
        if (active) {
            br_widget_text(((int32_t)card_width - br_widget_text_width(note)) / 2, 13, note, C_WHITE);
            br_art_rect(3, 25, (int32_t)(BR_STEP_VEL(word) * (card_width - 6u) / 127u), 1, BR_DIM);
            if (condition != BR_STEP_COND_ALWAYS) br_art_rect((int32_t)card_width - 8, 3, 3, 3, BR_ACC);
            if (word & BR_STEP_TIE) br_art_line((int32_t)card_width - 11, 4, (int32_t)card_width - 4, 4, BR_ACC);
        } else br_widget_text((int32_t)card_width / 2 - 3, 16, "-", enabled ? BR_DIM : BR_LINE);
        br_art_line(0, 27, (int32_t)card_width - 1, 27, BR_LINE);
        if (selected || playhead) {
            uint16_t edge = playhead ? BR_ACC : C_WHITE;
            br_art_line(0, 0, (int32_t)card_width - 1, 0, edge); br_art_line(0, 26, (int32_t)card_width - 1, 26, edge);
            br_art_line(0, 0, 0, 26, edge); br_art_line((int32_t)card_width - 2, 0, (int32_t)card_width - 2, 26, edge);
        }
        cv_blit(3u + cell % columns * pitch, 42u + cell / columns * row_pitch);
#else
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
#endif
        old_cell[cell] = h;
    }
    for (k = 0; k < 4u; ++k) {
        br_control_t c = br_control(k);
        char value[24];
        uint32_t h;
        br_control_text(k, &c, value);
        h = br_hash_text(value) ^ br_hash_text(c.label) ^ p ^
            br_ui.cursor << 8 ^ br_ui.lane << 16 ^ (k == br_active_knob() ? 0x80000000u : 0u);
        if (!br_region(&old_control[k], h * 131u + (uint32_t)c.value + ((uint32_t)c.low << 12) + ((uint32_t)c.high << 20), force)) continue;
        br_draw_seq_control(k, &c, value);
        cv_blit(4u + k * 60u, 166);
    }
    br_draw_status(force);
#endif
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
    hdc_chip(x, 0, w, 16, 5, color);
    br_widget_text(x + (w - br_widget_text_width(label)) / 2, 4, label, color == BR_LINE ? C_WHITE : C_BLACK);
    return;
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
#if BR_POLY_UI
    /* The effect that runs: a refused echo (polyphony delay loan) gets no ECHO badge. */
    uint32_t punch = br_ui.page == BR_PAGE_PUNCH && br_punch_ks.shift ? br_punch_effect : 0u;
#else
    uint32_t punch = br_ui.page == BR_PAGE_PUNCH && br_punch_ks.shift ? br_punch_ks.active : 0u;
#endif
    uint32_t override = br_env_direct_overridden(part) &&
        br_ui.page == BR_PAGE_ENV && screen == 2u &&
        br_mod_target_supported(br_part[part].sound.preset, br_part[part].mod.target);
    uint32_t scope = 0u;
    uint32_t home_voices = 0u;
#if BR_POLY_UI
    if (br_ui.page == BR_PAGE_HOME && part < BR_SYNTH_PARTS && br_poly_mode_active()) {
#if defined(BR_ADAPTIVE_POLY) && BR_ADAPTIVE_POLY
        uint32_t capacity[4]; br_poly_capacity_snapshot(capacity);
        home_voices = capacity[1];
#else
        home_voices = 6u;
#endif
    }
#endif
    uint32_t hash = (((bit + part * 7u) * 131u + br_ui.page) * 31u + screen) ^ br_badge_pulse_key(part, bits);
    hash = (((hash * 5u + punch) * 2u + override) * 2u + scope) * 17u + home_voices;
    if (br_ui.page == BR_PAGE_MIX && screen == 2u)
        hash = ((hash * 2u + br_midi_clock_enabled) * 2u + br_midi_clock_active) * 257u + br_midi_clock_bpm;
    if (br_retrig_page()) hash = ((hash * 67u + br_ui.cursor) * 5u + br_ui.lane) * 2u + br_retrig_editing();
    if (!br_region(&previous, hash, force)) return;
    cv_begin(240, 36, BR_BACKGROUND);
    cv_oy = br_retrig_page() ? 0 : 8;
    if (br_ui.page == BR_PAGE_HOME && part < BR_SYNTH_PARTS) {
        char label[24], number[8];
        br_text_copy(label, home_voices ? "POLY: " : "MONO", sizeof label);
        if (home_voices) {
            br_number(number, (int32_t)home_voices); br_text_append(label, number, sizeof label);
            br_text_append(label, " VOICES", sizeof label);
        }
        br_draw_page_badge(label, br_track_color(part));
    }
    if (br_ui.page == BR_PAGE_LFO && screen == BR_LFOS) br_draw_page_badge("GLOBAL LFO", BR_GLOBAL_ACC);
    if (override) br_draw_page_badge("PATCH OVERRIDE", BR_GLOBAL_ACC);
    if (bits) {
        uint16_t color = br_route_badge_tint(bit, part, BR_BACKGROUND);
        if (br_ui.page == BR_PAGE_ROUTE && bit >= 2u && bit < 4u) {
            uint32_t slot = bit - 2u;
            uint32_t brightness = (br_badge_pulse_key(part, 1u << bit) >> ((slot + 2u) * 7u)) & 127u;
            color = br_badge_blend(BR_BACKGROUND, br_track_color(part), brightness);
        }
        br_draw_named_badge_scaled(120, 1, br_route_badge_name[bit], color, 2);
    }
    if (br_ui.page == BR_PAGE_FX && screen != BR_FX_SENDS) {
        const char *label = screen == BR_FX_REVERB && !scope ? "GLOBAL" : "TRACK";
        uint16_t color = screen == BR_FX_REVERB && !scope ? BR_GLOBAL_ACC : br_track_color(part);
        br_draw_page_badge(label, color);
    }
    if (br_ui.page == BR_PAGE_MIX && screen == 2u) {
        char clock[24], number[12];
        if (br_midi_clock_active) {
            br_text_copy(clock, "MIDI ", sizeof clock); br_number(number, (int32_t)br_midi_clock_bpm);
            br_text_append(clock, number, sizeof clock); br_text_append(clock, " BPM", sizeof clock);
        } else br_text_copy(clock, br_midi_clock_enabled ? "CLOCK WAIT" : "CLOCK OFF", sizeof clock);
        br_draw_page_badge(clock, BR_GLOBAL_ACC);
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
    cv_oy = 0;
    cv_blit(0, 38);
}

/* Page position follows navigation order, including the reordered Global pages. */
static void br_draw_page_dots(uint32_t force)
{
    const uint8_t *ring = 0;
    uint32_t n = br_nav_ring(&ring), count = n ? n : br_screen_count(), h;
    uint32_t at = n ? br_nav_at(ring, n) : br_ui.screen[br_ui.page];
    if (count < 2u) return;
    h = ((br_ui.page * 31u + at) * 31u + count) * 31u + br_ui_accent;
    if (!br_region(&br_dots_old, h, force)) return;
    static uint32_t previous_width;
    uint32_t current_width = count * 8u + 8u;
    if (current_width > 240u) current_width = 240u;
    uint32_t width = current_width > previous_width ? current_width : previous_width;
    previous_width = current_width;
    int32_t left = (240 - (int32_t)width) / 2;
    cv_begin(width, 12, BR_BACKGROUND);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t x = 120 - left - (int32_t)(count * 8u - 4u) / 2 + (int32_t)i * 8;
        if (i == at) {   /* lit in the track colour with a faint halo */
            hd__radial((x + 2) * 256, 768, 1024, -1, 0u, 0u, hd_mix(BR_BACKGROUND, br_ui_accent, 110u));
            hd_circle_q8((x + 2) * 256, 768, 608, 0, br_ui_accent);
            hd_circle_q8((x + 2) * 256 - 160, 768 - 192, 192, 0, hd_lighten(br_ui_accent, 140u));
        } else hd_circle_q8((x + 2) * 256, 768, 512, 0, RGB(58, 66, 70));
    }
    cv_blit((uint32_t)left, 200);
}

static uint32_t br_draw_layout_old = 0xFFFFFFFFu;
/* Repaint only the incoming layout's uncovered background.
 * The header, controls and status are replaced by their normal canvas blits,
 * so they remain visible until their new pixels are ready. */
/* SEL sound bank: five rows; the highlighted sound is what the keys play. SEL again keeps it. */
static void br_sel_row_text(char *out, uint32_t n)
{
    uint32_t value = n + 1u;
    out[0] = (char)('0' + value / 100u); out[1] = (char)('0' + value / 10u % 10u);
    out[2] = (char)('0' + value % 10u); out[3] = ' ';
    br_text_copy(out + 4, br_sel_entry_name(br_ui.part, n), 12u);
}
/* A small dial: the chip that tells you which knob drives a list. Native 1x1 grid. */
static void br_knob_icon(int x, int y, uint16_t color)
{
    hdc_dial(x, y, color);
    return;
    br_shape_circle_grid(bw_q8(x + 5), bw_q8(y + 5), 5u * 256u, 256u, color, 0u, 1u);
    cv_rect(x + 5, y + 2, 1, 4, color);
}
/* List chrome shared with the SAVE / project menus: the knob chip, the list's left rule, the selected row. */
static void hds_chip(int32_t x, int32_t y, uint16_t line, uint16_t fill, uint32_t on)
{
    hd_rrect_frame(x, y, 64, 17, 5, line);
    if (on) hd_rrect_grad(x + 1, y + 1, 62, 15, 4, hd_lighten(fill, 60u), hd_darken(fill, 30u));
}
static void hds_rule(int32_t top, int32_t bottom)
{
    hd_line(3, top, 3, bottom, RGB(96, 104, 108));
}
/* The selected row meets the list rule at x=3 with a square left edge and rounded right end. */
static void hds_bar(int32_t y, uint16_t c)
{
    uint16_t top = hd_lighten(c, 56u), bot = hd_darken(c, 44u);
    uint16_t hi = hd_lighten(c, 140u), lo = hd_darken(c, 100u);
    hd__rrect(4, y - 3, 220, 20, 6, top, bot, hi, lo, 0);
    /* Fill the left corners using the identical gradient, including its edge lines. */
    hd__rrect(4, y - 3, 6, 20, 0, top, bot, hi, lo, 0);
}
/* The SOUNDS list (rows 34..213) in parts drawn from what each shows: the heading (rows 34..92, six bands),
 * each of the five rows (22 rows each from row 93, two bands) and the foot (203..213). A step of the
 * cursor redraws the two rows it leaves and reaches and the count, not the list. */
struct br_sel_view { char head[24], count[16], line[BR_SEL_ROWS][16]; uint32_t cur, top, p, chip; };
static void br_sel_art(void)
{
    const struct br_sel_view *v = br_view;
    /* The SAVE menu's knob chip: it says knob 1 scrolls this list, and fills while it does. */
    hds_chip(3, 73, C_WHITE, br_track_color(v->p), v->chip);
    hds_rule(93, 203);
    br_knob_icon(8, 76, v->chip ? C_BLACK : C_WHITE);
    br_widget_text(24, 78, BR_PANEL_KNOB1, v->chip ? C_BLACK : C_WHITE);
    br_widget_text(92, 78, v->head, BR_DIM);
    br_widget_text(232 - br_widget_text_width(v->count), 78, v->count, BR_DIM);
    for (uint32_t row = 0u; row < BR_SEL_ROWS; ++row) {
        uint32_t selected = v->top + row == v->cur;
        int32_t at = 96 + (int32_t)row * 22;
        if (selected) hds_bar(at, BR_ACC);
        br_widget_text_2x(12, at, v->line[row], selected ? C_BLACK : C_WHITE);
    }
}
static void br_draw_sel(uint32_t force)
{
    static uint32_t old[BR_SEL_ROWS + 2u];
    static uint8_t next[BR_SEL_ROWS + 2u];
    struct br_sel_view v;
    uint32_t p = br_ui.part, h, part, band, common;
    v.p = p; v.cur = br_sel.cursor[p]; v.top = br_sel.top[p];
    v.chip = br_sel.knob_ms && fm1_ms - br_sel.knob_ms < 400u;
    if (p == BR_DRUM_PART) br_text_copy(v.head, "DRUM KITS", sizeof v.head);
    else br_text_copy(v.head, br_bank_category(v.cur), sizeof v.head);
    br_number(v.count, (int32_t)v.cur + 1);
    br_text_append(v.count, p == BR_DRUM_PART ? (BR_KIT_BANK_COUNT == 12u ? "/12" : "/3") : "/128", sizeof v.count);
    for (uint32_t row = 0u; row < BR_SEL_ROWS; ++row) br_sel_row_text(v.line[row], v.top + row);
    common = ((((br_sel.shown && !br_sel.loaded) * 2u + br_sel.undo_ready) * 4u + p) * 31u + br_part[p].sound.preset) * 31u + br_ui_accent;
    br_view = &v; br_art = br_sel_art;
    /* parts: 0 the heading, 1..5 the rows, 6 the foot; bands of each (n) and their first row and height */
    for (part = 0; part < BR_SEL_ROWS + 2u; ++part) {
        uint32_t n = part ? part <= BR_SEL_ROWS ? 2u : 1u : 6u;
        if (!part) h = (br_hash_text(v.head) * 31u + br_hash_text(v.count)) * 3u + v.chip;
        else if (part <= BR_SEL_ROWS) h = br_hash_text(v.line[part - 1u]) * 2u + (v.top + part - 1u == v.cur);
        else h = 0u;
        h = h * 131u + common;
        if (force) old[part] = ~h ^ br_pass.epoch;   /* (drawn whole) */
        while ((band = br_roll(&old[part], &next[part], h, n)) < n) {
            uint32_t y = !part ? 34u + band * 10u : part <= BR_SEL_ROWS ? 93u + (part - 1u) * 22u + band * 11u : 203u;
            uint32_t rows = !part ? (band < 5u ? 10u : 9u) : 11u, slot = !part ? band : part <= BR_SEL_ROWS ? 4u + part * 2u + band : 16u;
            cv_begin(240, rows, BR_BACKGROUND);
            cv_oy = -(int32_t)y;
            br_sel_art();
            cv_oy = 0;
            br_crc_blit(0, y, &br_band_crc[slot]);
        }
    }
    br_draw_status(force);
}

static void br_repaint_layout_background(uint32_t layout, uint32_t keep_dots)
{
    uint32_t k;
    if (layout == 14u
#if BR_PRODUCT_SMK37
        || layout == 15u
#endif
    ) {
        br_background_rect(0, 32, 240, 184);
    } else if (layout == 9u) {
        br_background_rect(0, 32, 240, 198);
    } else if (layout == 0u || layout == 6u) {
        br_background_rect(0, 32, 240, 42);   /* (rows 74.. are the cards' or panels': drawn whole, not wiped first) */
        if (layout == 6u) {
            if (keep_dots) {
                br_background_rect(0, 190, 240, 10);
                br_background_rect(0, 200, 80, 8);
                br_background_rect(160, 200, 80, 8);
                br_background_rect(0, 208, 240, 8);
            } else br_background_rect(0, 190, 240, 26);
        }
    } else if (layout == 5u) {
        br_background_rect(0, 32, 240, 8);
    } else if (layout == 1u || layout == 11u) {
        uint32_t rows_end = 50u + BR_DRUM_LANES * 18u;
        br_background_rect(0, 32, 240, 4u);
        if (!BR_SEQ_2X8) br_background_rect(0, rows_end, 240, 162u - rows_end);   /* 2x8: the grid clears between lanes */
        br_background_rect(0, 210, 240, layout == 11u ? 30 : 20);
        for (k = 0; k < 4u; ++k) {
            br_background_rect(k * 60u, 158, 4, 52);
            br_background_rect(k * 60u + 58u, 158, 2, 52);
        }
    } else if (layout == 2u || layout == 8u || layout == 10u) {   /* the mixer and the FX send bars share this frame */
        uint32_t strip_h = layout == 8u ? BR_FX_SEND_STRIP_H : 176u, bottom = 34u + strip_h;
        br_background_rect(0, 32, 240, 2);
        br_background_rect(0, bottom, 240, bottom < 216u ? 216u - bottom : 230u - bottom);
        for (k = 0; layout == 8u && k < 4u; ++k) {   /* (the mixers' strips are 60 px: they draw their gaps) */
            br_background_rect(k * 60u, 34, 4, strip_h);
            br_background_rect(k * 60u + 58u, 34, 2, strip_h);
        }
    } else if (layout == 7u) {                   /* SEL list: header gap and rows above cue */
        br_background_rect(0, 32, 240, 2);
        br_background_rect(0, 214, 240, 2);
#if BR_PRODUCT_SMK37 || BR_ARR_UI
#if BR_ARR_UI
    } else if (layout == 12u && br_draw_layout_old == 13u) {
        /* The pattern row repaints 60..95; clear the exposed ribbon and ruler. */
        br_background_rect(0, 34, 240, BR_PRODUCT_SMK37 ? 32u : 26u);
        br_background_rect(0, 96, 240, 12);
    } else if (layout == 13u && br_draw_layout_old == 12u) {
        /* The ribbon and ruler repaint everything except this four-row gap. */
        br_background_rect(0, 88, 240, 4);
#if BR_PRODUCT_SMK37
        /* The SMK pattern view places additional labels below the ruler. */
        br_background_rect(0, 108, 240, 27);
#endif
#endif
    } else if (layout >= 12u) {
        br_background_rect(0, 32, 240, 184);
#endif
    } else if (layout == 3u && BR_SEQ_2X8) {     /* 2x8 cells paint their own column gaps; the caption owns 122-161 */
        br_background_rect(0, 32, 240, 4);
        br_background_rect(0, 76, 240, 6);
        br_background_rect(0, 36, 6, 86);
        br_background_rect(0, 210, 240, 6);
        for (k = 0; k < 4u; ++k) {
            br_background_rect(k * 60u, 158, 4, 52);
            br_background_rect(k * 60u + 58u, 158, 2, 52);
        }
    } else if (layout == 3u) {
#if BR_PRODUCT_SMK37
        br_background_rect(0, 32, 240, 138);
#endif
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
#if BR_PRODUCT_SMK37
/* HD: the same text intro (rule, name at scale 2, model) in anti-aliased type, drawn as two 40-row strips. */
static void br_intro_frame(void)
{
    lcd_fill(0, 0, 240, 88, C_BLACK);
    for (uint32_t y = 88u; y < 168u; y += 40u) {
        cv_begin(240u, 40u, C_BLACK);
        cv_oy = -(int32_t)y;
        hd_rrect(92, 91, 56, 2, 1, C_WHITE);
        br_widget_text_scaled((240 - br_widget_text_width_scaled("JAMESOS-K", 2)) / 2, 102, "JAMESOS-K", C_WHITE, 2);
        br_widget_text((240 - br_widget_text_width("SMK-37 PRO")) / 2, 131, "SMK-37 PRO", C_WHITE);
        cv_oy = 0;
        cv_blit(0u, y);
    }
    lcd_fill(0, 168, 240, 72, C_BLACK);
}
#elif BR_PRODUCT_SMK37
static void br_intro_frame(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    lcd_fill(92, 91, 56, 2, C_WHITE);
    draw_text_box(0, 102, 240, 2u, "JAMESOS-K", C_WHITE, 1);
    draw_text_box(0, 131, 240, 1u, "SMK-37 PRO", C_WHITE, 1);
}
#else
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
#endif

/* What the header and strip show (br_draw_top): the title, the battery level, and the SAVE screens'
 * variant (the globe, no model chip; the bank map in the track colour with the cursor marked). */
static struct { const char *name; int32_t battery; uint8_t save; } br_top;
#if BR_ARR_UI
static uint16_t br_arr_color(unsigned pattern);
static unsigned br_arr_bar_at(unsigned block);
/* SONG mode shares the eight-row strip with the step indicators, even when empty or stopped. */
static uint32_t br_song_strip_visible(void)
{
    return br_ps_state.mode == FM1_SONG_MODE_SONG;
}
static uint32_t br_song_strip_playing(void)
{
    return br_playing && br_ps_state.song_enabled && br_ps_state.song_position < br_ps_state.song_length;
}
static void br_song_strip_art(void)
{
    const fm1_pattern_song *s = &br_ps_state;
    unsigned total = br_arr_bar_at(s->song_length), a = 0u;
    hd_rrect(0, 0, 240, 3, 0, RGB(28, 33, 36));
    for (unsigned i = 0; i < s->song_length; a += s->entry[i++].bars) {
        int32_t x0 = (int32_t)(a * 240u / total);
        int32_t x1 = (int32_t)((a + s->entry[i].bars) * 240u / total);
        hd_rrect(x0, 0, x1 > x0 ? x1 - x0 : 1, 3, 0, br_arr_color(s->entry[i].pattern));
    }
    unsigned bar = br_song_strip_playing() ?
        br_arr_bar_at(s->song_position) + (s->pre ? 0u : s->bars_elapsed) : br_arr_bar_at(br_arr_at());
    int32_t x = total ? (int32_t)(bar * 240u / total) : 0;
    if (x > 239) x = 239;
    hd_rrect(x - 1, 0, 3, 3, 0, C_BLACK);
    hd_rrect(x, 0, 1, 3, 0, C_WHITE);
}
#endif
/* Place the steps below the song strip when present, otherwise against the header. */
static void br_compact_step_strip(void)
{
    uint32_t p = br_ui.part, length = br_part[p].length;
    int32_t step_y = 0;
#if BR_ARR_UI
    if (br_song_strip_visible()) step_y = 4;
#endif
    uint32_t banks = (length + 15u) / 16u, kit = br_is_kit(br_part[p].sound.preset);
    uint32_t mixer = br_ui.page == BR_PAGE_MIX && !br_top.save;
    uint32_t mark = br_top.save ? br_ui.cursor : br_seq_grid_page() ?
        (br_ui.step_edit_active ? br_ui.step_press_step : BR_STEPS) : br_ui.cursor;
    if (mixer) { length = BR_BANK_STEPS; banks = 1u; }
#if !BR_SEQ_2X8
    else { length = BR_BANK_STEPS; banks = 1u; }
#endif
    if (!banks) return;
    for (uint32_t k = 0; k < length; ++k) {
        uint32_t at = k;
#if !BR_SEQ_2X8
        if (!mixer) at += br_ui.step_bank * BR_BANK_STEPS;
#endif
        uint32_t step = br_part[p].step[at];
        uint32_t on = kit ? (BR_STEP_HIT(step) >> br_ui.lane) & 1u : (step & BR_STEP_ON) != 0u;
        uint32_t now = br_playing && (mixer ? k == br_global_beat * 4u + (br_transport_phase >> 5) : at == br_step_pos[p]);
        /* Every bank has the same share of the ribbon's width; shorten the last
         * segment in a bank to leave a visual gap before the next bank. */
        int32_t x = (int32_t)(k * 240u / (banks * 16u));
        int32_t end = (int32_t)((k + 1u) * 240u / (banks * 16u));
        int32_t cw = end - x - (k + 1u == banks * 16u ? 0 :
                                  (k & 15u) == 15u && k + 1u < length ? 3 : 1);
        if (cw < 1) cw = 1;
        uint16_t color = mixer ? BR_LINE : on ? (br_top.save ? br_track_color(p) : BR_ACC) : BR_LINE;
#if !BR_SEQ_2X8
        if (!mixer && at >= br_part[p].length) color = RGB(16, 19, 20);
#endif
        if (!mixer && banks >= 2u && k >> 4 != br_ui.step_bank) color = hd_darken(color, 100u);
        if (now) bw_mark_begin();
        hd_rrect(x, step_y, cw, 3, 0, now ? hd_lighten(color, 90u) : color);
        if (now) hd_rrect(x, step_y, cw, 1, 0, C_WHITE);
        if (!mixer && at == mark) hd_rrect(x, step_y + 3, cw, 1, 0, C_WHITE);
        hd_mark.on = 0u;
    }
}
static void br_strip_art(void)
{
#if BR_ARR_UI
    if (br_song_strip_visible()) br_song_strip_art();
#endif
    br_compact_step_strip();
}
#if defined(BR_PATTERN_SONG_EXPERIMENT) && BR_PRODUCT_SMK37
static void br_draw_smk_patterns(uint32_t force)
{
    static uint32_t old;
    static uint8_t next;
    uint32_t key = br_ps_state.used | br_ps_state.active << 16 | br_ps_state.queued << 24;
    for (uint32_t row = 0; row < 2u; ++row) {
        if (!br_band(&old, &next, key, row, force)) continue;
        cv_begin(240, 32, BR_BACKGROUND);
        /* HD: the pads' two rows of eight on the step grid's columns (sq_col_x). Saved slots are raised,
         * empty ones quiet; the active slot is ringed in the track colour, the queued one in gold. */
        for (uint32_t col = 0; col < 8u; ++col) {
            uint32_t slot = row * 8u + col, used = (br_ps_state.used >> slot) & 1u;
            int32_t x = (int32_t)col * 30, width = col == 7u ? 30 : 29;
            char n[8];
            if (slot >= FM1_PATTERN_COUNT) break;
            if (slot == br_ps_state.active)
                hd__rrect(x, 1, width, 30, 5, hd_mix(BR_CARD, BR_ACC, used ? 110u : 70u), hd_mix(BR_CARD, BR_ACC, used ? 60u : 36u),
                          hd_mix(BR_CARD, BR_ACC, 160u), hd_darken(BR_ACC, 200u), 0);
            else if (used) hd__rrect(x, 1, width, 30, 5, RGB(44, 50, 54), RGB(26, 30, 33), RGB(76, 84, 88), RGB(12, 14, 16), 0);
            else hd__rrect(x, 1, width, 30, 5, RGB(14, 17, 19), RGB(14, 17, 19), RGB(30, 35, 38), RGB(8, 10, 11), 0);
            if (slot == br_ps_state.active || slot == br_ps_state.queued) {
                uint16_t edge = slot == br_ps_state.active ? hd_lighten(BR_ACC, 40u) : BR_GLOBAL_ACC;
                hd_rrect_frame(x, 1, width, 30, 5, edge);
                hd_rrect_frame(x + 1, 2, width - 2, 28, 4, hd_darken(edge, 60u));
            }
            br_number(n, (int32_t)slot + 1);
            br_widget_text(x + (width - br_widget_text_width(n)) / 2, 12, n,
                           used || slot == br_ps_state.active ? C_WHITE : BR_DIM);
        }
        cv_blit(0, 66u + row * 38u);
        continue;
        for (uint32_t col = 0; col < 8u; ++col) {
            uint32_t slot = row * 8u + col;
            if (slot >= FM1_PATTERN_COUNT) break;
            int32_t x = 3 + (int32_t)col * 30;
            uint16_t fill = (br_ps_state.used & (1u << slot)) ? BR_CARD : BR_BACKGROUND;
            uint16_t edge = slot == br_ps_state.active ? BR_ACC :
                slot == br_ps_state.queued ? BR_GLOBAL_ACC : BR_LINE;
            char n[8];
            br_art_rect(x, 0, 25, 30, edge);
            br_art_rect(x + 1, 1, 23, 28, fill);
            br_number(n, (int32_t)slot + 1);
            br_widget_text(x + (25 - br_widget_text_width(n)) / 2, 11, n, C_WHITE);
        }
        cv_blit(0, 66u + row * 38u);
    }
#if !BR_ARR_UI
    if (!br_band(&old, &next, key, 2u, force)) return;
    cv_begin(240, 22, BR_BACKGROUND);
    cv_blit(0, 154);
#endif
}
#endif
#if BR_ARR_UI
#include "pattern_arranger_ui.c"   /* part 2: drawing */
#endif
/* The header's art (br_draw_body sets the title and battery level). */
static void br_header_art(void)
{
    char number[16];
    uint32_t p = br_ui.part;
    if (!cv_h) { br_metronome_icon(197); return; }   /* (measuring the marks: only the arm moves) */
    /* Global pages (the mixer included) wear the globe; the mixer still marks the selected strip. */
    if (br_top.save || br_ui.page == BR_PAGE_MIX || br_ui.page == BR_PAGE_PUNCH ||
        (br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] == BR_LFOS) ||
        (br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == BR_FX_REVERB))
        br_global_badge(6, 2);
#if BR_PRODUCT_SMK37
    else br_track_badge(6, 2, br_gate_page() ? BR_DRUM_PART : p, 0);
#else
    else br_track_badge(6, 2, p, 0);
#endif
    /* HOME screens 0 and 1 (both titled with the sound's name) lead a Plaits title with the model chip. The hash needs no chip bit: the
     * chip comes and goes only with the preset (hashed), the page (the title) or HOME's
     * screen (a layout change, which forces this redraw). */
    br_header_chip = !br_top.save && br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] < 2u && br_is_plaits(br_part[p].sound.preset);
    br_header_draw_title(br_top.name);
    br_number(number, (int32_t)br_bpm);
    br_metronome_icon(197);
    br_widget_text(225 - br_widget_text_width(number) / 2, 13, number, C_WHITE);
    br_battery_icon(br_top.battery);
}
/* The header and the strip (rows 0..31) as br_top says, each in five 48-px chunks drawn when its hash (all
 * but the metronome's swing, the playhead) changes, a chunk equal to what is shown not sent. A swing of the
 * metronome's arm or a playhead step redraws the boxes of the old and the new mark (br_anim). */
static void br_top_part(br_anim_t *a, uint32_t *old, uint8_t *next, uint32_t *crc, uint32_t h, uint32_t phase,
                        uint32_t y, uint32_t rows, uint32_t force, uint32_t *right, uint32_t hr)
{
    uint32_t n = right ? 4u : 5u;            /* (the header's last chunk, the arm's: its own hash, hr) */
    if (!br_anim_update(a, right ? hr : h, phase, 240, (int32_t)rows)) return;
    for (uint32_t k = 0; k < 5u; ++k) {        /* 48-px chunks; one equal to what is shown is not sent */
        if (k < n ? !br_band(old, next, h, k, force) : !br_region(right, hr, force)) continue;
        cv_begin(48, rows, y == 0u ? br_track_background(br_ui.part) : BR_BACKGROUND); cv_ox = (int32_t)(k * 48u);
        br_art();
        cv_ox = 0;
        br_crc_blit(k * 48u, y, &crc[k]);
    }
    if (*next >= n) br_anim_paint(a, 0u, y, crc, 5u);
}
/* Only the header draws rows 0..23, so a change of layout does not redraw it (its own epoch, br_top_epoch,
 * moves only when the whole screen is repainted): a page change sends just the chunks whose pixels differ
 * (the title's). */
static uint32_t br_top_epoch;
static uint8_t br_top_only;                     /* the header alone (a layout drawing its own row 24 strip) */
static void br_draw_top(uint32_t force)
{
    static uint32_t top_old, top_right, strip_old, top_crc[5], strip_crc[5];
    static uint8_t top_next, strip_next;
    uint32_t k, h, p = br_ui.part, epoch = br_pass.epoch;
    br_pass.epoch = br_top_epoch;
    h = br_hash_text(br_top.name) ^ (p << 20) ^ (br_part[p].sound.preset << 24) ^ br_top.save;
    h = (h * 31u + br_ui.page) * 31u + br_ui.screen[br_ui.page];   /* (badge, chip: no swing redraws them any more) */
#if BR_PRODUCT_SMK37
    h = h * 31u + br_gate_page();
#endif
    br_art = br_header_art;               /* (the title ends by x 190: columns 192.. show the tempo and battery) */
    br_top_part(&br_anim[4], &top_old, &top_next, top_crc, h,
                br_playing ? 1u + (((br_transport_phase >> 3) & 15u) << 1 | (br_global_beat & 1u)) : 0u, 0u, 24u, force,
                &top_right, ((br_bpm * 31u + (uint32_t)(br_top.battery + 1)) * 2u + br_playing) * 8u + p);   /* (p: the background) */
    if (br_ui.page == BR_PAGE_MIX && !br_top.save) {
        h = 0x47504c4fu ^ (br_playing << 31);
        k = br_global_beat * 4u + (br_transport_phase >> 5);
    } else {
        h = br_part[p].sound.preset ^ p << 24 ^ br_ui.cursor << 8 ^ br_ui.lane << 16 ^ br_playing << 31;
        h = h * 31u + br_seq_grid_page();   /* (the page only through this: a page change draws no strip) */
        h = h * 31u + br_ui.step_bank;
        h = (h * 31u + br_ui.step_press_step) * 3u + br_ui.step_edit_active;
        h = h * 31u + br_part[p].length;
        for (k = 0; k < BR_STEPS; k++) h = h * 31u + br_part[p].step[k];
        k = br_step_pos[p];
    }
    h = (h * 31u + br_ui_accent) * 2u + br_top.save;   /* (the segments' colour: the playhead no longer redraws them) */
#if BR_ARR_UI
    uint32_t song_strip = br_song_strip_visible();
    h = h * 31u + song_strip;
    if (song_strip) {
        const fm1_pattern_song *s = &br_ps_state;
        h = h * 31u + s->song_length;
        for (uint32_t i = 0; i < s->song_length; ++i)
            h = (h * 31u + s->entry[i].pattern) * 31u + s->entry[i].bars;
        if (br_song_strip_playing())
            h = (h * 31u + s->song_position) * 31u + (s->pre ? 0u : s->bars_elapsed);
        else h = h * 31u + br_arr_at();
    }
#endif
    br_pass.epoch = epoch;                      /* (some layouts draw over the strip's rows: it keeps the epoch) */
    if (br_top_only && !br_top.save
#if BR_ARR_UI
        && !song_strip
#endif
    ) return;   /* (a layout with its own strip at row 24; SAVE needs the strip) */
    br_art = br_strip_art;
    br_top_part(&br_anim[5], &strip_old, &strip_next, strip_crc, h, br_playing ? 1u + k : 0u, 24u, 8u, force, 0, 0u);
}
#if BR_PRODUCT_SMK37
/* The ARP page on the drum track: the drum track has no arpeggiator, so one centred line sends the player to
 * a track that has: "SWITCH TO" and the three synth track badges, whole line centred in the page body. */
static void br_draw_arp_cue(uint32_t force)
{
    static uint32_t old;
    if (br_region(&old, 1u, force)) {
        const char *text = "SWITCH TO";
        int32_t text_w = br_widget_text_width(text), badge_w = 30, gap = 6, lead = 8, x, k;
        x = (240 - (text_w + lead + 3 * badge_w + 2 * gap)) / 2;
        cv_begin(240, 24, BR_BACKGROUND);
        br_widget_text(x, 8, text, C_WHITE);
        x += text_w + lead;
        for (k = 0; k < 3; ++k, x += badge_w + gap) br_track_badge(x, 2, (uint32_t)k, 0u);
        cv_blit(0, 112);
    }
}
#endif
static void br_draw_body(void)
{
    static uint32_t card_old[4][2], comp_old;
    static uint8_t card_next[4][2], cards_done;
    static uint32_t background_part = 0xffffffffu;
    static uint8_t compressor_visible, dots_visible, snap_cue_visible;
    uint32_t k, h, force = 0u, p = br_ui.part;
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
    if (br_home_voice_page() && br_is_kit(br_part[p].sound.preset)) layout = 14u;
#if BR_PRODUCT_SMK37
    if (br_arp_cue_page()) layout = 15u;
#endif
#if BR_ARR_UI
    if (br_ui.page == BR_PAGE_PS) layout = 12u + BR_ARR_VIEW;
#elif defined(BR_PATTERN_SONG_EXPERIMENT) && BR_PRODUCT_SMK37
    if (br_ui.page == BR_PAGE_PS) layout = 12u;
#endif
    uint32_t geometry_changed = br_draw_layout_old != layout;
    uint32_t layout_changed = geometry_changed || background_part != p;
    if (br_draw_layout_old == 0xffffffffu) br_top_epoch += 0x9e3779b9u;   /* the whole screen, the top too */
    char text[64], number[16];
    const char *name = br_header_title();
    /* A pass that resumes a frame (no input since it began) skips the cards that frame has finished: their
     * text and hash are not worked out again; a change to one shows in the next frame. */
    if (!br_pass.on || !br_draw_more || br_ui.redraw) cards_done = 0u;
    br_clamp_ui_lane();
    /* Moving pictures take their time and LFO phases once per frame, so a frame that takes several passes
     * is drawn from one moment. */
    if (!br_pass.on || !br_draw_more) {
        br_badge_global_lfo_phase = (uint8_t)br_audio_global_lfo_phase();
        if (br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] == BR_LFOS)
        {
            br_badge_global_lfo_value = (uint8_t)br_global_target_visual_value();
            br_badge_global_aux = (br_master.fx.heat_type & 3u) | (br_master.comp.attack & 127u) << 2u |
                (br_master.comp.hold & 127u) << 9u | (br_master.comp.release & 127u) << 16u;
        }
#if BR_MODEL_MOD_EXPERIMENT
        if (br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] < BR_LFOS &&
            br_part[p].mod.lfo[br_ui.screen[BR_PAGE_LFO]].target == BR_TARGET_MODEL && br_is_plaits(br_part[p].sound.preset)) {
            uint32_t base = br_part[p].sound.preset - BR_FIRST_PLAITS;
            br_badge_model_target = (uint8_t)(br_part[p].mod.lfo[br_ui.screen[BR_PAGE_LFO]].depth ?
                br_audio_model_target(p, base) : base);
        }
#endif
    }
    for (uint32_t part = 0; part < BR_PARTS && (!br_pass.on || !br_draw_more); ++part) {
        for (uint32_t lfo = 0; lfo < BR_LFOS; ++lfo) {
            br_badge_lfo_phase[part][lfo] = (uint8_t)br_audio_lfo_phase(part, lfo);
            br_badge_lfo_depth[part][lfo] = (uint8_t)br_clip((int32_t)br_part[part].mod.lfo[lfo].depth, 0, 127);
        }
        for (uint32_t slot = 0; slot < 2u; ++slot)
            br_badge_patch_value[part][slot] = (int8_t)br_audio_patch_value(part, BR_ROUTES + slot);
    }
    /* Reverb is one global effect, so its screen takes the global (goldenrod) colour. */
    reverb_screen = br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == BR_FX_REVERB;
    br_ui_accent = br_ui.page == BR_PAGE_MIX || br_ui.page == BR_PAGE_PUNCH || reverb_screen ||
        (br_ui.page == BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO] == BR_LFOS) ? BR_GLOBAL_ACC : br_track_color(p);
    {   /* Changes of layout: wipe what the new layout does not paint, then draw everything once (a new
         * epoch). The wipes come before any region and may take several passes; the old layout's state
         * (br_draw_layout_old and the rest) is kept until every wipe is out. */
        uint32_t dots = (layout == 0u || layout == 5u || layout == 6u || layout == 14u || (BR_ARR_UI && layout >= 12u)) && br_screen_count() > 1u;   /* only centered
                                         * parameter layouts have an unobstructed eight-pixel strip here */
        uint32_t snap = br_fx_sends_page(), snap_left = snap_cue_visible && !snap;
        uint32_t comp_change = compressor_visible != compressor;
        if (layout_changed || snap_left || dots != dots_visible || comp_change) {
            uint32_t sig = br_draw_layout_old << 16 ^ layout << 10 ^ background_part << 6 ^ p << 4 ^ snap_left << 3 ^
                           dots << 2 ^ dots_visible << 1 ^ compressor ^ compressor_visible << 20;
            if (sig != br_wipe.sig) {
                br_wipe.sig = sig; br_wipe.done = 0u;
                if (layout_changed || snap_left || comp_change) br_pass.epoch += 0x9e3779b9u;   /* draw all once */
            }
            br_wipe.ord = 0u; br_wipe.on = 1u;
            if (geometry_changed) {
                /* The mixer and the drum grid keep a MUTE/SOLO-style cue bar in the last 24 rows; no other layout
                 * paints there, so wipe it when leaving them. */
                uint32_t cue_left = (br_draw_layout_old == 1u || br_draw_layout_old == 2u || br_draw_layout_old == 7u || br_draw_layout_old == 8u || br_draw_layout_old == 10u) &&
                                    layout != 1u && layout != 2u && layout != 7u && layout != 8u && layout != 10u;
                if (br_draw_layout_old == 9u || br_draw_layout_old == 10u) br_background_rect(0, 32, 240, 2);
                br_repaint_layout_background(layout, dots && dots_visible);
                if (layout == 0u) {
                    if (dots && dots_visible) {
                        br_background_rect(0, 200, 80, 8);
                        br_background_rect(160, 200, 80, 8);
                        br_background_rect(0, 208, 240, 8);
                    } else br_background_rect(0, 200, 240, 16);
                }
                (void)cue_left;   /* (rows 216..239: the status line is drawn whole on every layout; no wipe first) */
            }

            if (!dots && dots_visible) br_background_rect(0, 200, 240, 16);
            if (dots && !dots_visible) br_background_rect(0, 200, 240, 16);
            /* The compressor readout occupies a strip shared by ordinary pages.
             * Clear it when leaving either compressor screen, including to Settings. */
            if (comp_change) br_background_rect(0, 38, 240, 28);
            br_wipe.on = 0u;
            if (br_wipe.done < br_wipe.ord) { br_ui.redraw = 0; return; }   /* the rest of the wipes: next pass */
            if (dots && (!dots_visible || geometry_changed)) br_dots_old = 0xffffffffu;
            br_draw_layout_old = layout; background_part = p; snap_cue_visible = (uint8_t)snap;
            dots_visible = (uint8_t)dots; compressor_visible = (uint8_t)compressor; br_wipe.sig = 0xffffffffu;
        }
    }
    battery_level = fm1_battery_level();
#if BR_ARR_UI
    br_top_only = 0u;      /* both modes use the shared sequence/arrangement strip */
#endif
    br_top.name = name; br_top.battery = battery_level; br_top.save = 0u;
    br_draw_top(force);
    if (layout == 14u) {
        static uint32_t old;
        if (br_region(&old, 1u, force)) {
            const char *message = "NO SETTINGS FOR DRUMS";
            cv_begin(240, 20, BR_BACKGROUND);
            br_widget_text_2x((240 - 2 * br_widget_text_width(message)) / 2, 3, message, BR_DIM);
            cv_blit(0, 112);
        }
        br_draw_status(force);
        br_draw_page_dots(0u);
        br_ui.redraw = 0;
        return;
    }
#if BR_PRODUCT_SMK37
    if (layout == 15u) {
        br_draw_arp_cue(force);
        br_draw_status(force);
        br_ui.redraw = 0;
        return;
    }
#endif
#if BR_ARR_UI
    if (layout >= 12u) { br_arr_draw(force); br_ui.redraw = 0; return; }
#endif
#if defined(BR_PATTERN_SONG_EXPERIMENT) && BR_PRODUCT_SMK37
    if (layout == 12u) {
        br_draw_smk_patterns(force);
        br_draw_status(force);
        br_ui.redraw = 0;
        return;
    }
#endif
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
        br_draw_page_dots(0u);
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
        if (br_region(&comp_old, h, force)) {
            cv_begin(240, 20, BR_BACKGROUND);
            cv_oy = 8;
            hds_well(80, 0, 80, 12, 4);
            hd_rrect_frame(80, 0, 80, 12, 4, RGB(52, 60, 64));
            /* Compact segmented reduction meter, centered like the other
             * global readouts; a partial cell preserves smooth movement. */
            uint32_t fill = (reduction * 48u + 50u) / 100u;
            for (uint32_t cell = 0u; cell < 12u; ++cell) {
                int32_t x = 85 + (int32_t)cell * 6;
                uint32_t amount = fill > cell * 4u ? fill - cell * 4u : 0u;
                if (amount > 4u) amount = 4u;
                /* Rounded cells in the recess: unlit ones a quiet ghost, lit ones shaded gold. */
                hd_rrect(x, 4, 4, 4, 1, RGB(40, 46, 49));
                if (amount) hd_rrect_grad(x, 4, (int32_t)amount, 4, 1, br_palette_color(BR_PARTS, 1u), hd_darken(BR_GLOBAL_ACC, 50u));
                continue;
                cv_rect(x, 4, 4, 4, BR_LINE);
                if (amount) {
                    cv_rect(x, 4, (int32_t)amount, 4, BR_GLOBAL_ACC);
                    cv_rect(x, 4, (int32_t)amount, 1, br_palette_color(BR_PARTS, 1u));
                }
            }
            cv_oy = 0;
            cv_blit(0, 40);
        }
    }
    if (compressor && br_ui.screen[BR_PAGE_MIX] == 5u) {
        br_draw_comp_time(force);
        br_draw_page_dots(0u);
        br_ui.redraw = 0;
        return;
    }
    if (envelope) {
        br_draw_envelope(force);
        br_draw_page_dots(0u);
        br_ui.redraw = 0;
        return;
    }
    if (!compressor) br_draw_page_route_badge(force, p);
    /* Every card's value and label (part 0), then the pictures (part 1); cards_done: bit k part 0, bit
     * 4 + k part 1. */
    for (uint32_t step = 0, worked = 0; step < 8u; ++step) {
        uint32_t which = step >> 2, r;
        k = step & 3u;
        if ((cards_done >> step) & 1u) continue;
        /* The pass is over (full, out of time, or a note waits): the rest next pass. Working out a card's
         * text and hash costs time too, drawn or not, so the clock is asked here as well. */
        if (br_pass.on && (br_pass.more || (br_pass.drawn | worked) &&
            (BR_INPUT_WAITING() || (uint32_t)(BR_DRAW_NOW() - br_pass.t0) >= BR_PASS_TICKS))) {
            br_pass.more = 1u; br_ui.redraw = 0; return;
        }
        worked = 1u;
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
                phase = br_badge_lfo_phase[p][source - BR_SOURCE_LFO1];
            else if (source == BR_SOURCE_GLOBAL_LFO) phase = br_badge_global_lfo_phase;
            else if (source >= BR_SOURCE_LFO1_TRI && source <= BR_SOURCE_LFO2_SQU)
                phase = br_badge_lfo_phase[p][(source - BR_SOURCE_LFO1_TRI) >> 1];
        }
        if (kind >= BR_WIDGET_PARAM_FIRST) {                    /* parameter graphics: value, variant, companions */
            uint32_t env[4], a = br_param_animated(k, kind);
            h = (h * 31u + (uint32_t)c.value) * 31u + br_control_widget_variant(k, &c, kind);
            if (br_param_env(k, &c, kind, env)) h = (((h * 31u + env[0]) * 31u + env[1]) * 31u + env[2]) * 31u + env[3];
            if (a) {
                animated = 1u;
                phase = a == BR_PARAM_ANIM_GLOBAL_LFO ? br_badge_global_lfo_phase :
                        a >= BR_PARAM_ANIM_LFO1 ? br_badge_lfo_phase[p][a - BR_PARAM_ANIM_LFO1]
                                                : br_widget_phase(BR_WIDGET_TIMBRE);   /* the evolving 8 Hz clock */
            }
        }
        if (br_ui.page == BR_PAGE_HOME) animated = 0u;   /* HOME's pictures hold still (owner, Nov 2026) */
        phase = animated ? bw_phase_step(kind, phase) : 0u;      /* the phase drawn is the phase hashed */
        if (animated && !br_widget_marked(kind)) h ^= phase << 16;   /* (marked: br_draw_control) */
        r = br_draw_control(k, &c, kind, text, phase, route_badges, card_old[k], card_next[k], h, which);
        if (r & 1u) cards_done |= 1u << step;
        if (r & 2u) cards_done |= 1u << (step ^ 4u);
    }
    if (br_pass.on && br_pass.more) { br_ui.redraw = 0; return; }
    br_draw_status(force);
    br_draw_page_dots(0u);
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
        br_intro_until = 0u; br_ui.redraw = 1u; br_draw_layout_old = 0xffffffffu;   /* all of it, over black */
        lcd_fill(0, 0, 240, 240, C_BLACK);
    }
#if BR_PRODUCT_SMK37
    {
        static uint32_t was_peeking;
        if (br_smk_fader_peek.active) {
            /* Render the actual Global Mix page without changing the page that
             * owns key, knob, audition and sequencer interactions. */
            uint32_t page = br_ui.page, mix_screen = br_ui.screen[BR_PAGE_MIX];
            uint32_t route_screen = br_ui.screen[BR_PAGE_ROUTE];
            if (!was_peeking) { br_draw_layout_old = 0xffffffffu; br_ui.redraw = 1u; }
            br_ui.page = br_smk_fader_peek.bank ? BR_PAGE_ROUTE : BR_PAGE_MIX;
            if (br_smk_fader_peek.bank) br_ui.screen[BR_PAGE_ROUTE] = 3u;
            else br_ui.screen[BR_PAGE_MIX] = 0u;
            br_draw_body();
            br_ui.screen[BR_PAGE_MIX] = mix_screen;
            br_ui.screen[BR_PAGE_ROUTE] = route_screen;
            br_ui.page = page;
            was_peeking = 1u;
            br_monitor_overlay();
            return;
        }
        if (was_peeking) {
            was_peeking = 0u; br_draw_layout_old = 0xffffffffu; br_ui.redraw = 1u;
        }
    }
#endif
    br_draw_body();
    if (br_pass.sent <= BR_PASS_BYTES - 960u) br_monitor_overlay();   /* 240 x 2: with room left */
}
/* One pass of the main loop's drawing (see br_pass): the rows still held from the last pass, or as much of
 * the frame as fits. 1: the frame is not complete yet; call again on the next pass. */
static uint32_t br_draw_pass_with(void (*draw)(void))
{
    BR_UIT_DRAW_BEGIN();
    br_pass.on = 1u; br_pass.sent = 0u; br_pass.more = 0u; br_pass.drawn = 0u; br_pass.t0 = BR_DRAW_NOW();
    if (cv_held) { cv_send_held(BR_PASS_BYTES / 2u / cv_w); br_pass.more = 1u; }   /* the rest: next pass */
    else draw();
    BR_UIT_REGION_CLOSE();
    br_pass.on = 0u; br_pass.sent = 0u;
    BR_UIT_DRAW_END(br_pass.more);
    return br_pass.more;
}
