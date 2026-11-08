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
// Main synthesis voice.

#include "plaits/dsp/voice.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

PLAITS_COLD_INIT_ATTR void Voice::Init(BufferAllocator* allocator) {
  allocator_ = allocator;
  active_engine_ = NULL;
  static const float out_gain[kMaxEngines] = {
    1.0f, 0.7f, 1.0f, 1.0f, 1.0f, 0.7f, 0.8f, 0.5f,
    0.8f, 0.7f, 0.6f, 0.7f, 0.8f, 0.6f, 0.8f, -0.7f,
    -3.0f, -1.0f, -2.0f, -1.0f, -1.0f, 0.8f, 0.8f, 0.8f
  };
  static const float aux_gain[kMaxEngines] = {
    1.0f, 0.7f, 1.0f, 1.0f, 1.0f, 0.7f, 0.8f, 0.5f,
    0.8f, 0.6f, 0.6f, 0.6f, 0.8f, 0.6f, 0.8f, 0.8f,
    1.0f, -1.0f, 1.0f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f
  };
  static const bool already_enveloped[kMaxEngines] = {
    false, false, true, true, true, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, true, true, true, true, true
  };
  for (int i = 0; i < kMaxEngines; ++i) {
    model_post_processing_[i].out_gain = out_gain[i];
    model_post_processing_[i].aux_gain = aux_gain[i];
    model_post_processing_[i].already_enveloped = already_enveloped[i];
  }

  engine_quantizer_.Init(kMaxEngines, 0.05f, true);
  previous_engine_index_ = -1;
  reload_user_data_ = false;
  engine_cv_ = 0.0f;
  
  out_post_processor_.Init();
  aux_post_processor_.Init();

  decay_envelope_.Init();
  lpg_envelope_.Init();
  
  trigger_state_ = false;
  previous_note_ = 0.0f;
  
  trigger_delay_.Init(trigger_delay_line_);
}

PLAITS_COLD_INIT_ATTR void Voice::DestroyActiveEngine() {
  switch (previous_engine_index_) {
    case 0: engine_storage_.virtual_analog_vcf.~VirtualAnalogVCFEngine(); break;
    case 1: engine_storage_.phase_distortion.~PhaseDistortionEngine(); break;
    case 2: case 3: case 4: engine_storage_.six_op.~SixOpEngine(); break;
    case 5: engine_storage_.wave_terrain.~WaveTerrainEngine(); break;
    case 6: engine_storage_.string_machine.~StringMachineEngine(); break;
    case 7: engine_storage_.chiptune.~ChiptuneEngine(); break;
    case 8: engine_storage_.virtual_analog.~VirtualAnalogEngine(); break;
    case 9: engine_storage_.waveshaping.~WaveshapingEngine(); break;
    case 10: engine_storage_.fm.~FMEngine(); break;
    case 11: engine_storage_.grain.~GrainEngine(); break;
    case 12: engine_storage_.additive.~AdditiveEngine(); break;
    case 13: engine_storage_.wavetable.~WavetableEngine(); break;
    case 14: engine_storage_.chord.~ChordEngine(); break;
    case 15: engine_storage_.speech.~SpeechEngine(); break;
    case 16: engine_storage_.swarm.~SwarmEngine(); break;
    case 17: engine_storage_.noise.~NoiseEngine(); break;
    case 18: engine_storage_.particle.~ParticleEngine(); break;
    case 19: engine_storage_.string.~StringEngine(); break;
    case 20: engine_storage_.modal.~ModalEngine(); break;
    case 21: engine_storage_.bass_drum.~BassDrumEngine(); break;
    case 22: engine_storage_.snare_drum.~SnareDrumEngine(); break;
    case 23: engine_storage_.hi_hat.~HiHatEngine(); break;
    default: break;
  }
  active_engine_ = NULL;
}

