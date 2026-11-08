/* SPDX-License-Identifier: GPL-3.0-only */
/* Four logical drum lanes. BRAIDS uses the original compact percussion
 * models; 808/909 use the resident IMA assets in the const sample bank. */
#include "instrument_drums.h"
#ifndef BR_BRAIDS_SEEDED_NOTE_ON
#define BR_BRAIDS_SEEDED_NOTE_ON 1
#endif
#include "../hal/fm1_audio_rate.h"
#include "instrument_sample_player.h"
#include <string.h>
#include "../braids/braids_perc_c_api.h"
#include "instrument_sample_player.c"

#define Q15_ONE 32768u
#define DRUM_DECAY_MIN 1u
#define DRUM_FORCED_FADE ((64u * FM1_AUDIO_RATE_HZ + 22050u) / 44100u)

#if FM1_NATIVE_OUTPUT_32K
/* Matched one-pole decay coefficients: alpha32 =
 * 1 - (1 - alpha44)^((44.1 kHz)/(32 kHz)). Generated from the original
 * alpha = 256 + color * 240 curve, rounded to Q15. */
static const uint16_t br_drum_alpha_32k[128] = {
    352, 682, 1010, 1337, 1664, 1990, 2314, 2638,
    2961, 3282, 3603, 3923, 4242, 4560, 4877, 5193,
    5508, 5822, 6135, 6447, 6758, 7068, 7376, 7684,
    7991, 8297, 8602, 8905, 9208, 9510, 9810, 10110,
    10408, 10705, 11002, 11297, 11591, 11883, 12175, 12466,
    12755, 13044, 13331, 13617, 13902, 14185, 14468, 14749,
    15029, 15308, 15586, 15862, 16137, 16411, 16684, 16955,
    17226, 17495, 17762, 18028, 18293, 18557, 18819, 19080,
    19340, 19598, 19855, 20111, 20365, 20617, 20869, 21118,
    21367, 21613, 21859, 22103, 22345, 22586, 22825, 23063,
    23299, 23533, 23766, 23997, 24227, 24455, 24681, 24905,
    25128, 25349, 25568, 25785, 26001, 26214, 26426, 26636,
    26844, 27050, 27254, 27455, 27655, 27853, 28048, 28241,
    28432, 28621, 28808, 28992, 29173, 29352, 29529, 29703,
    29874, 30043, 30208, 30371, 30531, 30688, 30842, 30992,
    31139, 31282, 31422, 31558, 31689, 31817, 31940, 32058
};
#endif

typedef struct {
    uint32_t env, decay_q15, age, rng;
    uint32_t gate_limit_frames, forced_fade;
    uint32_t tail_fade;
    int32_t tail_sample, last_output;
    uint16_t note, velocity;
    int8_t pitch_delta;
    uint8_t active, pending, voice_id, length_choked;
    uint8_t pending_voice_id, voice_override;
    br_drum_sample_player_t sample;
} br_drum_voice_t;

typedef struct {
    uint8_t perc[BR_DRUM_LANES][BRAIDS_PERC_STATE_BYTES];
    br_drum_voice_t voice[BR_DRUM_LANES];
    uint32_t seed;
    uint32_t kit;
    int16_t braids_render_scratch[BR_DRUM_LANES][BR_BLOCK];
} br_drum_state_t;

typedef char br_drum_state_fits[(sizeof(br_drum_state_t) <= BR_DRUM_STATE_BYTES) ? 1 : -1];
typedef char br_drum_masked_state_fits[(sizeof(br_drum_state_t) <= BR_DRUM_MASKED_STATE_BYTES) ? 1 : -1];

/* The old factory timbre is retained for BRAIDS synthesis. */
static uint32_t br_drum_rng(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x ? x : 0x6D2B79F5u;
    return *state;
}

static uint32_t br_drum_decay_coeff(uint32_t tau_ms)
{
    /* 742 is the original per-ms Q15 coefficient at 44.1 kHz. */
    uint32_t per_ms = (742u * 44100u + FM1_AUDIO_RATE_HZ / 2u) /
                      FM1_AUDIO_RATE_HZ;
    uint32_t d = per_ms / (tau_ms ? tau_ms : 1u);
    if (!d) d = 1u;
    return Q15_ONE - d;
}

