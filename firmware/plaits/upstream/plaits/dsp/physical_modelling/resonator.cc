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
// Resonator, taken from Rings' code but with fixed position.

#include "plaits/dsp/physical_modelling/resonator.h"

#include <algorithm>

#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
#include <stdint.h>
#endif

#ifndef FM1_MODAL_PITCH_TOLERANT_CACHE
#define FM1_MODAL_PITCH_TOLERANT_CACHE 0
#endif

#ifndef FM1_MODAL_PITCH_CACHE_RELATIVE_TOLERANCE
#define FM1_MODAL_PITCH_CACHE_RELATIVE_TOLERANCE 0.0005f
#endif

#ifndef FM1_MODAL_PITCH_CACHE_F0_ONLY_GUARD
#define FM1_MODAL_PITCH_CACHE_F0_ONLY_GUARD 0
#endif

#if FM1_MODAL_PITCH_CACHE_F0_ONLY_GUARD && \
    !FM1_MODAL_PITCH_TOLERANT_CACHE
#error "The modal f0-only cache guard requires pitch-tolerant caching"
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
uint32_t fm1_modal_test_cache_blocks = 0;
uint32_t fm1_modal_test_low_rate_cache_dispatches = 0;
uint32_t fm1_modal_test_low_rate_cache_fallbacks = 0;
uint32_t fm1_modal_test_low_rate_cache_misses = 0;
uint32_t fm1_modal_test_persistent_direct_cache_hits = 0;
uint64_t fm1_modal_test_mode_input_frames = 0;
uint64_t fm1_modal_test_mode_filter_samples = 0;
uint64_t fm1_modal_test_mode_batches = 0;
uint64_t fm1_modal_test_low_rate_batches = 0;
uint64_t fm1_modal_test_low_rate_covered_frames = 0;
#endif

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
inline uint32_t FloatBits(float value) {
  uint32_t bits;
  __builtin_memcpy(&bits, &value, sizeof(bits));
  return bits;
}
#endif

PLAITS_COLD_INIT_ATTR void Resonator::Init(float position, int resolution) {
  resolution_ = min(resolution, kMaxNumModes);

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
  coefficient_cache_valid_ = false;
#endif

#if FM1_MODAL_MULTIRATE_LOW_MODES
  fill(&multirate_input_history_[0], &multirate_input_history_[16], 0.0f);
  multirate_input_write_ = 0;
  multirate_decimation_phase_ = 0;
  fill(&multirate_low_history_[0], &multirate_low_history_[8], 0.0f);
  multirate_low_write_ = 0;
  multirate_interpolation_phase_ = 0;
  fill(&multirate_high_delay_[0], &multirate_high_delay_[14], 0.0f);
  multirate_high_delay_write_ = 0;
  fill(&multirate_batch_low_[0],
       &multirate_batch_low_[kMaxNumModes / kModeBatchSize], false);
#endif
  
  CosineOscillator amplitudes;
  amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position);
  
  for (int i = 0; i < resolution; ++i) {
    mode_amplitude_[i] = amplitudes.Next() * 0.25f;
  }
  
  for (int i = 0; i < kMaxNumModes / kModeBatchSize; ++i) {
    mode_filters_[i].Init();
  }
}

#if FM1_MODAL_MULTIRATE_LOW_MODES
size_t Resonator::DecimateForLowModes(
    const float* input, size_t size, float* low_input) {
  size_t low_size = 0;
  uint8_t input_write = multirate_input_write_;
  uint8_t decimation_phase = multirate_decimation_phase_;
  for (size_t i = 0; i < size; ++i) {
    multirate_input_history_[input_write] = input[i];
    if (decimation_phase == 0) {
      const uint8_t w0 = input_write;
      const uint8_t w2 = (input_write + 14u) & 15u;
      const uint8_t w4 = (input_write + 12u) & 15u;
      const uint8_t w6 = (input_write + 10u) & 15u;
      const uint8_t w7 = (input_write + 9u) & 15u;
      const uint8_t w8 = (input_write + 8u) & 15u;
      const uint8_t w10 = (input_write + 6u) & 15u;
      const uint8_t w12 = (input_write + 4u) & 15u;
      const uint8_t w14 = (input_write + 2u) & 15u;
      low_input[low_size++] =
          -0.003651454f *
              (multirate_input_history_[w0] +
               multirate_input_history_[w14]) +
          0.016179253f *
              (multirate_input_history_[w2] +
               multirate_input_history_[w12]) -
          0.068411777f *
              (multirate_input_history_[w4] +
               multirate_input_history_[w10]) +
          0.304947517f *
              (multirate_input_history_[w6] +
               multirate_input_history_[w8]) +
          0.501872920f * multirate_input_history_[w7];
    }
    input_write = (input_write + 1u) & 15u;
    decimation_phase ^= 1u;
  }
  multirate_input_write_ = input_write;
  multirate_decimation_phase_ = decimation_phase;
  return low_size;
}

