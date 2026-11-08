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
// Random impulse train processed by a resonant filter.

#ifndef PLAITS_DSP_NOISE_PARTICLE_H_
#define PLAITS_DSP_NOISE_PARTICLE_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/utils/random.h"
#include "plaits_config.h"

#ifndef FM1_PARTICLE_FAST_RANDOM_FLOAT
#define FM1_PARTICLE_FAST_RANDOM_FLOAT 0
#endif

#ifndef FM1_PARTICLE_FAST_RANDOM_GATE
#define FM1_PARTICLE_FAST_RANDOM_GATE 1
#endif

#ifndef FM1_PARTICLE_SHARED_GATE
#define FM1_PARTICLE_SHARED_GATE 1
#endif
#if FM1_PARTICLE_SHARED_GATE != 0 && FM1_PARTICLE_SHARED_GATE != 1
#error "FM1_PARTICLE_SHARED_GATE must be 0 or 1"
#endif

#ifndef FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD
#define FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD 1
#endif
#if FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD != 0 && \
    FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD != 1
#error "FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD must be 0 or 1"
#endif

#ifndef FM1_PARTICLE_SKIP_SILENT
#define FM1_PARTICLE_SKIP_SILENT 1
#endif

#ifndef FM1_PARTICLE_FAST_INVERSE_SQRT
#define FM1_PARTICLE_FAST_INVERSE_SQRT 0
#endif

#ifndef FM1_PARTICLE_FAST_BANDPASS
#define FM1_PARTICLE_FAST_BANDPASS 1
#endif
#if FM1_PARTICLE_FAST_BANDPASS != 0 && FM1_PARTICLE_FAST_BANDPASS != 1
#error "FM1_PARTICLE_FAST_BANDPASS must be 0 or 1"
#endif