static uint32_t br_drum_lifetime_gain(const br_drum_voice_t *v)
{
    return v->forced_fade ? v->forced_fade * 32767u / DRUM_FORCED_FADE : 32767u;
}

static uint32_t br_drum_latch_gate(br_drum_voice_t *v, uint32_t gate)
{
    if (gate && (!v->gate_limit_frames || gate < v->gate_limit_frames))
        v->gate_limit_frames = gate;
    return v->gate_limit_frames;
}

static uint32_t br_drum_length_tau_ms(uint32_t length_samples)
{
    uint32_t ms = length_samples * 1000u / FM1_AUDIO_RATE_HZ;
    uint32_t tau = ms / 4u;
    return tau ? tau : 1u;
}

static void br_drum_start_tail_fade(br_drum_voice_t *v)
{
    if (v->active && v->last_output) {
        v->tail_sample = v->last_output;
        v->tail_fade = DRUM_FORCED_FADE;
    }
    v->last_output = 0;
}

static void br_drum_emit_tail_fade(br_drum_voice_t *v, uint32_t lane,
                                   uint32_t key_lane, int32_t *out,
                                   int32_t *key, uint32_t n)
{
    uint32_t i;
    if (!v->tail_fade) return;
    for (i = 0; i < n && v->tail_fade; ++i) {
        int32_t contribution = (int32_t)(((int64_t)v->tail_sample * v->tail_fade) /
                                         DRUM_FORCED_FADE);
        out[i] += contribution;
        if (key && lane == key_lane) key[i] += contribution;
        --v->tail_fade;
    }
    if (!v->tail_fade) v->tail_sample = 0;
}

static uint32_t br_drum_braids_model(uint32_t voice_id)
{
    switch (voice_id) {
    case BR_DRUM_KCK1: return BRAIDS_PERC_KICK;
    case BR_DRUM_KCK2: return BRAIDS_PERC_DRUM;
    case BR_DRUM_KCK3: return BRAIDS_PERC_KICK;
    case BR_DRUM_SNAR: return BRAIDS_PERC_SNARE;
    case BR_DRUM_RIM: return BRAIDS_PERC_SNARE;
    case BR_DRUM_CLAP: return BRAIDS_PERC_CLAP;
    case BR_DRUM_OPEN:
    case BR_DRUM_CLOS:
    case BR_DRUM_PEDL: return BRAIDS_PERC_HAT;
    case BR_DRUM_CYMB: return BRAIDS_PERC_BELL;
    case BR_DRUM_PERC: return BRAIDS_PERC_DRUM;
    case BR_DRUM_TOM: return BRAIDS_PERC_DRUM;
    default: return BRAIDS_PERC_KICK;
    }
}

static int32_t br_drum_voice_note_offset(uint32_t voice_id)
{
    /* These offsets preserve the existing Plaits low-tom, rim, and cymbal
     * pitch relationships for their new second-choice lane mappings. */
    return voice_id == BR_DRUM_KCK2 ? 12 :
           voice_id == BR_DRUM_KCK3 ? -5 :
           voice_id == BR_DRUM_PERC ? 7 :
           voice_id == BR_DRUM_RIM ? 12 :
           voice_id == BR_DRUM_PEDL ? 12 :
           voice_id == BR_DRUM_CLOS ? -4 :
           voice_id == BR_DRUM_CYMB ? -12 :
           voice_id == BR_DRUM_TOM ? -12 : 0;
}

static int32_t br_drum_clamp_pitch_delta(int32_t pitch_delta)
{
    if (pitch_delta < -24) return -24;
    if (pitch_delta > 24) return 24;
    return pitch_delta;
}

