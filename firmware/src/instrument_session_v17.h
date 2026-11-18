/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_MODEL_MOD_EXPERIMENT
#define BR_MODEL_MOD_EXPERIMENT 0
#endif
/* Session formats v17/v18: one field table drives encode, decode, defaults,
 * validation and the byte map in docs/SESSION-V18.md.
 *
 * The firmware (instrument_session.c) includes it with BR_V17_NAMES=0 and
 * BR_V17_RUNTIME=1: it uses the tables and binds each row to its runtime home;
 * the struct-based codec below (br_session_v17_t, br_v17_encode/decode) is
 * compiled only for host tools and tests (tools/session_v16_to_v17.c,
 * tests/test_session_v17.c).
 *
 * Wire format, little-endian:
 *   header (12 B): magic "JSES", version u16 = 18 (17 also accepted), header_len u8 = 12,
 *                  section_count u8 = 5, total_len u16, flags u16 = 0
 *   sections:      {id u8, section_version u8, len u16} + len payload bytes
 * Readers reject only structural errors (magic, version, header/total/section
 * lengths, a duplicated known section, a TRACKS length not divisible by 4).
 * Unknown sections, bytes after the last section and the tail of a section
 * longer than this table are skipped. Fields a shorter section does not hold
 * take their defaults. Out-of-range field values are clamped and counted.
 * There is no checksum: the project store header (ps CRC) and the FMP1
 * envelope CRC already cover the bytes. Spare bytes are written 0 and ignored
 * on read. Every field has its own bytes: nothing is bit-packed or aliased
 * except the pattern section's documented length/micro u16 and step words. */
#ifndef BR_INSTRUMENT_SESSION_V17_H
#define BR_INSTRUMENT_SESSION_V17_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef BR_V17_NAMES
#define BR_V17_NAMES 1 /* host: names for docs/dumps; the device builds with 0 */
#endif

#define BR_V17_MAGIC 0x5345534Au /* "JSES" */
#define BR_V17_LEGACY_VERSION 17u
#define BR_V18_VERSION 18u
#define BR_V19_VERSION 19u
#define BR_V17_VERSION 20u

/* Inverted old multipliers have no representation in the new 0..200% law. */
static inline uint32_t br_v19_from_old_mod(uint32_t old)
{ return old <= 64u ? 0u : old >= 127u ? 64u : ((old - 64u) * 64u + 31u) / 63u; }
#define BR_V17_HEADER_LEN 12u
#define BR_V17_SECTION_HEADER_LEN 4u
#define BR_V17_TRACKS 4u
#define BR_V17_KIT_TRACK 3u
#define BR_V17_SIZE_LIMIT 3584u /* PS_MAX_PAYLOAD: today's project payload limit */

/* ---- Value ranges: X(name, min, max). A field's wire size is 2 bytes when
 * the range needs it, otherwise 1; it is signed when min < 0. SPARE (index 0)
 * marks named reserve bytes: written 0, ignored on read. */
#define BR_V17_RANGES(X) \
    X(SPARE, 0, 0) \
    X(BOOL, 0, 1) \
    X(U7, 0, 127) \
    X(S7, -127, 127) \
    X(BPM, 60, 240) \
    X(SWING, 0, 77) \
    X(TRACK, 0, 3) \
    X(OCTAVE, -2, 2) \
    X(MIDI_CH, 0, 16) /* 0 = OFF */ \
    X(SMOOTH, 0, 100) \
    X(HEAT, 0, 2) \
    X(COMP_SRC, 0, 8) \
    X(COMP_THR, 0, 48) \
    X(COMP_RATIO, 0, 7) \
    X(COMP_MAKEUP, 0, 24) \
    X(REVERB_TYPE, 0, 2) /* ROOM, HALL, SPACE (UI ordinal) */ \
    X(PRESET, 48, 76) /* synth 52..75, kit 48..50 and 76; role checked by the device */ \
    X(TUNE, -24, 24) \
    X(TARGET, 0, 26 + BR_MODEL_MOD_EXPERIMENT) \
    X(GLOBAL_TARGET, 0, 75) \
    X(WAVE, 0, 4) \
    X(LFO_DIV, 0, 15) /* sync division index; list defined by the LFO-sync work */ \
    X(SOURCE, 0, 17) \
    X(ENGINE4, 0, 3) /* drive mode, delay engine, mod engine */ \
    X(SEQ_LEN, 1, 64) \
    X(SPEED, 0, 2) \
    X(QUANT, 0, 6) \
    X(ARP_MODE, 0, 4) \
    X(ARP_RATE, 0, 4) \
    X(ONE_TO_4, 1, 4) \
    X(ARP_GATE, 1, 127) \
    X(STEP_LEN, 0, 1024) \
    X(COND, 0, 144) \
    X(KIT_NOTE, 36, 84) \
    X(VOICE, 0, 2) \
    X(GATE, 0, 128) /* 128 = open */ \
    X(CHOICE4, 0, 3) \
    X(DELAY_SHORT, 0, 2) \
    X(VOICE_MODE, 0, 2) /* 0: legacy global, 1: mono, 2: poly */ \
    X(LEVEL32, 0, 31) \
    X(MANUAL5, 0, 63) /* reverb_manual: bits 0-4 manual controls; bit 5 sync pre-delay */

/* ---- Field rows, in wire order. S(name, range, default) is one field;
 * A(name, count, range, default) is `count` consecutive fields. Offsets are
 * implicit (the running sum of sizes), so fields cannot overlap and the only
 * bytes not owned by a setting are the named spares. */
