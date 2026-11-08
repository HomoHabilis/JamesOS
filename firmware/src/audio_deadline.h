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

#endif
