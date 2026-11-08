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
// Harmonic oscillator based on Chebyshev polynomials.
// Works well for a small number of harmonics. For the higher order harmonics,
// we need to reinitialize the recurrence by computing two high harmonics.

#ifndef PLAITS_DSP_OSCILLATOR_HARMONIC_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_HARMONIC_OSCILLATOR_H_

#ifndef FM1_HARMONIC_SHARED_RECURRENCE
#define FM1_HARMONIC_SHARED_RECURRENCE 1
#endif

#ifndef FM1_HARMONIC_TRIPLE_RECURRENCE
#define FM1_HARMONIC_TRIPLE_RECURRENCE 1
#endif

#ifndef FM1_HARMONIC_TEMPORAL_RECURRENCE
#define FM1_HARMONIC_TEMPORAL_RECURRENCE 1
#endif

#ifndef FM1_HARMONIC_TEMPORAL_BLOCK_SIZE
#define FM1_HARMONIC_TEMPORAL_BLOCK_SIZE 12
#endif

#ifndef FM1_HARMONIC_TEMPORAL_RAM
#define FM1_HARMONIC_TEMPORAL_RAM 1
#endif

#ifndef FM1_HARMONIC_TRIPLE_RAM
#define FM1_HARMONIC_TRIPLE_RAM 0
#endif
#if FM1_HARMONIC_TRIPLE_RAM != 0 && FM1_HARMONIC_TRIPLE_RAM != 1
#error "FM1_HARMONIC_TRIPLE_RAM must be 0 or 1"
#endif

/* Keep Temporal math selection independent from executable-bank placement.
 * When the larger Triple kernel is selected for RAM, Temporal stays on XIP. */
#if FM1_HARMONIC_TEMPORAL_RAM && !FM1_HARMONIC_TRIPLE_RAM
#define FM1_HARMONIC_TEMPORAL_RAM_PLACEMENT 1
#else
#define FM1_HARMONIC_TEMPORAL_RAM_PLACEMENT 0
#endif

#ifndef FM1_HARMONIC_TEMPORAL_RECURSIVE_COEFFICIENTS
#define FM1_HARMONIC_TEMPORAL_RECURSIVE_COEFFICIENTS 0
#endif

#ifndef FM1_HARMONIC_TEMPORAL_UNBRANCHED_PARTIAL_LOOP
#define FM1_HARMONIC_TEMPORAL_UNBRANCHED_PARTIAL_LOOP 0
#endif
#ifndef FM1_HARMONIC_CLENSHAW
#define FM1_HARMONIC_CLENSHAW 1
#endif
#ifndef FM1_HARMONIC_CLENSHAW_SEPARATE_BANKS
#define FM1_HARMONIC_CLENSHAW_SEPARATE_BANKS 1
#endif
#ifndef FM1_HARMONIC_CLENSHAW_SHARED_STEP
#define FM1_HARMONIC_CLENSHAW_SHARED_STEP 1
#endif
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP != 0 && \
    FM1_HARMONIC_CLENSHAW_SHARED_STEP != 1
#error "FM1_HARMONIC_CLENSHAW_SHARED_STEP must be 0 or 1"
#endif
#ifndef FM1_HARMONIC_CLENSHAW_SHARED_STEP_KEEP_FREQ_DIV
#define FM1_HARMONIC_CLENSHAW_SHARED_STEP_KEEP_FREQ_DIV 1
#endif
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP_KEEP_FREQ_DIV != 0 && \
    FM1_HARMONIC_CLENSHAW_SHARED_STEP_KEEP_FREQ_DIV != 1
#error "FM1_HARMONIC_CLENSHAW_SHARED_STEP_KEEP_FREQ_DIV must be 0 or 1"
#endif
#ifndef FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
#define FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH 1
#endif
#ifndef FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON
#define FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON 1.0e-7f
#endif
#ifndef FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
#define FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH 1
#endif
#ifndef FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON
#define FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON 1.0e-6f
#endif
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH && \
    !FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
#error "Settled amplitude path requires stable amplitude fastpath"
#endif
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH && \
    (!FM1_HARMONIC_CLENSHAW || !FM1_HARMONIC_CLENSHAW_SEPARATE_BANKS)
#error "Stable amplitude fastpath requires separate-bank Clenshaw"
#endif

#if FM1_HARMONIC_TEMPORAL_RAM_PLACEMENT && defined(FM1_DEVICE)
#define FM1_HARMONIC_TEMPORAL_RAM_CODE \
  __attribute__((section(".ram_audio.harmonic")))
#else
#define FM1_HARMONIC_TEMPORAL_RAM_CODE
#endif

#if FM1_HARMONIC_TRIPLE_RAM && defined(FM1_DEVICE)
#define FM1_HARMONIC_TRIPLE_RAM_CODE \
  __attribute__((section(".ram_audio.harmonic"), noinline, used))
#else
#define FM1_HARMONIC_TRIPLE_RAM_CODE
#endif

#if FM1_HARMONIC_TEMPORAL_RECURRENCE && \
    FM1_HARMONIC_TEMPORAL_BLOCK_SIZE != 12 && \
    FM1_HARMONIC_TEMPORAL_BLOCK_SIZE != 24
#error "Temporal harmonic recurrence supports 12- or 24-frame blocks"
#endif

