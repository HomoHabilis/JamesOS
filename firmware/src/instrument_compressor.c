/* SPDX-License-Identifier: GPL-3.0-only */
/* Sample-wise stereo-linked global compressor. Included by instrument_fx.c. */
#ifndef BR_COMPRESSOR_IMPLEMENTED
#define BR_COMPRESSOR_IMPLEMENTED

#ifndef BR_COMP_GAIN_POW_TABLE
#define BR_COMP_GAIN_POW_TABLE 1
#endif
#ifndef BR_COMP_SLEW_RECIP_Q16
#define BR_COMP_SLEW_RECIP_Q16 1
#endif
#ifndef BR_COMP_SLEW_RECIP_PRODUCT_REUSE
#define BR_COMP_SLEW_RECIP_PRODUCT_REUSE 1
#endif
#if BR_COMP_SLEW_RECIP_PRODUCT_REUSE != 0 && BR_COMP_SLEW_RECIP_PRODUCT_REUSE != 1
#error "BR_COMP_SLEW_RECIP_PRODUCT_REUSE must be 0 or 1"
#endif
#ifndef BR_COMP_MUL_Q15_SPLIT
#define BR_COMP_MUL_Q15_SPLIT 0
#endif
#ifndef BR_COMP_GAIN_RATE8_DIV_SHIFT
#define BR_COMP_GAIN_RATE8_DIV_SHIFT 1
#endif
#if BR_COMP_GAIN_RATE8_DIV_SHIFT != 0 && BR_COMP_GAIN_RATE8_DIV_SHIFT != 1
#error "BR_COMP_GAIN_RATE8_DIV_SHIFT must be 0 or 1"
#endif
#ifndef BR_COMP_MUL_Q15_APPROX32
#define BR_COMP_MUL_Q15_APPROX32 1
#endif
#if BR_COMP_MUL_Q15_APPROX32 != 0 && BR_COMP_MUL_Q15_APPROX32 != 1
#error "BR_COMP_MUL_Q15_APPROX32 must be 0 or 1"
#endif
#if BR_COMP_MUL_Q15_APPROX32 && BR_COMP_MUL_Q15_SPLIT
#error "BR_COMP_MUL_Q15_APPROX32 and BR_COMP_MUL_Q15_SPLIT are alternative fast paths"
#endif
#ifndef BR_COMP_INPLACE_TRACKS
#define BR_COMP_INPLACE_TRACKS 1
#endif
#ifndef BR_COMP_GAIN_UPDATE_RATE4
#define BR_COMP_GAIN_UPDATE_RATE4 0
#endif
#ifndef BR_COMP_GAIN_UPDATE_RATE2
#define BR_COMP_GAIN_UPDATE_RATE2 0
#endif
#ifndef BR_COMP_GAIN_UPDATE_RATE8
#define BR_COMP_GAIN_UPDATE_RATE8 1
#endif
#if BR_COMP_GAIN_RATE8_DIV_SHIFT && !BR_COMP_GAIN_UPDATE_RATE8
#error "BR_COMP_GAIN_RATE8_DIV_SHIFT requires BR_COMP_GAIN_UPDATE_RATE8"
#endif
#ifndef BR_COMP_HOT_DIV_RECIP
#define BR_COMP_HOT_DIV_RECIP 1
#endif
#ifndef BR_COMP_FULL_WET_FASTPATH
#define BR_COMP_FULL_WET_FASTPATH 1
#endif
#if (BR_COMP_GAIN_UPDATE_RATE4 + BR_COMP_GAIN_UPDATE_RATE2 + BR_COMP_GAIN_UPDATE_RATE8) > 1
#error "Select only one compressor gain update rate"
#endif
#if BR_COMP_GAIN_UPDATE_RATE4
#define BR_COMP_GAIN_UPDATE_INTERVAL 4u
#elif BR_COMP_GAIN_UPDATE_RATE2
#define BR_COMP_GAIN_UPDATE_INTERVAL 2u
#elif BR_COMP_GAIN_UPDATE_RATE8
#define BR_COMP_GAIN_UPDATE_INTERVAL 8u
#endif

#ifdef BR_SPLIT_AUDIO
volatile uint32_t br_comp_reduction_q15;
#else
static volatile uint32_t br_comp_reduction_q15;
#endif
static br_compressor_config_t br_comp_cached = BR_COMP_DEFAULTS;
static uint32_t br_comp_cached_revision;
static uint32_t br_comp_source_key;
static uint32_t br_comp_initialized;
static uint32_t br_comp_hold_left;
static int32_t br_comp_envelope;
static uint32_t br_comp_gain_q15 = 32768u;
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
/* The detector/envelope remain sample-wise. Only the comparatively expensive
 * envelope-to-compression-gain curve is evaluated at the selected interval;
 * cached endpoints are interpolated sample-wise before the existing gain
 * slew, so the control path and stereo-link state still advance every sample. */
static uint32_t br_comp_gain_target_from_q15 = 32768u;
static uint32_t br_comp_gain_target_to_q15 = 32768u;
static uint32_t br_comp_gain_target_phase;
static uint32_t br_comp_gain_target_valid;
static uint32_t br_comp_gain_target_reseed = 1u;
#endif
static uint32_t br_comp_threshold_cached, br_comp_ratio_cached;
static uint32_t br_comp_attack_cached, br_comp_release_cached, br_comp_hold_cached;
#if BR_COMP_SLEW_RECIP_Q16
static uint32_t br_comp_attack_recip_q16, br_comp_release_recip_q16;
#endif
static uint32_t br_comp_makeup_cached;
static uint32_t br_comp_threshold_recip_q24, br_comp_ratio_loss_q15;

