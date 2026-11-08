#ifndef FM1_MIDI_CLOCK_ARP_H
#define FM1_MIDI_CLOCK_ARP_H
#include <stdint.h>
#include "midi_clock_math.h"

/* Continuous 24-PPQN grid. Phase is monotonic Q16 pulses. The caller starts
 * at a known transport phase and asks about successive [start,end) spans. */
typedef struct {
    uint64_t next_index, cursor_q16;
    uint32_t divisor, swing;
} fm1_mca_t;

static uint64_t fm1_mca_boundary(const fm1_mca_t *a, uint64_t index)
{
    uint64_t step = (uint64_t)((24u << 16) / a->divisor);
    return index * step + ((index & 1u) ? step * a->swing / 128u : 0u);
}
static int fm1_mca_start(fm1_mca_t *a, uint64_t phase_q16, uint32_t divisor, uint32_t swing)
{
    uint64_t step;
    if ((divisor != 2u && divisor != 3u && divisor != 4u && divisor != 6u && divisor != 8u) || swing > 77u)
        return 0;
    a->divisor = divisor; a->swing = divisor % 3u ? swing : 0u;
    a->cursor_q16 = phase_q16;
    step = (uint64_t)((24u << 16) / divisor);
    a->next_index = fm1_clock_div64(phase_q16, step, 0);
    if (fm1_mca_boundary(a, a->next_index) < phase_q16) ++a->next_index;
    return 1;
}
/* 0: no onset, 1: one onset and its following gate basis in physical frames,
 * -1: invalid/nonmonotonic input or more than one onset in this span. The
 * caller must choose a fail-safe response to -1; no events are silently lost. */
static int fm1_mca_advance(fm1_mca_t *a, uint64_t end_q16, uint64_t period_q16,
                           uint32_t *gate_frames)
{
    uint64_t at, after, duration;
    if (end_q16 < a->cursor_q16 || !period_q16 || period_q16 > ((uint64_t)1000000u << 16)) return -1;
    at = fm1_mca_boundary(a, a->next_index);
    if (at < a->cursor_q16) return -1;
    if (at >= end_q16) { a->cursor_q16 = end_q16; return 0; }
    after = fm1_mca_boundary(a, a->next_index + 1u);
    if (after < end_q16 || after <= at) return -1;
    duration = ((after - at) * period_q16) >> 32;
    if (!duration || duration > UINT32_MAX) return -1;
    *gate_frames = (uint32_t)duration;
    ++a->next_index; a->cursor_q16 = end_q16;
    return 1;
}
#endif
