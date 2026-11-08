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

#ifndef PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_
#define PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_

#include "plaits_config.h"

#ifndef FM1_MODAL_EXACT_CACHE
#define FM1_MODAL_EXACT_CACHE 0
#endif

#ifndef FM1_MODAL_BOUNDED_CACHE
#define FM1_MODAL_BOUNDED_CACHE 1
#endif

// Store the four cached TPT coefficients per mode in one compact record.
// This keeps cache size unchanged while reducing mode-first pointer setup.
#ifndef FM1_MODAL_CACHE_AOS
#define FM1_MODAL_CACHE_AOS 1
#endif

// Keep the two prior input samples in scalar registers in the persistent
// cache-hit loop, avoiding its per-sample lag-history branch and second load.
// The default preserves the original indexed-history path.
#ifndef FM1_MODAL_STREAMED_INPUT_HISTORY
#define FM1_MODAL_STREAMED_INPUT_HISTORY 1
#endif
#if FM1_MODAL_STREAMED_INPUT_HISTORY != 0 && \
    FM1_MODAL_STREAMED_INPUT_HISTORY != 1
#error "FM1_MODAL_STREAMED_INPUT_HISTORY must be 0 or 1"
#endif

#ifndef FM1_MODAL_CUBIC_TAN_MAX_HZ
#define FM1_MODAL_CUBIC_TAN_MAX_HZ 4000
#endif

#ifndef FM1_MODAL_MULTIRATE_LOW_MODES
#define FM1_MODAL_MULTIRATE_LOW_MODES 0
#endif

#ifndef FM1_MODAL_MULTIRATE_LOW_MODE_CUTOFF_HZ
#define FM1_MODAL_MULTIRATE_LOW_MODE_CUTOFF_HZ 4000
#endif

// Experimental alternative: square the full-rate poles, then solve for the
// equivalent half-rate TPT coefficients. The default retains tan doubling.
#ifndef FM1_MODAL_RATE2_EXACT_POLES
#define FM1_MODAL_RATE2_EXACT_POLES 1
#endif

// Experimental transition policy: on a coefficient-cache miss, process the
// block at the native source rate and prepare half-rate coefficients for a
// later exact cache hit. This keeps fast-changing controls at native cadence.
#ifndef FM1_MODAL_RATE2_STABLE_CACHE_ONLY
#define FM1_MODAL_RATE2_STABLE_CACHE_ONLY 0
#endif

#ifndef FM1_MODAL_RATE2_REUSE_VALIDATED_CACHE
#define FM1_MODAL_RATE2_REUSE_VALIDATED_CACHE 0
#endif

#if FM1_MODAL_RATE2_REUSE_VALIDATED_CACHE && \
    !FM1_MODAL_RATE2_STABLE_CACHE_ONLY
#error "Validated rate-2 reuse requires stable-cache-only mode"
#endif

#if FM1_MODAL_RATE2_STABLE_CACHE_ONLY && \
    (!FM1_MODAL_MULTIRATE_LOW_MODES || !FM1_MODAL_CACHE_AOS || \
     !(FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE))
#error "Stable-cache Modal rate 2 requires multirate mode and AoS coefficient caching"
#endif

#ifndef FM1_MODAL_MODE_FIRST
#define FM1_MODAL_MODE_FIRST 1
#endif

#ifndef FM1_MODAL_PAIRED_DIRECT_FORM
#define FM1_MODAL_PAIRED_DIRECT_FORM 1
#endif

#ifndef FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
#define FM1_MODAL_MODE_FIRST_DIRECT_ACCUM 1
#endif

// Experimental second-order direct-form resonator. The transfer function is
// algebraically equivalent to the TPT SVF, but uses a shorter recurrence.
/* Move only the call-free direct recurrence to the standard executable bank. */
#ifndef FM1_MODAL_DF_KERNEL_RAM
#define FM1_MODAL_DF_KERNEL_RAM 1
#endif
#if FM1_MODAL_DF_KERNEL_RAM && defined(FM1_DEVICE)
#define FM1_MODAL_DF_KERNEL_CODE __attribute__((noinline, section(".ram_audio.modal_kernel")))
#else
#define FM1_MODAL_DF_KERNEL_CODE __attribute__((noinline))
#endif

#ifndef FM1_MODAL_DIRECT_FORM
#define FM1_MODAL_DIRECT_FORM 1
#endif

// Use the direct recurrence only in a bounded stable region. Outside
// it, retain the original TPT state-variable implementation. Keep the
// existing TPT/DF state remapping on coefficient changes and mode transitions.
#ifndef FM1_MODAL_HYBRID_DIRECT_FORM
#define FM1_MODAL_HYBRID_DIRECT_FORM 1
#endif
#ifndef FM1_MODAL_HYBRID_RECURRENCE_COEFF_CACHE
#define FM1_MODAL_HYBRID_RECURRENCE_COEFF_CACHE 1
#endif
#ifndef FM1_MODAL_HYBRID_DEFER_TPT_SYNC
#define FM1_MODAL_HYBRID_DEFER_TPT_SYNC 1
#endif
#ifndef FM1_MODAL_HYBRID_DIRECT_FORM_MIN_HZ
#define FM1_MODAL_HYBRID_DIRECT_FORM_MIN_HZ 100.0f
#endif
#ifndef FM1_MODAL_HYBRID_DIRECT_FORM_MAX_TAN
#define FM1_MODAL_HYBRID_DIRECT_FORM_MAX_TAN 6.0f
#endif
#ifndef FM1_MODAL_HYBRID_DIRECT_FORM_MAX_Q
#define FM1_MODAL_HYBRID_DIRECT_FORM_MAX_Q 2048.0f
#endif
#ifndef FM1_MODAL_HYBRID_DIRECT_FORM_MIN_TAN_R
#define FM1_MODAL_HYBRID_DIRECT_FORM_MIN_TAN_R 1.0e-5f
#endif


// The multirate path can use the existing bounded coefficient cache and
// paired direct-form kernel; it does not depend on the legacy TPT-only path.

#if FM1_MODAL_HYBRID_DIRECT_FORM && !FM1_MODAL_DIRECT_FORM
#error "The hybrid modal direct-form path requires the direct-form prototype"
#endif
#if FM1_MODAL_HYBRID_DEFER_TPT_SYNC && !FM1_MODAL_HYBRID_DIRECT_FORM
#error "Deferred TPT synchronization requires the hybrid direct-form path"
#endif
#if FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT && \
    (!FM1_MODAL_HYBRID_DIRECT_FORM || \
     !FM1_MODAL_HYBRID_RECURRENCE_COEFF_CACHE || \
     !(FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE))
#error "Persistent modal float cache-hit path requires hybrid direct form and coefficient caching"
#endif


#if FM1_MODAL_MODE_FIRST_DIRECT_ACCUM && !FM1_MODAL_MODE_FIRST
#error "Direct accumulation requires mode-first modal processing"
#endif

#if FM1_MODAL_CACHE_AOS && \
    !(FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE)
#error "Modal cache AoS requires the exact or bounded coefficient cache"
#endif

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE || \
    FM1_MODAL_MULTIRATE_LOW_MODES
#include <stdint.h>
#endif

#include "stmlib/dsp/filter.h"
#include "plaits/dsp/dsp.h"

namespace plaits {

#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
extern uint32_t fm1_modal_test_cache_blocks;
extern uint32_t fm1_modal_test_low_rate_cache_dispatches;
extern uint32_t fm1_modal_test_low_rate_cache_fallbacks;
extern uint32_t fm1_modal_test_low_rate_cache_misses;
extern uint32_t fm1_modal_test_persistent_direct_cache_hits;
extern uint64_t fm1_modal_test_mode_input_frames;
extern uint64_t fm1_modal_test_mode_filter_samples;
extern uint64_t fm1_modal_test_mode_batches;
extern uint64_t fm1_modal_test_low_rate_batches;
extern uint64_t fm1_modal_test_low_rate_covered_frames;
#endif

const int kMaxNumModes = 24;
const int kModeBatchSize = 4;

struct ModalCachedCoefficients {
  float g;
  float r_plus_g;
  float h;
  float gain;
};

static_assert(sizeof(ModalCachedCoefficients) == 4 * sizeof(float),
              "modal AoS cache must preserve the existing 4-float stride");

// We render 4 modes simultaneously since there are enough registers to hold
// all state variables.
template<int batch_size>
class ResonatorSvf {
 public:
  ResonatorSvf() { }
  ~ResonatorSvf() { }
  
  void Init() {
    for (int i = 0; i < batch_size; ++i) {
      state_1_[i] = state_2_[i] = 0.0f;
#if FM1_MODAL_MULTIRATE_LOW_MODES
      last_hp_[i] = last_bp_[i] = last_lp_[i] = 0.0f;
      last_frequency_[i] = last_q_[i] = 0.0f;
#endif
    }
#if FM1_MODAL_DIRECT_FORM
    input_x1_ = input_x2_ = 0.0f;
#if FM1_MODAL_MULTIRATE_LOW_MODES
    rate2_input_x1_ = rate2_input_x2_ = 0.0f;
#endif
    for (int i = 0; i < batch_size; ++i) {
      direct_y1_[i] = direct_y2_[i] = 0.0f;
      direct_state_valid_[i] = false;
      direct_transform_valid_[i] = false;
#if FM1_MODAL_MULTIRATE_LOW_MODES
      direct_low_rate_[i] = false;
      rate2_coeff_valid_[i] = false;
      rate2_g_[i] = rate2_h_[i] = 0.0f;
      rate2_r_plus_g_[i] = 0.0f;
      rate2_b0_[i] = rate2_a1_[i] = rate2_a2_[i] = 0.0f;
#endif
    }
#endif
#if FM1_MODAL_MULTIRATE_LOW_MODES
    rate_state_valid_ = false;
    last_low_rate_ = false;
#endif
  }