static const uint8_t br_comp_ratio_table[8] = {1, 2, 3, 4, 6, 8, 12, 20};
/* Q15 full-scale amplitudes for -48..0 dB, in 3 dB increments. */
static const uint16_t br_comp_threshold_table[17] = {
    130u, 184u, 260u, 367u, 518u, 732u, 1034u, 1461u, 2064u,
    2916u, 4119u, 5817u, 8215u, 11605u, 16384u, 23170u, 32767u
};
/* Q15 makeup gains, 0..24 dB in 3 dB increments. */
static const uint32_t br_comp_makeup_table[9] = {
    32768u, 46295u, 65382u, 92319u, 130447u, 184270u,
    260282u, 367596u, 519288u
};

/* Q8 log2 values at 1/16 intervals, including the final 2.0 endpoint. The
 * compressor evaluates this per sample, so a short linear table avoids the
 * repeated squaring used by the iterative version. Across the full normalized
 * Q16 interval, interpolation differs from the prior integer estimator by
 * at most two Q8 codes. */
#if defined(FM1_DEVICE)
#define BR_COMP_TABLE_DATA __attribute__((section(".data.fm1_fx_tables")))
#else
#define BR_COMP_TABLE_DATA
#endif
static const uint16_t br_comp_log2_fraction_table[17] BR_COMP_TABLE_DATA = {
      0u,  22u,  44u,  63u,  82u, 100u, 118u, 134u, 150u,
    165u, 179u, 193u, 207u, 220u, 232u, 244u, 256u
};
#if BR_COMP_GAIN_POW_TABLE
/* Results of the existing Q15 binary-power loop for 10^(-dB/20), dB 0..57.
 * The maximum runtime attenuation is 57 dB (threshold >= 130 Q15, detector
 * <= 131068, ratio <= 20:1). Keep this selector independent because placing
 * the table in hot RAM trades 116 bytes for fewer per-sample multiplies. */
static const uint16_t br_comp_gain_db_table[58] BR_COMP_TABLE_DATA = {
    32768u, 29196u, 26013u, 23177u, 20650u, 18398u, 16393u, 14605u,
    13013u, 11594u, 10330u,  9204u,  8200u,  7306u,  6510u,  5800u,
     5167u,  4603u,  4101u,  3654u,  3256u,  2901u,  2584u,  2302u,
     2051u,  1828u,  1628u,  1451u,  1293u,  1152u,  1026u,   914u,
      814u,   725u,   646u,   575u,   512u,   457u,   407u,   362u,
      323u,   288u,   256u,   228u,   203u,   181u,   161u,   144u,
      128u,   114u,   101u,    90u,    80u,    72u,    64u,    57u,
       50u,    45u
};
#endif
#undef BR_COMP_TABLE_DATA

static inline __attribute__((always_inline)) uint32_t br_comp_clamp(uint32_t x, uint32_t hi)
{ return x > hi ? hi : x; }

BR_AUDIO_API int32_t br_comp_threshold_db(uint32_t v)
{ return -(int32_t)(48u - br_comp_clamp(v, 48u)); }

static BR_FX_RAM_AUDIO uint32_t br_comp_ratio(uint32_t v)
{ return br_comp_ratio_table[br_comp_clamp(v, 7u)]; }

BR_AUDIO_API uint32_t br_comp_attack_samples(uint32_t v)
{
    uint32_t square = br_comp_clamp(v, 127u) * br_comp_clamp(v, 127u);
    uint32_t minimum = FS / 10000u, maximum = FS / 10u;
    return minimum + square * (maximum - minimum) / 16129u;
}

BR_AUDIO_API uint32_t br_comp_release_samples(uint32_t v)
{
    uint32_t square = br_comp_clamp(v, 127u) * br_comp_clamp(v, 127u);
    uint32_t minimum = FS / 100u, maximum = (FS * 2u);
    return minimum + square * (maximum - minimum) / 16129u;
}

BR_AUDIO_API uint32_t br_comp_hold_samples(uint32_t v)
{ v = br_comp_clamp(v, 127u); return (v * v * (FS / 2u)) / 16129u; }

static BR_FX_RAM_AUDIO uint32_t br_comp_makeup_q15(uint32_t value);
static BR_FX_RAM_AUDIO uint32_t br_comp_threshold_q15(uint32_t value);