#define BR_V17_GLOBAL_ROWS(S, A) \
    S(bpm, BPM, 120) \
    S(swing, SWING, 0) \
    S(selected_track, TRACK, 0) \
    S(octave, OCTAVE, 0) \
    S(arp_on, BOOL, 0) \
    S(poly_on, BOOL, 1) \
    S(monitor, BOOL, 0) \
    S(midi_clock_in, BOOL, 0) \
    S(midi_ch_t1, MIDI_CH, 1) \
    S(midi_ch_t2, MIDI_CH, 2) \
    S(midi_ch_t3, MIDI_CH, 3) \
    S(midi_ch_d1, MIDI_CH, 4) \
    S(midi_ch_d2, MIDI_CH, 5) \
    S(midi_ch_d3, MIDI_CH, 6) \
    S(midi_ch_d4, MIDI_CH, 7) \
    S(smk_fader_link, BOOL, 0) /* SMK-37 Pro: fader 1 sets T1 and T2, fader 2 ignored; FM-1: kept, unused */ \
    S(global_lfo_wave, WAVE, 0) \
    S(global_lfo_rate, U7, 102) \
    S(global_lfo_depth, U7, 0) \
    S(global_lfo_target, GLOBAL_TARGET, 0)

#define BR_V17_MASTER_ROWS(S, A) \
    S(filter, U7, 64) \
    S(resonance, U7, 0) \
    S(env_depth, S7, 0) \
    S(smooth, SMOOTH, 0) \
    S(heat_type, HEAT, 0) \
    S(heat_amount, U7, 0) \
    S(comp_source, COMP_SRC, 0) \
    S(comp_threshold, COMP_THR, 18) \
    S(comp_ratio, COMP_RATIO, 3) \
    S(comp_makeup, COMP_MAKEUP, 0) \
    S(comp_attack, U7, 12) \
    S(comp_release, U7, 40) \
    S(comp_hold, U7, 0) \
    S(comp_mix, U7, 127) \
    S(leveler_on, BOOL, 0) \
    S(reverb_type, REVERB_TYPE, 0) \
    S(reverb_size, U7, 0) \
    S(reverb_decay, U7, 64) \
    S(reverb_predelay, U7, 32) \
    S(reverb_tone, U7, 64) \
    S(reverb_mod_depth, U7, 64) \
    S(reverb_low_cut, U7, 0) \
    S(reverb_width, U7, 127) \
    S(reverb_manual, MANUAL5, 0) /* bits 0-4: manual controls; bit 5: tempo-synced pre-delay */ \
    A(global_route_source, 2, SOURCE, 0) \
    A(global_route_target, 2, TARGET, 0) \
    A(global_route_amount, 2, S7, 0) \
    A(global_route_enabled, 2, BOOL, 0)

/* One track block; the TRACKS section holds four, track t at stride*t. The
 * defaults are the synth-track power-on values; BR_V17_KIT_DEFAULTS lists the
 * fields whose kit-track (t = 3) default differs. Arp fields are unused on the
 * kit track. seq_def_* is the SEQ page default step (lane 0; kit lanes 1-3
 * live in the KIT section). */
#define BR_V17_TRACK_ROWS(S, A) \
    S(preset, PRESET, 52) \
    S(timbre, U7, 64) \
    S(morph, U7, 64) \
    S(harmonics, U7, 64) \
    S(osc_mix, U7, 0) \
    S(tune, TUNE, 0) \
    S(level, U7, 127) \
    S(pan, U7, 64) \
    S(mute, BOOL, 0) \
    S(amp_attack, U7, 0) \
    S(amp_decay, U7, 64) \
    S(amp_sustain, U7, 127) \
    S(amp_release, U7, 40) \
    S(filter, U7, 64) \
    S(resonance, U7, 0) \
    S(keytrack, U7, 0) \
    S(filter_env_depth, S7, 0) \
    S(timbre_mod, U7, 64) \
    S(pitch_mod, U7, 64) \
    S(morph_mod, U7, 64) \
    S(menv_attack, U7, 0) \
    S(menv_decay, U7, 64) \
    S(menv_sustain, U7, 0) \
    S(menv_release, U7, 24) \
    S(menv_amount, S7, 0) \
    S(menv_target, TARGET, 4) \
    S(menv_loop, BOOL, 0) \
    S(menv_retrigger, BOOL, 1) \
    S(lfo1_wave, WAVE, 0) \
    S(lfo1_rate, U7, 32) \
    S(lfo1_depth, U7, 0) \
    S(lfo1_target, TARGET, 0) \
    S(lfo1_sync, BOOL, 0) \
    S(lfo1_division, LFO_DIV, 0) \
    S(lfo1_key_reset, BOOL, 0) \
    S(lfo1_fade_in, U7, 0) \
    S(lfo2_wave, WAVE, 0) \
    S(lfo2_rate, U7, 48) \
    S(lfo2_depth, U7, 0) \
    S(lfo2_target, TARGET, 0) \
    S(lfo2_sync, BOOL, 0) \
    S(lfo2_division, LFO_DIV, 0) \
    S(lfo2_key_reset, BOOL, 0) \
    S(lfo2_fade_in, U7, 0) \
    A(route_source, 8, SOURCE, 0) \
    A(route_target, 8, TARGET, 0) \
    A(route_amount, 8, S7, 0) \
    A(route_enabled, 8, BOOL, 0) \
    S(drive_send, U7, 0) \
    S(drive_mode, ENGINE4, 0) \
    S(drive_gain, U7, 64) \
    S(drive_tone, U7, 127) \
    S(delay_send, U7, 0) \
    S(delay_engine, ENGINE4, 0) \
    S(delay_time, U7, 64) \
    S(delay_feedback, U7, 57) \
    S(delay_width, U7, 0) \
    S(delay_tone, U7, 127) \
    S(delay_wow_rate, U7, 64) \
    S(reverb_send, U7, 0) \
    S(mod_send, U7, 0) \
    S(mod_engine, ENGINE4, 0) \
    S(mod_rate, U7, 40) \
    S(mod_depth, U7, 48) \
    S(mod_feedback, U7, 0) \
    S(seq_length, SEQ_LEN, 16) \
    S(seq_speed, SPEED, 1) \
    S(rec_quantize, QUANT, 0) \
    S(arp_mode, ARP_MODE, 0) \
    S(arp_rate, ARP_RATE, 2) \
    S(arp_octaves, ONE_TO_4, 1) \
    S(arp_gate, ARP_GATE, 64) \
    S(arp_latch, BOOL, 0) \
    S(arp_follow_swing, BOOL, 1) \
    S(arp_repeat, ONE_TO_4, 1) \
    S(seq_def_note, U7, 60) \
    S(seq_def_length, STEP_LEN, 16) \
    S(seq_def_velocity, U7, 100) \
    S(seq_def_condition, COND, 0) \
    S(voice_mode, VOICE_MODE, 0) \
    S(harmonics_mod, GATE, 0) \
    S(delay_short, DELAY_SHORT, 0) /* 1/32T and 1/32D, 0 = legacy delay_time */ \
    S(drive_bias, U7, 0) /* legacy spare was zero; preserve existing drive sound */