static void br_drum_braids_controls(void *state, uint32_t voice_id,
                                    const br_sound_t *sound)
{
    uint32_t color = sound->color > 127u ? 127u : sound->color;
    uint32_t timbre = sound->timbre > 127u ? 127u : sound->timbre;
    if (voice_id == BR_DRUM_SNAR || voice_id == BR_DRUM_RIM ||
        voice_id == BR_DRUM_CLAP) {
        uint32_t snap = sound->drum_snap > 127u ? 127u : sound->drum_snap;
        color = (color + snap) / 2u;
    }
    braids_perc_controls(state, (uint16_t)(timbre * 32767u / 127u),
                         (uint16_t)(color * 32767u / 127u));
}

static void br_drum_start_voice(br_drum_state_t *s, uint32_t lane,
                                const br_sound_t *sound, uint32_t gate_frames)
{
    br_drum_voice_t *v = &s->voice[lane];
    uint32_t voice_id = v->pending_voice_id < BR_DRUM_VOICE_COUNT ?
        v->pending_voice_id : br_drum_voice_id(sound->drum_voice_mask, lane);
    uint32_t decay = sound->decay > 127u ? 127u : sound->decay;
    v->voice_id = (uint8_t)voice_id;
    v->voice_override = v->pending_voice_id < BR_DRUM_VOICE_COUNT;
    v->pending_voice_id = 255u;
    v->pending = 0u;
    v->active = 0u;
    v->age = 0u;
    v->env = Q15_ONE;
    v->gate_limit_frames = gate_frames;
    v->forced_fade = 0u;
    v->length_choked = 0u;
    v->last_output = 0;
    if (s->kit == BR_KIT_BRAIDS) {
        uint32_t model = br_drum_braids_model(voice_id);
        int32_t note = (int32_t)br_drum_lane_note(lane) + sound->tune +
                       v->pitch_delta +
                       br_drum_voice_note_offset(voice_id);
        uint32_t timbre = sound->timbre > 127u ? 127u : sound->timbre;
        uint32_t color = sound->color > 127u ? 127u : sound->color;
        if (voice_id == BR_DRUM_SNAR || voice_id == BR_DRUM_RIM ||
            voice_id == BR_DRUM_CLAP) {
            uint32_t snap = sound->drum_snap > 127u ? 127u : sound->drum_snap;
            color = (color + snap) / 2u;
        }
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        /* One engine state per logical lane; reseed and initialize it on every
         * retrigger so a previous voice choice can never resume a hidden tail. */
#if BR_BRAIDS_SEEDED_NOTE_ON
        braids_perc_note_on_seed(s->perc[lane], br_drum_rng(&v->rng), model,
            note * 128, (uint16_t)(timbre * 32767u / 127u),
            (uint16_t)(color * 32767u / 127u),
            (uint16_t)((v->velocity > 127u ? 127u : v->velocity) * 32767u / 127u));
#else
        braids_perc_init(s->perc[lane], br_drum_rng(&v->rng));
        braids_perc_note_on(s->perc[lane], model, note * 128,
            (uint16_t)(timbre * 32767u / 127u),
            (uint16_t)(color * 32767u / 127u),
            (uint16_t)((v->velocity > 127u ? 127u : v->velocity) * 32767u / 127u));
#endif
        v->decay_q15 = br_drum_decay_coeff(
            br_drum_length_tau_ms(br_note_length_samples(decay)));
        v->active = 1u;
    } else {
        const br_drum_sample_desc_t *sample =
            &br_drum_sample_bank[s->kit == BR_KIT_909 ? 1u : 0u][voice_id];
        v->active = (uint8_t)br_drum_sample_player_start(
            &v->sample, sample,
            (int32_t)sound->tune + v->pitch_delta, decay);
    }
}

__attribute__((minsize, noinline)) void br_drum_init(void *memory, uint32_t seed, uint32_t kit)
{
    br_drum_state_t *s = (br_drum_state_t *)memory;
    uint32_t lane;
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->seed = seed ? seed : 0x5EED1234u;
    s->kit = kit == BR_KIT_909 ? BR_KIT_909 :
             kit == BR_KIT_BRAIDS ? BR_KIT_BRAIDS : BR_KIT_808;
    for (lane = 0; lane < BR_DRUM_LANES; ++lane) {
        s->voice[lane].rng = s->seed ^ (0x9E3779B9u * (lane + 1u));
        if (!s->voice[lane].rng) s->voice[lane].rng = lane + 1u;
        s->voice[lane].voice_id = (uint8_t)(lane * 2u);
        s->voice[lane].pending_voice_id = 255u;
        if (s->kit == BR_KIT_BRAIDS)
            braids_perc_init(s->perc[lane], s->voice[lane].rng);
    }
}