  template<stmlib::FilterMode mode, bool add>
  void Process(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size
#if FM1_MODAL_MULTIRATE_LOW_MODES
      , bool low_rate = false
#endif
      ) {
    ProcessInternal<mode, add, false, false>(
        f, q, gain, in, out, size, NULL, NULL, NULL, NULL
#if FM1_MODAL_MULTIRATE_LOW_MODES
        , low_rate
#endif
        );
  }

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
  template<stmlib::FilterMode mode, bool add>
  void ProcessAndCache(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size,
      float* cached_g,
      float* cached_r_plus_g,
      float* cached_h,
      float* cached_gain
#if FM1_MODAL_MULTIRATE_LOW_MODES
      , bool low_rate = false
#endif
      ) {
    ProcessInternal<mode, add, true, false>(
        f, q, gain, in, out, size,
        cached_g, cached_r_plus_g, cached_h, cached_gain
#if FM1_MODAL_MULTIRATE_LOW_MODES
        , low_rate
#endif
        );
  }

  template<stmlib::FilterMode mode, bool add>
  void ProcessCached(
      float* cached_g,
      float* cached_r_plus_g,
      float* cached_h,
      float* cached_gain,
      const float* in,
      float* out,
      size_t size
#if FM1_MODAL_MULTIRATE_LOW_MODES
      , bool low_rate = false
#endif
      ) {
#if FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT
#if FM1_MODAL_MULTIRATE_LOW_MODES
    if (!low_rate && batch_size == kModeBatchSize &&
        size <= kMaxBlockSize &&
        TryProcessCachedDirectForm<mode, add>(
            cached_gain, in, out, size)) return;
#else
    if (batch_size == kModeBatchSize && size <= kMaxBlockSize &&
        TryProcessCachedDirectForm<mode, add>(
            cached_gain, in, out, size)) return;
#endif
#endif
    ProcessInternal<mode, add, false, true>(
        NULL, NULL, NULL, in, out, size,
        cached_g, cached_r_plus_g, cached_h, cached_gain
#if FM1_MODAL_MULTIRATE_LOW_MODES
        , low_rate
#endif
        );
  }
#if FM1_MODAL_CACHE_AOS
  template<stmlib::FilterMode mode, bool add>
  void ProcessAndCache(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size,
      ModalCachedCoefficients* cached
#if FM1_MODAL_MULTIRATE_LOW_MODES
      , bool low_rate = false
#endif
      ) {
    float cached_g[batch_size];
    float cached_r_plus_g[batch_size];
    float cached_h[batch_size];
    float cached_gain[batch_size];
    ProcessInternal<mode, add, true, false>(
        f, q, gain, in, out, size,
        cached_g, cached_r_plus_g, cached_h, cached_gain
#if FM1_MODAL_MULTIRATE_LOW_MODES
        , low_rate
#endif
        );
    for (int i = 0; i < batch_size; ++i) {
      cached[i].g = cached_g[i];
      cached[i].r_plus_g = cached_r_plus_g[i];
      cached[i].h = cached_h[i];
      cached[i].gain = cached_gain[i];
    }
  }

  template<stmlib::FilterMode mode, bool add>
  void ProcessCached(
      const ModalCachedCoefficients* cached,
      const float* in,
      float* out,
      size_t size
#if FM1_MODAL_MULTIRATE_LOW_MODES
      , bool low_rate = false
#endif
      ) {
#if FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT
#if FM1_MODAL_MULTIRATE_LOW_MODES
    if (!low_rate && batch_size == kModeBatchSize &&
        size <= kMaxBlockSize &&
        TryProcessCachedDirectForm<mode, add>(cached, in, out, size)) return;
#else
    if (batch_size == kModeBatchSize && size <= kMaxBlockSize &&
        TryProcessCachedDirectForm<mode, add>(cached, in, out, size)) return;
#endif
#endif
#if FM1_MODAL_MODE_FIRST && !FM1_MODAL_DIRECT_FORM
    if (batch_size == kModeBatchSize && size <= kMaxBlockSize &&
        (!FM1_MODAL_MODE_FIRST_DIRECT_ACCUM || in != out)) {
      ProcessModeFirstPacked<mode, add>(cached, in, out, size);
      return;
    }
#endif
    float cached_g[batch_size];
    float cached_r_plus_g[batch_size];
    float cached_h[batch_size];
    float cached_gain[batch_size];
    for (int i = 0; i < batch_size; ++i) {
      cached_g[i] = cached[i].g;
      cached_r_plus_g[i] = cached[i].r_plus_g;
      cached_h[i] = cached[i].h;
      cached_gain[i] = cached[i].gain;
    }
    ProcessInternal<mode, add, false, true>(
        NULL, NULL, NULL, in, out, size,
        cached_g, cached_r_plus_g, cached_h, cached_gain
#if FM1_MODAL_MULTIRATE_LOW_MODES
        , low_rate
#endif
        );
  }
#endif
#endif

 private:
#if FM1_MODAL_DIRECT_FORM
  struct DirectStateTransform {
    float m0;
    float m1;
    float n0;
    float n1;
    float alpha;
    float beta;
    float determinant_inverse;
  };

#ifndef FM1_MODAL_COMPACT_STATE_TRANSFORM
#define FM1_MODAL_COMPACT_STATE_TRANSFORM 1
#endif

  bool ComputeDirectStateTransform(
      float g,
      float r_plus_g,
      float h,
      DirectStateTransform* transform) const {
#if FM1_MODAL_COMPACT_STATE_TRANSFORM
    // Closed-form backward observability map of the same TPT bandpass.
    // This only changes transform construction; the TPT fallback remains.
    const float r = r_plus_g - g;
    const float gg = g * g;
    const float rg = r * g;
    const float e = 1.0f - rg + gg;
    if (!__builtin_isfinite(e) || !__builtin_isfinite(g) ||
        g <= 0.0f || __builtin_fabsf(e * h) < 1.0e-12f)
      return false;
    const float inv_e = 1.0f / e;
    const float inv_e2 = inv_e * inv_e;
    const float determinant = 2.0f * g * inv_e2;
    if (!__builtin_isfinite(determinant) ||
        __builtin_fabsf(determinant) < 1.0e-7f)
      return false;
    transform->m0 = inv_e;
    transform->m1 = g * inv_e;
    transform->n0 = (1.0f + rg - 3.0f * gg) * inv_e2;
    transform->n1 = g * (3.0f - rg - gg) * inv_e2;
    transform->alpha = -g * inv_e;
    transform->beta = -2.0f * g * (1.0f - gg) * inv_e2;
    transform->determinant_inverse = 1.0f / determinant;
    return __builtin_isfinite(transform->determinant_inverse);
#else
    const float a00 = 1.0f - 2.0f * g * h * r_plus_g;
    const float a01 = -2.0f * g * h;
    const float a10 = 2.0f * g * (1.0f - g * h * r_plus_g);
    const float a11 = 1.0f - 2.0f * g * g * h;
    const float determinant_a = a00 * a11 - a01 * a10;
    if (__builtin_fabsf(determinant_a) < 1.0e-12f) {
      return false;
    }
    const float inverse_a = 1.0f / determinant_a;
    const float c0 = 1.0f - g * h * r_plus_g;
    const float c1 = -g * h;
    const float d = g * h;
    const float b0 = 2.0f * g * h;
    const float b1 = 2.0f * g * g * h;
    const float m0 = (c0 * a11 - c1 * a10) * inverse_a;
    const float m1 = (-c0 * a01 + c1 * a00) * inverse_a;
    const float n0 = (m0 * a11 - m1 * a10) * inverse_a;
    const float n1 = (-m0 * a01 + m1 * a00) * inverse_a;
    const float alpha = d - (m0 * b0 + m1 * b1);
    const float beta = -(n0 * b0 + n1 * b1);
    const float determinant_observability = m0 * n1 - m1 * n0;
    if (!__builtin_isfinite(determinant_observability) ||
        __builtin_fabsf(determinant_observability) < 1.0e-7f) {
      return false;
    }
    transform->m0 = m0;
    transform->m1 = m1;
    transform->n0 = n0;
    transform->n1 = n1;
    transform->alpha = alpha;
    transform->beta = beta;
    transform->determinant_inverse = 1.0f / determinant_observability;
    return __builtin_isfinite(transform->determinant_inverse);
#endif
  }

  void TptStateToDirectState(
      const DirectStateTransform& transform,
      float state_1,
      float state_2,
      float input_x1,
      float input_x2,
      float* output_y1,
      float* output_y2) const {
    *output_y1 = transform.m0 * state_1 + transform.m1 * state_2 +
        transform.alpha * input_x1;
    *output_y2 = transform.n0 * state_1 + transform.n1 * state_2 +
        transform.beta * input_x1 + transform.alpha * input_x2;
  }

