/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BRAIDS_INSTRUMENT_H
#define BRAIDS_INSTRUMENT_H
#include <stdint.h>
#include "instrument_failure_job_profile.h"
#include "instrument_modules.h"
#include "../hal/fm1_audio_block.h"

/* Private fixed-eight foreground planner prototype. Off unless explicitly enabled. */
#ifndef BR_AUDIO_FOREGROUND_FIXED8_PLAN
#define BR_AUDIO_FOREGROUND_FIXED8_PLAN 0
#endif
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN != 0 && BR_AUDIO_FOREGROUND_FIXED8_PLAN != 1
#error "BR_AUDIO_FOREGROUND_FIXED8_PLAN must be 0 or 1"
#endif
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
void br_audio_fixed8_foreground_service(void);
void br_audio_fixed8_foreground_invalidate(void);
#endif

/* Private diagnostic-only extension. Off in normal builds. */
#ifndef BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG
#define BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG 0
#endif

/* Excludes experimental cross-block FX overlap modes 1–3 while retaining
 * the ordinary pipeline-mode-0 source/fusion path. Candidate builds may opt
 * in to this smaller firmware profile explicitly. */
#ifndef BR_AUDIO_FUSION_ONLY_BUILD
#define BR_AUDIO_FUSION_ONLY_BUILD 1
#endif
#ifndef BR_AUDIO_FUSION_FOUR_WAY_KIT
#define BR_AUDIO_FUSION_FOUR_WAY_KIT 1
#endif
#ifndef BR_AUDIO_FUSION_ACTIVITY_EWMA
#define BR_AUDIO_FUSION_ACTIVITY_EWMA 1
#endif

#define BR_PARTS 4u
#define BR_BPM_MIN 60u
#define BR_BPM_MAX 240u
#define BR_BPM_STORED_MAX 280u /* Earlier extended-range projects remain readable. */
#define BR_SWING_MAX 77u /* (128 + 77) / 256 rounds to 80% */
#define BR_SYNTH_PARTS 3u
#define BR_DRUM_PART 3u
#define BR_DRUM_LANES 4u
#define BR_DRUM_HIT_MASK 0x0fu
#define BR_DRUM_ALL_MASK BR_DRUM_HIT_MASK
#define BR_DRUM_CHOICE_MASK 0xffu
#define BR_STEPS 64u
#define BR_STEP_LENGTH_MAX (BR_STEPS * 16u)
#define BR_BANK_STEPS 16u
#define BR_BANKS (BR_STEPS / BR_BANK_STEPS)
#define BR_BLOCK BR_AUDIO_BLOCK_FRAMES
#define BR_FIRST_PLAITS 52u
#define BR_PLAITS_MODEL_COUNT 24u
#ifndef BR_ENABLE_PLAITS
#if defined(DINGHY_HOST) || defined(DINGHY_WEB)
#define BR_ENABLE_PLAITS 1
#else
#define BR_ENABLE_PLAITS 0
#endif
#endif
#define BR_KIT_PLAITS 76u
#define BR_PRESET_COUNT (BR_FIRST_PLAITS + (BR_ENABLE_PLAITS ? BR_PLAITS_MODEL_COUNT + 1u : 0u))
#define BR_FIRST_KIT 48u
/* Id 51 is reserved: never selectable on any track, kept so that saved preset ids stay stable. */
enum { BR_KIT_BRAIDS = 48, BR_KIT_808, BR_KIT_909, BR_PRESET_RESERVED };
static inline int br_is_kit(uint32_t preset)
{ return (preset >= BR_KIT_BRAIDS && preset <= BR_KIT_909) ||
    (BR_ENABLE_PLAITS && preset == BR_KIT_PLAITS); }
static inline int br_is_plaits(uint32_t preset)
{ return preset >= BR_FIRST_PLAITS && preset < BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT; }
/* A saved destination can be valid but unused by the selected engine. Share
 * this contract between target menus, modulation and the inactive fast path.
 * Never renumber or rewrite saved destinations when switching sounds. */
static inline uint32_t br_mod_target_supported(uint32_t preset, uint32_t target)
{
    if (!br_mod_target_active(target)) return 0u;
    if (target == BR_TARGET_HARMONICS)
        return br_is_plaits(preset) || preset == BR_KIT_PLAITS;
    if (target == BR_TARGET_OUTPUT) return br_is_plaits(preset);
    if (target == BR_TARGET_COLOR)
        return preset != BR_KIT_808 && preset != BR_KIT_909;
    return 1u;
}
/* Track roles are invariant across UI, MIDI, session restore, and audio. */
static inline int br_preset_allowed_on_part(uint32_t part, uint32_t preset)
{ return part < BR_PARTS && preset < BR_PRESET_COUNT &&
    (part == BR_DRUM_PART ? br_is_kit(preset) : (BR_ENABLE_PLAITS && br_is_plaits(preset))); }
static inline uint32_t br_default_part_preset(uint32_t part)
{ return part == BR_DRUM_PART ? BR_KIT_BRAIDS : BR_FIRST_PLAITS; }
/* Existing voice IDs 0..7 and their lane assignments remain stable. */
enum { BR_DRUM_KCK1, BR_DRUM_KCK2, BR_DRUM_SNAR, BR_DRUM_CLAP,
       BR_DRUM_OPEN, BR_DRUM_CLOS, BR_DRUM_CYMB, BR_DRUM_PERC,
       BR_DRUM_KCK3, BR_DRUM_RIM, BR_DRUM_PEDL, BR_DRUM_TOM,
       BR_DRUM_VOICE_COUNT };