void br_drum_choke(void *memory, uint32_t lane)
{
    br_drum_choke_mask(memory, lane, BR_DRUM_ALL_MASK);
}

void br_drum_choke_mask(void *memory, uint32_t lane, uint32_t lane_mask)
{
    br_drum_state_t *s = (br_drum_state_t *)memory;
    br_drum_voice_t *v;
    if (!s || lane >= BR_DRUM_LANES || !(lane_mask & (1u << lane))) return;
    v = &s->voice[lane];
    if (v->pending) { v->pending = 0u; v->active = 0u; }
    if (!v->active) return;
    if (s->kit == BR_KIT_BRAIDS) {
        braids_perc_choke(s->perc[lane]);
        v->length_choked = 1u;
    } else if (!v->forced_fade) {
        v->forced_fade = DRUM_FORCED_FADE;
    }
}

void br_drum_note_on(void *memory, uint32_t note, uint32_t velocity)
{
    br_drum_note_on_mask(memory, note, velocity, BR_DRUM_ALL_MASK);
}

void br_drum_note_on_mask(void *memory, uint32_t note, uint32_t velocity,
                          uint32_t lane_mask)
{
    br_drum_note_on_voice_mask(memory, note, velocity, lane_mask, 255u);
}

void br_drum_note_on_voice_mask(void *memory, uint32_t note, uint32_t velocity,
                                uint32_t lane_mask, uint32_t voice_id)
{
    br_drum_note_on_voice_pitch_mask(memory, note, velocity, lane_mask,
                                     voice_id, 0);
}

void br_drum_note_on_voice_pitch_mask(void *memory, uint32_t note,
                                     uint32_t velocity, uint32_t lane_mask,
                                     uint32_t voice_id, int32_t pitch_delta)
{
    br_drum_state_t *s = (br_drum_state_t *)memory;
    uint32_t lane;
    br_drum_voice_t *v;
    if (!s) return;
    lane = br_drum_lane(note);
    if (lane >= BR_DRUM_LANES || !(lane_mask & (1u << lane))) return;
    if (velocity > 127u) velocity = 127u;
    if (!velocity) return;
    if (voice_id >= BR_DRUM_VOICE_COUNT ||
        br_drum_voice_lane(voice_id) != lane) voice_id = 255u;
    v = &s->voice[lane];
    /* MIDI aliases choose a role, while the four choice bits select the voice
     * at render time. Store the canonical role note, not the alias pitch. */
    v->note = (uint16_t)br_drum_lane_note(lane);
    v->velocity = (uint16_t)velocity;
    v->pitch_delta = (int8_t)br_drum_clamp_pitch_delta(pitch_delta);
    v->pending_voice_id = (uint8_t)voice_id;
    br_drum_start_tail_fade(v);
    v->pending = 1u;
    v->active = 0u;
    v->age = 0u;
    v->forced_fade = 0u;
    br_drum_sample_player_reset(&v->sample);
}

static int32_t br_drum_sample_filter(br_drum_voice_t *v,
                                     const br_sound_t *sound,
                                     int32_t input)
{
    uint32_t timbre = sound->timbre > 127u ? 127u : sound->timbre;
    uint32_t snap = sound->drum_snap > 127u ? 127u : sound->drum_snap;
#if FM1_NATIVE_OUTPUT_32K
    int32_t alpha = br_drum_alpha_32k[timbre];
#else
    int32_t alpha = (int32_t)(256u + timbre * 240u); /* one-pole Q15 */
#endif
    int32_t high;
    int32_t sample;
    v->sample.lowpass += ((input - v->sample.lowpass) * alpha) >> 15;
    high = input - v->sample.lowpass;
    /* TIMBRE now owns the sample's low-pass corner; the retired COLOR value
     * cannot silently alter the 808/909 kits after it leaves the HOME page. */
    sample = v->sample.lowpass;
    if (v->age < DRUM_FORCED_FADE)
        sample += (high * (int32_t)snap *
                   (int32_t)(DRUM_FORCED_FADE - v->age)) >> 15;
    return sample;
}

