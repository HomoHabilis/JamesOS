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
// 808 and synthetic snare drum generators.

#include "plaits/dsp/engine/snare_drum_engine.h"
#include "../../../../../hal/fm1_drum_profile.h"
#include "stmlib/utils/random.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

PLAITS_COLD_INIT_ATTR void SnareDrumEngine::Init(BufferAllocator* allocator) {
  analog_snare_drum_.Init();
  synthetic_snare_drum_.Init();
}

PLAITS_COLD_INIT_ATTR void SnareDrumEngine::Reset() {
  
}

void SnareDrumEngine::Render(
    const EngineParameters& parameters,
    float* PLAITS_RENDER_OUT_RESTRICT out,
    float* PLAITS_RENDER_OUT_RESTRICT aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = EngineNoteToFrequency(parameters);
#if FM1_PLAITS_KIT_LOCAL_RNG
  uint32_t local_random_state = Random::state();
#endif
  
#if FM1_DRUM_PROFILE
  const fm1_drum_profile_scope_t main_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_MAIN, static_cast<uint32_t>(size));
#endif
  analog_snare_drum_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      parameters.harmonics,
      out,
      size
#if FM1_PLAITS_KIT_LOCAL_RNG
      , &local_random_state
#endif
      );
#if FM1_DRUM_PROFILE
  fm1_drum_profile_end(main_scope);
  const fm1_drum_profile_scope_t aux_scope =
      fm1_drum_profile_begin(FM1_DRUM_PROFILE_AUX, static_cast<uint32_t>(size));
#endif
  
  synthetic_snare_drum_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
      f0,
      parameters.timbre,
      parameters.morph,
      parameters.harmonics,
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

}  // namespace plaits
