/* Private read-only AC791N clock telemetry. No clock or power writes. */
#pragma once
#include <stdint.h>
static inline void fm1_clock_snapshot(uint32_t out[8])
{
    static const uint32_t address[8] = {
        0x10008u, 0x1000Cu, 0x10010u, 0x10014u,
        0x119A0u, 0x119A4u, 0x119A8u, 0x119ACu
    };
    uint32_t i;
    for (i = 0; i < 8u; ++i)
        out[i] = *(volatile uint32_t *)(uintptr_t)address[i];
}