static int32_t br_drum_render_sample(br_drum_voice_t *v,
                                     const br_sound_t *sound,
                                     uint32_t level,
                                     uint32_t level_shift,
                                     uint32_t lane_gate)
{
    uint32_t cutoff = v->gate_limit_frames;
    int32_t input, sample;
    int32_t contribution;
    if (cutoff && v->age >= cutoff && !v->forced_fade)
        v->forced_fade = DRUM_FORCED_FADE;
    if (!br_drum_sample_player_next(&v->sample, &input)) {
        v->active = 0u;
        return 0;
    }
    sample = br_drum_sample_filter(v, sound, input);
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    sample = sample * (int32_t)v->velocity >> 7;
    sample = sample * (int32_t)level >> level_shift;
    sample = sample >> 2; /* fixed 1/4 kit headroom */
    sample = sample * (int32_t)br_drum_lifetime_gain(v) >> 15;
    contribution = sample;
    v->last_output = contribution;
    ++v->age;
    if (v->forced_fade) {
        --v->forced_fade;
        if (!v->forced_fade) {
            v->active = 0u;
            br_drum_sample_player_reset(&v->sample);
        }
    }
    (void)lane_gate;
    return contribution;
}

void br_drum_render_gated(void *memory, const br_sound_t *sound,
                          const uint32_t lane_level[BR_DRUM_LANES],
                          const uint32_t lane_gate_frames[BR_DRUM_LANES],
                          int32_t *out, uint32_t n)
{
    br_drum_render_keyed(memory, sound, lane_level, lane_gate_frames, out, 0, n);
}

void br_drum_render_keyed(void *memory, const br_sound_t *sound,
                          const uint32_t lane_level[BR_DRUM_LANES],
                          const uint32_t lane_gate_frames[BR_DRUM_LANES],
                          int32_t *out, int32_t *key, uint32_t n)
{
    br_drum_render_keyed_mask(memory, sound, lane_level, lane_gate_frames,
                              BR_DRUM_ALL_MASK, out, key, n);
}