static inline uint32_t br_drum_lane_choice(uint32_t choices, uint32_t lane)
{ return lane < BR_DRUM_LANES ? (choices >> (lane * 2u)) & 3u : 0u; }
static inline uint32_t br_drum_set_lane_choice(uint32_t choices, uint32_t lane, uint32_t choice)
{
    uint32_t shift;
    if (lane >= BR_DRUM_LANES) return choices & BR_DRUM_CHOICE_MASK;
    shift = lane * 2u;
    choice %= 3u;
    return ((choices & ~(3u << shift)) | (choice << shift)) & BR_DRUM_CHOICE_MASK;
}
static inline uint32_t br_drum_voice_id(uint32_t choices, uint32_t lane)
{
    uint32_t choice;
    if (lane >= BR_DRUM_LANES) return BR_DRUM_KCK1;
    choice = br_drum_lane_choice(choices, lane);
    if (choice == 2u) return BR_DRUM_KCK3 + lane;
    if (choice > 2u) choice = 0u; /* reserved two-bit value */
    return lane * 2u + choice;
}
static inline uint32_t br_drum_voice_lane(uint32_t voice_id)
{ return voice_id < 8u ? voice_id / 2u : (voice_id < BR_DRUM_VOICE_COUNT ? voice_id - 8u : 0u); }

typedef struct {
    uint32_t preset, timbre, color;
    int32_t tune;
    uint32_t level, attack, decay, sustain, release;
    uint32_t drive, delay, reverb, space, mute;
    uint32_t filter;
    uint32_t filter_resonance;
    uint32_t filter_keytrack;
    uint32_t timbre_mod, fm_mod, morph_mod;
    uint32_t drum_snap;
    uint32_t plaits_harmonics, plaits_aux;
    uint32_t pan;
    uint32_t gate; /* post-ADSR maximum voice duration; BR_GATE_OPEN bypasses it */
    uint32_t drum_voice_mask; /* low eight bits: two-bit voice choice per lane */
    uint32_t drum_key_lane; /* audio-block snapshot, 0..3, derived from compressor source */
} br_sound_t;

/* Finite legacy gate values retain the existing note-length response.
 * Ten milliseconds to about three seconds, with finer control at the short end. */
static inline uint32_t br_note_length_samples(uint32_t value)
{
    if (value > 127u) value = 127u;
#if FM1_AUDIO_RATE_HZ != 44100u
    return (uint32_t)((((uint64_t)441u + (uint64_t)value * value * 8u) *
                       FM1_AUDIO_RATE_HZ + 22050u) / 44100u);
#else
    return 441u + value * value * 8u;
#endif
}
#define BR_GATE_OPEN 128u
#define BR_GATE_FADE ((64u * FM1_AUDIO_RATE_HZ + 22050u) / 44100u)
static inline uint32_t br_gate_samples(uint32_t value)
{ return value >= BR_GATE_OPEN ? 0u : br_note_length_samples(value); }

/* Each field is one atomic word. Revision brackets sound loads; audio takes only
 * complete even revisions. The main loop alone writes controls and steps. */
typedef struct {
    volatile uint32_t revision;
    volatile br_sound_t sound;
    volatile br_mod_config_t mod;
    volatile br_fx_config_t fx;
    volatile uint32_t lane_level[BR_DRUM_LANES];
    volatile uint32_t lane_gate[BR_DRUM_LANES]; /* maximum per-hit duration, quarter-note beats 1..128 */
    volatile uint32_t step[BR_STEPS];
    volatile uint32_t length, division, swing, gate;
    /* Arpeggiator (synth tracks): mode, rate index, octaves 1..4, gate 1..127,
     * latch and follow-swing flags, repeats 1..4. */
    volatile uint32_t arp_mode, arp_rate, arp_octaves, arp_gate;
    volatile uint32_t arp_latch, arp_swing, arp_repeat;
} br_part_t;

enum { BR_ARP_UP, BR_ARP_DOWN, BR_ARP_UP_DOWN, BR_ARP_RANDOM, BR_ARP_AS_PLAYED, BR_ARP_MODES };
#define BR_ARP_RATES 5u           /* 1/8, 1/8T, 1/16, 1/16T, 1/32 */
#define BR_ARP_MAX_NOTES 8u
#define BR_ARP_MAX_OCTAVES 4u
#define BR_ARP_MAX_REPEAT 4u
#define BR_ARP_REC_SLOTS 32u
/* Held keys for one track, published by the main loop: seq is odd while the
 * main loop writes and even when the contents are consistent. */
typedef struct {
    volatile uint32_t seq;
    volatile uint8_t count, notes[BR_ARP_MAX_NOTES];
} br_arp_keys_t;
/* An arp note start/stop with the sequencer position it happened at. */
typedef struct {
    uint8_t part, note, on;
    uint32_t clock, step, elapsed, duration, unit;
} br_arp_rec_t;
static inline void br_arp_part_defaults(br_part_t *p)
{
    p->arp_mode = BR_ARP_UP; p->arp_rate = 2u; p->arp_octaves = 1u; p->arp_gate = 64u;
    p->arp_latch = 0u; p->arp_swing = 1u; p->arp_repeat = 1u;
}

/* A step can hold kit lanes or one pitched note. Its single-word publication
 * avoids partially edited notes in the interrupt. The save format uses 2 bytes. */
#define BR_STEP_HIT(s) ((uint32_t)(s) & BR_DRUM_ALL_MASK)
#define BR_STEP_ACC(s) (((uint32_t)(s) >> 8) & BR_DRUM_ALL_MASK)
#define BR_STEP_NOTE(s) (((uint32_t)(s) >> 16) & 127u)
#define BR_STEP_VEL(s) (((uint32_t)(s) >> 23) & 127u)
#define BR_STEP_ON (1u << 30)
#define BR_STEP_TIE (1u << 31)
#define BR_NOTE_STEP(note, vel) (BR_STEP_ON | ((uint32_t)(note) & 127u) << 16 | ((uint32_t)(vel) & 127u) << 23)

