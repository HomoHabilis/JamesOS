/* SPDX-License-Identifier: GPL-3.0-only
 * Hash the Plaits drum models as this firmware drives them: the three drum
 * engines called directly with the kit path's trigger values (rising edge or
 * low, never "unpatched") and chunk sizes, the kick's half-rate kit entry, and
 * synth-track models 21-23 through the C API; then all 24 synth models
 * through the C API (Voice::Render and every engine). One FNV-1a line per case. */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "plaits_c_api.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/wavetable_pack.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

namespace {

struct Fnv {
  uint64_t h = 1469598103934665603ull;
  void add(const void* data, size_t bytes) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) h = (h ^ p[i]) * 1099511628211ull;
  }
};

enum Kind { BASS, BASS_RATE2, SNARE, HAT };
const char* const kKindName[] = {"bass", "bass_rate2", "snare", "hat"};
/* The kit renders at most 24 frames; clap waits and FIFO tops cut odd ones. */
const size_t kChunks[] = {24, 24, 7, 24, 13, 1, 24, 2, 24, 17};

uint64_t RenderEngine(Kind kind, float note, float timbre, float morph,
                      float harmonics, float accent) {
  static uint8_t scratch[16384];
  static plaits::BassDrumEngine bass;
  static plaits::SnareDrumEngine snare;
  static plaits::HiHatEngine hat;
  plaits::Engine* e = kind == SNARE ? static_cast<plaits::Engine*>(&snare) :
      kind == HAT ? static_cast<plaits::Engine*>(&hat) :
      static_cast<plaits::Engine*>(&bass);
  std::memset(scratch, 0, sizeof scratch);
  plaits_stmlib::BufferAllocator allocator(scratch, sizeof scratch);
  e->Init(&allocator);
  plaits_stmlib::Random::Seed(0x1234567u);
  plaits::EngineParameters p = {};
  p.note = note;
  p.timbre = timbre;
  p.morph = morph;
  p.harmonics = harmonics;
  p.accent = accent;
  Fnv fnv;
  float out[24], aux[24];
  for (size_t block = 0; block < 160; ++block) {
    const size_t n = kChunks[block % (sizeof kChunks / sizeof kChunks[0])];
    p.trigger = (block == 0 || block == 90) ? plaits::TRIGGER_RISING_EDGE :
        plaits::TRIGGER_LOW;
    bool already_enveloped = true;
    if (kind == BASS_RATE2) {
      bass.RenderKitRate2(p, out, aux, n, &already_enveloped);
    } else {
      e->Render(p, out, aux, n, &already_enveloped);
    }
    fnv.add(out, n * sizeof(float));
    fnv.add(aux, n * sizeof(float));
  }
  const uint32_t rng = plaits_stmlib::Random::state();
  fnv.add(&rng, sizeof rng);
  return fnv.h;
}

uint64_t RenderModel(uint32_t model, uint16_t timbre, uint16_t morph,
                     uint16_t harmonics, uint16_t aux_mix) {
  alignas(8) static uint8_t state[PLAITS_STATE_BYTES];
  plaits_state_init(state, 0x3456789u);
  Fnv fnv;
  for (uint32_t block = 0; block < 60; ++block) {
    if (block == 0 || block == 20 || block == 21)
      plaits_note_on(state, model, (48 + block / 4) * 128, timbre, morph,
                     harmonics, 40 + block * 3);
    if (block == 40) plaits_note_off(state, 128);
    int16_t out[32];
    plaits_render(state, model, (48 + block / 20) * 128, timbre, morph,
                  harmonics, aux_mix, out, 32);
    fnv.add(out, sizeof out);
  }
  return fnv.h;
}

/* Every synth model through the C API, with a retrigger while the gate is
 * still high, a legato pitch change and a release tail. */
uint64_t RenderSynth(uint32_t model, uint16_t timbre, uint16_t morph,
                     uint16_t harmonics, uint16_t aux_mix) {
  alignas(8) static uint8_t state[PLAITS_STATE_BYTES];
  plaits_state_init(state, 0x2468aceu);
  Fnv fnv;
  for (uint32_t block = 0; block < 72; ++block) {
    if (block == 0 || block == 18 || block == 19 || block == 50)
      plaits_note_on(state, model, (55 + block / 6) * 128, timbre, morph,
                     harmonics, 20 + block * 2);
    if (block == 36 || block == 66) plaits_note_off(state, 128);
    int16_t out[48];
    const uint32_t n = (block & 3u) == 3u ? 48u : 32u;
    plaits_render(state, model, (55 + block / 24) * 128 + (block & 7) * 9,
                  timbre, morph, harmonics, aux_mix, out, n);
    fnv.add(out, n * sizeof(int16_t));
  }
  return fnv.h;
}

}  // namespace

int main() {
  const float notes[] = {36.0f, 60.0f, 84.0f};
  const float knobs[] = {0.0f, 0.35f, 1.0f};
  const float accents[] = {0.2f, 1.0f};
  for (int kind = BASS; kind <= HAT; ++kind) {
    Fnv all;
    for (float note : notes) for (float t : knobs) for (float m : knobs)
      for (float h : knobs) for (float a : accents) {
        const uint64_t h1 = RenderEngine(static_cast<Kind>(kind), note, t, m, h, a);
        all.add(&h1, sizeof h1);
      }
    std::printf("engine %s %016llx\n", kKindName[kind],
                static_cast<unsigned long long>(all.h));
  }
  const uint16_t q[] = {0u, 12000u, 32767u};
  for (uint32_t model = 21; model <= 23; ++model) {
    Fnv all;
    for (uint16_t t : q) for (uint16_t m : q) for (uint16_t h : q) {
      const uint64_t h1 = RenderModel(model, t, m, h, model == 22 ? 9000u : 0u);
      all.add(&h1, sizeof h1);
    }
    std::printf("model %u %016llx\n", model, static_cast<unsigned long long>(all.h));
  }
#if FM1_PLAITS_PACK_WAVES
  /* As the device does at boot: decode the shared wavetables and DX banks. */
  if (!fm1_plaits_waves_init(nullptr)) return 1;
#endif
  const uint16_t aux[] = {0u, 16000u, 32767u};
  for (uint32_t model = 0; model < PLAITS_MODEL_COUNT; ++model) {
    Fnv all;
    uint32_t k = 0;
    for (uint16_t t : q) for (uint16_t m : q) for (uint16_t h : q) {
      const uint64_t h1 = RenderSynth(model, t, m, h, aux[k++ % 3u]);
      all.add(&h1, sizeof h1);
    }
    std::printf("synth %u %016llx\n", model, static_cast<unsigned long long>(all.h));
  }
  return 0;
}