  void DirectStateToTptState(
      const DirectStateTransform& transform,
      float output_y1,
      float output_y2,
      float input_x1,
      float input_x2,
      float* state_1,
      float* state_2) const {
    const float z1 = output_y1 - transform.alpha * input_x1;
    const float z2 = output_y2 - transform.beta * input_x1 -
        transform.alpha * input_x2;
    *state_1 = (z1 * transform.n1 - transform.m1 * z2) *
        transform.determinant_inverse;
    *state_2 = (transform.m0 * z2 - z1 * transform.n0) *
        transform.determinant_inverse;
  }

#if FM1_MODAL_MULTIRATE_LOW_MODES && FM1_MODAL_DIRECT_FORM && \
    FM1_MODAL_HYBRID_DIRECT_FORM
 public:
  bool BuildRate2Coefficients(
      float full_g, float full_r_plus_g, float full_h,
      float* g, float* r_plus_g, float* h,
      float* b0, float* a1, float* a2) const {
    const float full_g_squared = full_g * full_g;
    const float full_common =
        2.0f * full_h * full_g * full_r_plus_g;
    const float full_doubled_term = 2.0f * full_h * full_g_squared;
    const float full_a1 = -2.0f + full_common + full_doubled_term;
    const float full_a2 = 1.0f - full_common + full_doubled_term;
#if FM1_MODAL_RATE2_EXACT_POLES
    // The denominator poles at half the source rate are exact squares of the
    // full-rate poles. Solve the TPT parameters from those coefficients.
    const float rate2_a1 = 2.0f * full_a2 - full_a1 * full_a1;
    const float rate2_a2 = full_a2 * full_a2;
    const float hg2 = 0.25f * (1.0f + rate2_a1 + rate2_a2);
    const float hgr = 0.5f * (1.0f - rate2_a2);
    const float solved_h = 1.0f - hg2 - hgr;
    if (!(hg2 > 0.0f) || !(solved_h > 0.0f) ||
        !__builtin_isfinite(rate2_a1) ||
        !__builtin_isfinite(rate2_a2) ||
        !__builtin_isfinite(solved_h)) return false;
    const float solved_g = __builtin_sqrtf(hg2 / solved_h);
    if (!(solved_g > 0.0f) || !__builtin_isfinite(solved_g)) return false;
    const float solved_r = hgr / (solved_h * solved_g);
    *g = solved_g;
    *r_plus_g = solved_r + solved_g;
    *h = solved_h;
    *b0 = solved_g * solved_h;
    *a1 = rate2_a1;
    *a2 = rate2_a2;
#else
    // A doubled tan(pi*f) gives the usual frequency retiming at half rate.
    const float denominator = 1.0f - full_g_squared;
    if (!__builtin_isfinite(denominator) ||
        __builtin_fabsf(denominator) < 1.0e-6f) return false;
    const float doubled_g = 2.0f * full_g / denominator;
    const float reciprocal_q = full_r_plus_g - full_g;
    const float doubled_h = stmlib::PositiveReciprocal(
        1.0f + reciprocal_q * doubled_g + doubled_g * doubled_g);
    const float doubled_g_squared = doubled_g * doubled_g;
    const float common = 2.0f * doubled_h * doubled_g *
        (reciprocal_q + doubled_g);
    const float doubled_term = 2.0f * doubled_h * doubled_g_squared;
    *g = doubled_g;
    *r_plus_g = reciprocal_q + doubled_g;
    *h = doubled_h;
    *b0 = doubled_g * doubled_h;
    *a1 = -2.0f + common + doubled_term;
    *a2 = 1.0f - common + doubled_term;
#endif
    return __builtin_isfinite(*g) && __builtin_isfinite(*r_plus_g) &&
        __builtin_isfinite(*h) && __builtin_isfinite(*b0) &&
        __builtin_isfinite(*a1) && __builtin_isfinite(*a2);
  }

#if FM1_MODAL_RATE2_STABLE_CACHE_ONLY
  void BuildRate2Cache(const ModalCachedCoefficients* full_rate) {
    for (int i = 0; i < batch_size; ++i) {
      rate2_coeff_valid_[i] = BuildRate2Coefficients(
          full_rate[i].g, full_rate[i].r_plus_g, full_rate[i].h,
          &rate2_g_[i], &rate2_r_plus_g_[i], &rate2_h_[i],
          &rate2_b0_[i], &rate2_a1_[i], &rate2_a2_[i]);
    }
  }
#endif

  // A cache miss can enter the half-rate path only when every mode in the
  // batch has a stable direct-form rate-2 realization and finite state map.
  // Otherwise the caller keeps the whole batch on the original full-rate path.
  bool CanBuildRate2(const float* f, const float* q) {
    const float minimum_tan =
        1.01f * M_PI_F * FM1_MODAL_HYBRID_DIRECT_FORM_MIN_HZ / kSampleRate;
    for (int i = 0; i < batch_size; ++i) {
      float full_g;
#if FM1_MODAL_CUBIC_TAN_MAX_HZ > 0
      if (batch_size == kModeBatchSize &&
          f[i] < static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) /
              kSampleRate) {
        const float f2 = f[i] * f[i];
        const float f_max =
            static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) / kSampleRate;
        const float cubic = (M_PI_POW_3 / 3.0f) +
            0.88f * (2.0f * M_PI_POW_5 / 15.0f) * f_max * f_max;
        full_g = f[i] * (M_PI_F + f2 * cubic);
      } else {
        full_g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
      }
#else
      full_g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
#endif
      const float reciprocal_q = stmlib::PositiveReciprocal(q[i]);
      if (!__builtin_isfinite(full_g) ||
          !__builtin_isfinite(reciprocal_q)) return false;
      const float full_h = stmlib::PositiveReciprocal(
          1.0f + reciprocal_q * full_g + full_g * full_g);
      const float full_r_plus_g = reciprocal_q + full_g;
      float g;
      float r_plus_g;
      float h;
      float b0;
      float a1;
      float a2;
      if (!BuildRate2Coefficients(
              full_g, full_r_plus_g, full_h,
              &g, &r_plus_g, &h, &b0, &a1, &a2)) return false;
      const float rate2_reciprocal_q = r_plus_g - g;
      if (g < minimum_tan ||
          g > FM1_MODAL_HYBRID_DIRECT_FORM_MAX_TAN ||
          rate2_reciprocal_q <
              1.02f / FM1_MODAL_HYBRID_DIRECT_FORM_MAX_Q ||
          g * rate2_reciprocal_q <
              FM1_MODAL_HYBRID_DIRECT_FORM_MIN_TAN_R ||
          !__builtin_isfinite(h)) return false;

      DirectStateTransform next_transform;
      if (!ComputeDirectStateTransform(g, r_plus_g, h, &next_transform))
        return false;
      if (direct_state_valid_[i]) {
        if (!direct_transform_valid_[i]) return false;
        const float old_x1 = direct_low_rate_[i]
            ? rate2_input_x1_ : input_x1_;
        const float old_x2 = direct_low_rate_[i]
            ? rate2_input_x2_ : input_x2_;
        float tpt_1;
        float tpt_2;
        float y1;
        float y2;
        DirectStateToTptState(
            direct_transform_[i], direct_y1_[i], direct_y2_[i],
            old_x1, old_x2, &tpt_1, &tpt_2);
        TptStateToDirectState(
            next_transform, tpt_1, tpt_2,
            rate2_input_x1_, rate2_input_x2_, &y1, &y2);
        if (!__builtin_isfinite(tpt_1) || !__builtin_isfinite(tpt_2) ||
            !__builtin_isfinite(y1) || !__builtin_isfinite(y2)) return false;
      } else {
        if (!__builtin_isfinite(state_1_[i]) ||
            !__builtin_isfinite(state_2_[i])) return false;
        float y1;
        float y2;
        TptStateToDirectState(
            next_transform, state_1_[i], state_2_[i],
            rate2_input_x1_, rate2_input_x2_, &y1, &y2);
        if (!__builtin_isfinite(y1) || !__builtin_isfinite(y2)) return false;
      }
#if FM1_MODAL_RATE2_REUSE_VALIDATED_CACHE
      rate2_g_[i] = g;
      rate2_r_plus_g_[i] = r_plus_g;
      rate2_h_[i] = h;
      rate2_b0_[i] = b0;
      rate2_a1_[i] = a1;
      rate2_a2_[i] = a2;
      rate2_coeff_valid_[i] = true;
#endif
    }
    return true;
  }

  bool CanUseRate2() const {
    for (int i = 0; i < batch_size; ++i) {
      if (!rate2_coeff_valid_[i]) return false;
      if (direct_state_valid_[i] && direct_transform_valid_[i] &&
          direct_low_rate_[i]) continue;
      const float g = rate2_g_[i];
      const float r_plus_g = rate2_r_plus_g_[i];
      const float h = rate2_h_[i];
      DirectStateTransform next_transform;
      if (!ComputeDirectStateTransform(g, r_plus_g, h, &next_transform))
        return false;
      if (direct_state_valid_[i] && !direct_transform_valid_[i]) return false;
      if (direct_state_valid_[i] && !direct_low_rate_[i]) {
        float tpt_1;
        float tpt_2;
        float y1;
        float y2;
        DirectStateToTptState(
            direct_transform_[i], direct_y1_[i], direct_y2_[i],
            input_x1_, input_x2_, &tpt_1, &tpt_2);
        TptStateToDirectState(
            next_transform, tpt_1, tpt_2,
            rate2_input_x1_, rate2_input_x2_, &y1, &y2);
        if (!__builtin_isfinite(tpt_1) || !__builtin_isfinite(tpt_2) ||
            !__builtin_isfinite(y1) || !__builtin_isfinite(y2)) return false;
      } else if (!direct_state_valid_[i]) {
        float y1;
        float y2;
        if (!__builtin_isfinite(state_1_[i]) ||
            !__builtin_isfinite(state_2_[i])) return false;
        TptStateToDirectState(
            next_transform, state_1_[i], state_2_[i],
            rate2_input_x1_, rate2_input_x2_, &y1, &y2);
        if (!__builtin_isfinite(y1) || !__builtin_isfinite(y2)) return false;
      }
    }
    return true;
  }

  void UpdateOtherInputHistory(
      bool active_low_rate, const float* other_input, size_t size) {
    if (!size) return;
    float* x1 = active_low_rate ? &input_x1_ : &rate2_input_x1_;
    float* x2 = active_low_rate ? &input_x2_ : &rate2_input_x2_;
    const float last1 = other_input[size - 1];
    const float last2 = size > 1 ? other_input[size - 2] : *x1;
    if (size > 1) {
      *x2 = last2;
      *x1 = last1;
    } else {
      *x2 = *x1;
      *x1 = last1;
    }
  }
 private:
#endif

  template<stmlib::FilterMode mode, bool add, bool capture, bool cached>
  void ProcessDirectForm(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size,
      float* cached_g,
      float* cached_r_plus_g,
      float* cached_h,
      float* cached_gain,
      float input_last1,
      float input_last2) {
    for (int i = 0; i < batch_size; ++i) {
      float g;
      float r_plus_g;
      float h;
      float mode_gain;
      if (cached) {
        // The coefficient cache continues to hold the TPT coefficients so
        // the ordinary implementation remains a safe fallback.
        g = cached_g[i];
        r_plus_g = cached_r_plus_g[i];
        h = cached_h[i];
        mode_gain = cached_gain[i];
      } else {
#if FM1_MODAL_CUBIC_TAN_MAX_HZ > 0
        if (batch_size == kModeBatchSize &&
            f[i] < static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) /
                kSampleRate) {
          const float f2 = f[i] * f[i];
          const float f_max =
              static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) / kSampleRate;
          const float cubic = (M_PI_POW_3 / 3.0f) +
              0.88f * (2.0f * M_PI_POW_5 / 15.0f) * f_max * f_max;
          g = f[i] * (M_PI_F + f2 * cubic);
        } else {
          g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
        }
#else
        g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
#endif
        const float r = stmlib::PositiveReciprocal(q[i]);
        h = stmlib::PositiveReciprocal(1.0f + r * g + g * g);
        r_plus_g = r + g;
        mode_gain = gain[i];
      }

      if (!cached || !direct_transform_valid_[i]) {
        direct_transform_valid_[i] = ComputeDirectStateTransform(
            g, r_plus_g, h, &direct_transform_[i]);
      }

      if (!direct_transform_valid_[i]) {
        float state_1 = state_1_[i];
        float state_2 = state_2_[i];
        for (size_t j = 0; j < size; ++j) {
          const float hp = (in[j] - r_plus_g * state_1 - state_2) * h;
          const float g_hp = g * hp;
          const float bp = g_hp + state_1;
          state_1 = g_hp + bp;
          const float g_bp = g * bp;
          const float lp = g_bp + state_2;
          state_2 = g_bp + lp;
          const float contribution = mode_gain *
              ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
          if (add || i > 0) {
            out[j] += contribution;
          } else {
            out[j] = contribution;
          }
        }
#if STMLIB_FLUSH_TINY_FILTER_STATES
        state_1_[i] = __builtin_fabsf(state_1) < 1.0e-20f
            ? 0.0f : state_1;
        state_2_[i] = __builtin_fabsf(state_2) < 1.0e-20f
            ? 0.0f : state_2;
#else
        state_1_[i] = state_1;
        state_2_[i] = state_2;
#endif
        direct_state_valid_[i] = false;
        if (capture) {
          cached_g[i] = g;
          cached_r_plus_g[i] = r_plus_g;
          cached_h[i] = h;
          cached_gain[i] = mode_gain;
        }
        continue;
      }

      if (!cached || !direct_state_valid_[i]) {
        TptStateToDirectState(
            direct_transform_[i], state_1_[i], state_2_[i],
            input_x1_, input_x2_, &direct_y1_[i], &direct_y2_[i]);
      }

      const float g2 = g * g;
      const float b0 = g * h;
      const float common = 2.0f * h * g * r_plus_g;
      const float doubled_g2 = 2.0f * h * g2;
      const float a1 = -2.0f + common + doubled_g2;
      const float a2 = 1.0f - common + doubled_g2;
      float y1 = direct_y1_[i];
      float y2 = direct_y2_[i];
      for (size_t j = 0; j < size; ++j) {
        const float x2 = j >= 2 ? in[j - 2]
            : (j == 1 ? input_x1_ : input_x2_);
        const float y = b0 * (in[j] - x2) - a1 * y1 - a2 * y2;
        const float contribution = mode_gain * y;
#if FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
        if (add || i > 0) {
          out[j] += contribution;
        } else {
          out[j] = contribution;
        }
#else
        if (add) {
          out[j] += contribution;
        } else if (i == 0) {
          out[j] = contribution;
        } else {
          out[j] += contribution;
        }
#endif
        y2 = y1;
        y1 = y;
      }
#if STMLIB_FLUSH_TINY_FILTER_STATES
      y1 = __builtin_fabsf(y1) < 1.0e-20f ? 0.0f : y1;
      y2 = __builtin_fabsf(y2) < 1.0e-20f ? 0.0f : y2;
#endif
      direct_y1_[i] = y1;
      direct_y2_[i] = y2;
      DirectStateToTptState(
          direct_transform_[i], y1, y2, input_last1, input_last2,
          &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
          ? 0.0f : state_1_[i];
      state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
          ? 0.0f : state_2_[i];
#endif
      direct_state_valid_[i] = true;
      if (capture) {
        cached_g[i] = g;
        cached_r_plus_g[i] = r_plus_g;
        cached_h[i] = h;
        cached_gain[i] = mode_gain;
      }
    }
  }
#if FM1_MODAL_HYBRID_DIRECT_FORM
#if FM1_MODAL_DF_KERNEL_RAM
  struct DirectKernelState {
    float b0, a1, a2, gain, input_x1, input_x2, y1, y2;
  };
  template<bool append>
  static FM1_MODAL_DF_KERNEL_CODE void RenderDirectKernel(
      const float* in, float* out, size_t size, DirectKernelState* state) {
    const float b0 = state->b0, a1 = state->a1, a2 = state->a2;
    const float mode_gain = state->gain;
    const float input_x1 = state->input_x1, input_x2 = state->input_x2;
    float y1 = state->y1, y2 = state->y2;
    for (size_t j = 0; j < size; ++j) {
      const float x2 = j >= 2 ? in[j - 2]
          : (j == 1 ? input_x1 : input_x2);
      const float y = b0 * (in[j] - x2) - a1 * y1 - a2 * y2;
      const float contribution = mode_gain * y;
      if (append) out[j] += contribution;
      else out[j] = contribution;
      y2 = y1;
      y1 = y;
    }
    state->y1 = y1;
    state->y2 = y2;
  }
#endif
  struct HybridModeWork {
    float g;
    float r_plus_g;
    float h;
    float mode_gain;
    float cache_g;
    float cache_r_plus_g;
    float cache_h;
    float b0;
    float a1;
    float a2;
    float y1;
    float y2;
    float tpt_state_1;
    float tpt_state_2;
    bool use_direct;
  };
#if FM1_MODAL_DF_KERNEL_RAM
  /* Keep the existing paired recurrence intact while placing only its
   * sample loop in the executable RAM bank. Preparation and state handoff
   * remain in flash; this leaf is called once per direct-form mode pair. */
  template<bool add>
  static FM1_MODAL_DF_KERNEL_CODE void RenderPairedDirectKernel(
      const float* in, float* out, size_t size,
      HybridModeWork* a, HybridModeWork* b, int i,
      float input_prev1, float input_prev2) {
    const float b0a = a->b0, a1a = a->a1, a2a = a->a2;
    const float gain_a = a->mode_gain;
    const float b0b = b->b0, a1b = b->a1, a2b = b->a2;
    const float gain_b = b->mode_gain;
    float y1a = a->y1, y2a = a->y2;
    float y1b = b->y1, y2b = b->y2;
    for (size_t j = 0; j < size; ++j) {
      const float x = in[j];
      const float x2 = j >= 2 ? in[j - 2]
          : (j == 1 ? input_prev1 : input_prev2);
      const float ya = b0a * (x - x2) - a1a * y1a - a2a * y2a;
      const float contribution_a = gain_a * ya;
      if (add || i > 0) out[j] += contribution_a;
      else out[j] = contribution_a;
      const float yb = b0b * (x - x2) - a1b * y1b - a2b * y2b;
      const float contribution_b = gain_b * yb;
      out[j] += contribution_b;
      y2a = y1a; y1a = ya;
      y2b = y1b; y1b = yb;
    }
    a->y1 = y1a; a->y2 = y2a;
    b->y1 = y1b; b->y2 = y2b;
  }
#endif

  template<bool capture, bool cached>
  void PrepareHybridMode(
      int i, const float* f, const float* q, const float* gain,
      float* cached_g, float* cached_r_plus_g, float* cached_h,
      float* cached_gain, bool low_rate, HybridModeWork* work) {
    float full_g;
    float full_r_plus_g;
    float full_h;
    float mode_gain;
    if (cached) {
      full_g = cached_g[i];
      full_r_plus_g = cached_r_plus_g[i];
      full_h = cached_h[i];
      mode_gain = cached_gain[i];
    } else {
#if FM1_MODAL_CUBIC_TAN_MAX_HZ > 0
      if (batch_size == kModeBatchSize &&
          f[i] < static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) /
              kSampleRate) {
        const float f2 = f[i] * f[i];
        const float f_max =
            static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) / kSampleRate;
        const float cubic = (M_PI_POW_3 / 3.0f) +
            0.88f * (2.0f * M_PI_POW_5 / 15.0f) * f_max * f_max;
        full_g = f[i] * (M_PI_F + f2 * cubic);
      } else {
        full_g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
      }
#else
      full_g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
#endif
      const float r = stmlib::PositiveReciprocal(q[i]);
      full_h = stmlib::PositiveReciprocal(1.0f + r * full_g + full_g * full_g);
      full_r_plus_g = r + full_g;
      mode_gain = gain[i];
    }

    float g = full_g;
    float r_plus_g = full_r_plus_g;
    float h = full_h;
#if FM1_MODAL_MULTIRATE_LOW_MODES
    if (low_rate) {
      if (!cached) {
        rate2_coeff_valid_[i] = BuildRate2Coefficients(
            full_g, full_r_plus_g, full_h,
            &rate2_g_[i], &rate2_r_plus_g_[i], &rate2_h_[i],
            &rate2_b0_[i], &rate2_a1_[i], &rate2_a2_[i]);
      }
      if (rate2_coeff_valid_[i]) {
        g = rate2_g_[i];
        r_plus_g = rate2_r_plus_g_[i];
        h = rate2_h_[i];
      }
    }
#else
    (void)low_rate;
#endif

    const float reciprocal_q = r_plus_g - g;
    const float minimum_tan =
        1.01f * M_PI_F * FM1_MODAL_HYBRID_DIRECT_FORM_MIN_HZ / kSampleRate;
    bool use_direct =
        g >= minimum_tan &&
        g <= FM1_MODAL_HYBRID_DIRECT_FORM_MAX_TAN &&
        reciprocal_q >= 1.02f / FM1_MODAL_HYBRID_DIRECT_FORM_MAX_Q &&
        g * reciprocal_q >= FM1_MODAL_HYBRID_DIRECT_FORM_MIN_TAN_R;
#if FM1_MODAL_MULTIRATE_LOW_MODES
    const float old_input_x1 = direct_low_rate_[i]
        ? rate2_input_x1_ : input_x1_;
    const float old_input_x2 = direct_low_rate_[i]
        ? rate2_input_x2_ : input_x2_;
    const float next_input_x1 = low_rate ? rate2_input_x1_ : input_x1_;
    const float next_input_x2 = low_rate ? rate2_input_x2_ : input_x2_;
    const bool transform_is_current = cached && direct_transform_valid_[i] &&
        direct_low_rate_[i] == low_rate;
#else
    const float old_input_x1 = input_x1_;
    const float old_input_x2 = input_x2_;
    const float next_input_x1 = input_x1_;
    const float next_input_x2 = input_x2_;
    const bool transform_is_current = cached && direct_transform_valid_[i];
