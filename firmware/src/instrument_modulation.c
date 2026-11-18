/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point per-part LFO, modulation envelope, routing, and note quantizer. */
#include <string.h>
#include "../hal/fm1_audio_rate.h"
#define BR_MOD_Q15 32767
/* The exponential rate law needs no division per block (see
 * br_mod_lfo_delta), so the per-part phase-delta cache is off by default. */
#ifndef BR_MOD_LFO_PHASE_DELTA_CACHE
#define BR_MOD_LFO_PHASE_DELTA_CACHE 0
#endif
#if BR_MOD_LFO_PHASE_DELTA_CACHE != 0 && BR_MOD_LFO_PHASE_DELTA_CACHE != 1
#error "BR_MOD_LFO_PHASE_DELTA_CACHE must be 0 or 1"
#endif

typedef struct {
    uint32_t stage, env, release_start, env_pos;
    uint32_t phase[BR_LFOS];
    int32_t held[BR_LFOS];
    uint32_t rng, random, note_rng, note_random;
    int32_t previous_osc, previous_mix, global_lfo;
    uint32_t velocity, note;
    int32_t lfo_rate_mod[BR_LFOS];       /* patched into the LFO RATE inputs (Q8 knob units), taken up a block later */
} br_mod_state_t;

enum { BR_MOD_ENV_OFF, BR_MOD_ENV_ATTACK, BR_MOD_ENV_DECAY,
       BR_MOD_ENV_SUSTAIN, BR_MOD_ENV_RELEASE };

static uint32_t br_mod_clamp_u7(uint32_t x) { return x > 127u ? 127u : x; }
BR_COLD_SIZE static int32_t br_mod_clamp_i7(int32_t x)
{ return x < -127 ? -127 : x > 127 ? 127 : x; }
static int32_t br_mod_clamp_q15(int32_t x)
{ return x < -32767 ? -32767 : x > 32767 ? 32767 : x; }

/* Modulation is summed in Q8 target units: 256 = one knob step. */
#define BR_MOD_Q8_FULL (127 * 256)

/* MOD DEPTHS: 0..127 maps -100%..+100% around unity at 64. */
BR_COLD_SIZE static int32_t br_mod_attenuate(int32_t amount_q8, uint32_t value)
{
    if (value > 127u) value = 127u;
    if (value <= 64u) return (int32_t)((int64_t)amount_q8 * value / 64);
    return amount_q8 + (int32_t)((int64_t)amount_q8 * (value - 64u) / 63);
}

/* A signed 0..127 depth knob in Q8 units. Into PITCH it is quadratic,
 * 24 st * (d/127)^2 with sign, so the first steps give cents of vibrato. */
BR_COLD_SIZE static int32_t br_mod_depth_q8(int32_t depth, uint32_t target)
{
    int32_t q8 = br_mod_clamp_i7(depth) * 256;
    if (target == BR_TARGET_PITCH)
        q8 = (int32_t)((uint32_t)(q8 < 0 ? -q8 : q8) * (uint32_t)(q8 < 0 ? -q8 : q8) /
                       (uint32_t)BR_MOD_Q8_FULL) * (q8 < 0 ? -1 : 1);
    return q8;
}

/* LFO rate law: f = 0.02 Hz * 1000^(v/127), v = 0..127 in Q8 (x1.056 per
 * step, 0.02 Hz = 50 s per cycle ... 20 Hz). Returned in Hz Q20. 2^x comes
 * from a 17-point 2^(i/16) table, linearly interpolated (error < 0.03 %). */
BR_AUDIO_API uint32_t br_lfo_rate_hz_q20(uint32_t rate_q8)
{
    static const uint16_t exp2_16[17] = {
        0, 1451, 2966, 4548, 6200, 7925, 9727, 11608, 13573,
        15625, 17767, 20005, 22341, 24781, 27329, 29989, 32768
    };
    uint32_t x, i, m;
    if (rate_q8 > 127u << 8) rate_q8 = 127u << 8;
    x = rate_q8 * 41141u >> 11;          /* v * log2(1000)/127, octaves in Q16 */
    i = x >> 12 & 15u;
    m = 32768u + exp2_16[i] + ((exp2_16[i + 1u] - exp2_16[i]) * (x & 4095u) >> 12);
    return 20972u * m >> (15u - (x >> 16));  /* 0.02 Hz in Q20 = 20971.5 */
}

