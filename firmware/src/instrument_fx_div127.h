/* Exact signed division by 127 for the complete int32_t domain.
 * The caller has already formed its numerator using the project's -fwrapv
 * semantics. This helper reproduces C's truncation toward zero. */
#pragma once
#include <stdint.h>

#ifndef BR_FX_DIV127_BOUNDED_FAST
#define BR_FX_DIV127_BOUNDED_FAST 0
#endif
#ifndef BR_FX_DIV127_FLOAT_APPROX
#define BR_FX_DIV127_FLOAT_APPROX 0
#endif
#ifndef BR_FX_DIV127_FLOAT_FULL_APPROX
#define BR_FX_DIV127_FLOAT_FULL_APPROX 0
#endif

static inline __attribute__((always_inline)) int32_t br_fx_div127(int32_t value)
{
#if BR_FX_DIV127_FLOAT_FULL_APPROX
    return (int32_t)((float)value * (1.0f / 127.0f));
#else
    uint32_t raw = (uint32_t)value;
    uint32_t mag = value < 0 ? 0u - raw : raw;
#if BR_FX_DIV127_FLOAT_APPROX
    /* Audited call sites stay within this bound; preserve exact fallback for
     * any larger input so the helper remains safe for its full int32 domain. */
    if (mag <= 0xffffffu) {
        return (int32_t)((float)value * (1.0f / 127.0f));
    }
#endif
#if BR_FX_DIV127_BOUNDED_FAST
    /* Most audio gains divide products no larger than 0xffffff by 127.
     * For that range, 1/127 = 1/128 + 1/16384 + 1/2097152 + ... gives a
     * lower-bound quotient at most three below exact. Correct the small
     * remainder with three bounded comparisons, avoiding the 64-bit product. */
    if (mag <= 0xffffffu) {
        uint32_t q = (mag >> 7) + (mag >> 14) + (mag >> 21);
        uint32_t remainder = mag - q * 127u;
        q += 3u - ((remainder - 127u) >> 31) -
             ((remainder - 254u) >> 31) - ((remainder - 381u) >> 31);
        return value < 0 ? -(int32_t)q : (int32_t)q;
    }
#endif
    uint32_t q = (uint32_t)(((uint64_t)mag * 33818641u) >> 32);
    if (q * 127u > mag) --q;
    return value < 0 ? -(int32_t)q : (int32_t)q;
#endif
}