/* Kit lanes are 0..3 (kick, snare, hat, cymbal). def_*_l123[i] is the SEQ
 * page default step of kit lane i+1 (lane 0 is the kit track's seq_def_*). */
#define BR_V17_KIT_ROWS(S, A) \
    A(voice_choice, 4, VOICE, 0) \
    A(lane_level, 4, U7, 113) \
    A(lane_mute, 4, BOOL, 0) \
    S(kit_gate, GATE, 128) \
    S(drum_snap, U7, 64) \
    S(punch_loop_len, CHOICE4, 2) \
    S(punch_echo_time, CHOICE4, 2) \
    S(punch_filter_shape, CHOICE4, 0) \
    S(punch_trem_rate, CHOICE4, 2) \
    S(punch_echo_feedback, LEVEL32, 12) \
    S(punch_filter_amount, LEVEL32, 20) \
    S(punch_trem_depth, LEVEL32, 24) \
    A(def_note_l123, 3, KIT_NOTE, 60) \
    A(def_length_l123, 3, STEP_LEN, 0) \
    A(def_velocity_l123, 3, U7, 100) \
    A(def_condition_l123, 3, COND, 0) \
    A(spare_kit, 8, SPARE, 0)

/* ---- PATTERN section: the live pattern as bytes, same information as v16
 * minus the MIDI bits it used to hide in kit_retrig. X(name, bytes, elem,
 * rule, default_byte). Element size 2 = little-endian u16. */
#define BR_V17_PATTERN_BLOCKS(X) \
    X(step_synth, 384, 2, BR_V17_PR_SYNTH_STEP, 0)     /* [3][64]: lo note+1 (0 off), hi vel|tie<<7 */ \
    X(step_kit, 128, 2, BR_V17_PR_KIT_STEP, 0)         /* [64]: lo hit mask, hi accent mask (4 lanes) */ \
    X(synth_len_micro, 384, 2, BR_V17_PR_LEN_MICRO, 0) /* [3][64]: bits 0-10 length 0..1024, 11-15 micro */ \
    X(synth_condition, 192, 1, BR_V17_PR_COND, 0)      /* [3][64] */ \
    X(synth_note, 192, 1, BR_V17_PR_NOTE_OR_NONE, 255) /* [3][64], 255 none */ \
    X(synth_velocity, 192, 1, BR_V17_PR_NOTE_OR_NONE, 255) \
    X(kit_len_micro, 512, 2, BR_V17_PR_LEN_MICRO, 0)   /* [64][4] */ \
    X(kit_condition, 256, 1, BR_V17_PR_COND, 0)        /* [64][4] */ \
    X(kit_velocity, 256, 1, BR_V17_PR_VELOCITY, 100)   /* [64][4] */ \
    X(kit_pitch, 256, 1, BR_V17_PR_KIT_PITCH, 255)     /* [64][4], 36..84, 255 = C4 */ \
    X(kit_retrig, 256, 1, BR_V17_PR_RETRIG, 0)         /* [64][4]: bits 0-2 count, 3-5 spacing, 6-7 zero */

enum { BR_V17_PR_SYNTH_STEP, BR_V17_PR_KIT_STEP, BR_V17_PR_LEN_MICRO, BR_V17_PR_COND,
       BR_V17_PR_NOTE_OR_NONE, BR_V17_PR_VELOCITY, BR_V17_PR_KIT_PITCH, BR_V17_PR_RETRIG };

/* ---- Derived constants ---- */
#define BR_V17_RANGE_ENUM(n, lo, hi) BR_V17_R_##n,
enum { BR_V17_RANGES(BR_V17_RANGE_ENUM) BR_V17_RANGE_COUNT };
#define BR_V17_RANGE_SIZE(n, lo, hi) BR_V17_SZ_##n = ((hi) > 255 || (lo) < -128) ? 2 : 1,
enum { BR_V17_RANGES(BR_V17_RANGE_SIZE) BR_V17_SZ_END_ };

#define BR_V17_MS(n, r, d) int16_t n;
#define BR_V17_MA(n, c, r, d) int16_t n[c];
#define BR_V17_CS(n, r, d) + 1
#define BR_V17_CA(n, c, r, d) + (c)
#define BR_V17_WS(n, r, d) + BR_V17_SZ_##r
#define BR_V17_WA(n, c, r, d) + (c) * BR_V17_SZ_##r
#define BR_V17_PB_BYTES(n, b, e, r, d) + (b)
#define BR_V17_PB_OFF(n, b, e, r, d) BR_V17_PAT_##n, BR_V17_PAT_##n##_END_ = BR_V17_PAT_##n + (b) - 1,

