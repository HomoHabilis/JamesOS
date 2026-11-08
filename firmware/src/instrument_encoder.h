/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_INSTRUMENT_ENCODER_H
#define FM1_INSTRUMENT_ENCODER_H

#include <stdint.h>

/* Per-control acceleration history. A zero-initialized instance is ready. */
typedef struct {
    uint32_t last_ms;
    uint8_t same_direction_intervals;
    int8_t direction;
    uint8_t initialized;
} br_encoder_accel_t;

/* Preserve the first two same-direction events for precise editing. Starting
 * with the third, scale by the event's per-detent interval. Unsigned elapsed
 * time keeps the interval calculation correct across the millisecond wrap. */
static inline int32_t br_encoder_accel_step(br_encoder_accel_t *state,
                                             int32_t delta,
                                             uint32_t now_ms)
{
    uint32_t magnitude, elapsed;
    int8_t direction;
    uint32_t multiplier = 1u;

    if (!state || !delta) return 0;

    /* Bound both abs(INT32_MIN) and the largest later scaled result. */
    if (delta > 32767) delta = 32767;
    else if (delta < -32767) delta = -32767;
    direction = delta > 0 ? 1 : -1;
    magnitude = (uint32_t)(delta > 0 ? delta : -delta);

    if (!state->initialized) {
        state->initialized = 1u;
        state->direction = direction;
        state->last_ms = now_ms;
        state->same_direction_intervals = 0u;
        return delta;
    }

    elapsed = now_ms - state->last_ms;
    if (state->direction != direction || elapsed > 180u) {
        state->direction = direction;
        state->last_ms = now_ms;
        state->same_direction_intervals = 0u;
        return delta;
    }

    state->last_ms = now_ms;
    if (state->same_direction_intervals < 2u)
        ++state->same_direction_intervals;
    if (state->same_direction_intervals < 2u) return delta;

    /* Compare products instead of truncating elapsed / magnitude: e.g. 201ms
     * across two detents is 100.5ms per detent and must remain unaccelerated. */
    if (elapsed > 100u * magnitude) return delta;
    multiplier = elapsed <= 25u * magnitude ? 8u :
                 elapsed <= 55u * magnitude ? 4u : 2u;
    return delta * (int32_t)multiplier;
}

#endif