static BR_FX_RAM_AUDIO void br_comp_snapshot(const br_compressor_config_t *provided,
                                             uint32_t provided_revision)
{
    uint32_t before = provided ? provided_revision : br_master.revision, after;
    br_compressor_config_t c;
    if (before & 1u) return;
    if (br_comp_initialized && before == br_comp_cached_revision) return;
    if (provided) c = *provided;
    else {
        c.source = br_master.comp.source;
        c.threshold = br_master.comp.threshold;
        c.ratio = br_master.comp.ratio;
        c.makeup = br_master.comp.makeup;
        c.attack = br_master.comp.attack;
        c.release = br_master.comp.release;
        c.hold = br_master.comp.hold;
        c.mix = br_master.comp.mix;
    }
    after = provided ? before : br_master.revision;
    if (before != after || (after & 1u)) return;
    if (!br_comp_initialized || before != br_comp_cached_revision) {
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
        /* Control revisions take effect from an exact newly computed target;
         * interpolation resumes from that target on subsequent samples. */
        br_comp_gain_target_phase = 0u;
        br_comp_gain_target_valid = 0u;
        br_comp_gain_target_reseed = 1u;
#endif
        uint32_t source = br_comp_clamp(c.source, BR_COMP_SOURCE_COUNT - 1u);
        if (br_comp_initialized && source != br_comp_source_key) {
            /* Never carry reduction learned from a formerly protected source. */
            br_comp_envelope = 0;
            br_comp_hold_left = 0;
            br_comp_gain_q15 = 32768u;
            br_comp_reduction_q15 = 0;
        }
        br_comp_source_key = source;
        br_comp_cached = c;
        br_comp_cached.source = source;
        br_comp_cached.threshold = br_comp_clamp(c.threshold, 48u);
        br_comp_cached.ratio = br_comp_clamp(c.ratio, 7u);
        br_comp_cached.makeup = br_comp_clamp(c.makeup, 24u);
        br_comp_cached.attack = br_comp_clamp(c.attack, 127u);
        br_comp_cached.release = br_comp_clamp(c.release, 127u);
        br_comp_cached.hold = br_comp_clamp(c.hold, 127u);
        br_comp_cached.mix = br_comp_clamp(c.mix, 127u);
        br_comp_threshold_cached = br_comp_threshold_q15(br_comp_cached.threshold);
        br_comp_ratio_cached = br_comp_ratio(br_comp_cached.ratio);
        { uint32_t v = br_comp_clamp(br_comp_cached.attack, 127u);
          uint32_t square = v * v;
          br_comp_attack_cached = FS / 10000u + square * (FS / 10u - FS / 10000u) / 16129u;
#if BR_COMP_SLEW_RECIP_Q16
          br_comp_attack_recip_q16 = (65536u / br_comp_attack_cached) +
              (65536u % br_comp_attack_cached != 0u);
#endif
        }
        { uint32_t v = br_comp_clamp(br_comp_cached.release, 127u);
          uint32_t square = v * v;
          br_comp_release_cached = FS / 100u + square * (FS * 2u - FS / 100u) / 16129u;
#if BR_COMP_SLEW_RECIP_Q16
          br_comp_release_recip_q16 = (65536u / br_comp_release_cached) +
              (65536u % br_comp_release_cached != 0u);
#endif
        }
        { uint32_t v = br_comp_clamp(br_comp_cached.hold, 127u);
          br_comp_hold_cached = (v * v * (FS / 2u)) / 16129u; }
        br_comp_makeup_cached = br_comp_makeup_q15(br_comp_cached.makeup);
        br_comp_threshold_recip_q24 = br_comp_threshold_cached ?
            (1u << 24) / br_comp_threshold_cached : 0u;
        br_comp_ratio_loss_q15 = br_comp_ratio_cached > 1u ?
            ((br_comp_ratio_cached - 1u) * 32768u) / br_comp_ratio_cached : 0u;
        br_comp_cached_revision = before;
        br_comp_initialized = 1u;
    }
}

static BR_FX_RAM_AUDIO int32_t br_comp_abs(int32_t x) { return x < 0 ? -x : x; }

/* Exact signed division by four without SDIV. The selected detector is an
 * absolute audio/key sample in the normal path; this helper also preserves C's
 * truncation-toward-zero result if a negative value reaches it. */
static __attribute__((always_inline)) inline int32_t
br_comp_div4_toward_zero(int32_t value)
{
    uint32_t bits = (uint32_t)value;
    uint32_t magnitude = value < 0 ? 0u - bits : bits;
    int32_t quotient = (int32_t)(magnitude >> 2);
    return value < 0 ? -quotient : quotient;
}

#if BR_COMP_GAIN_RATE8_DIV_SHIFT
/* Exact signed /8 with C truncation toward zero for the bounded rate-8
 * interpolation numerator (difference * position). */
static __attribute__((always_inline)) inline int32_t
br_comp_div8_toward_zero(int32_t value)
{
    uint32_t bits = (uint32_t)value;
    uint32_t magnitude = value < 0 ? 0u - bits : bits;
    int32_t quotient = (int32_t)(magnitude >> 3);
    return value < 0 ? -quotient : quotient;
}
#endif

#if BR_COMP_HOT_DIV_RECIP
/* Exact reciprocal quotient for /96. ceil(2^38 / 96) is exact for every
 * uint32 magnitude; the wide product maps to a native 32x32->64 multiply and
 * avoids signed overflow and implementation-defined shifts. Division by 127
 * reuses br_fx_div127 from instrument_fx_div127.h. */
static __attribute__((always_inline)) inline uint32_t
br_comp_div96_magnitude(uint32_t magnitude)
{
    return (uint32_t)(((uint64_t)magnitude * 0xaaaaaaabu) >> 38);
}

static __attribute__((always_inline)) inline int32_t
br_comp_div96_toward_zero(int32_t value)
{
    uint32_t bits = (uint32_t)value;
    uint32_t magnitude = value < 0 ? 0u - bits : bits;
    int32_t quotient = (int32_t)br_comp_div96_magnitude(magnitude);
    return value < 0 ? -quotient : quotient;
}