/* Per-trigger metadata is separate from the packed step word so a kit step
 * can carry independent values for each of its four lanes. Pitched length
 * zero means one step; kit length zero preserves the natural one-shot tail.
 * Positive values are sixteenths of a step.
 * A kit pitch is MIDI note 36..84 (C2..C6), with 255 selecting C4. A kit
 * voice override is hidden compatibility metadata; 255 inherits its lane's
 * global preset choice. */
#define BR_STEP_LENGTH_DEFAULT 16u
#define BR_STEP_PITCH_DEFAULT 60u
enum {
    BR_STEP_COND_ALWAYS = 0,
    BR_STEP_COND_PROB_BASE = 1, /* 1..100: trigger probability in percent */
    BR_STEP_COND_FIRST = 101,
    BR_STEP_COND_NOT_FIRST = 102,
    BR_STEP_COND_PRE = 103,
    BR_STEP_COND_NOT_PRE = 104,
    BR_STEP_COND_NEI = 105,
    BR_STEP_COND_NOT_NEI = 106,
    BR_STEP_COND_FILL = 107,
    BR_STEP_COND_NOT_FILL = 108,
    BR_STEP_COND_AB_BASE = 109,
    BR_STEP_COND_COUNT = 145
};
extern volatile uint16_t br_seq_synth_length[BR_SYNTH_PARTS][BR_STEPS];
extern volatile uint8_t br_seq_synth_condition[BR_SYNTH_PARTS][BR_STEPS];
extern volatile uint8_t br_seq_synth_note[BR_SYNTH_PARTS][BR_STEPS];
extern volatile uint8_t br_seq_synth_velocity[BR_SYNTH_PARTS][BR_STEPS];
extern volatile uint16_t br_seq_kit_length[BR_STEPS][BR_DRUM_LANES];
extern volatile uint8_t br_seq_kit_condition[BR_STEPS][BR_DRUM_LANES];
extern volatile uint8_t br_seq_kit_velocity[BR_STEPS][BR_DRUM_LANES];
extern volatile uint8_t br_seq_kit_note[BR_STEPS][BR_DRUM_LANES];
extern volatile uint8_t br_seq_kit_pitch[BR_STEPS][BR_DRUM_LANES];
/* Retrigger byte: low three bits extra hits, next three bits note spacing. */
#define BR_RETRIG_COUNT_MAX 7u
#define BR_RETRIG_RATE_COUNT 8u
extern volatile uint8_t br_seq_kit_retrig[BR_STEPS][BR_DRUM_LANES];
/* Signed offsets are expressed in 1/128-note units. */
enum { BR_TIME_QUANT_STEP, BR_TIME_QUANT_OFF, BR_TIME_QUANT_8,
       BR_TIME_QUANT_16, BR_TIME_QUANT_32, BR_TIME_QUANT_64,
       BR_TIME_QUANT_128, BR_TIME_QUANT_COUNT };
extern volatile int8_t br_seq_synth_micro[BR_SYNTH_PARTS][BR_STEPS];
extern volatile int8_t br_seq_kit_micro[BR_STEPS][BR_DRUM_LANES];
extern volatile uint8_t br_seq_record_quantize[BR_PARTS];
static inline int32_t br_step_micro_get(uint32_t part, uint32_t step, uint32_t lane)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return 0;
    if (part == BR_DRUM_PART) return lane < BR_DRUM_LANES ? br_seq_kit_micro[step][lane] : 0;
    return lane == 0u ? br_seq_synth_micro[part][step] : 0;
}
static inline void br_step_micro_set(uint32_t part, uint32_t step, uint32_t lane, int32_t offset)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return;
    if (offset < -16) offset = -16;
    if (offset > 15) offset = 15;
    if (part == BR_DRUM_PART) { if (lane < BR_DRUM_LANES) br_seq_kit_micro[step][lane] = (int8_t)offset; }
    else if (!lane) br_seq_synth_micro[part][step] = (int8_t)offset;
}

extern volatile uint32_t br_fill_active;

#ifdef BR_SPLIT_AUDIO
#define BR_AUDIO_API
void br_fx_meter_enable(uint32_t enabled);
uint32_t br_fx_meter_read(uint32_t part);
void br_audio_kit_meter_enable(uint32_t enabled);
uint32_t br_audio_kit_meter_read(uint32_t lane);
uint32_t br_audio_kit_meter_epoch(void);
extern br_part_t br_part[BR_PARTS];
extern volatile uint32_t br_bpm, br_playing, br_transport_phase;
extern volatile uint8_t br_midi_channels[7], br_midi_clock_enabled, br_midi_clock_active;
extern volatile uint32_t br_midi_clock_bpm;
extern volatile uint32_t br_midi_clock_generation;
extern volatile uint32_t br_solo_mask, br_global_beat;
extern volatile uint32_t br_kit_mute_mask, br_kit_solo_mask;
extern volatile uint32_t br_step_pos[BR_PARTS];
extern volatile uint32_t br_quiet_part[BR_PARTS];
extern volatile uint32_t br_arp_on;
extern volatile uint32_t br_gf_kick;
/* Punch-in FX: the effect now held (BR_PUNCH_*), the loop slice setting, and
 * the borrowed echo buffer. */