enum {
    BR_V17_GLOBAL_SLOTS = 0 BR_V17_GLOBAL_ROWS(BR_V17_CS, BR_V17_CA),
    BR_V17_MASTER_SLOTS = 0 BR_V17_MASTER_ROWS(BR_V17_CS, BR_V17_CA),
    BR_V17_TRACK_SLOTS = 0 BR_V17_TRACK_ROWS(BR_V17_CS, BR_V17_CA),
    BR_V17_KIT_SLOTS = 0 BR_V17_KIT_ROWS(BR_V17_CS, BR_V17_CA),
    BR_V17_GLOBAL_BYTES = 0 BR_V17_GLOBAL_ROWS(BR_V17_WS, BR_V17_WA),
    BR_V17_MASTER_BYTES = 0 BR_V17_MASTER_ROWS(BR_V17_WS, BR_V17_WA),
    BR_V17_TRACK_BYTES = 0 BR_V17_TRACK_ROWS(BR_V17_WS, BR_V17_WA), /* one track */
    BR_V17_KIT_BYTES = 0 BR_V17_KIT_ROWS(BR_V17_WS, BR_V17_WA),
    BR_V17_PATTERN_BYTES = 0 BR_V17_PATTERN_BLOCKS(BR_V17_PB_BYTES),
    BR_V17_TOTAL_LEN = BR_V17_HEADER_LEN + 5 * BR_V17_SECTION_HEADER_LEN +
        BR_V17_GLOBAL_BYTES + BR_V17_MASTER_BYTES + BR_V17_TRACKS * BR_V17_TRACK_BYTES +
        BR_V17_KIT_BYTES + BR_V17_PATTERN_BYTES
};
enum { BR_V17_PATTERN_BLOCKS(BR_V17_PB_OFF) BR_V17_PAT_END_ };

_Static_assert(BR_V17_GLOBAL_BYTES == 20, "GLOBAL payload");
_Static_assert(BR_V17_MASTER_BYTES == 32, "MASTER payload");
_Static_assert(BR_V17_TRACK_BYTES == 112, "track block");
_Static_assert(BR_V17_KIT_BYTES == 44, "KIT payload");
_Static_assert(BR_V17_PATTERN_BYTES == 3008, "PATTERN payload");
_Static_assert(BR_V17_TOTAL_LEN == 3584, "v17 session length");
_Static_assert(BR_V17_TOTAL_LEN <= BR_V17_SIZE_LIMIT, "fits the project payload");

/* Section ids. Field sections come first in the file, PATTERN last. */
enum { BR_V17_SEC_GLOBAL = 1, BR_V17_SEC_MASTER = 2, BR_V17_SEC_TRACKS = 3,
       BR_V17_SEC_KIT = 4, BR_V17_SEC_PATTERN = 5 };
#define BR_V17_SECTION_VERSION 1u

/* ---- The standalone struct: one int16_t per field, unions give the codec
 * an index view (slot = position in wire order). */
typedef union { struct { BR_V17_GLOBAL_ROWS(BR_V17_MS, BR_V17_MA) }; int16_t v[BR_V17_GLOBAL_SLOTS]; } br_v17_global_t;
typedef union { struct { BR_V17_MASTER_ROWS(BR_V17_MS, BR_V17_MA) }; int16_t v[BR_V17_MASTER_SLOTS]; } br_v17_master_t;
typedef union { struct { BR_V17_TRACK_ROWS(BR_V17_MS, BR_V17_MA) }; int16_t v[BR_V17_TRACK_SLOTS]; } br_v17_track_t;
typedef union { struct { BR_V17_KIT_ROWS(BR_V17_MS, BR_V17_MA) }; int16_t v[BR_V17_KIT_SLOTS]; } br_v17_kit_t;
typedef struct {
    br_v17_global_t global;
    br_v17_master_t master;
    br_v17_track_t track[BR_V17_TRACKS];
    int16_t patch[BR_V17_TRACKS][2][4]; /* source, target, amount, enabled */
    uint8_t voice_extra[3][4]; /* portamento, glide mode, amp mode, LPG gate */
    br_v17_kit_t kit;
    uint8_t pattern[BR_V17_PATTERN_BYTES]; /* wire bytes, offsets BR_V17_PAT_* */
} br_session_v17_t;
_Static_assert(sizeof(br_v17_global_t) == 2 * BR_V17_GLOBAL_SLOTS, "global view");
_Static_assert(sizeof(br_v17_master_t) == 2 * BR_V17_MASTER_SLOTS, "master view");
_Static_assert(sizeof(br_v17_track_t) == 2 * BR_V17_TRACK_SLOTS, "track view");
_Static_assert(sizeof(br_v17_kit_t) == 2 * BR_V17_KIT_SLOTS, "kit view");

/* ---- Tables. Device cost: 4 B per row, 4 B per range, 6 B per section. */
typedef struct { int16_t min, max; } br_v17_range_t;
typedef struct { uint8_t range, count; int16_t def; } br_v17_row_t;
typedef struct { uint8_t id, repeat; uint16_t len; uint8_t first_row, rows; } br_v17_section_t;
typedef struct { uint8_t slot; int16_t value; } br_v17_override_t;
typedef struct { uint16_t bytes; uint8_t elem, rule, def; } br_v17_pattern_block_t;

#define BR_V17_RANGE_ROW(n, lo, hi) { (lo), (hi) },
static const br_v17_range_t br_v17_ranges[BR_V17_RANGE_COUNT] = { BR_V17_RANGES(BR_V17_RANGE_ROW) };
#define BR_V17_RS(n, r, d) { BR_V17_R_##r, 1u, (d) },
#define BR_V17_RA(n, c, r, d) { BR_V17_R_##r, (c), (d) },
#define BR_V17_KS(n, r, d) + 1
#define BR_V17_KA(n, c, r, d) + 1
enum {
    BR_V17_GLOBAL_ROW_COUNT = 0 BR_V17_GLOBAL_ROWS(BR_V17_KS, BR_V17_KA),
    BR_V17_MASTER_ROW_COUNT = 0 BR_V17_MASTER_ROWS(BR_V17_KS, BR_V17_KA),
    BR_V17_TRACK_ROW_COUNT = 0 BR_V17_TRACK_ROWS(BR_V17_KS, BR_V17_KA),
    BR_V17_KIT_ROW_COUNT = 0 BR_V17_KIT_ROWS(BR_V17_KS, BR_V17_KA),
    BR_V17_ROW_COUNT = BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT +
        BR_V17_TRACK_ROW_COUNT + BR_V17_KIT_ROW_COUNT
};
/* Row index of each field within its section's rows: BR_V17_GROW_<name>
 * (GLOBAL), BR_V17_MROW_ (MASTER), BR_V17_TROW_ (TRACKS), BR_V17_KROW_ (KIT). */
