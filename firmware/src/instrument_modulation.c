/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point per-part LFO, modulation envelope, routing, and note quantizer. */
#include <string.h>
#include "../hal/fm1_audio_rate.h"
#define BR_MOD_Q15 32767
#ifndef BR_MOD_LFO_PHASE_DELTA_CACHE
#define BR_MOD_LFO_PHASE_DELTA_CACHE 1
#endif
#if BR_MOD_LFO_PHASE_DELTA_CACHE != 0 && BR_MOD_LFO_PHASE_DELTA_CACHE != 1
#error "BR_MOD_LFO_PHASE_DELTA_CACHE must be 0 or 1"
#endif
#ifndef BR_MOD_NATIVE32_TIME32
#define BR_MOD_NATIVE32_TIME32 1
#endif
#if BR_MOD_NATIVE32_TIME32 != 0 && BR_MOD_NATIVE32_TIME32 != 1
#error "BR_MOD_NATIVE32_TIME32 must be 0 or 1"
#endif
#ifndef BR_MOD_ENV_SEGMENT_ADVANCE
#define BR_MOD_ENV_SEGMENT_ADVANCE 1
#endif
#if BR_MOD_ENV_SEGMENT_ADVANCE != 0 && BR_MOD_ENV_SEGMENT_ADVANCE != 1
#error "BR_MOD_ENV_SEGMENT_ADVANCE must be 0 or 1"
#endif

typedef struct {
    uint32_t stage, env, release_start;
    uint32_t phase[BR_LFOS];
    int32_t held[BR_LFOS];
    uint32_t rng, random;
    int32_t previous_osc, previous_mix;
    uint32_t velocity, note;
    int32_t lfo_rate_mod[BR_LFOS];       /* patched into the LFO RATE inputs, taken up a block later */
} br_mod_state_t;

enum { BR_MOD_ENV_OFF, BR_MOD_ENV_ATTACK, BR_MOD_ENV_DECAY,
       BR_MOD_ENV_SUSTAIN, BR_MOD_ENV_RELEASE };

static uint32_t br_mod_clamp_u7(uint32_t x) { return x > 127u ? 127u : x; }
static int32_t br_mod_clamp_i7(int32_t x)
{ return x < -127 ? -127 : x > 127 ? 127 : x; }
static int32_t br_mod_clamp_q15(int32_t x)
{ return x < -32767 ? -32767 : x > 32767 ? 32767 : x; }

/* 0..127 is a bipolar LFO 1 attenuverter with an exact zero at 64. */
static int32_t br_mod_lfo1_depth(uint32_t value, int32_t lfo_q15)
{
    int32_t depth;
    if (value > 127u) value = 127u;
    if (value < 64u) depth = -(int32_t)((64u - value) * 127u / 64u);
    else depth = (int32_t)((value - 64u) * 127u / 63u);
    return lfo_q15 * depth / 32767;
}

BR_AUDIO_API uint32_t br_lfo_rate_millihz(uint32_t rate)
{ return 30u + br_mod_clamp_u7(rate) * 157u; }

#if BR_MOD_LFO_PHASE_DELTA_CACHE
/* Cache the exact baseline phase delta, not a rounded per-sample substitute.
 * There is one entry per part/LFO. Unusual spans bypass storage so the key
 * remains lossless while normal audio blocks fit in this compact 8-byte entry. */
typedef struct {
    uint32_t delta;
    uint16_t frames;
    uint8_t rate;
    uint8_t valid;
} br_mod_phase_delta_cache_t;
static br_mod_phase_delta_cache_t br_mod_phase_delta_cache[BR_PARTS][BR_LFOS];

static void br_mod_phase_delta_cache_reset(uint32_t part)
{
    if (part < BR_PARTS)
        memset(br_mod_phase_delta_cache[part], 0,
               sizeof br_mod_phase_delta_cache[part]);
}

