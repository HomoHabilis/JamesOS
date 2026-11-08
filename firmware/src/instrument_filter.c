/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point per-part one-pole filter, included by instrument_audio.c. */
#ifndef BR_FILTER_MUL_SMALL32_PATH
#define BR_FILTER_MUL_SMALL32_PATH 1
#endif
#ifndef BR_FILTER_SKIP_STABLE_SLEW
#define BR_FILTER_SKIP_STABLE_SLEW 1
#endif

typedef struct {
    int32_t low;
    int32_t band;
    int32_t alpha;
    int32_t mix; /* positive = LP, negative = HP; zero = exact bypass */
    uint32_t resonance;
    uint32_t damping;
    uint32_t alpha_limit;
} br_filter_t;

#if !FM1_NATIVE_OUTPUT_32K && !FM1_NATIVE_OUTPUT_48K
static const uint16_t br_filter_alpha[63] = {
    93, 103, 115, 127, 141, 156, 173, 192, 212, 235, 261, 289, 320,
    355, 393, 436, 483, 535, 593, 656, 727, 805, 891, 987, 1092, 1209,
    1337, 1479, 1636, 1809, 1999, 2209, 2440, 2694, 2973, 3280, 3616,
    3984, 4386, 4826, 5306, 5828, 6396, 7011, 7676, 8393, 9165, 9992,
    10874, 11813, 12807, 13854, 14951, 16092, 17273, 18484, 19717,
    20959, 22199, 23422, 24613, 25757, 26839
};
#endif

#if FM1_NATIVE_OUTPUT_48K
/* Keep the 44.1 kHz pole time constants at 48 kHz:
 * alpha48 = 1 - (1 - alpha44)^(44.1/48). */
static const uint16_t br_filter_alpha_48k[63] = {
    85, 95, 106, 117, 130, 143, 159, 176, 195, 216, 240, 266, 294,
    326, 361, 401, 444, 492, 545, 603, 669, 740, 820, 908, 1005,
    1112, 1230, 1361, 1506, 1666, 1841, 2035, 2249, 2484, 2742,
    3026, 3338, 3679, 4053, 4462, 4909, 5396, 5927, 6502, 7126,
    7800, 8527, 9309, 10145, 11038, 11987, 12990, 14047, 15151,
    16301, 17487, 18703, 19938, 21181, 22419, 23637, 24821, 25955
};
#endif

#if FM1_NATIVE_OUTPUT_32K
/* Preserve the original filter's one-pole time constants at 32 kHz:
 * alpha32 = 1 - (1 - alpha44)^(44.1/32). */
static const uint16_t br_filter_alpha_32k[63] = {
    128, 142, 158, 175, 194, 215, 238, 264, 292, 323, 359, 398, 440,
    488, 540, 599, 664, 735, 814, 901, 998, 1104, 1222, 1352, 1495,
    1654, 1828, 2021, 2233, 2467, 2723, 3005, 3315, 3654, 4026, 4433,
    4877, 5361, 5887, 6460, 7080, 7751, 8475, 9252, 10085, 10973,
    11919, 12919, 13970, 15072, 16219, 17403, 18617, 19851, 21094,
    22333, 23554, 24740, 25878, 26952, 27948, 28855, 29662
};
#endif

/* Exact signed division by 2^shift with C truncation toward zero. This helper
 * is also used by the engine's native block-size-dependent output average. */
static int32_t br_filter_div_pow2(int32_t value, uint32_t shift)
{
    int32_t bias = value < 0 ? (int32_t)((1u << shift) - 1u) : 0;
    return (value + bias) >> shift;
}

static int32_t br_filter_mul_q15(int32_t x, uint32_t q)
{
#if BR_FILTER_MUL_SMALL32_PATH
    /* Source/part filter values usually occupy this interval. Both signs,
     * every Q15 coefficient and the truncation bias fit int32_t here. */
    if ((uint32_t)x + 65536u <= 131071u && q <= 32767u) {
        int32_t small = x * (int32_t)q;
        if (small < 0) small += 32767;
        return small >> 15;
    }
#endif
    int64_t product = (int64_t)x * (int32_t)q;
    if (product < 0) product += 32767;
    return (int32_t)(product >> 15);
}

static int32_t br_filter_slew(int32_t current, int32_t target)
{
    int32_t d = target - current;
#if FM1_NATIVE_OUTPUT_32K
    int32_t step = d / 6; /* 6 frames at 32 kHz ~= the original 8 at 44.1. */
#elif FM1_NATIVE_OUTPUT_48K
    int32_t step = d / 9; /* 9 frames at 48 kHz ~= the original 8 at 44.1. */
#else
    int32_t step = br_filter_div_pow2(d, 3u);
#endif
    if (!step && d) step = d > 0 ? 1 : -1;
    return current + step;
}