#endif
    DirectStateTransform next_transform;
    if (!transform_is_current && direct_state_valid_[i] &&
        direct_transform_valid_[i]) {
      DirectStateToTptState(
          direct_transform_[i], direct_y1_[i], direct_y2_[i],
          old_input_x1, old_input_x2, &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
          ? 0.0f : state_1_[i];
      state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
          ? 0.0f : state_2_[i];
#endif
    }
    if (use_direct && !transform_is_current) {
      use_direct = ComputeDirectStateTransform(
          g, r_plus_g, h, &next_transform);
    }

    if (!use_direct) {
#if FM1_MODAL_HYBRID_DEFER_TPT_SYNC
      if (direct_state_valid_[i] && direct_transform_valid_[i]) {
        DirectStateToTptState(
            direct_transform_[i], direct_y1_[i], direct_y2_[i],
            old_input_x1, old_input_x2, &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
        state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
            ? 0.0f : state_1_[i];
        state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
            ? 0.0f : state_2_[i];
#endif
      }
#endif
      direct_state_valid_[i] = false;
      direct_transform_valid_[i] = false;
      work->tpt_state_1 = state_1_[i];
      work->tpt_state_2 = state_2_[i];
      work->use_direct = false;
      work->g = g;
      work->r_plus_g = r_plus_g;
      work->h = h;
    } else {
      if (!transform_is_current) {
        direct_transform_[i] = next_transform;
        direct_transform_valid_[i] = true;
      }
      if (!cached || !direct_state_valid_[i] || !transform_is_current) {
        TptStateToDirectState(
            direct_transform_[i], state_1_[i], state_2_[i],
            next_input_x1, next_input_x2, &direct_y1_[i], &direct_y2_[i]);
      }
#if FM1_MODAL_HYBRID_RECURRENCE_COEFF_CACHE
      float b0;
      float a1;
      float a2;
#if FM1_MODAL_MULTIRATE_LOW_MODES
      if (low_rate && rate2_coeff_valid_[i]) {
        b0 = rate2_b0_[i];
        a1 = rate2_a1_[i];
        a2 = rate2_a2_[i];
      } else
#endif
      if (!cached || !transform_is_current) {
        const float g2 = g * g;
        b0 = g * h;
        const float common = 2.0f * h * g * r_plus_g;
        const float doubled_g2 = 2.0f * h * g2;
        a1 = -2.0f + common + doubled_g2;
        a2 = 1.0f - common + doubled_g2;
        direct_recurrence_b0_[i] = b0;
        direct_recurrence_a1_[i] = a1;
        direct_recurrence_a2_[i] = a2;
      } else {
        b0 = direct_recurrence_b0_[i];
        a1 = direct_recurrence_a1_[i];
        a2 = direct_recurrence_a2_[i];
      }
#else
      const float g2 = g * g;
      const float b0 = g * h;
      const float common = 2.0f * h * g * r_plus_g;
      const float doubled_g2 = 2.0f * h * g2;
      const float a1 = -2.0f + common + doubled_g2;
      const float a2 = 1.0f - common + doubled_g2;
#endif
      work->b0 = b0;
      work->a1 = a1;
      work->a2 = a2;
      work->y1 = direct_y1_[i];
      work->y2 = direct_y2_[i];
      work->use_direct = true;
#if FM1_MODAL_MULTIRATE_LOW_MODES
      direct_low_rate_[i] = low_rate;
#endif
    }
    work->cache_g = full_g;
    work->cache_r_plus_g = full_r_plus_g;
    work->cache_h = full_h;
    work->g = g;
    if (use_direct) {
      work->r_plus_g = r_plus_g;
      work->h = h;
    }
    work->mode_gain = mode_gain;
  }

  template<bool capture>
  void FinishHybridDirectMode(
      int i, float y1, float y2, float g, float r_plus_g, float h,
      float mode_gain, float input_last1, float input_last2,
      float* cached_g, float* cached_r_plus_g, float* cached_h,
      float* cached_gain) {
#if STMLIB_FLUSH_TINY_FILTER_STATES
    y1 = __builtin_fabsf(y1) < 1.0e-20f ? 0.0f : y1;
    y2 = __builtin_fabsf(y2) < 1.0e-20f ? 0.0f : y2;
#endif
    direct_y1_[i] = y1;
    direct_y2_[i] = y2;
#if !FM1_MODAL_HYBRID_DEFER_TPT_SYNC
    DirectStateToTptState(
        direct_transform_[i], y1, y2, input_last1, input_last2,
        &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
    state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
        ? 0.0f : state_1_[i];
    state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
        ? 0.0f : state_2_[i];
#endif
#endif
    direct_state_valid_[i] = true;
    if (capture) {
      cached_g[i] = g;
      cached_r_plus_g[i] = r_plus_g;
      cached_h[i] = h;
      cached_gain[i] = mode_gain;
    }
  }

#if FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT
  static float CachedModeGain(
      const ModalCachedCoefficients* cached, int i) {
    return cached[i].gain;
  }

  static float CachedModeGain(const float* cached_gain, int i) {
    return cached_gain[i];
  }

  template<stmlib::FilterMode mode, bool add, typename CachedGain>
#if FM1_MODAL_CACHE_HIT_FORCE_INLINE
  __attribute__((always_inline))
#endif
  inline bool TryProcessCachedDirectForm(
      const CachedGain* cached_gain,
      const float* in,
      float* out,
      size_t size) {
    if (batch_size != kModeBatchSize || size > kMaxBlockSize) {
      return false;
    }
    for (int i = 0; i < kModeBatchSize; ++i) {
      if (!direct_state_valid_[i] || !direct_transform_valid_[i]) {
        return false;
      }
#if FM1_MODAL_MULTIRATE_LOW_MODES
      // A full-rate cache-hit kernel cannot consume state left at half rate.
      if (direct_low_rate_[i]) return false;
#endif
    }

    const float* process_input = in;
#if FM1_MODAL_CACHE_HIT_ALIAS_FALLBACK
    /* Resonator::Process routes aliasing input through ProcessInternal's
     * existing copy path. Avoid reserving this buffer in the cache helper. */
    if (in == out) {
      return false;
    }
#else
    float input_copy[kMaxBlockSize];
    if (in == out) {
      for (size_t j = 0; j < size; ++j) {
        input_copy[j] = in[j];
      }
      process_input = input_copy;
    }
#endif
    const float input_last1 = size ? process_input[size - 1] : input_x1_;
    const float input_last2 = size > 1 ? process_input[size - 2] : input_x1_;

    for (int i = 0; i < kModeBatchSize; i += 2) {
      const float gain_a = CachedModeGain(cached_gain, i);
      const float gain_b = CachedModeGain(cached_gain, i + 1);
      const float b0a = direct_recurrence_b0_[i];
      const float a1a = direct_recurrence_a1_[i];
      const float a2a = direct_recurrence_a2_[i];
      const float b0b = direct_recurrence_b0_[i + 1];
      const float a1b = direct_recurrence_a1_[i + 1];
      const float a2b = direct_recurrence_a2_[i + 1];
      float y1a = direct_y1_[i];
      float y2a = direct_y2_[i];
      float y1b = direct_y1_[i + 1];
      float y2b = direct_y2_[i + 1];
#if FM1_MODAL_STREAMED_INPUT_HISTORY
      float input_history_1 = input_x1_;
      float input_history_2 = input_x2_;
#endif
      for (size_t j = 0; j < size; ++j) {
        const float x = process_input[j];
#if FM1_MODAL_STREAMED_INPUT_HISTORY
        const float x2 = input_history_2;
#else
        const float x2 = j >= 2 ? process_input[j - 2]
            : (j == 1 ? input_x1_ : input_x2_);
#endif
        const float ya = b0a * (x - x2) - a1a * y1a - a2a * y2a;
        const float contribution_a = gain_a * ya;
        if (add || i > 0) out[j] += contribution_a;
        else out[j] = contribution_a;
        const float yb = b0b * (x - x2) - a1b * y1b - a2b * y2b;
        out[j] += gain_b * yb;
        y2a = y1a;
        y1a = ya;
        y2b = y1b;
        y1b = yb;
#if FM1_MODAL_STREAMED_INPUT_HISTORY
        input_history_2 = input_history_1;
        input_history_1 = x;
#endif
      }
#if STMLIB_FLUSH_TINY_FILTER_STATES
      y1a = __builtin_fabsf(y1a) < 1.0e-20f ? 0.0f : y1a;
      y2a = __builtin_fabsf(y2a) < 1.0e-20f ? 0.0f : y2a;
      y1b = __builtin_fabsf(y1b) < 1.0e-20f ? 0.0f : y1b;
      y2b = __builtin_fabsf(y2b) < 1.0e-20f ? 0.0f : y2b;
#endif
      direct_y1_[i] = y1a;
      direct_y2_[i] = y2a;
      direct_y1_[i + 1] = y1b;
      direct_y2_[i + 1] = y2b;
#if !FM1_MODAL_HYBRID_DEFER_TPT_SYNC
      DirectStateToTptState(
          direct_transform_[i], y1a, y2a, input_last1, input_last2,
          &state_1_[i], &state_2_[i]);
      DirectStateToTptState(
          direct_transform_[i + 1], y1b, y2b, input_last1, input_last2,
          &state_1_[i + 1], &state_2_[i + 1]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
          ? 0.0f : state_1_[i];
      state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
          ? 0.0f : state_2_[i];
      state_1_[i + 1] = __builtin_fabsf(state_1_[i + 1]) < 1.0e-20f
          ? 0.0f : state_1_[i + 1];
      state_2_[i + 1] = __builtin_fabsf(state_2_[i + 1]) < 1.0e-20f
          ? 0.0f : state_2_[i + 1];
#endif
#endif
    }
    if (size > 1) {
      input_x2_ = input_last2;
      input_x1_ = input_last1;
    } else if (size == 1) {
      input_x2_ = input_x1_;
      input_x1_ = input_last1;
    }
#if defined(TEST) && FM1_MODAL_MULTIRATE_DIAGNOSTICS
    ++fm1_modal_test_persistent_direct_cache_hits;
#endif
    return true;
  }
#endif

  template<stmlib::FilterMode mode, bool add, bool capture>
  void RenderHybridScalarMode(
      int i, const float* in, float* out, size_t size,
      float input_last1, float input_last2,
      float input_prev1, float input_prev2,
      float* cached_g, float* cached_r_plus_g, float* cached_h,
      float* cached_gain, HybridModeWork* work) {
    if (!work->use_direct) {
      float state_1 = work->tpt_state_1;
      float state_2 = work->tpt_state_2;
      for (size_t j = 0; j < size; ++j) {
        const float hp =
            (in[j] - work->r_plus_g * state_1 - state_2) * work->h;
        const float g_hp = work->g * hp;
        const float bp = g_hp + state_1;
        state_1 = g_hp + bp;
        const float g_bp = work->g * bp;
        const float lp = g_bp + state_2;
        state_2 = g_bp + lp;
        const float contribution = work->mode_gain *
            ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
        if (add || i > 0) out[j] += contribution;
        else out[j] = contribution;
      }
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1) < 1.0e-20f
          ? 0.0f : state_1;
      state_2_[i] = __builtin_fabsf(state_2) < 1.0e-20f
          ? 0.0f : state_2;
#else
      state_1_[i] = state_1;
      state_2_[i] = state_2;
#endif
      if (capture) {
        cached_g[i] = work->cache_g;
        cached_r_plus_g[i] = work->cache_r_plus_g;
        cached_h[i] = work->cache_h;
        cached_gain[i] = work->mode_gain;
      }
      return;
    }

    float y1 = work->y1;
    float y2 = work->y2;
    for (size_t j = 0; j < size; ++j) {
      const float x2 = j >= 2 ? in[j - 2]
          : (j == 1 ? input_prev1 : input_prev2);
      const float y = work->b0 * (in[j] - x2) -
          work->a1 * y1 - work->a2 * y2;
      const float contribution = work->mode_gain * y;
      if (add || i > 0) out[j] += contribution;
      else out[j] = contribution;
      y2 = y1;
      y1 = y;
    }
    FinishHybridDirectMode<capture>(i, y1, y2,
        work->cache_g, work->cache_r_plus_g, work->cache_h,
        work->mode_gain,
        input_last1, input_last2,
        cached_g, cached_r_plus_g, cached_h, cached_gain);
  }

  template<stmlib::FilterMode mode, bool add, bool capture, bool cached>
  void ProcessHybridDirectFormPaired(
      const float* f, const float* q, const float* gain,
      const float* in, float* out, size_t size,
      float* cached_g, float* cached_r_plus_g, float* cached_h,
      float* cached_gain, float input_last1, float input_last2,
      float input_prev1, float input_prev2, bool low_rate) {
    for (int i = 0; i < batch_size; i += 2) {
      HybridModeWork a;
      HybridModeWork b;
      PrepareHybridMode<capture, cached>(i, f, q, gain,
          cached_g, cached_r_plus_g, cached_h, cached_gain, low_rate, &a);
      const bool has_b = i + 1 < batch_size;
      if (has_b) {
        PrepareHybridMode<capture, cached>(i + 1, f, q, gain,
            cached_g, cached_r_plus_g, cached_h, cached_gain, low_rate, &b);
      }
      if (has_b && a.use_direct && b.use_direct) {
#if FM1_MODAL_DF_KERNEL_RAM
        typedef void (*PairedDirectKernelFn)(
            const float*, float*, size_t, HybridModeWork*, HybridModeWork*,
            int, float, float);
        PairedDirectKernelFn volatile kernel = &RenderPairedDirectKernel<add>;
        kernel(in, out, size, &a, &b, i, input_prev1, input_prev2);
        const float y1a = a.y1, y2a = a.y2;
        const float y1b = b.y1, y2b = b.y2;
#else
        float y1a = a.y1, y2a = a.y2;
        float y1b = b.y1, y2b = b.y2;
        for (size_t j = 0; j < size; ++j) {
          const float x = in[j];
          const float x2 = j >= 2 ? in[j - 2]
              : (j == 1 ? input_prev1 : input_prev2);
          const float ya = a.b0 * (x - x2) - a.a1 * y1a - a.a2 * y2a;
          const float contribution_a = a.mode_gain * ya;
          if (add || i > 0) out[j] += contribution_a;
          else out[j] = contribution_a;
          const float yb = b.b0 * (x - x2) - b.a1 * y1b - b.a2 * y2b;
          const float contribution_b = b.mode_gain * yb;
          out[j] += contribution_b;
          y2a = y1a; y1a = ya;
          y2b = y1b; y1b = yb;
        }
#endif
        FinishHybridDirectMode<capture>(i, y1a, y2a,
            a.cache_g, a.cache_r_plus_g, a.cache_h, a.mode_gain,
            input_last1, input_last2,
            cached_g, cached_r_plus_g, cached_h, cached_gain);
        FinishHybridDirectMode<capture>(i + 1, y1b, y2b,
            b.cache_g, b.cache_r_plus_g, b.cache_h, b.mode_gain,
            input_last1, input_last2,
            cached_g, cached_r_plus_g, cached_h, cached_gain);
      } else {
        RenderHybridScalarMode<mode, add, capture>(i, in, out, size,
            input_last1, input_last2,
            input_prev1, input_prev2,
            cached_g, cached_r_plus_g, cached_h, cached_gain, &a);
        if (has_b) {
          RenderHybridScalarMode<mode, add, capture>(i + 1, in, out, size,
              input_last1, input_last2,
              input_prev1, input_prev2,
              cached_g, cached_r_plus_g, cached_h, cached_gain, &b);
        }
      }
    }
  }

  template<stmlib::FilterMode mode, bool add, bool capture, bool cached>
  void ProcessHybridDirectForm(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size,
      float* cached_g,
      float* cached_r_plus_g,
      float* cached_h,
      float* cached_gain,
      float input_last1,
      float input_last2,
      float input_prev1,
      float input_prev2,
      bool low_rate) {
#if FM1_MODAL_PAIRED_DIRECT_FORM
    ProcessHybridDirectFormPaired<mode, add, capture, cached>(
        f, q, gain, in, out, size, cached_g, cached_r_plus_g,
        cached_h, cached_gain, input_last1, input_last2,
        input_prev1, input_prev2, low_rate);
    return;
#endif
    for (int i = 0; i < batch_size; ++i) {
      float g;
      float r_plus_g;
      float h;
      float mode_gain;
      if (cached) {
        g = cached_g[i];
        r_plus_g = cached_r_plus_g[i];
        h = cached_h[i];
        mode_gain = cached_gain[i];
      } else {
#if FM1_MODAL_CUBIC_TAN_MAX_HZ > 0
        if (batch_size == kModeBatchSize &&
            f[i] < static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) /
                kSampleRate) {
          const float f2 = f[i] * f[i];
          const float f_max =
              static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) / kSampleRate;
          const float cubic = (M_PI_POW_3 / 3.0f) +
              0.88f * (2.0f * M_PI_POW_5 / 15.0f) * f_max * f_max;
          g = f[i] * (M_PI_F + f2 * cubic);
        } else {
          g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
        }
#else
        g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
#endif
        const float r = stmlib::PositiveReciprocal(q[i]);
        h = stmlib::PositiveReciprocal(1.0f + r * g + g * g);
        r_plus_g = r + g;
        mode_gain = gain[i];
      }

      // f and q are null on the packed coefficient-cache hit path. The
      // TPT cache already contains g=tan(pi*f) and r=1/q, so gate in that
      // coefficient space on both cache hits and coefficient rebuilds.
      const float reciprocal_q = r_plus_g - g;
      const float minimum_tan =
          1.01f * M_PI_F * FM1_MODAL_HYBRID_DIRECT_FORM_MIN_HZ / kSampleRate;
      bool use_direct =
          g >= minimum_tan &&
          g <= FM1_MODAL_HYBRID_DIRECT_FORM_MAX_TAN &&
          reciprocal_q >= 1.02f / FM1_MODAL_HYBRID_DIRECT_FORM_MAX_Q &&
          g * reciprocal_q >= FM1_MODAL_HYBRID_DIRECT_FORM_MIN_TAN_R;
#if FM1_MODAL_HYBRID_DEFER_TPT_SYNC
      // Keep the normal coefficient-change state remap, but perform the
      // direct-to-TPT half lazily at the next coefficient miss. This skips
      // a redundant transform on cache-hit blocks while preserving the
      // existing state trajectory when coefficients change.
      if (use_direct && !cached && direct_state_valid_[i] &&
          direct_transform_valid_[i]) {
        DirectStateToTptState(
            direct_transform_[i], direct_y1_[i], direct_y2_[i],
            input_x1_, input_x2_, &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
        state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
            ? 0.0f : state_1_[i];
        state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
            ? 0.0f : state_2_[i];
#endif
      }
#endif
      DirectStateTransform next_transform;
      const bool transform_is_current = cached && direct_transform_valid_[i];
      if (use_direct && !transform_is_current) {
        use_direct = ComputeDirectStateTransform(
            g, r_plus_g, h, &next_transform);
      }

      if (!use_direct) {
#if FM1_MODAL_HYBRID_DEFER_TPT_SYNC
        // Rebuild TPT coordinates only when leaving the direct recurrence.
        if (direct_state_valid_[i] && direct_transform_valid_[i]) {
          DirectStateToTptState(
              direct_transform_[i], direct_y1_[i], direct_y2_[i],
              input_x1_, input_x2_, &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
          state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
              ? 0.0f : state_1_[i];
          state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
              ? 0.0f : state_2_[i];
#endif
        }
#endif
        direct_state_valid_[i] = false;
        direct_transform_valid_[i] = false;

        float state_1 = state_1_[i];
        float state_2 = state_2_[i];
        for (size_t j = 0; j < size; ++j) {
          const float hp = (in[j] - r_plus_g * state_1 - state_2) * h;
          const float g_hp = g * hp;
          const float bp = g_hp + state_1;
          state_1 = g_hp + bp;
          const float g_bp = g * bp;
          const float lp = g_bp + state_2;
          state_2 = g_bp + lp;
          const float contribution = mode_gain *
              ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
          if (add || i > 0) {
            out[j] += contribution;
          } else {
            out[j] = contribution;
          }
        }
#if STMLIB_FLUSH_TINY_FILTER_STATES
        state_1_[i] = __builtin_fabsf(state_1) < 1.0e-20f
            ? 0.0f : state_1;
        state_2_[i] = __builtin_fabsf(state_2) < 1.0e-20f
            ? 0.0f : state_2;
#else
        state_1_[i] = state_1;
        state_2_[i] = state_2;
#endif
        if (capture) {
          cached_g[i] = g;
          cached_r_plus_g[i] = r_plus_g;
          cached_h[i] = h;
          cached_gain[i] = mode_gain;
        }
        continue;
      }

      if (!transform_is_current) {
        direct_transform_[i] = next_transform;
        direct_transform_valid_[i] = true;
      }
      if (!cached || !direct_state_valid_[i]) {
        TptStateToDirectState(
            direct_transform_[i], state_1_[i], state_2_[i],
            input_x1_, input_x2_, &direct_y1_[i], &direct_y2_[i]);
      }

#if FM1_MODAL_HYBRID_RECURRENCE_COEFF_CACHE
      float b0;
      float a1;
      float a2;
      if (!cached || !transform_is_current) {
        const float g2 = g * g;
        b0 = g * h;
        const float common = 2.0f * h * g * r_plus_g;
        const float doubled_g2 = 2.0f * h * g2;
        a1 = -2.0f + common + doubled_g2;
        a2 = 1.0f - common + doubled_g2;
        direct_recurrence_b0_[i] = b0;
        direct_recurrence_a1_[i] = a1;
        direct_recurrence_a2_[i] = a2;
      } else {
        b0 = direct_recurrence_b0_[i];
        a1 = direct_recurrence_a1_[i];
        a2 = direct_recurrence_a2_[i];
      }
#else
      const float g2 = g * g;
      const float b0 = g * h;
      const float common = 2.0f * h * g * r_plus_g;
      const float doubled_g2 = 2.0f * h * g2;
      const float a1 = -2.0f + common + doubled_g2;
      const float a2 = 1.0f - common + doubled_g2;
#endif
      float y1 = direct_y1_[i];
      float y2 = direct_y2_[i];
#if FM1_MODAL_DF_KERNEL_RAM
      DirectKernelState kernel_state = {
        b0, a1, a2, mode_gain, input_x1_, input_x2_, y1, y2
      };
      typedef void (*DirectKernelFn)(const float*, float*, size_t, DirectKernelState*);
      // Explicit indirect call handles the pi32v2 flash/RAM branch distance.
      if (add || i > 0) {
        DirectKernelFn volatile kernel = &RenderDirectKernel<true>;
        kernel(in, out, size, &kernel_state);
      } else {
        DirectKernelFn volatile kernel = &RenderDirectKernel<false>;
        kernel(in, out, size, &kernel_state);
      }
      y1 = kernel_state.y1;
      y2 = kernel_state.y2;
#else
      for (size_t j = 0; j < size; ++j) {
        const float x2 = j >= 2 ? in[j - 2]
            : (j == 1 ? input_x1_ : input_x2_);
        const float y = b0 * (in[j] - x2) - a1 * y1 - a2 * y2;
        const float contribution = mode_gain * y;
        if (add || i > 0) {
          out[j] += contribution;
        } else {
          out[j] = contribution;
        }
        y2 = y1;
        y1 = y;
      }
#endif
#if STMLIB_FLUSH_TINY_FILTER_STATES
      y1 = __builtin_fabsf(y1) < 1.0e-20f ? 0.0f : y1;
      y2 = __builtin_fabsf(y2) < 1.0e-20f ? 0.0f : y2;
#endif
      direct_y1_[i] = y1;
      direct_y2_[i] = y2;
#if !FM1_MODAL_HYBRID_DEFER_TPT_SYNC
      DirectStateToTptState(
          direct_transform_[i], y1, y2, input_last1, input_last2,
          &state_1_[i], &state_2_[i]);
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1_[i]) < 1.0e-20f
          ? 0.0f : state_1_[i];
      state_2_[i] = __builtin_fabsf(state_2_[i]) < 1.0e-20f
          ? 0.0f : state_2_[i];
#endif
#endif
      direct_state_valid_[i] = true;
      if (capture) {
        cached_g[i] = g;
        cached_r_plus_g[i] = r_plus_g;
        cached_h[i] = h;
        cached_gain[i] = mode_gain;
      }
    }
  }
#endif

#endif

  template<stmlib::FilterMode mode, bool add, bool capture, bool cached>
  void ProcessModeFirst(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size,
      float* cached_g,
      float* cached_r_plus_g,
      float* cached_h,
      float* cached_gain) {
#if !FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
    float accumulated[kMaxBlockSize];
    for (size_t j = 0; j < size; ++j) {
      accumulated[j] = 0.0f;
    }
#endif

    for (int i = 0; i < batch_size; ++i) {
      float g;
      float r_plus_g;
      float h;
      float mode_gain;
      if (cached) {
        g = cached_g[i];
        r_plus_g = cached_r_plus_g[i];
        h = cached_h[i];
        mode_gain = cached_gain[i];
      } else {
#if FM1_MODAL_CUBIC_TAN_MAX_HZ > 0
        if (batch_size == kModeBatchSize &&
            f[i] < static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) /
                kSampleRate) {
          const float f2 = f[i] * f[i];
          const float f_max =
              static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) / kSampleRate;
          const float cubic = (M_PI_POW_3 / 3.0f) +
              0.88f * (2.0f * M_PI_POW_5 / 15.0f) * f_max * f_max;
          g = f[i] * (M_PI_F + f2 * cubic);
        } else {
          g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
        }
#else
        g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
#endif
        const float r = stmlib::PositiveReciprocal(q[i]);
        h = stmlib::PositiveReciprocal(1.0f + r * g + g * g);
        r_plus_g = r + g;
        mode_gain = gain[i];
        if (capture) {
          cached_g[i] = g;
          cached_r_plus_g[i] = r_plus_g;
          cached_h[i] = h;
          cached_gain[i] = mode_gain;
        }
      }

      float state_1 = state_1_[i];
      float state_2 = state_2_[i];
      for (size_t j = 0; j < size; ++j) {
        const float hp = (in[j] - r_plus_g * state_1 - state_2) * h;
        const float g_hp = g * hp;
        const float bp = g_hp + state_1;
        state_1 = g_hp + bp;
        const float g_bp = g * bp;
        const float lp = g_bp + state_2;
        state_2 = g_bp + lp;
        const float contribution = mode_gain *
            ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
#if FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
        if (add || i > 0) {
          out[j] += contribution;
        } else {
          out[j] = contribution;
        }
#else
        accumulated[j] += contribution;
#endif
      }
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1) < 1.0e-20f
          ? 0.0f : state_1;
      state_2_[i] = __builtin_fabsf(state_2) < 1.0e-20f
          ? 0.0f : state_2;