#define BR_V17_GRS(n, r, d) BR_V17_GROW_##n,
#define BR_V17_GRA(n, c, r, d) BR_V17_GROW_##n,
#define BR_V17_MRS(n, r, d) BR_V17_MROW_##n,
#define BR_V17_MRA(n, c, r, d) BR_V17_MROW_##n,
#define BR_V17_TRS(n, r, d) BR_V17_TROW_##n,
#define BR_V17_TRA(n, c, r, d) BR_V17_TROW_##n,
#define BR_V17_KRS(n, r, d) BR_V17_KROW_##n,
#define BR_V17_KRA(n, c, r, d) BR_V17_KROW_##n,
enum { BR_V17_GLOBAL_ROWS(BR_V17_GRS, BR_V17_GRA) BR_V17_GROW_END_ };
enum { BR_V17_MASTER_ROWS(BR_V17_MRS, BR_V17_MRA) BR_V17_MROW_END_ };
enum { BR_V17_TRACK_ROWS(BR_V17_TRS, BR_V17_TRA) BR_V17_TROW_END_ };
enum { BR_V17_KIT_ROWS(BR_V17_KRS, BR_V17_KRA) BR_V17_KROW_END_ };
static const br_v17_row_t br_v17_rows[BR_V17_ROW_COUNT] = {
    BR_V17_GLOBAL_ROWS(BR_V17_RS, BR_V17_RA)
    BR_V17_MASTER_ROWS(BR_V17_RS, BR_V17_RA)
    BR_V17_TRACK_ROWS(BR_V17_RS, BR_V17_RA)
    BR_V17_KIT_ROWS(BR_V17_RS, BR_V17_RA)
};
/* Field sections in file order (index 0..3), then PATTERN. */
#define BR_V17_FIELD_SECTIONS 4u
static const br_v17_section_t br_v17_sections[BR_V17_FIELD_SECTIONS] = {
    { BR_V17_SEC_GLOBAL, 1u, BR_V17_GLOBAL_BYTES, 0u, BR_V17_GLOBAL_ROW_COUNT },
    { BR_V17_SEC_MASTER, 1u, BR_V17_MASTER_BYTES, BR_V17_GLOBAL_ROW_COUNT, BR_V17_MASTER_ROW_COUNT },
    { BR_V17_SEC_TRACKS, BR_V17_TRACKS, BR_V17_TRACK_BYTES,
      BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT, BR_V17_TRACK_ROW_COUNT },
    { BR_V17_SEC_KIT, 1u, BR_V17_KIT_BYTES,
      BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TRACK_ROW_COUNT, BR_V17_KIT_ROW_COUNT },
};
#define BR_V17_TSLOT(m) ((uint8_t)(offsetof(br_v17_track_t, m) / sizeof(int16_t)))
static const br_v17_override_t br_v17_kit_defaults[] = {
    { BR_V17_TSLOT(preset), 48 },          /* BRAIDS KIT */
    { BR_V17_TSLOT(osc_mix), 64 },
    { BR_V17_TSLOT(amp_release), 64 },
    { BR_V17_TSLOT(seq_def_length), 0 },
};
#define BR_V17_KIT_DEFAULT_COUNT (sizeof br_v17_kit_defaults / sizeof br_v17_kit_defaults[0])
#define BR_V17_PB_ROW(n, b, e, r, d) { (b), (e), (r), (d) },
static const br_v17_pattern_block_t br_v17_pattern_blocks[] = { BR_V17_PATTERN_BLOCKS(BR_V17_PB_ROW) };
#define BR_V17_PATTERN_BLOCK_COUNT (sizeof br_v17_pattern_blocks / sizeof br_v17_pattern_blocks[0])

#if BR_V17_NAMES
#define BR_V17_NS(n, r, d) #n,
#define BR_V17_NA(n, c, r, d) #n,
static const char *const br_v17_row_names[BR_V17_ROW_COUNT] = {
    BR_V17_GLOBAL_ROWS(BR_V17_NS, BR_V17_NA)
    BR_V17_MASTER_ROWS(BR_V17_NS, BR_V17_NA)
    BR_V17_TRACK_ROWS(BR_V17_NS, BR_V17_NA)
    BR_V17_KIT_ROWS(BR_V17_NS, BR_V17_NA)
};
#define BR_V17_RN(n, lo, hi) #n,
static const char *const br_v17_range_names[BR_V17_RANGE_COUNT] = { BR_V17_RANGES(BR_V17_RN) };
#define BR_V17_PN(n, b, e, r, d) #n,
static const char *const br_v17_pattern_names[] = { BR_V17_PATTERN_BLOCKS(BR_V17_PN) };
static const char *const br_v17_section_names[BR_V17_FIELD_SECTIONS] = { "GLOBAL", "MASTER", "TRACKS", "KIT" };
#endif

/* Decode results */
enum { BR_V17_OK = 0, BR_V17_ERR_SHORT = -1, BR_V17_ERR_MAGIC = -2, BR_V17_ERR_VERSION = -3,
       BR_V17_ERR_HEADER = -4, BR_V17_ERR_TOTAL = -5, BR_V17_ERR_SECTION = -6,
       BR_V17_ERR_DUPLICATE = -7, BR_V17_ERR_TRACKS = -8 };