static void br_drum_render_keyed_mask_scaled(
                               void *memory, const br_sound_t *sound,
                               const uint32_t lane_level[BR_DRUM_LANES],
                               const uint32_t lane_gate_frames[BR_DRUM_LANES],
                               uint32_t lane_mask, int32_t *out, int32_t *key,
                               uint32_t n, uint32_t level_shift)
{
    br_drum_state_t *s = (br_drum_state_t *)memory;
    uint32_t lane, i;
    uint32_t key_lane;
    if (!s || !sound || !lane_level || !out) return;
    if (n > BR_BLOCK) n = BR_BLOCK;
    if (key) memset(key, 0, n * sizeof *key);
    lane_mask &= BR_DRUM_ALL_MASK;
    key_lane = sound->drum_key_lane < BR_DRUM_LANES ? sound->drum_key_lane : 0u;

    for (lane = 0; lane < BR_DRUM_LANES; ++lane) {
        br_drum_voice_t *v;
        uint32_t level;
        if (!(lane_mask & (1u << lane))) continue;
        v = &s->voice[lane];
        if (v->pending) {
            uint32_t gate = lane_gate_frames ? lane_gate_frames[lane] : 0u;
            br_drum_start_voice(s, lane, sound, gate);
        } else if (v->active && !v->voice_override &&
                   v->voice_id != br_drum_voice_id(sound->drum_voice_mask, lane)) {
            /* A selection change discards the old tail. It cannot keep a
             * hidden alternate state alive to resume after a later toggle. */
            br_drum_start_tail_fade(v);
            v->active = 0u;
            v->forced_fade = 0u;
            br_drum_sample_player_reset(&v->sample);
        }
        if (!v->active) {
            br_drum_emit_tail_fade(v, lane, key_lane, out, key, n);
            continue;
        }
        if (s->kit == BR_KIT_BRAIDS) {
            uint32_t cutoff;
            uint32_t gate = lane_gate_frames ? lane_gate_frames[lane] : 0u;
            uint32_t offset;
            int16_t *tmp = s->braids_render_scratch[lane];
            br_drum_latch_gate(v, gate);
            if (v->forced_fade && !v->length_choked) {
                braids_perc_choke(s->perc[lane]);
                v->length_choked = 1u;
            }
            br_drum_braids_controls(s->perc[lane], v->voice_id, sound);
            for (offset = 0u; offset < n; offset += BRAIDS_PERC_BLOCK_MAX) {
                uint32_t count = n - offset < BRAIDS_PERC_BLOCK_MAX ?
                    n - offset : BRAIDS_PERC_BLOCK_MAX;
                braids_perc_render(s->perc[lane], tmp + offset, count);
            }
            level = lane_level[lane] > 127u ? 127u : lane_level[lane];
            cutoff = v->gate_limit_frames;
            for (i = 0; i < n; ++i) {
                if (v->active && cutoff && v->age >= cutoff && !v->forced_fade) {
                    if (!v->length_choked) braids_perc_choke(s->perc[lane]);
                    v->length_choked = 1u;
                    v->forced_fade = DRUM_FORCED_FADE;
                }
                if (v->active && level) {
                    int32_t shaped = ((int32_t)tmp[i] * (int32_t)v->env) >> 15;
                    int32_t contribution = (((shaped * (int32_t)br_drum_lifetime_gain(v)) >> 15) * (int32_t)level) >> 7;
                    out[i] += contribution;
                    if (key && lane == key_lane) key[i] += contribution;
                    v->last_output = contribution;
                } else v->last_output = 0;
                v->env = (v->env * v->decay_q15) >> 15;
                if (v->env < DRUM_DECAY_MIN) v->active = 0u;
                ++v->age;
                if (v->forced_fade) {
                    --v->forced_fade;
                    if (!v->forced_fade) v->active = 0u;
                }
            }
            v->active = v->active && braids_perc_active(s->perc[lane]);
        } else {
            int32_t sample_tune = (int32_t)sound->tune + v->pitch_delta;
            /* Keep IDs 0..7 sample playback bit-identical. New lane-specific
             * choices get a small, fixed voicing offset where the source set
             * has no dedicated third sample (notably pedal hats/rims). */
            if (v->voice_id >= BR_DRUM_KCK3)
                sample_tune += br_drum_voice_note_offset(v->voice_id);
            level = lane_level[lane];
            if (level_shift == 7u && level > 127u) level = 127u;
            br_drum_latch_gate(v, lane_gate_frames ? lane_gate_frames[lane] : 0u);
            br_drum_sample_player_update_controls(&v->sample,
                sample_tune, sound->decay);
            for (i = 0; i < n && v->active; ++i) {
                int32_t contribution = br_drum_render_sample(
                    v, sound, level, level_shift,
                    lane_gate_frames ? lane_gate_frames[lane] : 0u);
                out[i] += contribution;
                if (key && lane == key_lane) key[i] += contribution;
            }
        }
        br_drum_emit_tail_fade(v, lane, key_lane, out, key, n);
    }
}

void br_drum_render_keyed_mask(void *memory, const br_sound_t *sound,
                               const uint32_t lane_level[BR_DRUM_LANES],
                               const uint32_t lane_gate_frames[BR_DRUM_LANES],
                               uint32_t lane_mask, int32_t *out, int32_t *key,
                               uint32_t n)
{
    br_drum_render_keyed_mask_scaled(memory, sound, lane_level,
        lane_gate_frames, lane_mask, out, key, n, 7u);
}

