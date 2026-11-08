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
// Naive bass drum model (modulated oscillator with FM + envelope).
// Inadvertently 909-ish.

#ifndef PLAITS_DSP_DRUMS_SYNTHETIC_BASS_DRUM_H_
#define PLAITS_DSP_DRUMS_SYNTHETIC_BASS_DRUM_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/drums/kit_random.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

class SyntheticBassDrumClick {
 public:
  SyntheticBassDrumClick() { }
  ~SyntheticBassDrumClick() { }
  
  void Init() {
    lp_ = 0.0f;
    hp_ = 0.0f;
    filter_.Init();
    filter_.set_f_q<stmlib::FREQUENCY_FAST>(5000.0f / kSampleRate, 2.0f);
  }

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
  void SetRate2() {
    filter_.set_f_q<stmlib::FREQUENCY_FAST>(10000.0f / kSampleRate, 2.0f);
  }

  void SetFullRate() {
    filter_.set_f_q<stmlib::FREQUENCY_FAST>(5000.0f / kSampleRate, 2.0f);
  }
#endif
  
  float Process(float in) {
    SLOPE(lp_, in, 0.5f, 0.1f);
    ONE_POLE(hp_, lp_, 0.04f);
    return filter_.Process<stmlib::FILTER_MODE_LOW_PASS>(lp_ - hp_);
  }

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
  float ProcessRate2(float in) {
    SLOPE(lp_, in, 0.75f, 0.19f);
    ONE_POLE(hp_, lp_, 0.0784f);
    return filter_.Process<stmlib::FILTER_MODE_LOW_PASS>(lp_ - hp_);
  }
#endif
  
 private:
  float lp_;
  float hp_;
  stmlib::Svf filter_;
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticBassDrumClick);
};

class SyntheticBassDrumAttackNoise {
 public:
  SyntheticBassDrumAttackNoise() { }
  ~SyntheticBassDrumAttackNoise() { }
  
  void Init() {
    lp_ = 0.0f;
    hp_ = 0.0f;
  }
  
  template <bool local_random>
  float Render(uint32_t* local_random_state) {
    float sample = KitRandomFloat<local_random>(local_random_state);
    ONE_POLE(lp_, sample, 0.05f);
    ONE_POLE(hp_, lp_, 0.005f);
    return lp_ - hp_;
  }

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
  template <bool local_random>
  float RenderRate2(uint32_t* local_random_state) {
    const float sample = 0.5f * (
        KitRandomFloat<local_random>(local_random_state) +
        KitRandomFloat<local_random>(local_random_state));
    ONE_POLE(lp_, sample, 0.0975f);
    ONE_POLE(hp_, lp_, 0.009975f);
    return lp_ - hp_;
  }
#endif
  
 private:
  float lp_;
  float hp_;
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticBassDrumAttackNoise);
};

class SyntheticBassDrum {
 public:
  SyntheticBassDrum() { }
  ~SyntheticBassDrum() { }

  PLAITS_COLD_INIT_ATTR void Init() {
    phase_ = 0.0f;
    phase_noise_ = 0.0f;
    f0_ = 0.0f;
    fm_ = 0.0f;
    fm_lp_ = 0.0f;
    body_env_lp_ = 0.0f;
    body_env_ = 0.0f;
    body_env_pulse_width_ = 0;
    fm_pulse_width_ = 0;
    tone_lp_ = 0.0f;
    sustain_gain_ = 0.0f;
#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
    rate2_previous_output_ = 0.0f;
#endif
#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    coefficient_cache_valid_ = false;
#endif
#if FM1_PLAITS_KIT_CACHE_DERIVED_KICK
    derived_render_cache_valid_ = false;
#endif
    
    click_.Init();
    noise_.Init();
  }

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
  void SetFullRateFilter() { click_.SetFullRate(); }
  void SetRate2PreviousOutput(float value) { rate2_previous_output_ = value; }
#endif
  
