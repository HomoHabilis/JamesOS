#ifndef FM1_MIDI_CLOCK_TIMELINE_H
#define FM1_MIDI_CLOCK_TIMELINE_H

/* Standalone timing model: caller owns all state and serialization. Timer4
 * observations are USB receipt times, not timestamps from the MIDI source.
 * DMA edge_frame identifies the physical playback frame at the observed edge;
 * the caller must establish that mapping and the writable buffer lead. The
 * model does not infer missed edges or recover timestamps lost to USB batching.
 * Observe every edge and map queued timestamps within half a timer wrap. */
#include <stdint.h>
#include <limits.h>
#include "midi_clock_math.h"

#define FM1_MCT_QUEUE_CAPACITY 32u
#define FM1_MCT_AMBIGUOUS 1u
#define FM1_MCT_LATE 2u
#define FM1_MCT_OVERFLOW 4u

typedef struct {
    uint32_t timer_hz, sample_hz, half_frames, jitter_ticks;
    uint32_t expected_tick, tick_remainder, half_ticks, half_remainder;
    uint64_t edge_frame;
    uint8_t last_half, valid;
} fm1_mct_anchor_t;
typedef struct { uint32_t tick; uint8_t status, flags; } fm1_mct_event_t;
typedef struct {
    fm1_mct_event_t entries[FM1_MCT_QUEUE_CAPACITY];
    uint32_t read, write, last_tick, last_clock_tick, min_clock_ticks;
    uint8_t have_input, have_clock, overflow;
} fm1_mct_queue_t;

static int fm1_mct_anchor_init(fm1_mct_anchor_t *a, uint32_t timer_hz,
    uint32_t sample_hz, uint32_t half_frames, uint32_t jitter_ticks,
    uint32_t edge_tick, uint64_t edge_frame, uint8_t half)
{
    uint64_t half_ticks, half_remainder;
    if (!a || !timer_hz || !sample_hz || !half_frames || half > 1u) return 0;
    half_ticks = fm1_clock_div64((uint64_t)half_frames * timer_hz, sample_hz, &half_remainder);
    /* Independently jittered observations and filter lag need a window below
     * half an interval so that a missing edge still fails closed. */
    if (!half_ticks || half_ticks >= INT32_MAX || !jitter_ticks ||
        (uint64_t)jitter_ticks * 8u >= half_ticks) return 0;
    a->timer_hz=timer_hz; a->sample_hz=sample_hz; a->half_frames=half_frames;
    a->jitter_ticks=jitter_ticks; a->expected_tick=edge_tick; a->tick_remainder=0u;
    a->half_ticks=(uint32_t)half_ticks; a->half_remainder=(uint32_t)half_remainder;
    a->edge_frame=edge_frame; a->last_half=half; a->valid=1u;
    return 1;
}

/* Slew a predicted edge toward observations using a bounded low-pass update.
 * jitter_ticks bounds each observed ISR latency error relative to the true
 * edge; opposite jitter at initialization and later can differ by 2J. The
 * estimator tracks only modest clock drift; it is not hardware calibration.
 * A discontinuity or error beyond 3J plus the slew limit invalidates the anchor.
 * Skew exceeding J/8 ticks per half eventually invalidates it; max_distance
 * should bound extrapolation error between an event and the latest anchor. */
static int fm1_mct_edge(fm1_mct_anchor_t *a, uint32_t observed_tick, uint8_t half)
{
    uint32_t advance, remainder, expected;
    int32_t error, correction, max_slew;
    if (!a || !a->valid) return 0;
    if (half > 1u || half == a->last_half || UINT64_MAX-a->edge_frame < a->half_frames) {
        a->valid=0u; return 0;
    }
    /* Carry the precomputed fractional half duration; avoid runtime wide
     * division on every DMA edge. Both operands are below sample_hz. */
    advance=a->half_ticks;
    if (a->tick_remainder >= a->sample_hz-a->half_remainder) {
        remainder=a->tick_remainder-(a->sample_hz-a->half_remainder);
        ++advance;
    } else remainder=a->tick_remainder+a->half_remainder;
    expected=a->expected_tick+advance;
    error=(int32_t)(observed_tick-expected);
    max_slew=(int32_t)(a->jitter_ticks/8u);
    if (!max_slew) max_slew=1;
    if ((int64_t)error > 3*(int64_t)a->jitter_ticks + max_slew ||
        (int64_t)error < -3*(int64_t)a->jitter_ticks - max_slew) {
        a->valid=0u; return 0;
    }
    /* One eighth of the observed phase error, limited to J/8 per half.
     * A nonzero sub-eight-tick error must still converge. */
    correction=error/8;
    if (!correction && error) correction=error>0 ? 1 : -1;
    if (correction>max_slew) correction=max_slew;
    if (correction< -max_slew) correction= -max_slew;
    a->expected_tick=expected+(uint32_t)correction;
    a->tick_remainder=remainder;
    a->edge_frame+=a->half_frames; a->last_half=half;
    return 1;
}