void Resonator::ProcessCachedMultirate(
    const float* process_input, const float* low_input,
    float* low_output, float* high_output, size_t low_size, size_t size) {
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
  ++fm1_modal_test_cache_blocks;
#endif
  for (int batch = 0; batch < resolution_ / kModeBatchSize; ++batch) {
    const int offset = batch * kModeBatchSize;
#if FM1_MODAL_CACHE_AOS && FM1_MODAL_DIRECT_FORM && \
    FM1_MODAL_HYBRID_DIRECT_FORM
    if (multirate_batch_low_[batch] &&
        mode_filters_[batch].CanUseRate2()) {
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
      ++fm1_modal_test_mode_batches;
      ++fm1_modal_test_low_rate_cache_dispatches;
      fm1_modal_test_mode_input_frames += size * kModeBatchSize;
      fm1_modal_test_mode_filter_samples += low_size * kModeBatchSize;
      ++fm1_modal_test_low_rate_batches;
      fm1_modal_test_low_rate_covered_frames +=
          2u * low_size * kModeBatchSize;
#endif
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_cache_[offset], low_input, low_output, low_size, true);
      mode_filters_[batch].UpdateOtherInputHistory(
          true, process_input, size);
    } else
#endif
    {
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
      ++fm1_modal_test_mode_batches;
      fm1_modal_test_mode_input_frames += size * kModeBatchSize;
      fm1_modal_test_mode_filter_samples += size * kModeBatchSize;
      if (multirate_batch_low_[batch])
        ++fm1_modal_test_low_rate_cache_fallbacks;
#endif
#if FM1_MODAL_CACHE_AOS
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_cache_[offset], process_input, high_output, size
#if FM1_MODAL_MULTIRATE_LOW_MODES
          , false
#endif
          );
#else
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_g_[offset], &coefficient_r_plus_g_[offset],
          &coefficient_h_[offset], &coefficient_gain_[offset],
          process_input, high_output, size
#if FM1_MODAL_MULTIRATE_LOW_MODES
          , false
#endif
          );
#endif
      mode_filters_[batch].UpdateOtherInputHistory(
          false, low_input, low_size);
    }
  }
}

void Resonator::MixMultirateOutputs(
    const float* low_output, const float* high_output,
    float* out, size_t size, size_t low_size) {
  uint8_t low_write = multirate_low_write_;
  uint8_t interpolation_phase = multirate_interpolation_phase_;
  uint8_t high_delay_write = multirate_high_delay_write_;
  size_t low_index = 0;
  for (size_t i = 0; i < size; ++i) {
    float low_value;
    if (interpolation_phase == 0) {
      if (low_index < low_size) {
        multirate_low_history_[low_write] = low_output[low_index++];
      }
      const uint8_t w0 = low_write;
      const uint8_t w1 = (low_write + 7u) & 7u;
      const uint8_t w2 = (low_write + 6u) & 7u;
      const uint8_t w3 = (low_write + 5u) & 7u;
      const uint8_t w4 = (low_write + 4u) & 7u;
      const uint8_t w5 = (low_write + 3u) & 7u;
      const uint8_t w6 = (low_write + 2u) & 7u;
      const uint8_t w7 = (low_write + 1u) & 7u;
      low_value = 2.0f * (
          -0.003651454f *
              (multirate_low_history_[w0] +
               multirate_low_history_[w7]) +
          0.016179253f *
              (multirate_low_history_[w1] +
               multirate_low_history_[w6]) -
          0.068411777f *
              (multirate_low_history_[w2] +
               multirate_low_history_[w5]) +
          0.304947517f *
              (multirate_low_history_[w3] +
               multirate_low_history_[w4]));
      low_write = (low_write + 1u) & 7u;
    } else {
      low_value = multirate_low_history_[(low_write + 4u) & 7u];
    }
    interpolation_phase ^= 1u;

    const float high_value = multirate_high_delay_[high_delay_write];
    multirate_high_delay_[high_delay_write] = high_output[i];
    ++high_delay_write;
    if (high_delay_write == 14u) high_delay_write = 0;
    out[i] += high_value + low_value;
  }
  multirate_low_write_ = low_write;
  multirate_interpolation_phase_ = interpolation_phase;
  multirate_high_delay_write_ = high_delay_write;
}
#endif

