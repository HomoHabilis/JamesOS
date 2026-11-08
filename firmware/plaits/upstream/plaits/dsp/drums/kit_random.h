/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PLAITS_DSP_DRUMS_KIT_RANDOM_H_
#define PLAITS_DSP_DRUMS_KIT_RANDOM_H_

#include "plaits_config.h"
#include "stmlib/utils/random.h"

namespace plaits {

inline uint32_t KitRandomWord(uint32_t* state) {
  *state = *state * 1664525L + 1013904223L;
  return *state;
}

template <bool local_state>
inline float KitRandomFloat(uint32_t* state);

template <>
inline float KitRandomFloat<true>(uint32_t* state) {
  return static_cast<float>(KitRandomWord(state)) / 4294967296.0f;
}

template <>
inline float KitRandomFloat<false>(uint32_t*) {
  return stmlib::Random::GetFloat();
}

}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_KIT_RANDOM_H_
