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
// Additive synthesis with 32 partials.

#ifndef FM1_HARMONIC_RAM_INLINE_HELPERS
#define FM1_HARMONIC_RAM_INLINE_HELPERS 0
#endif
#if FM1_HARMONIC_RAM_INLINE_HELPERS
#define FM1_HARMONIC_RAM_TU 1
#endif

#include "plaits/dsp/engine/additive_engine.h"

#include <algorithm>
#include <stdio.h>

#ifndef FM1_HARMONIC_TARGET_AMPLITUDE_CACHE
#define FM1_HARMONIC_TARGET_AMPLITUDE_CACHE 1
#endif
#ifndef FM1_HARMONIC_CACHE_TEST
#define FM1_HARMONIC_CACHE_TEST 0
#endif

#include "stmlib/dsp/cosine_oscillator.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

#ifndef FM1_HARMONIC_FAST_AMPLITUDE_SINE
#define FM1_HARMONIC_FAST_AMPLITUDE_SINE 1
#endif
#ifndef FM1_HARMONIC_SHARED_AMPLITUDE_MARGIN
#define FM1_HARMONIC_SHARED_AMPLITUDE_MARGIN 1
#endif

#ifndef FM1_HARMONIC_CLENSHAW
#define FM1_HARMONIC_CLENSHAW 1
#endif
#ifndef FM1_HARMONIC_CLENSHAW_COVERAGE
#define FM1_HARMONIC_CLENSHAW_COVERAGE 0
#endif

#if FM1_HARMONIC_TRIPLE_RAM
#define FM1_HARMONIC_RENDER_TRIPLE(oscillator, high, companion, ...) \
  RenderTripleRamBridge<12, 1>(oscillator, high, companion, __VA_ARGS__)
#else
#define FM1_HARMONIC_RENDER_TRIPLE(oscillator, high, companion, ...) \
  oscillator.RenderTriple<1>(high, companion, __VA_ARGS__)
#endif

#if FM1_HARMONIC_CLENSHAW && FM1_HARMONIC_CLENSHAW_COVERAGE
extern "C" void fm1_harmonic_clenshaw_coverage_record(int accepted);
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

#if FM1_HARMONIC_FAST_AMPLITUDE_SINE
inline float FastAmplitudeSine(float phase) {
  // The bump phase is non-negative and bounded by the model controls. Fold it
  // to one half-cycle, then correct the parabolic sine with a cubic term.
  const int32_t integral = static_cast<int32_t>(phase);
  const float wrapped = phase - static_cast<float>(integral);
  const bool positive = wrapped <= 0.5f;
  const float x = positive ? wrapped : 1.0f - wrapped;
  const float y = 16.0f * x * (0.5f - x);
  const float corrected = y + 0.225f * (y * y - y);
  return positive ? corrected : -corrected;
}
#endif