static BR_FX_RAM_AUDIO int32_t br_comp_gain_slew96(int32_t current, int32_t target)
{
    int32_t difference = target - current;
    int32_t step = br_comp_div96_toward_zero(difference);
    if (!step && difference) step = difference > 0 ? 1 : -1;
    return current + step;
}
#endif

static BR_FX_RAM_AUDIO int32_t br_comp_slew(int32_t current, int32_t target, uint32_t samples)
{
    int32_t d = target - current;
    int32_t step;
    if (!samples) return target;
    step = d / (int32_t)samples;
    if (!step && d) step = d > 0 ? 1 : -1;
    return current + step;
}

#if BR_COMP_SLEW_RECIP_Q16
/* Equivalent to signed division for the bounded detector range, without a
 * per-sample divide. ceil(2^16 / samples) overestimates the quotient by at
 * most two for |delta| <= 131068; correct that bounded error before sign. */
static BR_FX_RAM_AUDIO int32_t br_comp_slew_recip_q16(int32_t current,
                                                       int32_t target,
                                                       uint32_t samples,
                                                       uint32_t reciprocal_q16)
{
    int32_t d = target - current;
    uint32_t magnitude, step;
    if (!samples || samples == 1u) return target;
    magnitude = (uint32_t)(d < 0 ? -d : d);
    step = (magnitude * reciprocal_q16) >> 16;
#if BR_COMP_SLEW_RECIP_PRODUCT_REUSE
    {
        uint32_t product = step * samples;
        if (product > magnitude) {
            --step;
            product -= samples;
            if (product > magnitude) --step;
        }
    }
#else
    if (step * samples > magnitude) --step;
    if (step * samples > magnitude) --step;
#endif
    if (!step && d) step = 1u;
    return current + (d < 0 ? -(int32_t)step : (int32_t)step);
}
#endif

static BR_FX_RAM_AUDIO int32_t br_comp_mul_q15(int32_t x, uint32_t q)
{
#if BR_COMP_MUL_Q15_APPROX32
    /* Deliberately floor negative products instead of rounding toward zero.
     * On this bounded branch, |x*q| <= 2^31, so the 32-bit product cannot
     * overflow; the fallback retains the complete original domain. */
    if (q <= 32768u && x >= -65536 && x <= 65535) {
        int32_t product = x * (int32_t)q;
        return product >> 15;
    }
#endif
#if BR_COMP_MUL_Q15_SPLIT
    /* Normal post-FX samples and compressor gains are tightly bounded. Split
     * q at Q15 so the common path uses only 32-bit products; keep the general
     * 64-bit path for unusual inputs or future gain ranges. The split is exact:
     * floor(|x|*q/2^15) = |x|*(q>>15) + floor(|x|*(q&32767)/2^15). */
    uint32_t magnitude = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    if (magnitude <= 131071u && q <= 524287u) {
        uint32_t scaled = magnitude * (q >> 15) +
            ((magnitude * (q & 32767u)) >> 15);
        return x < 0 ? -(int32_t)scaled : (int32_t)scaled;
    }
#endif
    int64_t product = (int64_t)x * q;
    if (product < 0)
        return -(int32_t)(((uint64_t)(-product)) >> 15);
    return (int32_t)(((uint64_t)product) >> 15);
}

static void br_comp_init(void)
{
    br_comp_cached = (br_compressor_config_t)BR_COMP_DEFAULTS;
    br_comp_cached_revision = 0u;
    br_comp_source_key = BR_COMP_OFF;
    br_comp_initialized = 0u;
    br_comp_hold_left = 0u;
    br_comp_envelope = 0;
    br_comp_gain_q15 = 32768u;
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    br_comp_gain_target_from_q15 = 32768u;
    br_comp_gain_target_to_q15 = 32768u;
    br_comp_gain_target_phase = 0u;
    br_comp_gain_target_valid = 0u;
    br_comp_gain_target_reseed = 1u;
#endif
    br_comp_threshold_cached = 0u;
    br_comp_ratio_cached = 1u;
    br_comp_attack_cached = FS / 10000u;
    br_comp_release_cached = FS / 100u;
#if BR_COMP_SLEW_RECIP_Q16
    br_comp_attack_recip_q16 = (65536u / (FS / 10000u)) +
        (65536u % (FS / 10000u) != 0u);
    br_comp_release_recip_q16 = (65536u / (FS / 100u)) +
        (65536u % (FS / 100u) != 0u);
#endif
    br_comp_hold_cached = 0u;
    br_comp_makeup_cached = 32768u;
    br_comp_threshold_recip_q24 = 0u;
    br_comp_ratio_loss_q15 = 0u;
    br_comp_reduction_q15 = 0u;
}

static BR_FX_RAM_AUDIO uint32_t br_comp_makeup_q15(uint32_t value)
{
    uint32_t pos = br_comp_clamp(value, 24u), idx = pos / 3u, rem = pos % 3u;
    uint32_t a = br_comp_makeup_table[idx];
    if (idx >= 8u || !rem) return a;
    return a + (br_comp_makeup_table[idx + 1u] - a) * rem / 3u;
}

static BR_FX_RAM_AUDIO uint32_t br_comp_threshold_q15(uint32_t value)
{
    uint32_t pos = br_comp_clamp(value, 48u), idx = pos / 3u, rem = pos % 3u;
    uint32_t a = br_comp_threshold_table[idx];
    if (idx >= 16u || !rem) return a;
    return a + (br_comp_threshold_table[idx + 1u] - a) * rem / 3u;
}