/* ---- Codec ---- */
static uint32_t br_v17_rd(const uint8_t *p, uint32_t n) { return n == 2u ? (uint32_t)p[0] | (uint32_t)p[1] << 8 : p[0]; }
static void br_v17_wr(uint8_t *p, uint32_t v, uint32_t n) { p[0] = (uint8_t)v; if (n == 2u) p[1] = (uint8_t)(v >> 8); }
static uint32_t br_v17_range_size(const br_v17_range_t *r) { return r->max > 255 || r->min < -128 ? 2u : 1u; }

#if !defined(BR_V17_RUNTIME)
static int16_t *br_v17_section_values(br_session_v17_t *s, uint32_t sec, uint32_t rep)
{
    return sec == 0u ? s->global.v : sec == 1u ? s->master.v : sec == 2u ? s->track[rep].v : s->kit.v;
}

#endif
static int32_t br_v17_clamp(int32_t v, const br_v17_range_t *r, uint32_t *clamps)
{
    if (v >= r->min && v <= r->max) return v;
    if (clamps) ++*clamps;
    return v < r->min ? r->min : r->max;
}

#if !defined(BR_V17_RUNTIME)
/* Fill one field section (one repetition) with its defaults. */
static void br_v17_section_defaults(br_session_v17_t *s, uint32_t sec, uint32_t rep)
{
    const br_v17_section_t *d = &br_v17_sections[sec];
    int16_t *v = br_v17_section_values(s, sec, rep);
    uint32_t i, k, slot = 0u;
    for (i = 0u; i < d->rows; ++i) {
        const br_v17_row_t *row = &br_v17_rows[d->first_row + i];
        for (k = 0u; k < row->count; ++k) v[slot++] = row->def;
    }
    if (sec == 2u && rep == BR_V17_KIT_TRACK)
        for (i = 0u; i < BR_V17_KIT_DEFAULT_COUNT; ++i) v[br_v17_kit_defaults[i].slot] = br_v17_kit_defaults[i].value;
}

static void br_v17_pattern_defaults(uint8_t *pattern, uint32_t from)
{
    uint32_t b, at = 0u;
    for (b = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) {
        uint32_t end = at + br_v17_pattern_blocks[b].bytes;
        for (; at < end; ++at) if (at >= from) pattern[at] = br_v17_pattern_blocks[b].def;
    }
}

static void br_v17_defaults(br_session_v17_t *s)
{
    uint32_t sec, rep;
    memset(s, 0, sizeof *s);
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec)
        for (rep = 0u; rep < br_v17_sections[sec].repeat; ++rep) br_v17_section_defaults(s, sec, rep);
    memset(s->patch, 0, sizeof s->patch);
    memset(s->voice_extra, 0, sizeof s->voice_extra);
    br_v17_pattern_defaults(s->pattern, 0u);
}

#endif

/* One pattern element brought into its documented range. */
static uint32_t br_v17_pattern_fix(uint32_t rule, uint32_t w)
{
    switch (rule) {
    case BR_V17_PR_SYNTH_STEP: return (w & 255u) > 128u ? w & 0xff00u : w;
    case BR_V17_PR_KIT_STEP: return w & 0x0f0fu;
    case BR_V17_PR_LEN_MICRO: return (w & 0x7ffu) > 1024u ? (w & 0xf800u) | 1024u : w;
    case BR_V17_PR_COND: return w > 144u ? 0u : w;
    case BR_V17_PR_NOTE_OR_NONE: return w > 127u ? 255u : w;
    case BR_V17_PR_VELOCITY: return w > 127u ? 127u : w;
    case BR_V17_PR_KIT_PITCH: return w != 255u && (w < 36u || w > 84u) ? 255u : w;
    default: return w & 0x3fu; /* BR_V17_PR_RETRIG */
    }
}

#if !defined(BR_V17_RUNTIME)
/* Bring every pattern element into its documented range; count changes. */
static void br_v17_pattern_sanitize(uint8_t *p, uint32_t *clamps)
{
    uint32_t b, at = 0u, dummy = 0u;
    if (!clamps) clamps = &dummy;
    for (b = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) {
        const br_v17_pattern_block_t *k = &br_v17_pattern_blocks[b];
        uint32_t end = at + k->bytes;
        for (; at < end; at += k->elem) {
            uint32_t v = k->elem == 2u ? br_v17_rd(p + at, 2u) : p[at], w = br_v17_pattern_fix(k->rule, v);
            if (w != v) { ++*clamps; br_v17_wr(p + at, w, k->elem); }
        }
    }
}

/* Encode `s` into `out` (cap >= BR_V17_TOTAL_LEN). Out-of-range values are
 * clamped in the output and counted in *clamps (may be NULL); spare members
 * are written 0 (a non-zero spare counts). Returns bytes written, 0 if cap is
 * too small. `s` is not modified. */