PLAITS_COLD_INIT_ATTR Engine* Voice::ActivateEngine(int index) {
  DestroyActiveEngine();
  allocator_->Free();
  switch (index) {
    case 0: active_engine_ = ::new (&engine_storage_.virtual_analog_vcf) VirtualAnalogVCFEngine(); break;
    case 1: active_engine_ = ::new (&engine_storage_.phase_distortion) PhaseDistortionEngine(); break;
    case 2: case 3: case 4:
      active_engine_ = ::new (&engine_storage_.six_op) SixOpEngine(); break;
    case 5: active_engine_ = ::new (&engine_storage_.wave_terrain) WaveTerrainEngine(); break;
    case 6: active_engine_ = ::new (&engine_storage_.string_machine) StringMachineEngine(); break;
    case 7: active_engine_ = ::new (&engine_storage_.chiptune) ChiptuneEngine(); break;
    case 8: active_engine_ = ::new (&engine_storage_.virtual_analog) VirtualAnalogEngine(); break;
    case 9: active_engine_ = ::new (&engine_storage_.waveshaping) WaveshapingEngine(); break;
    case 10: active_engine_ = ::new (&engine_storage_.fm) FMEngine(); break;
    case 11: active_engine_ = ::new (&engine_storage_.grain) GrainEngine(); break;
    case 12: active_engine_ = ::new (&engine_storage_.additive) AdditiveEngine(); break;
    case 13: active_engine_ = ::new (&engine_storage_.wavetable) WavetableEngine(); break;
    case 14: active_engine_ = ::new (&engine_storage_.chord) ChordEngine(); break;
    case 15: active_engine_ = ::new (&engine_storage_.speech) SpeechEngine(); break;
    case 16: active_engine_ = ::new (&engine_storage_.swarm) SwarmEngine(); break;
    case 17: active_engine_ = ::new (&engine_storage_.noise) NoiseEngine(); break;
    case 18: active_engine_ = ::new (&engine_storage_.particle) ParticleEngine(); break;
    case 19: active_engine_ = ::new (&engine_storage_.string) StringEngine(); break;
    case 20: active_engine_ = ::new (&engine_storage_.modal) ModalEngine(); break;
    case 21: active_engine_ = ::new (&engine_storage_.bass_drum) BassDrumEngine(); break;
    case 22: active_engine_ = ::new (&engine_storage_.snare_drum) SnareDrumEngine(); break;
    default: active_engine_ = ::new (&engine_storage_.hi_hat) HiHatEngine(); break;
  }
  active_engine_->Init(allocator_);
  const uint8_t* data = NULL;
  if (index >= 2 && index <= 4) data = fm_patches_table[index - 2];
  active_engine_->LoadUserData(data);
  active_engine_->Reset();
  active_engine_->post_processing_settings = model_post_processing_[index];
  return active_engine_;
}

void Voice::PrepareEngine(int index) {
  if (index < 0 || index >= kMaxEngines) return;
  const int engine_index = engine_quantizer_.Process(index, engine_cv_);
  if (engine_index != previous_engine_index_ || reload_user_data_) {
    ActivateEngine(engine_index);
    out_post_processor_.Reset();
    previous_engine_index_ = engine_index;
    reload_user_data_ = false;
  }
}