namespace plaits {

#if FM1_PARTICLE_FAST_RANDOM_FLOAT
inline float ParticleRandomFloat() {
  // Keep 23 random mantissa bits and form [0, 1) without integer-to-float
  // conversion and scaling. The discarded precision is below the model's
  // stochastic event and frequency resolution.
  const uint32_t bits =
      (stmlib::Random::GetWord() >> 9) | 0x3f800000u;
  float value;
  __builtin_memcpy(&value, &bits, sizeof(value));
  return value - 1.0f;
}
#else
inline float ParticleRandomFloat() {
  return stmlib::Random::GetFloat();
}
#endif

#if FM1_PLAITS_PARTICLE_LOCAL_RNG
/* Same LCG as Random::GetWord(), but advances a caller-owned state so a
 * ParticleEngine render only selects its per-core stream at its boundaries. */
inline uint32_t ParticleRandomWord(uint32_t* state) {
  *state = *state * 1664525L + 1013904223L;
  return *state;
}

inline float ParticleRandomFloat(uint32_t* state) {
#if FM1_PARTICLE_FAST_RANDOM_FLOAT
  const uint32_t bits = (ParticleRandomWord(state) >> 9) | 0x3f800000u;
  float value;
  __builtin_memcpy(&value, &bits, sizeof(value));
  return value - 1.0f;
#else
  return static_cast<float>(ParticleRandomWord(state)) / 4294967296.0f;
#endif
}
#endif

inline float ParticleInverseSqrtGain(float x) {
#if FM1_PARTICLE_FAST_INVERSE_SQRT
  static_assert(sizeof(float) == sizeof(uint32_t),
                "particle inverse square root requires binary32 floats");
  uint32_t bits;
  __builtin_memcpy(&bits, &x, sizeof(bits));
  // The bit seed is defined only for positive normal binary32 inputs. The
  // actual event-gain range is positive and normal; preserve libm behavior for
  // any exceptional value rather than allowing the approximation to diverge.
  if (bits >= 0x00800000u && bits <= 0x7f7fffffu) {
    bits = 0x5f375a86u - (bits >> 1);
    float estimate;
    __builtin_memcpy(&estimate, &bits, sizeof(estimate));
    const float half_x = 0.5f * x;
    estimate *= 1.5f - half_x * estimate * estimate;
    estimate *= 1.5f - half_x * estimate * estimate;
    return 0.5f * estimate;
  }
#endif
  return 0.5f / stmlib::Sqrt(x);
}

#if FM1_PARTICLE_FAST_RANDOM_GATE
inline float ParticleRandomFloatFromWord(uint32_t word) {
  return static_cast<float>(word) / 4294967296.0f;
}
#endif

#if FM1_PARTICLE_FAST_RANDOM_GATE && \
    FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD
/* Return the exact largest RNG word for which Random::GetFloat() <= density.
 * Positive integer-to-binary32 conversion rounds ties to even. Below 2^24,
 * every integer is exactly representable; above it, the largest accepted
 * word extends to the upper midpoint of density * 2^32, including the tie
 * only when that float's significand is even. Invalid/out-of-range densities
 * produce no random event; densities >= 1 are handled by the caller. */
inline bool ParticleRandomGateThreshold(float density, uint32_t* threshold) {
  static_assert(sizeof(float) == sizeof(uint32_t),
                "particle gate threshold requires binary32 floats");
  if (!(density >= 0.0f && density < 1.0f)) return false;

  const float scaled_density = density * 4294967296.0f;
  uint32_t scaled_bits;
  __builtin_memcpy(&scaled_bits, &scaled_density, sizeof(scaled_bits));
  const uint32_t exponent = (scaled_bits >> 23) & 0xffu;
  uint32_t max_word = static_cast<uint32_t>(scaled_density);
  if (exponent >= 151u) {
    const uint32_t half_ulp = 1u << (exponent - 151u);
    max_word += half_ulp - (scaled_bits & 1u);
  }
  *threshold = max_word;
  return true;
}
#endif

#if FM1_PARTICLE_FAST_RANDOM_GATE && FM1_PARTICLE_SHARED_GATE
struct ParticleRandomGate {
  float density;
  uint32_t threshold;
  uint32_t disable;
  bool always;
  bool force_first_event;
};

inline ParticleRandomGate PrepareParticleRandomGate(float density, bool sync) {
  ParticleRandomGate gate;
  gate.density = density;
  gate.always = density >= 1.0f;
  gate.threshold = 0u;
#if FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD
  const bool threshold_valid = gate.always ||
      ParticleRandomGateThreshold(density, &gate.threshold);
  gate.disable = threshold_valid ? 0u : 1u;
  gate.force_first_event = sync && density == density;
#else
  if (!gate.always)
    gate.threshold = static_cast<uint32_t>(density * 4294967296.0f);
  gate.disable = 0u;
  gate.force_first_event = sync;
#endif
  return gate;
}
#endif

class Particle {
 public:
  Particle() { }
  ~Particle() { }

  inline void Init() {
    pre_gain_ = 0.0f;
    filter_.Init();
  }
  