/* Approximate log2(value) in Q8 for a positive fixed-point value. */
static BR_FX_RAM_AUDIO uint32_t br_comp_log2_q8(uint32_t value_q16)
{
    uint32_t whole, normalized, index, remainder, fraction;
    if (value_q16 < 65536u) return 0u;
    whole = 31u - (uint32_t)__builtin_clz(value_q16) - 16u;
    normalized = value_q16 >> (whole + 8u);
    index = (normalized - 256u) >> 4;
    remainder = (normalized - 256u) & 15u;
    fraction = br_comp_log2_fraction_table[index] +
        ((uint32_t)(br_comp_log2_fraction_table[index + 1u] -
                    br_comp_log2_fraction_table[index]) * remainder >> 4);
    return whole * 256u + fraction;
}

static BR_FX_RAM_AUDIO uint32_t br_comp_gain_for_coeff(uint32_t envelope,
                                       uint32_t threshold_recip_q24,
                                       uint32_t ratio_loss_q15)
{
    uint32_t ratio_q16, over_db_q8, attenuation_q8, integer_db, fraction;
    uint32_t gain = 32768u, power, exponent;
    if (!threshold_recip_q24 || ratio_loss_q15 == 0u) return 32768u;
    /* The detector is clamped to 131068 and the threshold reciprocal is
     * below 2^17. Split the product at bit 8 to keep each partial product in
     * uint32_t while retaining the original floor exactly. */
    ratio_q16 = (envelope >> 8) * threshold_recip_q24 +
        (((envelope & 255u) * threshold_recip_q24) >> 8);
    if (ratio_q16 <= 65536u) return 32768u;
    over_db_q8 = br_comp_log2_q8(ratio_q16) * 1541u / 256u;
    attenuation_q8 = (over_db_q8 * ratio_loss_q15) / 32768u;
    integer_db = attenuation_q8 / 256u;
    fraction = attenuation_q8 & 255u;
#if BR_COMP_GAIN_POW_TABLE
    if (integer_db < sizeof br_comp_gain_db_table / sizeof br_comp_gain_db_table[0]) {
        gain = br_comp_gain_db_table[integer_db];
    } else
#endif
    {
        power = 29196u;
        exponent = integer_db;
        while (exponent) {
            if (exponent & 1u) gain = (gain * power) >> 15;
            exponent >>= 1;
            if (exponent) power = (power * power) >> 15;
        }
    }
    /* Linear interpolation over the final fractional dB is within 0.05 dB. */
    gain = (gain * (32768u - (3571u * fraction) / 256u)) >> 15;
    return gain;
}

static BR_FX_RAM_AUDIO uint32_t br_comp_gain_for_level(uint32_t envelope, uint32_t threshold,
                                       uint32_t ratio)
{
    uint32_t reciprocal = threshold ? (1u << 24) / threshold : 0u;
    uint32_t loss = ratio > 1u ? ((ratio - 1u) * 32768u) / ratio : 0u;
    return br_comp_gain_for_coeff(envelope, reciprocal, loss);
}

#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
static BR_FX_RAM_AUDIO uint32_t br_comp_gain_target_rate_n(uint32_t envelope)
{
    uint32_t position;
    int32_t difference;
    if (br_comp_gain_target_reseed) {
        br_comp_gain_target_phase = 0u;
        br_comp_gain_target_valid = 0u;
        br_comp_gain_target_reseed = 0u;
    }
    if (!br_comp_gain_target_phase) {
        uint32_t next = br_comp_gain_for_coeff((uint32_t)envelope,
                                                br_comp_threshold_recip_q24,
                                                br_comp_ratio_loss_q15);
        if (!br_comp_gain_target_valid) {
            br_comp_gain_target_from_q15 = next;
            br_comp_gain_target_to_q15 = next;
            br_comp_gain_target_valid = 1u;
        } else {
            br_comp_gain_target_from_q15 = br_comp_gain_target_to_q15;
            br_comp_gain_target_to_q15 = next;
        }
    }
    position = br_comp_gain_target_phase + 1u;
    difference = (int32_t)br_comp_gain_target_to_q15 -
                 (int32_t)br_comp_gain_target_from_q15;
    br_comp_gain_target_phase = position == BR_COMP_GAIN_UPDATE_INTERVAL ? 0u : position;
#if BR_COMP_GAIN_UPDATE_INTERVAL == 4u
    return (uint32_t)((int32_t)br_comp_gain_target_from_q15 +
                      br_comp_div4_toward_zero(difference * (int32_t)position));
#elif BR_COMP_GAIN_UPDATE_INTERVAL == 8u && BR_COMP_GAIN_RATE8_DIV_SHIFT
    return (uint32_t)((int32_t)br_comp_gain_target_from_q15 +
                      br_comp_div8_toward_zero(difference * (int32_t)position));
#else
    return (uint32_t)((int32_t)br_comp_gain_target_from_q15 +
                      difference * (int32_t)position /
                          (int32_t)BR_COMP_GAIN_UPDATE_INTERVAL);
#endif
}
#endif