/* Q0.32 phase advance for one block: f * frames * 2^32 / FS, as two
 * 32x32->64 multiplies by the constant 2^44/FS (relative error < 1e-6). */
static uint32_t br_mod_lfo_delta(uint32_t rate_q8, uint32_t frames)
{
    uint32_t per_sample_q8 = (uint32_t)((uint64_t)br_lfo_rate_hz_q20(rate_q8) *
        (uint32_t)((1ull << 44) / FM1_AUDIO_RATE_HZ) >> 24);
    return (uint32_t)((uint64_t)per_sample_q8 * frames >> 8);
}

#if BR_MOD_LFO_PHASE_DELTA_CACHE
/* Cache the exact baseline phase delta, not a rounded per-sample substitute.
 * There is one entry per part/LFO. Unusual spans bypass storage so the key
 * remains lossless while normal audio blocks fit in this compact 8-byte entry.
 * A zeroed entry (rate 0, 0 frames, delta 0) is itself correct. */
typedef struct {
    uint32_t delta;
    uint16_t frames;
    uint16_t rate_q8;
} br_mod_phase_delta_cache_t;
static br_mod_phase_delta_cache_t br_mod_phase_delta_cache[BR_PARTS][BR_LFOS];

BR_COLD_SIZE static void br_mod_phase_delta_cache_reset(uint32_t part)
{
    if (part < BR_PARTS)
        memset(br_mod_phase_delta_cache[part], 0,
               sizeof br_mod_phase_delta_cache[part]);
}

static uint32_t br_mod_phase_delta(uint32_t part, uint32_t lfo,
                                   uint32_t rate_q8, uint32_t frames)
{
    br_mod_phase_delta_cache_t *entry = 0;
    uint32_t delta;
    if (part < BR_PARTS && lfo < BR_LFOS && frames <= 65535u) {
        entry = &br_mod_phase_delta_cache[part][lfo];
        if (entry->rate_q8 == rate_q8 && entry->frames == frames)
            return entry->delta;
    }
    delta = br_mod_lfo_delta(rate_q8, frames);
    if (entry) {
        entry->delta = delta;
        entry->frames = (uint16_t)frames;
        entry->rate_q8 = (uint16_t)rate_q8;
    }
    return delta;
}
#endif

static uint32_t br_mod_rng(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x ? x : 0x6D2B79F5u;
    return *state;
}

static __attribute__((minsize, noinline)) void br_mod_init(br_mod_state_t *s, uint32_t seed)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0x42524D31u;
    s->note_rng = s->rng ^ 0x9e3779b9u;
    if (!s->note_rng) s->note_rng = 0x6D2B79F5u;
    s->note_random = 32768u;
    s->random = br_mod_rng(&s->rng) >> 16;
    for (i = 0; i < BR_LFOS; ++i) s->held[i] = (int32_t)(br_mod_rng(&s->rng) >> 16) - 32768;
}

/* Envelope time law shared by the amp envelope, this mod/filter envelope and
 * the display: t(v) = t0 * (tmax/t0)^(v/127), equal ratio per knob step.
 * Attack 0.5 ms..5 s, decay/release 1 ms..12 s. log2 of the time is v*K (Q16);
 * 2^frac comes from a 17-point mantissa table with linear interpolation. */
static const uint16_t br_env_exp2_q15[17] = {
    0u, 1451u, 2966u, 4548u, 6200u, 7925u, 9727u, 11608u, 13573u,
    15625u, 17767u, 20005u, 22341u, 24781u, 27329u, 29989u, 32768u };