inline float NthHarmonicCompensation(int n, float stiffness) {
  float stretch_factor = 1.0f;
  for (int i = 0; i < n - 1; ++i) {
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      stiffness *= 0.93f;
    } else {
      stiffness *= 0.98f;
    }
  }
  return 1.0f / stretch_factor;
}

void Resonator::Process(
    float f0,
    float structure,
    float brightness,
    float damping,
    const float* in,
    float* out,
    size_t size) {
#if FM1_MODAL_DIRECT_FORM
  float input_copy[kMaxBlockSize];
  const float* process_input = in;
  if (in == out && size <= kMaxBlockSize) {
    for (size_t i = 0; i < size; ++i) {
      input_copy[i] = in[i];
    }
    process_input = input_copy;
  }
#else
  const float* process_input = in;
#endif
#if FM1_MODAL_MULTIRATE_LOW_MODES
  float low_input[kMaxBlockSize / 2 + 1];
  float low_output[kMaxBlockSize / 2 + 1];
  float high_output[kMaxBlockSize];
  fill(&low_output[0], &low_output[kMaxBlockSize / 2 + 1], 0.0f);
  fill(&high_output[0], &high_output[kMaxBlockSize], 0.0f);
  const size_t low_size = DecimateForLowModes(process_input, size, low_input);
#endif
#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
  const float requested_f0 = f0;
  const float requested_structure = structure;
  const float requested_brightness = brightness;
  const float requested_damping = damping;
  const uint32_t key[6] = {
      FloatBits(requested_f0),
      FloatBits(requested_structure),
      FloatBits(requested_brightness),
      FloatBits(requested_damping),
      static_cast<uint32_t>(resolution_),
      static_cast<uint32_t>(kSampleRateHz),
  };
#if FM1_MODAL_EXACT_CACHE
  bool cache_hit = coefficient_cache_valid_;
  for (size_t i = 0; cache_hit && i < 6; ++i) {
    cache_hit = coefficient_key_[i] == key[i];
  }
  if (cache_hit) {
#if FM1_MODAL_MULTIRATE_LOW_MODES
    ProcessCachedMultirate(
        process_input, low_input, low_output, high_output, low_size, size);
    MixMultirateOutputs(low_output, high_output, out, size, low_size);
#else
    for (int batch = 0; batch < resolution_ / kModeBatchSize; ++batch) {
      const int offset = batch * kModeBatchSize;
#if FM1_MODAL_CACHE_AOS
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_cache_[offset], process_input, out, size);
#else
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_g_[offset],
          &coefficient_r_plus_g_[offset],
          &coefficient_h_[offset],
          &coefficient_gain_[offset],
          process_input,
          out,
          size);
#endif
    }
#endif
    return;
  }
#endif