static BR_FX_RAM_AUDIO uint32_t br_comp_mix_gain(uint32_t compressed_q15, uint32_t makeup_q15,
                                uint32_t mix)
{
    uint32_t wet_q15 = compressed_q15 * (makeup_q15 >> 15) +
        (compressed_q15 * (makeup_q15 & 32767u) >> 15);
    mix = br_comp_clamp(mix, 127u);
#if BR_COMP_FULL_WET_FASTPATH
    /* At the fully-wet endpoint, (wet - unity) * 127 / 127 is exact and
     * returns wet directly. The selected mix is stable until the next block. */
    if (mix == 127u) return wet_q15;
#endif
#if BR_COMP_HOT_DIV_RECIP
    return (uint32_t)(32768 + br_fx_div127(
        ((int32_t)wet_q15 - 32768) * (int32_t)mix));
#else
    return (uint32_t)(32768 + ((int32_t)wet_q15 - 32768) * (int32_t)mix / 127);
#endif
}

/* Tracks contain post-FX/post-pan samples. Keys are pre-mixer mono taps;
 * normalize the chosen key by four here to match the dry mixer scale. */
static BR_FX_RAM_AUDIO int br_comp_bypassed(void)
{
    return br_comp_cached.source == BR_COMP_OFF || !br_comp_cached.mix ||
        (br_comp_cached.ratio == 0u && br_comp_cached.makeup == 0u);
}

static BR_FX_RAM_AUDIO void br_comp_reset_detector(void)
{
    br_comp_envelope = 0;
    br_comp_hold_left = 0;
    br_comp_gain_q15 = 32768u;
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    br_comp_gain_target_phase = 0u;
    br_comp_gain_target_valid = 0u;
    br_comp_gain_target_reseed = 1u;
#endif
    br_comp_reduction_q15 = 0;
}

#if BR_COMP_FINISH_SUM_FOLD && BR_COMP_INPLACE_TRACKS
/* Split the detector/gain transition from per-track application so the
 * experimental finish path can apply this unchanged gain once to the sum. */
static BR_FX_RAM_AUDIO uint32_t br_comp_next_mix_gain(int32_t detector,
                                                      uint32_t source)
{
    uint32_t gain_q15, mix, combined_q;
    if (source != BR_COMP_ALL) detector = br_comp_div4_toward_zero(detector);
    if (detector > 131068) detector = 131068;
    if (detector > br_comp_envelope) {
#if BR_COMP_SLEW_RECIP_Q16
        br_comp_envelope = br_comp_slew_recip_q16(br_comp_envelope, detector,
            br_comp_attack_cached, br_comp_attack_recip_q16);
#else
        br_comp_envelope = br_comp_slew(br_comp_envelope, detector, br_comp_attack_cached);
#endif
        br_comp_hold_left = br_comp_hold_cached;
    } else if (br_comp_hold_left) {
        --br_comp_hold_left;
    } else {
#if BR_COMP_SLEW_RECIP_Q16
        br_comp_envelope = br_comp_slew_recip_q16(br_comp_envelope, detector,
            br_comp_release_cached, br_comp_release_recip_q16);
#else
        br_comp_envelope = br_comp_slew(br_comp_envelope, detector, br_comp_release_cached);
#endif
    }
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    gain_q15 = br_comp_gain_target_rate_n((uint32_t)br_comp_envelope);
#else
    gain_q15 = br_comp_gain_for_coeff((uint32_t)br_comp_envelope,
                                      br_comp_threshold_recip_q24,
                                      br_comp_ratio_loss_q15);
#endif
#if BR_COMP_HOT_DIV_RECIP
    br_comp_gain_q15 = (uint32_t)br_comp_gain_slew96((int32_t)br_comp_gain_q15,
                                                     (int32_t)gain_q15);
#else
    br_comp_gain_q15 = (uint32_t)br_comp_slew((int32_t)br_comp_gain_q15,
                                              (int32_t)gain_q15, 96u);
#endif
    mix = br_comp_cached.mix;
    combined_q = br_comp_mix_gain(br_comp_gain_q15, br_comp_makeup_cached, mix);
    br_comp_reduction_q15 = 32768u - br_comp_gain_q15;
    return combined_q;
}

#ifndef BR_COMP_BLOCK_LOCAL_STATE
#define BR_COMP_BLOCK_LOCAL_STATE 1
#endif
#if BR_COMP_BLOCK_LOCAL_STATE
/* Block-local copy of the compressor detector/gain state and its cached
 * parameters. Each load or store of these globals costs about ten cycles on
 * this part, and the per-frame gain step touched roughly thirty of them. The
 * finish loop loads this once, runs br_comp_local_next per frame and stores it
 * once. br_comp_local_next is br_comp_next_mix_gain with every global replaced
 * by the matching field; tests/test_comp_local.c checks them step for step. */
typedef struct {
    int32_t envelope;
    uint32_t hold_left, gain_q15, reduction_q15;
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    uint32_t target_from_q15, target_to_q15, target_phase, target_valid, target_reseed;
#endif
    uint32_t attack, release, hold, makeup, mix, threshold_recip_q24, ratio_loss_q15;
#if BR_COMP_SLEW_RECIP_Q16
    uint32_t attack_recip_q16, release_recip_q16;
#endif
} br_comp_local_t;

