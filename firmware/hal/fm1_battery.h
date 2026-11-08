/* SPDX-License-Identifier: GPL-3.0-only */
/* Stock-derived FM-1 ADC channel 3 gauge. These are level bins, not percent
 * or calibrated voltage: Felucca firmware/src/ui_draw.c uses 531/561/591. */
#pragma once
#include <stdint.h>

typedef struct {
    int32_t filtered_q5;
    uint32_t last_good_ms;
    uint32_t valid;
} fm1_battery_state_t;

static int32_t fm1_battery_bars_from_adc(int32_t raw)
{
    if (raw <= 0 || raw > 1023) return -1;
    return raw >= 591 ? 3 : raw >= 561 ? 2 : raw >= 531 ? 1 : 0;
}

static void fm1_battery_sample(fm1_battery_state_t *state, int32_t raw, uint32_t now)
{
    if (raw <= 0 || raw > 1023) return;
    if (!state->valid) state->filtered_q5 = raw * 32;
    else state->filtered_q5 += (raw * 32 - state->filtered_q5) / 4;
    state->last_good_ms = now;
    state->valid = 1u;
}

static int32_t fm1_battery_bars(const fm1_battery_state_t *state, uint32_t now)
{
    if (!state->valid || (uint32_t)(now - state->last_good_ms) > 5000u) return -1;
    return fm1_battery_bars_from_adc((state->filtered_q5 + 16) / 32);
}