/* Audio-engine levels include the preset's Q6 calibration and are Q13. */
static void br_drum_render_keyed_mask_q13(
                               void *memory, const br_sound_t *sound,
                               const uint32_t lane_level[BR_DRUM_LANES],
                               const uint32_t lane_gate_frames[BR_DRUM_LANES],
                               uint32_t lane_mask, int32_t *out, int32_t *key,
                               uint32_t n)
{
    br_drum_render_keyed_mask_scaled(memory, sound, lane_level,
        lane_gate_frames, lane_mask, out, key, n, 13u);
}

void br_drum_render(void *memory, const br_sound_t *sound,
                    const uint32_t lane_level[BR_DRUM_LANES],
                    int32_t *out, uint32_t n)
{
    br_drum_render_gated(memory, sound, lane_level, 0, out, n);
}

void br_drum_all_off(void *memory)
{
    br_drum_all_off_mask(memory, BR_DRUM_ALL_MASK);
}

void br_drum_all_off_mask(void *memory, uint32_t lane_mask)
{
    br_drum_state_t *s = (br_drum_state_t *)memory;
    uint32_t lane;
    if (!s) return;
    lane_mask &= BR_DRUM_ALL_MASK;
    for (lane = 0; lane < BR_DRUM_LANES; ++lane) {
        br_drum_voice_t *v;
        if (!(lane_mask & (1u << lane))) continue;
        v = &s->voice[lane];
        v->pending = 0u;
        if (s->kit == BR_KIT_BRAIDS && v->active) {
            braids_perc_choke(s->perc[lane]);
            v->length_choked = 1u;
        } else {
            v->active = 0u;
            br_drum_sample_player_reset(&v->sample);
        }
    }
}

int br_drum_quiet(const void *memory)
{
    return br_drum_quiet_mask(memory, BR_DRUM_ALL_MASK);
}

int br_drum_quiet_mask(const void *memory, uint32_t lane_mask)
{
    const br_drum_state_t *s = (const br_drum_state_t *)memory;
    uint32_t lane;
    if (!s) return 1;
    lane_mask &= BR_DRUM_ALL_MASK;
    for (lane = 0; lane < BR_DRUM_LANES; ++lane) {
        if ((lane_mask & (1u << lane)) &&
            (s->voice[lane].pending || s->voice[lane].active ||
             s->voice[lane].tail_fade)) return 0;
    }
    return 1;
}

uint32_t br_drum_lane_scheduler_state_ex(const void *memory,
                                        const br_sound_t *sound,
                                        uint32_t lane,
                                        uint32_t *engine_work,
                                        uint32_t *render_voice,
                                        uint32_t *render_cold)
{
    const br_drum_state_t *s = (const br_drum_state_t *)memory;
    const br_drum_voice_t *v;
    uint32_t selected_voice, voice, work = 0u;
    if (engine_work) *engine_work = 0u;
    if (render_voice) *render_voice = 0u;
    if (render_cold) *render_cold = 0u;
    if (!s || !sound || lane >= BR_DRUM_LANES ||
        s->kit != BR_KIT_BRAIDS) return 0u;
    v = &s->voice[lane];
    selected_voice = v->pending &&
        v->pending_voice_id < BR_DRUM_VOICE_COUNT ? v->pending_voice_id :
        br_drum_voice_id(sound->drum_voice_mask, lane);
    voice = selected_voice;
    if (v->pending) {
        work = 1u;
    } else if (v->active && v->voice_override) {
        work = 1u;
        voice = v->voice_id;
    } else if (v->active && v->voice_id == selected_voice) {
        work = 1u;
        voice = v->voice_id;
    }
    if (engine_work) *engine_work = work;
    if (render_voice) *render_voice = voice;
    if (render_cold && work)
        *render_cold = v->pending || !braids_perc_initialized(s->perc[lane]);
    return 1u;
}

uint32_t br_drum_lane_scheduler_state(const void *memory,
                                     const br_sound_t *sound,
                                     uint32_t lane,
                                     uint32_t *engine_work,
                                     uint32_t *render_voice)
{
    return br_drum_lane_scheduler_state_ex(memory, sound, lane, engine_work,
                                           render_voice, 0);
}