  inline float DistortedSine(float phase, float phase_noise, float dirtiness) {
    phase += phase_noise * dirtiness;
    MAKE_INTEGRAL_FRACTIONAL(phase);
    phase = phase_fractional;
    float triangle = (phase < 0.5f ? phase : 1.0f - phase) * 4.0f - 1.0f;
    float sine = 2.0f * triangle / (1.0f + __builtin_fabsf(triangle));
    float clean_sine = Sine(phase + 0.75f);
    return sine + (1.0f - dirtiness) * (clean_sine - sine);
  }
  
  inline float TransistorVCA(float s, float gain) {
    s = (s - 0.6f) * gain;
    return 3.0f * s / (2.0f + __builtin_fabsf(s)) + gain * 0.3f;
  }
  
  void Render(
      bool sustain,
      bool trigger,
      float accent,
      float f0,
      float tone,
      float decay,
      float dirtiness,
      float fm_envelope_amount,
      float fm_envelope_decay,
      float* out,
      size_t size) {
#if BR_PLAITS_KIT_KICK_SYNTH_RATE2 && BR_PLAITS_KIT_KICK_SHARED_KERNEL
    RenderShared<false>(false, sustain, trigger, accent, f0, tone, decay,
        dirtiness, fm_envelope_amount, fm_envelope_decay, out, size, nullptr);
#else
    if (sustain) RenderImpl<true, false>(trigger, accent, f0, tone, decay, dirtiness, fm_envelope_amount, fm_envelope_decay, out, size, nullptr);
    else RenderImpl<false, false>(trigger, accent, f0, tone, decay, dirtiness, fm_envelope_amount, fm_envelope_decay, out, size, nullptr);
#endif
  }

#if FM1_PLAITS_KIT_LOCAL_RNG
  void Render(
      bool sustain, bool trigger, float accent, float f0, float tone,
      float decay, float dirtiness, float fm_envelope_amount,
      float fm_envelope_decay, float* out, size_t size,
      uint32_t* local_random_state) {
#if BR_PLAITS_KIT_KICK_SYNTH_RATE2 && BR_PLAITS_KIT_KICK_SHARED_KERNEL
    RenderShared<true>(false, sustain, trigger, accent, f0, tone, decay,
        dirtiness, fm_envelope_amount, fm_envelope_decay, out, size,
        local_random_state);
#else
    if (sustain) RenderImpl<true, true>(trigger, accent, f0, tone, decay, dirtiness, fm_envelope_amount, fm_envelope_decay, out, size, local_random_state);
    else RenderImpl<false, true>(trigger, accent, f0, tone, decay, dirtiness, fm_envelope_amount, fm_envelope_decay, out, size, local_random_state);
#endif
  }
#endif

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
  template <bool local_random>
  void RenderRate2(
      bool sustain, bool trigger, float accent, float f0, float tone,
      float decay, float dirtiness, float fm_envelope_amount,
      float fm_envelope_decay, float* out, size_t size,
      uint32_t* local_random_state) {
#if BR_PLAITS_KIT_KICK_SHARED_KERNEL
    RenderShared<local_random>(true, sustain, trigger, accent, f0, tone, decay,
        dirtiness, fm_envelope_amount, fm_envelope_decay, out, size,
        local_random_state);
#else
    const size_t half_size = size >> 1;
    if (!half_size) return;

    decay *= decay;
    fm_envelope_decay *= fm_envelope_decay;
    const float fm_decay = 1.0f - 1.0f /
        (0.008f * (1.0f + fm_envelope_decay * 4.0f) * kSampleRate);
    const float fm_decay_rate2 = fm_decay * fm_decay;
    dirtiness *= std::max(1.0f - 8.0f * f0, 0.0f);

#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    if (!coefficient_cache_valid_ || cached_body_decay_ != decay ||
        cached_tone_ != tone) {
      cached_body_decay_ = decay;
      cached_body_decay_ratio_ = stmlib::SemitonesToRatio(-decay * 60.0f);
      cached_tone_ = tone;
      cached_tone_ratio_ = stmlib::SemitonesToRatio(tone * 108.0f);
      coefficient_cache_valid_ = true;
    }
    const float body_decay_ratio = cached_body_decay_ratio_;
    const float tone_ratio = cached_tone_ratio_;
#else
    const float body_decay_ratio = stmlib::SemitonesToRatio(-decay * 60.0f);
    const float tone_ratio = stmlib::SemitonesToRatio(tone * 108.0f);
#endif
    const float body_env_decay = 1.0f -
        1.0f / (0.02f * kSampleRate) * body_decay_ratio;
    const float body_env_decay_rate2 = body_env_decay * body_env_decay;
    const float transient_env_decay =
        1.0f - 1.0f / (0.005f * kSampleRate);
    const float transient_env_decay_rate2 =
        transient_env_decay * transient_env_decay;
    const float tone_f = std::min(4.0f * f0 * tone_ratio, 1.0f);
    const float tone_f_rate2 = 1.0f - (1.0f - tone_f) * (1.0f - tone_f);
    const float transient_level = tone;

