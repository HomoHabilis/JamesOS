/* Offline helper for a block-quantized pattern transport prototype.
 * It does not publish patterns or touch sequencer/voice state. */
#ifndef FM1_PATTERN_BAR_CLOCK_H
#define FM1_PATTERN_BAR_CLOCK_H
#include <stdint.h>

#define FM1_PATTERN_BAR_PULSES_Q16 (UINT64_C(96) << 16)

typedef struct { uint64_t next_q16; } fm1_pattern_bar_clock;

/* Call after processing [start,end). A return of 1 means apply the already
 * prepared snapshot at the beginning of the NEXT block. -1 means invalid or
 * more than one bar was crossed; do not silently discard song entries. */
static inline int fm1_pattern_bar_internal(uint32_t beat, uint32_t elapsed,
                                            uint32_t quarter, uint32_t frames)
{
    uint64_t until, bar;
    if (beat > 3u || !quarter || elapsed >= quarter) return -1;
    /* Most blocks end within the current beat and cannot cross a bar. */
    if (frames < quarter - elapsed) return 0;
    bar = (uint64_t)quarter * 4u;
    until = (uint64_t)(4u - beat) * quarter - elapsed;
    if ((uint64_t)frames < until) return 0;
    return (uint64_t)frames - until < bar ? 1 : -1;
}

/* Start anchors the first complete bar to the actual sequencer start phase,
 * which can differ from zero when external clock lock is acquired late. */
static inline int fm1_pattern_bar_external_start(fm1_pattern_bar_clock *clock,
                                                  uint64_t origin_q16)
{
    if (UINT64_MAX - origin_q16 < FM1_PATTERN_BAR_PULSES_Q16) return 0;
    clock->next_q16 = origin_q16 + FM1_PATTERN_BAR_PULSES_Q16;
    return 1;
}

/* On suspect-clock relock use precisely the shift applied to the sequencer
 * targets. Stop/Continue otherwise retain this clock; Start resets it. */
static inline int fm1_pattern_bar_external_shift(fm1_pattern_bar_clock *clock,
                                                  uint64_t shift_q16)
{
    if (UINT64_MAX - clock->next_q16 < shift_q16) return 0;
    clock->next_q16 += shift_q16;
    return 1;
}

/* Before rendering a block, projected follower phase may advance slightly
 * between adjacent projections. If that gap straddles the boundary, publish
 * at this block's start. A gap larger than one current block is ambiguous. */
static inline int fm1_pattern_bar_external_gap(fm1_pattern_bar_clock *clock,
    uint64_t start_q16, uint64_t end_q16)
{
    uint64_t next = clock->next_q16;
    if (end_q16 < start_q16) return -1;
    if (start_q16 <= next) return 0;
    if (start_q16 - next > end_q16 - start_q16 ||
        UINT64_MAX - next < FM1_PATTERN_BAR_PULSES_Q16 ||
        start_q16 >= next + FM1_PATTERN_BAR_PULSES_Q16) return -1;
    clock->next_q16 = next + FM1_PATTERN_BAR_PULSES_Q16;
    return 1;
}

static inline int fm1_pattern_bar_external(fm1_pattern_bar_clock *clock,
                                           uint64_t start_q16, uint64_t end_q16)
{
    uint64_t next = clock->next_q16;
    if (end_q16 < start_q16 || next < start_q16) return -1;
    if (end_q16 < next) return 0;
    if (UINT64_MAX - next < FM1_PATTERN_BAR_PULSES_Q16 ||
        end_q16 >= next + FM1_PATTERN_BAR_PULSES_Q16) return -1;
    clock->next_q16 = next + FM1_PATTERN_BAR_PULSES_Q16;
    return 1;
}
#endif
