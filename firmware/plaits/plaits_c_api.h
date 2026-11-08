/* SPDX-License-Identifier: MIT
 * Integer-control ABI for the upstream Plaits float DSP.
 */
#ifndef DINGHY_PLAITS_C_API_H_
#define DINGHY_PLAITS_C_API_H_

#include <stdint.h>
#include "upstream/plaits_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLAITS_MODEL_COUNT 24u
/* Reserve one extra 64-byte FIFO extension for the opt-in 24-frame source
 * renderer while keeping the default 48-frame FIFO behavior unchanged. The
 * optional float-control cache adds four cached values to each Plaits state. */
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_DEVICE_CANDIDATE) && FM1_PATTERN_DEVICE_CANDIDATE
#define PLAITS_STATE_BYTES 23040u
#if FM1_PLAITS_CACHE_FLOAT_CONTROLS
#define PLAITS_STATE_ABI_SYMBOL plaits_state_abi_23040_cached
#else
#define PLAITS_STATE_ABI_SYMBOL plaits_state_abi_23040_uncached
#endif
#elif FM1_PLAITS_CACHE_FLOAT_CONTROLS
#define PLAITS_STATE_BYTES 23576u
#define PLAITS_STATE_ABI_SYMBOL plaits_state_abi_23576
#else
#define PLAITS_STATE_BYTES 23560u
#define PLAITS_STATE_ABI_SYMBOL plaits_state_abi_23560
#endif
#define PLAITS_STATE_ALIGN 8u
/* Call once during engine setup. The symbol name follows the C API state
 * layout so mismatched C/C++ configuration flags fail at link time. */
#define PLAITS_STATE_ABI_GUARD() PLAITS_STATE_ABI_SYMBOL()
void PLAITS_STATE_ABI_SYMBOL(void);

void plaits_state_init(void *state, uint32_t seed);
/* Optionally allocate and initialize the selected engine before the first
 * note render. The default build leaves this as a no-op. */
void plaits_prepare_model(void *state, uint32_t model);
void plaits_note_on(void *state, uint32_t model, int32_t pitch_q7,
                    uint16_t timbre_q15, uint16_t morph_q15,
                    uint16_t harmonics_q15, uint32_t velocity_0_127);
/* The outer audio layer has already matched the original MIDI key; 128u is
 * the force-release sentinel for this monophonic Plaits state. */
void plaits_note_off(void *state, uint32_t raw_note);
void plaits_all_off(void *state);
uint32_t plaits_quiet(const void *state);
/* Read-only predispatch cost-class query; does not mutate the voice. */
int32_t plaits_active_engine(const void *state);
/* Writes all n frames. Larger requests are split into FIFO-safe chunks. */
void plaits_render(void *state, uint32_t model, int32_t pitch_q7,
                   uint16_t timbre_q15, uint16_t morph_q15,
                   uint16_t harmonics_q15, uint16_t aux_mix_q15,
                   int16_t *out, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif
