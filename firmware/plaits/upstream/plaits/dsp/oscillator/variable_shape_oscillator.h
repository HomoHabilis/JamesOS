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
// Continuously variable waveform: triangle > saw > square. Both square and
// triangle have variable slope / pulse-width. Additionally, the phase resets
// can be locked to a master frequency.
//
// A template parameter allows the generation of a master + slave signal,
// which can be used as the phase for phase distortion or modulation synthesis.

#ifndef PLAITS_DSP_OSCILLATOR_VARIABLE_SHAPE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_VARIABLE_SHAPE_OSCILLATOR_H_

/* Optional code placement for the model-0 VCF render hot path. Keep this
 * independent from the oscillator's exact-control optimization selector. */
#ifndef FM1_PLAITS_VCF_RENDER_RAM
#define FM1_PLAITS_VCF_RENDER_RAM 1
#endif

#if FM1_PLAITS_VCF_RENDER_RAM
#if defined(__APPLE__)
#define FM1_PLAITS_VCF_RAM_CODE \
  __attribute__((section("__TEXT,__ram_audio"), noinline, used))
#else
#define FM1_PLAITS_VCF_RAM_CODE \
  __attribute__((section(".ram_audio.fm1_plaits"), noinline, used))
#endif
#else
#define FM1_PLAITS_VCF_RAM_CODE
#endif

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include "plaits/dsp/oscillator/oscillator.h"

#include <algorithm>

#if defined(PLAITS_VARIABLE_SHAPE_OSCILLATOR_TEST_HOOKS)
extern "C" void plaits_variable_shape_oscillator_test_observe(
    uint32_t pw_hoisted, uint32_t waveshape_hoisted);
#endif

namespace plaits {

#if FM1_PLAITS_VCF_RENDER_RAM && defined(FM1_PLAITS_VCF_TU)
/* Match std::min/max's ordered comparisons and reference selection exactly,
 * while keeping these tiny helpers local to the VCF placement translation
 * unit. The out-of-line standard-library template can be beyond PI32's
 * long-jump range from the relocated render path. */
static inline __attribute__((always_inline)) const float&
VcfRamMinLikeStd(const float& a, const float& b) {
  return b < a ? b : a;
}

static inline __attribute__((always_inline)) const float&
VcfRamMaxLikeStd(const float& a, const float& b) {
  return a < b ? b : a;
}

#define PLAITS_VSO_MAX(a, b) VcfRamMaxLikeStd((a), (b))
#else
#define PLAITS_VSO_MAX(a, b) std::max((a), (b))
#endif

#if FM1_PLAITS_VCF_RENDER_RAM && defined(FM1_PLAITS_VCF_TU)
#define PLAITS_VSO_VCF_INLINE __attribute__((always_inline)) inline
#else
#define PLAITS_VSO_VCF_INLINE inline
#endif

class VariableShapeOscillator {
 public:
  VariableShapeOscillator() { }
  ~VariableShapeOscillator() { }

  void Init() {
    master_phase_ = 0.0f;
    slave_phase_ = 0.0f;
    next_sample_ = 0.0f;
    previous_pw_ = 0.5f;
    high_ = false;
  
    master_frequency_ = 0.0f;
    slave_frequency_ = 0.01f;
    pw_ = 0.5f;
    waveshape_ = 0.0f;
    phase_modulation_ = 0.0f;
  }

  void Render(
      float frequency,
      float pw,
      float waveshape,
      float* out,
      size_t size) {
    Render<false, false>(0.0f, frequency, pw, waveshape, 0.0f, out, size);
  }
  
  void Render(
      float master_frequency,
      float frequency,
      float pw,
      float waveshape,
      float* out,
      size_t size) {
    Render<true, false>(
        master_frequency,
        frequency,
        pw,
        waveshape,
        0.0f,
        out, size);
  }
  