static uint32_t br_v17_encode(const br_session_v17_t *s, uint8_t *out, uint32_t cap, uint32_t *clamps)
{
    uint32_t sec, rep, i, k, at;
    if (!s || !out || cap < BR_V17_TOTAL_LEN) return 0u;
    br_v17_wr(out, BR_V17_MAGIC & 0xffffu, 2u); br_v17_wr(out + 2, BR_V17_MAGIC >> 16, 2u);
    br_v17_wr(out + 4, BR_V17_VERSION, 2u);
    out[6] = BR_V17_HEADER_LEN; out[7] = 5u;
    br_v17_wr(out + 8, BR_V17_TOTAL_LEN, 2u); br_v17_wr(out + 10, 0u, 2u);
    at = BR_V17_HEADER_LEN;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        out[at] = d->id; out[at + 1] = BR_V17_SECTION_VERSION;
        br_v17_wr(out + at + 2, (uint32_t)d->len * d->repeat, 2u);
        at += BR_V17_SECTION_HEADER_LEN;
        for (rep = 0u; rep < d->repeat; ++rep) {
            const int16_t *v = br_v17_section_values((br_session_v17_t *)(uintptr_t)s, sec, rep);
            uint32_t slot = 0u;
            for (i = 0u; i < d->rows; ++i) {
                const br_v17_row_t *row = &br_v17_rows[d->first_row + i];
                const br_v17_range_t *r = &br_v17_ranges[row->range];
                uint32_t n = br_v17_range_size(r);
                for (k = 0u; k < row->count; ++k, ++slot, at += n)
                    br_v17_wr(out + at, (uint32_t)br_v17_clamp(v[slot], r, clamps), n);
            }
        }
    }
    /* Track section starts after GLOBAL and MASTER; its route-enabled array
     * begins at byte 68 of each 112-byte track block. */
    for (rep = 0u; rep < BR_V17_TRACKS; ++rep) {
        uint32_t bit = 0u;
        uint32_t base = BR_V17_HEADER_LEN + 3u * BR_V17_SECTION_HEADER_LEN + BR_V17_GLOBAL_BYTES + BR_V17_MASTER_BYTES + rep * BR_V17_TRACK_BYTES + 68u;
        for (i = 0u; i < 2u; ++i) {
            uint32_t source = (uint32_t)br_v17_clamp(s->patch[rep][i][0], &br_v17_ranges[BR_V17_R_SOURCE], clamps);
            uint32_t target = (uint32_t)br_v17_clamp(s->patch[rep][i][1], &br_v17_ranges[BR_V17_R_TARGET], clamps);
            uint32_t amount = (uint32_t)br_v17_clamp(s->patch[rep][i][2], &br_v17_ranges[BR_V17_R_S7], clamps) & 255u;
            uint32_t enable = (uint32_t)br_v17_clamp(s->patch[rep][i][3], &br_v17_ranges[BR_V17_R_BOOL], clamps);
            uint32_t packed = source | target << 5u | amount << 10u | enable << 18u;
            for (k = 0u; k < 19u; ++k, ++bit) out[base + bit / 7u] |= (uint8_t)(((packed >> k) & 1u) << (1u + bit % 7u));
        }
    }
    /* v19 uses six formerly reserved KIT bytes, two per synth track. */
    {
        uint32_t base = BR_V17_HEADER_LEN + 4u * BR_V17_SECTION_HEADER_LEN + BR_V17_GLOBAL_BYTES +
            BR_V17_MASTER_BYTES + BR_V17_TRACKS * BR_V17_TRACK_BYTES + BR_V17_KIT_BYTES - 8u;
        for (rep = 0u; rep < 3u; ++rep) {
            uint32_t amount = s->voice_extra[rep][0], mode = s->voice_extra[rep][1], amp = s->voice_extra[rep][2], gate = s->voice_extra[rep][3];
            if (amount > 100u) { amount = 100u; if (clamps) ++*clamps; }
            if (mode > 2u) { mode = 2u; if (clamps) ++*clamps; }
            if (amp > 1u) { amp = 1u; if (clamps) ++*clamps; }
            if (gate > 31u) { gate = 31u; if (clamps) ++*clamps; }
            out[base + rep * 2u] = (uint8_t)amount;
            out[base + rep * 2u + 1u] = (uint8_t)(mode | (amp << 2u) | (gate << 3u));
        }
    }
    out[at] = BR_V17_SEC_PATTERN; out[at + 1] = BR_V17_SECTION_VERSION;
    br_v17_wr(out + at + 2, BR_V17_PATTERN_BYTES, 2u);
    at += BR_V17_SECTION_HEADER_LEN;
    memcpy(out + at, s->pattern, BR_V17_PATTERN_BYTES);
    br_v17_pattern_sanitize(out + at, clamps);
    return at + BR_V17_PATTERN_BYTES;
}

/* Decode one field-section payload of `len` bytes. */
static void br_v17_decode_fields(br_session_v17_t *s, uint32_t sec, const uint8_t *p, uint32_t len, uint32_t *clamps, uint32_t packed_patches)
{
    const br_v17_section_t *d = &br_v17_sections[sec];
    uint32_t rep, i, k, stride = len / d->repeat;
    for (rep = 0u; rep < d->repeat; ++rep) {
        const uint8_t *q = p + rep * stride;
        int16_t *v = br_v17_section_values(s, sec, rep);
        uint32_t slot = 0u, at = 0u;
        for (i = 0u; i < d->rows && at < stride; ++i) {
            const br_v17_row_t *row = &br_v17_rows[d->first_row + i];
            const br_v17_range_t *r = &br_v17_ranges[row->range];
            uint32_t n = br_v17_range_size(r);
            for (k = 0u; k < row->count; ++k, ++slot, at += n) {
                int32_t x;
                if (at + n > stride) { at = stride; break; } /* the rest keep their defaults */
                if (row->range == BR_V17_R_SPARE) continue;
                x = (int32_t)br_v17_rd(q + at, n);
                if (packed_patches && sec == 2u && i == BR_V17_TROW_route_enabled) x &= 1;
                if (r->min < 0) x = n == 2u ? (int32_t)(int16_t)(uint16_t)x : (int32_t)(int8_t)(uint8_t)x;
                v[slot] = (int16_t)br_v17_clamp(x, r, clamps);
            }
        }
    }
}

/* Decode `len` bytes. `out` always ends up fully initialised (defaults where
 * the record has nothing); on an error return its content is unspecified.
 * Out-of-range values are clamped and counted in *clamps (may be NULL). */