    if (trigger) {
      fm_ = 1.0f;
      body_env_ = transient_env_ = 0.3f + 0.7f * accent;
      body_env_pulse_width_ = kSampleRate * 0.001f;
      fm_pulse_width_ = kSampleRate * 0.0013f;
    }

    float f0_rate2 = 2.0f * f0_;
    stmlib::ParameterInterpolator f0_mod(&f0_rate2, 2.0f * f0, half_size);
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_, accent * decay, half_size);
    click_.SetRate2();

    float previous_output = rate2_previous_output_;
    for (size_t i = 0; i < half_size; ++i) {
      const float random_a = KitRandomFloat<local_random>(local_random_state);
      const float random_b = KitRandomFloat<local_random>(local_random_state);
      const float phase_noise_input = 0.5f * (random_a + random_b) - 0.5f;
      ONE_POLE(phase_noise_, phase_noise_input, 0.003996f);

      float mix = 0.0f;
      const float half_phase_step = f0_mod.Next();
      if (sustain) {
        phase_ += half_phase_step;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        const float body = DistortedSine(phase_, phase_noise_, dirtiness);
        mix -= TransistorVCA(body, sustain_gain.Next());
      } else {
        float phase_midpoint = phase_;
        if (fm_pulse_width_) {
          fm_pulse_width_ = fm_pulse_width_ > 2 ? fm_pulse_width_ - 2 : 0;
          phase_ = phase_midpoint = 0.25f;
        } else {
          fm_ *= fm_decay_rate2;
          const float fm = 1.0f + fm_envelope_amount * 3.5f * fm_lp_;
          const float step = std::min(half_phase_step * fm, 1.0f);
          phase_midpoint += 0.5f * step;
          if (phase_midpoint >= 1.0f) phase_midpoint -= 1.0f;
          phase_ += step;
          if (phase_ >= 1.0f) phase_ -= 1.0f;
        }

        if (body_env_pulse_width_) {
          body_env_pulse_width_ = body_env_pulse_width_ > 2
              ? body_env_pulse_width_ - 2 : 0;
        } else {
          body_env_ *= body_env_decay_rate2;
          transient_env_ *= transient_env_decay_rate2;
        }

        ONE_POLE(body_env_lp_, body_env_, 0.19f);
        ONE_POLE(transient_env_lp_, transient_env_, 0.19f);
        ONE_POLE(fm_lp_, fm_, 0.19f);

        const float body = DistortedSine(
            phase_midpoint, phase_noise_, dirtiness);
        const float transient = click_.ProcessRate2(
            body_env_pulse_width_ ? 0.0f : 1.0f) +
            noise_.RenderRate2<local_random>(local_random_state);
        mix -= TransistorVCA(body, body_env_lp_);
        mix -= transient * transient_env_lp_ * transient_level;
      }

      ONE_POLE(tone_lp_, mix, tone_f_rate2);
      const float current_output = tone_lp_;
      *out++ = previous_output + 0.25f * (current_output - previous_output);
      *out++ = previous_output + 0.75f * (current_output - previous_output);
      previous_output = current_output;
    }
    f0_ = f0;
    rate2_previous_output_ = previous_output;
#endif
  }
