// Copyright 2012 Emilie Gillet.
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
// Macro-oscillator.

#include "braids/macro_oscillator.h"

#include <algorithm>

#include "stmlib/utils/dsp.h"

#include "braids/parameter_interpolation.h"
#include "braids/resources.h"

namespace braids {
  
using namespace stmlib;

void MacroOscillator::Render(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  RenderFn fn = fn_table_[shape_];
  mix_phase_ = previous_osc_mix_ * 65536;
  mix_increment_ = size && osc_mix_ != previous_osc_mix_ ? (static_cast<int32_t>(osc_mix_) - previous_osc_mix_) *
      65536 / static_cast<int32_t>(size) : 0;
  (this->*fn)(sync, buffer, size);
  previous_osc_mix_ = osc_mix_;
}

int16_t MacroOscillator::MixComponents(int16_t a, int16_t b, uint16_t balance) {
  if (osc_mix_ == 64u && previous_osc_mix_ == 64u)
    return Mix(a, b, balance);
  mix_phase_ += mix_increment_;
  int32_t mix = mix_phase_ >> 16;
  if (mix <= 0) return a;
  if (mix >= 128) return b;
  // Bias the existing balance instead of replacing a TIMBRE/COLOR control.
  uint32_t adjusted = mix <= 64 ? static_cast<uint32_t>(balance) * mix >> 6 :
      balance + ((65535u - balance) * (mix - 64) >> 6);
  return Mix(a, b, static_cast<uint16_t>(adjusted));
}

void MacroOscillator::RenderCSaw(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_CSAW);
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_aux_parameter(parameter_[1]);
  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  int16_t shift = -(parameter_[1] - 32767) >> 4;
  while (size--) {
    int32_t s = *buffer + shift;
    *buffer++ = (s * 13) >> 3;
  }
}

void MacroOscillator::RenderMorph(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[1].set_pitch(pitch_);
  
  uint16_t balance;
  if (parameter_[0] <= 10922) {
    analog_oscillator_[0].set_parameter(0);
    analog_oscillator_[1].set_parameter(0);
    analog_oscillator_[0].set_shape(OSC_SHAPE_TRIANGLE);
    analog_oscillator_[1].set_shape(OSC_SHAPE_SAW);
    balance = parameter_[0] * 6;
  } else if (parameter_[0] <= 21845) {
    analog_oscillator_[0].set_parameter(0);
    analog_oscillator_[1].set_parameter(0);
    analog_oscillator_[0].set_shape(OSC_SHAPE_SQUARE);
    analog_oscillator_[1].set_shape(OSC_SHAPE_SAW);
    balance = 65535 - (parameter_[0] - 10923) * 6;
  } else {
    analog_oscillator_[0].set_parameter((parameter_[0] - 21846) * 3);
    analog_oscillator_[1].set_parameter(0);
    analog_oscillator_[0].set_shape(OSC_SHAPE_SQUARE);
    analog_oscillator_[1].set_shape(OSC_SHAPE_SINE);
    balance = 0;
  }
  
  int16_t* shape_1 = buffer;
  int16_t* shape_2 = temp_buffer_;
  analog_oscillator_[0].Render(sync, shape_1, NULL, size);
  analog_oscillator_[1].Render(sync, shape_2, NULL, size);
  
  int32_t lp_cutoff = pitch_ - (parameter_[1] >> 1) + 128 * 128;
  if (lp_cutoff < 0) {
    lp_cutoff = 0;
  } else if (lp_cutoff > 32767) {
    lp_cutoff = 32767;
  }
  int32_t f = Interpolate824(lut_svf_cutoff, lp_cutoff << 17);
  int32_t lp_state = lp_state_;
  int32_t fuzz_amount = parameter_[1] << 1;
  if (pitch_ > (80 << 7)) {
    fuzz_amount -= (pitch_ - (80 << 7)) << 4;
    if (fuzz_amount < 0) {
      fuzz_amount = 0;
    }
  }
  while (size--) {
    int16_t sample = MixComponents(*shape_1++, *shape_2++, balance);
    int32_t shifted_sample = sample;
    lp_state += (shifted_sample - lp_state) * f >> 15;
    CLIP(lp_state)
    shifted_sample = lp_state + 32768;
  
    int16_t fuzzed = Interpolate88(ws_violent_overdrive, shifted_sample);
    *buffer++ = Mix(sample, fuzzed, fuzz_amount);
  }
  lp_state_ = lp_state;
}

void MacroOscillator::RenderSawSquare(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[1].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[1].set_pitch(pitch_);

  analog_oscillator_[0].set_shape(OSC_SHAPE_VARIABLE_SAW);
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);
  
  int16_t* saw_buffer = buffer;
  int16_t* square_buffer = temp_buffer_;
  
  analog_oscillator_[0].Render(sync, saw_buffer, NULL, size);
  analog_oscillator_[1].Render(sync, square_buffer, NULL, size);
  
  BEGIN_INTERPOLATE_PARAMETER_1
  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t balance = parameter_1 << 1;
    int16_t attenuated_square = static_cast<int32_t>(
          *square_buffer++) * 148 >> 8;
    *buffer++ = MixComponents(*saw_buffer++, attenuated_square, balance);
  }
  END_INTERPOLATE_PARAMETER_1
}

