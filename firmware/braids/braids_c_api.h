/* SPDX-License-Identifier: MIT
 * C ABI for the vendored Mutable Instruments BRAIDS DSP. */
#ifndef FELUCCA_BRAIDS_C_API_H_
#define FELUCCA_BRAIDS_C_API_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MacroOscillator owns the original fixed delay-line workspace. */
#define BRAIDS_STATE_BYTES 17408u
#define BRAIDS_MODEL_COUNT 48u

void braids_state_init(void *state, uint32_t seed);
void braids_state_ensure(void *state, uint32_t seed);
void braids_note_on(void *state, uint32_t model, int32_t pitch_q7,
                    uint16_t timbre_q15, uint16_t color_q15);
void braids_render(void *state, uint32_t model, int32_t pitch_q7,
                   uint16_t timbre_q15, uint16_t color_q15,
                   int16_t *out, uint32_t n);
/* Models with existing independently rendered components (1..12).
 * mix=64 preserves their native blend; endpoints favour either component or
 * fundamental/detuned group. Unsupported models ignore mix. */
uint32_t braids_has_osc_mix(uint32_t model);
void braids_render_mix(void *state, uint32_t model, int32_t pitch_q7,
                      uint16_t timbre_q15, uint16_t color_q15,
                      int16_t *out, uint32_t n, uint32_t mix);

#ifdef __cplusplus
}
#endif

#endif