extern volatile uint32_t br_punch_effect, br_punch_loop_len;
int16_t *br_fx_echo_line(uint32_t part, uint32_t *samples);
extern br_arp_keys_t br_arp_keys[BR_SYNTH_PARTS];
extern volatile br_arp_rec_t br_arp_rec[BR_ARP_REC_SLOTS];
extern volatile uint32_t br_arp_rec_write, br_arp_rec_read;
extern volatile uint32_t br_comp_reduction_q15;
extern volatile uint32_t br_ready;
#else
#define BR_AUDIO_API static
static br_part_t br_part[BR_PARTS];
static volatile uint32_t br_bpm = 120, br_playing;
static volatile uint8_t br_midi_channels[7] = {1,2,3,4,5,6,7};
static volatile uint8_t br_midi_clock_enabled, br_midi_clock_active;
static volatile uint32_t br_midi_clock_bpm;
static volatile uint32_t br_midi_clock_generation;
static volatile uint32_t br_transport_phase;
static volatile uint32_t br_solo_mask, br_global_beat;
static volatile uint32_t br_kit_mute_mask, br_kit_solo_mask;
static volatile uint32_t br_step_pos[BR_PARTS];
static volatile uint32_t br_quiet_part[BR_PARTS];
static volatile uint32_t br_arp_on;
static volatile uint32_t br_gf_kick;
static volatile uint32_t br_punch_effect, br_punch_loop_len;
static int16_t *br_fx_echo_line(uint32_t part, uint32_t *samples);
static br_arp_keys_t br_arp_keys[BR_SYNTH_PARTS];
static volatile br_arp_rec_t br_arp_rec[BR_ARP_REC_SLOTS];
static volatile uint32_t br_arp_rec_write, br_arp_rec_read;
#endif

static inline uint32_t br_step_length_get(uint32_t part, uint32_t step, uint32_t lane)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return 0u;
    if (part == BR_DRUM_PART) {
        if (lane >= BR_DRUM_LANES) return 0u;
        return br_seq_kit_length[step][lane];
    }
    if (lane != 0u) return 0u;
    return br_seq_synth_length[part][step] ? br_seq_synth_length[part][step] :
        BR_STEP_LENGTH_DEFAULT;
}

static inline void br_step_length_set(uint32_t part, uint32_t step, uint32_t lane,
                                      uint32_t length)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return;
    if (length > BR_STEP_LENGTH_MAX) length = BR_STEP_LENGTH_MAX;
    if (part == BR_DRUM_PART) {
        if (lane < BR_DRUM_LANES) br_seq_kit_length[step][lane] = (uint16_t)length;
    } else if (lane == 0u) br_seq_synth_length[part][step] = (uint16_t)length;
}

static inline uint32_t br_step_condition_get(uint32_t part, uint32_t step, uint32_t lane)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return BR_STEP_COND_ALWAYS;
    if (part == BR_DRUM_PART) {
        if (lane >= BR_DRUM_LANES) return BR_STEP_COND_ALWAYS;
        return br_seq_kit_condition[step][lane];
    }
    return lane == 0u ? br_seq_synth_condition[part][step] : BR_STEP_COND_ALWAYS;
}

static inline void br_step_condition_set(uint32_t part, uint32_t step, uint32_t lane,
                                         uint32_t condition)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return;
    if (condition >= BR_STEP_COND_COUNT) condition = BR_STEP_COND_ALWAYS;
    if (part == BR_DRUM_PART) {
        if (lane < BR_DRUM_LANES) br_seq_kit_condition[step][lane] = (uint8_t)condition;
    } else if (lane == 0u) br_seq_synth_condition[part][step] = (uint8_t)condition;
}

static inline uint32_t br_step_note_get(uint32_t part, uint32_t step, uint32_t lane)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return 0u;
    if (part == BR_DRUM_PART) {
        if (lane >= BR_DRUM_LANES) return 0u;
        return br_seq_kit_pitch[step][lane] >= 36u &&
               br_seq_kit_pitch[step][lane] <= 84u ?
            br_seq_kit_pitch[step][lane] : BR_STEP_PITCH_DEFAULT;
    }
    if (lane != 0u) return 0u;
    if (br_part[part].step[step] & BR_STEP_ON)
        return BR_STEP_NOTE(br_part[part].step[step]);
    return br_seq_synth_note[part][step] < 128u ?
        br_seq_synth_note[part][step] : 60u;
}

static inline void br_step_note_set(uint32_t part, uint32_t step, uint32_t lane,
                                    uint32_t note)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return;
    if (part == BR_DRUM_PART) {
        if (lane >= BR_DRUM_LANES) return;
        if (note < 36u) note = 36u;
        if (note > 84u) note = 84u;
        br_seq_kit_pitch[step][lane] = (uint8_t)note;
    } else if (lane == 0u) {
        if (br_part[part].step[step] & BR_STEP_ON) {
            uint32_t word = br_part[part].step[step];
            br_part[part].step[step] = (word & ~(127u << 16)) | ((note & 127u) << 16);
        } else br_seq_synth_note[part][step] = (uint8_t)(note & 127u);
    }
}

static inline uint32_t br_step_velocity_get(uint32_t part, uint32_t step, uint32_t lane)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return 0u;
    if (part == BR_DRUM_PART)
        return lane < BR_DRUM_LANES ? br_seq_kit_velocity[step][lane] : 0u;
    if (lane != 0u) return 0u;
    if (br_part[part].step[step] & BR_STEP_ON)
        return BR_STEP_VEL(br_part[part].step[step]);
    return br_seq_synth_velocity[part][step] < 128u ?
        br_seq_synth_velocity[part][step] : 100u;
}

static inline void br_step_velocity_set(uint32_t part, uint32_t step, uint32_t lane,
                                        uint32_t velocity)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return;
    if (velocity > 127u) velocity = 127u;
    if (part == BR_DRUM_PART) {
        if (lane < BR_DRUM_LANES) br_seq_kit_velocity[step][lane] = (uint8_t)velocity;
    } else if (lane == 0u) {
        if (br_part[part].step[step] & BR_STEP_ON) {
            uint32_t word = br_part[part].step[step];
            br_part[part].step[step] = (word & ~(127u << 23)) | (velocity << 23);
        }
        br_seq_synth_velocity[part][step] = (uint8_t)velocity;
    }
}

static inline void br_step_trigger_init(uint32_t part, uint32_t step, uint32_t lane)
{
    /* A caller may have edited metadata on an inactive step. Activation must
     * preserve those choices; br_step_trigger_clear() resets metadata. */
    (void)part;
    (void)step;
    (void)lane;
}