#if FM1_HARMONIC_SHARED_RECURRENCE || FM1_HARMONIC_TRIPLE_RECURRENCE
#include <stdint.h>
#endif

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

#if defined(FM1_HARMONIC_RAM_TU) || defined(FM1_HARMONIC_TRIPLE_RAM_TU)
__attribute__((always_inline))
#endif
inline float HarmonicAmplitudeAttenuation(float normalized_frequency) {
#if PLAITS_INTERNAL_SAMPLE_RATE == 48000
  return 1.0f - normalized_frequency * 2.0f;
#else
  // Keep the 48 kHz spectral tilt at the same physical frequency while still
  // hard-clipping partial oscillators at the current source Nyquist.
  if (normalized_frequency >= 0.5f) {
    return 0.0f;
  }
  return 1.0f - (normalized_frequency * 2.0f) *
      (kSampleRate / 48000.0f);
#endif
}

template<int num_harmonics>
class HarmonicOscillator {
 public:
  HarmonicOscillator() { }
  ~HarmonicOscillator() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
    stable_target_valid_ = false;
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
    stable_amplitude_settled_ = false;
#endif
#endif
    for (int i = 0; i < num_harmonics; ++i) {
      amplitude_[i] = 0.0f;
    }
  }
  
  template<int first_harmonic_index>
  void Render(
      float frequency,
      const float* amplitudes,
      float* out,
      size_t size) {
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
    stable_target_valid_ = false;
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
    stable_amplitude_settled_ = false;
#endif
#endif
#if FM1_HARMONIC_TEMPORAL_RECURRENCE
#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
    if (first_harmonic_index != 1) {
#if FM1_HARMONIC_TEMPORAL_RAM_PLACEMENT && defined(FM1_DEVICE)
      typedef bool (HarmonicOscillator<num_harmonics>::*TemporalRenderer)(
          float, const float*, float*, size_t);
      TemporalRenderer volatile temporal_renderer =
          &HarmonicOscillator<num_harmonics>::template
              RenderTemporal<first_harmonic_index>;
      if ((this->*temporal_renderer)(frequency, amplitudes, out, size)) {
        return;
      }
#else
      if (RenderTemporal<first_harmonic_index>(frequency, amplitudes, out, size)) {
        return;
      }
#endif
    }
#else
    if (RenderTemporal<first_harmonic_index>(frequency, amplitudes, out, size)) {
      return;
    }
#endif
#endif

    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    
    stmlib::ParameterInterpolator am[num_harmonics];
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    
    for (int i = 0; i < num_harmonics; ++i) {
      float f = frequency * static_cast<float>(first_harmonic_index + i);
      if (f >= 0.5f) {
        f = 0.5f;
      }
      am[i].Init(
          &amplitude_[i], amplitudes[i] * HarmonicAmplitudeAttenuation(f), size);
    }

    while (size--) {
      phase_ += fm.Next();
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
      const float two_x = 2.0f * SineNoWrap(phase_);
      float previous, current;
      if (first_harmonic_index == 1) {
        previous = 1.0f;
        current = two_x * 0.5f;
      } else {
        const float k = first_harmonic_index;
        previous = Sine(phase_ * (k - 1.0f) + 0.25f);
        current = Sine(phase_ * k);
      }
      
      float sum = 0.0f;
      for (int i = 0; i < num_harmonics; ++i) {
        sum += am[i].Next() * current;
        float temp = current;
        current = two_x * current - previous;
        previous = temp;
      }
      if (first_harmonic_index == 1) {
        *out++ = sum;
      } else {
        *out++ += sum;
      }
    }
  }

#if FM1_HARMONIC_SHARED_RECURRENCE
  bool HasSamePhaseAndFrequency(
      const HarmonicOscillator<num_harmonics>& other) const {
    return SameBits(phase_, other.phase_) &&
        SameBits(frequency_, other.frequency_);
  }

  // Render two same-frequency banks while sharing the sine lookup and
  // Chebyshev recurrence. If state diverged, return without touching either
  // oscillator or output so the caller can use the original render order.
  template<int first_harmonic_index>
  bool RenderPair(
      HarmonicOscillator<num_harmonics>& other,
      float frequency,
      const float* amplitudes,
      float* out,
      const float* other_amplitudes,
      float* other_out,
      size_t size) {
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
    stable_target_valid_ = false;
    other.stable_target_valid_ = false;
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
    stable_amplitude_settled_ = false;
    other.stable_amplitude_settled_ = false;
#endif
#endif
    if (out == other_out ||
        !SameBits(phase_, other.phase_) ||
        !SameBits(frequency_, other.frequency_)) {
      return false;
    }

    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }

    {
      struct PairedAmplitudeIncrement {
        float value;
        float other_value;
      } increment[num_harmonics];
      stmlib::ParameterInterpolator fm(&frequency_, frequency, size);

      for (int i = 0; i < num_harmonics; ++i) {
        float f = frequency * static_cast<float>(first_harmonic_index + i);
        if (f >= 0.5f) {
          f = 0.5f;
        }
        const float attenuation = HarmonicAmplitudeAttenuation(f);
        increment[i].value =
            (amplitudes[i] * attenuation - amplitude_[i]) /
            static_cast<float>(size);
        increment[i].other_value =
            (other_amplitudes[i] * attenuation - other.amplitude_[i]) /
            static_cast<float>(size);
      }

      while (size--) {
        phase_ += fm.Next();
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        const float two_x = 2.0f * SineNoWrap(phase_);
        float previous, current;
        if (first_harmonic_index == 1) {
          previous = 1.0f;
          current = two_x * 0.5f;
        } else {
          const float k = first_harmonic_index;
          previous = Sine(phase_ * (k - 1.0f) + 0.25f);
          current = Sine(phase_ * k);
        }

        float sum = 0.0f;
        float other_sum = 0.0f;
        for (int i = 0; i < num_harmonics; ++i) {
          amplitude_[i] += increment[i].value;
          sum += amplitude_[i] * current;
          other.amplitude_[i] += increment[i].other_value;
          other_sum += other.amplitude_[i] * current;
          const float temp = current;
          current = two_x * current - previous;
          previous = temp;
        }
        if (first_harmonic_index == 1) {
          *out++ = sum;
          *other_out++ = other_sum;
        } else {
          *out++ += sum;
          *other_out++ += other_sum;
        }
      }
    }

    other.phase_ = phase_;
    other.frequency_ = frequency_;
    return true;
  }
#if FM1_HARMONIC_SHARED_RECURRENCE || FM1_HARMONIC_TRIPLE_RECURRENCE
  // Render the 1..N bank, its 1..N companion and the following N..2N bank
  // together. Share the base sine and phase increment, while retaining direct
  // high-bank seeds so the recurrence stays bit-identical to separate renders.
  // If oscillator state is not aligned, leave it untouched for fallback.
  template<int first_harmonic_index>
  FM1_HARMONIC_TRIPLE_RAM_CODE
  bool RenderTriple(
      HarmonicOscillator<num_harmonics>& high,
      HarmonicOscillator<num_harmonics>& companion,
      float frequency,
      const float* amplitudes,
      float* out,
      const float* high_amplitudes,
      const float* companion_amplitudes,
      float* companion_out,
      size_t size) {
    if (out == companion_out || size == 0 ||
        !SameBits(phase_, high.phase_) ||
        !SameBits(phase_, companion.phase_) ||
        !SameBits(frequency_, high.frequency_) ||
        !SameBits(frequency_, companion.frequency_)) {
      return false;
    }

    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }

    {
#if FM1_HARMONIC_CLENSHAW
      // Evaluate the contiguous 24-term MAIN series and 12-term AUX series
      // with Clenshaw's recurrence. This path is used only after the three
      // oscillators' phase and frequency states have been proven bit-aligned
      // above. It advances every amplitude state exactly once per sample and
      // retains all coefficients and their original interpolation.
      if (first_harmonic_index != 1) {
        return false;
      }
      struct TripleAmplitudeIncrement {
        float low;
        float high;
        float companion;
      } increment[num_harmonics];
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
      bool stable_amplitudes = false;
      bool cached_targets_match = stable_target_valid_ &&
          high.stable_target_valid_ && companion.stable_target_valid_;
#else
      bool stable_amplitudes = stable_target_valid_ &&
          high.stable_target_valid_ && companion.stable_target_valid_ &&
          FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON > 0.0f &&
          FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON <= 1.0e-3f;
#endif
#endif
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP
      const float interpolation_step = 1.0f / static_cast<float>(size);
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP_KEEP_FREQ_DIV
      stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
#else
      stmlib::ParameterInterpolator fm;
      fm.InitWithStep(&frequency_, frequency, interpolation_step);
#endif
#else
      stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
#endif

      for (int i = 0; i < num_harmonics; ++i) {
        float low_f = frequency * static_cast<float>(first_harmonic_index + i);
        if (low_f >= 0.5f) {
          low_f = 0.5f;
        }
        float high_f = frequency * static_cast<float>(
            first_harmonic_index + num_harmonics + i);
        if (high_f >= 0.5f) {
          high_f = 0.5f;
        }
        const float low_attenuation = HarmonicAmplitudeAttenuation(low_f);
        const float high_attenuation = HarmonicAmplitudeAttenuation(high_f);
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH && \
    FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
        const float low_target = amplitudes[i] * low_attenuation;
        const float high_target = high_amplitudes[i] * high_attenuation;
        const float companion_target =
            companion_amplitudes[i] * low_attenuation;
        if (cached_targets_match &&
            (!SameBits(stable_target_[i], low_target) ||
             !SameBits(high.stable_target_[i], high_target) ||
             !SameBits(companion.stable_target_[i], companion_target))) {
          cached_targets_match = false;
        }
        // The common settled path only compares targets. It does not stage
        // them or write cache rows; misses recompute targets in the slow path.
#elif FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
        const float low_target = amplitudes[i] * low_attenuation;
        const float high_target = high_amplitudes[i] * high_attenuation;
        const float companion_target =
            companion_amplitudes[i] * low_attenuation;
        const float low_error = low_target - amplitude_[i];
        const float high_error = high_target - high.amplitude_[i];
        const float companion_error =
            companion_target - companion.amplitude_[i];
        const float low_magnitude = low_target < 0.0f ?
            -low_target : low_target;
        const float high_magnitude = high_target < 0.0f ?
            -high_target : high_target;
        const float companion_magnitude = companion_target < 0.0f ?
            -companion_target : companion_target;
        const float low_bound = FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON *
            (low_magnitude > 1.0f ? low_magnitude : 1.0f);
        const float high_bound = FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON *
            (high_magnitude > 1.0f ? high_magnitude : 1.0f);
        const float companion_bound =
            FM1_HARMONIC_STABLE_AMPLITUDE_EPSILON *
            (companion_magnitude > 1.0f ? companion_magnitude : 1.0f);
        if (stable_amplitudes &&
            (!(low_magnitude <= 3.0e38f) ||
             !(high_magnitude <= 3.0e38f) ||
             !(companion_magnitude <= 3.0e38f) ||
             !SameBits(stable_target_[i], low_target) ||
             !SameBits(high.stable_target_[i], high_target) ||
             !SameBits(companion.stable_target_[i], companion_target) ||
             !(low_error >= -low_bound && low_error <= low_bound) ||
             !(high_error >= -high_bound && high_error <= high_bound) ||
             !(companion_error >= -companion_bound &&
               companion_error <= companion_bound))) {
          stable_amplitudes = false;
        }
        stable_target_[i] = low_target;
        high.stable_target_[i] = high_target;
        companion.stable_target_[i] = companion_target;
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP
        increment[i].low =
            (low_target - amplitude_[i]) * interpolation_step;
        increment[i].high =
            (high_target - high.amplitude_[i]) * interpolation_step;
        increment[i].companion =
            (companion_target - companion.amplitude_[i]) * interpolation_step;
#else
        increment[i].low =
            (low_target - amplitude_[i]) / static_cast<float>(size);
        increment[i].high =
            (high_target - high.amplitude_[i]) / static_cast<float>(size);
        increment[i].companion =
            (companion_target - companion.amplitude_[i]) /
            static_cast<float>(size);
#endif
#else
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP
        increment[i].low =
            (amplitudes[i] * low_attenuation - amplitude_[i]) *
            interpolation_step;
        increment[i].high =
            (high_amplitudes[i] * high_attenuation - high.amplitude_[i]) *
            interpolation_step;
        increment[i].companion =
            (companion_amplitudes[i] * low_attenuation -
             companion.amplitude_[i]) * interpolation_step;
#else
        increment[i].low =
            (amplitudes[i] * low_attenuation - amplitude_[i]) /
            static_cast<float>(size);
        increment[i].high =
            (high_amplitudes[i] * high_attenuation - high.amplitude_[i]) /
            static_cast<float>(size);
        increment[i].companion =
            (companion_amplitudes[i] * low_attenuation -
             companion.amplitude_[i]) / static_cast<float>(size);
#endif
#endif
      }

#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
      bool settled_shortcut = false;
      if (cached_targets_match && stable_amplitude_settled_ &&
          high.stable_amplitude_settled_ &&
          companion.stable_amplitude_settled_ &&
          FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON > 0.0f &&
          FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON <= 1.0e-3f) {
        // No error tests, target-cache writes, or increments on stable blocks.
        stable_amplitudes = true;
        settled_shortcut = true;
      } else {
        // Target changes always take one ordinary interpolation block before
        // the state can settle again.
        bool within_settle_bound = cached_targets_match &&
            FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON > 0.0f &&
            FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON <= 1.0e-3f;
        for (int i = 0; i < num_harmonics; ++i) {
          float low_f = frequency *
              static_cast<float>(first_harmonic_index + i);
          if (low_f >= 0.5f) low_f = 0.5f;
          float high_f = frequency * static_cast<float>(
              first_harmonic_index + num_harmonics + i);
          if (high_f >= 0.5f) high_f = 0.5f;
          const float low_attenuation = HarmonicAmplitudeAttenuation(low_f);
          const float high_attenuation = HarmonicAmplitudeAttenuation(high_f);
          const float low_target = amplitudes[i] * low_attenuation;
          const float high_target = high_amplitudes[i] * high_attenuation;
          const float companion_target =
              companion_amplitudes[i] * low_attenuation;
          const float low_error = low_target - amplitude_[i];
          const float high_error = high_target - high.amplitude_[i];
          const float companion_error =
              companion_target - companion.amplitude_[i];
          const float low_magnitude = low_target < 0.0f ?
              -low_target : low_target;
          const float high_magnitude = high_target < 0.0f ?
              -high_target : high_target;
          const float companion_magnitude = companion_target < 0.0f ?
              -companion_target : companion_target;
          const float low_bound = FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON *
              (low_magnitude > 1.0f ? low_magnitude : 1.0f);
          const float high_bound = FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON *
              (high_magnitude > 1.0f ? high_magnitude : 1.0f);
          const float companion_bound =
              FM1_HARMONIC_SETTLED_AMPLITUDE_EPSILON *
              (companion_magnitude > 1.0f ? companion_magnitude : 1.0f);
          if (!(low_magnitude <= 3.0e38f) ||
              !(high_magnitude <= 3.0e38f) ||
              !(companion_magnitude <= 3.0e38f) ||
              !(low_error >= -low_bound && low_error <= low_bound) ||
              !(high_error >= -high_bound && high_error <= high_bound) ||
              !(companion_error >= -companion_bound &&
                companion_error <= companion_bound)) {
            within_settle_bound = false;
          }
#if FM1_HARMONIC_CLENSHAW_SHARED_STEP
          increment[i].low =
              (low_target - amplitude_[i]) * interpolation_step;
          increment[i].high =
              (high_target - high.amplitude_[i]) * interpolation_step;
          increment[i].companion =
              (companion_target - companion.amplitude_[i]) *
              interpolation_step;
#else
          increment[i].low = (low_target - amplitude_[i]) /
              static_cast<float>(size);
          increment[i].high = (high_target - high.amplitude_[i]) /
              static_cast<float>(size);
          increment[i].companion = (companion_target -
              companion.amplitude_[i]) / static_cast<float>(size);
#endif
          if (!cached_targets_match) {
            stable_target_[i] = low_target;
            high.stable_target_[i] = high_target;
            companion.stable_target_[i] = companion_target;
          }
        }
        stable_amplitudes = within_settle_bound;
        if (stable_amplitudes) {
          for (int i = 0; i < num_harmonics; ++i) {
            amplitude_[i] = stable_target_[i];
            high.amplitude_[i] = high.stable_target_[i];
            companion.amplitude_[i] = companion.stable_target_[i];
          }
        }
        stable_amplitude_settled_ = stable_amplitudes;
        high.stable_amplitude_settled_ = stable_amplitudes;
        companion.stable_amplitude_settled_ = stable_amplitudes;
      }
      stable_target_valid_ = true;
      high.stable_target_valid_ = true;
      companion.stable_target_valid_ = true;
#else
      stable_target_valid_ = true;
      high.stable_target_valid_ = true;
      companion.stable_target_valid_ = true;
      if (stable_amplitudes) {
        // Settle the tiny remaining interpolation error once, then leave these
        // coefficients unchanged throughout the sample loop below.
        for (int i = 0; i < num_harmonics; ++i) {
          amplitude_[i] = stable_target_[i];
          high.amplitude_[i] = high.stable_target_[i];
          companion.amplitude_[i] = companion.stable_target_[i];
        }
      }
#endif
#endif

#if FM1_HARMONIC_CLENSHAW_SEPARATE_BANKS
      // Keep low MAIN, upper MAIN, and AUX as independent 12-term chains.
      // The upper-bank final form uses its original T_12/T_13 seeds; this
      // limits recurrence dependency depth while retaining all partials.
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
      if (stable_amplitudes) {
        while (size--) {
          phase_ += fm.Next();
          if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
          }
          const float x = SineNoWrap(phase_);
          const float two_x = 2.0f * x;
          const float high_previous = Sine(
              phase_ * static_cast<float>(first_harmonic_index +
                                          num_harmonics - 1) + 0.25f);
          const float high_current = Sine(
              phase_ * static_cast<float>(first_harmonic_index +
                                          num_harmonics));
          float low_b1 = 0.0f;
          float low_b2 = 0.0f;
          float high_b1 = 0.0f;
          float high_b2 = 0.0f;
          float companion_b1 = 0.0f;
          float companion_b2 = 0.0f;
          for (int i = num_harmonics - 1; i >= 0; --i) {
            const float low_b = amplitude_[i] + two_x * low_b1 - low_b2;
            const float high_b =
                high.amplitude_[i] + two_x * high_b1 - high_b2;
            const float companion_b = companion.amplitude_[i] +
                two_x * companion_b1 - companion_b2;
            low_b2 = low_b1;
            low_b1 = low_b;
            high_b2 = high_b1;
            high_b1 = high_b;
            companion_b2 = companion_b1;
            companion_b1 = companion_b;
          }
          *out++ = (x * low_b1 - low_b2) +
              (high_current * high_b1 - high_previous * high_b2);
          *companion_out++ = x * companion_b1 - companion_b2;
        }
      } else {
#endif
      while (size--) {
        phase_ += fm.Next();
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        const float x = SineNoWrap(phase_);
        const float two_x = 2.0f * x;
        const float high_previous = Sine(
            phase_ * static_cast<float>(first_harmonic_index +
                                        num_harmonics - 1) + 0.25f);
        const float high_current = Sine(
            phase_ * static_cast<float>(first_harmonic_index +
                                        num_harmonics));
        float low_b1 = 0.0f;
        float low_b2 = 0.0f;
        float high_b1 = 0.0f;
        float high_b2 = 0.0f;
        float companion_b1 = 0.0f;
        float companion_b2 = 0.0f;
        for (int i = num_harmonics - 1; i >= 0; --i) {
          amplitude_[i] += increment[i].low;
          high.amplitude_[i] += increment[i].high;
          companion.amplitude_[i] += increment[i].companion;

          const float low_b =
              amplitude_[i] + two_x * low_b1 - low_b2;
          const float high_b =
              high.amplitude_[i] + two_x * high_b1 - high_b2;
          const float companion_b =
              companion.amplitude_[i] + two_x * companion_b1 - companion_b2;
          low_b2 = low_b1;
          low_b1 = low_b;
          high_b2 = high_b1;
          high_b1 = high_b;
          companion_b2 = companion_b1;
          companion_b1 = companion_b;
        }
        *out++ = (x * low_b1 - low_b2) +
            (high_current * high_b1 - high_previous * high_b2);
        *companion_out++ = x * companion_b1 - companion_b2;
      }
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
      }