/* Map an event near the latest anchor into the physical playback-frame epoch.
 * max_distance limits ambiguous modular differences and stale queued events.
 * Rounds toward the later modeled frame. Physical accuracy also depends on
 * measured anchor, ISR-latency and drift uncertainty. */
static int fm1_mct_map(const fm1_mct_anchor_t *a, uint32_t tick,
                       uint32_t max_distance, uint64_t *frame)
{
    int32_t dt; int64_t num, frames;
    uint64_t magnitude, remainder, quotient;
    if (!a || !a->valid || !frame || max_distance >= INT32_MAX) return 0;
    dt=(int32_t)(tick-a->expected_tick);
    if ((int64_t)dt > max_distance || (int64_t)dt < -(int64_t)max_distance) return 0;
    num=(int64_t)dt*a->sample_hz - (int64_t)a->tick_remainder;
    magnitude = num < 0 ? (uint64_t)(-(num + 1)) + 1u : (uint64_t)num;
    quotient = fm1_clock_div64(magnitude, a->timer_hz, &remainder);
    frames = num < 0 ? -(int64_t)quotient : (int64_t)quotient;
    if (num > 0 && remainder) ++frames;
    if (frames < 0 && (uint64_t)(-frames) > a->edge_frame) return 0;
    if (frames >= 0 && UINT64_MAX-a->edge_frame < (uint64_t)frames) return 0;
    *frame=frames < 0 ? a->edge_frame-(uint64_t)(-frames) : a->edge_frame+(uint64_t)frames;
    return 1;
}

static void fm1_mct_queue_init(fm1_mct_queue_t *q, uint32_t min_clock_ticks)
{
    q->read=q->write=q->last_tick=q->last_clock_tick=0u;
    q->min_clock_ticks=min_clock_ticks ? min_clock_ticks : 1u;
    q->have_input=q->have_clock=q->overflow=0u;
}
/* Input must be producer-ordered and within half a timer wrap. Successive F8s
 * closer than min_clock_ticks have unrecoverable timing ambiguity. */
static int fm1_mct_push(fm1_mct_queue_t *q, uint8_t status, uint32_t tick)
{
    fm1_mct_event_t *e;
    if (!q || q->overflow) return 0;
    if (q->write-q->read >= FM1_MCT_QUEUE_CAPACITY) { q->overflow=1u; return 0; }
    if (q->have_input && (int32_t)(tick-q->last_tick)<0) return 0;
    e=&q->entries[q->write%FM1_MCT_QUEUE_CAPACITY];
    e->tick=tick; e->status=status; e->flags=0u;
    if (status==0xf8u) {
        if (q->have_clock && (uint32_t)(tick-q->last_clock_tick)<q->min_clock_ticks)
            e->flags|=FM1_MCT_AMBIGUOUS;
        q->last_clock_tick=tick; q->have_clock=1u;
    }
    q->last_tick=tick; q->have_input=1u;
    ++q->write; return 1;
}
/* 1: event due at or before the next writable block, 0: empty/future,
 * -1: anchor/queue invalid or timestamp too old. The consumer must invalidate
 * clock lock on overflow or ambiguous F8 and never synthesize missing pulses. */
static int fm1_mct_pop(fm1_mct_queue_t *q, const fm1_mct_anchor_t *a,
    uint64_t next_writable_frame, uint32_t max_distance,
    fm1_mct_event_t *event, uint64_t *scheduled_frame)
{
    uint64_t frame;
    fm1_mct_event_t e;
    if (!q || !event || !scheduled_frame || !a || !a->valid || q->overflow) return -1;
    if (q->read == q->write) return 0;
    e=q->entries[q->read%FM1_MCT_QUEUE_CAPACITY];
    if (!fm1_mct_map(a,e.tick,max_distance,&frame)) return -1;
    if (frame > next_writable_frame) return 0;
    if (frame < next_writable_frame) e.flags|=FM1_MCT_LATE;
    *event=e; *scheduled_frame=next_writable_frame; ++q->read;
    return 1;
}
#endif
