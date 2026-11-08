/* SPDX-License-Identifier: MIT
 * Uses the original Plaits BassDrumEngine, SnareDrumEngine, and HiHatEngine.
 * Copyright for upstream DSP remains with Emilie Gillet under MIT.
 */
#include "plaits_kit_c_api.h"
#include "../hal/fm1_audio_rate.h"
#include <stddef.h>
#include <new>
#include <string.h>
#include "../hal/fm1_drum_profile.h"
#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "stmlib/utils/random.h"
#include "stmlib/utils/buffer_allocator.h"

/* Keep these independently switchable while measuring the wrapper changes.
 * Experimental paths remain off pending repeatable hardware timing benefit. */
#ifndef BR_PLAITS_KIT_GAIN3_FOLD
#define BR_PLAITS_KIT_GAIN3_FOLD 0
#endif
#ifndef BR_PLAITS_KIT_GAIN_RECIPROCAL
#define BR_PLAITS_KIT_GAIN_RECIPROCAL 1
#endif
#ifndef BR_PLAITS_KIT_FLOAT_OUTPUT_SCALE
#define BR_PLAITS_KIT_FLOAT_OUTPUT_SCALE 1
#endif
#ifndef BR_PLAITS_KIT_SAFE_PREFIX
#define BR_PLAITS_KIT_SAFE_PREFIX 1
#endif
#ifndef BR_PLAITS_KIT_NATIVE48_CHUNK48
#define BR_PLAITS_KIT_NATIVE48_CHUNK48 0
#endif
#ifndef BR_PLAITS_KIT_LOCAL_PHASE_CURSOR
#define BR_PLAITS_KIT_LOCAL_PHASE_CURSOR 1
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
#ifndef FM1_PLAITS_SRC_LAGRANGE4
#define FM1_PLAITS_SRC_LAGRANGE4 1
#endif
#if FM1_PLAITS_SRC_LAGRANGE4 && \
    !(FM1_PLAITS_SOURCE_RATE_38400 || FM1_PLAITS_SOURCE_RATE_32K || \
      FM1_PLAITS_SOURCE_RATE_48000)
#error "FM1_PLAITS_SRC_LAGRANGE4 is only for lower-rate source paths"
#endif
#if FM1_NATIVE_OUTPUT_32K && !FM1_PLAITS_SOURCE_RATE_32K
#error "Native 32 kHz output requires the Plaits kit 32 kHz source path"
#endif
#define FM1_PLAITS_KIT_NATIVE_RATE \
    ((FM1_NATIVE_OUTPUT_32K && FM1_PLAITS_SOURCE_RATE_32K) || \
     (FM1_NATIVE_OUTPUT_48K && FM1_PLAITS_SOURCE_RATE_48000))

namespace {
static const uint32_t kLanes = BR_DRUM_LANES;
static const uint32_t kBassVoices = 1u;
static const uint32_t kSnareVoices = 2u;
static const uint32_t kHatVoices = 2u;
/* Two source-frame history values replace two FIFO slots when cubic SRC is
 * enabled; the source-render chunks still have ample append headroom. */
static const uint32_t kFifo =
    FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_KIT_NATIVE_RATE ? 62u : 64u;
static const uint32_t kOutRate = FM1_AUDIO_RATE_HZ;
static const uint32_t kRenderLimit = 24u;
#if defined(TEST)
static uint32_t g_test_output_chunk_limit;
static uint32_t g_test_render_impl_calls;
#endif
#if (FM1_PLAITS_SOURCE_RATE_32K + FM1_PLAITS_SOURCE_RATE_44100 + \
     FM1_PLAITS_SOURCE_RATE_38400 + FM1_PLAITS_SOURCE_RATE_48000) > 1
#error "Select at most one non-default Plaits source-rate experiment"
#endif
#if FM1_PLAITS_SOURCE_RATE_44100 && \
    (FM1_NATIVE_OUTPUT_32K || FM1_NATIVE_OUTPUT_48K)
#error "The 44.1 kHz Plaits kit source selector requires the 44.1 kHz output profile"
#endif
#if FM1_NATIVE_OUTPUT_48K && !FM1_PLAITS_SOURCE_RATE_48000
#error "Native 48 kHz output requires the Plaits kit 48 kHz source path"
#endif
#if FM1_PLAITS_SOURCE_RATE_44100
#if PLAITS_INTERNAL_SAMPLE_RATE != 44100
#error "FM1_PLAITS_SOURCE_RATE_44100 requires all Plaits units to build at 44.1 kHz"
#endif
static const uint32_t kRatioNum = 1u;
static const uint32_t kRatioDen = 1u;
static const uint32_t kPhaseShift = 0u;
static const uint32_t kClapWaitFrames = PLAITS_INTERNAL_SAMPLE_RATE / 100u;
#elif FM1_PLAITS_SOURCE_RATE_32K
#if PLAITS_INTERNAL_SAMPLE_RATE != 32000
#error "FM1_PLAITS_SOURCE_RATE_32K requires all Plaits units to build at 32 kHz"
#endif
#if FM1_NATIVE_OUTPUT_32K
static const uint32_t kRatioNum = 1u;
static const uint32_t kRatioDen = 1u;
static const uint32_t kPhaseShift = 0u;
#else
static const uint32_t kRatioNum = 320u;
static const uint32_t kRatioDen = 441u;
static const uint32_t kPhaseShift = 9u;
#endif
static const uint32_t kClapWaitFrames = PLAITS_INTERNAL_SAMPLE_RATE / 100u;
#elif FM1_PLAITS_SOURCE_RATE_38400
#if PLAITS_INTERNAL_SAMPLE_RATE != 38400
#error "FM1_PLAITS_SOURCE_RATE_38400 requires all Plaits units to build at 38.4 kHz"
#endif
static const uint32_t kRatioNum = 128u;
static const uint32_t kRatioDen = 147u;
static const uint32_t kPhaseShift = 8u;
static const uint32_t kClapWaitFrames = PLAITS_INTERNAL_SAMPLE_RATE / 100u;
#elif FM1_PLAITS_SOURCE_RATE_48000
#if PLAITS_INTERNAL_SAMPLE_RATE != 48000
#error "FM1_PLAITS_SOURCE_RATE_48000 requires all Plaits units to build at 48 kHz"
#endif
#if FM1_NATIVE_OUTPUT_48K
static const uint32_t kRatioNum = 1u;
static const uint32_t kRatioDen = 1u;
static const uint32_t kPhaseShift = 0u;
#else
static const uint32_t kRatioNum = 160u;
static const uint32_t kRatioDen = 147u;
static const uint32_t kPhaseShift = 8u;
#endif
static const uint32_t kClapWaitFrames = PLAITS_INTERNAL_SAMPLE_RATE / 100u;
#else
#if PLAITS_INTERNAL_SAMPLE_RATE != 48000
#error "Non-48 kHz Plaits builds must explicitly enable their source-rate selector"
#endif
static const uint32_t kRatioNum = 160u;
static const uint32_t kRatioDen = 147u;
static const uint32_t kPhaseShift = 8u;
static const uint32_t kClapWaitFrames = PLAITS_INTERNAL_SAMPLE_RATE / 100u;
#endif
static const uint32_t kPhaseMask = (1u << kPhaseShift) - 1u;
static const uint32_t kPhaseIndexStep = kRatioNum / kRatioDen;
static const uint32_t kPhaseRemainderStep = kRatioNum % kRatioDen;
static_assert(plaits::kSampleRateHz == PLAITS_INTERNAL_SAMPLE_RATE,
              "Plaits kit wrapper and upstream translation units use different rates");
#if (PLAITS_INTERNAL_SAMPLE_RATE == 48000 && !FM1_PLAITS_KIT_NATIVE_RATE) || \
    PLAITS_INTERNAL_SAMPLE_RATE == 38400
