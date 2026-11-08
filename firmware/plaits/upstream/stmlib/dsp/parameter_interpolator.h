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
// Linear interpolation of parameters in rendering loops.

#ifndef STMLIB_DSP_PARAMETER_INTERPOLATOR_H_
#define STMLIB_DSP_PARAMETER_INTERPOLATOR_H_

#include "stmlib/stmlib.h"

namespace stmlib {

#if defined(FM1_HARMONIC_RAM_TU) || \
    defined(FM1_HARMONIC_TRIPLE_RAM_TU) || \
    (defined(FM1_PLAITS_VCF_RENDER_RAM) && \
     FM1_PLAITS_VCF_RENDER_RAM && defined(FM1_PLAITS_VCF_TU))
#define STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE \
  __attribute__((always_inline)) inline
#else
#define STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE inline
#endif

class ParameterInterpolator {
 public:
  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE ParameterInterpolator() { }
  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE
  ParameterInterpolator(float* state, float new_value, size_t size) {
    Init(state, new_value, size);
  }

  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE
  ParameterInterpolator(float* state, float new_value, float step) {
    state_ = state;
    value_ = *state;
    increment_ = (new_value - *state) * step;
  }

  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE ~ParameterInterpolator() {
    *state_ = value_;
  }
  
  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE
  void Init(float* state, float new_value, size_t size) {
    state_ = state;
    value_ = *state;
    increment_ = (new_value - *state) / static_cast<float>(size);
  }

  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE
  void InitWithStep(float* state, float new_value, float step) {
    state_ = state;
    value_ = *state;
    increment_ = (new_value - *state) * step;
  }

  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE float Next() {
    value_ += increment_;
    return value_;
  }

  STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE float subsample(float t) {
    return value_ + increment_ * t;
  }
  
 private:
  float* state_;
  float value_;
  float increment_;
};

#undef STMLIB_PARAMETER_INTERPOLATOR_VCF_INLINE

}  // namespace stmlib

#endif  // STMLIB_DSP_PARAMETER_INTERPOLATOR_H_
