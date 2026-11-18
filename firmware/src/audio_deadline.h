/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_AUDIO_DEADLINE_H
#define BR_AUDIO_DEADLINE_H
#include "../hal/fm1_audio_rate.h"

/* Fixed T4 budget for one 256-frame half, measured with the existing floor
 * conversion. Reserve twice the observed maximum post-final-worker-wait-to-ack tail. */
#define BR_AUDIO_HALF_DEADLINE_TICKS FM1_AUDIO_HALF_TICKS
#if FM1_NATIVE_OUTPUT_32K
#define BR_AUDIO_PIPELINE_TAIL_RESERVE_TICKS FM1_NATIVE32_TAIL_RESERVE_TICKS
#else
#define BR_AUDIO_PIPELINE_TAIL_RESERVE_TICKS 9216u
#endif
#define BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS \
    (BR_AUDIO_HALF_DEADLINE_TICKS - BR_AUDIO_PIPELINE_TAIL_RESERVE_TICKS)

/* CPU0's wait for CPU1's streamed master FX (instrument_fx.c br_fx_finish_block).
 * CPU1 has the whole master section left when CPU0 finishes its mix (master FX,
 * the global ladder, which runs from flash, then the limiter over the block), and
 * that grows with the global filter's settings and movement. A fixed 18,415-tick
 * wait (0.77 ms) took a slow but healthy CPU1 for a hung one and held it in reset
 * for good. Wait at least that, and otherwise until the half's source deadline
 * (render start + BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS): the output is still on
 * time then. Past that deadline the minimum applies, as before (FM1_CPU1_WORKER_TIMEOUT_TICKS
 * bounds any wait far above a half). */
#define BR_AUDIO_STREAM_WAIT_MIN_TICKS 18415u
static inline __attribute__((always_inline)) uint32_t br_audio_stream_wait_ticks(uint32_t deadline_valid,
                                                  uint32_t deadline_tick, uint32_t now)
{
    int32_t left = (int32_t)(deadline_tick - now);
    /* A deadline more than a half away is not this half's: keep the minimum. */
    return deadline_valid && left > (int32_t)BR_AUDIO_STREAM_WAIT_MIN_TICKS &&
        left <= (int32_t)BR_AUDIO_HALF_DEADLINE_TICKS ? (uint32_t)left : BR_AUDIO_STREAM_WAIT_MIN_TICKS;
}

#endif