#if defined(FM1_DEVICE)
static const float kPhaseFrac[kRatioDen]
    __attribute__((section(".data.fm1_plaits_phase"))) = {
#else
static const float kPhaseFrac[kRatioDen] = {
#endif
    0.0000000000000000f, 0.0067901611328125f, 0.0135955810546875f, 0.0204010009765625f, 0.0272064208984375f, 0.0340118408203125f, 0.0408020019531250f, 0.0476074218750000f, 0.0544128417968750f, 0.0612182617187500f, 0.0680236816406250f, 0.0748291015625000f, 0.0816192626953125f, 0.0884246826171875f, 0.0952301025390625f, 0.1020355224609375f, 0.1088409423828125f, 0.1156311035156250f, 0.1224365234375000f, 0.1292419433593750f, 0.1360473632812500f, 0.1428527832031250f, 0.1496582031250000f, 0.1564483642578125f, 0.1632537841796875f, 0.1700592041015625f, 0.1768646240234375f, 0.1836700439453125f, 0.1904754638671875f, 0.1972656250000000f, 0.2040710449218750f, 0.2108764648437500f, 0.2176818847656250f, 0.2244873046875000f, 0.2312774658203125f, 0.2380828857421875f, 0.2448883056640625f, 0.2516937255859375f, 0.2584991455078125f, 0.2653045654296875f, 0.2720947265625000f, 0.2789001464843750f, 0.2857055664062500f, 0.2925109863281250f, 0.2993164062500000f, 0.3061218261718750f, 0.3129119873046875f, 0.3197174072265625f, 0.3265228271484375f, 0.3333282470703125f, 0.3401336669921875f, 0.3469238281250000f, 0.3537292480468750f, 0.3605346679687500f, 0.3673400878906250f, 0.3741455078125000f, 0.3809509277343750f, 0.3877410888671875f, 0.3945465087890625f, 0.4013519287109375f, 0.4081573486328125f, 0.4149627685546875f, 0.4217681884765625f, 0.4285583496093750f, 0.4353637695312500f, 0.4421691894531250f, 0.4489746093750000f, 0.4557800292968750f, 0.4625701904296875f, 0.4693756103515625f, 0.4761810302734375f, 0.4829864501953125f, 0.4897918701171875f, 0.4965972900390625f, 0.5033874511718750f, 0.5101928710937500f, 0.5169982910156250f, 0.5238037109375000f, 0.5306091308593750f, 0.5374145507812500f, 0.5442047119140625f, 0.5510101318359375f, 0.5578155517578125f, 0.5646209716796875f, 0.5714263916015625f, 0.5782165527343750f, 0.5850219726562500f, 0.5918273925781250f, 0.5986328125000000f, 0.6054382324218750f, 0.6122436523437500f, 0.6190338134765625f, 0.6258392333984375f, 0.6326446533203125f, 0.6394500732421875f, 0.6462554931640625f, 0.6530609130859375f, 0.6598510742187500f, 0.6666564941406250f, 0.6734619140625000f, 0.6802673339843750f, 0.6870727539062500f, 0.6938629150390625f, 0.7006683349609375f, 0.7074737548828125f, 0.7142791748046875f, 0.7210845947265625f, 0.7278900146484375f, 0.7346801757812500f, 0.7414855957031250f, 0.7482910156250000f, 0.7550964355468750f, 0.7619018554687500f, 0.7687072753906250f, 0.7754974365234375f, 0.7823028564453125f, 0.7891082763671875f, 0.7959136962890625f, 0.8027191162109375f, 0.8095092773437500f, 0.8163146972656250f, 0.8231201171875000f, 0.8299255371093750f, 0.8367309570312500f, 0.8435363769531250f, 0.8503265380859375f, 0.8571319580078125f, 0.8639373779296875f, 0.8707427978515625f, 0.8775482177734375f, 0.8843536376953125f, 0.8911437988281250f, 0.8979492187500000f, 0.9047546386718750f, 0.9115600585937500f, 0.9183654785156250f, 0.9251556396484375f, 0.9319610595703125f, 0.9387664794921875f, 0.9455718994140625f, 0.9523773193359375f, 0.9591827392578125f, 0.9659729003906250f, 0.9727783203125000f, 0.9795837402343750f, 0.9863891601562500f, 0.9931945800781250f
};
#endif
static const uint32_t kSilenceFrames = 512u;
static inline float phase_fraction(uint32_t remainder) {
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
  (void)remainder;
  return 0.0f;
#elif PLAITS_INTERNAL_SAMPLE_RATE == 48000 || PLAITS_INTERNAL_SAMPLE_RATE == 38400
  return kPhaseFrac[remainder];
#else
  return static_cast<float>(remainder) * (1.0f / kRatioDen);
#endif
}
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_KIT_NATIVE_RATE
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
#if FM1_PLAITS_SRC_LAGRANGE4 && FM1_PLAITS_SRC_ROLLING_CACHE && \
    !FM1_PLAITS_SOURCE_RATE_44100 && !FM1_PLAITS_KIT_NATIVE_RATE
struct SourceCubicCache {
  uint32_t valid;
  uint32_t index;
  float point[4];
  float c1, c2, c3;
};
#define BR_PLAITS_SRC_CUBIC_CACHE_PARAM , SourceCubicCache *cache
#define BR_PLAITS_SRC_CUBIC_CACHE_ARG(cache_ptr) , (cache_ptr)
#else
#define BR_PLAITS_SRC_CUBIC_CACHE_PARAM
#define BR_PLAITS_SRC_CUBIC_CACHE_ARG(cache_ptr)
#endif
struct Lane {
  uint32_t voice_id, active, pending, note, velocity, gate_edge;
  uint8_t pending_voice_id, voice_override;
  int8_t pitch_delta;
  uint32_t phase, fifo_count, age, silent;
  uint32_t clap_wait, clap_remaining;
  uint32_t choke_remaining, choked;
  uint32_t gate_frames, gate_fade;
  uint32_t random_state;
  int32_t tail_sample, last_output;
  uint32_t tail_fade;
#if FM1_PLAITS_KIT_CACHE_NOTE_FREQUENCY
  float cached_note;
  float cached_note_frequency;
  uint32_t note_frequency_valid;
#endif
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_KIT_NATIVE_RATE
  /* Previous source samples support one-frame-delayed cubic resampling. */
  float history[2];
#endif
  float fifo[kFifo];
};
struct KitState {
  plaits::BassDrumEngine bass[kBassVoices];
  plaits::SnareDrumEngine snare[kSnareVoices];
  plaits::HiHatEngine hat[kHatVoices];
  float scratch[plaits::kMaxBlockSize * 2u];
  float out[plaits::kMaxBlockSize];
  float aux[plaits::kMaxBlockSize];
  Lane lane[kLanes];
  uint32_t seed;
  uint32_t last_velocity;
};
static_assert(sizeof(KitState) <= PLAITS_KIT_STATE_BYTES, "increase Plaits kit state cap");
static_assert((PLAITS_KIT_STATE_BYTES & 7u) == 0u, "state cap alignment");
static_assert(alignof(KitState) <= PLAITS_KIT_STATE_ALIGN, "increase alignment cap");

static uint32_t output_chunk_limit() {
#if defined(TEST)
  if (g_test_output_chunk_limit) return g_test_output_chunk_limit;
#endif
#if BR_PLAITS_KIT_NATIVE48_CHUNK48 && FM1_PLAITS_KIT_NATIVE_RATE
  return BR_BLOCK < 48u ? BR_BLOCK : 48u;
#else
  return BR_BLOCK < 32u ? BR_BLOCK : 32u;
#endif
}

#if FM1_PLAITS_SRC_LAGRANGE4 && FM1_PLAITS_SRC_ROLLING_CACHE && \
    !FM1_PLAITS_SOURCE_RATE_44100 && !FM1_PLAITS_KIT_NATIVE_RATE
static inline float RenderCachedSourceCubic(Lane *l, uint32_t index,
                                            float t,
                                            SourceCubicCache *cache) {
  if (!cache->valid || cache->index != index) {
    if (cache->valid && index == cache->index + 1u) {
      cache->point[0] = cache->point[1];
      cache->point[1] = cache->point[2];
      cache->point[2] = cache->point[3];
      cache->point[3] = l->fifo[index + 1u];
    } else {
      cache->point[0] = index >= 2u ? l->fifo[index - 2u] :
          (index == 1u ? l->history[1] : l->history[0]);
      cache->point[1] = index >= 1u ? l->fifo[index - 1u] : l->history[1];
      cache->point[2] = l->fifo[index];
      cache->point[3] = l->fifo[index + 1u];
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
  return cache->point[1] + t *
      (cache->c1 + t * (cache->c2 + t * cache->c3));
}
#endif

static uint32_t clamp127(uint32_t x) { return x > 127u ? 127u : x; }
static int32_t clamp_pitch_delta(int32_t x) {
  return x < -24 ? -24 : x > 24 ? 24 : x;
}
static float unit(uint32_t x) { return static_cast<float>(clamp127(x)) * (1.0f / 127.0f); }
static uint32_t source_sample_count(uint32_t home_gate_frames,
                                    uint32_t gate_frames,
                                    uint32_t gate_fade, uint32_t age,
                                    uint32_t n) {
  const uint32_t lifetime_frames =
      home_gate_frames && gate_frames
          ? (home_gate_frames < gate_frames ? home_gate_frames : gate_frames)
          : (home_gate_frames ? home_gate_frames : gate_frames);
  uint32_t source_samples = n;
  if (gate_fade) {
    if (source_samples > gate_fade) source_samples = gate_fade;
  } else if (lifetime_frames && source_samples) {
    const uint32_t until_fade = age < lifetime_frames
        ? lifetime_frames - age : 0u;
    if (source_samples > until_fade)
      source_samples = source_samples < until_fade + 64u
          ? source_samples : until_fade + 64u;
  }
  return source_samples;
}
static uint32_t cost_signature_word(uint32_t hash, uint32_t word) {
  hash ^= word;
  return hash * 16777619u;
}
static uint32_t source_cost_signature(
    const KitState *s, const br_sound_t *sound,
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask) {
  uint32_t hash = 2166136261u;
  hash = cost_signature_word(hash, sound->drum_voice_mask);
  hash = cost_signature_word(hash, (uint32_t)sound->tune);
  hash = cost_signature_word(hash, sound->timbre);
  hash = cost_signature_word(hash, sound->color);
  hash = cost_signature_word(hash, sound->plaits_harmonics);
  hash = cost_signature_word(hash, sound->decay);
  hash = cost_signature_word(hash, sound->drum_snap);
  for (uint32_t lane = 0; lane < kLanes; ++lane) {
    const Lane *l = &s->lane[lane];
    if (!(lane_mask & (1u << lane))) continue;
    hash = cost_signature_word(hash, lane);
    hash = cost_signature_word(hash, l->pending &&
        l->pending_voice_id < BR_DRUM_VOICE_COUNT ? l->pending_voice_id :
        br_drum_voice_id(sound->drum_voice_mask, lane));
    hash = cost_signature_word(hash, l->active);
    hash = cost_signature_word(hash, l->note);
    hash = cost_signature_word(hash, l->velocity);
    hash = cost_signature_word(hash, l->gate_frames);
    hash = cost_signature_word(hash, l->gate_fade != 0u);
    hash = cost_signature_word(hash, l->choked);
    hash = cost_signature_word(hash, l->choke_remaining != 0u);
    hash = cost_signature_word(hash,
        lane_gate_frames ? lane_gate_frames[lane] : 0u);
  }
  return hash ? hash : 1u;
}

/* Exact truncating division for the signed lane-gain product. Since 127 =
 * 128 - 1, x = 127*a + (a + b) when x = 128*a + b. In this renderer the
 * converted sample is bounded by +/-65534 and lane_gain by 127, so three
 * folds reduce the absolute product (at most 8,322,818) below 254 (the
 * recurrence bound is 131); the final quotient is one comparison. The legacy
 * four-fold path remains
 * selectable for direct attribution. */
static int32_t scale_sample_by_lane_gain(int32_t sample, uint32_t lane_gain) {
  const int32_t product = sample * static_cast<int32_t>(lane_gain);
  const bool negative = product < 0;
  uint32_t magnitude = negative ?
      0u - static_cast<uint32_t>(product) : static_cast<uint32_t>(product);
#if BR_PLAITS_KIT_GAIN_RECIPROCAL
  /* ceil(2^32 / 127) is 33,818,641. For this adapter's bounded product
   * (|sample| <= 65,534 and lane_gain <= 127), the high word of this product
   * is the exact floor(magnitude / 127). The reciprocal error is only 111
   * units over 2^32, far below the smallest distance to the next quotient. */
  const uint32_t quotient = static_cast<uint32_t>(
      (static_cast<uint64_t>(magnitude) * 33818641u) >> 32);
#else
  uint32_t quotient = 0u;
  uint32_t part = magnitude >> 7;
  quotient += part;
  magnitude = part + (magnitude & 127u);
  part = magnitude >> 7;
  quotient += part;
  magnitude = part + (magnitude & 127u);
  part = magnitude >> 7;
  quotient += part;
  magnitude = part + (magnitude & 127u);
#if !BR_PLAITS_KIT_GAIN3_FOLD
  part = magnitude >> 7;
  quotient += part;
  magnitude = part + (magnitude & 127u);
#endif
  quotient += static_cast<uint32_t>(magnitude >= 127u);
#endif
  const int32_t signed_quotient = static_cast<int32_t>(quotient);
  return negative ? -signed_quotient : signed_quotient;
}
#if BR_PLAITS_KIT_FLOAT_OUTPUT_SCALE || \
    defined(BR_PLAITS_KIT_DEADLINE_TEST)
static float lane_gain_output_scale(uint32_t lane_gain) {
  return 32767.0f * (static_cast<float>(lane_gain) * (1.0f / 127.0f));
}
static int32_t scale_float_output_by_lane_gain(float value,
                                               float output_scale) {
  return static_cast<int32_t>(value * output_scale);
}
#endif
static const uint32_t kCanonicalNotes[kLanes] = {36u, 38u, 46u, 49u};
static uint32_t engine_role(uint32_t voice_id) {
  return voice_id == BR_DRUM_KCK1 || voice_id == BR_DRUM_KCK2 ||
             voice_id == BR_DRUM_KCK3 ? 0u :
         voice_id == BR_DRUM_SNAR || voice_id == BR_DRUM_CLAP ||
             voice_id == BR_DRUM_PERC || voice_id == BR_DRUM_RIM ||
             voice_id == BR_DRUM_TOM ? 1u : 2u;
}
static uint32_t role_slot(uint32_t lane, uint32_t voice_id) {
  if (lane == 3u) return 1u;
  (void)voice_id;
  return 0u;
}
static plaits::Engine *engine(KitState *s, uint32_t lane, uint32_t voice_id) {
  const uint32_t r = engine_role(voice_id);
  const uint32_t slot = role_slot(lane, voice_id);
  return r == 0u ? static_cast<plaits::Engine*>(&s->bass[0]) :
         r == 1u ? static_cast<plaits::Engine*>(&s->snare[slot]) :
                   static_cast<plaits::Engine*>(&s->hat[slot]);
}
static void start_tail_fade(Lane *l) {
  if (l->active && l->last_output) {
    l->tail_sample = l->last_output;
    l->tail_fade = 64u;
  }
  l->last_output = 0;
}
static void emit_tail_fade(Lane *l, uint32_t lane, uint32_t key_lane,
                           int32_t *out, int32_t *key, uint32_t n) {
  for (uint32_t i = 0; i < n && l->tail_fade; ++i) {
    const int32_t sample = static_cast<int32_t>(
        static_cast<int64_t>(l->tail_sample) * l->tail_fade / 64u);
    out[i] += sample;
    if (key && lane == key_lane) key[i] += sample;
    --l->tail_fade;
  }
  if (!l->tail_fade) l->tail_sample = 0;
}
static void init_one_engine(KitState *s, plaits::Engine *e) {
  stmlib::BufferAllocator allocator(s->scratch, sizeof(s->scratch));
  e->Init(&allocator);
}
static void trigger_lane(KitState *s, uint32_t lane, uint32_t voice_id,
                         uint32_t velocity) {
  Lane *l = &s->lane[lane];
  /* Every logical role owns exactly one resident engine slot. Reinitialize
   * that slot on retrigger/choice changes, never leaving an alternate tail. */
  init_one_engine(s, engine(s, lane, voice_id));
  l->voice_id = voice_id;
  l->voice_override = l->pending_voice_id < BR_DRUM_VOICE_COUNT;
  l->pending_voice_id = 255u;
  l->active = 1u;
  l->pending = 0u;
  l->last_output = 0;
  l->choked = 0u;
  l->choke_remaining = 0u;
  l->gate_frames = 0u;
  l->gate_fade = 0u;
  l->velocity = clamp127(velocity);
  if (!l->velocity) l->velocity = 1u;
  l->gate_edge = 1u;
  l->age = l->silent = 0u;
  l->clap_remaining = voice_id == BR_DRUM_CLAP ? 3u : 0u;
  l->clap_wait = kClapWaitFrames; /* additional strikes remain 10 ms apart */
  l->fifo_count = 0u;
  l->phase = 0u;
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_KIT_NATIVE_RATE
  l->history[0] = l->history[1] = 0.0f;
#endif
}
static void render_source(KitState *s, uint32_t lane, const br_sound_t *sound,
                         uint32_t count, float *dst) {
  Lane *l = &s->lane[lane];
  plaits::EngineParameters p = {};
  const int32_t note_offset = l->voice_id == BR_DRUM_KCK2 ? 12 :
      l->voice_id == BR_DRUM_KCK3 ? -5 :
      l->voice_id == BR_DRUM_PERC ? 7 :
      l->voice_id == BR_DRUM_RIM ? 12 :
      l->voice_id == BR_DRUM_PEDL ? 12 :
      l->voice_id == BR_DRUM_CLOS ? -4 :
      l->voice_id == BR_DRUM_CYMB ? -12 :
      l->voice_id == BR_DRUM_TOM ? -12 : 0;
  p.note = static_cast<float>(l->note) +
      static_cast<float>(sound->tune + l->pitch_delta + note_offset);
#if FM1_PLAITS_KIT_CACHE_NOTE_FREQUENCY
  if (!l->note_frequency_valid || l->cached_note != p.note) {
    l->cached_note = p.note;
    l->cached_note_frequency = plaits::NoteToFrequency(p.note);
    l->note_frequency_valid = 1u;
  }
  p.note_frequency = l->cached_note_frequency;
  p.note_frequency_cached = true;
#endif
  /* HARMONICS offsets the existing decay/snap contour. Its saved default of
     64 preserves the prior kit voicing to within control quantization. */
  p.timbre = unit(sound->timbre);
  p.morph = unit(sound->color);
  p.harmonics = unit(sound->decay) * 0.70f + unit(sound->drum_snap) * 0.30f +
      (unit(sound->plaits_harmonics) - unit(64u)) * 0.70f;
  if (p.harmonics < 0.0f) p.harmonics = 0.0f;
  if (p.harmonics > 1.0f) p.harmonics = 1.0f;
  p.accent = unit(l->velocity);
  uint32_t written = 0u;
  while (written < count) {
    uint32_t chunk = count - written;
    uint32_t edge = l->gate_edge;
    uint32_t wait_internal = l->clap_wait;
    if (chunk > kRenderLimit) chunk = kRenderLimit;
    if (!edge && l->clap_remaining) {
      if (!wait_internal) {
        edge = 1u;
        l->clap_wait = kClapWaitFrames;
        --l->clap_remaining;
      }
      else if (chunk > wait_internal) chunk = wait_internal;
    }
    p.trigger = edge ? plaits::TRIGGER_RISING_EDGE : plaits::TRIGGER_LOW;
    bool already_enveloped = true;
    const uint32_t old_random = stmlib::Random::state();
    stmlib::Random::Seed(l->random_state);
#if BR_PLAITS_KIT_KICK_SYNTH_RATE2
    if (l->voice_id == BR_DRUM_KCK1 || l->voice_id == BR_DRUM_KCK2 ||
        l->voice_id == BR_DRUM_KCK3) {
      s->bass[0].RenderKitRate2(p, s->out, s->aux, chunk,
                                &already_enveloped);
    } else {
      engine(s, lane, l->voice_id)->Render(p, s->out, s->aux, chunk,
                                           &already_enveloped);
    }
#else
    engine(s, lane, l->voice_id)->Render(p, s->out, s->aux, chunk,
                                         &already_enveloped);
#endif
    l->random_state = stmlib::Random::state();
    stmlib::Random::Seed(old_random);
    l->gate_edge = 0u;
    /* Voice stays fixed during this source chunk. Preserve the original
     * floating-point coefficients and operation order for each output. */
    float main_mix, aux_mix;
    if (l->voice_id == BR_DRUM_CLAP) { main_mix = 0.72f; aux_mix = 0.28f; }
    else if (l->voice_id == BR_DRUM_RIM) { main_mix = 0.50f; aux_mix = 0.50f; }
    else if (l->voice_id == BR_DRUM_SNAR || l->voice_id == BR_DRUM_PERC) { main_mix = 0.82f; aux_mix = 0.18f; }
    else if (l->voice_id == BR_DRUM_TOM) { main_mix = 0.92f; aux_mix = 0.08f; }
    else if (l->voice_id == BR_DRUM_PEDL) { main_mix = 0.84f; aux_mix = 0.16f; }
    else if (l->voice_id == BR_DRUM_CYMB) { main_mix = 0.28f; aux_mix = 0.72f; }
    else { main_mix = 0.65f; aux_mix = 0.35f; }
    for (uint32_t i = 0; i < chunk; ++i) {
      float v = main_mix * s->out[i] + aux_mix * s->aux[i];
      if (v > 2.0f) v = 2.0f;
      if (v < -2.0f) v = -2.0f;
      dst[written + i] = v;
    }
    written += chunk;
    if (l->clap_remaining)
      l->clap_wait = l->clap_wait > chunk ? l->clap_wait - chunk : 0u;
    else l->clap_wait = 0u;
  }
}
static uint32_t append_fifo(KitState *s, uint32_t lane,
                            const br_sound_t *sound) {
  Lane *l = &s->lane[lane];
  uint32_t need = l->fifo_count + kRenderLimit;
  uint32_t internal = kRenderLimit;
  if (need > kFifo) return 0u;
  /* Each append renders 24 internal frames; the wrapper may need several
     appends to cover one output chunk at the selected source/output ratio. */
  render_source(s, lane, sound, internal, l->fifo + l->fifo_count);
  l->fifo_count += internal;
  return 1u;
}
static uint32_t fill_fifo(KitState *s, uint32_t lane, const br_sound_t *sound,
                          uint32_t required) {
  Lane *l = &s->lane[lane];
  uint32_t append_units = 0u;
  if (required > kFifo) return 0u;
  while (l->fifo_count < required) {
    const uint32_t old_count = l->fifo_count;
    append_units += append_fifo(s, lane, sound);
    if (l->fifo_count == old_count) return append_units;
  }
  return append_units;
}
static float next_sample(KitState *s, uint32_t lane
                         BR_PLAITS_SRC_CUBIC_CACHE_PARAM) {
  Lane *l = &s->lane[lane];
  /* Keep the source index and ratio remainder packed between blocks. */
  uint32_t index = l->phase >> kPhaseShift;
  uint32_t remainder = l->phase & kPhaseMask;
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
  if (index >= l->fifo_count) return 0.0f;
  const float value = l->fifo[index++];
  l->phase = index << kPhaseShift;
  return value;
#else
  if (index + 1u >= l->fifo_count) return 0.0f;
  const float frac = phase_fraction(remainder);
#if FM1_PLAITS_SRC_LAGRANGE4
#if !FM1_PLAITS_SRC_ROLLING_CACHE
  const float p0 = index >= 2u ? l->fifo[index - 2u] :
      (index == 1u ? l->history[1] : l->history[0]);
  const float p1 = index >= 1u ? l->fifo[index - 1u] : l->history[1];
#endif
#if FM1_PLAITS_SRC_ROLLING_CACHE
  const float value = RenderCachedSourceCubic(l, index, frac, cache);
#else
  const float value = lagrange4_one_frame(p0, p1, l->fifo[index],
                                           l->fifo[index + 1u], frac);
#endif
#else
  float a = l->fifo[index], b = l->fifo[index + 1u];
  float value = a + (b - a) * frac;
#endif
  index += kPhaseIndexStep;
  remainder += kPhaseRemainderStep;
  if (remainder >= kRatioDen) {
    remainder -= kRatioDen;
    ++index;
  }
  l->phase = (index << kPhaseShift) | remainder;
  return value;
#endif
}
#if BR_PLAITS_KIT_LOCAL_PHASE_CURSOR
/* Same recurrence as next_sample(), but keep phase components in caller-local
 * scalars for the hot render loop and commit the packed phase once per chunk. */
static inline float next_sample_cursor(Lane *l, uint32_t *index,
                                       uint32_t *remainder
#if FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH && FM1_PLAITS_KIT_NATIVE_RATE
                                       , uint32_t fifo_count_snapshot
#endif
                                       BR_PLAITS_SRC_CUBIC_CACHE_PARAM) {
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
  (void)remainder;
#if FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH && FM1_PLAITS_KIT_NATIVE_RATE
  if (*index >= fifo_count_snapshot) return 0.0f;
#else
  if (*index >= l->fifo_count) return 0.0f;
#endif
  return l->fifo[(*index)++];
#else
  if (*index + 1u >= l->fifo_count) return 0.0f;
  const float frac = phase_fraction(*remainder);
#if FM1_PLAITS_SRC_LAGRANGE4
#if !FM1_PLAITS_SRC_ROLLING_CACHE
  const float p0 = *index >= 2u ? l->fifo[*index - 2u] :
      (*index == 1u ? l->history[1] : l->history[0]);
  const float p1 = *index >= 1u ? l->fifo[*index - 1u] : l->history[1];
#endif
#if FM1_PLAITS_SRC_ROLLING_CACHE
  const float value = RenderCachedSourceCubic(l, *index, frac, cache);
#else
  const float value = lagrange4_one_frame(p0, p1, l->fifo[*index],
                                           l->fifo[*index + 1u], frac);
#endif
#else
  const float a = l->fifo[*index];
  const float b = l->fifo[*index + 1u];
  const float value = a + (b - a) * frac;
#endif
  *index += kPhaseIndexStep;
  *remainder += kPhaseRemainderStep;
  if (*remainder >= kRatioDen) {
    *remainder -= kRatioDen;
    ++*index;
  }
  return value;
#endif
}
#endif
static void consume_fifo(Lane *l) {
  uint32_t consumed = l->phase >> kPhaseShift;
  if (consumed && consumed <= l->fifo_count) {
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_KIT_NATIVE_RATE
    if (consumed >= 2u) {
      l->history[0] = l->fifo[consumed - 2u];
      l->history[1] = l->fifo[consumed - 1u];
    } else {
      l->history[0] = l->history[1];
      l->history[1] = l->fifo[0];
    }
#endif
    if (consumed < l->fifo_count)
      memmove(l->fifo, l->fifo + consumed, (l->fifo_count - consumed) * sizeof(float));
    l->fifo_count -= consumed;
    l->phase &= kPhaseMask;
  }
}
}  // namespace

extern "C" {
static uint32_t render_keyed_impl(void *state, const br_sound_t *sound,
                                  const uint32_t lane_level[BR_DRUM_LANES],
                                  const uint32_t lane_gate_frames[BR_DRUM_LANES],
                                  uint32_t lane_mask, int32_t *out,
                                  int32_t *key, uint32_t n, bool saturate,
                                  uint8_t append_units_by_lane[BR_DRUM_LANES]);
static uint32_t render_keyed_request(
    void *state, const br_sound_t *sound,
    const uint32_t lane_level[BR_DRUM_LANES],
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    int32_t *out, int32_t *key, uint32_t n, bool saturate,
    uint8_t append_units_by_lane[BR_DRUM_LANES]);

__attribute__((minsize, noinline)) void plaits_kit_init(void *state, uint32_t seed) {
  if (!state) return;
  memset(state, 0, sizeof(KitState));
  KitState *s = new (state) KitState;
  s->seed = seed ? seed : 1u;
  init_one_engine(s, &s->bass[0]);
  init_one_engine(s, &s->snare[0]);
  init_one_engine(s, &s->snare[1]);
  init_one_engine(s, &s->hat[0]);
  init_one_engine(s, &s->hat[1]);
  for (uint32_t i = 0; i < kLanes; ++i) {
    s->lane[i].note = kCanonicalNotes[i];
    s->lane[i].random_state = s->seed + 0x9e3779b9u * (i + 1u);
  }
}
__attribute__((minsize, noinline)) void plaits_kit_init_pair(void *state_a, void *state_b, uint32_t seed) {
  if (!state_a || !state_b || state_a == state_b) return;
  const uint32_t random_before = stmlib::Random::state();
  plaits_kit_init(state_a, seed);
  const uint32_t random_after_one_init = stmlib::Random::state();
  stmlib::Random::Seed(random_before);
  plaits_kit_init(state_b, seed);
  stmlib::Random::Seed(random_after_one_init);
}
__attribute__((minsize, noinline)) void plaits_kit_init_quad(void *states[4], uint32_t seed) {
  if (!states) return;
  for (uint32_t i = 0; i < 4u; ++i) {
    if (!states[i]) return;
    for (uint32_t j = 0; j < i; ++j) {
      if (states[i] == states[j]) return;
    }
  }

  /* Initialize the first kit from the caller's RNG stream, then replay the
   * same initialization stream for each independent masked state. Leave the
   * process RNG where one ordinary kit initialization would leave it. */
  const uint32_t random_before = stmlib::Random::state();
  plaits_kit_init(states[0], seed);
  const uint32_t random_after_one_init = stmlib::Random::state();
  for (uint32_t i = 1; i < 4u; ++i) {
    stmlib::Random::Seed(random_before);
    plaits_kit_init(states[i], seed);
  }
  stmlib::Random::Seed(random_after_one_init);
}
void plaits_kit_note_on(void *state, uint32_t raw_note, uint32_t velocity) {
  plaits_kit_note_on_mask(state, raw_note, velocity, BR_DRUM_ALL_MASK);
}
void plaits_kit_note_on_mask(void *state, uint32_t raw_note, uint32_t velocity,
                             uint32_t lane_mask) {
  plaits_kit_note_on_voice_mask(state, raw_note, velocity, lane_mask, 255u);
}
void plaits_kit_note_on_voice_mask(void *state, uint32_t raw_note,
                                   uint32_t velocity, uint32_t lane_mask,
                                   uint32_t voice_id) {
  plaits_kit_note_on_voice_pitch_mask(state, raw_note, velocity, lane_mask,
                                      voice_id, 0);
}
void plaits_kit_note_on_voice_pitch_mask(void *state, uint32_t raw_note,
                                         uint32_t velocity,
                                         uint32_t lane_mask,
                                         uint32_t voice_id,
                                         int32_t pitch_delta) {
  if (!state) return;
  KitState *s = static_cast<KitState*>(state);
  uint32_t lane = br_drum_lane(raw_note);
  if (lane >= kLanes || !(lane_mask & (1u << lane))) return;
  if (!velocity) return;
  if (voice_id >= BR_DRUM_VOICE_COUNT || br_drum_voice_lane(voice_id) != lane)
    voice_id = 255u;
  s->lane[lane].note = kCanonicalNotes[lane];
  s->lane[lane].velocity = clamp127(velocity);
  s->lane[lane].pitch_delta =
      static_cast<int8_t>(clamp_pitch_delta(pitch_delta));
  s->lane[lane].pending_voice_id = (uint8_t)voice_id;
  start_tail_fade(&s->lane[lane]);
  s->lane[lane].pending = 1u;
  s->lane[lane].active = 0u;
  s->lane[lane].fifo_count = 0u;
  s->lane[lane].clap_remaining = 0u;
}
void plaits_kit_note_off(void *state, uint32_t raw_note) {
  plaits_kit_note_off_mask(state, raw_note, BR_DRUM_ALL_MASK);
}
void plaits_kit_note_off_mask(void *state, uint32_t raw_note,
                              uint32_t lane_mask) {
  if (!state) return;
  KitState *s = static_cast<KitState*>(state);
  if (raw_note >= 128u) { plaits_kit_all_off_mask(state, lane_mask); return; }
  uint32_t lane = br_drum_lane(raw_note);
  if (lane < kLanes && (lane_mask & (1u << lane))) {
    if (s->lane[lane].pending) {
      s->lane[lane].pending = 0u;
      s->lane[lane].pending_voice_id = 255u;
    }
    s->lane[lane].gate_edge = 0u;
  }
}
void plaits_kit_all_off(void *state) {
  plaits_kit_all_off_mask(state, BR_DRUM_ALL_MASK);
}
void plaits_kit_all_off_mask(void *state, uint32_t lane_mask) {
  if (!state) return;
  KitState *s = static_cast<KitState*>(state);
  for (uint32_t i = 0; i < kLanes; ++i) {
    if (!(lane_mask & (1u << i))) continue;
    s->lane[i].active = 0u;
    s->lane[i].pending = 0u;
    s->lane[i].pending_voice_id = 255u;
    s->lane[i].voice_override = 0u;
    s->lane[i].pitch_delta = 0;
    s->lane[i].tail_sample = 0;
    s->lane[i].tail_fade = 0u;
    s->lane[i].last_output = 0;
    s->lane[i].clap_remaining = 0u;
    s->lane[i].choke_remaining = 0u;
    s->lane[i].choked = 0u;
    s->lane[i].fifo_count = 0u;
#if FM1_PLAITS_SRC_LAGRANGE4 && !FM1_PLAITS_KIT_NATIVE_RATE
    s->lane[i].history[0] = s->lane[i].history[1] = 0.0f;
#endif
    s->lane[i].silent = kSilenceFrames;
  }
}
void plaits_kit_render(void *state, const br_sound_t *sound,
                       const uint32_t lane_level[BR_DRUM_LANES], int32_t *out, uint32_t n) {
  plaits_kit_render_gated(state, sound, lane_level, 0, out, n);
}
void plaits_kit_render_gated(void *state, const br_sound_t *sound,
                             const uint32_t lane_level[BR_DRUM_LANES],
                             const uint32_t lane_gate_frames[BR_DRUM_LANES],
                             int32_t *out, uint32_t n) {
  plaits_kit_render_keyed(state, sound, lane_level, lane_gate_frames, out, nullptr, n);
}
void plaits_kit_render_keyed(void *state, const br_sound_t *sound,
                             const uint32_t lane_level[BR_DRUM_LANES],
                             const uint32_t lane_gate_frames[BR_DRUM_LANES],
                             int32_t *out, int32_t *key, uint32_t n) {
  render_keyed_request(state, sound, lane_level, lane_gate_frames,
                       BR_DRUM_ALL_MASK, out, key, n, true, nullptr);
}
void plaits_kit_render_keyed_mask(void *state, const br_sound_t *sound,
                                  const uint32_t lane_level[BR_DRUM_LANES],
                                  const uint32_t lane_gate_frames[BR_DRUM_LANES],
                                  uint32_t lane_mask, int32_t *out,
                                  int32_t *key, uint32_t n) {
  (void)render_keyed_request(state, sound, lane_level, lane_gate_frames,
                             lane_mask, out, key, n, false, nullptr);
}
uint32_t plaits_kit_render_keyed_mask_counted(
    void *state, const br_sound_t *sound,
    const uint32_t lane_level[BR_DRUM_LANES],
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    int32_t *out, int32_t *key, uint32_t n) {
  return render_keyed_request(state, sound, lane_level, lane_gate_frames,
                              lane_mask, out, key, n, false, nullptr);
}
uint32_t plaits_kit_render_keyed_mask_counted_by_lane(
    void *state, const br_sound_t *sound,
    const uint32_t lane_level[BR_DRUM_LANES],
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    int32_t *out, int32_t *key, uint32_t n,
    uint8_t append_units[BR_DRUM_LANES]) {
  return render_keyed_request(state, sound, lane_level, lane_gate_frames,
                              lane_mask, out, key, n, false, append_units);
}

uint32_t plaits_kit_predict_append_units(
    const void *state, const br_sound_t *sound,
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    uint32_t n) {
  return plaits_kit_predict_append_units_ex(state, sound, lane_gate_frames,
                                             lane_mask, n, nullptr, nullptr,
                                             nullptr);
}

uint32_t plaits_kit_predict_append_units_ex(
    const void *state, const br_sound_t *sound,
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    uint32_t n, uint32_t *nonproportional_mask, uint32_t *cost_signature,
    uint8_t units_by_lane[BR_DRUM_LANES]) {
  if (nonproportional_mask) *nonproportional_mask = 0u;
  if (cost_signature) *cost_signature = 0u;
  if (units_by_lane)
    for (uint32_t lane = 0; lane < kLanes; ++lane) units_by_lane[lane] = 0u;
  if (!state || !sound) return 0u;
  if (n > BR_BLOCK) n = BR_BLOCK;
  const uint32_t chunk_limit = output_chunk_limit();
  if (!chunk_limit || !n) return 0u;
  lane_mask &= BR_DRUM_ALL_MASK;
  const KitState *s = static_cast<const KitState*>(state);
  const uint32_t home_gate_frames = 0u;
  uint32_t total_units = 0u;
  uint32_t uncertain = 0u;
  for (uint32_t lane = 0; lane < kLanes; ++lane) {
    const Lane *l = &s->lane[lane];
    if (!(lane_mask & (1u << lane))) continue;
    const uint32_t selected_voice = l->pending &&
        l->pending_voice_id < BR_DRUM_VOICE_COUNT ? l->pending_voice_id :
        br_drum_voice_id(sound->drum_voice_mask, lane);
    const bool pending = l->pending != 0u;
    if (pending || (l->active && !l->voice_override && l->voice_id != selected_voice) ||
        (l->active && (selected_voice == BR_DRUM_CLAP || l->clap_remaining)))
      uncertain |= 1u << lane;
    if (!pending && (!l->active ||
        (!l->voice_override && l->voice_id != selected_voice))) continue;

    /* For the common one-call path, only the FIFO fill count is needed. The
     * full chunk simulation below remains for direct multi-chunk callers. */
    uint32_t fifo = pending ? 0u : l->fifo_count;
    uint32_t phase = pending ? 0u : l->phase;
    uint32_t age = pending ? 0u : l->age;
    uint32_t gate_fade = pending ? 0u : l->gate_fade;
    uint32_t gate_frames = pending ? 0u : l->gate_frames;
    bool active = true;  // Pending events become active in the first render chunk.
    if (lane_gate_frames && lane_gate_frames[lane] &&
        (!gate_frames || lane_gate_frames[lane] < gate_frames))
      gate_frames = lane_gate_frames[lane];
    uint32_t lane_units = 0u;
    if (n <= chunk_limit) {
      const uint32_t source_samples = source_sample_count(
          home_gate_frames, gate_frames, gate_fade, age, n);
      if (source_samples) {
        const uint32_t initial_index = phase >> kPhaseShift;
        const uint32_t initial_remainder = phase & kPhaseMask;
        const uint32_t last = source_samples - 1u;
        const uint32_t last_index = initial_index +
            (initial_remainder + last * kRatioNum) / kRatioDen;
        const uint32_t required = last_index +
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
            1u;
#else
            2u;
#endif
        if (required <= kFifo) {
          while (fifo < required && fifo + kRenderLimit <= kFifo) {
            fifo += kRenderLimit;
            ++lane_units;
          }
        }
      }
    } else for (uint32_t offset = 0u; offset < n && active;) {
      uint32_t chunk = n - offset;
      if (chunk > chunk_limit) chunk = chunk_limit;
      const uint32_t source_samples = source_sample_count(
          home_gate_frames, gate_frames, gate_fade, age, chunk);
      if (source_samples) {
        const uint32_t initial_index = phase >> kPhaseShift;
        const uint32_t initial_remainder = phase & kPhaseMask;
        const uint32_t last = source_samples - 1u;
        const uint32_t last_index = initial_index +
            (initial_remainder + last * kRatioNum) / kRatioDen;
        const uint32_t required = last_index +
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
            1u;
#else
            2u;
#endif
        if (required <= kFifo) {
          while (fifo < required && fifo + kRenderLimit <= kFifo) {
            fifo += kRenderLimit;
            ++lane_units;
          }
        }

        /* The render loop always advances age for each processed output
         * frame, but phase advances only while the source interpolation taps
         * are present in the FIFO. */
        const uint32_t age_before = age;
        uint32_t processed = source_samples;
        uint32_t phase_index = initial_index;
        uint32_t phase_remainder = initial_remainder;
        for (uint32_t i = 0u; i < processed; ++i) {
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
          if (phase_index >= fifo) break;
#else
          if (phase_index + 1u >= fifo) break;
#endif
          phase_index += kPhaseIndexStep;
          phase_remainder += kPhaseRemainderStep;
          if (phase_remainder >= kRatioDen) {
            phase_remainder -= kRatioDen;
            ++phase_index;
          }
        }
        phase = (phase_index << kPhaseShift) | phase_remainder;
        age += processed;

        if (gate_fade) {
          if (processed >= gate_fade) {
            gate_fade = 0u;
            active = false;
            fifo = 0u;
          } else {
            gate_fade -= processed;
          }
        } else if (gate_frames) {
          const uint32_t until_fade = age_before < gate_frames
              ? gate_frames - age_before : 0u;
          const uint32_t fade_samples = processed > until_fade
              ? processed - until_fade : 0u;
          if (!fade_samples) {
            /* The lifetime boundary was not reached in this chunk. */
          } else if (fade_samples >= 64u) {
            active = false;
            fifo = 0u;
          } else {
            gate_fade = 64u - fade_samples;
          }
        }
        if (active) {
          const uint32_t consumed = phase >> kPhaseShift;
          if (consumed && consumed <= fifo) {
            fifo -= consumed;
            phase &= kPhaseMask;
          }
        }
      }
      offset += chunk;
    }
    total_units += lane_units;
    if (units_by_lane) units_by_lane[lane] =
        (uint8_t)(lane_units > 255u ? 255u : lane_units);
  }
  if (nonproportional_mask) *nonproportional_mask = uncertain;
  if (cost_signature)
    *cost_signature = source_cost_signature(s, sound, lane_gate_frames, lane_mask);
  return total_units;
}

static uint32_t render_keyed_impl(void *state, const br_sound_t *sound,
                                 const uint32_t lane_level[BR_DRUM_LANES],
                                 const uint32_t lane_gate_frames[BR_DRUM_LANES],
                                 uint32_t lane_mask, int32_t *out,
                                 int32_t *key, uint32_t n, bool saturate,
                                 uint8_t append_units_by_lane[BR_DRUM_LANES]) {
#if defined(TEST)
  ++g_test_render_impl_calls;
#endif
  if (append_units_by_lane)
    for (uint32_t lane = 0; lane < kLanes; ++lane) append_units_by_lane[lane] = 0u;
  if (!state || !sound || !lane_level || !out) return 0u;
  if (n > BR_BLOCK) n = BR_BLOCK;
  lane_mask &= BR_DRUM_ALL_MASK;
  KitState *s = static_cast<KitState*>(state);
  const uint32_t home_gate_frames = 0u;
  const uint32_t key_lane = sound->drum_key_lane < BR_DRUM_LANES
      ? sound->drum_key_lane : 0u;
  uint32_t append_units = 0u;
  for (uint32_t i = 0; i < n; ++i) out[i] = 0;
  if (key) for (uint32_t i = 0; i < n; ++i) key[i] = 0;
  for (uint32_t lane = 0; lane < kLanes; ++lane) {
    if (!(lane_mask & (1u << lane))) continue;
    Lane *l = &s->lane[lane];
    const uint32_t selected_voice = l->pending &&
        l->pending_voice_id < BR_DRUM_VOICE_COUNT ? l->pending_voice_id :
        br_drum_voice_id(sound->drum_voice_mask, lane);
#if FM1_DRUM_PROFILE
    const uint32_t profile_voice = l->pending ? selected_voice :
        (l->voice_id < BR_DRUM_VOICE_COUNT ? l->voice_id : selected_voice);
    fm1_drum_profile_set_context(lane, profile_voice);
    const fm1_drum_profile_scope_t outer_scope =
        fm1_drum_profile_begin(FM1_DRUM_PROFILE_OUTER_RENDER, n);
#endif
    if (l->pending) {
#if FM1_DRUM_PROFILE
      const fm1_drum_profile_scope_t trigger_scope =
          fm1_drum_profile_begin(FM1_DRUM_PROFILE_TRIGGER_SETUP, 0u);
#endif
      trigger_lane(s, lane, selected_voice, l->velocity);
#if FM1_DRUM_PROFILE
      fm1_drum_profile_end(trigger_scope);
#endif
    } else if (l->active && !l->voice_override && l->voice_id != selected_voice) {
      /* Changing a lane choice discards the old voice. Its engine slot is
       * reinitialized on the next trigger. A 64-frame last-sample ramp avoids
       * a hard discontinuity without retaining a second engine tail. */
      start_tail_fade(l);
      l->active = 0u;
      l->fifo_count = 0u;
      l->clap_remaining = 0u;
      l->choke_remaining = 0u;
      l->choked = 0u;
    }
    if (!l->active) {
      emit_tail_fade(l, lane, key_lane, out, key, n);
#if FM1_DRUM_PROFILE
      fm1_drum_profile_append(0u, n);
      fm1_drum_profile_end(outer_scope);
#endif
      continue;
    }
    const uint32_t lane_gain = clamp127(lane_level[lane]);
#if BR_PLAITS_KIT_FLOAT_OUTPUT_SCALE
    const float output_scale = lane_gain_output_scale(lane_gain);
#endif
    if (lane_gate_frames && lane_gate_frames[lane] &&
        (!l->gate_frames || lane_gate_frames[lane] < l->gate_frames))
      l->gate_frames = lane_gate_frames[lane];
    const uint32_t lifetime_frames =
        home_gate_frames && l->gate_frames
            ? (home_gate_frames < l->gate_frames ? home_gate_frames : l->gate_frames)
            : (home_gate_frames ? home_gate_frames : l->gate_frames);
    const uint32_t source_samples = source_sample_count(
        home_gate_frames, l->gate_frames, l->gate_fade, l->age, n);
#if BR_PLAITS_KIT_LOCAL_PHASE_CURSOR
    uint32_t resample_index = l->phase >> kPhaseShift;
    uint32_t resample_remainder = l->phase & kPhaseMask;
#endif
#if FM1_PLAITS_SRC_LAGRANGE4 && FM1_PLAITS_SRC_ROLLING_CACHE && \
    !FM1_PLAITS_SOURCE_RATE_44100 && !FM1_PLAITS_KIT_NATIVE_RATE
    SourceCubicCache source_cubic_cache = {};
#endif
#if FM1_DRUM_PROFILE
    uint32_t lane_units = 0u;
#endif
    if (source_samples) {
      const uint32_t initial_index = l->phase >> kPhaseShift;
      const uint32_t initial_remainder = l->phase & kPhaseMask;
      const uint32_t last = source_samples - 1u;
      const uint32_t last_index = initial_index +
          (initial_remainder + last * kRatioNum) / kRatioDen;
#if FM1_DRUM_PROFILE
      lane_units = fill_fifo(s, lane, sound, last_index +
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
                             1u);
#else
                             2u);
#endif
#else
      const uint32_t lane_units = fill_fifo(s, lane, sound, last_index +
#if FM1_PLAITS_SOURCE_RATE_44100 || FM1_PLAITS_KIT_NATIVE_RATE
                                            1u);
#else
                                            2u);
#endif
#endif
      append_units += lane_units;
      if (append_units_by_lane)
        append_units_by_lane[lane] =
            (uint8_t)(lane_units > 255u ? 255u : lane_units);
    }
#if FM1_DRUM_PROFILE
    fm1_drum_profile_append(lane_units, n);
#endif
#if FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH && FM1_PLAITS_KIT_NATIVE_RATE
    /* Rendering below never mutates fifo_count; take one lane snapshot for
     * the per-frame cursor instead of reloading the field on every sample. */
    const uint32_t fifo_count_snapshot = l->fifo_count;
#endif
    float peak = 0.0f;
#if BR_PLAITS_KIT_SAFE_PREFIX
    /* Before the first gate-fade sample, an open, unchoked lane needs none of
     * the per-sample lifetime/fade/choke tests below. Stop strictly before
     * the lifetime boundary so that the original 64-frame fade setup and its
     * first (unity-gain) sample still execute in the reference loop. */
    uint32_t i = 0u;
    if (!l->gate_fade && !l->choked) {
      uint32_t safe_prefix = n;
      if (lifetime_frames) {
        safe_prefix = l->age < lifetime_frames
            ? lifetime_frames - l->age : 0u;
        if (safe_prefix > n) safe_prefix = n;
      }
      for (; i < safe_prefix; ++i) {
#if BR_PLAITS_KIT_LOCAL_PHASE_CURSOR
        const float value = next_sample_cursor(l, &resample_index,
            &resample_remainder
#if FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH && FM1_PLAITS_KIT_NATIVE_RATE
            , fifo_count_snapshot
#endif
            BR_PLAITS_SRC_CUBIC_CACHE_ARG(&source_cubic_cache));
#else
        const float value = next_sample(s, lane
            BR_PLAITS_SRC_CUBIC_CACHE_ARG(&source_cubic_cache));
#endif
        const float av = value < 0.0f ? -value : value;
        if (av > peak) peak = av;
#if BR_PLAITS_KIT_FLOAT_OUTPUT_SCALE
        const int32_t sample = scale_float_output_by_lane_gain(value, output_scale);
#else
        int32_t sample = static_cast<int32_t>(value * 32767.0f);
        sample = scale_sample_by_lane_gain(sample, lane_gain);
#endif
        out[i] += sample;
        if (key && lane == key_lane) key[i] += sample;
        l->last_output = sample;
        ++l->age;
      }
    }
#else
    uint32_t i = 0u;
#endif
    for (; i < n; ++i) {
      bool gate_fade_finished = false;
#if BR_PLAITS_KIT_LOCAL_PHASE_CURSOR
      float value = next_sample_cursor(l, &resample_index,
          &resample_remainder
#if FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH && FM1_PLAITS_KIT_NATIVE_RATE
          , fifo_count_snapshot
#endif
          BR_PLAITS_SRC_CUBIC_CACHE_ARG(&source_cubic_cache));
#else
      float value = next_sample(s, lane
          BR_PLAITS_SRC_CUBIC_CACHE_ARG(&source_cubic_cache));
#endif
      if (lifetime_frames && l->age >= lifetime_frames && !l->gate_fade)
        l->gate_fade = 64u;
      if (l->gate_fade) {
        value *= static_cast<float>(l->gate_fade) * (1.0f / 64.0f);
        --l->gate_fade;
        gate_fade_finished = l->gate_fade == 0u;
      }
      if (l->choked) {
        if (l->choke_remaining) {
          value *= static_cast<float>(l->choke_remaining) * (1.0f / 64.0f);
          --l->choke_remaining;
        } else value = 0.0f;
      }
      float av = value < 0.0f ? -value : value;
      if (av > peak) peak = av;
#if BR_PLAITS_KIT_FLOAT_OUTPUT_SCALE
      const int32_t sample = scale_float_output_by_lane_gain(value, output_scale);
#else
      int32_t sample = static_cast<int32_t>(value * 32767.0f);
      sample = scale_sample_by_lane_gain(sample, lane_gain);
#endif
      out[i] += sample;
      if (key && lane == key_lane) key[i] += sample;
      l->last_output = sample;
      ++l->age;
      /* If a live gate change arrives after its cutoff, this still emits
         the complete 64-frame fade before deactivating the lane. */
      if (gate_fade_finished) {
        l->active = 0u;
        l->fifo_count = 0u;
        l->clap_remaining = 0u;
        break;
      }
    }
#if BR_PLAITS_KIT_LOCAL_PHASE_CURSOR
    l->phase = (resample_index << kPhaseShift) | resample_remainder;
#endif
#if FM1_DRUM_PROFILE
    if (!l->active) {
      fm1_drum_profile_end(outer_scope);
      continue;
    }
#else
    if (!l->active) continue;
#endif
    consume_fifo(l);
    if (peak < 0.0002f) l->silent += n; else l->silent = 0u;
    /* Do not truncate a long natural tail at a fixed age. The existing quiet
     * window remains the retirement condition; gate and choke fades still
     * retire their lanes explicitly. */
    if (l->silent >= kSilenceFrames) {
      l->active = 0u;
      l->fifo_count = 0u;
      l->clap_remaining = 0u;
    }
    emit_tail_fade(l, lane, key_lane, out, key, n);
#if FM1_DRUM_PROFILE
    fm1_drum_profile_end(outer_scope);
#endif
  }
  if (saturate) for (uint32_t i = 0; i < n; ++i) {
    if (out[i] > 32767) out[i] = 32767;
    if (out[i] < -32768) out[i] = -32768;
  }
  return append_units;
}
static uint32_t render_keyed_request(
    void *state, const br_sound_t *sound,
    const uint32_t lane_level[BR_DRUM_LANES],
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    int32_t *out, int32_t *key, uint32_t n, bool saturate,
    uint8_t append_units_by_lane[BR_DRUM_LANES]) {
  const uint32_t chunk_limit = output_chunk_limit();
  if (!state || !sound || !lane_level || !out || !chunk_limit)
    return render_keyed_impl(state, sound, lane_level, lane_gate_frames,
                             lane_mask, out, key, n, saturate,
                             append_units_by_lane);
  if (n <= chunk_limit)
    return render_keyed_impl(state, sound, lane_level, lane_gate_frames,
                             lane_mask, out, key, n, saturate,
                             append_units_by_lane);

  if (append_units_by_lane)
    for (uint32_t lane = 0; lane < kLanes; ++lane)
      append_units_by_lane[lane] = 0u;
  uint32_t total_units = 0u;
  for (uint32_t offset = 0u; offset < n;) {
    uint32_t chunk = n - offset;
    if (chunk > chunk_limit) chunk = chunk_limit;
    uint8_t chunk_units[BR_DRUM_LANES] = {};
    total_units += render_keyed_impl(
        state, sound, lane_level, lane_gate_frames, lane_mask, out + offset,
        key ? key + offset : nullptr, chunk, saturate,
        append_units_by_lane ? chunk_units : nullptr);
    if (append_units_by_lane) {
      for (uint32_t lane = 0; lane < kLanes; ++lane) {
        const uint32_t sum = append_units_by_lane[lane] + chunk_units[lane];
        append_units_by_lane[lane] =
            static_cast<uint8_t>(sum > 255u ? 255u : sum);
      }
    }
    offset += chunk;
  }
  return total_units;
}
uint32_t plaits_kit_quiet(const void *state) {
  return plaits_kit_quiet_mask(state, BR_DRUM_ALL_MASK);
}
uint32_t plaits_kit_quiet_mask(const void *state, uint32_t lane_mask) {
  if (!state) return 1u;
  const KitState *s = static_cast<const KitState*>(state);
  for (uint32_t i = 0; i < kLanes; ++i)
    if ((lane_mask & (1u << i)) &&
        (s->lane[i].active || s->lane[i].pending || s->lane[i].tail_fade)) return 0u;
  return 1u;
}
uint32_t plaits_kit_lane_scheduler_state(const void *state,
                                         const br_sound_t *sound,
                                         uint32_t lane,
                                         uint32_t *engine_work,
                                         uint32_t *render_voice) {
  if (engine_work) *engine_work = 0u;
  if (render_voice) *render_voice = 0u;
  if (!state || !sound || lane >= kLanes) return 0u;
  const KitState *s = static_cast<const KitState*>(state);
  const Lane *l = &s->lane[lane];
  const uint32_t selected_voice = l->pending &&
      l->pending_voice_id < BR_DRUM_VOICE_COUNT ? l->pending_voice_id :
      br_drum_voice_id(sound->drum_voice_mask, lane);
  uint32_t does_engine_work = 0u;
  uint32_t voice = selected_voice;
  if (l->pending) {
    does_engine_work = 1u;
  } else if (l->active && l->voice_override) {
    does_engine_work = 1u;
    voice = l->voice_id;
  } else if (l->active && l->voice_id == selected_voice) {
    does_engine_work = 1u;
    voice = l->voice_id;
  }
  if (engine_work) *engine_work = does_engine_work;
  if (render_voice) *render_voice = voice;
  return 1u;
}
#if defined(TEST)
/* Host-only visibility for exact lane RNG/gate/FIFO/tail transition checks. */
void plaits_kit_test_metadata(const void* state, uint32_t words[82]) {
  if (!state || !words) return;
  const KitState* s = static_cast<const KitState*>(state);
  uint32_t* p = words;
  *p++ = s->seed;
  *p++ = s->last_velocity;
  for (uint32_t i = 0u; i < kLanes; ++i) {
    const Lane& l = s->lane[i];
    *p++ = l.voice_id;
    *p++ = l.active;
    *p++ = l.pending;
    *p++ = l.note;
    *p++ = l.velocity;
    *p++ = l.gate_edge;
    *p++ = l.phase;
    *p++ = l.fifo_count;
    *p++ = l.age;
    *p++ = l.silent;
    *p++ = l.clap_wait;
    *p++ = l.clap_remaining;
    *p++ = l.choke_remaining;
    *p++ = l.choked;
    *p++ = l.gate_frames;
    *p++ = l.gate_fade;
    *p++ = l.random_state;
    *p++ = static_cast<uint32_t>(l.tail_sample);
    *p++ = static_cast<uint32_t>(l.last_output);
    *p++ = l.tail_fade;
  }
}
/* Segmentation can leave a different stale source cursor in a non-rendering
 * lane after a fade or while a pending trigger is masked. It is ignored until
 * trigger_lane resets it. Compare all live state bytes and FIFO samples. */
int plaits_kit_test_state_equal_except_nonrendered_phase(
    const void* a, const void* b, uint32_t* phase_mask,
    uint32_t* mismatch_offset, uint32_t* active_phase_mask,
    uint32_t* pending_phase_mask) {
  if (phase_mask) *phase_mask = 0u;
  if (mismatch_offset) *mismatch_offset = 0xffffffffu;
  if (active_phase_mask) *active_phase_mask = 0u;
  if (pending_phase_mask) *pending_phase_mask = 0u;
  if (!a || !b) return 0;
  const KitState* lhs = static_cast<const KitState*>(a);
  const KitState* rhs = static_cast<const KitState*>(b);
  uint32_t ignored = 0u;
  uint32_t active_diff = 0u;
  uint32_t pending_diff = 0u;
  for (uint32_t lane = 0u; lane < kLanes; ++lane) {
    if (lhs->lane[lane].phase == rhs->lane[lane].phase) continue;
    if (lhs->lane[lane].active || rhs->lane[lane].active)
      active_diff |= 1u << lane;
    else if (lhs->lane[lane].pending || rhs->lane[lane].pending)
      pending_diff |= 1u << lane;
    ignored |= 1u << lane;
  }
  if (active_diff) return 0;
  const uint8_t* left = static_cast<const uint8_t*>(a);
  const uint8_t* right = static_cast<const uint8_t*>(b);
  for (size_t offset = 0u; offset < sizeof(KitState); ++offset) {
    bool is_ignored_phase_byte = false;
    bool is_unused_fifo_byte = false;
    for (uint32_t lane = 0u; lane < kLanes; ++lane) {
      const size_t lane_offset =
          reinterpret_cast<const uint8_t*>(&lhs->lane[lane]) - left;
      const size_t phase_offset = lane_offset + offsetof(Lane, phase);
      if ((ignored & (1u << lane)) && offset >= phase_offset &&
          offset < phase_offset + sizeof(uint32_t)) {
        is_ignored_phase_byte = true;
      }
      const size_t fifo_offset = lane_offset + offsetof(Lane, fifo);
      const size_t fifo_live_end = fifo_offset +
          static_cast<size_t>(lhs->lane[lane].fifo_count) * sizeof(float);
      const size_t fifo_capacity_end = fifo_offset + sizeof(lhs->lane[lane].fifo);
      if (offset >= fifo_live_end && offset < fifo_capacity_end) {
        is_unused_fifo_byte = true;
      }
    }
    if (!is_ignored_phase_byte && !is_unused_fifo_byte &&
        left[offset] != right[offset]) {
      if (mismatch_offset) *mismatch_offset = static_cast<uint32_t>(offset);
      return 0;
    }
  }
  if (phase_mask) *phase_mask = ignored;
  if (active_phase_mask) *active_phase_mask = active_diff;
  if (pending_phase_mask) *pending_phase_mask = pending_diff;
  return 1;
}
#endif
}

#if defined(BR_PLAITS_KIT_DEADLINE_TEST)
extern "C" int32_t plaits_kit_deadline_test_gain(int32_t sample,
                                                   uint32_t lane_gain) {
  return scale_sample_by_lane_gain(sample, lane_gain);
}
extern "C" void plaits_kit_deadline_test_set_choke(void *state,
                                                    uint32_t lane,
                                                    uint32_t remaining) {
  if (!state || lane >= kLanes) return;
  KitState *s = static_cast<KitState*>(state);
  s->lane[lane].choked = 1u;
  s->lane[lane].choke_remaining = remaining;
}
#endif
#if defined(TEST)
extern "C" void plaits_kit_test_set_output_chunk_limit(uint32_t limit) {
  if (!limit || limit == 32u || limit == 48u)
    g_test_output_chunk_limit = limit;
}
extern "C" void plaits_kit_test_reset_render_impl_calls(void) {
  g_test_render_impl_calls = 0u;
}
extern "C" uint32_t plaits_kit_test_render_impl_calls(void) {
  return g_test_render_impl_calls;
}
#endif