#define SEMI * 128

const int16_t intervals[65] = {
  -24 SEMI, -24 SEMI, -24 SEMI + 4,
  -23 SEMI, -22 SEMI, -21 SEMI, -20 SEMI, -19 SEMI, -18 SEMI,
  -17 SEMI - 4, -17 SEMI,
  -16 SEMI, -15 SEMI, -14 SEMI, -13 SEMI,
  -12 SEMI - 4, -12 SEMI,
  -11 SEMI, -10 SEMI, -9 SEMI, -8 SEMI,
  -7 SEMI - 4, -7 SEMI,
  -6 SEMI, -5 SEMI, -4 SEMI, -3 SEMI, -2 SEMI, -1 SEMI,
  -24, -8, -4, 0, 4, 8, 24,
  1 SEMI, 2 SEMI, 3 SEMI, 4 SEMI, 5 SEMI, 6 SEMI,
  7 SEMI, 7 SEMI + 4,
  8 SEMI, 9 SEMI, 10 SEMI, 11 SEMI,
  12 SEMI, 12 SEMI + 4,
  13 SEMI, 14 SEMI, 15 SEMI, 16 SEMI,
  17 SEMI, 17 SEMI + 4,
  18 SEMI, 19 SEMI, 20 SEMI, 21 SEMI, 22 SEMI, 23 SEMI,
  24 SEMI - 4, 24 SEMI, 24 SEMI
};

#if defined(__clang__)
__attribute__((minsize))
#endif
void MacroOscillator::RenderTriple(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  AnalogOscillatorShape base_shape;
  switch (shape_) {
    case MACRO_OSC_SHAPE_TRIPLE_SAW:
      base_shape = OSC_SHAPE_SAW;
      break;
    case MACRO_OSC_SHAPE_TRIPLE_TRIANGLE:
      base_shape = OSC_SHAPE_TRIANGLE;
      break;
    case MACRO_OSC_SHAPE_TRIPLE_SQUARE:
      base_shape = OSC_SHAPE_SQUARE;
      break;
    default:
      base_shape = OSC_SHAPE_SINE;
      break;
  }
  
  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[1].set_parameter(0);
  analog_oscillator_[2].set_parameter(0);

  analog_oscillator_[0].set_pitch(pitch_);
  for (size_t i = 0; i < 2; ++i) {
    int16_t detune_1 = intervals[parameter_[i] >> 9];
    int16_t detune_2 = intervals[((parameter_[i] >> 8) + 1) >> 1];
    uint16_t xfade = parameter_[i] << 8;
    int16_t detune = detune_1 + ((detune_2 - detune_1) * xfade >> 16);
    analog_oscillator_[i + 1].set_pitch(pitch_ + detune);
  }

  analog_oscillator_[0].set_shape(base_shape);
  analog_oscillator_[1].set_shape(base_shape);
  analog_oscillator_[2].set_shape(base_shape);

  std::fill(&buffer[0], &buffer[size], 0);
  // Original weighting is 1/3 fundamental + 2/3 detuned pair. Preserve that
  // centre, with unit-normalised groups at either endpoint. Coefficients are
  // computed once per chunk, not once per oscillator sample.
  int32_t gain[2][2];
  for (size_t end = 0; end < 2; ++end) {
    int32_t mix = end ? osc_mix_ : previous_osc_mix_;
    gain[end][0] = 21 * (mix <= 64 ? 192 - 2 * mix : 128 - mix);
    gain[end][1] = 21 * (mix <= 64 ? mix : 64 + (mix - 64) / 2);
  }
  for (size_t i = 0; i < 3; ++i) {
    analog_oscillator_[i].Render(sync, temp_buffer_, NULL, size);
    int32_t coefficient = gain[1][i != 0];
    if (!mix_increment_) {
      for (size_t j = 0; j < size; ++j)
        buffer[j] += temp_buffer_[j] * coefficient >> 12;
      continue;
    }
    int32_t phase = gain[0][i != 0] * 65536;
    int32_t increment = (coefficient - gain[0][i != 0]) * 65536 /
        static_cast<int32_t>(size);
    for (size_t j = 0; j < size; ++j) {
      phase += increment;
      buffer[j] += temp_buffer_[j] * (phase >> 16) >> 12;
    }
  }
}

