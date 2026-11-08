// Copyright 2016 Emilie Gillet.
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
// Naive snare drum model (two modulated oscillators + filtered noise).
// Uses a few magic numbers taken from the 909 schematics:
// - Ratio between the two modes of the drum set to 1.47.
// - Funky coupling between the two modes.
// - Noise coloration filters and envelope shapes for the snare.

#ifndef PLAITS_DSP_DRUMS_SYNTHETIC_SNARE_DRUM_H_
#define PLAITS_DSP_DRUMS_SYNTHETIC_SNARE_DRUM_H_

#include <algorithm>
#include <stdint.h>
#include <string.h>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/drums/kit_random.h"

#ifndef FM1_SNARE_DISTORTED_SINE_RECIP1
#define FM1_SNARE_DISTORTED_SINE_RECIP1 1
#endif
#if FM1_SNARE_DISTORTED_SINE_RECIP1
#include <float.h>
#endif

namespace plaits {

#if FM1_SNARE_DISTORTED_SINE_RECIP1
static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 &&
                  FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
              "snare reciprocal bit seed requires IEEE-754 binary32");
#endif

class SyntheticSnareDrum {
 public:
  SyntheticSnareDrum() { }
  ~SyntheticSnareDrum() { }

  PLAITS_COLD_INIT_ATTR void Init() {
    phase_[0] = 0.0f;
    phase_[1] = 0.0f;
    drum_amplitude_ = 0.0f;
    snare_amplitude_ = 0.0f;
    fm_ = 0.0f;
    hold_counter_ = 0;
    sustain_gain_ = 0.0f;
#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    decay_coefficients_cached_ = false;
    filter_frequency_cached_ = false;
#endif

    drum_lp_.Init();
    snare_hp_.Init();
    snare_lp_.Init();
  }
  
  inline float DistortedSine(float phase) {
    float triangle = (phase < 0.5f ? phase : 1.0f - phase) * 4.0f - 1.3f;
#if FM1_SNARE_DISTORTED_SINE_RECIP1
    float denominator = 1.0f + __builtin_fabsf(triangle);
    if (denominator <= 4.0f) {
      // The denominator is positive and bounded here. This IEEE-754 bit seed
      // plus one Newton step gives about 0.255% maximum reciprocal error over
      // [1, 4]. Keep the exact divide for unusual out-of-range phases.
      uint32_t bits;
      __asm__ volatile("" : "+m"(denominator) : : "memory");
      __builtin_memcpy(&bits, &denominator, sizeof(bits));
      bits = 0x7ef311c3u - bits;
      float reciprocal;
      __builtin_memcpy(&reciprocal, &bits, sizeof(reciprocal));
      __asm__ volatile("" : "+m"(reciprocal) : : "memory");
      reciprocal *= 2.0f - denominator * reciprocal;
      return (2.0f * triangle) * reciprocal;
    }
#endif
    return 2.0f * triangle / (1.0f + __builtin_fabsf(triangle));
  }
  
  void Render(
      bool sustain,
      bool trigger,
      float accent,
      float f0,
      float fm_amount,
      float decay,
      float snappy,
      float* PLAITS_RENDER_OUT_RESTRICT out,
      size_t size) {
    if (sustain) RenderImpl<true, false>(trigger, accent, f0, fm_amount, decay, snappy, out, size, nullptr);
    else RenderImpl<false, false>(trigger, accent, f0, fm_amount, decay, snappy, out, size, nullptr);
  }

#if FM1_PLAITS_KIT_LOCAL_RNG
  void Render(
      bool sustain, bool trigger, float accent, float f0, float fm_amount,
      float decay, float snappy, float* PLAITS_RENDER_OUT_RESTRICT out, size_t size,
      uint32_t* local_random_state) {
    if (sustain) RenderImpl<true, true>(trigger, accent, f0, fm_amount, decay, snappy, out, size, local_random_state);
    else RenderImpl<false, true>(trigger, accent, f0, fm_amount, decay, snappy, out, size, local_random_state);
  }
#endif

  template <bool sustain, bool local_random>
  void RenderImpl(
      bool trigger,
      float accent,
      float f0,
      float fm_amount,
      float decay,
      float snappy,
      float* PLAITS_RENDER_OUT_RESTRICT out,
      size_t size,
      uint32_t* local_random_state) {
    const float decay_xt = decay * (1.0f + decay * (decay - 1.0f));
    fm_amount *= fm_amount;
#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    const bool update_cached_coefficients = !decay_coefficients_cached_ ||
        cached_decay_ != decay ||
        cached_fm_amount_ != fm_amount || cached_snappy_ != snappy;
    if (update_cached_coefficients) {
      cached_decay_ = decay;
      cached_fm_amount_ = fm_amount;
      cached_snappy_ = snappy;
      cached_drum_decay_ratio_ = stmlib::SemitonesToRatio(
          -decay_xt * 72.0f - fm_amount * 12.0f + snappy * 7.0f);
      cached_snare_decay_ratio_ = stmlib::SemitonesToRatio(
          -decay * 60.0f - snappy * 7.0f);
    }
    const float drum_decay_ratio = cached_drum_decay_ratio_;
    const float snare_decay_ratio = cached_snare_decay_ratio_;
#else
    const float drum_decay_ratio = stmlib::SemitonesToRatio(
        -decay_xt * 72.0f - fm_amount * 12.0f + snappy * 7.0f);
    const float snare_decay_ratio = stmlib::SemitonesToRatio(
        -decay * 60.0f - snappy * 7.0f);
#endif
    const float drum_decay = 1.0f - 1.0f / (0.015f * kSampleRate) * \
        drum_decay_ratio;
    const float snare_decay = 1.0f - 1.0f / (0.01f * kSampleRate) * \
        snare_decay_ratio;
    const float fm_decay = 1.0f - 1.0f / (0.007f * kSampleRate);
    
    snappy = snappy * 1.1f - 0.05f;
    CONSTRAIN(snappy, 0.0f, 1.0f);
    
#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    if (update_cached_coefficients) {
      cached_drum_level_ = stmlib::Sqrt(1.0f - snappy);
      cached_snare_level_ = stmlib::Sqrt(snappy);
      decay_coefficients_cached_ = true;
    }
    const float drum_level = cached_drum_level_;
    const float snare_level = cached_snare_level_;
#else
    const float drum_level = stmlib::Sqrt(1.0f - snappy);
    const float snare_level = stmlib::Sqrt(snappy);
#endif
    
