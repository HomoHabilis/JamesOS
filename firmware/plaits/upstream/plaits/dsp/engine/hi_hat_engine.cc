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
// 808-style HH with two noise sources - one faithful to the original, the other
// more metallic.

#include "plaits/dsp/engine/hi_hat_engine.h"
#include "../../../../../hal/fm1_drum_profile.h"

namespace plaits {

using namespace stmlib;

PLAITS_COLD_INIT_ATTR void HiHatEngine::Init(BufferAllocator* allocator) {
  hi_hat_1_.Init();
  hi_hat_2_.Init();
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize * 2);
  // Both hat models consume normalized Plaits controls. -1 is outside that
  // domain, so the first render always computes the coefficients.
  cached_tone_ = -1.0f;
  cached_decay_ = -1.0f;
  cached_coefficients_.envelope_decay = 0.0f;
  cached_coefficients_.cut_decay = 0.0f;
  cached_coefficients_.cutoff = 0.0f;
}

PLAITS_COLD_INIT_ATTR void HiHatEngine::Reset() {
  
}

void HiHatEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = EngineNoteToFrequency(parameters);

  const float tone = parameters.timbre;
  const float decay = parameters.morph;
  HiHatControlCoefficients coefficients;
  const bool cacheable = tone >= 0.0f && tone <= 1.0f &&
      decay >= 0.0f && decay <= 1.0f;
  if (cacheable && tone == cached_tone_ && decay == cached_decay_) {
    coefficients = cached_coefficients_;
  } else {
    // Preserve the original expression order exactly. Out-of-domain direct
    // Engine callers use the same calculation but do not populate the cache.
    coefficients.envelope_decay = 1.0f - 0.003f *
        SemitonesToRatio(-decay * 84.0f);
    coefficients.cut_decay = 1.0f - 0.0025f *
        SemitonesToRatio(-decay * 36.0f);
#if FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION && \
    FM1_PLAITS_RATE_TIME_CALIBRATION
    /* These are per-sample VCA coefficients. Preserve the 44.1 kHz time
     * constant at any non-reference source rate by scaling coefficient loss
     * in proportion to the rate change. The loss is at most 0.003, so this
     * first-order transform closely matches pow(c, 44100 / kSampleRate)
     * while avoiding powf in a live control update. */
    if (cacheable && kSampleRateHz != 44100u) {
      const float rate_scale = 44100.0f / kSampleRate;
      coefficients.envelope_decay = 1.0f -
          (1.0f - coefficients.envelope_decay) * rate_scale;
      coefficients.cut_decay = 1.0f -
          (1.0f - coefficients.cut_decay) * rate_scale;
    }
#endif
    coefficients.cutoff = 150.0f / kSampleRate *
        SemitonesToRatio(tone * 72.0f);
    CONSTRAIN(coefficients.cutoff, 0.0f, 16000.0f / kSampleRate);
    if (cacheable) {
      cached_tone_ = tone;
      cached_decay_ = decay;
      cached_coefficients_ = coefficients;
    }
  }
  
#if FM1_DRUM_PROFILE
  const fm1_drum_profile_scope_t main_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_MAIN, static_cast<uint32_t>(size));
#endif
  hi_hat_1_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      tone,
      decay,
      coefficients,
      parameters.harmonics,
      temp_buffer_,
      temp_buffer_ + size,
      out,
      size);
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(main_scope);
  const fm1_drum_profile_scope_t aux_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_AUX, static_cast<uint32_t>(size));
#endif
  
  hi_hat_2_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      tone,
      decay,
      coefficients,
      parameters.harmonics,
      temp_buffer_,
      temp_buffer_ + size,
      aux,
      size);
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(aux_scope);
#endif
}

}  // namespace plaits
