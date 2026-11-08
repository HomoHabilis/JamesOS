/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_INSTRUMENT_KIT_SOURCE_CONTROLS_H_
#define FM1_INSTRUMENT_KIT_SOURCE_CONTROLS_H_

#include <stdint.h>
#include "instrument_math.h"
#ifndef BR_DRUM_LANES
#define BR_DRUM_LANES 4u
#endif

typedef struct {
    uint32_t level[BR_DRUM_LANES];
    uint32_t gate_frames[BR_DRUM_LANES];
} br_kit_source_controls_t;

/* Exact equivalents of the old per-source-job expressions. `quarter_frames`
 * is the CPU0 BPM snapshot returned by br_tempo_quarter_frames() for this
 * audio block. */
static inline uint32_t br_kit_gate_frames_from_quarter(uint32_t beats,
                                                        uint32_t quarter_frames)
{
    if (!beats) beats = 128u;
    if (beats > 128u) beats = 128u;
    return quarter_frames * beats;
}

static inline void br_kit_source_controls_prepare(
    const uint32_t lane_level[BR_DRUM_LANES], const uint32_t lane_gate[BR_DRUM_LANES],
    uint32_t part_level, uint32_t mute, uint32_t quarter_frames,
    br_kit_source_controls_t *out)
{
    uint32_t i;
    uint32_t bounded_level = part_level > 127u ? 127u : part_level;
    for (i = 0; i < BR_DRUM_LANES; ++i) {
        uint32_t product = lane_level[i] * bounded_level;
        out->level[i] = mute ? 0u : br_div127_lane_product_exact(product);
        out->gate_frames[i] = br_kit_gate_frames_from_quarter(
            lane_gate[i], quarter_frames);
    }
}

#endif