  template<bool enable_sync, bool output_phase,
           bool hoist_constant_shape_controls = false>
  void Render(
      float master_frequency,
      float frequency,
      float pw,
      float waveshape,
      float phase_modulation_amount,
      float* out,
      size_t size) {
    static_assert(!hoist_constant_shape_controls ||
                      (!enable_sync && !output_phase),
                  "constant-shape hoist is only valid for free-running audio");
    if (master_frequency >= kMaxFrequency) {
      master_frequency = kMaxFrequency;
    }
    if (frequency >= kMaxFrequency) {
      frequency = kMaxFrequency;
    }
    
    if (frequency >= 0.25f) {
      pw = 0.5f;
    } else {
      CONSTRAIN(pw, frequency * 2.0f, 1.0f - 2.0f * frequency);
    }
    
    stmlib::ParameterInterpolator master_fm(
        &master_frequency_, master_frequency, size);
    stmlib::ParameterInterpolator fm(&slave_frequency_, frequency, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
    stmlib::ParameterInterpolator waveshape_modulation(
        &waveshape_, waveshape, size);
    stmlib::ParameterInterpolator phase_modulation(
        &phase_modulation_, phase_modulation_amount, size);

    /* The VCF can render a full block with unchanged shape controls after
     * their first interpolated step rounds back to the stored value. Check
     * that exact first-step condition without changing either interpolator;
     * every Next() call below still executes in the original order. The
     * fast specialization is restricted to positive finite VCF control ranges
     * with room for interpolation roundoff. */
    bool constant_pw = false;
    bool constant_waveshape = false;
    float constant_slope_up = 0.0f;
    float constant_slope_down = 0.0f;
    float constant_square_amount = 0.0f;
    float constant_triangle_amount = 0.0f;
    if (hoist_constant_shape_controls && size != 0u &&
        pw_ >= 0.5f && pw_ < 0.999f && pw >= 0.5f && pw < 0.999f) {
      const float first_pw = pwm.subsample(1.0f);
      uint32_t first_pw_bits = 0u;
      uint32_t current_pw_bits = 0u;
      __builtin_memcpy(&first_pw_bits, &first_pw, sizeof(first_pw_bits));
      __builtin_memcpy(&current_pw_bits, &pw_, sizeof(current_pw_bits));
      if (first_pw >= 0.5f && first_pw < 0.999f &&
          first_pw_bits == current_pw_bits) {
        constant_pw = true;
        constant_slope_up = 1.0f / first_pw;
        constant_slope_down = 1.0f / (1.0f - first_pw);
      }
    }
    if (hoist_constant_shape_controls && size != 0u &&
        waveshape_ >= 0.5f && waveshape_ <= 1.001f &&
        waveshape >= 0.5f && waveshape <= 1.001f) {
      const float first_waveshape = waveshape_modulation.subsample(1.0f);
      uint32_t first_waveshape_bits = 0u;
      uint32_t current_waveshape_bits = 0u;
      __builtin_memcpy(&first_waveshape_bits, &first_waveshape,
                       sizeof(first_waveshape_bits));
      __builtin_memcpy(&current_waveshape_bits, &waveshape_,
                       sizeof(current_waveshape_bits));
      if (first_waveshape >= 0.5f && first_waveshape <= 1.001f &&
          first_waveshape_bits == current_waveshape_bits) {
        constant_waveshape = true;
        constant_square_amount =
            PLAITS_VSO_MAX(first_waveshape - 0.5f, 0.0f) * 2.0f;
        constant_triangle_amount =
            PLAITS_VSO_MAX(1.0f - first_waveshape * 2.0f, 0.0f);
      }
    }
#if defined(PLAITS_VARIABLE_SHAPE_OSCILLATOR_TEST_HOOKS)
    if (hoist_constant_shape_controls && size != 0u) {
      plaits_variable_shape_oscillator_test_observe(
          constant_pw ? 1u : 0u, constant_waveshape ? 1u : 0u);
    }
#endif

    float next_sample = next_sample_;
    
    while (size--) {
      bool reset = false;
      bool transition_during_reset = false;
      float reset_time = 0.0f;

      float this_sample = next_sample;
      next_sample = 0.0f;
    
      const float master_frequency = master_fm.Next();
      const float slave_frequency = fm.Next();
      const float pw = pwm.Next();
      const float waveshape = waveshape_modulation.Next();
      
      const float square_amount = constant_waveshape ?
          constant_square_amount :
          PLAITS_VSO_MAX(waveshape - 0.5f, 0.0f) * 2.0f;
      const float triangle_amount = constant_waveshape ?
          constant_triangle_amount :
          PLAITS_VSO_MAX(1.0f - waveshape * 2.0f, 0.0f);
      
      const float slope_up = constant_pw ?
          constant_slope_up : 1.0f / (pw);
      const float slope_down = constant_pw ?
          constant_slope_down : 1.0f / (1.0f - pw);

      if (enable_sync) {
        master_phase_ += master_frequency;
        if (master_phase_ >= 1.0f) {
          master_phase_ -= 1.0f;
          reset_time = master_phase_ / master_frequency;
      
          float slave_phase_at_reset = slave_phase_ + \
              (1.0f - reset_time) * slave_frequency;
          reset = true;
          if (slave_phase_at_reset >= 1.0f) {
            slave_phase_at_reset -= 1.0f;
            transition_during_reset = true;
          }
          if (!high_ && slave_phase_at_reset >= pw) {
            transition_during_reset = true;
          }
          float value = ComputeNaiveSample(
              slave_phase_at_reset,
              pw,
              slope_up,
              slope_down,
              triangle_amount,
              square_amount);
          this_sample -= value * stmlib::ThisBlepSample(reset_time);
          next_sample -= value * stmlib::NextBlepSample(reset_time);
        }
      } else if (output_phase) {
        master_phase_ += master_frequency;
        if (master_phase_ >= 1.0f) {
          master_phase_ -= 1.0f;
        }
      }
      
      slave_phase_ += slave_frequency;
      while (transition_during_reset || !reset) {
        if (!high_) {
          if (slave_phase_ < pw) {
            break;
          }
          float t = (slave_phase_ - pw) / (previous_pw_ - pw + slave_frequency);
          float triangle_step = (slope_up + slope_down) * slave_frequency;
          triangle_step *= triangle_amount;
          
          this_sample += square_amount * stmlib::ThisBlepSample(t);
          next_sample += square_amount * stmlib::NextBlepSample(t);
          this_sample -= triangle_step * stmlib::ThisIntegratedBlepSample(t);
          next_sample -= triangle_step * stmlib::NextIntegratedBlepSample(t);
          high_ = true;
        }
      
        if (high_) {
          if (slave_phase_ < 1.0f) {
            break;
          }
          slave_phase_ -= 1.0f;
          float t = slave_phase_ / slave_frequency;
          float triangle_step = (slope_up + slope_down) * slave_frequency;
          triangle_step *= triangle_amount;

          this_sample -= (1.0f - triangle_amount) * stmlib::ThisBlepSample(t);
          next_sample -= (1.0f - triangle_amount) * stmlib::NextBlepSample(t);
          this_sample += triangle_step * stmlib::ThisIntegratedBlepSample(t);
          next_sample += triangle_step * stmlib::NextIntegratedBlepSample(t);
          high_ = false;
        }
      }
    
      if (enable_sync && reset) {
        slave_phase_ = reset_time * slave_frequency;
        high_ = false;
      }
    
      next_sample += ComputeNaiveSample(
          slave_phase_,
          pw,
          slope_up,
          slope_down,
          triangle_amount,
          square_amount);
      previous_pw_ = pw;
      
      if (output_phase) {
        float phasor = master_phase_;
        if (enable_sync) {
          // A trick to prevent discontinuities when the phase wraps around.
          const float w = 4.0f * (1.0f - master_phase_) * master_phase_;
          this_sample *= w * (2.0f - w);
          
          // Apply some asymmetry on the main phasor too.
          const float p2 = phasor * phasor;
          phasor += (p2 * p2 - phasor) * __builtin_fabsf(pw - 0.5f) * 2.0f;
        }
        *out++ = phasor + phase_modulation.Next() * this_sample;
      } else {
        *out++ = (2.0f * this_sample - 1.0f);
      }
    }
    
    next_sample_ = next_sample;
  }
  
  inline void set_master_phase(float master_phase) {
    master_phase_ = master_phase;
  }
  
 private:
  PLAITS_VSO_VCF_INLINE float ComputeNaiveSample(
      float phase,
      float pw,
      float slope_up,
      float slope_down,
      float triangle_amount,
      float square_amount) const {
    float saw = phase;
    float square = phase < pw ? 0.0f : 1.0f;
    float triangle = phase < pw
        ? phase * slope_up
        : 1.0f - (phase - pw) * slope_down;
    saw += (square - saw) * square_amount;
    saw += (triangle - saw) * triangle_amount;
    return saw;
  }

  // Oscillator state.
  float master_phase_;
  float slave_phase_;
  float next_sample_;
  float previous_pw_;
  bool high_;

  // For interpolation of parameters.
  float master_frequency_;
  float slave_frequency_;
  float pw_;
  float waveshape_;
  float phase_modulation_;

  DISALLOW_COPY_AND_ASSIGN(VariableShapeOscillator);
};

#undef PLAITS_VSO_MAX
#undef PLAITS_VSO_VCF_INLINE

/* With RAM placement enabled, keep only the free-running, non-phase-output
 * instantiation in the dedicated translation unit below. Other template
 * modes remain ordinary XIP code. */
#if FM1_PLAITS_VCF_RENDER_RAM
extern template void VariableShapeOscillator::Render<false, false, false>(
    float master_frequency,
    float frequency,
    float pw,
    float waveshape,
    float phase_modulation_amount,
    float* out,
    size_t size);
#endif
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_VARIABLE_SHAPE_OSCILLATOR_H_