#endif

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2 && BR_PLAITS_KIT_KICK_SHARED_KERNEL
  template <bool local_random>
  void RenderShared(
      bool rate2,
      bool sustain,
      bool trigger,
      float accent,
      float f0,
      float tone,
      float decay,
      float dirtiness,
      float fm_envelope_amount,
      float fm_envelope_decay,
      float* out,
      size_t size,
      uint32_t* local_random_state) {
    const size_t updates = rate2 ? (size >> 1) : size;
    if (!updates) return;

#if FM1_PLAITS_KIT_CACHE_DERIVED_KICK
    const float input_decay = decay;
    const float input_fm_envelope_decay = fm_envelope_decay;
    const float input_dirtiness = dirtiness;
    const bool refresh_render_coefficients = !derived_render_cache_valid_ ||
        cached_render_rate2_ != rate2 || cached_render_f0_ != f0 ||
        cached_render_tone_ != tone || cached_render_decay_ != input_decay ||
        cached_render_dirtiness_ != input_dirtiness ||
        cached_render_fm_amount_ != fm_envelope_amount ||
        cached_render_fm_decay_ != input_fm_envelope_decay;
    float fm_decay;
    float fm_decay_rate2;
    float body_env_decay;
    float body_env_decay_rate2;
    float transient_env_decay;
    float transient_env_decay_rate2;
    float tone_f;
    float tone_f_rate2;
    float transient_level;
    if (refresh_render_coefficients) {
      decay *= decay;
      fm_envelope_decay *= fm_envelope_decay;
      fm_decay = 1.0f - 1.0f /
          (0.008f * (1.0f + fm_envelope_decay * 4.0f) * kSampleRate);
      fm_decay_rate2 = fm_decay * fm_decay;
      dirtiness *= std::max(1.0f - 8.0f * f0, 0.0f);

#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
      if (!coefficient_cache_valid_ || cached_body_decay_ != decay ||
          cached_tone_ != tone) {
        cached_body_decay_ = decay;
        cached_body_decay_ratio_ = stmlib::SemitonesToRatio(-decay * 60.0f);
        cached_tone_ = tone;
        cached_tone_ratio_ = stmlib::SemitonesToRatio(tone * 108.0f);
        coefficient_cache_valid_ = true;
      }
      const float body_decay_ratio = cached_body_decay_ratio_;
      const float tone_ratio = cached_tone_ratio_;
#else
      const float body_decay_ratio = stmlib::SemitonesToRatio(-decay * 60.0f);
      const float tone_ratio = stmlib::SemitonesToRatio(tone * 108.0f);
#endif
      body_env_decay = 1.0f -
          1.0f / (0.02f * kSampleRate) * body_decay_ratio;
      body_env_decay_rate2 = body_env_decay * body_env_decay;
      transient_env_decay = 1.0f - 1.0f / (0.005f * kSampleRate);
      transient_env_decay_rate2 =
          transient_env_decay * transient_env_decay;
      tone_f = std::min(4.0f * f0 * tone_ratio, 1.0f);
      tone_f_rate2 = 1.0f - (1.0f - tone_f) * (1.0f - tone_f);
      transient_level = tone;

      cached_render_rate2_ = rate2;
      cached_render_f0_ = f0;
      cached_render_tone_ = tone;
      cached_render_decay_ = input_decay;
      cached_render_dirtiness_ = input_dirtiness;
      cached_render_fm_amount_ = fm_envelope_amount;
      cached_render_fm_decay_ = input_fm_envelope_decay;
      cached_render_decay_squared_ = decay;
      cached_render_fm_decay_squared_ = fm_envelope_decay;
      cached_render_dirtiness_scaled_ = dirtiness;
      cached_render_fm_decay_coeff_ = fm_decay;
      cached_render_fm_decay_rate2_ = fm_decay_rate2;
      cached_render_body_decay_coeff_ = body_env_decay;
      cached_render_body_decay_rate2_ = body_env_decay_rate2;
      cached_render_transient_decay_coeff_ = transient_env_decay;
      cached_render_transient_decay_rate2_ = transient_env_decay_rate2;
      cached_render_tone_f_ = tone_f;
      cached_render_tone_f_rate2_ = tone_f_rate2;
      cached_render_transient_level_ = transient_level;
      derived_render_cache_valid_ = true;
    } else {
      decay = cached_render_decay_squared_;
      fm_envelope_decay = cached_render_fm_decay_squared_;
      dirtiness = cached_render_dirtiness_scaled_;
      fm_decay = cached_render_fm_decay_coeff_;
      fm_decay_rate2 = cached_render_fm_decay_rate2_;
      body_env_decay = cached_render_body_decay_coeff_;
      body_env_decay_rate2 = cached_render_body_decay_rate2_;
      transient_env_decay = cached_render_transient_decay_coeff_;
      transient_env_decay_rate2 = cached_render_transient_decay_rate2_;
      tone_f = cached_render_tone_f_;
      tone_f_rate2 = cached_render_tone_f_rate2_;
      transient_level = cached_render_transient_level_;
    }
#else
    decay *= decay;
    fm_envelope_decay *= fm_envelope_decay;
    const float fm_decay = 1.0f - 1.0f /
        (0.008f * (1.0f + fm_envelope_decay * 4.0f) * kSampleRate);
    const float fm_decay_rate2 = fm_decay * fm_decay;
    dirtiness *= std::max(1.0f - 8.0f * f0, 0.0f);

#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    if (!coefficient_cache_valid_ || cached_body_decay_ != decay ||
        cached_tone_ != tone) {
      cached_body_decay_ = decay;
      cached_body_decay_ratio_ = stmlib::SemitonesToRatio(-decay * 60.0f);
      cached_tone_ = tone;
      cached_tone_ratio_ = stmlib::SemitonesToRatio(tone * 108.0f);
      coefficient_cache_valid_ = true;
    }
    const float body_decay_ratio = cached_body_decay_ratio_;
    const float tone_ratio = cached_tone_ratio_;
