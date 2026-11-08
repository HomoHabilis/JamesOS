/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_INSTRUMENT_MATH_H_
#define FM1_INSTRUMENT_MATH_H_

#include <stdint.h>

/* Exact quotient shortcuts for the bounded seven-bit control products used
 * by the audio wrappers. These keep truncation identical to /127. */
static inline uint32_t br_div127_lane_product_exact(uint32_t product) {
    /* Caller guarantees product <= 127 * 127. */
    return (product + 1u + (product >> 7)) >> 7;
}

static inline uint32_t br_scale_7bit_q15_exact(uint32_t value) {
    /* Caller guarantees value <= 127; 32767 = 127 * 258 + 1. */
    return value * 258u + (value == 127u);
}

/*
 * Exact signed truncation-toward-zero divisions for 32-bit audio intermediates.
 * These are candidates for targets where constant integer division is costly;
 * callers should benchmark before replacing ordinary division.
 *
 * The unsigned high-multiply quotient uses floor(2^32 / divisor), so its
 * estimate is at most one low. The remainder check performs the exact fixup.
 */
static inline uint32_t br_div127_unsigned_exact(uint32_t value) {
    uint32_t quotient = (uint32_t)(((uint64_t)value * 33818640u) >> 32);
    uint32_t remainder = value - quotient * 127u;
    return quotient + (remainder >= 127u);
}

static inline uint32_t br_div16129_unsigned_exact(uint32_t value) {
    uint32_t quotient = (uint32_t)(((uint64_t)value * 266288u) >> 32);
    uint32_t remainder = value - quotient * 16129u;
    return quotient + (remainder >= 16129u);
}

static inline int32_t br_div127_exact(int32_t value) {
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int32_t quotient = (int32_t)br_div127_unsigned_exact(magnitude);
    return value < 0 ? -quotient : quotient;
}

static inline int32_t br_div16129_exact(int32_t value) {
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int32_t quotient = (int32_t)br_div16129_unsigned_exact(magnitude);
    return value < 0 ? -quotient : quotient;
}

#endif
