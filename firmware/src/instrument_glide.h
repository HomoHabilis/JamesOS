/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_GLIDE_H
#define BR_INSTRUMENT_GLIDE_H
#include <stdint.h>

/* Pitch is Q7 semitones. The ramp retains a Q16 fraction, so advancing it
 * in different block sizes produces the same pitch at the same sample. */
typedef struct {
    int32_t start_q16, target_q16, step_q16;
    uint32_t duration, elapsed;
    uint8_t valid;
} br_glide_t;

enum { BR_GLIDE_OFF, BR_GLIDE_LEGATO, BR_GLIDE_ALWAYS };

static inline int32_t br_glide_pitch(const br_glide_t *g)
{
    if (!g->duration || g->elapsed >= g->duration) return g->target_q16;
    return g->start_q16 + (int32_t)((int64_t)g->step_q16 * g->elapsed);
}

/* 0 is immediate; 100 is two seconds per octave. LEGATO only glides when
 * a key was already held; ALWAYS uses the previous pitch once one exists. */
static inline void br_glide_note(br_glide_t *g, uint32_t pitch_q7, uint32_t amount,
                                 uint32_t mode, uint32_t overlapping, uint32_t sample_rate)
{
    int32_t target = (int32_t)(pitch_q7 > 16383u ? 16383u : pitch_q7) << 16;
    int32_t start = g->valid ? br_glide_pitch(g) : target;
    uint32_t distance = (uint32_t)(start > target ? start - target : target - start) >> 16;
    uint32_t ms_per_octave;
    if (amount > 100u) amount = 100u;
    ms_per_octave = (2000u * amount * amount + 5000u) / 10000u;
    if (amount && !ms_per_octave) ms_per_octave = 1u;
    g->start_q16 = start; g->target_q16 = target; g->elapsed = 0u; g->valid = 1u;
    g->duration = mode != BR_GLIDE_OFF && amount && (mode == BR_GLIDE_ALWAYS ||
        (mode == BR_GLIDE_LEGATO && overlapping)) ?
        (uint32_t)(((uint64_t)sample_rate * ms_per_octave * distance) / (1000u * 1536u)) : 0u;
    g->step_q16 = g->duration ? (target - start) / (int32_t)g->duration : 0;
}

static inline uint32_t br_glide_advance(br_glide_t *g, uint32_t samples)
{
    uint32_t left = g->duration - g->elapsed;
    g->elapsed += samples < left ? samples : left;
    return (uint32_t)((br_glide_pitch(g) + 32768) >> 16);
}
#endif
