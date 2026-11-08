/* SPDX-License-Identifier: MIT
 * Compact adapter around the original BRAIDS percussion DSP equations. */
#include "braids_perc_c_api.h"
#include "../hal/fm1_audio_rate.h"
#include "braids_interp_math.h"
#include <stdint.h>
#include <string.h>
#include "braids/excitation.h"
#include "braids/parameter_interpolation.h"
#include "braids/resources.h"
#include "braids/svf.h"

namespace {
using namespace braids;
using namespace stmlib;


static const uint32_t kMagic = 0x42525043u;
#ifndef FM1_BRAIDS_PERC_KICK_RESONANCE_CACHE
#define FM1_BRAIDS_PERC_KICK_RESONANCE_CACHE 1
#endif
#ifndef FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
#define FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST 1
#endif
#if FM1_NATIVE_OUTPUT_32K
static const uint32_t kRatioNumerator = 3u; /* 96 kHz model frames / 32 kHz output */
static const uint32_t kRatioDenominator = 1u;
static const uint32_t kChokeFrames = (FM1_AUDIO_RATE_HZ * 3u) / 2000u;
static const uint32_t kQuietFrames = (4096u * FM1_AUDIO_RATE_HZ + 22050u) / 44100u;
#elif FM1_NATIVE_OUTPUT_48K
static const uint32_t kRatioNumerator = 2u; /* 96 kHz model frames / 48 kHz output */
static const uint32_t kRatioDenominator = 1u;
static const uint32_t kChokeFrames = (FM1_AUDIO_RATE_HZ * 3u) / 2000u;
static const uint32_t kQuietFrames = (4096u * FM1_AUDIO_RATE_HZ + 22050u) / 44100u;
#else
static const uint32_t kRatioNumerator = 320u;
static const uint32_t kRatioDenominator = 147u;
static const uint32_t kChokeFrames = 66u;
static const uint32_t kQuietFrames = 4096u;
#endif
static const uint32_t kPitchTableStart = 128u * 128u;
static const uint32_t kOctave = 12u * 128u;
static const int16_t kBellPartials[11] = {
    -1284, -1283, -184, -183, 385, 1175, 1536, 2233, 2434, 2934, 3110};
static const int16_t kBellPartialAmplitudes[11] = {
    8192, 5488, 8192, 14745, 21872, 13680, 11960, 10895, 10895, 6144, 10895};
static const uint16_t kBellDecayLong[11] = {
    65533,65533,65533,65532,65531,65531,65530,65529,65527,65523,65519};
static const uint16_t kBellDecayShort[11] = {
    65308,65283,65186,65123,64839,64889,64632,64409,64038,63302,62575};
static const int16_t kDrumPartials[6] = {0, 0, 1041, 1747, 1846, 3072};
static const int16_t kDrumPartialAmplitude[6] = {16986,2654,3981,5308,3981,2985};
static const uint16_t kDrumDecayLong[6] = {65533,65531,65531,65531,65531,65516};
static const uint16_t kDrumDecayShort[6] = {65083,64715,64715,64715,64715,62312};

struct AddState {
  uint32_t phase[11];
  uint32_t increment[11];
  int32_t amplitude[11];
  int32_t target[11];
  int16_t previous;
  uint16_t decay_raw_remainder;
  uint32_t current_partial;
  int32_t lp_noise[3];
};

struct PercState {
  uint32_t magic;
  uint32_t seed;
  uint32_t rng;
  uint32_t phase;
  uint32_t source_age;
  int16_t pitch;
  int16_t parameter[2];
  uint16_t velocity;
  uint16_t phase_num;
  uint16_t raw_count;
  uint16_t choke_left;
  uint16_t quiet_samples;
  uint8_t model;
  uint8_t active;
  uint8_t strike;
  uint8_t initialized;
  uint8_t choked;
  uint8_t pad[3];
  uint32_t hat_phase[6];
  uint32_t hat_rng;
  Excitation pulse[4];
  Svf svf[3];
  int32_t kick_lp;
  AddState add;
#if FM1_NATIVE_OUTPUT_32K
  int16_t raw[104];
#else
  int16_t raw[80];
#endif
  int16_t history[8];
};

static_assert(sizeof(PercState) <= BRAIDS_PERC_STATE_BYTES,
              "compact BRAIDS percussion lane state exceeds 1 KiB");

static uint32_t random_word(PercState *s) {
  s->rng = s->rng * 1664525u + 1013904223u;
  return s->rng;
}
static int16_t random_sample(PercState *s) {
  return static_cast<int16_t>(random_word(s) >> 16);
}
static uint32_t phase_increment(int16_t pitch) {
  if (pitch >= static_cast<int32_t>(kPitchTableStart))
    pitch = static_cast<int16_t>(kPitchTableStart - 1u);
  int32_t ref = static_cast<int32_t>(pitch) - static_cast<int32_t>(kPitchTableStart);
  uint32_t shifts = 0;
  while (ref < 0) { ref += static_cast<int32_t>(kOctave); ++shifts; }
  uint32_t a = lut_oscillator_increments[ref >> 4];
  uint32_t b = lut_oscillator_increments[(ref >> 4) + 1];
  return (a + (static_cast<int32_t>(b - a) * (ref & 15) >> 4)) >> shifts;
}
static int32_t clip16(int32_t x) {
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return x;
}
static int32_t scale_velocity(int32_t sample, uint16_t velocity) {
  /* For |product| <= 32768 * 32767, division by 32767 is exactly
   * (magnitude + (magnitude >> 15) + 1) >> 15. This avoids a per-sample
   * signed divide while preserving truncation toward zero. */
  int32_t product = sample * static_cast<int32_t>(velocity);
  uint32_t magnitude = product < 0 ? static_cast<uint32_t>(-product) :
                                    static_cast<uint32_t>(product);
  uint32_t quotient = (magnitude + (magnitude >> 15) + 1u) >> 15;
  return product < 0 ? -static_cast<int32_t>(quotient) :
                       static_cast<int32_t>(quotient);
}
static bool consume_decay_interval(PercState *s, uint32_t raw_frames) {
  if (s->parameter[0] >= 32000) {
#if FM1_NATIVE_OUTPUT_48K
    s->add.decay_raw_remainder = 0u;
#endif
    return false;
  }
#if FM1_NATIVE_OUTPUT_48K
  /* The original additive envelopes update once per 24-frame BRAIDS call.
   * Native48's 2:1 adapter often emits shorter tail chunks (24+24+16 for a
   * 64-frame request). Do not apply a full decay step to those partial calls:
   * accumulate their elapsed model frames and update once per 24 instead. */
  uint32_t elapsed = s->add.decay_raw_remainder + raw_frames;
  if (elapsed < 24u) {
    s->add.decay_raw_remainder = static_cast<uint16_t>(elapsed);
    return false;
  }
  s->add.decay_raw_remainder = static_cast<uint16_t>(elapsed - 24u);
  return true;
#else
  (void)s;
  (void)raw_frames;
  return true;
#endif
}
static void init_model(PercState *s) {
  uint32_t i;
  for (i = 0; i < 4; ++i) s->pulse[i].Init();
  for (i = 0; i < 3; ++i) s->svf[i].Init();
  s->phase = 0;
  s->source_age = 0;
  s->kick_lp = 0;
  memset(&s->add, 0, sizeof(s->add));
  memset(s->hat_phase, 0, sizeof(s->hat_phase));
  /* Original HatState starts at zero independently of the oscillator RNG. */
  s->hat_rng = 0;
  s->rng = s->seed;
  s->initialized = 1;
  if (s->model == BRAIDS_PERC_KICK) {
    s->pulse[0].set_delay(0); s->pulse[0].set_decay(3340);
    s->pulse[1].set_delay(48); s->pulse[1].set_decay(3072);
    s->pulse[2].set_delay(192); s->pulse[2].set_decay(4093);
    s->svf[0].set_punch(32768); s->svf[0].set_mode(SVF_MODE_BP);
  } else if (s->model == BRAIDS_PERC_SNARE || s->model == BRAIDS_PERC_CLAP) {
    s->pulse[0].set_delay(0); s->pulse[0].set_decay(1536);
    s->pulse[1].set_delay(48); s->pulse[1].set_decay(3072);
    s->pulse[2].set_delay(48); s->pulse[2].set_decay(1200);
    s->pulse[3].set_delay(0);
    s->svf[2].set_resonance(2000); s->svf[2].set_mode(SVF_MODE_BP);
  } else if (s->model == BRAIDS_PERC_HAT) {
    s->svf[0].set_mode(SVF_MODE_BP); s->svf[0].set_resonance(12000);
    s->svf[1].set_mode(SVF_MODE_HP); s->svf[1].set_resonance(2000);
  }
  s->strike = 1;
}
static void trigger_snare(PercState *s) {
  int32_t decay = 49152 - s->pitch;
  decay += s->parameter[1] < 16384 ? 0 : s->parameter[1] - 16384;
  if (decay > 65535) decay = 65535;
  s->svf[0].set_resonance(29000 + (decay >> 5));
  s->svf[1].set_resonance(26500 + (decay >> 5));
  s->pulse[3].set_decay(4092 + (decay >> 14));
  s->pulse[0].Trigger(15 * 32768);
  s->pulse[1].Trigger(-32768);
  s->pulse[2].Trigger(13107);
  int32_t snappy = s->parameter[1];
  if (snappy >= 14336) snappy = 14336;
  s->pulse[3].Trigger(512 + (snappy << 1));
}

static void render_kick(PercState *s, int16_t *buffer, uint32_t size
#if FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
                        , int16_t kick_resonance,
                        int32_t kick_lp_coefficient
#endif
                        ) {
  if (s->strike) {
    s->pulse[0].Trigger(12 * 32768 * 7 / 10);
    s->pulse[1].Trigger(-19662 * 7 / 10);
    s->pulse[2].Trigger(18000);
    s->svf[0].set_punch(24000);
    s->strike = 0;
  }
#if FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
  const int16_t resonance = kick_resonance;
  const int32_t lp_coefficient = kick_lp_coefficient;
#if FM1_BRAIDS_PERC_KICK_RESONANCE_CACHE
  s->svf[0].set_resonance_if_changed(resonance);
#else
  s->svf[0].set_resonance(resonance);
#endif
#else
  uint32_t decay = static_cast<uint16_t>(s->parameter[0]);
  uint32_t scaled = 65535u - (decay << 1);
  uint32_t squared = scaled * scaled >> 16;
  scaled = squared * scaled >> 18;
#if FM1_BRAIDS_PERC_KICK_RESONANCE_CACHE
  s->svf[0].set_resonance_if_changed(
      static_cast<int16_t>(32768 - 128 - scaled));
#else
  s->svf[0].set_resonance(32768 - 128 - scaled);
#endif
  uint32_t coefficient = static_cast<uint16_t>(s->parameter[1]);
  coefficient = coefficient * coefficient >> 15;
  coefficient = coefficient * coefficient >> 15;
  int32_t lp_coefficient = 128 + (coefficient >> 1) * 3;
#endif
  int32_t lp = s->kick_lp;
  while (size) {
    int32_t excitation = s->pulse[0].Process();
    excitation += !s->pulse[1].done() ? 16384 : 0;
    excitation += s->pulse[1].Process();
    s->pulse[2].Process();
    s->svf[0].set_frequency(s->pitch + (s->pulse[2].done() ? 0 : (17 << 7)));
    for (int j = 0; j < 2; ++j) {
      int32_t resonator = (excitation >> 4) + s->svf[0].Process(excitation);
      lp += (resonator - lp) * lp_coefficient >> 15;
      lp = clip16(lp);
      *buffer++ = static_cast<int16_t>(lp);
    }
    size -= 2;
  }
  s->kick_lp = lp;
}
static void render_snare(PercState *s, int16_t *buffer, uint32_t size) {
  if (s->strike) { trigger_snare(s); s->strike = 0; }
  s->svf[0].set_frequency(s->pitch + (12 << 7));
  s->svf[1].set_frequency(s->pitch + (24 << 7));
  s->svf[2].set_frequency(s->pitch + (60 << 7));
  int32_t g1 = 22000 - (s->parameter[0] >> 1);
  int32_t g2 = 22000 + (s->parameter[0] >> 1);
  while (size) {
    if (s->model == BRAIDS_PERC_CLAP && s->source_age != 0 &&
        (s->source_age == 960u || s->source_age == 1920u || s->source_age == 2880u))
      trigger_snare(s);
    int32_t ex1 = s->pulse[0].Process() + s->pulse[1].Process();
    ex1 += !s->pulse[1].done() ? 2621 : 0;
    int32_t ex2 = s->pulse[2].Process();
    ex2 += !s->pulse[2].done() ? 13107 : 0;
    int32_t noise = random_sample(s) * s->pulse[3].Process() >> 15;
    int32_t sample = (s->svf[0].Process(ex1) + (ex1 >> 4)) * g1 >> 15;
    sample += (s->svf[1].Process(ex2) + (ex2 >> 4)) * g2 >> 15;
    sample += s->svf[2].Process(noise);
    sample = clip16(sample);
    *buffer++ = static_cast<int16_t>(sample);
    *buffer++ = static_cast<int16_t>(sample);
    s->source_age += 2;
    size -= 2;
  }
}
static void render_hat(PercState *s, int16_t *buffer, uint32_t size) {
  uint32_t inc[7];
  int32_t note = (40 << 7) + (s->pitch >> 1);
  inc[0] = phase_increment(static_cast<int16_t>(note));
  uint32_t root = inc[0] >> 10;
  inc[1] = root * 24273u >> 4;
  inc[2] = root * 12561u >> 4;
  inc[3] = root * 18417u >> 4;
  inc[4] = root * 22452u >> 4;
  inc[5] = root * 31858u >> 4;
  inc[6] = inc[0] * 24u;
  int32_t xfade = s->parameter[1];
  s->svf[0].set_frequency(s->parameter[0] >> 1);
  s->svf[1].set_frequency(s->parameter[0] >> 1);
  while (size--) {
    s->phase += inc[6];
    if (s->phase < inc[6]) s->hat_rng = s->hat_rng * 1664525u + 1013904223u;
    int32_t noise_hat = 0;
    for (uint32_t i = 0; i < 6; ++i) {
      s->hat_phase[i] += inc[i];
      noise_hat += s->hat_phase[i] >> 31;
    }
    noise_hat = (noise_hat - 3) * 5461;
    noise_hat = clip16(s->svf[0].Process(noise_hat));
    int32_t noise = static_cast<int32_t>(s->hat_rng >> 16) - 32768;
    noise = clip16(s->svf[1].Process(noise >> 1));
    int32_t sample = noise_hat + ((noise - noise_hat) * xfade >> 15);
    *buffer++ = static_cast<int16_t>(clip16(sample));
  }
}
static void render_bell(PercState *s, int16_t *buffer, uint32_t size) {
  uint32_t first = s->add.current_partial;
  uint32_t last = first + 3u;
  if (last > 11u) last = 11u;
  s->add.current_partial = (first + 3u) % 11u;
  if (s->strike) {
    for (uint32_t i = 0; i < 11u; ++i) {
      s->add.amplitude[i] = kBellPartialAmplitudes[i];
      s->add.phase[i] = 1u << 30;
    }
    s->strike = 0;
    first = 0; last = 11;
  }
  for (uint32_t i = first; i < last; ++i) {
    int16_t p = static_cast<int16_t>(s->pitch + kBellPartials[i] +
        ((i & 1u) ? (s->parameter[1] >> 7) : -(s->parameter[1] >> 7)));
    s->add.increment[i] = phase_increment(p) << 1;
  }
  if (consume_decay_interval(s, size)) {
    for (uint32_t i = 0; i < 11u; ++i) {
      int32_t balance = (32767 - s->parameter[0]) >> 8;
      balance = balance * balance >> 7;
      int32_t d = kBellDecayLong[i] - ((kBellDecayLong[i] - kBellDecayShort[i]) * balance >> 7);
      s->add.amplitude[i] = s->add.amplitude[i] * d >> 16;
    }
  }
  int16_t previous = s->add.previous;
  while (size) {
    int32_t out = 0;
    for (uint32_t i = 0; i < 11u; ++i) {
      s->add.phase[i] += s->add.increment[i];
      out += Interpolate824(wav_sine, s->add.phase[i]) * s->add.amplitude[i] >> 17;
    }
    out = clip16(out);
    *buffer++ = static_cast<int16_t>((out + previous) >> 1);
    *buffer++ = static_cast<int16_t>(out);
    previous = static_cast<int16_t>(out);
    size -= 2;
  }
  s->add.previous = previous;
}
static void render_drum(PercState *s, int16_t *buffer, uint32_t size) {
  if (s->strike) {
    int reset = s->add.amplitude[0] < 1024;
    for (uint32_t i = 0; i < 6u; ++i) {
      s->add.target[i] = kDrumPartialAmplitude[i];
      if (reset) s->add.phase[i] = 1u << 30;
    }
    s->strike = 0;
  } else if (consume_decay_interval(s, size)) {
    for (uint32_t i = 0; i < 6u; ++i) {
      int32_t balance = (32767 - s->parameter[0]) >> 8;
      balance = balance * balance >> 7;
      int32_t d = kDrumDecayLong[i] - ((kDrumDecayLong[i] - kDrumDecayShort[i]) * balance >> 7);
      s->add.target[i] = s->add.amplitude[i] * d >> 16;
    }
  }
  for (uint32_t i = 0; i < 6u; ++i)
    s->add.increment[i] = phase_increment(static_cast<int16_t>(s->pitch + kDrumPartials[i])) << 1;
  int16_t previous = s->add.previous;
  int32_t cutoff = (s->pitch - 12 * 128) + (s->parameter[1] >> 2);
  if (cutoff < 0) cutoff = 0;
  if (cutoff > 32767) cutoff = 32767;
  int32_t f = Interpolate824(lut_svf_cutoff, cutoff << 16);
  int32_t lp0 = s->add.lp_noise[0], lp1 = s->add.lp_noise[1], lp2 = s->add.lp_noise[2];
  int32_t hg = s->parameter[1] < 12888 ? (s->parameter[1] + 4096) : 16384;
  int32_t ng = s->parameter[1] < 16384 ? 0 : s->parameter[1] - 16384;
  ng = ng * 12888 >> 14;
  int32_t fade_increment = 65536 / static_cast<int32_t>(size), fade = 0;
  while (size) {
    fade += fade_increment;
    int32_t noise = random_sample(s);
    if (noise > 16384) noise = 16384;
    if (noise < -16384) noise = -16384;
    lp0 += (noise - lp0) * f >> 15;
    lp1 += (lp0 - lp1) * f >> 15;
    lp2 += (lp1 - lp2) * f >> 15;
    int32_t partial[6], harmonics = 0;
    for (uint32_t i = 0; i < 6u; ++i) {
      s->add.phase[i] += s->add.increment[i];
      partial[i] = Interpolate824(wav_sine, s->add.phase[i]);
      int32_t amp = s->add.amplitude[i] +
          ((s->add.target[i] - s->add.amplitude[i]) * fade >> 15);
      partial[i] = partial[i] * amp >> 16;
      harmonics += partial[i];
    }
    int32_t sample = partial[0];
    int32_t n1 = partial[1] * lp2 >> 8;
    int32_t n2 = partial[3] * lp2 >> 9;
    sample += n1 * (12288 - ng) >> 14;
    sample += n2 * ng >> 14;
    sample += harmonics * hg >> 14;
    sample = clip16(sample);
    *buffer++ = static_cast<int16_t>((sample + previous) >> 1);
    *buffer++ = static_cast<int16_t>(sample);
    previous = static_cast<int16_t>(sample);
    size -= 2;
  }
  s->add.previous = previous;
  s->add.lp_noise[0] = lp0; s->add.lp_noise[1] = lp1; s->add.lp_noise[2] = lp2;
  for (uint32_t i = 0; i < 11u; ++i) s->add.amplitude[i] = s->add.target[i];
}
/* Keep model scratch and dispatch out of the rate-conversion caller's frame.
 * This diagnostic code-generation experiment changes no DSP equations. */
#ifndef FM1_BRAIDS_PERC_RAW_NOINLINE
#define FM1_BRAIDS_PERC_RAW_NOINLINE 1
#endif
#if FM1_BRAIDS_PERC_RAW_NOINLINE
__attribute__((noinline))
#endif
static void render_raw(PercState *s, int16_t *out, uint32_t size
#if FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
                       , int16_t kick_resonance,
                       int32_t kick_lp_coefficient
#endif
                       ) {
  if (!s->initialized) init_model(s);
  switch (s->model) {
    case BRAIDS_PERC_KICK: render_kick(s, out, size
#if FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
        , kick_resonance, kick_lp_coefficient
#endif
        ); break;
    case BRAIDS_PERC_HAT: render_hat(s, out, size); break;
    case BRAIDS_PERC_SNARE:
    case BRAIDS_PERC_CLAP: render_snare(s, out, size); break;
    case BRAIDS_PERC_BELL: render_bell(s, out, size); break;
    default: render_drum(s, out, size); break;
  }
}
} // namespace

