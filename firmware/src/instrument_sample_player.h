/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_SAMPLE_PLAYER_H
#define BR_INSTRUMENT_SAMPLE_PLAYER_H

#include <stdint.h>
#include "drum_sample_bank.h"

/* One resident, sequentially decoded IMA voice. The decoder state is private
 * to the logical lane and never writes to the const XIP sample bank. */
typedef struct {
    const br_drum_sample_desc_t *sample;
    uint32_t frame;
    uint32_t end_frame;
    uint32_t fade_start_frame;
    uint32_t step_q16;
    uint32_t decoded_block;
    uint32_t decoded_frame;
    uint32_t previous_block;
    int32_t predictor;
    int32_t previous_sample;
    int32_t previous_block_last;
    int32_t lowpass;
    uint16_t fraction;
    uint8_t index;
    uint8_t block_ready;
    uint8_t previous_block_valid;
    uint8_t active;
    uint8_t codec;
    uint8_t tune_index;
    uint8_t decay;
    uint8_t fade_started;
} br_drum_sample_player_t;

/* tune is semitones, clamped to the implemented -36..+36 range. Decay 127
 * plays the complete source asset; shorter values select a shorter window. */
int br_drum_sample_player_start(br_drum_sample_player_t *player,
                                const br_drum_sample_desc_t *sample,
                                int32_t tune, uint32_t decay);
/* Apply changed pitch/decay controls without rewinding the decoder. A length
 * increase is accepted only before the closing fade starts. */
void br_drum_sample_player_update_controls(br_drum_sample_player_t *player,
                                           int32_t tune, uint32_t decay);
void br_drum_sample_player_reset(br_drum_sample_player_t *player);
/* Return 1 and write the next interpolated mono sample, or return 0 at end. */
int br_drum_sample_player_next(br_drum_sample_player_t *player,
                               int32_t *sample);

#endif /* BR_INSTRUMENT_SAMPLE_PLAYER_H */
