/* Track-level fade: bounded steps, retargets, zero and exact stationary path. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/src/instrument_level_smoothing.h"
static void exercise(uint32_t from, uint32_t to, uint32_t chunk)
{
    br_level_slew_t s = {0}; br_level_block_t b = {0};
    uint32_t base = br_level_prepare(&s, &b, from, chunk, 220u);
    assert(base == from && !b.active);
    int32_t prev = (int32_t)from * 200;
    for (uint32_t pos = 0; pos < 440u; pos += chunk) {
        uint32_t n = chunk < 440u - pos ? chunk : 440u - pos;
        base = br_level_prepare(&s, &b, to, n, 220u);
        int32_t gain = b.start, err = 0;
        int32_t rem = b.remainder < 0 ? -b.remainder : b.remainder;
        for (uint32_t i = 0; i < n; ++i) {
            int32_t now = b.active ? ((int32_t)base * 200 * gain) / 32768 : (int32_t)base * 200;
            int32_t diff = now - prev;
            assert(diff >= -140 && diff <= 140);
            if (from < to) assert(diff >= -2); else assert(diff <= 2);
            prev = now;
            if (b.active && i < b.frames) {
                gain += b.step; err += rem;
                if (err >= (int32_t)b.frames) { gain += b.remainder < 0 ? -1 : 1; err -= (int32_t)b.frames; }
            }
        }
    }
    assert(prev == (int32_t)to * 200);
    base = br_level_prepare(&s, &b, to, chunk, 220u);
    assert(base == to && !b.active);
}
int main(void)
{
    const uint32_t chunks[] = {1, 17, 64, 128, 256};
    for (uint32_t i = 0; i < sizeof chunks / sizeof *chunks; ++i) {
        exercise(0, 127, chunks[i]); exercise(127, 0, chunks[i]);
        exercise(40, 41, chunks[i]); exercise(41, 40, chunks[i]);
    }
    br_level_slew_t s = {0}; br_level_block_t b = {0};
    br_level_prepare(&s, &b, 127, 64, 220);
    br_level_prepare(&s, &b, 0, 64, 220);
    int32_t before = s.current;
    br_level_prepare(&s, &b, 127, 64, 220);
    assert(s.current > before && s.current < 127 * 65536);
    puts("Track level smoothing checks passed");
    return 0;
}