BR_AUDIO_API uint32_t br_time_samples(uint32_t value, uint32_t decay_curve)
{
    uint32_t e = br_mod_clamp_u7(value) * (decay_curve ? 6993u : 6857u);
    uint32_t j = e >> 12 & 15u, lo = br_env_exp2_q15[j];
    uint32_t m = 32768u + lo +
        (((br_env_exp2_q15[j + 1u] - lo) * (e & 4095u) + 2048u) >> 12);
    /* t0 in Q8 samples: 0.5 ms or 1 ms at the build's output rate. */
    uint32_t base = decay_curve ? (FM1_AUDIO_RATE_HZ * 32u + 62u) / 125u
                                : (FM1_AUDIO_RATE_HZ * 16u + 62u) / 125u;
    return (((base * m) >> 15 << (e >> 16)) + 128u) >> 8;
}

BR_AUDIO_API uint32_t br_lpg_release_samples(void)
{
    return (FM1_AUDIO_RATE_HZ + 250u) / 500u; /* 2 ms anti-click close */
}

BR_AUDIO_API uint32_t br_lpg_gate_samples(uint32_t value)
{
    if (!value) return 0u;
    if (value > 31u) value = 31u;
    uint32_t n = value - 1u;
    uint32_t ms = 10u + (n * n * 1990u + 450u) / 900u;
    return (FM1_AUDIO_RATE_HZ * ms + 500u) / 1000u;
}

/* Decay and release move a position x (Q31 in a uint32) linearly; the level
 * is x^2, which makes their tails fall like a natural decay instead of a
 * ramp, at one multiply per sample. Attack stays linear in level. */
#define BR_ENV_POS_TOP (65535u << 15)
static uint32_t br_env_level(uint32_t pos)
{
    uint32_t x = pos >> 15;
    return x * (x + 1u) >> 17;
}

