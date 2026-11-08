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
// Fast 16-bit pseudo random number generator.

#ifndef STMLIB_UTILS_RANDOM_H_
#define STMLIB_UTILS_RANDOM_H_

#include "stmlib/stmlib.h"

namespace stmlib {

/* Keep one software RNG stream per PI32V2 core. The AC79 SDK's matching WL82
 * cpu.h defines current_cpu_id() with this exact `cnum` read (SDK commit
 * 5fa84a9, include_lib/driver/cpu/wl82/asm/cpu.h). Host tests can select a
 * simulated core without changing target code generation. */
static inline uint32_t RandomCoreId() {
#if defined(STMLIB_RANDOM_TEST_CORE_ID)
  extern uint32_t RandomTestCoreId();
  return RandomTestCoreId() & 1u;
#elif defined(FM1_DEVICE)
  uint32_t id;
  asm volatile("%0 = cnum" : "=r"(id) ::);
  return id;
#else
  return 0u;
#endif
}

class Random {
 public:
  static inline uint32_t state() { return rng_state_[RandomCoreId()]; }

  static inline void Seed(uint32_t seed) {
    rng_state_[RandomCoreId()] = seed;
  }

  static inline uint32_t GetWord() {
    const uint32_t core = RandomCoreId();
    rng_state_[core] = rng_state_[core] * 1664525L + 1013904223L;
    return rng_state_[core];
  }
  
  static inline int16_t GetSample() {
    return static_cast<int16_t>(GetWord() >> 16);
  }

  static inline float GetFloat() {
    return static_cast<float>(GetWord()) / 4294967296.0f;
  }

 private:
  static uint32_t rng_state_[2];

  DISALLOW_COPY_AND_ASSIGN(Random);
};

}  // namespace stmlib

#endif  // STMLIB_UTILS_RANDOM_H_