#if FM1_HARMONIC_TARGET_AMPLITUDE_CACHE
namespace {
const size_t kCachedTargetCount = 32;
const size_t kCacheMetadataOffset = kNumHarmonics + kCachedTargetCount;
const size_t kCacheAllocationCount = kCacheMetadataOffset + 4;
#if FM1_HARMONIC_CACHE_TEST
uint32_t harmonic_target_cache_hits = 0;
uint32_t harmonic_target_cache_misses = 0;
uint32_t harmonic_target_cache_unavailable = 0;
uint32_t harmonic_target_cache_state_mismatches = 0;
uint32_t harmonic_target_cache_first_index = 0xffffffffu;
uint32_t harmonic_target_cache_first_actual = 0;
uint32_t harmonic_target_cache_first_reference = 0;
uint32_t harmonic_target_cache_test_serial = 0;
uint32_t harmonic_target_cache_first_serial = 0xffffffffu;
uint32_t harmonic_target_cache_first_hit = 0;
uint32_t harmonic_target_cache_first_key[3] = {0, 0, 0};
uint32_t harmonic_target_cache_raw_target_mismatches = 0;
uint32_t harmonic_target_cache_first_raw_index = 0xffffffffu;
uint32_t harmonic_target_cache_first_raw_actual = 0;
uint32_t harmonic_target_cache_first_raw_reference = 0;
float harmonic_target_cache_reference_targets[kNumHarmonics];
bool harmonic_target_cache_test_hit = false;
uint32_t harmonic_target_cache_test_key[3] = {0, 0, 0};
#endif
inline uint32_t HarmonicFloatBits(float value) {
  uint32_t bits;
  __builtin_memcpy(&bits, &value, sizeof(bits));
  return bits;
}
void ComputeCachedTargets(float centroid, float slope, float bumps,
                          float* targets, size_t count,
                          float shared_margin) {
  const float n = static_cast<float>(count) - 1.0f;
#if FM1_HARMONIC_SHARED_AMPLITUDE_MARGIN
  const float margin = shared_margin;
#else
  const float margin = (1.0f / slope - 1.0f) / (1.0f + bumps);
#endif
  const float center = centroid * (n + margin) - 0.5f * margin;
  for (size_t i = 0; i < count; ++i) {
    float order = __builtin_fabsf(static_cast<float>(i) - center) * slope;
    float gain = 1.0f - order;
    gain += __builtin_fabsf(gain);
    gain *= gain;
    float b = 0.25f + order * bumps;
#if FM1_HARMONIC_FAST_AMPLITUDE_SINE
    float bump_factor = 1.0f + FastAmplitudeSine(b);
#else
    float bump_factor = 1.0f + Sine(b);
#endif
    gain *= bump_factor;
    gain *= gain;
    gain *= gain;
    targets[i] = gain;
  }
}
void ApplyCachedTargets(float* amplitudes, const int* indices, size_t count,
                        const float* targets) {
  float sum = 0.001f;
  for (size_t i = 0; i < count; ++i) {
    const int j = indices[i];
#if FM1_PLAITS_RATE_TIME_CALIBRATION
#if PLAITS_INTERNAL_SAMPLE_RATE == 32000
    ONE_POLE(amplitudes[j], targets[i], 0.00137786439f);
#elif PLAITS_INTERNAL_SAMPLE_RATE == 48000
    ONE_POLE(amplitudes[j], targets[i], 0.00091878734f);
#else
    ONE_POLE(amplitudes[j], targets[i], 0.00114835224f);
#endif
#else
    ONE_POLE(amplitudes[j], targets[i], 0.001f);
#endif
    sum += amplitudes[j];
  }
  sum = 1.0f / sum;
  for (size_t i = 0; i < count; ++i) amplitudes[indices[i]] *= sum;
}
}  // namespace
#endif

PLAITS_COLD_INIT_ATTR void AdditiveEngine::Init(BufferAllocator* allocator) {
#if FM1_HARMONIC_TARGET_AMPLITUDE_CACHE
  amplitudes_ = allocator->Allocate<float>(kCacheAllocationCount);
  amplitudes_[kCacheMetadataOffset + 3] = 0.0f;
#else
  amplitudes_ = allocator->Allocate<float>(kNumHarmonics);
#endif
  for (int i = 0; i < kNumHarmonicOscillators; ++i) {
    harmonic_oscillator_[i].Init();
  }
}

PLAITS_COLD_INIT_ATTR void AdditiveEngine::Reset() {
  fill(
      &amplitudes_[0],
      &amplitudes_[kNumHarmonics],
      0.0f);
#if FM1_HARMONIC_TARGET_AMPLITUDE_CACHE
  amplitudes_[kCacheMetadataOffset + 3] = 0.0f;
#endif
}

void AdditiveEngine::UpdateAmplitudes(
    float centroid,
    float slope,
    float bumps,
    float* amplitudes,
    const int* harmonic_indices,
    size_t num_harmonics,
    float shared_margin) {
  const float n = (static_cast<float>(num_harmonics) - 1.0f);
#if FM1_HARMONIC_SHARED_AMPLITUDE_MARGIN
  const float margin = shared_margin;
#else
  const float margin = (1.0f / slope - 1.0f) / (1.0f + bumps);
#endif
  const float center = centroid * (n + margin) - 0.5f * margin;

  float sum = 0.001f;

  for (size_t i = 0; i < num_harmonics; ++i) {
    float order = __builtin_fabsf(static_cast<float>(i) - center) * slope;
    float gain = 1.0f - order;
    gain += __builtin_fabsf(gain);
    gain *= gain;

    float b = 0.25f + order * bumps;
#if FM1_HARMONIC_FAST_AMPLITUDE_SINE
    float bump_factor = 1.0f + FastAmplitudeSine(b);
#else
    float bump_factor = 1.0f + Sine(b);
#endif

    gain *= bump_factor;
    gain *= gain;
    gain *= gain;
    
    int j = harmonic_indices[i];
#if FM1_HARMONIC_TARGET_AMPLITUDE_CACHE && FM1_HARMONIC_CACHE_TEST
    harmonic_target_cache_reference_targets[j] = gain;
#endif
    
    // Warning about the following line: this is not a proper LP filter because
    // of the normalization. But in spite of its strange working, this line
    // turns out to be absolutely essential.
    //
    // I have tried both normalizing the LP-ed spectrum, and LP-ing the
    // normalized spectrum, and both of them cause more annoyances than this
    // "incorrect" solution.
    
#if FM1_PLAITS_RATE_TIME_CALIBRATION
    /* Match the reference 44.1 kHz smoothing interval for this block update. */
#if PLAITS_INTERNAL_SAMPLE_RATE == 32000
    ONE_POLE(amplitudes[j], gain, 0.00137786439f);
#elif PLAITS_INTERNAL_SAMPLE_RATE == 48000
    ONE_POLE(amplitudes[j], gain, 0.00091878734f);
#else
    ONE_POLE(amplitudes[j], gain, 0.00114835224f);
#endif
#else
    ONE_POLE(amplitudes[j], gain, 0.001f);
#endif
    sum += amplitudes[j];
  }

  sum = 1.0f / sum;

  for (size_t i = 0; i < num_harmonics; ++i) {
    amplitudes[harmonic_indices[i]] *= sum;
  }
}