void Voice::Render(
    const Patch& patch,
    const Modulations& modulations,
    Frame* frames,
    size_t size) {
  // Trigger, LPG, internal envelope.

#if PLAITS_USE_RENDER_CHUNK24
  /* The control envelopes update once per Voice::Render call. In 24-frame
   * mode, preserve their time constants by scaling to the actual call size.
   * Unselected models still render 12 frames and retain the reference step. */
  const float control_step_frames = static_cast<float>(size);
#else
  /* Preserve the reference 12-frame control cadence for all existing callers
   * when the larger source render experiment is disabled. */
  const float control_step_frames = static_cast<float>(kBlockSize);
#endif

  // Delay trigger by 1ms to deal with sequencers or MIDI interfaces whose
  // CV out lags behind the GATE out.
  trigger_delay_.Write(modulations.trigger);
  float trigger_value;
#if PLAITS_USE_RENDER_CHUNK24
  if (size == kBlockSize) {
    trigger_value = trigger_delay_.Read(kTriggerDelay);
  } else {
#if FM1_MODAL20_RENDER_CHUNK24
    /* The one-block correction is only for selected Modal at 24 frames. */
    const int trigger_delay_frames =
        patch.engine == 20 ? kTriggerDelay + 1 : kTriggerDelay;
#else
    const int trigger_delay_frames = kTriggerDelay;
#endif
    const float trigger_delay_blocks =
        static_cast<float>(trigger_delay_frames * kBlockSize) /
        control_step_frames;
    trigger_value = trigger_delay_.Read(trigger_delay_blocks);
  }
#else
  trigger_value = trigger_delay_.Read(kTriggerDelay);
#endif
  
  bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (trigger_value > 0.3f) {
      trigger_state_ = true;
      if (!modulations.level_patched) {
        lpg_envelope_.Trigger();
      }
      decay_envelope_.Trigger();
      engine_cv_ = modulations.engine;
    }
  } else {
    if (trigger_value < 0.1f) {
      trigger_state_ = false;
    }
  }
  if (!modulations.trigger_patched) {
    engine_cv_ = modulations.engine;
  }

  // Engine selection.
  int engine_index = engine_quantizer_.Process(
      patch.engine,
      engine_cv_);
  
  Engine* e = active_engine_;
  
  if (engine_index != previous_engine_index_ || reload_user_data_) {
    e = ActivateEngine(engine_index);

    out_post_processor_.Reset();
    previous_engine_index_ = engine_index;
    reload_user_data_ = false;
  }
  EngineParameters p;
#if FM1_PLAITS_KIT_CACHE_NOTE_FREQUENCY
  p.note_frequency = 0.0f;
  p.note_frequency_cached = false;
#endif

  bool rising_edge = trigger_state_ && !previous_trigger_state;
  float note = (modulations.note + previous_note_) * 0.5f;
  previous_note_ = modulations.note;
  const PostProcessingSettings& pp_s = e->post_processing_settings;

  if (modulations.trigger_patched) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | \
      (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }
  
  const float short_decay = (200.0f * control_step_frames) / kSampleRate *
      SemitonesToRatio(-96.0f * patch.decay);
  float decay_envelope_for_controls;
  float decay_envelope_squared_for_pitch;
#if PLAITS_USE_RENDER_CHUNK24
  const bool modal_two_step = FM1_MODAL20_RENDER_CHUNK24 &&
      size == kMaxBlockSize && engine_index == 20;
  if (modal_two_step) {
    /* Keep envelope state at the 12-frame cadence. Use midpoint control
     * values for timbre/morph and mean-square for pitch for this 24-frame
     * Modal render. */
    const float half_decay = (200.0f * kBlockSize) / kSampleRate *
        SemitonesToRatio(-96.0f * patch.decay);
    const float half_step = half_decay * 2.0f;
    decay_envelope_.Process(half_step);
    const float first_half_value = decay_envelope_.value();
    decay_envelope_.Process(half_step);
    const float second_half_value = decay_envelope_.value();
    decay_envelope_for_controls =
        0.5f * (first_half_value + second_half_value);
    decay_envelope_squared_for_pitch = 0.5f *
        (first_half_value * first_half_value +
         second_half_value * second_half_value);
  } else {
    decay_envelope_.Process(short_decay * 2.0f);
    decay_envelope_for_controls = decay_envelope_.value();
    decay_envelope_squared_for_pitch =
        decay_envelope_for_controls * decay_envelope_for_controls;
  }
#else
  decay_envelope_.Process(short_decay * 2.0f);
  decay_envelope_for_controls = decay_envelope_.value();
  decay_envelope_squared_for_pitch =
      decay_envelope_for_controls * decay_envelope_for_controls;
