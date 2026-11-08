#ifndef FM1_MIDI_CLOCK_SCHEDULER_H
#define FM1_MIDI_CLOCK_SCHEDULER_H

#include <stdint.h>

/* One track's future-boundary planner. All returned durations are physical
 * audio frames. This module neither advances audio nor changes an in-flight
 * gate, retrigger or micro-event. A 24-PPQN reference phase uses Q16 pulses;
 * period_q16 is Q16 physical frames per pulse. One instance is needed per
 * independently divided track; initialize it at a known common phase. */
typedef struct {
    uint64_t target_phase_q16;
    uint32_t ordinal, frame_fraction_q16;
} fm1_mcs_track_t;

static void fm1_mcs_start(fm1_mcs_track_t *track, uint64_t phase_q16)
{
    track->target_phase_q16 = phase_q16;
    track->ordinal = track->frame_fraction_q16 = 0u;
}

/* Plan the duration from this step boundary to the next one. `phase_q16` is
 * the continuous reference phase at the actual boundary, not at block entry.
 * division 0,1,2 means 1/8, 1/16, 1/32; swing is 0..77. The
 * actual pattern step determines swing parity, including odd-length loops. A 1/16-step
 * correction limit ensures short steps stay positive and phase converges
 * gradually. Caller must schedule early next-step events using this duration.
 * Return zero if the reference or input is invalid; caller must then stop or
 * explicitly choose another clock policy. */
static uint32_t fm1_mcs_next(fm1_mcs_track_t *track, uint64_t phase_q16,
                             uint64_t period_q16, uint32_t division, uint32_t swing,
                             uint32_t step)
{
    uint64_t base, span, nominal, next, total;
    int64_t phase_delta, desired, correction, limit;
    if (!period_q16 || period_q16 > ((uint64_t)1000000u << 16) ||
        division > 2u || swing > 77u) return 0u;
    base = ((uint64_t)24u << 15) >> division;
    span = (base * ((step & 1u) ? 128u - swing : 128u + swing)) / 128u;
    next = track->target_phase_q16 + span;
    if (next < track->target_phase_q16 || next > INT64_MAX || phase_q16 > INT64_MAX)
        return 0u;
    /* Reject wildly displaced references before signed multiplication. */
    phase_delta = (int64_t)next - (int64_t)phase_q16;
    if (phase_delta > (int64_t)(base * 4u) || phase_delta < -(int64_t)(base * 4u))
        return 0u;
    nominal = (span * period_q16) >> 16;
    desired = (phase_delta * (int64_t)period_q16) / 65536;
    correction = desired - (int64_t)nominal;
    limit = (int64_t)(nominal / 16u);
    if (correction > limit) correction = limit;
    if (correction < -limit) correction = -limit;
    total = (uint64_t)((int64_t)nominal + correction) + track->frame_fraction_q16;
    if (total < 65536u || (total >> 16) > UINT32_MAX) return 0u;
    track->target_phase_q16 = next;
    ++track->ordinal;
    track->frame_fraction_q16 = (uint32_t)(total & 65535u);
    return (uint32_t)(total >> 16);
}
#endif
