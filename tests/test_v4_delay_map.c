/* All 128 saved v16 TIME values retain their note durations. */
#include "../tools/legacy_delay_map.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const uint8_t old_24ths[11] = {3,4,6,8,12,24,24,48,48,72,96};
    const uint8_t new_24ths[14] = {3,4,6,8,9,12,16,18,24,32,36,48,72,96};
    for (uint32_t value = 0; value < 128; ++value) {
        uint32_t mapped = jamesos_v16_delay_time(value);
        assert(mapped <= 127u && old_24ths[value * 11u / 128u] == new_24ths[mapped * 14u / 128u]);
    }
    puts("all 128 old delay TIME values retain their musical duration");
    return 0;
}
