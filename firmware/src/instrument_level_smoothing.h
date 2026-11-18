/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_LEVEL_SMOOTHING_H
#define BR_LEVEL_SMOOTHING_H
#include <stdint.h>
/* LEVEL law: gain = (v/127)^2 as a source level 0..127 (round(v*v/127), at
 * least 1 above zero): -36.1/-24.0/-12.0/-4.9/-2.2/0 dB at 16/32/64/96/112/127.
 * The source paths keep their linear level multiply, so the curve costs nothing
 * per sample; below v = 14 it stays on the source floor of 1 (-42 dB). Shared by
 * the track level, the kit lane levels and the dB read-out. */
static inline uint32_t br_level_law(uint32_t v)
{
    if (v > 127u) v = 127u;
    return !v ? 0u : v < 8u ? 1u : (v * v + 63u) / 127u;
}
/* Q16 level, with a linear five-millisecond transition when the target changes. */
typedef struct { int32_t current, target, step; uint32_t remaining, ready; } br_level_slew_t;
typedef struct { int32_t start, step, remainder, end; uint32_t frames, active; } br_level_block_t;
static inline uint32_t br_level_prepare(br_level_slew_t *s, br_level_block_t *b,
                                        uint32_t target, uint32_t n, uint32_t ramp)
{
    int32_t goal, start, end, delta;
    uint32_t base, frames;
    if (target > 127u) target = 127u;
    goal = (int32_t)(target << 16);
    b->active = 0u;
    if (!s->ready) { s->ready = 1u; s->current = s->target = goal; s->remaining = 0u; return target; }
    if (goal != s->target) {
        s->target = goal; s->remaining = ramp ? ramp : 1u;
        s->step = (goal - s->current) / (int32_t)s->remaining;
    }
    if (!s->remaining || !n) return target;
    start = s->current;
    frames = n < s->remaining ? n : s->remaining;
    end = frames == s->remaining ? goal : start + s->step * (int32_t)frames;
    s->current = end; s->remaining -= frames;
    base = ((uint32_t)(start > goal ? start : goal) + 65535u) >> 16;
    if (!base) return target;
    b->start = (start / 2) / (int32_t)base;
    b->end = (end / 2) / (int32_t)base;
    delta = b->end - b->start;
    b->frames = frames;
    b->step = delta / (int32_t)frames;
    b->remainder = delta % (int32_t)frames;
    b->active = 1u;
    return base;
}
#endif