#endif
#else
      while (size--) {
        phase_ += fm.Next();
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        const float x = SineNoWrap(phase_);
        const float two_x = 2.0f * x;

        // MAIN covers T_1..T_(2N): first consume the upper coefficient bank
        // T_(N+1)..T_(2N), then continue the same recurrence through T_N..T_1.
        // AUX is an independent T_1..T_N series and can be interleaved with
        // the upper-bank part of MAIN.
        float main_b1 = 0.0f;
        float main_b2 = 0.0f;
        float companion_b1 = 0.0f;
        float companion_b2 = 0.0f;
        for (int i = num_harmonics - 1; i >= 0; --i) {
          high.amplitude_[i] += increment[i].high;
          companion.amplitude_[i] += increment[i].companion;

          const float main_b =
              high.amplitude_[i] + two_x * main_b1 - main_b2;
          const float companion_b =
              companion.amplitude_[i] + two_x * companion_b1 - companion_b2;
          main_b2 = main_b1;
          main_b1 = main_b;
          companion_b2 = companion_b1;
          companion_b1 = companion_b;
        }

        for (int i = num_harmonics - 1; i >= 0; --i) {
          amplitude_[i] += increment[i].low;
          const float main_b =
              amplitude_[i] + two_x * main_b1 - main_b2;
          main_b2 = main_b1;
          main_b1 = main_b;
        }
        *out++ = x * main_b1 - main_b2;
        *companion_out++ = x * companion_b1 - companion_b2;
      }