static uint32_t br_mod_phase_delta(uint32_t part, uint32_t lfo,
                                   uint32_t rate, uint32_t frames)
{
    br_mod_phase_delta_cache_t *entry = 0;
    uint32_t hz_milli, increment, delta;
    if (part < BR_PARTS && lfo < BR_LFOS && frames <= 65535u) {
        entry = &br_mod_phase_delta_cache[part][lfo];
        if (entry->valid && entry->rate == rate && entry->frames == frames)
            return entry->delta;
    }
    hz_milli = br_lfo_rate_millihz(rate);
#if FM1_NATIVE_OUTPUT_32K
    increment = (uint32_t)(((uint64_t)hz_milli << 32) /
                           (1000u * FM1_AUDIO_RATE_HZ));
#else
    increment = hz_milli * 9739u / 100u;
#endif
    delta = increment * frames;
    if (entry) {
        entry->delta = delta;
        entry->frames = (uint16_t)frames;
        entry->rate = (uint8_t)rate;
        entry->valid = 1u;
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
    s->random = br_mod_rng(&s->rng) >> 16;
    for (i = 0; i < BR_LFOS; ++i) s->held[i] = (int32_t)(br_mod_rng(&s->rng) >> 16) - 32768;
}

static uint32_t br_mod_time(uint32_t value, uint32_t scale)
{
    value = br_mod_clamp_u7(value);
#if BR_MOD_NATIVE32_TIME32 && FM1_AUDIO_RATE_HZ == 32000u
    /* For value<=127 and scale<=8 the largest numerator is
     * (32+127*127*8)*32000+22050 = 4,130,070,050, below UINT32_MAX.
     * Both real call-site scales (3 and 8) take the exact narrow path. */
    if (scale <= 8u)
        return ((32u + value * value * scale) * 32000u + 22050u) / 44100u;
#elif FM1_AUDIO_RATE_HZ == 48000u
    /* Cancel the exact 48000/44100 ratio before multiplying. The rounded
     * numerator stays below 21 million for both real scales (3 and 8). */
    if (scale <= 8u)
        return ((32u + value * value * scale) * 160u + 73u) / 147u;
#endif
    return (uint32_t)((((uint64_t)32u + (uint64_t)value * value * scale) *
                       FM1_AUDIO_RATE_HZ + 22050u) / 44100u);
}

static void br_mod_note_on(br_mod_state_t *s, const br_mod_config_t *c,
                           uint32_t note, uint32_t velocity)
{
    s->note = note > 127u ? 127u : note;
    s->velocity = velocity > 127u ? 127u : velocity;
    br_gf_kick = 1u;                       /* retriggers the global filter envelope */
    if (!c->retrigger && s->stage != BR_MOD_ENV_OFF) return;
    s->stage = BR_MOD_ENV_ATTACK;
    s->env = 0;
    s->release_start = 0;
}

static void br_mod_note_off(br_mod_state_t *s)
{
    if (s->stage == BR_MOD_ENV_OFF || s->stage == BR_MOD_ENV_RELEASE) return;
    s->stage = BR_MOD_ENV_RELEASE;
    s->release_start = s->env;
}

#if BR_MOD_LFO_PHASE_DELTA_CACHE
static void br_mod_advance(br_mod_state_t *s, const br_mod_config_t *c, uint32_t n,
                           uint32_t part)
#else
static void br_mod_advance(br_mod_state_t *s, const br_mod_config_t *c, uint32_t n)
#endif
{
    uint32_t i, j;
    /* Sustain is a stable envelope state with no per-sample update. Skip the
     * empty n-iteration loop while leaving the independent LFO/RNG advance
     * below untouched. This also avoids duration setup when n is zero. */
    if (n && s->stage != BR_MOD_ENV_OFF && s->stage != BR_MOD_ENV_SUSTAIN) {
        uint32_t sustain = br_mod_clamp_u7(c->sustain) * 32767u / 127u;
        uint32_t attack = br_mod_time(c->attack, 3u);
        uint32_t decay = br_mod_time(c->decay, 8u);
        uint32_t release = br_mod_time(c->release, 8u);
        uint32_t attack_step = (32767u + attack - 1u) / attack;
        uint32_t decay_step = (32767u - sustain + decay - 1u) / decay;
        uint32_t release_step = (s->release_start + release - 1u) / release;
#if BR_MOD_ENV_SEGMENT_ADVANCE
        /* A non-looping envelope has at most three stage boundaries here.
         * Retain the original discrete transition sample, including a decay
         * subtraction that crosses sustain before the next sample settles.
         * Bounded products fit uint32_t; unusual states/lengths fall back. */
        if (!c->loop && n <= 65535u && s->env <= 32767u &&
            s->release_start <= 32767u &&
            s->stage >= BR_MOD_ENV_ATTACK && s->stage <= BR_MOD_ENV_RELEASE) {
            uint32_t left = n;
            while (left && s->stage != BR_MOD_ENV_OFF &&
                   s->stage != BR_MOD_ENV_SUSTAIN) {
                if (s->stage == BR_MOD_ENV_ATTACK) {
                    uint32_t distance = 32767u - s->env;
                    uint32_t delta = attack_step * left;
                    if (delta < distance) { s->env += delta; break; }
                    uint32_t take = distance ?
                        (distance + attack_step - 1u) / attack_step : 1u;
                    left -= take;
                    s->env = 32767u;
                    s->stage = BR_MOD_ENV_DECAY;
                } else if (s->stage == BR_MOD_ENV_DECAY) {
                    if (s->env <= sustain) {
                        s->env = sustain;
                        s->stage = BR_MOD_ENV_SUSTAIN;
                    } else {
                        uint32_t distance = s->env - sustain;
                        uint32_t delta = decay_step * left;
                        if (delta < distance) { s->env -= delta; break; }
                        uint32_t take = (distance + decay_step - 1u) / decay_step;
                        uint32_t amount = take * decay_step;
                        s->env = amount >= s->env ? sustain : s->env - amount;
                        left -= take;
                    }
                } else {
                    uint32_t delta = release_step * left;
                    if (s->env && delta < s->env) { s->env -= delta; break; }
                    uint32_t take = s->env ?
                        (s->env + release_step - 1u) / release_step : 1u;
                    left -= take;
                    s->env = 0u;
                    s->stage = BR_MOD_ENV_OFF;
                }
            }
        } else
#endif
        for (i = 0; i < n && s->stage != BR_MOD_ENV_OFF; ++i) {
            if (s->stage == BR_MOD_ENV_ATTACK) {
                s->env += attack_step;
                if (s->env >= 32767u) { s->env = 32767u; s->stage = BR_MOD_ENV_DECAY; }
            } else if (s->stage == BR_MOD_ENV_DECAY) {
                if (s->env <= sustain) {
                    s->env = sustain;
                    if (c->loop) { s->env = 0; s->stage = BR_MOD_ENV_ATTACK; }
                    else s->stage = BR_MOD_ENV_SUSTAIN;
                } else s->env = s->env > decay_step ? s->env - decay_step : sustain;
            } else if (s->stage == BR_MOD_ENV_RELEASE) {
                s->env = s->env > release_step ? s->env - release_step : 0u;
                if (!s->env) s->stage = BR_MOD_ENV_OFF;
            }
        }
    }
    for (j = 0; j < BR_LFOS; ++j) {
        int32_t patched_rate = (int32_t)br_mod_clamp_u7(c->lfo[j].rate) + s->lfo_rate_mod[j];
        uint32_t rate = (uint32_t)(patched_rate < 0 ? 0 : patched_rate > 127 ? 127 : patched_rate);
        /* Linear 0.03..19.969 Hz map; derive Q0.32 phase from the active rate. */
#if BR_MOD_LFO_PHASE_DELTA_CACHE
        uint32_t phase_delta = br_mod_phase_delta(part, j, rate, n);
        uint32_t old = s->phase[j];
        s->phase[j] += phase_delta;
#else
        uint32_t hz_milli = br_lfo_rate_millihz(rate);
#if FM1_NATIVE_OUTPUT_32K
        uint32_t increment = (uint32_t)(((uint64_t)hz_milli << 32) /
                                        (1000u * FM1_AUDIO_RATE_HZ));
#else
        uint32_t increment = hz_milli * 9739u / 100u;
#endif
        uint32_t old = s->phase[j];
        s->phase[j] += increment * n;
#endif
        if (s->phase[j] < old && c->lfo[j].wave == BR_WAVE_RANDOM)
            s->held[j] = (int32_t)(br_mod_rng(&s->rng) >> 16) - 32768;
    }
    s->random = br_mod_rng(&s->rng) >> 16;
}

static int32_t br_mod_lfo(const br_mod_state_t *s, uint32_t index, uint32_t wave)
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

static int32_t br_mod_source(const br_mod_state_t *s, uint32_t source,
                             int32_t amp_q15, int32_t lfo[BR_LFOS])
{
    switch (source) {
    case BR_SOURCE_OSC: return s->previous_osc;
    case BR_SOURCE_MIX: return s->previous_mix;
    case BR_SOURCE_AMP: return amp_q15;
    case BR_SOURCE_ENV: return (int32_t)s->env;
    case BR_SOURCE_LFO1: return lfo[0];
    case BR_SOURCE_LFO2: return lfo[1];
    /* Source ID 7 remains in saved enum space, but the third LFO was retired.
     * Keep this safe even if a legacy route reaches this helper directly. */
    case BR_SOURCE_LFO3: return 0;
    case BR_SOURCE_VELOCITY: return (int32_t)(s->velocity * 32767u / 127u);
    case BR_SOURCE_KEY: return (int32_t)(s->note * 32767u / 127u);
    case BR_SOURCE_RANDOM: return (int32_t)s->random - 32768;
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

static void br_mod_add(int32_t amount[BR_TARGET_COUNT], uint32_t target, int32_t value)
{
    if (br_mod_target_active(target)) amount[target] += value;
}

static void br_mod_u7(uint32_t *value, int32_t delta)
{
    int32_t x = (int32_t)br_mod_clamp_u7(*value) + delta;
    *value = (uint32_t)(x < 0 ? 0 : x > 127 ? 127 : x);
}

static void br_mod_effective(br_mod_state_t *state, const br_mod_config_t *config,
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
            br_mod_add(amount, target, lfo[i] * (int32_t)br_mod_clamp_u7(l->depth) / 32767);
    }
    for (i = 0; i < BR_ROUTES; ++i) {
        const br_route_config_t *r = &config->route[i];
        if (!r->enabled || !br_mod_source_active(r->source) ||
            !br_mod_target_supported(base->preset, r->target)) continue;
        if (r->source == BR_SOURCE_ENV) env_patched = 1u;
        br_mod_add(amount, r->target, br_mod_source(state, r->source, amp_q15, lfo) *
                   br_mod_clamp_i7(r->amount) / 32767);
    }
    /* An explicit envelope patch replaces the direct connection. Preserve the
     * stored direct target so disabling patches restores existing projects. */
    if (!env_patched && br_mod_target_supported(base->preset, config->target))
        br_mod_add(amount, config->target,
                   (int32_t)state->env * br_mod_clamp_i7(config->amount) / 32767);
    /* These dedicated HOME controls do not change existing EDIT routes. */
    br_mod_add(amount, BR_TARGET_TIMBRE,
               br_mod_lfo1_depth(base->timbre_mod, lfo[0]));
    br_mod_add(amount, BR_TARGET_PITCH,
               br_mod_lfo1_depth(base->fm_mod, lfo[0]));
    if (br_is_plaits(base->preset))
        br_mod_add(amount, BR_TARGET_COLOR,
                   br_mod_lfo1_depth(base->morph_mod, lfo[0]));
    for (target = 1; target < BR_TARGET_COUNT; ++target) amount[target] = br_mod_clamp_i7(amount[target]);
    br_mod_u7(&sound->timbre, amount[BR_TARGET_TIMBRE]);
    br_mod_u7(&sound->color, amount[BR_TARGET_COLOR]);
    br_mod_u7(&sound->filter, amount[BR_TARGET_FILTER]);
    br_mod_u7(&sound->filter_resonance, amount[BR_TARGET_RESONANCE]);
    br_mod_u7(&sound->attack, amount[BR_TARGET_ATTACK]);
    br_mod_u7(&sound->sustain, amount[BR_TARGET_SUSTAIN]);
    br_mod_u7(&sound->release, amount[BR_TARGET_RELEASE]);
    br_mod_u7(&sound->level, amount[BR_TARGET_LEVEL]);
    br_mod_u7(&sound->decay, amount[BR_TARGET_DECAY]);
    /* BR_TARGET_GATE stays in the saved enum for old sessions, but has no
     * runtime effect now that note length belongs to each sequencer trigger. */
    br_mod_u7(&sound->delay, amount[BR_TARGET_DELAY]);
    br_mod_u7(&sound->reverb, amount[BR_TARGET_REVERB]);
    br_mod_u7(&fx->mod_mix, amount[BR_TARGET_MOD]);
    br_mod_u7(&sound->plaits_harmonics, amount[BR_TARGET_HARMONICS]);
    br_mod_u7(&sound->plaits_aux, amount[BR_TARGET_OUTPUT]);
    for (i = 0; i < BR_LFOS; ++i) state->lfo_rate_mod[i] = amount[BR_TARGET_LFO1_RATE + i];
    *pitch_mod_q7 = amount[BR_TARGET_PITCH] * 24 * 128 / 127;
    /* Every kit renderer consumes tune, including the later Plaits kit.
     * The melodic pitch accumulator is not used by kit source jobs. */
    if (br_is_kit(base->preset)) {
        sound->tune += amount[BR_TARGET_PITCH] * 24 / 127;
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