#else
    const float body_decay_ratio = stmlib::SemitonesToRatio(-decay * 60.0f);
    const float tone_ratio = stmlib::SemitonesToRatio(tone * 108.0f);
#endif
    const float body_env_decay = 1.0f -
        1.0f / (0.02f * kSampleRate) * body_decay_ratio;
    const float body_env_decay_rate2 = body_env_decay * body_env_decay;
    const float transient_env_decay =
        1.0f - 1.0f / (0.005f * kSampleRate);
    const float transient_env_decay_rate2 =
        transient_env_decay * transient_env_decay;
    const float tone_f = std::min(4.0f * f0 * tone_ratio, 1.0f);
    const float tone_f_rate2 = 1.0f - (1.0f - tone_f) * (1.0f - tone_f);
    const float transient_level = tone;
#endif

    if (trigger) {
      fm_ = 1.0f;
      body_env_ = transient_env_ = 0.3f + 0.7f * accent;
      body_env_pulse_width_ = kSampleRate * 0.001f;
      fm_pulse_width_ = kSampleRate * 0.0013f;
    }

    const float gain_target = accent * decay;
    float gain_value = sustain_gain_;
    const float gain_increment = (gain_target - sustain_gain_) /
        static_cast<float>(updates);
    float f0_value = rate2 ? 2.0f * f0_ : f0_;
    const float f0_target = rate2 ? 2.0f * f0 : f0;
    const float f0_increment = (f0_target - f0_value) /
        static_cast<float>(updates);
    if (rate2) click_.SetRate2(); else click_.SetFullRate();

    float previous_output = rate2_previous_output_;
    for (size_t i = 0; i < updates; ++i) {
      if (rate2) {
        const float random_a = KitRandomFloat<local_random>(local_random_state);
        const float random_b = KitRandomFloat<local_random>(local_random_state);
        const float phase_noise_input = 0.5f * (random_a + random_b) - 0.5f;
        ONE_POLE(phase_noise_, phase_noise_input, 0.003996f);
      } else {
        ONE_POLE(phase_noise_,
            KitRandomFloat<local_random>(local_random_state) - 0.5f, 0.002f);
      }

      float mix = 0.0f;
      if (rate2) f0_value += f0_increment;
      if (sustain) {
        if (!rate2) f0_value += f0_increment;
        phase_ += f0_value;
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        const float body = DistortedSine(phase_, phase_noise_, dirtiness);
        gain_value += gain_increment;
        mix -= TransistorVCA(body, gain_value);
      } else {
        float phase_midpoint = phase_;
        if (fm_pulse_width_) {
          if (rate2) {
            fm_pulse_width_ = fm_pulse_width_ > 2
                ? fm_pulse_width_ - 2 : 0;
          } else {
            --fm_pulse_width_;
          }
          phase_ = phase_midpoint = 0.25f;
        } else {
          if (!rate2) f0_value += f0_increment;
          fm_ *= rate2 ? fm_decay_rate2 : fm_decay;
          const float fm = 1.0f + fm_envelope_amount * 3.5f * fm_lp_;
          const float step = std::min(f0_value * fm, rate2 ? 1.0f : 0.5f);
          phase_midpoint += rate2 ? 0.5f * step : step;
          if (phase_midpoint >= 1.0f) phase_midpoint -= 1.0f;
          phase_ += step;
          if (phase_ >= 1.0f) phase_ -= 1.0f;
        }

        if (body_env_pulse_width_) {
          if (rate2) {
            body_env_pulse_width_ = body_env_pulse_width_ > 2
                ? body_env_pulse_width_ - 2 : 0;
          } else {
            --body_env_pulse_width_;
          }
        } else {
          body_env_ *= rate2 ? body_env_decay_rate2 : body_env_decay;
          transient_env_ *= rate2 ? transient_env_decay_rate2 : transient_env_decay;
        }

        const float envelope_lp_f = rate2 ? 0.19f : 0.1f;
        ONE_POLE(body_env_lp_, body_env_, envelope_lp_f);
        ONE_POLE(transient_env_lp_, transient_env_, envelope_lp_f);
        ONE_POLE(fm_lp_, fm_, envelope_lp_f);

        const float body = DistortedSine(
            phase_midpoint, phase_noise_, dirtiness);
        const float transient = rate2
            ? click_.ProcessRate2(body_env_pulse_width_ ? 0.0f : 1.0f) +
                noise_.RenderRate2<local_random>(local_random_state)
            : click_.Process(body_env_pulse_width_ ? 0.0f : 1.0f) +
                noise_.Render<local_random>(local_random_state);
        mix -= TransistorVCA(body, body_env_lp_);
        mix -= transient * transient_env_lp_ * transient_level;
      }

      ONE_POLE(tone_lp_, mix, rate2 ? tone_f_rate2 : tone_f);
      if (rate2) {
        const float current_output = tone_lp_;
        *out++ = previous_output + 0.25f * (current_output - previous_output);
        *out++ = previous_output + 0.75f * (current_output - previous_output);
        previous_output = current_output;
      } else {
        *out++ = tone_lp_;
      }
    }
    sustain_gain_ = gain_value;
    f0_ = rate2 ? f0 : f0_value;
    rate2_previous_output_ = rate2 ? previous_output : rate2_previous_output_;
  }
