// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Limiter.

#ifndef STMLIB_DSP_LIMITER_H_
#define STMLIB_DSP_LIMITER_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "plaits_config.h"

namespace stmlib {

inline float LimiterReciprocal(float x) {
#if FM1_PLAITS_FAST_LIMITER_RECIPROCAL
  static_assert(sizeof(float) == sizeof(uint32_t),
                "fast limiter reciprocal requires 32-bit float");
  uint32_t input_bits;
  __builtin_memcpy(&input_bits, &x, sizeof(input_bits));
  /* x is peak_ and the approximation is used only when x > 1.0f. Keep an
   * exact fallback for unexpected values and preserve IEEE edge handling. */
  if (input_bits >= 0x3f800000u && input_bits <= 0x4b800000u) {
    const uint32_t reciprocal_bits = 0x7ef311c3u - input_bits;
    float reciprocal;
    __builtin_memcpy(&reciprocal, &reciprocal_bits, sizeof(reciprocal));
    reciprocal *= 2.0f - x * reciprocal;
    return reciprocal;
  }
#endif
  return 1.0f / x;
}

class Limiter {
 public:
  Limiter() { }
  ~Limiter() { }

  void Init() {
    peak_ = 0.5f;
  }

  void Process(float pre_gain, float* in_out, size_t size) {
    while (size--) {
      float s = *in_out * pre_gain;
      SLOPE(peak_, __builtin_fabsf(s), 0.05f, 0.00002f);
      float gain = (peak_ <= 1.0f ? 1.0f : LimiterReciprocal(peak_));
      *in_out++ = s * gain * 0.8f;
    }
  }

 private:
  float peak_;

  DISALLOW_COPY_AND_ASSIGN(Limiter);
};

}  // namespace stmlib

#endif  // STMLIB_DSP_LIMITER_H_
