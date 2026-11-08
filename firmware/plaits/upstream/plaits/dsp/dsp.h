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
// Utility DSP routines.

#ifndef PLAITS_DSP_DSP_H_
#define PLAITS_DSP_DSP_H_

/* Experimental host/target aliasing hint. Production callers provide drum
 * output buffers disjoint from the engine object and from each other. */
#ifndef FM1_PLAITS_KIT_RENDER_OUT_RESTRICT
#define FM1_PLAITS_KIT_RENDER_OUT_RESTRICT 1
#endif
#if FM1_PLAITS_KIT_RENDER_OUT_RESTRICT
#define PLAITS_RENDER_OUT_RESTRICT __restrict__
#else
#define PLAITS_RENDER_OUT_RESTRICT
#endif

#include "stmlib/stmlib.h"
#include "plaits_config.h"

namespace plaits {
  
/* Every Plaits translation unit must receive the same source-rate definition.
 * Native-rate profiles render without a source-rate converter; lower-rate
 * profiles retain their explicit selectors. */
#ifndef PLAITS_INTERNAL_SAMPLE_RATE
#define PLAITS_INTERNAL_SAMPLE_RATE 32000
#endif
#ifndef FM1_PLAITS_SOURCE_RATE_32K
#define FM1_PLAITS_SOURCE_RATE_32K 1
#endif
#ifndef FM1_PLAITS_SOURCE_RATE_44100
#define FM1_PLAITS_SOURCE_RATE_44100 0
#endif
#ifndef FM1_PLAITS_SOURCE_RATE_38400
#define FM1_PLAITS_SOURCE_RATE_38400 0
#endif
#ifndef FM1_PLAITS_SOURCE_RATE_48000
#define FM1_PLAITS_SOURCE_RATE_48000 0
#endif
#ifndef FM1_NATIVE_OUTPUT_48K
#define FM1_NATIVE_OUTPUT_48K 0
#endif
#ifndef FM1_PLAITS_RENDER_CHUNK24
#define FM1_PLAITS_RENDER_CHUNK24 0
#endif
#ifndef FM1_PLAITS_RENDER_CHUNK24_MODEL_MASK
#define FM1_PLAITS_RENDER_CHUNK24_MODEL_MASK 0x00001000u
#endif
#ifndef FM1_MODAL20_RENDER_CHUNK24
#define FM1_MODAL20_RENDER_CHUNK24 1
#endif
#if FM1_PLAITS_RENDER_CHUNK24 || FM1_PLAITS_RENDER_CHUNK24_MODEL_MASK || \
    FM1_MODAL20_RENDER_CHUNK24
#define PLAITS_USE_RENDER_CHUNK24 1
#else
#define PLAITS_USE_RENDER_CHUNK24 0
#endif

#if PLAITS_INTERNAL_SAMPLE_RATE != 32000 && \
    PLAITS_INTERNAL_SAMPLE_RATE != 38400 && \
    PLAITS_INTERNAL_SAMPLE_RATE != 44100 && \
    PLAITS_INTERNAL_SAMPLE_RATE != 48000
#error "Plaits adapters currently support only 32 kHz, 38.4 kHz, 44.1 kHz, and 48 kHz source rates"
#endif
#if FM1_PLAITS_RATE_TIME_CALIBRATION && \
    PLAITS_INTERNAL_SAMPLE_RATE != 32000 && \
    PLAITS_INTERNAL_SAMPLE_RATE != 38400 && \
    PLAITS_INTERNAL_SAMPLE_RATE != 44100 && \
    PLAITS_INTERNAL_SAMPLE_RATE != 48000
#error "FM1_PLAITS_RATE_TIME_CALIBRATION requires a calibrated source rate"
#endif
#if PLAITS_INTERNAL_SAMPLE_RATE == 44100 && !FM1_PLAITS_SOURCE_RATE_44100
#error "44.1 kHz Plaits units require the explicit 44.1 kHz source selector"
#endif
#if FM1_PLAITS_SOURCE_RATE_44100 && FM1_NATIVE_OUTPUT_48K
#error "The 44.1 kHz Plaits source selector cannot be used with native 48 kHz output"
#endif

static const uint32_t kSampleRateHz = PLAITS_INTERNAL_SAMPLE_RATE;
static const float kSampleRate = static_cast<float>(kSampleRateHz);

// There is no proper PLL for I2S, only a divider on the system clock to derive
// the bit clock.
// The division ratio is set to 47 (23 EVEN, 1 ODD) by the ST libraries.
//
// Bit clock = 72000000 / 47 = 1531.91 kHz
// Frame clock = Bit clock / 32 = 47872.34 Hz
//
// That's only 4.6 cts of error, but we care!

/* The legacy correction below is specific to the original STM32 board's
 * measured 47,872.34 Hz frame clock. Explicit 44.1 kHz and FM-1 native48
 * source profiles use their nominal source rates; their physical clocks are
 * measured separately. */
#if FM1_PLAITS_SOURCE_RATE_44100 || \
    (FM1_NATIVE_OUTPUT_48K && FM1_PLAITS_SOURCE_RATE_48000)
static const float kCorrectedSampleRate = kSampleRate;
#else
static const float kCorrectedSampleRate =
    (47872.34f / 48000.0f) * kSampleRate;
#endif
const float a0 = (440.0f / 8.0f) / kCorrectedSampleRate;

const size_t kMaxBlockSize = 24;
const size_t kBlockSize = 12;

}  // namespace plaits

#endif  // PLAITS_DSP_DSP_H_