static __attribute__((noinline)) uint32_t br_env_pos(uint32_t level)
{
    uint32_t v = level << 17, root = 0u, bit = 1u << 30;
    while (bit) {
        if (v >= root + bit) { v -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root << 15;
}

BR_COLD_SIZE static void br_mod_note_on(br_mod_state_t *s, const br_mod_config_t *c,
                           uint32_t note, uint32_t velocity)
{
    s->note = note > 127u ? 127u : note;
    s->velocity = velocity > 127u ? 127u : velocity;
    /* Shared by all voices or kit lanes on this track: the newest note-on
     * chooses one value, held until the next note-on. Keep its RNG separate
     * so existing block-random and random-LFO sequences are unchanged. */
    s->note_random = br_mod_rng(&s->note_rng) >> 16;
    br_gf_kick = 1u;                       /* retriggers the global filter envelope */
    if (!c->retrigger && s->stage != BR_MOD_ENV_OFF) return;
    s->stage = BR_MOD_ENV_ATTACK;
    s->env = s->env_pos = 0;
    s->release_start = 0;
}

BR_COLD_SIZE static void br_mod_note_off(br_mod_state_t *s)
{
    if (s->stage == BR_MOD_ENV_OFF || s->stage == BR_MOD_ENV_RELEASE) return;
    s->stage = BR_MOD_ENV_RELEASE;
    s->release_start = s->env;
    s->env_pos = br_env_pos(s->env);
}

#if BR_MOD_LFO_PHASE_DELTA_CACHE
static void br_mod_advance(br_mod_state_t *s, const br_mod_config_t *c, uint32_t n,
                           uint32_t part)
#else
static void br_mod_advance(br_mod_state_t *s, const br_mod_config_t *c, uint32_t n)
#endif
{
    uint32_t j;
    /* Sustain is a stable envelope state with no per-sample update. Skip the
     * empty n-iteration loop while leaving the independent LFO/RNG advance
     * below untouched. This also avoids duration setup when n is zero. */
    if (n && s->stage != BR_MOD_ENV_OFF && s->stage != BR_MOD_ENV_SUSTAIN) {
        uint32_t sustain = br_mod_clamp_u7(c->sustain) * 32767u / 127u;
        uint32_t target = br_env_pos(sustain);
        uint32_t attack = br_time_samples(c->attack, 0u);
        uint32_t decay = br_time_samples(c->decay, 1u);
        uint32_t release = br_time_samples(c->release, 1u);
        uint32_t left = n;
        /* Whole segments per block, sample-exact with the per-sample rule:
         * each sample adds/subtracts one fractional step, and the sample that
         * reaches or passes a segment end lands on it exactly. */
        while (left) {
            uint32_t distance, step;
            if (s->stage == BR_MOD_ENV_ATTACK) {
                distance = (32767u << 16) - s->env_pos;
                step = ((32767u << 16) + attack - 1u) / attack;
            } else if (s->stage == BR_MOD_ENV_DECAY) {
                distance = s->env_pos > target ? s->env_pos - target : 0u;
                step = (BR_ENV_POS_TOP - target + decay - 1u) / decay;
            } else {
                distance = s->env_pos;
                step = (br_env_pos(s->release_start) + release - 1u) / release;
            }
            if ((uint64_t)step * left < distance) {
                distance = step * left;
                s->env_pos += s->stage == BR_MOD_ENV_ATTACK ? distance : 0u - distance;
                s->env = s->stage == BR_MOD_ENV_ATTACK ? s->env_pos >> 16 :
                         br_env_level(s->env_pos);
                break;
            }
            distance = distance ? (distance + step - 1u) / step : 1u;
            left -= distance;
            if (s->stage == BR_MOD_ENV_ATTACK) {
                s->env_pos = BR_ENV_POS_TOP; s->env = 32767u;
                s->stage = BR_MOD_ENV_DECAY;
            } else if (s->stage == BR_MOD_ENV_DECAY) {
                s->env_pos = target; s->env = sustain;
                s->stage = BR_MOD_ENV_SUSTAIN;
                if (c->loop) { s->env = s->env_pos = 0u; s->stage = BR_MOD_ENV_ATTACK; }
                else break;
            } else {
                s->env = s->env_pos = 0u;
                s->stage = BR_MOD_ENV_OFF;
                break;
            }
        }
    }
    for (j = 0; j < BR_LFOS; ++j) {
        /* A patched rate is added to the knob before the exponential law,
         * so +/- equal amounts give equal frequency ratios. */
        int32_t patched_rate = (int32_t)br_mod_clamp_u7(c->lfo[j].rate) * 256 + s->lfo_rate_mod[j];
        uint32_t rate_q8 = (uint32_t)(patched_rate < 0 ? 0 : patched_rate > 127 * 256 ? 127 * 256 : patched_rate);
        uint32_t old = s->phase[j];
#if BR_MOD_LFO_PHASE_DELTA_CACHE
        s->phase[j] += br_mod_phase_delta(part, j, rate_q8, n);
#else
        s->phase[j] += br_mod_lfo_delta(rate_q8, n);
#endif
        if (s->phase[j] < old && c->lfo[j].wave == BR_WAVE_RANDOM)
            s->held[j] = (int32_t)(br_mod_rng(&s->rng) >> 16) - 32768;
    }
    s->random = br_mod_rng(&s->rng) >> 16;
}

BR_COLD_SIZE static int32_t br_mod_lfo(const br_mod_state_t *s, uint32_t index, uint32_t wave)
{
    static const int16_t sine32[32] = {
        0, 6393, 12539, 18204, 23169, 27244, 30273, 32137,
        32767, 32137, 30273, 27244, 23169, 18204, 12539, 6393,
        0, -6393, -12539, -18204, -23169, -27244, -30273, -32137,
        -32767, -32137, -30273, -27244, -23169, -18204, -12539, -6393
    };
    uint32_t phase = s->phase[index], ix = phase >> 27;
    if (wave == BR_WAVE_SINE) {
        int32_t a = sine32[ix], b = sine32[(ix + 1u) & 31u];
        uint32_t fraction = (phase >> 12) & 32767u;
        return a + (b - a) * (int32_t)fraction / 32768;
    }
    if (wave == BR_WAVE_TRIANGLE) {
        uint32_t q = phase >> 16;
        if (q < 16384u) return (int32_t)(q * 2u);
        if (q < 49152u) return 32767 - (int32_t)((q - 16384u) * 2u);
        return -32767 + (int32_t)((q - 49152u) * 2u);
    }
    if (wave == BR_WAVE_SAW) return (int32_t)(phase >> 16) - 32768;
    if (wave == BR_WAVE_SQUARE) return phase < 0x80000000u ? 32767 : -32767;
    return s->held[index];
}

BR_COLD_SIZE static int32_t br_mod_source(const br_mod_state_t *s, uint32_t source,
                             int32_t amp_q15, int32_t lfo[BR_LFOS])
{
    switch (source) {
    case BR_SOURCE_OSC: return s->previous_osc;
    case BR_SOURCE_MIX: return s->previous_mix;
    case BR_SOURCE_AMP: return amp_q15;
    case BR_SOURCE_ENV: return (int32_t)s->env;
    case BR_SOURCE_LFO1: return lfo[0];
    case BR_SOURCE_LFO2: return lfo[1];
    case BR_SOURCE_GLOBAL_LFO: return s->global_lfo;
    /* Source ID 7 remains in saved enum space, but the third LFO was retired.
     * Keep this safe even if a legacy route reaches this helper directly. */
    case BR_SOURCE_LFO3: return 0;
    case BR_SOURCE_VELOCITY: return (int32_t)(s->velocity * 32767u / 127u);
    case BR_SOURCE_KEY: return (int32_t)(s->note * 32767u / 127u);
    case BR_SOURCE_RANDOM: return (int32_t)s->random - 32768;
    case BR_SOURCE_NOTE_RANDOM: return (int32_t)s->note_random - 32768;
    case BR_SOURCE_GATE:
        return s->stage == BR_MOD_ENV_ATTACK || s->stage == BR_MOD_ENV_DECAY ||
               s->stage == BR_MOD_ENV_SUSTAIN ? 32767 : 0;
    case BR_SOURCE_LFO1_TRI: return br_mod_lfo(s, 0u, BR_WAVE_TRIANGLE);
    case BR_SOURCE_LFO1_SQU: return br_mod_lfo(s, 0u, BR_WAVE_SQUARE);
    case BR_SOURCE_LFO2_TRI: return br_mod_lfo(s, 1u, BR_WAVE_TRIANGLE);
    case BR_SOURCE_LFO2_SQU: return br_mod_lfo(s, 1u, BR_WAVE_SQUARE);
    default: return 0;
    }
}

BR_COLD_SIZE static void br_mod_add(int32_t amount[BR_TARGET_COUNT], uint32_t target, int32_t value)
{
    if (br_mod_target_active(target)) amount[target] += value;
}

/* Apply a Q8 sum to a 0..127 control, rounded to the nearest step. */
BR_COLD_SIZE static void br_mod_u7_range(uint32_t *value, int32_t delta_q8, int32_t low, int32_t high)
{
    int32_t x = (int32_t)br_mod_clamp_u7(*value) + ((delta_q8 + 128) >> 8);
    *value = (uint32_t)(x < low ? low : x > high ? high : x);
}
BR_COLD_SIZE static void br_mod_u7(uint32_t *value, int32_t delta_q8)
{ br_mod_u7_range(value, delta_q8, 0, 127); }

/* The filter control is bipolar (0..62 low-pass, 63/64 open, 65..127
 * high-pass). Modulation sweeps through the open center and clamps at the
 * endpoints, without altering the stored control. */
static void br_mod_filter(uint32_t *value, int32_t delta_q8)
{
    br_mod_u7(value, delta_q8);
}

BR_COLD_SIZE static void br_mod_effective(br_mod_state_t *state, const br_mod_config_t *config,
                             const br_sound_t *base, const br_fx_config_t *base_fx,
                             int32_t amp_q15, br_sound_t *sound, br_fx_config_t *fx,
                             int32_t *pitch_mod_q7)
{
    int32_t lfo[BR_LFOS], amount[BR_TARGET_COUNT] = {0};
    uint32_t i, target, env_patched = 0u;
    *sound = *base;
    *fx = *base_fx;
    for (i = 0; i < BR_LFOS; ++i) {
        const br_lfo_config_t *l = &config->lfo[i];
        uint32_t wave = l->wave < BR_WAVE_COUNT ? l->wave : BR_WAVE_SINE;
        lfo[i] = br_mod_lfo(state, i, wave);
        target = l->target;
        if (br_mod_target_supported(base->preset, target))
            br_mod_add(amount, target, lfo[i] * br_mod_depth_q8((int32_t)br_mod_clamp_u7(l->depth), target) / 32767);
    }
    for (i = 0; i < BR_EFFECTIVE_ROUTES; ++i) {
        const br_route_config_t *r = &config->route[i];
        if (!r->enabled || br_target_is_arp(r->target) || !br_mod_source_active(r->source) ||
            !br_mod_target_supported(base->preset, r->target)) continue;
        if (r->source == BR_SOURCE_ENV) env_patched = 1u;
        br_mod_add(amount, r->target, br_mod_source(state, r->source, amp_q15, lfo) *
                   br_mod_depth_q8(r->amount, r->target) / 32767);
    }
    /* An explicit envelope patch replaces the direct connection. Preserve the
     * stored direct target so disabling patches restores existing projects. */
    if (!env_patched && !br_target_is_arp(config->target) && br_mod_target_supported(base->preset, config->target))
        br_mod_add(amount, config->target,
                   (int32_t)state->env * (br_mod_clamp_i7(config->amount) * 256) / 32767);
    /* HOME ENV DEPTH: the filter's own depth from the same envelope, always on,
     * whatever the envelope's TARGET. */
    if (base->filter_env_depth && br_mod_target_supported(base->preset, BR_TARGET_FILTER))
        br_mod_add(amount, BR_TARGET_FILTER,
                   (int32_t)state->env * (br_mod_clamp_i7(base->filter_env_depth) * 256) / 32767);
    /* The LFO DEPTHS attenuverters scale the whole sum into their targets. */
    amount[BR_TARGET_TIMBRE] = br_mod_attenuate(amount[BR_TARGET_TIMBRE], base->timbre_mod);
    amount[BR_TARGET_PITCH] = br_mod_attenuate(amount[BR_TARGET_PITCH], base->fm_mod);
    amount[BR_TARGET_HARMONICS] = br_mod_attenuate(amount[BR_TARGET_HARMONICS],
        base->harmonics_mod ? base->harmonics_mod - 1u : 64u);
    if (br_is_plaits(base->preset))
        amount[BR_TARGET_COLOR] = br_mod_attenuate(amount[BR_TARGET_COLOR], base->morph_mod);
    for (target = 1; target < BR_TARGET_COUNT; ++target)
        amount[target] = amount[target] < -BR_MOD_Q8_FULL ? -BR_MOD_Q8_FULL :
                         amount[target] > BR_MOD_Q8_FULL ? BR_MOD_Q8_FULL : amount[target];
    br_mod_u7(&sound->timbre, amount[BR_TARGET_TIMBRE]);
    br_mod_u7(&sound->color, amount[BR_TARGET_COLOR]);
    br_mod_filter(&sound->filter, amount[BR_TARGET_FILTER]);
    br_mod_u7(&sound->filter_resonance, amount[BR_TARGET_RESONANCE]);
    br_mod_u7(&sound->attack, amount[BR_TARGET_ATTACK]);
    br_mod_u7(&sound->sustain, amount[BR_TARGET_SUSTAIN]);
    br_mod_u7(&sound->release, amount[BR_TARGET_RELEASE]);
    br_mod_u7(&sound->level, amount[BR_TARGET_LEVEL]);
    br_mod_u7(&sound->pan, amount[BR_TARGET_PAN]);
    br_mod_u7(&sound->drive, amount[BR_TARGET_DIST_SEND]);
#if BR_MODEL_MOD_EXPERIMENT
    if (br_is_plaits(base->preset)) {
        /* Full-scale modulation spans all model indices around the base;
         * keep the actual sound settings and saved preset unchanged. */
        int32_t q = amount[BR_TARGET_MODEL] * (int32_t)(BR_PLAITS_MODEL_COUNT - 1u);
        int32_t delta = q < 0 ? -((-q + BR_MOD_Q8_FULL / 2) / BR_MOD_Q8_FULL) :
                                (q + BR_MOD_Q8_FULL / 2) / BR_MOD_Q8_FULL;
        int32_t model = (int32_t)(base->preset - BR_FIRST_PLAITS) + delta;
        if (model < 0) model = 0;
        if (model >= (int32_t)BR_PLAITS_MODEL_COUNT) model = (int32_t)BR_PLAITS_MODEL_COUNT - 1;
        sound->preset = BR_FIRST_PLAITS + (uint32_t)model;
    }
#endif
    br_mod_u7(&sound->decay, amount[BR_TARGET_DECAY]);
    /* The new kit-only destination leaves the retired gate ID inert in older
     * sessions. 128 is OPEN and must remain distinct from the 0..127 range. */
    if (br_is_kit(base->preset)) {
        int32_t gate = (int32_t)(base->gate > BR_GATE_OPEN ? BR_GATE_OPEN : base->gate) +
                       ((amount[BR_TARGET_KIT_GATE] + 128) >> 8);
        sound->gate = (uint32_t)(gate < 0 ? 0 : gate > (int32_t)BR_GATE_OPEN ? BR_GATE_OPEN : gate);
    }
    br_mod_u7(&sound->delay, amount[BR_TARGET_DELAY]);
    br_mod_u7(&sound->reverb, amount[BR_TARGET_REVERB]);
    br_mod_u7(&fx->mod_mix, amount[BR_TARGET_MOD]);
    br_mod_u7(&sound->plaits_harmonics, amount[BR_TARGET_HARMONICS]);
    br_mod_u7(&sound->plaits_aux, amount[BR_TARGET_OUTPUT]);
    for (i = 0; i < BR_LFOS; ++i) state->lfo_rate_mod[i] = amount[BR_TARGET_LFO1_RATE + i];
    /* 127 units = 24 st, so Q8 units * 12/127 = Q7 semitones (0.78 cent). */
    *pitch_mod_q7 = (amount[BR_TARGET_PITCH] * 12 +
                     (amount[BR_TARGET_PITCH] < 0 ? -63 : 63)) / 127;
    /* Every kit renderer consumes tune, including the later Plaits kit.
     * The melodic pitch accumulator is not used by kit source jobs. */
    if (br_is_kit(base->preset)) {
        sound->tune += (*pitch_mod_q7 + 64) >> 7;     /* kits tune in whole semitones */
        if (sound->tune < -48) sound->tune = -48;
        if (sound->tune > 48) sound->tune = 48;
    }
}

static void br_mod_capture(br_mod_state_t *s, int32_t osc, int32_t mix)
{
    s->previous_osc = br_mod_clamp_q15(osc);
    s->previous_mix = br_mod_clamp_q15(mix);
}

static int32_t br_mod_quantize(uint32_t note, int32_t transpose, uint32_t scale,
                               uint32_t root, uint32_t enabled)
{
    static const uint16_t masks[BR_SCALE_COUNT] = {
        0x0FFFu, 0x0AB5u, 0x05ADu, 0x06ADu, 0x06B5u,
        0x0295u, 0x04A9u, 0x04E9u
    };
    int32_t target = (int32_t)note + transpose, best = target;
    int32_t d, best_d = 1000;
    uint32_t candidate;
    if (target < 0) target = 0;
    if (target > 127) target = 127;
    if (!enabled) return target;
    if (scale >= BR_SCALE_COUNT) scale = BR_SCALE_CHROMATIC;
    root %= 12u;
    for (candidate = 0; candidate < 128u; ++candidate) {
        uint32_t pc = (candidate + 12u - root) % 12u;
        if (!(masks[scale] & (1u << pc))) continue;
        d = (int32_t)candidate - target;
        if (d < 0) d = -d;
        if (d < best_d || (d == best_d && (int32_t)candidate < best)) {
            best_d = d; best = (int32_t)candidate;
        }
    }
    return best;
}
