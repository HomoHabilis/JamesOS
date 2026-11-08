// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef STMLIB_DSP_POSITIVE_RECIPROCAL_H_
#define STMLIB_DSP_POSITIVE_RECIPROCAL_H_

#include <stdint.h>

#ifndef STMLIB_FAST_POSITIVE_RECIPROCAL
#define STMLIB_FAST_POSITIVE_RECIPROCAL 0
#endif

#ifndef STMLIB_FAST_POSITIVE_RECIPROCAL_NEWTON_STEPS
#define STMLIB_FAST_POSITIVE_RECIPROCAL_NEWTON_STEPS 2
#endif

#if STMLIB_FAST_POSITIVE_RECIPROCAL && \
    STMLIB_FAST_POSITIVE_RECIPROCAL_NEWTON_STEPS != 1 && \
    STMLIB_FAST_POSITIVE_RECIPROCAL_NEWTON_STEPS != 2
#error "Positive reciprocal supports one or two Newton steps"
#endif

namespace stmlib {

// Fast reciprocal for the positive-normal coefficient ranges used by the
// filters. The bit seed is refined with Newton iteration; values outside the
// bounded range, including nonpositive, subnormal, infinite, and NaN inputs,
// retain ordinary IEEE division behavior.
inline float PositiveReciprocal(float x) {
#if STMLIB_FAST_POSITIVE_RECIPROCAL
  static_assert(sizeof(float) == sizeof(uint32_t),
                "positive reciprocal requires 32-bit binary floats");
  uint32_t input_bits;
  __builtin_memcpy(&input_bits, &x, sizeof(input_bits));
  // Positive IEEE-754 values are monotonically ordered by their bit pattern.
  // This range check also rejects negative, zero, subnormal, infinite, and NaN
  // inputs before the reciprocal seed is formed.
  if (input_bits >= 0x33800000u && input_bits <= 0x4b800000u) {
    uint32_t reciprocal_bits = 0x7ef311c3u - input_bits;
    float reciprocal;
    __builtin_memcpy(&reciprocal, &reciprocal_bits, sizeof(reciprocal));

    reciprocal *= 2.0f - x * reciprocal;
#if STMLIB_FAST_POSITIVE_RECIPROCAL_NEWTON_STEPS == 2
    reciprocal *= 2.0f - x * reciprocal;
#endif
    return reciprocal;
  }
#endif
  return 1.0f / x;
}

/* Two-Newton-step reciprocal for explicitly bounded hot paths. It remains
 * approximate, so callers must opt in and retain an exact divide fallback
 * outside this normal range. */
inline float PositiveReciprocalTwoStep(float x) {
  static_assert(sizeof(float) == sizeof(uint32_t),
                "one-step reciprocal requires 32-bit binary floats");
  uint32_t input_bits;
  __builtin_memcpy(&input_bits, &x, sizeof(input_bits));
  if (input_bits >= 0x33800000u && input_bits <= 0x4b800000u) {
    uint32_t reciprocal_bits = 0x7ef311c3u - input_bits;
    float reciprocal;
    __builtin_memcpy(&reciprocal, &reciprocal_bits, sizeof(reciprocal));
    reciprocal *= 2.0f - x * reciprocal;
    reciprocal *= 2.0f - x * reciprocal;
    return reciprocal;
  }
  return 1.0f / x;
}

}  // namespace stmlib

#endif  // STMLIB_DSP_POSITIVE_RECIPROCAL_H_