#if FM1_MODAL_BOUNDED_CACHE
  bool bounded_hit = false;
  if (coefficient_cache_valid_ &&
      static_cast<uint32_t>(resolution_) == key[4] &&
      coefficient_cache_rate_hz_ == key[5]) {
    const float source_f0 = coefficient_source_[0];
#if FM1_MODAL_PITCH_TOLERANT_CACHE
    const float frequency_tolerance =
        FM1_MODAL_PITCH_CACHE_RELATIVE_TOLERANCE;
    const float f0_scale = max(__builtin_fabsf(source_f0), 1.0e-8f);
    const bool f0_near =
        __builtin_fabsf(requested_f0 - source_f0) <=
            f0_scale * frequency_tolerance;
#else
    const float frequency_tolerance = 0.0001f;
    const bool f0_near = FloatBits(requested_f0) == FloatBits(source_f0);
#endif
    const bool raw_parameters_exact =
        FloatBits(requested_f0) == FloatBits(source_f0) &&
        FloatBits(requested_structure) == FloatBits(coefficient_source_[1]) &&
        FloatBits(requested_brightness) == FloatBits(coefficient_source_[2]) &&
        FloatBits(requested_damping) == FloatBits(coefficient_source_[3]);
#if FM1_MODAL_PITCH_TOLERANT_CACHE
    const bool raw_parameters_near =
        f0_near &&
        FloatBits(requested_structure) == FloatBits(coefficient_source_[1]) &&
        FloatBits(requested_brightness) == FloatBits(coefficient_source_[2]) &&
        FloatBits(requested_damping) == FloatBits(coefficient_source_[3]);
#else
    const bool raw_parameters_near =
        f0_near &&
        __builtin_fabsf(requested_structure - coefficient_source_[1]) <= 0.01f &&
        __builtin_fabsf(requested_brightness - coefficient_source_[2]) <= 0.01f &&
        __builtin_fabsf(requested_damping - coefficient_source_[3]) <= 0.01f;
#endif
    if (raw_parameters_exact) {
      // Exact raw inputs imply the entire derived coefficient vector matches;
      // skip reconstructing and checking every mode on the common steady path.
      bounded_hit = true;
    } else if (raw_parameters_near) {
#if FM1_MODAL_MULTIRATE_LOW_MODES
      // A near-key reuse can move a modal frequency across the rate boundary.
      // Rebuild once rather than reuse the wrong cadence mask or pole cache.
      bounded_hit = false;
#else
#if FM1_MODAL_PITCH_CACHE_F0_ONLY_GUARD
      // With all non-pitch inputs bit-identical, every uncapped modal frequency
      // scales with f0 and its Q changes by at most the same relative bound.
      // This skips rebuilding and validating the full 24-mode vector on a
      // slowly moving note. Modes at the Nyquist clamp remain clamped.
      bounded_hit = true;
#else
      float stiffness = Interpolate(lut_stiffness, requested_structure, 64.0f);
      float compensated_f0 = requested_f0 * NthHarmonicCompensation(3, stiffness);
      float harmonic = compensated_f0;
      float stretch_factor = 1.0f;
      const float q_sqrt = SemitonesToRatio(requested_damping * 79.7f);
      float q = 500.0f * q_sqrt * q_sqrt;
      float candidate_brightness = requested_brightness;
      candidate_brightness *= 1.0f - requested_structure * 0.3f;
      candidate_brightness *= 1.0f - requested_damping * 0.3f;
      const float q_loss = candidate_brightness *
          (2.0f - candidate_brightness) * 0.85f + 0.15f;
      bounded_hit = true;
      for (int i = 0; i < resolution_; ++i) {
        float mode_frequency = harmonic * stretch_factor;
        if (mode_frequency >= 0.499f) {
          mode_frequency = 0.499f;
        }
        const float mode_attenuation = 1.0f - mode_frequency * 2.0f;
        const float mode_gain = mode_amplitude_[i] * mode_attenuation;
        const float mode_q = 1.0f + mode_frequency * q;
        const float cached_f = coefficient_mode_f_[i];
        const float cached_q = coefficient_mode_q_[i];
#if FM1_MODAL_CACHE_AOS
        const float cached_gain = coefficient_cache_[i].gain;
#else
        const float cached_gain = coefficient_gain_[i];
#endif
        const float f_scale = max(__builtin_fabsf(cached_f), 1.0e-8f);
        const float q_scale = max(__builtin_fabsf(cached_q), 1.0f);
        const float gain_scale = max(__builtin_fabsf(cached_gain), 1.0e-4f);
        if (__builtin_fabsf(mode_frequency - cached_f) >
                f_scale * frequency_tolerance ||
            __builtin_fabsf(mode_q - cached_q) > q_scale * 0.0005f ||
            __builtin_fabsf(mode_gain - cached_gain) > gain_scale * 0.0005f) {
          bounded_hit = false;
          break;
        }
        stretch_factor += stiffness;
        if (stiffness < 0.0f) {
          stiffness *= 0.93f;
        } else {
          stiffness *= 0.98f;
        }
        harmonic += compensated_f0;
        q *= q_loss;
      }
#endif
#endif
    }
  }
  if (bounded_hit) {
#if FM1_MODAL_MULTIRATE_LOW_MODES
    ProcessCachedMultirate(
        process_input, low_input, low_output, high_output, low_size, size);
    MixMultirateOutputs(low_output, high_output, out, size, low_size);
#else
    for (int batch = 0; batch < resolution_ / kModeBatchSize; ++batch) {
      const int offset = batch * kModeBatchSize;
#if FM1_MODAL_CACHE_AOS
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_cache_[offset], process_input, out, size);
#else
      mode_filters_[batch].ProcessCached<FILTER_MODE_BAND_PASS, true>(
          &coefficient_g_[offset],
          &coefficient_r_plus_g_[offset],
          &coefficient_h_[offset],
          &coefficient_gain_[offset],
          process_input,
          out,
          size);
#endif
    }