static int br_v17_decode(const uint8_t *in, uint32_t len, br_session_v17_t *out, uint32_t *clamps)
{
    uint32_t header_len, count, total, at, i, seen = 0u;
    if (!out) return BR_V17_ERR_SHORT;
    br_v17_defaults(out);
    if (!in || len < BR_V17_HEADER_LEN) return BR_V17_ERR_SHORT;
    if ((br_v17_rd(in, 2u) | br_v17_rd(in + 2, 2u) << 16) != BR_V17_MAGIC) return BR_V17_ERR_MAGIC;
    if (br_v17_rd(in + 4, 2u) != BR_V17_VERSION && br_v17_rd(in + 4, 2u) != BR_V19_VERSION && br_v17_rd(in + 4, 2u) != BR_V18_VERSION && br_v17_rd(in + 4, 2u) != BR_V17_LEGACY_VERSION) return BR_V17_ERR_VERSION;
    if (br_v17_rd(in + 4, 2u) < BR_V19_VERSION)
        for (i = 0u; i < BR_V17_TRACKS; ++i)
            out->track[i].timbre_mod = out->track[i].pitch_mod = out->track[i].morph_mod = 127;
    header_len = in[6]; count = in[7]; total = br_v17_rd(in + 8, 2u);
    if (header_len < BR_V17_HEADER_LEN) return BR_V17_ERR_HEADER;
    if (total > len || total < header_len) return BR_V17_ERR_TOTAL;
    at = header_len;
    for (i = 0u; i < count; ++i) {
        uint32_t id, slen, sec;
        const uint8_t *p;
        if (total - at < BR_V17_SECTION_HEADER_LEN) return BR_V17_ERR_SECTION;
        id = in[at]; slen = br_v17_rd(in + at + 2, 2u);
        at += BR_V17_SECTION_HEADER_LEN;
        if (total - at < slen) return BR_V17_ERR_SECTION;
        p = in + at; at += slen;
        if (id < BR_V17_SEC_GLOBAL || id > BR_V17_SEC_PATTERN) continue;   /* unknown: skip */
        if (seen & (1u << id)) return BR_V17_ERR_DUPLICATE;
        seen |= 1u << id;
        if (id == BR_V17_SEC_PATTERN) {
            memcpy(out->pattern, p, slen < BR_V17_PATTERN_BYTES ? slen : BR_V17_PATTERN_BYTES);
            br_v17_pattern_sanitize(out->pattern, clamps);
            continue;
        }
        for (sec = 0u; br_v17_sections[sec].id != id; ++sec) {}
        if (slen % br_v17_sections[sec].repeat) return BR_V17_ERR_TRACKS;
        br_v17_decode_fields(out, sec, p, slen, clamps, br_v17_rd(in + 4, 2u) >= BR_V18_VERSION);
        if (sec == 3u && br_v17_rd(in + 4, 2u) >= BR_V19_VERSION) {
            for (uint32_t rep = 0u; rep < 3u; ++rep) {
                uint32_t at = BR_V17_KIT_BYTES - 8u + rep * 2u;
                if (at + 1u < slen) {
                    uint32_t amount = p[at], mode = p[at + 1u] & 3u, amp = (p[at + 1u] >> 2u) & 1u;
                    if (br_v17_rd(in + 4, 2u) == BR_V19_VERSION && (p[at + 1u] & 0xf8u) && clamps) ++*clamps;
                    if (amount > 100u) { amount = 100u; if (clamps) ++*clamps; }
                    if (mode > 2u) { mode = 2u; if (clamps) ++*clamps; }
                    out->voice_extra[rep][0] = (uint8_t)amount;
                    out->voice_extra[rep][1] = (uint8_t)mode;
                    out->voice_extra[rep][2] = (uint8_t)amp;
                    out->voice_extra[rep][3] = br_v17_rd(in + 4, 2u) >= BR_V17_VERSION ? p[at + 1u] >> 3u : 0u;
                }
            }
        }
        if (sec == 2u && br_v17_rd(in + 4, 2u) >= BR_V18_VERSION) {
            uint32_t stride = slen / BR_V17_TRACKS;
            for (uint32_t rep = 0u; rep < BR_V17_TRACKS; ++rep) for (uint32_t slot = 0u; slot < 2u; ++slot) {
                uint32_t packed = 0u;
                for (uint32_t n = 0u; n < 19u; ++n) {
                    uint32_t bit = slot * 19u + n, at = 68u + bit / 7u;
                    if (at < stride) packed |= ((uint32_t)(p[rep * stride + at] >> (1u + bit % 7u)) & 1u) << n;
                }
                out->patch[rep][slot][0] = (int16_t)br_v17_clamp((int32_t)(packed & 31u), &br_v17_ranges[BR_V17_R_SOURCE], clamps);
                out->patch[rep][slot][1] = (int16_t)br_v17_clamp((int32_t)((packed >> 5u) & 31u), &br_v17_ranges[BR_V17_R_TARGET], clamps);
                out->patch[rep][slot][2] = (int16_t)br_v17_clamp((int8_t)((packed >> 10u) & 255u), &br_v17_ranges[BR_V17_R_S7], clamps);
                out->patch[rep][slot][3] = (int16_t)((packed >> 18u) & 1u);
            }
        }
    }
    if (br_v17_rd(in + 4, 2u) == BR_V17_LEGACY_VERSION)
        for (uint32_t rep = 0u; rep < BR_V17_TRACKS; ++rep) for (uint32_t slot = 0u; slot < 2u; ++slot) {
            out->patch[rep][slot][0] = out->master.global_route_source[slot];
            out->patch[rep][slot][1] = out->master.global_route_target[slot];
            out->patch[rep][slot][2] = out->master.global_route_amount[slot];
            out->patch[rep][slot][3] = out->master.global_route_enabled[slot];
        }
    if (br_v17_rd(in + 4, 2u) < BR_V19_VERSION)
        for (uint32_t rep = 0u; rep < BR_V17_TRACKS; ++rep) {
            br_v17_track_t *t = &out->track[rep];
            t->timbre_mod = (int16_t)br_v19_from_old_mod((uint32_t)t->timbre_mod);
            t->pitch_mod = (int16_t)br_v19_from_old_mod((uint32_t)t->pitch_mod);
            t->morph_mod = (int16_t)br_v19_from_old_mod((uint32_t)t->morph_mod);
            if (t->harmonics_mod) t->harmonics_mod = (int16_t)(1u + br_v19_from_old_mod((uint32_t)t->harmonics_mod - 1u));
        }
    return BR_V17_OK;
}
#endif /* !BR_V17_RUNTIME */

#endif
