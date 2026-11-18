/* 1.0a's eleven TIME buckets include duplicate quarter and half notes.
 * Convert their musical duration to the current fourteen-entry stored range. */
#ifndef JAMESOS_LEGACY_DELAY_MAP_H
#define JAMESOS_LEGACY_DELAY_MAP_H
#include <stdint.h>
static inline uint32_t jamesos_v16_delay_time(uint32_t stored)
{
    static const uint8_t new_index[11] = {0, 1, 2, 3, 5, 8, 8, 11, 11, 12, 13};
    if (stored > 127u) stored = 127u;
    uint32_t index = new_index[stored * 11u / 128u];
    return (index * 128u + 64u) / 14u;
}
#endif