#endif
    return;
  }
#endif
  coefficient_cache_valid_ = false;
#endif

  float stiffness = Interpolate(lut_stiffness, structure, 64.0f);
  f0 *= NthHarmonicCompensation(3, stiffness);
  
  float harmonic = f0;
  float stretch_factor = 1.0f;
  float q_sqrt = SemitonesToRatio(damping * 79.7f);
  float q = 500.0f * q_sqrt * q_sqrt;
  brightness *= 1.0f - structure * 0.3f;
  brightness *= 1.0f - damping * 0.3f;
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
  
  float mode_q[kModeBatchSize];
  float mode_f[kModeBatchSize];
  float mode_a[kModeBatchSize];
  int batch_counter = 0;
#if FM1_MODAL_MULTIRATE_LOW_MODES
  bool batch_is_low = true;
#endif
  
  ResonatorSvf<kModeBatchSize>* batch_processor = &mode_filters_[0];
  
  
  for (int i = 0; i < resolution_; ++i) {
    float mode_frequency = harmonic * stretch_factor;
    if (mode_frequency >= 0.499f) {
      mode_frequency = 0.499f;
    }
    const float mode_attenuation = 1.0f - mode_frequency * 2.0f;

#if FM1_MODAL_MULTIRATE_LOW_MODES
    if (mode_frequency >=
        static_cast<float>(FM1_MODAL_MULTIRATE_LOW_MODE_CUTOFF_HZ) /
            kSampleRate) {
      batch_is_low = false;
    }
#endif
    
    mode_f[batch_counter] = mode_frequency;
    mode_q[batch_counter] = 1.0f + mode_frequency * q;
    mode_a[batch_counter] = mode_amplitude_[i] * mode_attenuation;
#if FM1_MODAL_BOUNDED_CACHE
    coefficient_mode_f_[i] = mode_frequency;
    coefficient_mode_q_[i] = mode_q[batch_counter];
#endif
    ++batch_counter;
    
    if (batch_counter == kModeBatchSize) {
      batch_counter = 0;
#if FM1_MODAL_MULTIRATE_LOW_MODES
      if (batch_is_low &&
          !batch_processor->CanBuildRate2(mode_f, mode_q)) {
        batch_is_low = false;
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
        ++fm1_modal_test_low_rate_cache_fallbacks;
#endif
      }
      if (batch_is_low) {
        const int batch_index = (i + 1) / kModeBatchSize - 1;
        const int offset = batch_index * kModeBatchSize;
        multirate_batch_low_[batch_index] = true;
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
        ++fm1_modal_test_low_rate_cache_misses;
#endif
#if FM1_MODAL_RATE2_STABLE_CACHE_ONLY
        // Cache misses coincide with new Modal controls. Render those blocks
        // at the native rate, but prepare the exact half-rate coefficients so
        // an unchanged following block can use the lower cadence.
        batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
            mode_f, mode_q, mode_a, process_input, high_output, size,
            &coefficient_cache_[offset], false);
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
        ++fm1_modal_test_mode_batches;
        fm1_modal_test_mode_input_frames += size * kModeBatchSize;
        fm1_modal_test_mode_filter_samples += size * kModeBatchSize;
#endif
#if !FM1_MODAL_RATE2_REUSE_VALIDATED_CACHE
        batch_processor->BuildRate2Cache(&coefficient_cache_[offset]);
#endif
        batch_processor->UpdateOtherInputHistory(
            false, low_input, low_size);
#else
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
        ++fm1_modal_test_mode_batches;
        fm1_modal_test_mode_input_frames += size * kModeBatchSize;
        fm1_modal_test_mode_filter_samples += low_size * kModeBatchSize;
        ++fm1_modal_test_low_rate_batches;
        fm1_modal_test_low_rate_covered_frames +=
            2u * low_size * kModeBatchSize;
#endif
#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
#if FM1_MODAL_CACHE_AOS
        batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
            mode_f, mode_q, mode_a, low_input, low_output, low_size,
            &coefficient_cache_[offset], true);
#else
        batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
            mode_f, mode_q, mode_a, low_input, low_output, low_size,
            &coefficient_g_[offset], &coefficient_r_plus_g_[offset],
            &coefficient_h_[offset], &coefficient_gain_[offset], true);