#endif
#endif
#else
      struct TripleAmplitudeIncrement {
        float low;
        float high;
        float companion;
      } increment[num_harmonics];
      stmlib::ParameterInterpolator fm(&frequency_, frequency, size);

      for (int i = 0; i < num_harmonics; ++i) {
        float low_f = frequency * static_cast<float>(first_harmonic_index + i);
        if (low_f >= 0.5f) {
          low_f = 0.5f;
        }
        float high_f = frequency * static_cast<float>(
            first_harmonic_index + num_harmonics + i);
        if (high_f >= 0.5f) {
          high_f = 0.5f;
        }
        const float low_attenuation =
            HarmonicAmplitudeAttenuation(low_f);
        const float high_attenuation =
            HarmonicAmplitudeAttenuation(high_f);
        increment[i].low =
            (amplitudes[i] * low_attenuation - amplitude_[i]) /
            static_cast<float>(size);
        increment[i].high =
            (high_amplitudes[i] * high_attenuation - high.amplitude_[i]) /
            static_cast<float>(size);
        increment[i].companion =
            (companion_amplitudes[i] * low_attenuation -
             companion.amplitude_[i]) / static_cast<float>(size);
      }

      while (size--) {
        phase_ += fm.Next();
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        const float two_x = 2.0f * SineNoWrap(phase_);
        float previous = 1.0f;
        float current = two_x * 0.5f;
        float sum = 0.0f;
        float companion_sum = 0.0f;
        for (int i = 0; i < num_harmonics; ++i) {
          const float temp = current;
          amplitude_[i] += increment[i].low;
          sum += amplitude_[i] * current;
          companion.amplitude_[i] += increment[i].companion;
          companion_sum += companion.amplitude_[i] * current;
          current = two_x * current - previous;
          previous = temp;
        }

        previous = Sine(phase_ * static_cast<float>(
            first_harmonic_index + num_harmonics - 1) + 0.25f);
        current = Sine(phase_ * static_cast<float>(
            first_harmonic_index + num_harmonics));
        float high_sum = 0.0f;
        for (int i = 0; i < num_harmonics; ++i) {
          const float temp = current;
          high.amplitude_[i] += increment[i].high;
          high_sum += high.amplitude_[i] * current;
          current = two_x * current - previous;
          previous = temp;
        }
        *out++ = sum;
        *companion_out++ = companion_sum;
        out[-1] += high_sum;
      }