#endif

#if !(BR_PLAITS_KIT_KICK_SYNTH_RATE2 && BR_PLAITS_KIT_KICK_SHARED_KERNEL)
  template <bool sustain, bool local_random>
  void RenderImpl(
      bool trigger,
      float accent,
      float f0,
      float tone,
      float decay,
      float dirtiness,
      float fm_envelope_amount,
      float fm_envelope_decay,
      float* out,
      size_t size,
      uint32_t* local_random_state) {
    decay *= decay;
    fm_envelope_decay *= fm_envelope_decay;
    
    stmlib::ParameterInterpolator f0_mod(&f0_, f0, size);
    
    dirtiness *= std::max(1.0f - 8.0f * f0, 0.0f);
    
    const float fm_decay = 1.0f - \
        1.0f / (0.008f * (1.0f + fm_envelope_decay * 4.0f) * kSampleRate);

#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
    if (!coefficient_cache_valid_ || cached_body_decay_ != decay ||
        cached_tone_ != tone) {
      cached_body_decay_ = decay;
      cached_body_decay_ratio_ = stmlib::SemitonesToRatio(-decay * 60.0f);
      cached_tone_ = tone;
      cached_tone_ratio_ = stmlib::SemitonesToRatio(tone * 108.0f);
      coefficient_cache_valid_ = true;
    }
    const float body_decay_ratio = cached_body_decay_ratio_;
    const float tone_ratio = cached_tone_ratio_;
#else
    const float body_decay_ratio = stmlib::SemitonesToRatio(-decay * 60.0f);
    const float tone_ratio = stmlib::SemitonesToRatio(tone * 108.0f);
#endif
    const float body_env_decay = 1.0f - 1.0f / (0.02f * kSampleRate) * \
        body_decay_ratio;
    const float transient_env_decay = 1.0f - 1.0f / (0.005f * kSampleRate);
    const float tone_f = std::min(
        4.0f * f0 * tone_ratio,
        1.0f);
    const float transient_level = tone;
    
    if (trigger) {
      fm_ = 1.0f;
      body_env_ = transient_env_ = 0.3f + 0.7f * accent;
      body_env_pulse_width_ = kSampleRate * 0.001f;
      fm_pulse_width_ = kSampleRate * 0.0013f;
    }
    
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_,
        accent * decay,
        size);
    
    while (size--) {
      ONE_POLE(phase_noise_, KitRandomFloat<local_random>(local_random_state) - 0.5f, 0.002f);
      
      float mix = 0.0f;

      if (sustain) {
        phase_ += f0_mod.Next();
        if (phase_ >= 1.0f) {
          phase_ -= 1.0f;
        }
        float body = DistortedSine(phase_, phase_noise_, dirtiness);
        mix -= TransistorVCA(body, sustain_gain.Next());
      } else {
        if (fm_pulse_width_) {
          --fm_pulse_width_;
          phase_ = 0.25f;
        } else {
          fm_ *= fm_decay;
          float fm = 1.0f + fm_envelope_amount * 3.5f * fm_lp_;
          phase_ += std::min(f0_mod.Next() * fm, 0.5f);
          if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
          }
        }
      
        if (body_env_pulse_width_) {
          --body_env_pulse_width_;
        } else {
          body_env_ *= body_env_decay;
          transient_env_ *= transient_env_decay;
        }
      
        const float envelope_lp_f = 0.1f;
        ONE_POLE(body_env_lp_, body_env_, envelope_lp_f);
        ONE_POLE(transient_env_lp_, transient_env_, envelope_lp_f);
        ONE_POLE(fm_lp_, fm_, envelope_lp_f);
      
        float body = DistortedSine(phase_, phase_noise_, dirtiness);
        float transient = click_.Process(
            body_env_pulse_width_ ? 0.0f : 1.0f) +
            noise_.Render<local_random>(local_random_state);
      
        mix -= TransistorVCA(body, body_env_lp_);
        mix -= transient * transient_env_lp_ * transient_level;
      }

      ONE_POLE(tone_lp_, mix, tone_f);
      *out++ = tone_lp_;
    }
  }