#else
      state_1_[i] = state_1;
      state_2_[i] = state_2;
#endif
    }

#if !FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
    for (size_t j = 0; j < size; ++j) {
      if (add) {
        out[j] += accumulated[j];
      } else {
        out[j] = accumulated[j];
      }
    }
#endif
  }

#if FM1_MODAL_CACHE_AOS
  template<stmlib::FilterMode mode, bool add>
  void ProcessModeFirstPacked(
      const ModalCachedCoefficients* cached,
      const float* in,
      float* out,
      size_t size) {
#if !FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
    float accumulated[kMaxBlockSize];
    for (size_t j = 0; j < size; ++j) {
      accumulated[j] = 0.0f;
    }
#endif

    for (int i = 0; i < batch_size; ++i) {
      const float g = cached[i].g;
      const float r_plus_g = cached[i].r_plus_g;
      const float h = cached[i].h;
      const float mode_gain = cached[i].gain;
      float state_1 = state_1_[i];
      float state_2 = state_2_[i];
      for (size_t j = 0; j < size; ++j) {
        const float hp = (in[j] - r_plus_g * state_1 - state_2) * h;
        const float g_hp = g * hp;
        const float bp = g_hp + state_1;
        state_1 = g_hp + bp;
        const float g_bp = g * bp;
        const float lp = g_bp + state_2;
        state_2 = g_bp + lp;
        const float contribution = mode_gain *
            ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
#if FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
        if (add || i > 0) {
          out[j] += contribution;
        } else {
          out[j] = contribution;
        }
#else
        accumulated[j] += contribution;
#endif
      }
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1) < 1.0e-20f
          ? 0.0f : state_1;
      state_2_[i] = __builtin_fabsf(state_2) < 1.0e-20f
          ? 0.0f : state_2;
#else
      state_1_[i] = state_1;
      state_2_[i] = state_2;
#endif
    }

#if !FM1_MODAL_MODE_FIRST_DIRECT_ACCUM
    for (size_t j = 0; j < size; ++j) {
      if (add) {
        out[j] += accumulated[j];
      } else {
        out[j] = accumulated[j];
      }
    }
#endif
  }
#endif

  template<stmlib::FilterMode mode, bool add, bool capture, bool cached>
  void ProcessInternal(
      const float* f,
      const float* q,
      const float* gain,
      const float* in,
      float* out,
      size_t size,
      float* cached_g,
      float* cached_r_plus_g,
      float* cached_h,
      float* cached_gain
#if FM1_MODAL_MULTIRATE_LOW_MODES
      , bool low_rate
#endif
      ) {
#if FM1_MODAL_MULTIRATE_LOW_MODES
    const bool process_low_rate = low_rate;
#else
    const bool process_low_rate = false;
#endif
#if FM1_MODAL_DIRECT_FORM
    const float* process_input = in;
    float input_copy[kMaxBlockSize];
    if (in == out && size <= kMaxBlockSize) {
      for (size_t i = 0; i < size; ++i) {
        input_copy[i] = in[i];
      }
      process_input = input_copy;
    }
#if FM1_MODAL_MULTIRATE_LOW_MODES
    const float input_prev1 = process_low_rate ? rate2_input_x1_ : input_x1_;
    const float input_prev2 = process_low_rate ? rate2_input_x2_ : input_x2_;
#else
    const float input_prev1 = input_x1_;
    const float input_prev2 = input_x2_;
#endif
    const float input_last1 = size ? process_input[size - 1] : input_prev1;
    const float input_last2 = size > 1 ? process_input[size - 2] : input_prev1;
    if (mode == stmlib::FILTER_MODE_BAND_PASS &&
        batch_size == kModeBatchSize && size <= kMaxBlockSize) {
#if FM1_MODAL_HYBRID_DIRECT_FORM
      ProcessHybridDirectForm<mode, add, capture, cached>(
          f, q, gain, process_input, out, size,
          cached_g, cached_r_plus_g, cached_h, cached_gain,
          input_last1, input_last2, input_prev1, input_prev2,
          process_low_rate);
#else
      ProcessDirectForm<mode, add, capture, cached>(
          f, q, gain, process_input, out, size,
          cached_g, cached_r_plus_g, cached_h, cached_gain,
          input_last1, input_last2);
#endif
#if FM1_MODAL_MULTIRATE_LOW_MODES
      float* history_x1 = process_low_rate ? &rate2_input_x1_ : &input_x1_;
      float* history_x2 = process_low_rate ? &rate2_input_x2_ : &input_x2_;
#else
      float* history_x1 = &input_x1_;
      float* history_x2 = &input_x2_;
#endif
      if (size > 1) {
        *history_x2 = input_last2;
        *history_x1 = input_last1;
      } else if (size == 1) {
        *history_x2 = *history_x1;
        *history_x1 = input_last1;
      }
      return;
    }
    for (int i = 0; i < batch_size; ++i) {
      direct_state_valid_[i] = false;
      direct_transform_valid_[i] = false;
    }
#endif
#if FM1_MODAL_MODE_FIRST
    if (batch_size == kModeBatchSize && size <= kMaxBlockSize &&
        (!FM1_MODAL_MODE_FIRST_DIRECT_ACCUM || in != out)) {
      ProcessModeFirst<mode, add, capture, cached>(
          f, q, gain, in, out, size,
          cached_g, cached_r_plus_g, cached_h, cached_gain);
#if FM1_MODAL_DIRECT_FORM
      if (size > 1) {
        input_x2_ = input_last2;
        input_x1_ = input_last1;
      } else if (size == 1) {
        input_x2_ = input_x1_;
        input_x1_ = input_last1;
      }
#endif
      return;
    }
#endif
    float g[batch_size];
    float r[batch_size];
    float r_plus_g[batch_size];
    float h[batch_size];
    float state_1[batch_size];
    float state_2[batch_size];
    float gains[batch_size];
#if FM1_MODAL_MULTIRATE_LOW_MODES
    const size_t process_size = size;
    const bool rate_changed = process_size && rate_state_valid_ &&
        low_rate != last_low_rate_;
#endif
    for (int i = 0; i < batch_size; ++i) {
      if (cached) {
        g[i] = cached_g[i];
        r_plus_g[i] = cached_r_plus_g[i];
        h[i] = cached_h[i];
        gains[i] = cached_gain[i];
      } else {
#if FM1_MODAL_CUBIC_TAN_MAX_HZ > 0
        if (batch_size == kModeBatchSize &&
            f[i] < static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) /
                kSampleRate) {
          const float f2 = f[i] * f[i];
          const float f_max =
              static_cast<float>(FM1_MODAL_CUBIC_TAN_MAX_HZ) / kSampleRate;
          const float cubic = (M_PI_POW_3 / 3.0f) +
              0.88f * (2.0f * M_PI_POW_5 / 15.0f) * f_max * f_max;
          g[i] = f[i] * (M_PI_F + f2 * cubic);
        } else {
          g[i] = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
        }
#else
        g[i] = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f[i]);
#endif
        r[i] = stmlib::PositiveReciprocal(q[i]);
        h[i] = stmlib::PositiveReciprocal(
            1.0f + r[i] * g[i] + g[i] * g[i]);
        r_plus_g[i] = r[i] + g[i];
        gains[i] = gain[i];
        if (capture) {
          cached_g[i] = g[i];
          cached_r_plus_g[i] = r_plus_g[i];
          cached_h[i] = h[i];
          cached_gain[i] = gains[i];
        }
      }