#endif

  float compressed_level = 1.3f * modulations.level / (0.3f + __builtin_fabsf(modulations.level));
  CONSTRAIN(compressed_level, 0.0f, 1.0f);
  p.accent = modulations.level_patched ? compressed_level : 0.8f;

  bool use_internal_envelope = modulations.trigger_patched;

  // Actual synthesis parameters.
  
  p.harmonics = patch.harmonics + modulations.harmonics;
  CONSTRAIN(p.harmonics, 0.0f, 1.0f);

  float internal_envelope_amplitude = 1.0f;
  float internal_envelope_amplitude_timbre = 1.0f;
  if (engine_index == 15) {
    internal_envelope_amplitude = 2.0f - p.harmonics * 6.0f;
    CONSTRAIN(internal_envelope_amplitude, 0.0f, 1.0f);
    static_cast<SpeechEngine*>(e)->set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ?
            0.0f : patch.frequency_modulation_amount);
    static_cast<SpeechEngine*>(e)->set_speed( 
        !modulations.trigger_patched || modulations.morph_patched ?
            0.0f : patch.morph_modulation_amount);
  } else if (engine_index == 7) {
    if (modulations.trigger_patched && !modulations.timbre_patched) {
      // Disable internal envelope on TIMBRE, and enable the envelope generator
      // built into the chiptune engine.
      internal_envelope_amplitude_timbre = 0.0f;
      static_cast<ChiptuneEngine*>(e)->set_envelope_shape(patch.timbre_modulation_amount);
    } else {
      static_cast<ChiptuneEngine*>(e)->set_envelope_shape(ChiptuneEngine::NO_ENVELOPE);
    }
  }
  
  p.note = ApplyModulations(
      patch.note + note,
      patch.frequency_modulation_amount,
      modulations.frequency_patched,
      modulations.frequency,
      use_internal_envelope,
      internal_envelope_amplitude * \
          decay_envelope_squared_for_pitch * 48.0f,
      1.0f,
      -119.0f,
      120.0f);

  p.timbre = ApplyModulations(
      patch.timbre,
      patch.timbre_modulation_amount,
      modulations.timbre_patched,
      modulations.timbre,
      use_internal_envelope,
      internal_envelope_amplitude_timbre * decay_envelope_for_controls,
      0.0f,
      0.0f,
      1.0f);

  p.morph = ApplyModulations(
      patch.morph,
      patch.morph_modulation_amount,
      modulations.morph_patched,
      modulations.morph,
      use_internal_envelope,
      internal_envelope_amplitude * decay_envelope_for_controls,
      0.0f,
      0.0f,
      1.0f);

  bool already_enveloped = pp_s.already_enveloped;
  e->Render(p, out_buffer_, aux_buffer_, size, &already_enveloped);
  
  bool lpg_bypass = already_enveloped || \
      (!modulations.level_patched && !modulations.trigger_patched);
  
  // Compute LPG parameters.
  if (!lpg_bypass) {
    const float hf = patch.lpg_colour;
    const float decay_tail = (20.0f * control_step_frames) / kSampleRate *
        SemitonesToRatio(-72.0f * patch.decay + 12.0f * hf) - short_decay;
    
    if (modulations.level_patched) {
      lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
    } else {
      const float attack = NoteToFrequency(p.note) * control_step_frames * 2.0f;
      lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
    }
  } else {
    lpg_envelope_.Init();
  }

#if FM1_PLAITS_SHARED_LPG_COEFFICIENTS
  const bool lpg_filter_prepared = !lpg_bypass;
  if (lpg_filter_prepared) {
    const bool coefficients_changed = out_post_processor_.PrepareLpgCoefficients(
        lpg_envelope_.frequency());
    /* The two filters have identical coefficient keys but independent
     * histories. Copy only when the shared frequency changes. */
    if (coefficients_changed) {
      aux_post_processor_.CopyLpgCoefficientsFrom(out_post_processor_);
    }
  }
#endif
  
  out_post_processor_.Process(
      pp_s.out_gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      out_buffer_,
      &frames->out,
      size,
      2
#if FM1_PLAITS_SHARED_LPG_COEFFICIENTS
      , lpg_filter_prepared
#endif
      );

  aux_post_processor_.Process(
      pp_s.aux_gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      aux_buffer_,
      &frames->aux,
      size,
      2
#if FM1_PLAITS_SHARED_LPG_COEFFICIENTS
      , lpg_filter_prepared
#endif
      );
}
  
}  // namespace plaits