  inline void Render(
#if FM1_PARTICLE_FAST_RANDOM_GATE && FM1_PARTICLE_SHARED_GATE
      const ParticleRandomGate& random_gate,
#else
      bool sync,
      float density,
#endif
      float* density_sqrt_cache,
      bool* density_sqrt_ready,
      float gain,
      float frequency,
      float spread,
      float q,
      float* out,
      float* aux,
      size_t size
#if FM1_PLAITS_PARTICLE_LOCAL_RNG
      , uint32_t* local_random_state
#endif
      ) {
#if FM1_PARTICLE_FAST_RANDOM_GATE
#if FM1_PLAITS_PARTICLE_LOCAL_RNG
    uint32_t random_word = ParticleRandomWord(local_random_state);
#else
    uint32_t random_word = stmlib::Random::GetWord();
#endif
#if FM1_PARTICLE_SHARED_GATE
    const float density = random_gate.density;
    const bool density_always_triggers = random_gate.always;
    const uint32_t density_threshold = random_gate.threshold;
    const uint32_t density_gate_disable = random_gate.disable;
    random_word |= density_gate_disable;
    bool force_first_event = random_gate.force_first_event;
#else
    const bool density_always_triggers = density >= 1.0f;
    uint32_t density_threshold = 0u;
#if FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD
    const bool density_threshold_valid = density_always_triggers ||
        ParticleRandomGateThreshold(density, &density_threshold);
    const uint32_t density_gate_disable = density_threshold_valid ? 0u : 1u;
    random_word |= density_gate_disable;
    bool force_first_event = sync && density == density;
#else
    if (!density_always_triggers) {
      density_threshold = static_cast<uint32_t>(density * 4294967296.0f);
    }
    bool force_first_event = sync;
#endif
#endif
#else
#if FM1_PLAITS_PARTICLE_LOCAL_RNG
    float u = ParticleRandomFloat(local_random_state);
#else
    float u = ParticleRandomFloat();
#endif
    if (sync) {
      u = density;
    }
#endif
    bool can_radomize_frequency = true;
#if FM1_PARTICLE_FAST_BANDPASS
    // The filter's band-pass path uses the same two products twice to advance
    // its TPT integrator states. Cache the equivalent grouped coefficients for
    // this render block and refresh them only when the randomized cutoff is
    // installed.
    float bp_r_plus_g = filter_.r() + filter_.g();
    float bp_two_g = 2.0f * filter_.g();
#endif
    while (size--) {
      float s = 0.0f;
#if FM1_PARTICLE_FAST_RANDOM_GATE
      const bool event = force_first_event || density_always_triggers ||
          random_word <= density_threshold;
      if (event) {
        const float u = force_first_event
            ? density : ParticleRandomFloatFromWord(random_word);
#else
      if (u <= density) {
#endif
        s = u * gain;
        if (can_radomize_frequency) {
#if FM1_PLAITS_PARTICLE_LOCAL_RNG
          const float u = 2.0f * ParticleRandomFloat(local_random_state) - 1.0f;
#else
          const float u = 2.0f * ParticleRandomFloat() - 1.0f;
#endif
          const float f = std::min(
              stmlib::SemitonesToRatio(spread * u) * frequency,
              0.25f);
          if (!*density_sqrt_ready) {
            *density_sqrt_cache = stmlib::Sqrt(density);
            *density_sqrt_ready = true;
          }
          pre_gain_ = ParticleInverseSqrtGain(
              q * f * *density_sqrt_cache);
          filter_.set_f_q<stmlib::FREQUENCY_DIRTY>(f, q);
#if FM1_PARTICLE_FAST_BANDPASS
          bp_r_plus_g = filter_.r() + filter_.g();
          bp_two_g = 2.0f * filter_.g();
#endif
          // Keep the cutoff constant for this whole block.
          can_radomize_frequency = false;
        }
      }
      *aux++ += s;
#if FM1_PARTICLE_SKIP_SILENT
      if (pre_gain_ != 0.0f) {
#if FM1_PARTICLE_FAST_BANDPASS
        *out += filter_.ProcessBandPassReduced(
            pre_gain_ * s, bp_r_plus_g, bp_two_g);
#else
        *out += filter_.Process<stmlib::FILTER_MODE_BAND_PASS>(pre_gain_ * s);
#endif
      }
      ++out;
#else
#if FM1_PARTICLE_FAST_BANDPASS
      *out++ += filter_.ProcessBandPassReduced(
          pre_gain_ * s, bp_r_plus_g, bp_two_g);
#else
      *out++ += filter_.Process<stmlib::FILTER_MODE_BAND_PASS>(pre_gain_ * s);
#endif
#endif
#if FM1_PARTICLE_FAST_RANDOM_GATE
      force_first_event = false;
#if FM1_PLAITS_PARTICLE_LOCAL_RNG
      random_word = ParticleRandomWord(local_random_state);
#else
      random_word = stmlib::Random::GetWord();
#endif
#if FM1_PARTICLE_EXACT_RANDOM_GATE_THRESHOLD
      random_word |= density_gate_disable;
#endif
#else
#if FM1_PLAITS_PARTICLE_LOCAL_RNG
      u = ParticleRandomFloat(local_random_state);
#else
      u = ParticleRandomFloat();
#endif
#endif
    }
#if STMLIB_FLUSH_TINY_FILTER_STATES
    filter_.FlushTinyState();
#endif
#if FM1_PARTICLE_SKIP_SILENT
    if (pre_gain_ != 0.0f && filter_.IsTinyState()) {
      filter_.Reset();
      pre_gain_ = 0.0f;
    }
#endif
  }
 
 private:
  float pre_gain_;
  stmlib::Svf filter_;
  
  DISALLOW_COPY_AND_ASSIGN(Particle);
};

}  // namespace plaits

#endif  // PLAITS_DSP_NOISE_PARTICLE_H_
