/* SPDX-License-Identifier: MIT
 * Integer-control adapter for the upstream Plaits reference DSP.
 * Copyright 2026.
 */
#include "plaits_c_api.h"
#include "../hal/fm1_audio_rate.h"

#include <new>
#include <string.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"
#include "stmlib/utils/random.h"

#ifndef FM1_PLAITS_AUX_MIX_Q15_SHIFT
#define FM1_PLAITS_AUX_MIX_Q15_SHIFT 0
#endif
#ifndef FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
#define FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP 1
#endif

namespace {

static const uint32_t kOutputRate = FM1_AUDIO_RATE_HZ;
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
#ifndef FM1_PLAITS_SRC_LAGRANGE4
#define FM1_PLAITS_SRC_LAGRANGE4 1
#endif
#if FM1_PLAITS_SRC_LAGRANGE4 && \
    !(FM1_PLAITS_SOURCE_RATE_38400 || FM1_PLAITS_SOURCE_RATE_32K)
#error "FM1_PLAITS_SRC_LAGRANGE4 is only for lower-rate source paths"
#endif
#if (FM1_PLAITS_SOURCE_RATE_32K + FM1_PLAITS_SOURCE_RATE_44100 + \
     FM1_PLAITS_SOURCE_RATE_38400 + FM1_PLAITS_SOURCE_RATE_48000) > 1
#error "Select at most one non-default Plaits source-rate experiment"
#endif
#if FM1_PLAITS_SOURCE_RATE_44100 && \
    (FM1_NATIVE_OUTPUT_32K || FM1_NATIVE_OUTPUT_48K)
#error "The 44.1 kHz Plaits source selector requires the 44.1 kHz output profile"
#endif
#if FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION && \
    (!FM1_PLAITS_SOURCE_RATE_32K || !FM1_PLAITS_SRC_LAGRANGE4 || \
     !FM1_PLAITS_SRC_ROLLING_CACHE)
#error "FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION requires the 32 kHz cached cubic path"
#endif
#if FM1_PLAITS_SOURCE_RATE_44100
#if PLAITS_INTERNAL_SAMPLE_RATE != 44100
#error "FM1_PLAITS_SOURCE_RATE_44100 requires all Plaits units to build at 44.1 kHz"
#endif
static const uint32_t kPhaseStepNumerator = 1u;
static const uint32_t kPhaseDenominator = 1u;
#elif FM1_PLAITS_SOURCE_RATE_32K
#if PLAITS_INTERNAL_SAMPLE_RATE != 32000
#error "FM1_PLAITS_SOURCE_RATE_32K requires all Plaits units to build at 32 kHz"
#endif
#if FM1_NATIVE_OUTPUT_32K
static const uint32_t kPhaseStepNumerator = 1u;
static const uint32_t kPhaseDenominator = 1u;
#else
static const uint32_t kPhaseStepNumerator = 320u;
static const uint32_t kPhaseDenominator = 441u;
#endif
#elif FM1_PLAITS_SOURCE_RATE_38400
#if PLAITS_INTERNAL_SAMPLE_RATE != 38400
#error "FM1_PLAITS_SOURCE_RATE_38400 requires all Plaits units to build at 38.4 kHz"
#endif
static const uint32_t kPhaseStepNumerator = 128u;
static const uint32_t kPhaseDenominator = 147u;
#elif FM1_PLAITS_SOURCE_RATE_48000
#if PLAITS_INTERNAL_SAMPLE_RATE != 48000
#error "FM1_PLAITS_SOURCE_RATE_48000 requires all Plaits units to build at 48 kHz"
#endif
#if FM1_NATIVE_OUTPUT_48K
static const uint32_t kPhaseStepNumerator = 1u;
static const uint32_t kPhaseDenominator = 1u;
#else
static const uint32_t kPhaseStepNumerator = 160u;
static const uint32_t kPhaseDenominator = 147u;
#endif
#else
#if PLAITS_INTERNAL_SAMPLE_RATE != 48000
#error "Non-48 kHz Plaits builds must explicitly enable their source-rate selector"
#endif
static const uint32_t kPhaseStepNumerator = 160u;
static const uint32_t kPhaseDenominator = 147u;
#endif
static const uint32_t kPhaseIndexStep =
    kPhaseStepNumerator / kPhaseDenominator;
static const uint32_t kPhaseRemainderStep =
    kPhaseStepNumerator % kPhaseDenominator;
#if FM1_NATIVE_OUTPUT_32K && !FM1_PLAITS_SOURCE_RATE_32K
#error "Native 32 kHz output requires the Plaits 32 kHz source path"
#endif
#if FM1_NATIVE_OUTPUT_48K && !FM1_PLAITS_SOURCE_RATE_48000
#error "Native 48 kHz output requires the Plaits 48 kHz source path"
#endif
#define FM1_PLAITS_NATIVE_RATE \
    ((FM1_NATIVE_OUTPUT_32K && FM1_PLAITS_SOURCE_RATE_32K) || \
     (FM1_NATIVE_OUTPUT_48K && FM1_PLAITS_SOURCE_RATE_48000))
static_assert(plaits::kSampleRateHz == PLAITS_INTERNAL_SAMPLE_RATE,
              "Plaits wrapper and upstream translation units use different rates");