void MacroOscillator::RenderSub(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  AnalogOscillatorShape base_shape = shape_ == MACRO_OSC_SHAPE_SQUARE_SUB ?
      OSC_SHAPE_SQUARE : OSC_SHAPE_VARIABLE_SAW;
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_shape(base_shape);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(0);
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);
  int16_t octave = parameter_[1] < 16384 ? (24 << 7) : (12 << 7);
  analog_oscillator_[1].set_pitch(pitch_ - octave);

  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, NULL, size);
  
  BEGIN_INTERPOLATE_PARAMETER_1

  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t sub_gain = (parameter_1 < 16384
        ? (16383 - parameter_1) : (parameter_1 - 16384)) << 1;
    *buffer = MixComponents(*buffer, *temp_buffer, sub_gain);
    buffer++;
    temp_buffer++;
  }
  
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderDualSync(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  AnalogOscillatorShape base_shape = shape_ == MACRO_OSC_SHAPE_SQUARE_SYNC ?
      OSC_SHAPE_SQUARE : OSC_SHAPE_SAW;
  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[0].set_shape(base_shape);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(0);
  analog_oscillator_[1].set_shape(base_shape);
  analog_oscillator_[1].set_pitch(pitch_ + (parameter_[0] >> 2));

  analog_oscillator_[0].Render(sync, buffer, sync_buffer_, size);
  analog_oscillator_[1].Render(sync_buffer_, temp_buffer_, NULL, size);
  
  BEGIN_INTERPOLATE_PARAMETER_1

  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t balance = parameter_1 << 1;
    
    *buffer = (MixComponents(*buffer, *temp_buffer, balance) >> 2) * 3;
    buffer++;
    temp_buffer++;
  }
  
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderSineTriangle(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  int32_t attenuation_sine = 32767 - 6 * (pitch_ - (92 << 7));
  int32_t attenuation_tri = 32767 - 7 * (pitch_ - (80 << 7));
  if (attenuation_tri < 0) attenuation_tri = 0;
  if (attenuation_sine < 0) attenuation_sine = 0;
  if (attenuation_tri > 32767) attenuation_tri = 32767;
  if (attenuation_sine > 32767) attenuation_sine = 32767;
  
  int32_t timbre = parameter_[0];

  analog_oscillator_[0].set_parameter(timbre * attenuation_sine >> 15);
  analog_oscillator_[1].set_parameter(timbre * attenuation_tri >> 15);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[1].set_pitch(pitch_);
  
  analog_oscillator_[0].set_shape(OSC_SHAPE_SINE_FOLD);
  analog_oscillator_[1].set_shape(OSC_SHAPE_TRIANGLE_FOLD);

  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, NULL, size);

  int16_t* temp_buffer = temp_buffer_;
  
  BEGIN_INTERPOLATE_PARAMETER_1
  
  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t balance = parameter_1 << 1;
    
    *buffer = MixComponents(*buffer, *temp_buffer, balance);
    buffer++;
    temp_buffer++;
  }
  
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderBuzz(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_shape(OSC_SHAPE_BUZZ);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(parameter_[0]);
  analog_oscillator_[1].set_shape(OSC_SHAPE_BUZZ);
  analog_oscillator_[1].set_pitch(pitch_ + (parameter_[1] >> 8));

  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, NULL, size);
  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    if (osc_mix_ == 64u && previous_osc_mix_ == 64u) {
      *buffer = (*buffer >> 1) + (*temp_buffer >> 1);
    } else {
      *buffer = MixComponents(*buffer, *temp_buffer, 32768u);
    }
    buffer++;
    temp_buffer++;
  }
}

void MacroOscillator::RenderDigital(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  digital_oscillator_.set_parameters(parameter_[0], parameter_[1]);
  digital_oscillator_.set_pitch(pitch_);
  digital_oscillator_.set_shape(static_cast<DigitalOscillatorShape>(
      shape_ - MACRO_OSC_SHAPE_TRIPLE_RING_MOD));
  digital_oscillator_.Render(sync, buffer, size);
}

void MacroOscillator::RenderSawComb(
  const uint8_t* sync,
  int16_t* buffer,
  size_t size) {
  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_SAW);
  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  
  digital_oscillator_.set_parameters(parameter_[0], parameter_[1]);
  digital_oscillator_.set_pitch(pitch_);
  digital_oscillator_.set_shape(OSC_SHAPE_COMB_FILTER);
  digital_oscillator_.Render(sync, buffer, size);
}

/* static */
MacroOscillator::RenderFn MacroOscillator::fn_table_[] = {
  &MacroOscillator::RenderCSaw,
  &MacroOscillator::RenderMorph,
  &MacroOscillator::RenderSawSquare,
  &MacroOscillator::RenderSineTriangle,
  &MacroOscillator::RenderBuzz,
  &MacroOscillator::RenderSub,
  &MacroOscillator::RenderSub,
  &MacroOscillator::RenderDualSync,
  &MacroOscillator::RenderDualSync,
  &MacroOscillator::RenderTriple,
  &MacroOscillator::RenderTriple,
  &MacroOscillator::RenderTriple,
  &MacroOscillator::RenderTriple,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderSawComb,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  // &MacroOscillator::RenderDigital
};

}  // namespace braids