#endif
#else
        batch_processor->Process<FILTER_MODE_BAND_PASS, true>(
            mode_f,
            mode_q,
            mode_a,
            low_input,
            low_output,
            low_size,
            true);
#endif
        batch_processor->UpdateOtherInputHistory(
            true, process_input, size);
#endif
      } else {
        const int batch_index = (i + 1) / kModeBatchSize - 1;
        const int offset = batch_index * kModeBatchSize;
        multirate_batch_low_[batch_index] = false;
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
        ++fm1_modal_test_mode_batches;
        fm1_modal_test_mode_input_frames += size * kModeBatchSize;
        fm1_modal_test_mode_filter_samples += size * kModeBatchSize;
#endif
#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
#if FM1_MODAL_CACHE_AOS
        batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
            mode_f, mode_q, mode_a, process_input, high_output, size,
            &coefficient_cache_[offset], false);
#else
        batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
            mode_f, mode_q, mode_a, process_input, high_output, size,
            &coefficient_g_[offset], &coefficient_r_plus_g_[offset],
            &coefficient_h_[offset], &coefficient_gain_[offset], false);
#endif
#else
        batch_processor->Process<FILTER_MODE_BAND_PASS, true>(
            mode_f,
            mode_q,
            mode_a,
            process_input,
            high_output,
            size,
            false);
#endif
        batch_processor->UpdateOtherInputHistory(
            false, low_input, low_size);
      }
      batch_is_low = true;
#else
#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
      const int offset = (i + 1) - kModeBatchSize;
#if FM1_MODAL_CACHE_AOS
      batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
          mode_f, mode_q, mode_a, process_input, out, size,
          &coefficient_cache_[offset]);
#else
      batch_processor->ProcessAndCache<FILTER_MODE_BAND_PASS, true>(
          mode_f,
          mode_q,
          mode_a,
          process_input,
          out,
          size,
          &coefficient_g_[offset],
          &coefficient_r_plus_g_[offset],
          &coefficient_h_[offset],
          &coefficient_gain_[offset]);
#endif
#else
      batch_processor->Process<FILTER_MODE_BAND_PASS, true>(
          mode_f,
          mode_q,
          mode_a,
          process_input,
          out,
          size);
#endif
#endif
      ++batch_processor;
    }
    
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93f;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98f;
    }
    harmonic += f0;
    q *= q_loss;
  }

#if FM1_MODAL_MULTIRATE_LOW_MODES
  MixMultirateOutputs(low_output, high_output, out, size, low_size);
#endif

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
#if FM1_MODAL_EXACT_CACHE
  for (size_t i = 0; i < 6; ++i) {
    coefficient_key_[i] = key[i];
  }
#endif
  coefficient_cache_rate_hz_ = key[5];
  coefficient_source_[0] = requested_f0;
  coefficient_source_[1] = requested_structure;
  coefficient_source_[2] = requested_brightness;
  coefficient_source_[3] = requested_damping;
  coefficient_cache_valid_ = true;
#endif
}

}  // namespace plaits