static inline __attribute__((always_inline)) void br_comp_local_load(br_comp_local_t *l)
{
    l->envelope = br_comp_envelope;
    l->hold_left = br_comp_hold_left;
    l->gain_q15 = br_comp_gain_q15;
    l->reduction_q15 = br_comp_reduction_q15;
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    l->target_from_q15 = br_comp_gain_target_from_q15;
    l->target_to_q15 = br_comp_gain_target_to_q15;
    l->target_phase = br_comp_gain_target_phase;
    l->target_valid = br_comp_gain_target_valid;
    l->target_reseed = br_comp_gain_target_reseed;
#endif
    l->attack = br_comp_attack_cached;
    l->release = br_comp_release_cached;
    l->hold = br_comp_hold_cached;
    l->makeup = br_comp_makeup_cached;
    l->mix = br_comp_cached.mix;
    l->threshold_recip_q24 = br_comp_threshold_recip_q24;
    l->ratio_loss_q15 = br_comp_ratio_loss_q15;
#if BR_COMP_SLEW_RECIP_Q16
    l->attack_recip_q16 = br_comp_attack_recip_q16;
    l->release_recip_q16 = br_comp_release_recip_q16;
#endif
}

static inline __attribute__((always_inline)) void br_comp_local_store(const br_comp_local_t *l)
{
    br_comp_envelope = l->envelope;
    br_comp_hold_left = l->hold_left;
    br_comp_gain_q15 = l->gain_q15;
    br_comp_reduction_q15 = l->reduction_q15;
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    br_comp_gain_target_from_q15 = l->target_from_q15;
    br_comp_gain_target_to_q15 = l->target_to_q15;
    br_comp_gain_target_phase = l->target_phase;
    br_comp_gain_target_valid = l->target_valid;
    br_comp_gain_target_reseed = l->target_reseed;
#endif
}

#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
static inline __attribute__((always_inline)) uint32_t br_comp_local_target(
    br_comp_local_t *l, uint32_t envelope)
{
    uint32_t position;
    int32_t difference;
    if (l->target_reseed) {
        l->target_phase = 0u;
        l->target_valid = 0u;
        l->target_reseed = 0u;
    }
    if (!l->target_phase) {
        uint32_t next = br_comp_gain_for_coeff(envelope, l->threshold_recip_q24,
                                               l->ratio_loss_q15);
        if (!l->target_valid) {
            l->target_from_q15 = next;
            l->target_to_q15 = next;
            l->target_valid = 1u;
        } else {
            l->target_from_q15 = l->target_to_q15;
            l->target_to_q15 = next;
        }
    }
    position = l->target_phase + 1u;
    difference = (int32_t)l->target_to_q15 - (int32_t)l->target_from_q15;
    l->target_phase = position == BR_COMP_GAIN_UPDATE_INTERVAL ? 0u : position;
#if BR_COMP_GAIN_UPDATE_INTERVAL == 4u
    return (uint32_t)((int32_t)l->target_from_q15 +
                      br_comp_div4_toward_zero(difference * (int32_t)position));
#elif BR_COMP_GAIN_UPDATE_INTERVAL == 8u && BR_COMP_GAIN_RATE8_DIV_SHIFT
    return (uint32_t)((int32_t)l->target_from_q15 +
                      br_comp_div8_toward_zero(difference * (int32_t)position));
#else
    return (uint32_t)((int32_t)l->target_from_q15 +
                      difference * (int32_t)position /
                          (int32_t)BR_COMP_GAIN_UPDATE_INTERVAL);
#endif
}
#endif

static inline __attribute__((always_inline)) uint32_t br_comp_local_next(
    br_comp_local_t *l, int32_t detector, uint32_t source)
{
    uint32_t gain_q15, combined_q;
    if (source != BR_COMP_ALL) detector = br_comp_div4_toward_zero(detector);
    if (detector > 131068) detector = 131068;
    if (detector > l->envelope) {
#if BR_COMP_SLEW_RECIP_Q16
        l->envelope = br_comp_slew_recip_q16(l->envelope, detector,
            l->attack, l->attack_recip_q16);
#else
        l->envelope = br_comp_slew(l->envelope, detector, l->attack);
#endif
        l->hold_left = l->hold;
    } else if (l->hold_left) {
        --l->hold_left;
    } else {
#if BR_COMP_SLEW_RECIP_Q16
        l->envelope = br_comp_slew_recip_q16(l->envelope, detector,
            l->release, l->release_recip_q16);
#else
        l->envelope = br_comp_slew(l->envelope, detector, l->release);
#endif
    }
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
    gain_q15 = br_comp_local_target(l, (uint32_t)l->envelope);
#else
    gain_q15 = br_comp_gain_for_coeff((uint32_t)l->envelope,
                                      l->threshold_recip_q24, l->ratio_loss_q15);
#endif
#if BR_COMP_HOT_DIV_RECIP
    l->gain_q15 = (uint32_t)br_comp_gain_slew96((int32_t)l->gain_q15, (int32_t)gain_q15);
#else
    l->gain_q15 = (uint32_t)br_comp_slew((int32_t)l->gain_q15, (int32_t)gain_q15, 96u);
#endif
    combined_q = br_comp_mix_gain(l->gain_q15, l->makeup, l->mix);
    l->reduction_q15 = 32768u - l->gain_q15;
    return combined_q;
}
#endif
static BR_FX_RAM_AUDIO void br_comp_process_tracks(
    int32_t tracks[BR_PARTS][BR_BLOCK][2], uint32_t sample,
    int32_t key, uint32_t protected_part)
{
    int32_t detector = 0;
    uint32_t p, source = br_comp_cached.source, combined_q;
    if (source == BR_COMP_ALL) {
        int32_t mix_l = 0, mix_r = 0;
        for (p = 0; p < BR_PARTS; ++p) {
            mix_l += tracks[p][sample][0]; mix_r += tracks[p][sample][1];
        }
        detector = br_comp_abs(mix_l);
        if (br_comp_abs(mix_r) > detector) detector = br_comp_abs(mix_r);
    } else {
        detector = br_comp_abs(key);
    }
    combined_q = br_comp_next_mix_gain(detector, source);
    if (combined_q != 32768u) {
        for (p = 0; p < BR_PARTS; ++p)
            if (source == BR_COMP_ALL || p != protected_part) {
                tracks[p][sample][0] = br_comp_mul_q15(tracks[p][sample][0], combined_q);
                tracks[p][sample][1] = br_comp_mul_q15(tracks[p][sample][1], combined_q);
            }
    }
}
#else
#if BR_COMP_INPLACE_TRACKS
static BR_FX_RAM_AUDIO void br_comp_process_tracks(
    int32_t tracks[BR_PARTS][BR_BLOCK][2], uint32_t sample,
    int32_t key, uint32_t protected_part)