static inline void br_step_trigger_clear(uint32_t part, uint32_t step, uint32_t lane)
{
    if (part >= BR_PARTS || step >= BR_STEPS) return;
    if (part == BR_DRUM_PART) {
        uint32_t word;
        if (lane >= BR_DRUM_LANES) return;
        word = br_part[part].step[step];
        word &= ~((1u << lane) | (1u << (lane + 8u)));
        br_part[part].step[step] = word;
        br_seq_kit_retrig[step][lane] = 0u;
        br_seq_kit_pitch[step][lane] = 255u;
        br_seq_kit_micro[step][lane] = 0;
        br_seq_kit_note[step][lane] = 255u;
        br_seq_kit_velocity[step][lane] = 100u;
        br_step_length_set(part, step, lane, 0u);
        br_step_condition_set(part, step, lane, BR_STEP_COND_ALWAYS);
    } else if (lane == 0u) {
        br_part[part].step[step] = 0u;
        br_seq_synth_note[part][step] = 255u;
        br_seq_synth_micro[part][step] = 0;
        br_seq_synth_velocity[part][step] = 255u;
        br_step_length_set(part, step, lane, 0u);
        br_step_condition_set(part, step, lane, BR_STEP_COND_ALWAYS);
    }
}

#ifdef BR_SPLIT_AUDIO
#define BR_AUDIO_JOB_SOURCE 0u
#define BR_AUDIO_JOB_FUSED_TRACK 1u
#define BR_AUDIO_JOB_KIT_FINALIZE 2u
#define BR_AUDIO_JOB_KIND_COUNT 3u   /* kinds the timing diagnostics count */
/* A synth track split in two: its source and post (at the track's own index),
 * then its effects as a tail job that depends on it. */
#define BR_AUDIO_JOB_TRACK_SOURCE 3u
#define BR_AUDIO_JOB_TRACK_FX 4u
/* In a split graph the shared reverb runs as its own independent tail job. */
#define BR_AUDIO_JOB_SHARED_REVERB 5u
#define BR_AUDIO_JOB_KIND_LIMIT 6u
#define BR_AUDIO_JOB_TIMING_CORE_COUNT 2u
typedef struct {
    uint32_t part;
    uint32_t frames;
    int32_t *output;
    uint32_t lane_mask; /* 0xff for a whole source, a fixed kit partition otherwise. */
    int32_t *key;
    uint32_t predecessor; /* UINT32_MAX or the chunk that must finish first. */
    uint32_t predecessor2; /* optional second predecessor for a kit merge. */
    uint32_t predecessor3; /* optional third predecessor for a four-lane merge. */
    uint32_t predecessor4; /* optional fourth predecessor for a four-lane merge. */
    uint32_t kind;
    uint32_t cost_hint;
    uint32_t duration_ticks; /* last completed job duration; separate from state. */
    uint32_t executor_core;
    uint32_t affinity_core;
    uint32_t fixed_plan_core;
    uint32_t fixed_plan_order;
    volatile uint32_t state; /* 0 pending, 1 claimed, 2 complete. */
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
    uint32_t failure_profile_start_tick;
    uint32_t failure_profile_flags;
#endif
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    uint32_t failure_profile_stage_flags;
    uint32_t failure_profile_stage_start_tick;
    uint32_t failure_profile_source_ticks;
    uint32_t failure_profile_merge_ticks;
    uint32_t failure_profile_post_ticks;
    uint32_t failure_profile_fx_ticks;
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
    br_audio_fx_substage_trace_t failure_profile_fx_substage;
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
    br_audio_model_substage_trace_t failure_profile_model_substage;
#endif
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
    uint32_t activity_class; /* sealed on CPU0 before worker publication */
#endif
} br_audio_source_job_t;
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
#define BR_AUDIO_MAX_SOURCE_JOBS (BR_PARTS * 5u)
#else
#define BR_AUDIO_MAX_SOURCE_JOBS (BR_PARTS * 4u)
#endif
typedef struct {
    volatile uint8_t lock;
    volatile uint32_t count;
    volatile uint32_t completed;
    uint32_t deadline_tick;
    volatile uint32_t fixed_plan_active;
    volatile uint32_t fixed_plan_count[2];
    volatile uint32_t fixed_plan_cursor[2];
    volatile uint32_t fixed_plan_completed[2];
} br_audio_source_queue_t;
/* Stable, CPU0-sealed timing for the latest fully joined fused source queue.
 * Values are T4 ticks on device; only the optional timing diagnostic build
 * collects them. The kind indices match BR_AUDIO_JOB_* above. */
typedef struct {
    uint32_t version;
    uint32_t generation;
    uint32_t valid;
    uint32_t frames;
    uint32_t job_count;
    uint32_t completed;
    uint32_t queue_wall_ticks;
    uint32_t core_ticks[BR_AUDIO_JOB_TIMING_CORE_COUNT];
    uint32_t core_jobs[BR_AUDIO_JOB_TIMING_CORE_COUNT];
    uint32_t kind_ticks[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_AUDIO_JOB_KIND_COUNT];
    uint32_t kind_jobs[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_AUDIO_JOB_KIND_COUNT];
    uint32_t part_ticks[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_PARTS];
    uint32_t part_jobs[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_PARTS];
#if BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2
    uint32_t fixed8_ready_queue_makespan_ticks;
    uint32_t fixed8_predicted_makespan_ticks;
    uint32_t fixed8_plan_active;
    uint32_t fixed8_rejection_reason;
    uint32_t fixed8_prepare_ticks;
#endif
#if BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG
    uint32_t job_row_count; /* captured rows, capped at eight */
    struct {
        uint32_t meta;
        uint32_t cost_hint_core0;
        uint32_t cost_hint_core1;
        uint32_t duration_ticks;
    } job_rows[8];
#endif
} br_audio_source_job_timing_t;
#if BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG
#if BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2
#define BR_AUDIO_SOURCE_JOB_TIMING_WORDS 77u
#else
#define BR_AUDIO_SOURCE_JOB_TIMING_WORDS 72u
#endif
#elif BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2
#define BR_AUDIO_SOURCE_JOB_TIMING_WORDS 44u
#else
#define BR_AUDIO_SOURCE_JOB_TIMING_WORDS 39u
#endif
void br_audio_source_job_timing_snapshot(br_audio_source_job_timing_t *out);
#if BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG
/* Foreground diagnostic page reads never spin indefinitely on a live writer. */
uint32_t br_audio_source_job_timing_snapshot_bounded(
    br_audio_source_job_timing_t *out, uint32_t max_attempts);
