/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_SNAPSHOT_H
#define FM1_PATTERN_SNAPSHOT_H
#include "instrument.h"

/* In-memory snapshot only: this layout is not a persistent project format. */
typedef struct {
    uint32_t step[BR_PARTS][BR_STEPS];
    uint32_t length[BR_PARTS], division[BR_PARTS];
    uint16_t synth_length[BR_SYNTH_PARTS][BR_STEPS];
    uint8_t synth_condition[BR_SYNTH_PARTS][BR_STEPS];
    uint8_t synth_note[BR_SYNTH_PARTS][BR_STEPS];
    uint8_t synth_velocity[BR_SYNTH_PARTS][BR_STEPS];
    int8_t synth_micro[BR_SYNTH_PARTS][BR_STEPS];
    uint16_t kit_length[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_condition[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_velocity[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_retrig[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_note[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_pitch[BR_STEPS][BR_DRUM_LANES];
    int8_t kit_micro[BR_STEPS][BR_DRUM_LANES];
} br_pattern_snapshot;

/* Both operations require exclusive access to all sequencer fields, with the
 * audio reader quiesced and foreground editing stopped. Neither operation is
 * atomic, ISR safe, a playback publication mechanism, nor a flash operation.
 * Returns 0 on success, -1 for a null argument. */
int br_pattern_snapshot_capture(br_pattern_snapshot *snapshot);
int br_pattern_snapshot_apply(const br_pattern_snapshot *snapshot);
#endif