#endif
    }

    high.phase_ = companion.phase_ = phase_;
    high.frequency_ = companion.frequency_ = frequency_;
    return true;
  }
#endif

 private:
#if FM1_HARMONIC_TEMPORAL_RECURRENCE
#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
  __attribute__((always_inline))
#endif
  static inline float TemporalAmplitudeAttenuation(float normalized_frequency) {
#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
#if PLAITS_INTERNAL_SAMPLE_RATE == 48000
    return 1.0f - normalized_frequency * 2.0f;
#else
    if (normalized_frequency >= 0.5f) {
      return 0.0f;
    }
    return 1.0f - (normalized_frequency * 2.0f) *
        (kSampleRate / 48000.0f);
#endif
#else
    return HarmonicAmplitudeAttenuation(normalized_frequency);
#endif
  }

#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
  __attribute__((always_inline))
#endif
  static inline float TemporalSine(float phase) {
#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
    float index = phase - static_cast<float>(static_cast<int32_t>(phase));
    index *= kSineLUTSize;
    const int32_t index_integral = static_cast<int32_t>(index);
    const float index_fractional =
        index - static_cast<float>(index_integral);
    const float a = lut_sine[index_integral];
    const float b = lut_sine[index_integral + 1];
    return a + (b - a) * index_fractional;
#else
    return Sine(phase);
#endif
  }

#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
  __attribute__((always_inline))
