/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_CPU_OVERRUN_WARNING_H
#define BR_CPU_OVERRUN_WARNING_H
#include <stdint.h>
typedef struct {
    uint32_t last_count, first_ms;
    uint8_t initialized, pending, warned, notice_pending;
} br_cpu_warning_t;
/* Two or more audio overruns within four 4/4 bars. Counters saturate and the
 * once-per-power-on latch deliberately survives project load and INIT. */
static inline uint32_t br_cpu_warning_step(br_cpu_warning_t *s, uint32_t now,
                                            uint32_t count, uint32_t bpm)
{
    uint32_t delta, window;
    if (!s->initialized) { s->initialized = 1u; s->last_count = count; return 0u; }
    if (s->warned) return 0u;
    if (bpm < 30u) bpm = 30u;
    if (bpm > 300u) bpm = 300u;
    window = 960000u / bpm;
    if (s->pending && (uint32_t)(now - s->first_ms) > window) s->pending = 0u;
    if (count < s->last_count) { s->last_count = count; s->pending = 0u; return 0u; }
    delta = count - s->last_count; s->last_count = count;
    if (!delta) return 0u;
    if (delta >= 2u || s->pending) { s->warned = 1u; s->notice_pending = 1u; return 1u; }
    s->pending = 1u; s->first_ms = now;
    return 0u;
}
/* Keep the notice queued while a more urgent status message is visible. */
static inline uint32_t br_cpu_warning_take_notice(br_cpu_warning_t *s, uint32_t message_busy)
{
    if (!s->notice_pending || message_busy) return 0u;
    s->notice_pending = 0u;
    return 1u;
}
#endif
