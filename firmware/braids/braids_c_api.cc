/* SPDX-License-Identifier: MIT
 * Mutable Instruments BRAIDS at the Felucca C boundary. */
#include "braids_c_api.h"
#include "../hal/fm1_audio_rate.h"

#include <cstring>

#include "braids/macro_oscillator.h"

namespace {

struct State {
  uint32_t magic;
  uint32_t seed;
  braids::MacroOscillator oscillator;
  uint8_t model;
  uint8_t reserved[3];
  uint16_t phase_num;
  uint16_t raw_count;
#if FM1_NATIVE_OUTPUT_32K
  int16_t raw[104]; /* 3 raw 96 kHz samples/output + one lookahead, in 24-frame appends */
#else
  int16_t raw[72];
#endif
  int16_t history[8];
};

static const uint32_t kStateMagic = 0x42524453u;

static_assert(sizeof(State) <= BRAIDS_STATE_BYTES,
              "BRAIDS state pool too small for the unmodified delay lines");

static braids::MacroOscillator *osc(void *state) {
  return &static_cast<State *>(state)->oscillator;
}

static int16_t clamp_pitch(int32_t pitch_q7) {
  if (pitch_q7 < 0) return 0;
  if (pitch_q7 > 16383) return 16383;
  return static_cast<int16_t>(pitch_q7);
}

static int16_t clamp_parameter(uint16_t value) {
  return value > 32767u ? 32767 : static_cast<int16_t>(value);
}

static uint8_t clamp_model(uint32_t model) {
  return static_cast<uint8_t>(model < BRAIDS_MODEL_COUNT ? model : 0u);
}

/* Share the cold model-reset body instead of duplicating it at both entry
 * points; the common unchanged-model check stays at the call sites. */
__attribute__((minsize, noinline)) static void reset_model(State *s, uint8_t model) {
  if (s->model == model) return;
  memset(static_cast<void *>(&s->oscillator), 0, sizeof(s->oscillator));
  s->oscillator.Init();
  s->oscillator.set_shape(braids::MACRO_OSC_SHAPE_CSAW);
  s->oscillator.SeedRandom(s->seed);
  s->model = model;
  s->phase_num = 0;
  s->raw_count = 0;
  memset(s->history, 0, sizeof(s->history));
}

}  // namespace

extern "C" __attribute__((minsize, noinline)) void braids_state_init(void *state, uint32_t seed) {
  memset(state, 0, sizeof(State));
  State *s = static_cast<State *>(state);
  s->seed = seed;
  osc(state)->Init();
  osc(state)->SeedRandom(seed);
  osc(state)->set_shape(braids::MACRO_OSC_SHAPE_CSAW);
  s->model = 0;
  s->magic = kStateMagic;
}

extern "C" void braids_state_ensure(void *state, uint32_t seed) {
  if (static_cast<State *>(state)->magic != kStateMagic)
    braids_state_init(state, seed);
}

extern "C" void braids_note_on(void *state, uint32_t model,
                               int32_t pitch_q7, uint16_t timbre_q15,
                               uint16_t color_q15) {
  State *s = static_cast<State *>(state);
  uint8_t shape = clamp_model(model);
  if (s->model != shape) reset_model(s, shape);
  osc(state)->set_shape(static_cast<braids::MacroOscillatorShape>(shape));
  osc(state)->set_pitch(clamp_pitch(pitch_q7));
  osc(state)->set_parameters(clamp_parameter(timbre_q15),
                             clamp_parameter(color_q15));
  osc(state)->Strike();
}

extern "C" void braids_render(void *state, uint32_t model,
                              int32_t pitch_q7, uint16_t timbre_q15,
                              uint16_t color_q15, int16_t *out, uint32_t n) {
  braids_render_mix(state, model, pitch_q7, timbre_q15, color_q15, out, n, 64u);
}

extern "C" uint32_t braids_has_osc_mix(uint32_t model) {
  return model >= 1u && model <= 12u;
}

extern "C" void braids_render_mix(void *state, uint32_t model,
                              int32_t pitch_q7, uint16_t timbre_q15,
                              uint16_t color_q15, int16_t *out, uint32_t n,
                              uint32_t mix) {
#if FM1_NATIVE_OUTPUT_32K
  static const uint32_t kRatioNumerator = 3u;  /* Braids raw oscillator runs at 96 kHz. */
  static const uint32_t kRatioDenominator = 1u;
#else
  static const uint32_t kRatioNumerator = 320u;
  static const uint32_t kRatioDenominator = 147u;
#endif
  static const int16_t kResampler[9] = {0, -137, 364, 5287, 21740,
                                         5287, 364, -137, 0};
  uint8_t sync[24] = {0};
  int16_t chunk[24];
  State *s = static_cast<State *>(state);
  braids::MacroOscillator *o = &s->oscillator;
  uint8_t shape = clamp_model(model);
  if (s->model != shape) reset_model(s, shape);
  o->set_osc_mix(braids_has_osc_mix(model) ? mix : 64u);
  o->set_shape(static_cast<braids::MacroOscillatorShape>(shape));
  uint32_t end_num = s->phase_num + n * kRatioNumerator;
  uint32_t consumed = end_num / kRatioDenominator;
  uint32_t last_num = s->phase_num + (n ? n - 1u : 0u) * kRatioNumerator;
  uint32_t last_base = last_num / kRatioDenominator;
  uint32_t needed = last_base + 2u;
  if (needed < consumed + 1u) needed = consumed + 1u;
  o->set_pitch(clamp_pitch(pitch_q7));
  o->set_parameters(clamp_parameter(timbre_q15), clamp_parameter(color_q15));
  while (s->raw_count < needed) {
    uint32_t count = needed - s->raw_count;
    /* Several original models produce two output samples per inner step. */
    if (count & 1u) ++count;
    if (count > 24u) count = 24u;
    o->Render(sync, chunk, count);
    for (uint32_t i = 0; i < count; ++i) {
      int32_t filtered = 0;
      for (uint32_t tap = 0; tap < 9u; ++tap) {
        int32_t x = tap == 0u ? chunk[i] : s->history[tap - 1u];
        filtered += x * kResampler[tap];
      }
      for (uint32_t h = 7; h > 0; --h)
        s->history[h] = s->history[h - 1u];
      s->history[0] = chunk[i];
      filtered = filtered >= 0 ? filtered / 32768
                               : -((-filtered + 32767) / 32768);
      if (filtered > 32767) filtered = 32767;
      if (filtered < -32768) filtered = -32768;
      s->raw[s->raw_count + i] = static_cast<int16_t>(filtered);
    }
    s->raw_count += static_cast<uint16_t>(count);
  }
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t position_num = s->phase_num + i * kRatioNumerator;
    uint32_t index = position_num / kRatioDenominator;
#if FM1_NATIVE_OUTPUT_32K
    out[i] = s->raw[index];
#else
    uint32_t fraction = position_num % kRatioDenominator;
    int32_t a = s->raw[index];
    int32_t b = s->raw[index + 1u];
    out[i] = static_cast<int16_t>((a * (int32_t)(kRatioDenominator - fraction) +
                                  b * (int32_t)fraction) /
                                 (int32_t)kRatioDenominator);
#endif
  }
  s->phase_num = static_cast<uint16_t>(end_num % kRatioDenominator);
  if (consumed) {
    uint32_t remaining = s->raw_count - consumed;
    for (uint32_t i = 0; i < remaining; ++i)
      s->raw[i] = s->raw[i + consumed];
    s->raw_count = static_cast<uint16_t>(remaining);
  }
}