static void br_filter_init(br_filter_t *f)
{
    f->low = 0;
    f->band = 0;
    f->alpha = 32767;
    f->mix = 0;
    f->resonance = 0xFFFFFFFFu;
    f->damping = 65534u;
    f->alpha_limit = 32767u;
}

static int32_t br_filter_process(br_filter_t *f, int32_t input, uint32_t control)
{
    int32_t target_alpha, target_mix, filtered, wet;
    if (control >= 63u && control <= 64u && !f->mix && f->alpha == 32767) {
        int32_t delta, step;
        uint32_t magnitude;
        if (input > 262144) input = 262144;
        if (input < -262144) input = -262144;
        delta = input - f->low;
        magnitude = delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
        /* Exact truncation of delta*32767/32768, retaining the hidden pole
         * history for a later filter edit, including the one-unit floor. */
        step = (int32_t)(magnitude - ((magnitude + 32767u) >> 15));
        if (!step && magnitude) step = 1;
        f->low += delta < 0 ? -step : step;
        return input;
    }
    if (control < 63u) {
#if FM1_NATIVE_OUTPUT_32K
        target_alpha = br_filter_alpha_32k[control];
#elif FM1_NATIVE_OUTPUT_48K
        target_alpha = br_filter_alpha_48k[control];
#else
        target_alpha = br_filter_alpha[control];
#endif
        target_mix = 32767;
    } else if (control <= 64u) {
        target_alpha = f->mix == 0 ? 32767 : f->alpha;
        target_mix = 0;
    } else {
        uint32_t position = control - 65u;
#if FM1_NATIVE_OUTPUT_32K
        target_alpha = br_filter_alpha_32k[position];
#elif FM1_NATIVE_OUTPUT_48K
        target_alpha = br_filter_alpha_48k[position];
#else
        target_alpha = br_filter_alpha[position];
#endif
        target_mix = -32767;
    }
    /* Fade through dry before changing LP/HP direction or its pole. */
    if ((f->mix > 0 && target_mix < 0) || (f->mix < 0 && target_mix > 0))
        target_alpha = f->alpha;
#if BR_FILTER_SKIP_STABLE_SLEW
    /* br_filter_slew divides by the profile's frame count even when its endpoints already match.
     * Preserve every state transition, but skip those no-op calls once the
     * selected pole and LP/HP mix have settled. */
    if (f->alpha != target_alpha)
        f->alpha = br_filter_slew(f->alpha, target_alpha);
    if (f->mix != target_mix)
        f->mix = br_filter_slew(f->mix, target_mix);
#else
    f->alpha = br_filter_slew(f->alpha, target_alpha);
    f->mix = br_filter_slew(f->mix, target_mix);
#endif
    if (input > 262144) input = 262144;
    if (input < -262144) input = -262144;
    int32_t delta = br_filter_mul_q15(input - f->low, (uint32_t)f->alpha);
    if (!delta && input != f->low) delta = input > f->low ? 1 : -1;
    f->low += delta;
    if (f->mix > 0) {
        filtered = f->low;
        wet = f->mix;
    } else if (f->mix < 0) {
        filtered = input - f->low;
        wet = -f->mix;
    } else return input;
    return input + br_filter_mul_q15(filtered - input, (uint32_t)wet);
}

static int br_filter_quiet(const br_filter_t *f)
{
    return f->low >= -2 && f->low <= 2 && f->band >= -2 && f->band <= 2;
}

static int32_t br_filter_state_clip(int64_t value)
{
    const int32_t limit = 1048576;
    return value < -limit ? -limit : value > limit ? limit : (int32_t)value;
}

/* Exact precomputed Jury bounds: min(32767, isqrt(k*k + 4*32768²) - k),
 * with the original two-unit rounding margin when below saturation.
 * k = 65534 - resonance*477. Entries >= 35u saturate to 32767. */
static const uint16_t br_filter_resonance_limit[35] = {
    27144, 27284, 27426, 27569, 27713, 27858, 28005, 28153,
    28302, 28453, 28605, 28758, 28913, 29069, 29227, 29386,
    29546, 29708, 29871, 30036, 30203, 30371, 30540, 30711,
    30884, 31058, 31234, 31411, 31590, 31771, 31953, 32137,
    32323, 32511, 32700,
};

/* Shift the cutoff by roughly one control step per two semitones. The
 * neutral 63/64 settings stay true bypasses regardless of key or resonance. */
