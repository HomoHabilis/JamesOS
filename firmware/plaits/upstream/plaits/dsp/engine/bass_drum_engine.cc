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
// 808 and synthetic bass drum generators.

#include "plaits/dsp/engine/bass_drum_engine.h"
#include "../../../../../hal/fm1_drum_profile.h"
#include "stmlib/utils/random.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

PLAITS_COLD_INIT_ATTR void BassDrumEngine::Init(BufferAllocator* allocator) {
  analog_bass_drum_.Init();
  synthetic_bass_drum_.Init();
  overdrive_.Init();
}

PLAITS_COLD_INIT_ATTR void BassDrumEngine::Reset() {
  
}

void BassDrumEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = EngineNoteToFrequency(parameters);
  
  const float attack_fm_amount = min(parameters.harmonics * 4.0f, 1.0f);
  const float self_fm_amount = max(min(parameters.harmonics * 4.0f - 1.0f, 1.0f), 0.0f);
  const float drive = max(parameters.harmonics * 2.0f - 1.0f, 0.0f) * \
      max(1.0f - 16.0f * f0, 0.0f);
  
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
#if FM1_PLAITS_KIT_LOCAL_RNG
  uint32_t local_random_state = Random::state();
#endif
  
#if FM1_DRUM_PROFILE
  const fm1_drum_profile_scope_t main_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_MAIN, static_cast<uint32_t>(size));
#endif
  analog_bass_drum_.Render(
      sustain,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      attack_fm_amount,
      self_fm_amount,
      out,
      size);

  overdrive_.Process(
      0.5f + 0.5f * drive,
      out,
      size);
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(main_scope);
  const fm1_drum_profile_scope_t aux_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_AUX, static_cast<uint32_t>(size));
#endif

  synthetic_bass_drum_.Render(
      sustain,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      sustain
          ? parameters.harmonics
          : 0.4f - 0.25f * parameters.morph * parameters.morph,
      min(parameters.harmonics * 2.0f, 1.0f),
      max(parameters.harmonics * 2.0f - 1.0f, 0.0f),
      aux,
      size
#if FM1_PLAITS_KIT_LOCAL_RNG
      , &local_random_state
#endif
      );
#if FM1_PLAITS_KIT_LOCAL_RNG
  Random::Seed(local_random_state);
#endif
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(aux_scope);
#endif
}

#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
void BassDrumEngine::RenderKitRate2(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  /* FIFO slices are usually 24 frames. Keep irregular odd slices on the
   * reference path so a half-rate pair never crosses a caller boundary. */
  if (size & 1u) {
    synthetic_bass_drum_.SetFullRateFilter();
    Render(parameters, out, aux, size, already_enveloped);
    synthetic_bass_drum_.SetRate2PreviousOutput(aux[size - 1u]);
    return;
  }

  const float f0 = EngineNoteToFrequency(parameters);
  const float attack_fm_amount = min(parameters.harmonics * 4.0f, 1.0f);
  const float self_fm_amount = max(min(parameters.harmonics * 4.0f - 1.0f, 1.0f), 0.0f);
  const float drive = max(parameters.harmonics * 2.0f - 1.0f, 0.0f) *
      max(1.0f - 16.0f * f0, 0.0f);
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
#if FM1_PLAITS_KIT_LOCAL_RNG
  uint32_t local_random_state = Random::state();
#else
  uint32_t local_random_state = 0u;
#endif

#if FM1_DRUM_PROFILE
  const fm1_drum_profile_scope_t main_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_MAIN, static_cast<uint32_t>(size));
#endif
  analog_bass_drum_.Render(
      sustain,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      attack_fm_amount,
      self_fm_amount,
      out,
      size);
  overdrive_.Process(0.5f + 0.5f * drive, out, size);
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(main_scope);
  const fm1_drum_profile_scope_t aux_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_AUX, static_cast<uint32_t>(size));
#endif

  synthetic_bass_drum_.RenderRate2<FM1_PLAITS_KIT_LOCAL_RNG>(
      sustain,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      sustain
          ? parameters.harmonics
          : 0.4f - 0.25f * parameters.morph * parameters.morph,
      min(parameters.harmonics * 2.0f, 1.0f),
      max(parameters.harmonics * 2.0f - 1.0f, 0.0f),
      aux,
      size,
      FM1_PLAITS_KIT_LOCAL_RNG ? &local_random_state : nullptr);
#if FM1_PLAITS_KIT_LOCAL_RNG
  Random::Seed(local_random_state);
#endif
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(aux_scope);
#endif
}
#endif

}  // namespace plaits