#if FM1_MODAL_MULTIRATE_LOW_MODES
      if (rate_changed) {
        const float current_frequency = low_rate ? 0.5f * f[i] : f[i];
        const float frequency_scale = current_frequency > last_frequency_[i]
            ? current_frequency : last_frequency_[i];
        const float q_scale = q[i] > last_q_[i] ? q[i] : last_q_[i];
        const bool coefficients_continuous =
            __builtin_fabsf(current_frequency - last_frequency_[i]) <=
                1.0e-3f * frequency_scale &&
            __builtin_fabsf(q[i] - last_q_[i]) <= 1.0e-3f * q_scale;
        if (coefficients_continuous) {
          state_1[i] = last_bp_[i] + g[i] * last_hp_[i];
          state_2[i] = last_lp_[i] + g[i] * last_bp_[i];
        } else {
          state_1[i] = state_1_[i];
          state_2[i] = state_2_[i];
        }
      } else {
        state_1[i] = state_1_[i];
        state_2[i] = state_2_[i];
      }
#else
      state_1[i] = state_1_[i];
      state_2[i] = state_2_[i];
#endif
    }
    while (size--) {
      float s_in = *in++;
      float s_out = 0.0f;
      for (int i = 0; i < batch_size; ++i) {
        const float hp = (s_in - r_plus_g[i] * state_1[i] - state_2[i]) * h[i];
        const float g_hp = g[i] * hp;
        const float bp = g_hp + state_1[i];
        state_1[i] = g_hp + bp;
        const float g_bp = g[i] * bp;
        const float lp = g_bp + state_2[i];
        state_2[i] = g_bp + lp;
        s_out += gains[i] * ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
#if FM1_MODAL_MULTIRATE_LOW_MODES
        if (!size) {
          last_hp_[i] = hp;
          last_bp_[i] = bp;
          last_lp_[i] = lp;
        }
#endif
      }
      if (add) {
        *out++ += s_out;
      } else {
        *out++ = s_out;
      }
    }
#if FM1_MODAL_MULTIRATE_LOW_MODES
    if (process_size) {
      rate_state_valid_ = true;
      last_low_rate_ = low_rate;
      for (int i = 0; i < batch_size; ++i) {
        last_frequency_[i] = low_rate ? 0.5f * f[i] : f[i];
        last_q_[i] = q[i];
      }
    }
#endif
    for (int i = 0; i < batch_size; ++i) {
#if STMLIB_FLUSH_TINY_FILTER_STATES
      state_1_[i] = __builtin_fabsf(state_1[i]) < 1.0e-20f
          ? 0.0f : state_1[i];
      state_2_[i] = __builtin_fabsf(state_2[i]) < 1.0e-20f
          ? 0.0f : state_2[i];
#else
      state_1_[i] = state_1[i];
      state_2_[i] = state_2[i];
#endif
    }
#if FM1_MODAL_DIRECT_FORM
    if (size > 1) {
      input_x2_ = input_last2;
      input_x1_ = input_last1;
    } else if (size == 1) {
      input_x2_ = input_x1_;
      input_x1_ = input_last1;
    }
#endif
  }

  float state_1_[batch_size];
  float state_2_[batch_size];
#if FM1_MODAL_DIRECT_FORM
  float input_x1_;
  float input_x2_;
#if FM1_MODAL_MULTIRATE_LOW_MODES
  float rate2_input_x1_;
  float rate2_input_x2_;
  bool direct_low_rate_[batch_size];
  bool rate2_coeff_valid_[batch_size];
  float rate2_g_[batch_size];
  float rate2_r_plus_g_[batch_size];
  float rate2_h_[batch_size];
  float rate2_b0_[batch_size];
  float rate2_a1_[batch_size];
  float rate2_a2_[batch_size];
#endif
  float direct_y1_[batch_size];
  float direct_y2_[batch_size];
  DirectStateTransform direct_transform_[batch_size];
  bool direct_state_valid_[batch_size];
  bool direct_transform_valid_[batch_size];
#if FM1_MODAL_HYBRID_RECURRENCE_COEFF_CACHE
  float direct_recurrence_b0_[batch_size];
  float direct_recurrence_a1_[batch_size];
  float direct_recurrence_a2_[batch_size];
#endif
#endif
#if FM1_MODAL_MULTIRATE_LOW_MODES
  float last_hp_[batch_size];
  float last_bp_[batch_size];
  float last_lp_[batch_size];
  float last_frequency_[batch_size];
  float last_q_[batch_size];
  bool rate_state_valid_;
  bool last_low_rate_;
#endif
  
  DISALLOW_COPY_AND_ASSIGN(ResonatorSvf);
};

class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }
  
  void Init(float position, int resolution);
  void Process(
      float f0,
      float structure,
      float brightness,
      float damping,
      const float* in,
      float* out,
      size_t size);
  
 private:
  int resolution_;
  
  float mode_amplitude_[kMaxNumModes];
  ResonatorSvf<kModeBatchSize> mode_filters_[kMaxNumModes / kModeBatchSize];

#if FM1_MODAL_EXACT_CACHE || FM1_MODAL_BOUNDED_CACHE
  bool coefficient_cache_valid_;
  uint32_t coefficient_cache_rate_hz_;
  float coefficient_source_[4];
#if FM1_MODAL_CACHE_AOS
  ModalCachedCoefficients coefficient_cache_[kMaxNumModes];
  static_assert(sizeof(coefficient_cache_) ==
                    4 * sizeof(float) * kMaxNumModes,
                "AoS modal cache must preserve the existing storage size");
#else
  float coefficient_g_[kMaxNumModes];
  float coefficient_r_plus_g_[kMaxNumModes];
  float coefficient_h_[kMaxNumModes];
  float coefficient_gain_[kMaxNumModes];
#endif
#endif

#if FM1_MODAL_EXACT_CACHE
  uint32_t coefficient_key_[6];
#endif

#if FM1_MODAL_BOUNDED_CACHE
  float coefficient_mode_f_[kMaxNumModes];
  float coefficient_mode_q_[kMaxNumModes];
#endif

#if FM1_MODAL_MULTIRATE_LOW_MODES
  size_t DecimateForLowModes(
      const float* input, size_t size, float* low_input);
  void ProcessCachedMultirate(
      const float* process_input, const float* low_input,
      float* low_output, float* high_output, size_t low_size, size_t size);
  void MixMultirateOutputs(
      const float* low_output, const float* high_output,
      float* out, size_t size, size_t low_size);
  float multirate_input_history_[16];
  uint8_t multirate_input_write_;
  uint8_t multirate_decimation_phase_;
  float multirate_low_history_[8];
  uint8_t multirate_low_write_;
  uint8_t multirate_interpolation_phase_;
  float multirate_high_delay_[14];
  uint8_t multirate_high_delay_write_;
  bool multirate_batch_low_[kMaxNumModes / kModeBatchSize];
#endif
  
  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_