extern "C" void braids_perc_init(void *state, uint32_t seed) {
  memset(static_cast<void *>(state), 0, sizeof(PercState));
  PercState *s = static_cast<PercState *>(state);
  s->magic = kMagic;
  s->seed = seed ? seed : 0x21u;
  s->model = BRAIDS_PERC_KICK;
  s->active = 0;
}
static inline __attribute__((always_inline)) void braids_perc_note_on_impl(
    PercState *s, uint32_t seed, uint32_t model, int32_t pitch_q7,
    uint16_t timbre_q15, uint16_t color_q15, uint16_t velocity_q15) {
  seed = seed ? seed : 0x21u;
  memset(static_cast<void *>(s), 0, sizeof(PercState));
  s->magic = kMagic;
  s->seed = seed;
  s->rng = seed;
  s->hat_rng = 0;
  s->model = model <= BRAIDS_PERC_CLAP ? static_cast<uint8_t>(model) : BRAIDS_PERC_KICK;
  s->pitch = pitch_q7 < 0 ? 0 : pitch_q7 > 16383 ? 16383 : static_cast<int16_t>(pitch_q7);
  s->parameter[0] = timbre_q15 > 32767u ? 32767 : static_cast<int16_t>(timbre_q15);
  s->parameter[1] = color_q15 > 32767u ? 32767 : static_cast<int16_t>(color_q15);
  s->velocity = velocity_q15 > 32767u ? 32767 : velocity_q15;
  s->active = 1;
  s->strike = 1;
  s->initialized = 0;
}
extern "C" void braids_perc_note_on_seed(void *state, uint32_t seed,
                                         uint32_t model, int32_t pitch_q7,
                                         uint16_t timbre_q15,
                                         uint16_t color_q15,
                                         uint16_t velocity_q15) {
  if (!state) return;
  braids_perc_note_on_impl(static_cast<PercState *>(state), seed, model,
      pitch_q7, timbre_q15, color_q15, velocity_q15);
}
extern "C" void braids_perc_note_on(void *state, uint32_t model,
                                    int32_t pitch_q7, uint16_t timbre_q15,
                                    uint16_t color_q15, uint16_t velocity_q15) {
  if (!state) return;
  PercState *s = static_cast<PercState *>(state);
  uint32_t seed = s->magic == kMagic ? s->seed : 0x21u;
  braids_perc_note_on_impl(s, seed, model, pitch_q7, timbre_q15, color_q15,
      velocity_q15);
}
extern "C" void braids_perc_choke(void *state) {
  PercState *s = static_cast<PercState *>(state);
  if (s->magic == kMagic && s->active) {
    s->choked = 1;
    s->choke_left = static_cast<uint16_t>(kChokeFrames);
  }
}
extern "C" void braids_perc_controls(void *state, uint16_t timbre_q15,
                                      uint16_t color_q15) {
  PercState *s = static_cast<PercState *>(state);
  if (s->magic != kMagic) return;
  s->parameter[0] = timbre_q15 > 32767u ? 32767 : static_cast<int16_t>(timbre_q15);
  s->parameter[1] = color_q15 > 32767u ? 32767 : static_cast<int16_t>(color_q15);
}
extern "C" uint8_t braids_perc_active(const void *state) {
  const PercState *s = static_cast<const PercState *>(state);
  return s->magic == kMagic ? s->active : 0u;
}
extern "C" uint8_t braids_perc_initialized(const void *state) {
  const PercState *s = static_cast<const PercState *>(state);
  return s->magic == kMagic ? s->initialized : 0u;
}
extern "C" void braids_perc_render(void *state, int16_t *out, uint32_t n) {
  PercState *s = static_cast<PercState *>(state);
  if (s->magic != kMagic || !out || n > BRAIDS_PERC_BLOCK_MAX) {
    if (out) for (uint32_t i = 0; i < n; ++i) out[i] = 0;
    return;
  }
  uint32_t end_num = s->phase_num + n * kRatioNumerator;
  uint32_t consumed = end_num / kRatioDenominator;
  uint32_t last_num = s->phase_num + (n ? n - 1u : 0u) * kRatioNumerator;
  uint32_t needed = last_num / kRatioDenominator + 2u;
  if (needed < consumed + 1u) needed = consumed + 1u;
  int16_t chunk[24];
#if FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
  int16_t kick_resonance = 0;
  int32_t kick_lp_coefficient = 0;
  if (s->model == BRAIDS_PERC_KICK && s->raw_count < needed) {
    uint32_t decay = static_cast<uint16_t>(s->parameter[0]);
    uint32_t scaled = 65535u - (decay << 1);
    uint32_t squared = scaled * scaled >> 16;
    scaled = squared * scaled >> 18;
    kick_resonance = static_cast<int16_t>(32768 - 128 - scaled);
    uint32_t coefficient = static_cast<uint16_t>(s->parameter[1]);
    coefficient = coefficient * coefficient >> 15;
    coefficient = coefficient * coefficient >> 15;
    kick_lp_coefficient = 128 + (coefficient >> 1) * 3;
  }
#endif
  while (s->raw_count < needed) {
    uint32_t count = needed - s->raw_count;
    if (count & 1u) ++count;
    if (count > 24u) count = 24u;
#if FM1_NATIVE_OUTPUT_32K
    /* Native output reads raw frames at offsets 0, 3, 6, ... . Keep the
     * complete generator and filter history cadence, but evaluate the FIR
     * only for frames the native decimator can observe. raw_count carries
     * the global modulo-three alignment across API calls. */
    uint32_t fir_phase = s->raw_count % 3u;
#endif
    render_raw(s, chunk, count
#if FM1_BRAIDS_PERC_KICK_COEFFICIENT_HOIST
        , kick_resonance, kick_lp_coefficient
#endif
        );
    for (uint32_t i = 0; i < count; ++i) {
      /* The FIR is symmetric and its endpoint coefficients are zero. Pairing
       * symmetric samples removes the generic tap loop while retaining the
       * exact integer sum (the maximum absolute sum fits signed 32-bit). */
#if FM1_NATIVE_OUTPUT_32K
      if (fir_phase == 0u) {
#endif
      int32_t filtered =
          (static_cast<int32_t>(s->history[0]) + s->history[6]) * -137 +
          (static_cast<int32_t>(s->history[1]) + s->history[5]) * 364 +
          (static_cast<int32_t>(s->history[2]) + s->history[4]) * 5287 +
          static_cast<int32_t>(s->history[3]) * 21740;
#if FM1_NATIVE_OUTPUT_32K
      filtered >>= 15;
      if (filtered > 32767) filtered = 32767;
      if (filtered < -32768) filtered = -32768;
      s->raw[s->raw_count + i] = static_cast<int16_t>(filtered);
      }
      if (++fir_phase == 3u) fir_phase = 0u;
#endif
      s->history[7] = s->history[6];
      s->history[6] = s->history[5];
      s->history[5] = s->history[4];
      s->history[4] = s->history[3];
      s->history[3] = s->history[2];
      s->history[2] = s->history[1];
      s->history[1] = s->history[0];
      s->history[0] = chunk[i];
#if !FM1_NATIVE_OUTPUT_32K
      /* ARM target and host compilers use arithmetic right shift here, giving
       * the same floor rounding for signed values as the explicit formula. */
      filtered >>= 15;
      if (filtered > 32767) filtered = 32767;
      if (filtered < -32768) filtered = -32768;
      s->raw[s->raw_count + i] = static_cast<int16_t>(filtered);
#endif
    }
    s->raw_count += static_cast<uint16_t>(count);
  }
  uint32_t index = 0u;
#if !FM1_NATIVE_OUTPUT_32K && !FM1_NATIVE_OUTPUT_48K
  uint32_t fraction = s->phase_num;
#endif
  for (uint32_t i = 0; i < n; ++i) {
#if FM1_NATIVE_OUTPUT_32K
    int32_t sample = s->raw[index];
#elif FM1_NATIVE_OUTPUT_48K
    /* The 96 kHz model is exactly 2:1 with native 48 kHz output. The shared
     * 8-tap low-pass still runs at model rate; select every second filtered
     * frame without the 44.1 kHz interpolation path. */
    int32_t sample = s->raw[index];
#else
    int32_t numerator =
        s->raw[index] * static_cast<int32_t>(kRatioDenominator - fraction) +
        s->raw[index + 1u] * static_cast<int32_t>(fraction);
    int32_t sample = braids_interp_div147(numerator);
#endif
    sample = scale_velocity(sample, s->velocity);
    if (s->choked) {
      if (s->choke_left) {
        sample = sample * s->choke_left / static_cast<int32_t>(kChokeFrames);
        --s->choke_left;
        if (!s->choke_left) s->active = 0;
      } else {
        sample = 0;
      }
    }
    out[i] = static_cast<int16_t>(clip16(sample));
    int32_t abs_sample = sample < 0 ? -sample : sample;
    if (abs_sample < 8) {
      if (s->quiet_samples < 65535u) ++s->quiet_samples;
      if (s->quiet_samples >= kQuietFrames) s->active = 0;
    } else {
      s->quiet_samples = 0;
    }
#if FM1_NATIVE_OUTPUT_32K
    index += 3u;
#elif FM1_NATIVE_OUTPUT_48K
    index += 2u;
#else
    /* 320 / 147 = 2 + 26 / 147. The increment pattern repeats exactly
     * without a per-sample quotient and remainder. */
    index += 2u;
    fraction += 26u;
    if (fraction >= kRatioDenominator) {
      fraction -= kRatioDenominator;
      ++index;
    }
#endif
  }
  s->phase_num = static_cast<uint16_t>(end_num % kRatioDenominator);
  if (consumed) {
    uint32_t remaining = s->raw_count - consumed;
    for (uint32_t i = 0; i < remaining; ++i) s->raw[i] = s->raw[i + consumed];
    s->raw_count = static_cast<uint16_t>(remaining);
  }
}
