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
BR_COLD_SIZE static inline uint32_t br_kit_gate_frames_from_quarter(uint32_t beats,
                                                        uint32_t quarter_frames)
{
    if (!beats) beats = 128u;
    if (beats > 128u) beats = 128u;
    return quarter_frames * beats;
}

BR_COLD_SIZE static inline void br_kit_source_controls_prepare(
    const uint32_t lane_level[BR_DRUM_LANES], const uint32_t lane_gate[BR_DRUM_LANES],
    uint32_t part_level, uint32_t mute, uint32_t quarter_frames,
    br_kit_source_controls_t *out)
{
    uint32_t i;
    uint32_t bounded_level = part_level > 127u ? 127u : part_level;
    for (i = 0; i < BR_DRUM_LANES; ++i) {
        /* Lane LEVEL follows the track law (v/127)^2; part_level is already the
         * track's law value. Rounded, at least 1 while both are up. */
        uint32_t lane = lane_level[i] > 127u ? 127u : lane_level[i];
        uint32_t product = lane * lane * bounded_level;          /* <= 127^3 */
        uint32_t level = (product + 8064u) / 16129u;
        out->level[i] = mute ? 0u : level ? level : product ? 1u : 0u;
        out->gate_frames[i] = br_kit_gate_frames_from_quarter(
            lane_gate[i], quarter_frames);
    }
}

#endif