static uint32_t br_filter_keytrack(uint32_t control, uint32_t note)
{
    int32_t offset, position;
    if (control >= 63u && control <= 64u) return control;
    if (note > 127u) note = 127u;
    offset = ((int32_t)note - 60) / 2;
    position = control < 63u ? (int32_t)control : (int32_t)control - 65;
    position += offset;
    if (position < 0) position = 0;
    if (position > 62) position = 62;
    return control < 63u ? (uint32_t)position : 65u + (uint32_t)position;
}

/* Explicit state-variable path used only when resonance is nonzero. Keep its
 * Euler integrator inside the Jury stability bound for every damping value.
 * At low resonance that bound slightly limits the highest cutoff settings;
 * zero resonance keeps the historical one-pole arithmetic unchanged. */
static int32_t br_filter_process_resonant(br_filter_t *f, int32_t input,
                                           uint32_t control,
                                           uint32_t resonance)
{
    int32_t target_alpha, target_mix, filtered, wet, high;
    if (resonance > 127u) resonance = 127u;
    if (f->resonance != resonance) {
        f->resonance = resonance;
        f->damping = 65534u - resonance * 477u;
        f->alpha_limit = resonance < 35u ? br_filter_resonance_limit[resonance] : 32767u;
    }
    if (control < 63u) {
#if FM1_NATIVE_OUTPUT_32K
        target_alpha = br_filter_alpha_32k[control];
#elif FM1_NATIVE_OUTPUT_48K
        target_alpha = br_filter_alpha_48k[control];
#else
        target_alpha = br_filter_alpha[control];
#endif
        target_mix = 32767;
    } else if (control <= 64u) {
        f->band = 0;
        return br_filter_process(f, input, control);
    } else {
        uint32_t position = control - 65u;
#if FM1_NATIVE_OUTPUT_32K
        target_alpha = br_filter_alpha_32k[position];
#elif FM1_NATIVE_OUTPUT_48K
        target_alpha = br_filter_alpha_48k[position];
#else
        target_alpha = br_filter_alpha[position];
#endif
        target_mix = -32767;
    }
    if ((uint32_t)target_alpha > f->alpha_limit)
        target_alpha = (int32_t)f->alpha_limit;
    if ((uint32_t)f->alpha > f->alpha_limit)
        f->alpha = (int32_t)f->alpha_limit;
    if ((f->mix > 0 && target_mix < 0) || (f->mix < 0 && target_mix > 0))
        target_alpha = f->alpha;
#if BR_FILTER_SKIP_STABLE_SLEW
    if (f->alpha != target_alpha)
        f->alpha = br_filter_slew(f->alpha, target_alpha);
    if (f->mix != target_mix)
        f->mix = br_filter_slew(f->mix, target_mix);
#else
    f->alpha = br_filter_slew(f->alpha, target_alpha);
    f->mix = br_filter_slew(f->mix, target_mix);
#endif
    if (input > 262144) input = 262144;
    if (input < -262144) input = -262144;
    /* The integrator's rounding leaves a dead band: with silence in, the two states can freeze at a
     * constant offset (hundreds of units at low cutoffs) and the output never returns to zero, which
     * keeps the track, and the shared reverb it feeds, running for ever. Silence therefore walks both
     * states to zero a unit at a time; a ringing resonance is far larger than that and is unaffected. */
    if (!input) {
        if (f->low) f->low += f->low > 0 ? -1 : 1;
        if (f->band) f->band += f->band > 0 ? -1 : 1;
    }
    f->low = br_filter_state_clip((int64_t)f->low +
        br_filter_mul_q15(f->band, (uint32_t)f->alpha));
    high = input - f->low - br_filter_mul_q15(f->band, f->damping);
    f->band = br_filter_state_clip((int64_t)f->band +
        br_filter_mul_q15(high, (uint32_t)f->alpha));
    high = input - f->low - br_filter_mul_q15(f->band, f->damping);
    if (f->mix > 0) {
        filtered = f->low;
        wet = f->mix;
    } else if (f->mix < 0) {
        filtered = high;
        wet = -f->mix;
    } else return input;
    return input + br_filter_mul_q15(filtered - input, (uint32_t)wet);
}

static int32_t br_filter_process_controls(br_filter_t *f, int32_t input,
                                          uint32_t control,
                                          uint32_t resonance,
                                          uint32_t keytrack,
                                          uint32_t note)
{
    if (control > 127u) control = 127u;
    if (keytrack) control = br_filter_keytrack(control, note);
    if (!resonance) {
        f->band = 0;
        return br_filter_process(f, input, control);
    }
    return br_filter_process_resonant(f, input, control, resonance);
}
