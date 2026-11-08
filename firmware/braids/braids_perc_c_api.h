/* SPDX-License-Identifier: MIT
 * Compact C ABI for original BRAIDS percussion algorithms. */
#ifndef FELUCCA_BRAIDS_PERC_C_API_H_
#define FELUCCA_BRAIDS_PERC_C_API_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BRAIDS_PERC_STATE_BYTES 1024u
#define BRAIDS_PERC_BLOCK_MAX 32u
enum {
  BRAIDS_PERC_KICK = 0,
  BRAIDS_PERC_HAT = 1,
  BRAIDS_PERC_SNARE = 2,
  BRAIDS_PERC_BELL = 3,
  BRAIDS_PERC_DRUM = 4,
  BRAIDS_PERC_CLAP = 5
};
void braids_perc_init(void *state, uint32_t seed);
void braids_perc_note_on(void *state, uint32_t model, int32_t pitch_q7,
                         uint16_t timbre_q15, uint16_t color_q15,
                         uint16_t velocity_q15);
/* Same state transition with an explicit seed and one state clear. */
void braids_perc_note_on_seed(void *state, uint32_t seed, uint32_t model,
                              int32_t pitch_q7, uint16_t timbre_q15,
                              uint16_t color_q15, uint16_t velocity_q15);
void braids_perc_controls(void *state, uint16_t timbre_q15, uint16_t color_q15);
void braids_perc_render(void *state, int16_t *out, uint32_t n);
void braids_perc_choke(void *state);
uint8_t braids_perc_active(const void *state);
uint8_t braids_perc_initialized(const void *state);
#ifdef __cplusplus
}
#endif
#endif