inline float Bump(float x, float centroid, float slope) {
  float d = __builtin_fabsf(x - centroid);
  float bump = 1.0f - d * slope;
  return bump + __builtin_fabsf(bump);
}

const int integer_harmonics[24] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23
};

const int organ_harmonics[8] = {
  0, 1, 2, 3, 5, 7, 9, 11
};

void AdditiveEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);

  const float centroid = parameters.timbre;
  const float raw_bumps = parameters.harmonics;
  const float raw_slope = (1.0f - 0.6f * raw_bumps) * parameters.morph;
  const float slope = 0.01f + 1.99f * raw_slope * raw_slope * raw_slope;
  const float bumps = 16.0f * raw_bumps * raw_bumps;
#if FM1_HARMONIC_SHARED_AMPLITUDE_MARGIN
  const float shared_margin = (1.0f / slope - 1.0f) / (1.0f + bumps);
#else
  const float shared_margin = 0.0f;
#endif
#if FM1_HARMONIC_TARGET_AMPLITUDE_CACHE
#if FM1_HARMONIC_CACHE_TEST
  float amplitude_reference[kNumHarmonics];
  __builtin_memcpy(amplitude_reference, amplitudes_, sizeof(amplitude_reference));
#endif
  float* const target_cache = &amplitudes_[kNumHarmonics];
  float* const target_cache_metadata = &amplitudes_[kCacheMetadataOffset];
  const uint32_t key_centroid = HarmonicFloatBits(centroid);
  const uint32_t key_slope = HarmonicFloatBits(slope);
  const uint32_t key_bumps = HarmonicFloatBits(bumps);
  // Inspect exponent bits so finite-math-only target flags cannot optimize
  // away the NaN/Inf cache exclusion.
  const bool cache_inputs_valid =
      (key_centroid & 0x7f800000u) != 0x7f800000u &&
      (key_slope & 0x7f800000u) != 0x7f800000u &&
      (key_bumps & 0x7f800000u) != 0x7f800000u;
  bool target_cache_hit = false;
  if (cache_inputs_valid && target_cache_metadata[3] == 1.0f) {
    uint32_t old_key[3];
    __builtin_memcpy(old_key, target_cache_metadata, sizeof(old_key));
    target_cache_hit = old_key[0] == key_centroid &&
        old_key[1] == key_slope && old_key[2] == key_bumps;
  }
  if (!target_cache_hit) {
    ComputeCachedTargets(centroid, slope, bumps, target_cache, 24,
                         shared_margin);
    ComputeCachedTargets(centroid, slope, bumps, target_cache + 24, 8,
                         shared_margin);
    if (cache_inputs_valid) {
      const uint32_t key[3] = {key_centroid, key_slope, key_bumps};
      __builtin_memcpy(target_cache_metadata, key, sizeof(key));
      target_cache_metadata[3] = 1.0f;
    } else {
      target_cache_metadata[3] = 0.0f;
    }
#if FM1_HARMONIC_CACHE_TEST
    ++harmonic_target_cache_misses;
#endif
  } else {
#if FM1_HARMONIC_CACHE_TEST
    ++harmonic_target_cache_hits;
#endif
  }
#if FM1_HARMONIC_CACHE_TEST
  harmonic_target_cache_test_hit = target_cache_hit;
  harmonic_target_cache_test_key[0] = key_centroid;
  harmonic_target_cache_test_key[1] = key_slope;
  harmonic_target_cache_test_key[2] = key_bumps;
#endif
#if FM1_HARMONIC_CACHE_TEST
#define FM1_HARMONIC_UPDATE_AMPLITUDES(c, s, b, a, indices, count, margin, offset) \
  do { \
    ++harmonic_target_cache_test_serial; \
    ApplyCachedTargets((a), (indices), (count), target_cache + (offset)); \
    UpdateAmplitudes((c), (s), (b), amplitude_reference + (offset), \
                     (indices), (count), (margin)); \
    for (size_t test_i = 0; test_i < (count); ++test_i) { \
      const int test_j = (indices)[test_i]; \
      const uint32_t target_actual_bits = HarmonicFloatBits(target_cache[(offset) + test_i]); \
      const uint32_t target_reference_bits = \
          HarmonicFloatBits(harmonic_target_cache_reference_targets[test_j]); \
      if (target_actual_bits != target_reference_bits) { \
        if (harmonic_target_cache_raw_target_mismatches == 0) { \
          harmonic_target_cache_first_raw_index = (offset) + test_i; \
          harmonic_target_cache_first_raw_actual = target_actual_bits; \
          harmonic_target_cache_first_raw_reference = target_reference_bits; \
        } \
        ++harmonic_target_cache_raw_target_mismatches; \
      } \
      if (HarmonicFloatBits((a)[test_j]) != \
          HarmonicFloatBits(amplitude_reference[(offset) + test_j])) { \
        if (harmonic_target_cache_state_mismatches == 0) { \
          harmonic_target_cache_first_index = (offset) + test_j; \
          harmonic_target_cache_first_actual = HarmonicFloatBits((a)[test_j]); \
          harmonic_target_cache_first_reference = \
              HarmonicFloatBits(amplitude_reference[(offset) + test_j]); \
          harmonic_target_cache_first_serial = harmonic_target_cache_test_serial; \
          harmonic_target_cache_first_hit = harmonic_target_cache_test_hit ? 1u : 0u; \
          for (size_t key_i = 0; key_i < 3; ++key_i) \
            harmonic_target_cache_first_key[key_i] = harmonic_target_cache_test_key[key_i]; \
        } \
        ++harmonic_target_cache_state_mismatches; \
      } \
    } \
  } while (0)
#else
#define FM1_HARMONIC_UPDATE_AMPLITUDES(c, s, b, a, indices, count, margin, offset) \
  ApplyCachedTargets((a), (indices), (count), target_cache + (offset))
#endif
#else
#define FM1_HARMONIC_UPDATE_AMPLITUDES(c, s, b, a, indices, count, margin, offset) \
  UpdateAmplitudes((c), (s), (b), (a), (indices), (count), (margin))
#endif
#if FM1_HARMONIC_TRIPLE_RECURRENCE
  FM1_HARMONIC_UPDATE_AMPLITUDES(
      centroid, slope, bumps, &amplitudes_[0], integer_harmonics, 24,
      shared_margin, 0);
  FM1_HARMONIC_UPDATE_AMPLITUDES(
      centroid, slope, bumps, &amplitudes_[24], organ_harmonics, 8,
      shared_margin, 24);

#if FM1_HARMONIC_TEMPORAL_RECURRENCE && FM1_HARMONIC_SHARED_RECURRENCE
  // Keep the physical triple path as the default. The temporal experiment
  // uses the existing paired low/AUX renderer and applies its bounded block
  // recurrence to the aligned upper bank; steep pitch moves still fall back
  // to the exact upper-bank renderer.
#if FM1_HARMONIC_CLENSHAW
  const bool clenshaw_rendered = FM1_HARMONIC_RENDER_TRIPLE(
          harmonic_oscillator_[0], harmonic_oscillator_[1],
          harmonic_oscillator_[2], f0,
          &amplitudes_[0], out,
          &amplitudes_[12], &amplitudes_[24], aux, size);
#if FM1_HARMONIC_CLENSHAW_COVERAGE
  fm1_harmonic_clenshaw_coverage_record(clenshaw_rendered ? 1 : 0);
#endif
  if (clenshaw_rendered) {
    return;
  }