#endif
/* Called with audio IRQs masked together with the existing audio metric reset. */
void br_audio_source_job_timing_reset(void);
/* Stage attribution for the same retained peak queue. Each count word packs a
 * four-bit source-job count (low nibble) and FX-job count (high nibble). */
typedef struct {
    uint32_t version;
    uint32_t generation;
    uint32_t valid;
    uint32_t frames;
    uint32_t job_count;
    uint32_t completed;
    uint32_t queue_wall_ticks;
    uint32_t core_mask;
    uint32_t source_ticks[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_PARTS];
    uint32_t fx_ticks[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_PARTS];
    uint32_t packed_jobs[BR_AUDIO_JOB_TIMING_CORE_COUNT][BR_PARTS];
} br_audio_source_stage_timing_t;
#define BR_AUDIO_SOURCE_STAGE_TIMING_WORDS 32u
void br_audio_source_stage_timing_snapshot(br_audio_source_stage_timing_t *out);
typedef struct {
    uint32_t count;
    uint32_t job_index[BR_AUDIO_MAX_SOURCE_JOBS];
} br_audio_source_batch_t;
/* Internal callback used by the audio-source worker. CPU0 prepares all
 * events and controls first; each job owns one part's DSP state and bus row. */
uint32_t br_audio_worker_sources(void *opaque_job);
/* Fixed, whitelisted CPU1 task. It accepts only the resident source queue and
 * indexes the static source-job table; callers cannot submit function pointers. */
uint32_t br_audio_worker_source_batch(void *opaque_batch);
uint32_t br_audio_worker_source_queue_context_valid(void *opaque_queue);
uint32_t br_engine_source_mode_get(void);
void br_engine_source_mode_set(uint32_t mode);
uint32_t br_engine_source_fx_fusion_get(void);
void br_engine_source_fx_fusion_set(uint32_t enabled);
void br_audio_source_diagnostic_snapshot(uint32_t out[4]);
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
void br_audio_source_cost_diagnostic_snapshot(uint32_t out[8]);
#endif
#if defined(FM1_DEVICE)
void fm1_audio_cpu1_join_clear(void);
void fm1_audio_cpu1_join_mark(uint32_t tick);
#endif
uint32_t br_fx_worker_track_batch(void *opaque_batch);
#if !BR_AUDIO_FUSION_ONLY_BUILD
uint32_t br_fx_worker_hybrid_batch(void *opaque_context);
#endif
uint32_t br_fx_fusion_prepare(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n);
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
void br_fx_fusion_process_track(uint32_t part, const int32_t *input,
                                uint32_t n,
                                br_audio_fx_substage_trace_t *trace);
#else
void br_fx_fusion_process_track(uint32_t part, const int32_t *input, uint32_t n);
#endif
void br_fx_fusion_cancel(void);
/* Called only after the source worker has joined. This repeats the exact
 * bus/frame/track completion checks used by br_fx_process. */
uint32_t br_fx_fusion_complete(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n);
uint32_t br_fx_fusion_matches_mask(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n,
                                   uint32_t expected_mask);
uint32_t br_fx_fusion_cost(uint32_t part);
/* Shared reverb as its own job (split graphs): whether it runs this block, mark
 * this block's reverb as separate (track 3 then skips it), run it, and its last cost. */
uint32_t br_fx_shared_reverb_runs(void);
void br_fx_shared_reverb_separate(void);
void br_fx_fusion_shared_reverb(uint32_t n);
uint32_t br_fx_shared_reverb_cost(void);
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
extern volatile uint32_t br_audio_source_worker_fault;
#endif
#endif

enum {
    BR_SELECT_MASTER_FILTER, BR_SELECT_MASTER_DISTORTION, /* retired saved ID */
    BR_SELECT_MASTER_SATURATION, BR_SELECT_MASTER_WOW,
    BR_SELECT_TEMPO, BR_SELECT_TRACK_LEVEL, BR_SELECT_MASTER_COMP, BR_SELECT_COUNT
};
enum {
    BR_COMP_OFF, BR_COMP_ALL, BR_COMP_TRACK_1, BR_COMP_TRACK_2,
    BR_COMP_TRACK_3, BR_COMP_DRUM_1, BR_COMP_DRUM_2, BR_COMP_DRUM_3,
    BR_COMP_DRUM_4, BR_COMP_SOURCE_COUNT
};
typedef struct {
    uint32_t source, threshold, ratio, makeup, attack, release, hold, mix;
} br_compressor_config_t;
/* Threshold is -48..0 dB, ratio selects 1/2/3/4/6/8/12/20:1, makeup
 * is 0..24 dB. Timing and parallel mix use the usual 0..127 knob range. */
/* Preserve the old numeric T4 source as the first drum-lane source. */
#define BR_COMP_TRACK_4 BR_COMP_DRUM_1
static inline __attribute__((always_inline)) uint32_t br_comp_source_part(uint32_t source)
{ return source >= BR_COMP_DRUM_1 && source <= BR_COMP_DRUM_4 ? BR_DRUM_PART :
    source >= BR_COMP_TRACK_1 && source <= BR_COMP_TRACK_3 ? source - BR_COMP_TRACK_1 : BR_PARTS; }