#endif

 private:
  float f0_;
  float phase_;
  float phase_noise_;

  float fm_;
  float fm_lp_;
  float body_env_;
  float body_env_lp_;
  float transient_env_;
  float transient_env_lp_;
  
  float sustain_gain_;
#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
  float rate2_previous_output_;
#endif
  
  float tone_lp_;
  
  SyntheticBassDrumClick click_;
  SyntheticBassDrumAttackNoise noise_;

#if FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
  float cached_body_decay_;
  float cached_body_decay_ratio_;
  float cached_tone_;
  float cached_tone_ratio_;
  bool coefficient_cache_valid_;
#endif
#if FM1_PLAITS_KIT_CACHE_DERIVED_KICK
  float cached_render_f0_;
  float cached_render_tone_;
  float cached_render_decay_;
  float cached_render_dirtiness_;
  float cached_render_fm_amount_;
  float cached_render_fm_decay_;
  float cached_render_decay_squared_;
  float cached_render_fm_decay_squared_;
  float cached_render_dirtiness_scaled_;
  float cached_render_fm_decay_coeff_;
  float cached_render_fm_decay_rate2_;
  float cached_render_body_decay_coeff_;
  float cached_render_body_decay_rate2_;
  float cached_render_transient_decay_coeff_;
  float cached_render_transient_decay_rate2_;
  float cached_render_tone_f_;
  float cached_render_tone_f_rate2_;
  float cached_render_transient_level_;
  bool cached_render_rate2_;
  bool derived_render_cache_valid_;
#endif
  
  int body_env_pulse_width_;
  int fm_pulse_width_;
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticBassDrum);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_SYNTHETIC_BASS_DRUM_H_