#ifndef FM1_PLAITS_PHASE_LUT
#define FM1_PLAITS_PHASE_LUT 1
#endif
#if FM1_PLAITS_PHASE_LUT && !FM1_PLAITS_NATIVE_RATE && \
    (PLAITS_INTERNAL_SAMPLE_RATE == 48000 || \
                              PLAITS_INTERNAL_SAMPLE_RATE == 38400)
#if defined(FM1_DEVICE)
static const uint16_t kPhaseFracQ16[kPhaseDenominator]
    __attribute__((section(".data.fm1_plaits_phase"))) = {
#else
static const uint16_t kPhaseFracQ16[kPhaseDenominator] = {
#endif
    0, 445, 891, 1337, 1783, 2229, 2674, 3120, 3566, 4012, 4458, 4904, 5349, 5795, 6241, 6687, 7133, 7578, 8024, 8470, 8916, 9362, 9808, 10253, 10699, 11145, 11591, 12037, 12483, 12928, 13374, 13820, 14266, 14712, 15157, 15603, 16049, 16495, 16941, 17387, 17832, 18278, 18724, 19170, 19616, 20062, 20507, 20953, 21399, 21845, 22291, 22736, 23182, 23628, 24074, 24520, 24966, 25411, 25857, 26303, 26749, 27195, 27641, 28086, 28532, 28978, 29424, 29870, 30315, 30761, 31207, 31653, 32099, 32545, 32990, 33436, 33882, 34328, 34774, 35220, 35665, 36111, 36557, 37003, 37449, 37894, 38340, 38786, 39232, 39678, 40124, 40569, 41015, 41461, 41907, 42353, 42799, 43244, 43690, 44136, 44582, 45028, 45473, 45919, 46365, 46811, 47257, 47703, 48148, 48594, 49040, 49486, 49932, 50378, 50823, 51269, 51715, 52161, 52607, 53052, 53498, 53944, 54390, 54836, 55282, 55727, 56173, 56619, 57065, 57511, 57957, 58402, 58848, 59294, 59740, 60186, 60631, 61077, 61523, 61969, 62415, 62861, 63306, 63752, 64198, 64644, 65090
};
static_assert(sizeof(kPhaseFracQ16) / sizeof(kPhaseFracQ16[0]) ==
              kPhaseDenominator, "phase coefficient LUT size mismatch");
#endif
#if PLAITS_USE_RENDER_CHUNK24
static const uint32_t kFifoFrames =
    FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE ? 62u : 64u;
#else
static const uint32_t kFifoFrames =
    FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE ? 46u : 48u;
#endif
#if PLAITS_USE_RENDER_CHUNK24
static const uint32_t kMaxRenderFrames = plaits::kMaxBlockSize;
#else
static const uint32_t kMaxRenderFrames = plaits::kBlockSize;
#endif
static_assert(kMaxRenderFrames <= plaits::kMaxBlockSize,
              "Plaits source render chunk exceeds the engine maximum");
/* plaits_render splits public requests at 32 output frames. The 48 kHz path
 * needs at most 36 source frames for one such request; 38.4 kHz needs at most
 * 29. Since the FIFO is below that lookahead before appending, a fixed render
 * chunk needs at most max-required + chunk - 1 slots. */
#if FM1_PLAITS_SOURCE_RATE_38400
static const uint32_t kMaxRequiredFrames = 29u;
#else
static const uint32_t kMaxRequiredFrames = 36u;
#endif
static_assert(kFifoFrames >= kMaxRequiredFrames + kMaxRenderFrames - 1u,
              "Plaits FIFO cannot hold one render chunk plus lookahead");
static const uint32_t kQuietFrames = 3u * kOutputRate;
#ifndef FM1_PLAITS_SHORT_PERCUSSIVE_QUIET
#define FM1_PLAITS_SHORT_PERCUSSIVE_QUIET 1
#endif
/* Once the track envelope is off the voice's output is discarded, so the
 * quiet period only keeps the engine state warm for the next note. Kick,
 * snare and hi-hat (engines 21-23) decay on their own and stop rendering
 * after 0.75 s instead of 3 s. */
static inline uint32_t QuietFramesFor(uint32_t model) {
#if FM1_PLAITS_SHORT_PERCUSSIVE_QUIET
  if (model >= 21u && model <= 23u) return (3u * kOutputRate) / 4u;
#else
  (void)model;
#endif
  return kQuietFrames;
}

struct PlaitsState {
  explicit PlaitsState(uint32_t seed)
      : allocator(arena, sizeof(arena)), random_state(seed ? seed : 1u),
        model(0), pitch_q7(69 * 128), timbre_q15(0),
        morph_q15(0), harmonics_q15(0), velocity(127), gate(0),
        fifo_count(0), phase_q16(0), quiet_frames(kQuietFrames),
        last_rendered_gate(0), retrigger_low_pending(0) {
#if FM1_PLAITS_CACHE_FLOAT_CONTROLS
    note = 69.0f;
    timbre = morph = harmonics = 0.0f;
#endif
    voice.Init(&allocator);
    memset(fifo, 0, sizeof(fifo));
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE
    memset(history, 0, sizeof(history));
#endif
  }

  plaits::Voice voice;
  stmlib::BufferAllocator allocator;
  uint8_t arena[16384];
  plaits::Voice::Frame fifo[kFifoFrames];
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE
  /* Last two consumed source frames, for a one-source-frame-delayed cubic
   * interpolator. The ordinary FIFO append/consume cadence is unchanged. */
  plaits::Voice::Frame history[2];
#endif
  uint32_t random_state;
  uint32_t model;
  int32_t pitch_q7;
  uint16_t timbre_q15;
  uint16_t morph_q15;
  uint16_t harmonics_q15;
#if FM1_PLAITS_CACHE_FLOAT_CONTROLS
  float note;
  float timbre;
  float morph;
  float harmonics;
#endif
  uint32_t velocity;
  uint32_t gate;
  uint32_t fifo_count;
  uint32_t phase_q16;
  uint32_t quiet_frames;
  uint32_t last_rendered_gate;
  uint32_t retrigger_low_pending;
};

#if FM1_PLAITS_SRC_LAGRANGE4
static inline float lagrange4_one_frame(float p0, float p1, float p2,
                                        float p3, float t) {
  /* Cubic Lagrange through nodes -1,0,1,2. The result is timestamped one
   * source frame behind the existing [index,index+1] linear interpolator. */
  const float c1 = (-p0 * (1.0f / 3.0f)) - p1 * 0.5f + p2 - p3 * (1.0f / 6.0f);
  const float c2 = p0 * 0.5f - p1 + p2 * 0.5f;
  const float c3 = -p0 * (1.0f / 6.0f) + p1 * 0.5f - p2 * 0.5f + p3 * (1.0f / 6.0f);
  return p1 + t * (c1 + t * (c2 + t * c3));
}
#endif

inline uint32_t RenderFramesForModel(uint32_t model) {
#if FM1_PLAITS_RENDER_CHUNK24
  (void)model;
  return plaits::kMaxBlockSize;
#elif FM1_PLAITS_RENDER_CHUNK24_MODEL_MASK || FM1_MODAL20_RENDER_CHUNK24
  return ((FM1_PLAITS_RENDER_CHUNK24_MODEL_MASK & (1u << model)) ||
          (FM1_MODAL20_RENDER_CHUNK24 && model == 20u))
      ? plaits::kMaxBlockSize : plaits::kBlockSize;
#else
  (void)model;
  return plaits::kBlockSize;
#endif
}

static_assert(sizeof(PlaitsState) <= PLAITS_STATE_BYTES,
              "increase PLAITS_STATE_BYTES for the Plaits reference state");
static_assert((PLAITS_STATE_BYTES & 7u) == 0u,
              "Plaits state cap must preserve native eight-byte alignment");
static_assert(alignof(PlaitsState) <= 8u,
              "Plaits state needs a larger published alignment");

inline float UnitQ15(uint16_t value) {
  return static_cast<float>(value) * (1.0f / 32767.0f);
}

inline float PitchQ7(int32_t value) {
  return static_cast<float>(value) * (1.0f / 128.0f);
}

inline int32_t MixAuxQ15(int32_t weighted) {
#if FM1_PLAITS_AUX_MIX_Q15_SHIFT && \
    (FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_NATIVE_RATE)
  /* The int16 channels are blended with nonnegative weights summing to 32767.
   * Divide by 32768 instead: this is at most one output LSB away and avoids
   * a non-power-of-two quotient in the per-sample output path. */
  return weighted >> 15;
#else
  return weighted / 32767;
#endif
}

#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE
static inline int32_t MixAuxFrame(const plaits::Voice::Frame& frame,
                                  uint32_t out_gain, uint32_t aux_mix_q15) {
  if (!aux_mix_q15) return frame.out;
  return MixAuxQ15(static_cast<int32_t>(frame.out) * out_gain +
                   static_cast<int32_t>(frame.aux) * aux_mix_q15);
}
#endif

#if FM1_PLAITS_SRC_LAGRANGE4 && FM1_PLAITS_SRC_ROLLING_CACHE && \
    !FM1_PLAITS_NATIVE_RATE
struct SourceCubicCache {
  uint32_t valid;
  uint32_t index;
  float point[4];
  float c1, c2, c3;
};

static inline float MixedSourcePoint(const PlaitsState* s, uint32_t index,
                                     uint32_t point, uint32_t out_gain,
                                     uint32_t aux_mix_q15) {
  const plaits::Voice::Frame* frame;
  if (point == 0u) {
    frame = index >= 2u ? &s->fifo[index - 2u] :
        (index == 1u ? &s->history[1] : &s->history[0]);
  } else if (point == 1u) {
    frame = index >= 1u ? &s->fifo[index - 1u] : &s->history[1];
  } else {
    frame = &s->fifo[index + point - 2u];
  }
  return static_cast<float>(MixAuxFrame(*frame, out_gain, aux_mix_q15));
}

static inline void PrepareSourceCubic(SourceCubicCache* cache,
                                      const PlaitsState* s, uint32_t index,
                                      uint32_t out_gain,
                                      uint32_t aux_mix_q15) {
  if (cache->valid && cache->index == index) return;
  if (cache->valid && index == cache->index + 1u) {
    cache->point[0] = cache->point[1];
    cache->point[1] = cache->point[2];
    cache->point[2] = cache->point[3];
    cache->point[3] = MixedSourcePoint(s, index, 3u, out_gain,
                                        aux_mix_q15);
  } else {
    for (uint32_t i = 0u; i < 4u; ++i)
      cache->point[i] = MixedSourcePoint(s, index, i, out_gain,
                                          aux_mix_q15);
  }
  const float p0 = cache->point[0];
  const float p1 = cache->point[1];
  const float p2 = cache->point[2];
  const float p3 = cache->point[3];
  cache->c1 = (-p0 * (1.0f / 3.0f)) - p1 * 0.5f + p2 - p3 * (1.0f / 6.0f);
  cache->c2 = p0 * 0.5f - p1 + p2 * 0.5f;
  cache->c3 = -p0 * (1.0f / 6.0f) + p1 * 0.5f - p2 * 0.5f + p3 * (1.0f / 6.0f);
  cache->index = index;
  cache->valid = 1u;
}

static inline float RenderSourceCubic(const SourceCubicCache& cache, float t) {
  return cache.point[1] + t * (cache.c1 + t * (cache.c2 + t * cache.c3));
}
#endif

void AppendFrames(PlaitsState* s, const plaits::Patch& patch,
                  plaits::Modulations& mod, uint32_t frames) {
  if (!frames || frames > kMaxRenderFrames ||
      s->fifo_count + frames > kFifoFrames) return;
  /* Control values remain constant while this output block fills the FIFO. */
  const bool force_low = s->retrigger_low_pending != 0u;
  mod.trigger = (!force_low && s->gate) ? 1.0f : 0.0f;
  mod.level = static_cast<float>(s->velocity) * (1.0f / 127.0f);
  /* FIFO capacity was checked above, so the reference Voice can render
   * directly into its final storage instead of staging/copying 12 frames. */
  plaits::Voice::Frame* block = &s->fifo[s->fifo_count];

  const uint32_t previous_rng = stmlib::Random::state();
  stmlib::Random::Seed(s->random_state);
  s->voice.Render(patch, mod, block, frames);
  s->random_state = stmlib::Random::state();
  stmlib::Random::Seed(previous_rng);
  if (force_low) s->retrigger_low_pending = 0u;
  s->last_rendered_gate = (!force_low && s->gate) ? 1u : 0u;

  s->fifo_count += frames;
}

void UpdateControls(PlaitsState* s, uint32_t model, int32_t pitch_q7,
                    uint16_t timbre_q15, uint16_t morph_q15,
                    uint16_t harmonics_q15) {
  if (model >= PLAITS_MODEL_COUNT) model = PLAITS_MODEL_COUNT - 1u;
  if (timbre_q15 > 32767u) timbre_q15 = 32767u;
  if (morph_q15 > 32767u) morph_q15 = 32767u;
  if (harmonics_q15 > 32767u) harmonics_q15 = 32767u;
  s->model = model;
#if FM1_PLAITS_CACHE_FLOAT_CONTROLS
  if (s->pitch_q7 != pitch_q7) s->note = PitchQ7(pitch_q7);
  if (s->timbre_q15 != timbre_q15) s->timbre = UnitQ15(timbre_q15);
  if (s->morph_q15 != morph_q15) s->morph = UnitQ15(morph_q15);
  if (s->harmonics_q15 != harmonics_q15)
    s->harmonics = UnitQ15(harmonics_q15);
#endif
  s->pitch_q7 = pitch_q7;
  s->timbre_q15 = timbre_q15;
  s->morph_q15 = morph_q15;
  s->harmonics_q15 = harmonics_q15;
}

#if FM1_PLAITS_CAPI_PREPARE_LARGE_CALL
/* The large-call wrapper reuses the same bounded render body as the legacy
 * 32-frame recursion, but shares its prepared controls across all chunks. */
inline void RenderPreparedChunk(PlaitsState* s,
                                const plaits::Patch& patch,
                                plaits::Modulations& mod,
                                uint16_t aux_mix_q15,
                                int16_t* out,
                                uint32_t n) {
  if (aux_mix_q15 > 32767u) aux_mix_q15 = 32767u;
  const uint32_t out_gain = 32767u - aux_mix_q15;
  uint32_t index = 0u;
  uint32_t phase_remainder = s->phase_q16;
#if FM1_PLAITS_SRC_LAGRANGE4 && FM1_PLAITS_SRC_ROLLING_CACHE && \
    !FM1_PLAITS_SOURCE_RATE_44100 && !FM1_PLAITS_NATIVE_RATE
  SourceCubicCache cubic_cache = {};
#endif
  // Fill exactly far enough for this output block before entering its sample
  // loop. The 44.1 kHz path is unity-rate; 32 kHz advances by 320/441,
  // 38.4 kHz by 128/147, and 48 kHz by 160/147 source frames per output sample.
  if (n) {
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_NATIVE_RATE
    const uint32_t required = n;
#else
    const uint32_t transitions = n - 1u;
    const uint32_t required = 2u +
        (phase_remainder + kPhaseStepNumerator * transitions) /
            kPhaseDenominator;
#endif
    while (s->fifo_count < required) {
      const uint32_t previous_count = s->fifo_count;
      const uint32_t frames = RenderFramesForModel(s->model);
      AppendFrames(s, patch, mod, frames);
      if (s->fifo_count == previous_count) break;
    }
  }
#if FM1_PLAITS_NATIVE_RATE && FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH
  /* The FIFO is filled above; native output consumes one source frame per
   * output frame. Move the bounds check outside the common sample loop. */
  const uint32_t available = s->fifo_count < n ? s->fifo_count : n;
  for (uint32_t i = 0; i < available; ++i) {
    int32_t sample;
    if (!aux_mix_q15) {
      sample = s->fifo[i].out;
    } else {
      const int32_t weighted =
          static_cast<int32_t>(s->fifo[i].out) * out_gain +
          static_cast<int32_t>(s->fifo[i].aux) * aux_mix_q15;
      sample = MixAuxQ15(weighted);
    }
#if !FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    out[i] = static_cast<int16_t>(sample);
  }
  for (uint32_t i = available; i < n; ++i) out[i] = 0;
  index = available;
#else
  for (uint32_t i = 0; i < n; ++i) {
    if (s->fifo_count <= index) { out[i] = 0; continue; }
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_NATIVE_RATE
    /* Native source and output rates coincide, so the FIFO sample is already
     * at the requested time. Avoid the interpolator and its lookahead read. */
    int32_t sample;
    if (!aux_mix_q15) {
      sample = s->fifo[index].out;
    } else {
      const int32_t weighted =
          static_cast<int32_t>(s->fifo[index].out) * out_gain +
          static_cast<int32_t>(s->fifo[index].aux) * aux_mix_q15;
      sample = MixAuxQ15(weighted);
    }
#if !FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    out[i] = static_cast<int16_t>(sample);
    ++index;
#else
    if (s->fifo_count < index + 2u) { out[i] = 0; continue; }
#if FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION
    const float t = static_cast<float>(phase_remainder) *
        (1.0f / static_cast<float>(kPhaseDenominator));
#else
#if FM1_PLAITS_PHASE_LUT && !FM1_PLAITS_NATIVE_RATE && \
    (PLAITS_INTERNAL_SAMPLE_RATE == 48000 || \
     PLAITS_INTERNAL_SAMPLE_RATE == 38400)
    const uint32_t frac = kPhaseFracQ16[phase_remainder];
#else
    const uint32_t frac = (phase_remainder << 16) / kPhaseDenominator;
#endif
#endif
#if !FM1_PLAITS_SRC_LAGRANGE4 || !FM1_PLAITS_SRC_ROLLING_CACHE
    int32_t mixed_a, mixed_b;
    if (!aux_mix_q15) {
      /* The general weighted path below divides each mixed endpoint by 32767.
       * At zero AUX that is exactly the original int16 OUT value. */
      mixed_a = s->fifo[index].out;
      mixed_b = s->fifo[index + 1u].out;
    } else {
      const int32_t a = static_cast<int32_t>(s->fifo[index].out) * out_gain +
          static_cast<int32_t>(s->fifo[index].aux) * aux_mix_q15;
      const int32_t b = static_cast<int32_t>(s->fifo[index + 1u].out) * out_gain +
          static_cast<int32_t>(s->fifo[index + 1u].aux) * aux_mix_q15;
      mixed_a = MixAuxQ15(a);
      mixed_b = MixAuxQ15(b);
    }
#endif
#if FM1_PLAITS_SRC_LAGRANGE4
#if !FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION
    const float t = static_cast<float>(frac) * (1.0f / 65536.0f);
#endif
#if FM1_PLAITS_SRC_ROLLING_CACHE
    PrepareSourceCubic(&cubic_cache, s, index, out_gain, aux_mix_q15);
    int32_t sample = static_cast<int32_t>(RenderSourceCubic(cubic_cache, t));
#else
    const plaits::Voice::Frame& prev2 = index >= 2u ? s->fifo[index - 2u] :
        (index == 1u ? s->history[1] : s->history[0]);
    const plaits::Voice::Frame& prev1 = index >= 1u ? s->fifo[index - 1u] :
        s->history[1];
    const int32_t mixed_prev2 = MixAuxFrame(prev2, out_gain, aux_mix_q15);
    const int32_t mixed_prev1 = MixAuxFrame(prev1, out_gain, aux_mix_q15);
    const int32_t mixed_cur = mixed_a;
    const int32_t mixed_next = mixed_b;
    int32_t sample = static_cast<int32_t>(lagrange4_one_frame(
        static_cast<float>(mixed_prev2), static_cast<float>(mixed_prev1),
        static_cast<float>(mixed_cur), static_cast<float>(mixed_next), t));
#endif
#else
    const int32_t delta = mixed_b - mixed_a;
    /* |delta| <= 65535 and frac <= 65535, so this unsigned product fits in
     * 32 bits. The negative branch reproduces arithmetic >> 16 (floor), not
     * C's truncation-toward-zero division. */
    const uint32_t magnitude = static_cast<uint32_t>(delta < 0 ? -delta : delta);
    const uint32_t product = magnitude * frac;
    const int32_t step = delta < 0 ?
        -static_cast<int32_t>((product + 0xffffu) >> 16) :
         static_cast<int32_t>(product >> 16);
    int32_t sample = mixed_a + step;
#endif
#if FM1_PLAITS_SRC_LAGRANGE4
    /* Cubic interpolation has negative outer weights and can exceed int16.
     * Clamp before narrowing to prevent wraparound. */
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#elif !FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    out[i] = static_cast<int16_t>(sample);
    index += kPhaseIndexStep;
    phase_remainder += kPhaseRemainderStep;
    if (phase_remainder >= kPhaseDenominator) {
      phase_remainder -= kPhaseDenominator;
      ++index;
    }
#endif
  }
#endif
  const uint32_t consumed = index;
  if (consumed && consumed <= s->fifo_count) {
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE
    if (consumed >= 2u) {
      s->history[0] = s->fifo[consumed - 2u];
      s->history[1] = s->fifo[consumed - 1u];
    } else {
      s->history[0] = s->history[1];
      s->history[1] = s->fifo[0];
    }
#endif
    if (consumed < s->fifo_count) {
      memmove(s->fifo, s->fifo + consumed,
              (s->fifo_count - consumed) * sizeof(s->fifo[0]));
    }
    s->fifo_count -= consumed;
  }
  s->phase_q16 = phase_remainder;
  if (!s->gate) {
    s->quiet_frames += n;
    if (s->quiet_frames > kQuietFrames) s->quiet_frames = kQuietFrames;
  } else {
    s->quiet_frames = 0u;
  }
}
#endif


}  // namespace