#endif
  static inline float TemporalSineNoWrap(float phase) {
#if FM1_HARMONIC_TEMPORAL_RAM && defined(FM1_DEVICE)
    float index = phase * kSineLUTSize;
    const int32_t index_integral = static_cast<int32_t>(index);
    const float index_fractional =
        index - static_cast<float>(index_integral);
    const float a = lut_sine[index_integral];
    const float b = lut_sine[index_integral + 1];
    return a + (b - a) * index_fractional;
#else
    return SineNoWrap(phase);
#endif
  }

  // Keep this opt-in placement local to the temporal renderer. It has no
  // external DSP callees and reads the shared sine table from RAM.
  template<int first_harmonic_index>
#if FM1_HARMONIC_TEMPORAL_RAM_PLACEMENT && defined(FM1_DEVICE)
  FM1_HARMONIC_TEMPORAL_RAM_CODE
#endif
  bool RenderTemporal(
      float frequency,
      const float* amplitudes,
      float* out,
      size_t size) {
    // The first bank already needs only one sine lookup per sample. Restrict
    // the experiment to the higher bank, where block seeding amortizes its
    // direct starting-phase lookups and avoids the per-sample high-order seeds.
    if (size == 0 || first_harmonic_index == 1) {
      return false;
    }
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }

    // A blockwise temporal recurrence assumes a constant phase increment
    // inside each sub-block. Bound the worst-case phase curvature at the
    // highest retained partial to 0.02 cycles. Large pitch changes use the
    // exact source recurrence below, preserving its note-transition behavior.
    const float frequency_step = (frequency - frequency_) /
        static_cast<float>(size);
    const float block = static_cast<float>(FM1_HARMONIC_TEMPORAL_BLOCK_SIZE);
    const float highest_partial = static_cast<float>(
        first_harmonic_index + num_harmonics - 1);
    if (__builtin_fabsf(frequency_step) * block * block * highest_partial >
        0.16f) {
      return false;
    }

    float amplitude_increment[num_harmonics];
    for (int i = 0; i < num_harmonics; ++i) {
      float f = frequency * static_cast<float>(first_harmonic_index + i);
      if (f >= 0.5f) {
        f = 0.5f;
      }
      amplitude_increment[i] =
          (amplitudes[i] * TemporalAmplitudeAttenuation(f) - amplitude_[i]) /
          static_cast<float>(size);
    }

    float frequency_value = frequency_;
    float sums[FM1_HARMONIC_TEMPORAL_BLOCK_SIZE];
    float first_sample[num_harmonics];
    float second_sample[num_harmonics];
    float recurrence[num_harmonics];

    size_t offset = 0;
    while (offset < size) {
      size_t count = size - offset;
      if (count > FM1_HARMONIC_TEMPORAL_BLOCK_SIZE) {
        count = FM1_HARMONIC_TEMPORAL_BLOCK_SIZE;
      }

      float first_phase = 0.0f;
      float second_phase = 0.0f;
      float frequency_sum = 0.0f;
      for (size_t j = 0; j < count; ++j) {
        frequency_value += frequency_step;
        const float increment = frequency_value;
        frequency_sum += increment;
        phase_ += increment;
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        if (j == 0) {
          first_phase = phase_;
        } else if (j == 1) {
          second_phase = phase_;
        }
      }
      if (count == 1) {
        second_phase = first_phase;
      }
      const float average_frequency = frequency_sum /
          static_cast<float>(count);

      // Seed each block from the exact fundamental phase. The spatial
      // Chebyshev recurrence is retained here, so only the time evolution
      // within the bounded block uses the temporal recurrence.
      const float two_x = 2.0f * TemporalSineNoWrap(first_phase);
      float previous;
      float current;
      if (first_harmonic_index == 1) {
        previous = 1.0f;
        current = two_x * 0.5f;
      } else {
        previous = TemporalSine(
            first_phase * static_cast<float>(first_harmonic_index - 1) +
            0.25f);
        current = TemporalSine(
            first_phase * static_cast<float>(first_harmonic_index));
      }
      for (int i = 0; i < num_harmonics; ++i) {
        first_sample[i] = current;
        const float temp = current;
        current = two_x * current - previous;
        previous = temp;
      }

      const float two_x_next = 2.0f * TemporalSineNoWrap(second_phase);
      if (first_harmonic_index == 1) {
        previous = 1.0f;
        current = two_x_next * 0.5f;
      } else {
        previous = TemporalSine(
            second_phase * static_cast<float>(first_harmonic_index - 1) +
            0.25f);
        current = TemporalSine(
            second_phase * static_cast<float>(first_harmonic_index));
      }
      for (int i = 0; i < num_harmonics; ++i) {
        second_sample[i] = current;
        const float temp = current;
        current = two_x_next * current - previous;
        previous = temp;
      }

#if FM1_HARMONIC_TEMPORAL_RECURSIVE_COEFFICIENTS
      // Seed the bounded cosine bank twice, then generate the remaining
      // Chebyshev coefficients with multiply/subtracts instead of one wrapped
      // sine-table interpolation per partial.
      const float coefficient_two_x =
          2.0f * TemporalSine(average_frequency + 0.25f);
      if (__builtin_fabsf(coefficient_two_x) < 1.8f) {
        float coefficient_previous = 2.0f * TemporalSine(
            average_frequency * static_cast<float>(first_harmonic_index - 1) +
            0.25f);
        float coefficient_current = 2.0f * TemporalSine(
            average_frequency * static_cast<float>(first_harmonic_index) +
            0.25f);
        for (int i = 0; i < num_harmonics; ++i) {
          recurrence[i] = coefficient_current;
          const float temp = coefficient_current;
          coefficient_current = coefficient_two_x * coefficient_current -
              coefficient_previous;
          coefficient_previous = temp;
        }
      } else {
        for (int i = 0; i < num_harmonics; ++i) {
          const float partial_frequency = average_frequency *
              static_cast<float>(first_harmonic_index + i);
          recurrence[i] = 2.0f * TemporalSine(partial_frequency + 0.25f);
        }
      }
#else
      for (int i = 0; i < num_harmonics; ++i) {
        const float partial_frequency = average_frequency *
            static_cast<float>(first_harmonic_index + i);
          recurrence[i] = 2.0f * TemporalSine(partial_frequency + 0.25f);
      }
#endif

      for (size_t j = 0; j < count; ++j) {
        sums[j] = 0.0f;
      }
      for (int i = 0; i < num_harmonics; ++i) {
#if FM1_HARMONIC_TEMPORAL_UNBRANCHED_PARTIAL_LOOP
        float wave_previous = first_sample[i];
        float wave_current = second_sample[i];
        float amplitude = amplitude_[i];
        const float coefficient = recurrence[i];
        amplitude += amplitude_increment[i];
        sums[0] += amplitude * wave_previous;
        if (count > 1) {
          amplitude += amplitude_increment[i];
          sums[1] += amplitude * wave_current;
          float next = coefficient * wave_current - wave_previous;
          wave_previous = wave_current;
          wave_current = next;
          for (size_t j = 2; j < count; ++j) {
            amplitude += amplitude_increment[i];
            sums[j] += amplitude * wave_current;
            next = coefficient * wave_current - wave_previous;
            wave_previous = wave_current;
            wave_current = next;
          }
        }
        amplitude_[i] = amplitude;
#else
        float wave_previous = first_sample[i];
        float wave_current = second_sample[i];
        float amplitude = amplitude_[i];
        const float coefficient = recurrence[i];
        for (size_t j = 0; j < count; ++j) {
          const float wave = j == 0 ? wave_previous : wave_current;
          amplitude += amplitude_increment[i];
          sums[j] += amplitude * wave;
          if (j >= 1) {
            const float next = coefficient * wave_current - wave_previous;
            wave_previous = wave_current;
            wave_current = next;
          }
        }
        amplitude_[i] = amplitude;
#endif
      }

      if (first_harmonic_index == 1) {
        for (size_t j = 0; j < count; ++j) {
          out[offset + j] = sums[j];
        }
      } else {
        for (size_t j = 0; j < count; ++j) {
          out[offset + j] += sums[j];
        }
      }
      offset += count;
    }
    frequency_ = frequency_value;
    return true;
  }