static inline __attribute__((always_inline)) uint32_t br_comp_source_drum_lane(uint32_t source)
{ return source >= BR_COMP_DRUM_1 && source <= BR_COMP_DRUM_4 ? source - BR_COMP_DRUM_1 : 0u; }
#define BR_COMP_DEFAULTS {BR_COMP_OFF, 18u, 3u, 0u, 12u, 40u, 0u, 127u}
enum { BR_HEAT_BIT, BR_HEAT_SAT, BR_HEAT_WOW, BR_HEAT_COUNT };
/* Legacy fields remain for migration, never as independent active processors.
 * New controls select exactly one HEAT engine and one amount. */
typedef struct {
    uint32_t filter, distortion, saturation, wow, resonance;
    uint32_t heat_type, heat_amount;
    /* Global filter: bipolar depth of the note-triggered envelope (-127..127) and the
     * smoothness of cutoff moves (0..100). Stored in the spare legacy gate bytes. */
    int32_t env_depth;
    uint32_t smooth;
} br_master_fx_t;
typedef struct {
    volatile uint32_t revision;
    volatile br_master_fx_t fx;
    volatile uint32_t select_target;
    volatile br_compressor_config_t comp;
} br_master_t;
 #ifdef BR_SPLIT_AUDIO
extern br_master_t br_master;
extern br_compressor_config_t br_source_compressor;
extern uint32_t br_source_compressor_revision;
#else
static br_master_t br_master = {0, {64, 0, 0, 0, 0, BR_HEAT_BIT, 0}, BR_SELECT_MASTER_FILTER, BR_COMP_DEFAULTS};
static br_compressor_config_t br_source_compressor = BR_COMP_DEFAULTS;
static uint32_t br_source_compressor_revision = 0xffffffffu;
#endif

/* One immutable block's controls, captured after source preparation and
 * consumed by the next block's FX pass in the optional 32-frame pipeline. */
typedef struct {
    br_sound_t sound[BR_PARTS];
    br_fx_config_t fx[BR_PARTS];
    br_master_fx_t master_fx;
    br_compressor_config_t compressor;
    uint32_t revision;
} br_pipeline_controls_t;
typedef struct {
    int32_t bus[BR_PARTS][BR_BLOCK];
    int32_t key[BR_PARTS][BR_BLOCK];
    br_pipeline_controls_t controls;
    uint32_t frames, valid;
} br_pipeline_frame_t;

/* Audio-only selected-lane taps, one bounded block per track. Tonal
 * sidechains use their track bus; only the selected kit lane enters its key. */
#ifdef BR_SPLIT_AUDIO
extern int32_t br_comp_key[BR_PARTS][BR_BLOCK];
#elif defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static int32_t br_comp_key[BR_PARTS][BR_BLOCK] __attribute__((section(".pool")));
#else
static int32_t br_comp_key[BR_PARTS][BR_BLOCK];
#endif

/* Audio owner: original BRAIDS adapter and compact drum state live in its pool.
 * Main-loop note methods enqueue events; render is the sole DSP-state writer. */
#ifdef BR_SPLIT_AUDIO
void br_engine_init(void);
void br_engine_render(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n);
void br_engine_post_join_finish(void);
uint32_t br_engine_late_drop_get(void);
void br_audio_late_drop_snapshot(uint32_t out[9]);
void br_audio_late_drop_reset(void);
void br_engine_pipeline_begin(br_pipeline_frame_t *previous, int32_t *out, uint32_t n);
/* 1 = output complete/safe; 0 = failed but worker is contained and output
 * cleared; 2 = worker ownership could not be revoked, so output must not be
 * touched or reused. */
uint32_t br_engine_pipeline_finish(void);
uint32_t br_engine_pipeline_capture(br_pipeline_frame_t *current,
                                    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n);
void br_engine_pipeline_reset(void);
void br_engine_pipeline_source_deadline_set(uint32_t absolute_tick);
void br_engine_pipeline_source_deadline_clear(void);
uint32_t br_engine_pipeline_mode_get(void);
uint32_t br_engine_pipeline_uncontained_fault_get(void);
uint32_t br_engine_pipeline_mode_set(uint32_t mode);
uint32_t br_engine_pipeline_generation_get(void);
void br_engine_note_on(uint32_t part, uint32_t note, uint32_t velocity);
void br_engine_note_off(uint32_t part, uint32_t note);
void br_engine_midi_transport(uint8_t status);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
void br_engine_clock_diag_snapshot(uint32_t out[23]);
void br_engine_clock_dma_edge(uint32_t tick, uint32_t half, uint32_t lead_frames);
void br_engine_clock_dma_invalid(void);
void br_engine_clock_midi_event(uint8_t status, uint32_t tick, uint32_t generation);
void br_engine_clock_source_changed(uint32_t tick);
#endif
uint32_t br_engine_record_clock(uint32_t part);
uint32_t br_engine_step_duration(uint32_t part, uint32_t step);
void br_engine_part_off(uint32_t part);
void br_engine_all_off(void);
int br_engine_quiet(void);
const char *br_preset_name(uint32_t preset);
void br_preset_sound(uint32_t preset, br_sound_t *sound);
void br_fx_init(void);
void br_fx_warm_controls_quiescent(void);
void br_fx_process(int32_t bus[BR_PARTS][BR_BLOCK], int32_t *out, uint32_t n);
void br_fx_set_serial_mode(uint32_t enabled);
uint32_t br_fx_serial_mode_get(void);
int br_fx_quiet(void);
uint32_t br_fx_delay_samples(uint32_t value);
uint32_t br_fx_delay_note_index(uint32_t stored);
uint32_t br_fx_delay_note_store(uint32_t index);
int32_t br_clip(int32_t x, int32_t low, int32_t high);
uint32_t br_time_samples(uint32_t value, uint32_t scale);
uint32_t br_lfo_rate_millihz(uint32_t rate);
uint32_t br_audio_lfo_phase(uint32_t part, uint32_t lfo);
void br_engine_record_position(uint32_t part, uint32_t *step, uint32_t *elapsed, uint32_t *duration, uint32_t *unit);
int32_t br_audio_patch_value(uint32_t part, uint32_t slot);
int32_t br_comp_threshold_db(uint32_t value);
uint32_t br_comp_attack_samples(uint32_t value);
uint32_t br_comp_release_samples(uint32_t value);
uint32_t br_comp_hold_samples(uint32_t value);
#else
/* The single-file fallback profile has no source/FX overlap pipeline. Keep
 * dinghy's shared audio callback linkable with the selector safely off. */
