/* SPDX-License-Identifier: MIT
 * Exact bounded interpolation arithmetic for the BRAIDS percussion adapter. */
#ifndef FIRMWARE_BRAIDS_BRAIDS_INTERP_MATH_H_
#define FIRMWARE_BRAIDS_BRAIDS_INTERP_MATH_H_

#include <stdint.h>

/* The caller's weighted sum is in [-32768 * 147, 32767 * 147]. For every
 * magnitude in that interval, ceil(2^32 / 147) gives the exact quotient after
 * a high-word multiply: the reciprocal excess is 59 and max_magnitude * 59
 * is below 2^32, so the product cannot cross the next quotient boundary. */
static inline int32_t braids_interp_div147(int32_t numerator) {
  uint32_t magnitude = numerator < 0
      ? 0u - (uint32_t)numerator
      : (uint32_t)numerator;
  uint32_t quotient = (uint32_t)(((uint64_t)magnitude * 29217465ull) >> 32);
  return numerator < 0 ? -(int32_t)quotient : (int32_t)quotient;
}

#endif