#endif

#if FM1_HARMONIC_SHARED_RECURRENCE || FM1_HARMONIC_TRIPLE_RECURRENCE
#if FM1_HARMONIC_TRIPLE_RAM
  static inline __attribute__((always_inline)) bool SameBits(const float& a, const float& b) {
#else
  static inline bool SameBits(const float& a, const float& b) {
#endif
    static_assert(sizeof(float) == sizeof(uint32_t),
                  "harmonic state comparison expects 32-bit floats");
    uint32_t a_bits;
    uint32_t b_bits;
    __builtin_memcpy(&a_bits, &a, sizeof(a_bits));
    __builtin_memcpy(&b_bits, &b, sizeof(b_bits));
    return a_bits == b_bits;
  }
#endif

  // Oscillator state.
  float phase_;

  // For interpolation of parameters.
  float frequency_;
  float amplitude_[num_harmonics];
#if FM1_HARMONIC_STABLE_AMPLITUDE_FASTPATH
  float stable_target_[num_harmonics];
  bool stable_target_valid_;
#if FM1_HARMONIC_SETTLED_AMPLITUDE_FASTPATH
  bool stable_amplitude_settled_;
#endif
#endif
  
  DISALLOW_COPY_AND_ASSIGN(HarmonicOscillator);
};

#if FM1_HARMONIC_TRIPLE_RAM
// Keep the target RAM entry reachable across XIP/RAM branch range. The
// volatile typed member pointer deliberately preserves the indirect call;
// on host builds it exercises the same call bridge without a RAM section.
template<int num_harmonics, int first_harmonic_index>
inline __attribute__((always_inline)) bool RenderTripleRamBridge(
    HarmonicOscillator<num_harmonics>& oscillator,
    HarmonicOscillator<num_harmonics>& high,
    HarmonicOscillator<num_harmonics>& companion,
    float frequency,
    const float* amplitudes,
    float* out,
    const float* high_amplitudes,
    const float* companion_amplitudes,
    float* companion_out,
    size_t size) {
  typedef bool (HarmonicOscillator<num_harmonics>::*TripleRenderer)(
      HarmonicOscillator<num_harmonics>&,
      HarmonicOscillator<num_harmonics>&,
      float,
      const float*,
      float*,
      const float*,
      const float*,
      float*,
      size_t);
  TripleRenderer volatile renderer =
      &HarmonicOscillator<num_harmonics>::template
          RenderTriple<first_harmonic_index>;
  return (oscillator.*renderer)(high, companion, frequency, amplitudes, out,
      high_amplitudes, companion_amplitudes, companion_out, size);
}
#endif

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_HARMONIC_OSCILLATOR_H_
