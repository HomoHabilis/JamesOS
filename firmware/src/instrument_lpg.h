/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_LPG_H
#define BR_INSTRUMENT_LPG_H
#include <stdint.h>

/* A low-pass gate couples the amplitude envelope to a one-pole low-pass.
 * The gate is warm and dark as it closes and opens to 0.5 Nyquist at full
 * envelope. State belongs to a voice, so overlapping notes stay independent. */
static inline int32_t br_lpg_sample(int32_t sample, uint32_t envelope, int32_t *state)
{
    uint32_t coefficient = 1024u + (envelope > 32767u ? 32767u : envelope) * 15u / 32u;
    int32_t delta = sample - *state;
    /* Input is a signed 16-bit oscillator sample, so this product fits i32. */
    *state += (delta * (int32_t)coefficient) >> 15;
    return *state;
}
#endif