#endif
  if (harmonic_oscillator_[0].HasSamePhaseAndFrequency(
          harmonic_oscillator_[1]) &&
      harmonic_oscillator_[0].RenderPair<1>(
          harmonic_oscillator_[2], f0,
          &amplitudes_[0], out, &amplitudes_[24], aux, size)) {
    harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);
  } else if (!FM1_HARMONIC_RENDER_TRIPLE(
          harmonic_oscillator_[0], harmonic_oscillator_[1],
          harmonic_oscillator_[2], f0,
          &amplitudes_[0], out,
          &amplitudes_[12], &amplitudes_[24], aux, size)) {
    harmonic_oscillator_[0].Render<1>(f0, &amplitudes_[0], out, size);
    harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);
    harmonic_oscillator_[2].Render<1>(f0, &amplitudes_[24], aux, size);
  }
#else
  if (!FM1_HARMONIC_RENDER_TRIPLE(
          harmonic_oscillator_[0], harmonic_oscillator_[1],
          harmonic_oscillator_[2], f0,
          &amplitudes_[0], out,
          &amplitudes_[12], &amplitudes_[24], aux, size)) {
    harmonic_oscillator_[0].Render<1>(f0, &amplitudes_[0], out, size);
    harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);
    harmonic_oscillator_[2].Render<1>(f0, &amplitudes_[24], aux, size);
  }
#endif
#elif FM1_HARMONIC_SHARED_RECURRENCE
  FM1_HARMONIC_UPDATE_AMPLITUDES(
      centroid, slope, bumps, &amplitudes_[0], integer_harmonics, 24,
      shared_margin, 0);
  FM1_HARMONIC_UPDATE_AMPLITUDES(
      centroid, slope, bumps, &amplitudes_[24], organ_harmonics, 8,
      shared_margin, 24);

  if (harmonic_oscillator_[0].RenderPair<1>(
          harmonic_oscillator_[2], f0,
          &amplitudes_[0], out, &amplitudes_[24], aux, size)) {
    harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);
  } else {
    harmonic_oscillator_[0].Render<1>(f0, &amplitudes_[0], out, size);
    harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);
    harmonic_oscillator_[2].Render<1>(f0, &amplitudes_[24], aux, size);
  }
#else
  FM1_HARMONIC_UPDATE_AMPLITUDES(
      centroid, slope, bumps, &amplitudes_[0], integer_harmonics, 24,
      shared_margin, 0);
  harmonic_oscillator_[0].Render<1>(f0, &amplitudes_[0], out, size);
  harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);

  FM1_HARMONIC_UPDATE_AMPLITUDES(
      centroid, slope, bumps, &amplitudes_[24], organ_harmonics, 8,
      shared_margin, 24);

  harmonic_oscillator_[2].Render<1>(f0, &amplitudes_[24], aux, size);
#endif
#undef FM1_HARMONIC_UPDATE_AMPLITUDES
}

}  // namespace plaits
#if FM1_HARMONIC_TARGET_AMPLITUDE_CACHE && FM1_HARMONIC_CACHE_TEST
extern "C" void fm1_harmonic_target_cache_test_stats(
    uint32_t* hits, uint32_t* misses, uint32_t* unavailable,
    uint32_t* state_mismatches) {
  if (hits) *hits = plaits::harmonic_target_cache_hits;
  if (misses) *misses = plaits::harmonic_target_cache_misses;
  if (unavailable) *unavailable = plaits::harmonic_target_cache_unavailable;
  if (state_mismatches) *state_mismatches = plaits::harmonic_target_cache_state_mismatches;
  fprintf(stderr, "harmonic_cache_raw_target_mismatches=%u\n",
          plaits::harmonic_target_cache_raw_target_mismatches);
  fprintf(stderr, "first_cache_state_mismatch index=%u actual=%08x reference=%08x serial=%u hit=%u key=%08x,%08x,%08x\n",
          plaits::harmonic_target_cache_first_index,
          plaits::harmonic_target_cache_first_actual,
          plaits::harmonic_target_cache_first_reference,
          plaits::harmonic_target_cache_first_serial,
          plaits::harmonic_target_cache_first_hit,
          plaits::harmonic_target_cache_first_key[0],
          plaits::harmonic_target_cache_first_key[1],
          plaits::harmonic_target_cache_first_key[2]);
  fprintf(stderr, "first_raw_target_mismatch index=%u actual=%08x reference=%08x count=%u\n",
          plaits::harmonic_target_cache_first_raw_index,
          plaits::harmonic_target_cache_first_raw_actual,
          plaits::harmonic_target_cache_first_raw_reference,
          plaits::harmonic_target_cache_raw_target_mismatches);
}
#endif