static inline void br_engine_pipeline_begin(br_pipeline_frame_t *previous,
                                             int32_t *out, uint32_t n)
{ (void)previous; (void)out; (void)n; }
static inline uint32_t br_engine_pipeline_finish(void) { return 1u; }
static inline uint32_t br_engine_pipeline_capture(br_pipeline_frame_t *current,
                                                   int32_t bus[BR_PARTS][BR_BLOCK],
                                                   uint32_t n)
{ (void)current; (void)bus; (void)n; return 0u; }
static inline void br_engine_pipeline_reset(void) { }
static inline uint32_t br_engine_pipeline_mode_get(void) { return 0u; }
uint32_t br_engine_pipeline_uncontained_fault_get(void);
static inline uint32_t br_engine_pipeline_mode_set(uint32_t mode)
{ (void)mode; return 0u; }
static inline uint32_t br_engine_pipeline_generation_get(void) { return 0u; }
static void br_engine_init(void);
static void br_engine_render(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n);
static void br_engine_post_join_finish(void);
static inline uint32_t br_engine_late_drop_get(void) { return 0u; }
static inline void br_audio_late_drop_snapshot(uint32_t out[9])
{ for (uint32_t i = 0u; i < 9u; ++i) out[i] = 0u; }
static inline void br_audio_late_drop_reset(void) { }
static void br_engine_note_on(uint32_t part, uint32_t note, uint32_t velocity);
static void br_engine_note_off(uint32_t part, uint32_t note);
static void br_engine_midi_transport(uint8_t status);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
static void br_engine_clock_diag_snapshot(uint32_t out[23]);
static void br_engine_clock_dma_edge(uint32_t tick, uint32_t half, uint32_t lead_frames);
static void br_engine_clock_dma_invalid(void);
static void br_engine_clock_midi_event(uint8_t status, uint32_t tick, uint32_t generation);
static void br_engine_clock_source_changed(uint32_t tick);
#endif
static uint32_t br_engine_record_clock(uint32_t part);
static uint32_t br_engine_step_duration(uint32_t part, uint32_t step);
static void br_engine_part_off(uint32_t part);
static void br_engine_all_off(void);
static int br_engine_quiet(void);
static void br_fx_warm_controls_quiescent(void);
static int br_fx_quiet(void);
static const char *br_preset_name(uint32_t preset);
static void br_preset_sound(uint32_t preset, br_sound_t *sound);
#endif

/* Clear volatile key-down recording state after startup or session restore. */
void br_ui_recording_state_reset(void);

#if !BR_AUDIO_FUSION_ONLY_BUILD
void br_fx_capture_pipeline_controls(br_pipeline_controls_t *controls);
void br_fx_process_pipeline(int32_t bus[BR_PARTS][BR_BLOCK],
                            int32_t keys[BR_PARTS][BR_BLOCK],
                            const br_pipeline_controls_t *controls,
                            int32_t *out, uint32_t n);
uint32_t br_fx_process_pipeline_reverse_start(int32_t bus[BR_PARTS][BR_BLOCK],
    int32_t keys[BR_PARTS][BR_BLOCK], const br_pipeline_controls_t *controls,
    int32_t *out, uint32_t n);
uint32_t br_fx_process_pipeline_hybrid_start(
    int32_t bus[BR_PARTS][BR_BLOCK], int32_t keys[BR_PARTS][BR_BLOCK],
    const br_pipeline_controls_t *controls, int32_t *out, uint32_t n,
    uint32_t fx_valid, void *source_queue);
uint32_t br_fx_process_pipeline_reverse_finish(int32_t *out, uint32_t n,
                                                uint32_t timeout_ticks);
#endif

#endif

#if defined(FM1_DEVICE)
extern volatile uint32_t br_diagnostic_synth_ticks;
/* Absolute T4 stamps through the serial tail of the current half (0 = not
 * reached), read by the platform at ACK for diagnostic command 0xC8. */
#define BR_TAIL_FX_START 0u
#define BR_TAIL_MIX_END 1u
#define BR_TAIL_WAIT_END 2u
#define BR_TAIL_FX_END 3u
#define BR_TAIL_COMMIT_START 4u
#define BR_TAIL_COMMIT_END 5u
#define BR_TAIL_STAMPS 6u
extern volatile uint32_t br_tail_stamp[BR_TAIL_STAMPS];
/* Sub-stamps between the CPU1 join and the FX stage, read via 0xCA:
 * validation done, scheduler history done, diagnostics/predictor done,
 * modulation start, modulation done. */
#define BR_GAP_STAMPS 5u
extern volatile uint32_t br_gap_stamp[BR_GAP_STAMPS];
/* Shared-reverb job: blocks run, guard hits, last and worst job ticks (0xCD). */
extern volatile uint32_t br_gr_stats[8];
/* The app passes the block's output gain before the FX stage; when CPU1's
 * master-FX stream also applies it, the platform skips its own scaling. */
extern volatile int32_t br_fx_output_gain_q12;
extern volatile uint32_t br_fx_output_scale_request, br_fx_output_prescaled;
#endif

#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
BR_AUDIO_API uint32_t br_fx_bounded_pole_benchmark(uint32_t mode, uint32_t seed);
#endif