    const float snare_f_min = std::min(10.0f * f0, 0.5f);
    const float snare_f_max = std::min(35.0f * f0, 0.5f);

#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    if (!filter_frequency_cached_ || cached_filter_f0_ != f0) {
      cached_filter_f0_ = f0;
      snare_hp_.set_f<stmlib::FREQUENCY_FAST>(snare_f_min);
      drum_lp_.set_f<stmlib::FREQUENCY_FAST>(3.0f * f0);
      filter_frequency_cached_ = true;
    }
#else
    snare_hp_.set_f<stmlib::FREQUENCY_FAST>(snare_f_min);
#endif
    snare_lp_.set_f_q<stmlib::FREQUENCY_FAST>(snare_f_max,
        0.5f + 2.0f * snappy);
#if !FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    drum_lp_.set_f<stmlib::FREQUENCY_FAST>(3.0f * f0);
#endif
    
    if (trigger) {
      snare_amplitude_ = drum_amplitude_ = 0.3f + 0.7f * accent;
      fm_ = 1.0f;
      phase_[0] = phase_[1] = 0.0f;
      hold_counter_ = static_cast<int>((0.04f + decay * 0.03f) * kSampleRate);
    }
    
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_,
        accent * decay,
        size);
    float reset_noise_amount = (0.125f - f0) * 8.0f;
    CONSTRAIN(reset_noise_amount, 0.0f, 1.0f);
    reset_noise_amount *= reset_noise_amount;
    reset_noise_amount *= fm_amount;

    while (size--) {
      if (sustain) {
        snare_amplitude_ = sustain_gain.Next();
        drum_amplitude_ = snare_amplitude_;
        fm_ = 0.0f;
      } else {
        // Compute all D envelopes.
        // The envelope for the drum has a very long tail.
        // The envelope for the snare has a "hold" stage which lasts between
        // 40 and 70 ms
        drum_amplitude_ *= (drum_amplitude_ > 0.03f || !(size & 1))
            ? drum_decay
            : 1.0f;
        if (hold_counter_) {
          --hold_counter_;
        } else {
          snare_amplitude_ *= snare_decay;
        }
        fm_ *= fm_decay;
      }

      // The 909 circuit has a funny kind of oscillator coupling - the signal
      // leaving Q40's collector and resetting all oscillators allow some
      // intermodulation.
      float reset_noise = 0.0f;
      reset_noise += phase_[0] > 0.5f ? -1.0f : 1.0f;
      reset_noise += phase_[1] > 0.5f ? -1.0f : 1.0f;
      reset_noise *= reset_noise_amount * 0.025f;

      float f = f0 * (1.0f + fm_amount * (4.0f * fm_));
      phase_[0] += f;
      phase_[1] += f * 1.47f;
      if (reset_noise_amount > 0.1f) {
        if (phase_[0] >= 1.0f + reset_noise) {
          phase_[0] = 1.0f - phase_[0];
        }
        if (phase_[1] >= 1.0f + reset_noise) {
          phase_[1] = 1.0f - phase_[1];
        }
      } else {
        if (phase_[0] >= 1.0f) {
          phase_[0] -= 1.0f;
        }
        if (phase_[1] >= 1.0f) {
          phase_[1] -= 1.0f;
        }
      }
      
      float drum = -0.1f;
      drum += DistortedSine(phase_[0]) * 0.60f;
      drum += DistortedSine(phase_[1]) * 0.25f;
      drum *= drum_amplitude_ * drum_level;
      drum = drum_lp_.Process<stmlib::FILTER_MODE_LOW_PASS>(drum);
      
      float noise = KitRandomFloat<local_random>(local_random_state);
      float snare = snare_lp_.Process<stmlib::FILTER_MODE_LOW_PASS>(noise);
      snare = snare_hp_.Process<stmlib::FILTER_MODE_HIGH_PASS>(snare);
      snare = (snare + 0.1f) * (snare_amplitude_ + fm_) * snare_level;
      
      *out++ = snare + drum;  // It's a snare, it's a drum, it's a snare drum.
    }
  }

 private:
  float phase_[2];
  float drum_amplitude_;
  float snare_amplitude_;
  float fm_;
  float sustain_gain_;
  int hold_counter_;
  
  stmlib::OnePole drum_lp_;
  stmlib::OnePole snare_hp_;
  stmlib::Svf snare_lp_;
#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
  float cached_decay_;
  float cached_fm_amount_;
  float cached_snappy_;
  float cached_drum_decay_ratio_;
  float cached_snare_decay_ratio_;
  float cached_drum_level_;
  float cached_snare_level_;
  float cached_filter_f0_;
  bool decay_coefficients_cached_;
  bool filter_frequency_cached_;
#endif
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticSnareDrum);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_SYNTHETIC_SNARE_DRUM_H_