extern "C" {

void PLAITS_STATE_ABI_SYMBOL(void) {}

__attribute__((minsize, noinline)) void plaits_state_init(void* state, uint32_t seed) {
  if (!state) return;
  memset(state, 0, sizeof(PlaitsState));
  new (state) PlaitsState(seed);
}

void plaits_prepare_model(void* state, uint32_t model) {
#if FM1_PLAITS_PREWARM_MODEL
  if (!state) return;
  if (model >= PLAITS_MODEL_COUNT) model = PLAITS_MODEL_COUNT - 1u;
  PlaitsState* s = static_cast<PlaitsState*>(state);
  const uint32_t previous_rng = stmlib::Random::state();
  stmlib::Random::Seed(s->random_state);
  s->voice.PrepareEngine(static_cast<int>(model));
  s->random_state = stmlib::Random::state();
  stmlib::Random::Seed(previous_rng);
#else
  (void)state;
  (void)model;
#endif
}

void plaits_note_on(void* state, uint32_t model, int32_t pitch_q7,
                    uint16_t timbre_q15, uint16_t morph_q15,
                    uint16_t harmonics_q15, uint32_t velocity_0_127) {
  if (!state) return;
  PlaitsState* s = static_cast<PlaitsState*>(state);
  UpdateControls(s, model, pitch_q7, timbre_q15, morph_q15, harmonics_q15);
  if (s->last_rendered_gate) s->retrigger_low_pending = 1u;
  s->gate = 1u;
  s->velocity = velocity_0_127 > 127u ? 127u : velocity_0_127;
  s->quiet_frames = 0u;
}

void plaits_note_off(void* state, uint32_t raw_note) {
  if (!state) return;
  PlaitsState* s = static_cast<PlaitsState*>(state);
  if (s->gate && raw_note == 128u) {
    s->gate = 0u;
    s->quiet_frames = 0u;
  }
}

void plaits_all_off(void* state) {
  if (!state) return;
  PlaitsState* s = static_cast<PlaitsState*>(state);
  const bool was_active = s->gate || s->last_rendered_gate ||
      s->retrigger_low_pending;
  s->gate = 0u;
  if (was_active) s->quiet_frames = 0u;
}

uint32_t plaits_quiet(const void* state) {
  if (!state) return 1u;
  const PlaitsState* s = static_cast<const PlaitsState*>(state);
  return !s->gate && s->quiet_frames >= QuietFramesFor(s->model);
}

int32_t plaits_active_engine(const void* state) {
  if (!state) return -1;
  const PlaitsState* s = static_cast<const PlaitsState*>(state);
  return s->voice.active_engine();
}

#if defined(TEST)
/* Host-only visibility for exact state-transition comparisons. */
void plaits_test_metadata(const void* state, uint32_t words[9]) {
  if (!state || !words) return;
  const PlaitsState* s = static_cast<const PlaitsState*>(state);
  words[0] = s->random_state;
  words[1] = s->model;
  words[2] = s->velocity;
  words[3] = s->gate;
  words[4] = s->fifo_count;
  words[5] = s->phase_q16;
  words[6] = s->quiet_frames;
  words[7] = s->last_rendered_gate;
  words[8] = s->retrigger_low_pending;
}
#endif

void plaits_render(void* state, uint32_t model, int32_t pitch_q7,
                   uint16_t timbre_q15, uint16_t morph_q15,
                   uint16_t harmonics_q15, uint16_t aux_mix_q15,
                   int16_t* out, uint32_t n) {
  if (!state || !out) return;
  /* Keep each source render within the adapter's bounded FIFO window while
   * honoring a larger caller block. Splitting here makes a 64-frame request
   * exactly equivalent to two existing 32-frame calls. */
#if FM1_PLAITS_CAPI_PREPARE_LARGE_CALL
  if (n > 32u) {
    PlaitsState* s = static_cast<PlaitsState*>(state);
    UpdateControls(s, model, pitch_q7, timbre_q15, morph_q15, harmonics_q15);
    plaits::Patch patch;
#if FM1_PLAITS_CACHE_FLOAT_CONTROLS
    patch.note = s->note;
    patch.harmonics = s->harmonics;
    patch.timbre = s->timbre;
    patch.morph = s->morph;
#else
    patch.note = PitchQ7(s->pitch_q7);
    patch.harmonics = UnitQ15(s->harmonics_q15);
    patch.timbre = UnitQ15(s->timbre_q15);
    patch.morph = UnitQ15(s->morph_q15);
#endif
    patch.frequency_modulation_amount = 0.0f;
    patch.timbre_modulation_amount = 0.0f;
    patch.morph_modulation_amount = 0.0f;
    patch.engine = static_cast<int>(s->model);
    patch.decay = 0.5f;
    patch.lpg_colour = 0.0f;
    plaits::Modulations mod;
    mod.engine = 0.0f;
    mod.note = 0.0f;
    mod.frequency = 0.0f;
    mod.harmonics = 0.0f;
    mod.timbre = 0.0f;
    mod.morph = 0.0f;
    mod.trigger = 0.0f;
    mod.level = 0.0f;
    mod.frequency_patched = false;
    mod.timbre_patched = false;
    mod.morph_patched = false;
    mod.trigger_patched = true;
    mod.level_patched = true;
    uint32_t offset = 0u;
    while (offset < n) {
      uint32_t chunk = n - offset;
      if (chunk > 32u) chunk = 32u;
      RenderPreparedChunk(s, patch, mod, aux_mix_q15, out + offset, chunk);
      offset += chunk;
    }
    return;
  }
#endif
  if (n > 32u) {
    uint32_t offset = 0u;
    while (offset < n) {
      uint32_t chunk = n - offset;
      if (chunk > 32u) chunk = 32u;
      plaits_render(state, model, pitch_q7, timbre_q15, morph_q15,
                    harmonics_q15, aux_mix_q15, out + offset, chunk);
      offset += chunk;
    }
    return;
  }
  PlaitsState* s = static_cast<PlaitsState*>(state);
  UpdateControls(s, model, pitch_q7, timbre_q15, morph_q15, harmonics_q15);
  plaits::Patch patch;
#if FM1_PLAITS_CACHE_FLOAT_CONTROLS
  patch.note = s->note;
  patch.harmonics = s->harmonics;
  patch.timbre = s->timbre;
  patch.morph = s->morph;
#else
  patch.note = PitchQ7(s->pitch_q7);
  patch.harmonics = UnitQ15(s->harmonics_q15);
  patch.timbre = UnitQ15(s->timbre_q15);
  patch.morph = UnitQ15(s->morph_q15);
#endif
  patch.frequency_modulation_amount = 0.0f;
  patch.timbre_modulation_amount = 0.0f;
  patch.morph_modulation_amount = 0.0f;
  patch.engine = static_cast<int>(s->model);
  patch.decay = 0.5f;
  patch.lpg_colour = 0.0f;
  plaits::Modulations mod;
  mod.engine = 0.0f;
  mod.note = 0.0f;
  mod.frequency = 0.0f;
  mod.harmonics = 0.0f;
  mod.timbre = 0.0f;
  mod.morph = 0.0f;
  mod.trigger = 0.0f;
  mod.level = 0.0f;
  mod.frequency_patched = false;
  mod.timbre_patched = false;
  mod.morph_patched = false;
  mod.trigger_patched = true;
  mod.level_patched = true;
  if (aux_mix_q15 > 32767u) aux_mix_q15 = 32767u;
  const uint32_t out_gain = 32767u - aux_mix_q15;
  uint32_t index = 0u;
  uint32_t phase_remainder = s->phase_q16;
#if FM1_PLAITS_SRC_LAGRANGE4 && FM1_PLAITS_SRC_ROLLING_CACHE && \
    !FM1_PLAITS_SOURCE_RATE_44100 && !FM1_PLAITS_NATIVE_RATE
  SourceCubicCache cubic_cache = {};
#endif
  // Fill exactly far enough for this output block before entering its sample
  // loop. The 44.1 kHz experiment is unity-rate and needs no interpolation
  // lookahead; 32 kHz advances by 320/441 and 48 kHz by 160/147 source frames
  // per output sample.
  if (n) {
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_NATIVE_RATE
    const uint32_t required = n;
#else
    const uint32_t transitions = n - 1u;
    const uint32_t required = 2u +
        (phase_remainder + kPhaseStepNumerator * transitions) /
            kPhaseDenominator;
#endif
    while (s->fifo_count < required) {
      const uint32_t previous_count = s->fifo_count;
      const uint32_t frames = RenderFramesForModel(s->model);
      AppendFrames(s, patch, mod, frames);
      if (s->fifo_count == previous_count) break;
    }
  }
#if FM1_PLAITS_NATIVE_RATE && FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH
  /* The FIFO is filled above; native output consumes one source frame per
   * output frame. Move the bounds check outside the common sample loop. */
  const uint32_t available = s->fifo_count < n ? s->fifo_count : n;
  for (uint32_t i = 0; i < available; ++i) {
    int32_t sample;
    if (!aux_mix_q15) {
      sample = s->fifo[i].out;
    } else {
      const int32_t weighted =
          static_cast<int32_t>(s->fifo[i].out) * out_gain +
          static_cast<int32_t>(s->fifo[i].aux) * aux_mix_q15;
      sample = MixAuxQ15(weighted);
    }
#if !FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    out[i] = static_cast<int16_t>(sample);
  }
  for (uint32_t i = available; i < n; ++i) out[i] = 0;
  index = available;
#else
  for (uint32_t i = 0; i < n; ++i) {
    if (s->fifo_count <= index) { out[i] = 0; continue; }
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_NATIVE_RATE
    /* Native source and output rates coincide, so the FIFO sample is already
     * at the requested time. Avoid the interpolator and its lookahead read. */
    int32_t sample;
    if (!aux_mix_q15) {
      sample = s->fifo[index].out;
    } else {
      const int32_t weighted =
          static_cast<int32_t>(s->fifo[index].out) * out_gain +
          static_cast<int32_t>(s->fifo[index].aux) * aux_mix_q15;
      sample = MixAuxQ15(weighted);
    }
#if !FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    out[i] = static_cast<int16_t>(sample);
    ++index;
#else
    if (s->fifo_count < index + 2u) { out[i] = 0; continue; }
#if FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION
    const float t = static_cast<float>(phase_remainder) *
        (1.0f / static_cast<float>(kPhaseDenominator));
#else
#if FM1_PLAITS_PHASE_LUT && !FM1_PLAITS_NATIVE_RATE && \
    (PLAITS_INTERNAL_SAMPLE_RATE == 48000 || \
     PLAITS_INTERNAL_SAMPLE_RATE == 38400)
    const uint32_t frac = kPhaseFracQ16[phase_remainder];
#else
    const uint32_t frac = (phase_remainder << 16) / kPhaseDenominator;
#endif
#endif
#if !FM1_PLAITS_SRC_LAGRANGE4 || !FM1_PLAITS_SRC_ROLLING_CACHE
    int32_t mixed_a, mixed_b;
    if (!aux_mix_q15) {
      /* The general weighted path below divides each mixed endpoint by 32767.
       * At zero AUX that is exactly the original int16 OUT value. */
      mixed_a = s->fifo[index].out;
      mixed_b = s->fifo[index + 1u].out;
    } else {
      const int32_t a = static_cast<int32_t>(s->fifo[index].out) * out_gain +
          static_cast<int32_t>(s->fifo[index].aux) * aux_mix_q15;
      const int32_t b = static_cast<int32_t>(s->fifo[index + 1u].out) * out_gain +
          static_cast<int32_t>(s->fifo[index + 1u].aux) * aux_mix_q15;
      mixed_a = MixAuxQ15(a);
      mixed_b = MixAuxQ15(b);
    }
#endif
#if FM1_PLAITS_SRC_LAGRANGE4
#if !FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION
    const float t = static_cast<float>(frac) * (1.0f / 65536.0f);
#endif
#if FM1_PLAITS_SRC_ROLLING_CACHE
    PrepareSourceCubic(&cubic_cache, s, index, out_gain, aux_mix_q15);
    int32_t sample = static_cast<int32_t>(RenderSourceCubic(cubic_cache, t));
#else
    const plaits::Voice::Frame& prev2 = index >= 2u ? s->fifo[index - 2u] :
        (index == 1u ? s->history[1] : s->history[0]);
    const plaits::Voice::Frame& prev1 = index >= 1u ? s->fifo[index - 1u] :
        s->history[1];
    const int32_t mixed_prev2 = MixAuxFrame(prev2, out_gain, aux_mix_q15);
    const int32_t mixed_prev1 = MixAuxFrame(prev1, out_gain, aux_mix_q15);
    const int32_t mixed_cur = mixed_a;
    const int32_t mixed_next = mixed_b;
    int32_t sample = static_cast<int32_t>(lagrange4_one_frame(
        static_cast<float>(mixed_prev2), static_cast<float>(mixed_prev1),
        static_cast<float>(mixed_cur), static_cast<float>(mixed_next), t));
#endif
#else
    const int32_t delta = mixed_b - mixed_a;
    /* |delta| <= 65535 and frac <= 65535, so this unsigned product fits in
     * 32 bits. The negative branch reproduces arithmetic >> 16 (floor), not
     * C's truncation-toward-zero division. */
    const uint32_t magnitude = static_cast<uint32_t>(delta < 0 ? -delta : delta);
    const uint32_t product = magnitude * frac;
    const int32_t step = delta < 0 ?
        -static_cast<int32_t>((product + 0xffffu) >> 16) :
         static_cast<int32_t>(product >> 16);
    int32_t sample = mixed_a + step;
#endif
#if FM1_PLAITS_SRC_LAGRANGE4
    /* Cubic interpolation has negative outer weights and can exceed int16.
     * Clamp before narrowing to prevent wraparound. */
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#elif !FM1_PLAITS_SKIP_BOUNDED_OUTPUT_CLAMP
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    out[i] = static_cast<int16_t>(sample);
    index += kPhaseIndexStep;
    phase_remainder += kPhaseRemainderStep;
    if (phase_remainder >= kPhaseDenominator) {
      phase_remainder -= kPhaseDenominator;
      ++index;
    }
#endif
  }
#endif
  const uint32_t consumed = index;
  if (consumed && consumed <= s->fifo_count) {
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_NATIVE_RATE
    if (consumed >= 2u) {
      s->history[0] = s->fifo[consumed - 2u];
      s->history[1] = s->fifo[consumed - 1u];
    } else {
      s->history[0] = s->history[1];
      s->history[1] = s->fifo[0];
    }
#endif
    if (consumed < s->fifo_count) {
      memmove(s->fifo, s->fifo + consumed,
              (s->fifo_count - consumed) * sizeof(s->fifo[0]));
    }
    s->fifo_count -= consumed;
  }
  s->phase_q16 = phase_remainder;
  if (!s->gate) {
    s->quiet_frames += n;
    if (s->quiet_frames > kQuietFrames) s->quiet_frames = kQuietFrames;
  } else {
    s->quiet_frames = 0u;
  }
}

}  // extern "C"