#else
static BR_FX_RAM_AUDIO void br_comp_process_tracks(int32_t tracks[BR_PARTS][2],
                                   const int32_t keys[BR_PARTS])
#endif
{
    {
        int32_t detector = 0;
#if BR_COMP_INPLACE_TRACKS
        uint32_t p, source = br_comp_cached.source;
#else
        uint32_t p, source = br_comp_cached.source, protected_part = 0u;
#endif
        uint32_t gain_q15, mix, combined_q;
        /* The block caller enters only when the snapshotted compressor is
         * active. Bypass/reset is decided once in br_fx_prepare_block, so do
         * not repeat that control check for every audio sample. */
        if (source == BR_COMP_ALL) {
            int32_t mix_l = 0, mix_r = 0;
            for (p = 0; p < BR_PARTS; ++p) {
#if BR_COMP_INPLACE_TRACKS
                mix_l += tracks[p][sample][0]; mix_r += tracks[p][sample][1];
#else
                mix_l += tracks[p][0]; mix_r += tracks[p][1];
#endif
            }
            detector = br_comp_abs(mix_l);
            if (br_comp_abs(mix_r) > detector) detector = br_comp_abs(mix_r);
        } else {
#if BR_COMP_INPLACE_TRACKS
            detector = br_comp_abs(key);
#else
            protected_part = br_comp_source_part(source);
            detector = br_comp_abs(keys[protected_part]);
#endif
        }
        if (source != BR_COMP_ALL) detector = br_comp_div4_toward_zero(detector);
        if (detector > 131068) detector = 131068;

        if (detector > br_comp_envelope) {
#if BR_COMP_SLEW_RECIP_Q16
            br_comp_envelope = br_comp_slew_recip_q16(br_comp_envelope, detector,
                br_comp_attack_cached, br_comp_attack_recip_q16);
#else
            br_comp_envelope = br_comp_slew(br_comp_envelope, detector, br_comp_attack_cached);
#endif
            br_comp_hold_left = br_comp_hold_cached;
        } else if (br_comp_hold_left) {
            --br_comp_hold_left;
        } else {
#if BR_COMP_SLEW_RECIP_Q16
            br_comp_envelope = br_comp_slew_recip_q16(br_comp_envelope, detector,
                br_comp_release_cached, br_comp_release_recip_q16);
#else
            br_comp_envelope = br_comp_slew(br_comp_envelope, detector, br_comp_release_cached);
#endif
        }
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
        gain_q15 = br_comp_gain_target_rate_n((uint32_t)br_comp_envelope);
#else
        gain_q15 = br_comp_gain_for_coeff((uint32_t)br_comp_envelope,
                                          br_comp_threshold_recip_q24,
                                          br_comp_ratio_loss_q15);
#endif
#if BR_COMP_HOT_DIV_RECIP
        br_comp_gain_q15 = (uint32_t)br_comp_gain_slew96((int32_t)br_comp_gain_q15,
                                                         (int32_t)gain_q15);
#else
        br_comp_gain_q15 = (uint32_t)br_comp_slew((int32_t)br_comp_gain_q15,
                                                  (int32_t)gain_q15, 96u);
#endif
        mix = br_comp_cached.mix;
        combined_q = br_comp_mix_gain(br_comp_gain_q15, br_comp_makeup_cached, mix);
        if (combined_q != 32768u) {
            for (p = 0; p < BR_PARTS; ++p)
                if (source == BR_COMP_ALL || p != protected_part) {
#if BR_COMP_INPLACE_TRACKS
                    tracks[p][sample][0] = br_comp_mul_q15(tracks[p][sample][0], combined_q);
                    tracks[p][sample][1] = br_comp_mul_q15(tracks[p][sample][1], combined_q);
#else
                    tracks[p][0] = br_comp_mul_q15(tracks[p][0], combined_q);
                    tracks[p][1] = br_comp_mul_q15(tracks[p][1], combined_q);
#endif
                }
        }
        {
            int32_t reduction = (int32_t)(32768u - br_comp_gain_q15);
            br_comp_reduction_q15 = (uint32_t)reduction;
        }
    }
}
#endif
#endif
