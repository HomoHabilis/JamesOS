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
// One voice of modal synthesis.

#include "plaits/dsp/engine/modal_engine.h"
#include "../../../../../src/instrument_failure_job_profile.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

PLAITS_COLD_INIT_ATTR void ModalEngine::Init(BufferAllocator* allocator) {
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
  harmonics_lp_ = 0.0f;
  Reset();
}

PLAITS_COLD_INIT_ATTR void ModalEngine::Reset() {
  voice_.Init();
}

void ModalEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
  br_audio_model_substage_trace_t* profile =
      br_audio_model_substage_current_trace();
  if (profile) {
    (void)br_audio_model_substage_begin(
        profile, BR_AUDIO_MODEL_SUBSTAGE_ID_PREP);
  }
#endif
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
#if FM1_PLAITS_RATE_TIME_CALIBRATION
  /* Match the 44.1 kHz time constant for each smoothing update. */
#if PLAITS_INTERNAL_SAMPLE_RATE == 32000
  const float harmonics_smoothing = 0.0137551406f;
#elif PLAITS_INTERNAL_SAMPLE_RATE == 48000
  const float harmonics_smoothing = 0.00919124594f;
#else
  const float harmonics_smoothing = 0.0114758271f;
#endif
#else
  const float harmonics_smoothing = 0.01f;
#endif
  
#if PLAITS_USE_RENDER_CHUNK24
  float rendered_harmonics;
#if FM1_MODAL20_RENDER_CHUNK24
  if (size == kMaxBlockSize) {
    ONE_POLE(harmonics_lp_, parameters.harmonics, harmonics_smoothing);
    const float first = harmonics_lp_;
    ONE_POLE(harmonics_lp_, parameters.harmonics, harmonics_smoothing);
    rendered_harmonics = 0.5f * (first + harmonics_lp_);
  } else {
    ONE_POLE(harmonics_lp_, parameters.harmonics, harmonics_smoothing);
    rendered_harmonics = harmonics_lp_;
  }
#else
  if (size == kMaxBlockSize) {
    /* Match the reference 12-frame smoothing time constant while holding
     * this call's control value across the 24-frame source render. */
    ONE_POLE(harmonics_lp_, parameters.harmonics, harmonics_smoothing);
  }
  ONE_POLE(harmonics_lp_, parameters.harmonics, harmonics_smoothing);
  rendered_harmonics = harmonics_lp_;
#endif
#else
  ONE_POLE(harmonics_lp_, parameters.harmonics, harmonics_smoothing);
  const float rendered_harmonics = harmonics_lp_;
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
  const float f0 = NoteToFrequency(parameters.note);
  if (profile) {
    br_audio_model_substage_end(
        profile, BR_AUDIO_MODEL_SUBSTAGE_ID_PREP, 0u);
  }
#endif
  
  voice_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      parameters.accent,
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
      f0,
#else
      NoteToFrequency(parameters.note),
#endif
      rendered_harmonics,
      parameters.timbre,
      parameters.morph,
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
      profile,
#endif
      temp_buffer_,
      out,
      aux,
      size);
}

}  // namespace plaits
