/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DINGHY_MASTER_GAIN_CONTROL_H
#define DINGHY_MASTER_GAIN_CONTROL_H
#include <stdint.h>

#define FM1_MASTER_GAIN_TEST_Q12 215
#define FM1_MASTER_GAIN_QUALITY_Q12 4096

typedef struct {
    int32_t live_q12;
    uint32_t test_mode;
} fm1_master_gain_control_t;

/* Cache each valid main-loop ADC result even while test gain is held. */
static inline int32_t fm1_master_gain_update_live(
    fm1_master_gain_control_t *state, int32_t live_q12)
{
    state->live_q12 = live_q12;
    return state->test_mode == 2u ? FM1_MASTER_GAIN_QUALITY_Q12 :
        state->test_mode ? FM1_MASTER_GAIN_TEST_Q12 : live_q12;
}

/* `allowed` is the caller's stopped/quiet diagnostic gate. Invalid or
 * disallowed requests leave both mode and effective gain unchanged. */
static inline uint32_t fm1_master_gain_set_test_mode(
    fm1_master_gain_control_t *state, uint32_t mode, uint32_t allowed,
    volatile int32_t *effective_q12)
{
    if (!allowed || mode > 2u || !effective_q12) return 0u;
    state->test_mode = mode;
    *effective_q12 = mode == 2u ? FM1_MASTER_GAIN_QUALITY_Q12 :
        mode ? FM1_MASTER_GAIN_TEST_Q12 : state->live_q12;
    return 1u;
}

static inline uint32_t fm1_master_gain_test_allowed(
    uint32_t playing, uint32_t armed, uint32_t muted, uint32_t aborted,
    uint32_t engine_quiet, uint32_t fx_quiet)
{
    return !playing && !armed && !muted && !aborted && engine_quiet && fx_quiet;
}

#endif
