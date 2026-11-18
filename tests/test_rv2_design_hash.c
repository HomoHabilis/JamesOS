/* SPDX-License-Identifier: GPL-3.0-only
 * Hash the global reverb's control-rate designer (rv2_design, rv2_target_rt in
 * firmware/src/instrument_reverb2.c, included as instrument_fx.c includes it) over its
 * whole input space: every TYPE 0..4 x SIZE 0..127 x DECAY 0..127 without overrides;
 * each override (pre, tone, mod, lowcut, width) swept 0..127 with the others derived,
 * for every TYPE at 8 SIZEs x 8 DECAYs; 4096 pseudo-random all-override settings; and
 * rv2_target_rt for every TYPE x DECAY. FNV-1a 64 over the coefficient words. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RV2_API static
#define RV2_RAM __attribute__((noinline))
#define RV2_CORE_CALL(fn, ...) \
    ({ __typeof__(&(fn)) volatile rv2_core_fn_ = &(fn); rv2_core_fn_(__VA_ARGS__); })
#include "../firmware/src/instrument_reverb2.c"

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n--) h = (h ^ *b++) * 1099511628211ull;
    return h;
}

static uint64_t design(uint64_t h, unsigned t, unsigned s, unsigned d, const rv2_overrides_t *ov)
{
    rv2_coeffs_t c;
    memset(&c, 0, sizeof c);
    rv2_design(t, s, d, ov, &c);
    return fnv(h, &c, sizeof c);
}

int main(void)
{
    static const unsigned grid[8] = {0, 1, 31, 63, 64, 65, 100, 127};
    const uint64_t seed = 1469598103934665603ull;
    uint64_t h;
    unsigned t, s, d, f, v, k;

    h = seed;
    for (t = 0; t < 5; ++t) for (s = 0; s < 128; ++s) for (d = 0; d < 128; ++d)
        h = design(h, t, s, d, NULL);
    printf("plain %016llx\n", (unsigned long long)h);

    for (f = 0; f < 5; ++f) {
        h = seed;
        for (v = 0; v < 128; ++v) for (t = 0; t < 5; ++t) for (s = 0; s < 8; ++s) for (d = 0; d < 8; ++d) {
            rv2_overrides_t ov = {RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE};
            ((uint8_t *)&ov)[f] = (uint8_t)v;
            h = design(h, t, grid[s], grid[d], &ov);
        }
        printf("override%u %016llx\n", f, (unsigned long long)h);
    }

    {
        uint32_t r = 12345u;
        h = seed;
        for (k = 0; k < 4096; ++k) {
            rv2_overrides_t ov;
            uint8_t *o = (uint8_t *)&ov;
            for (f = 0; f < 5; ++f) { r = r * 1664525u + 1013904223u; o[f] = (uint8_t)((r >> 24) & 127u); }
            r = r * 1664525u + 1013904223u;
            h = design(h, (r >> 8) % 5u, (r >> 13) & 127u, (r >> 20) & 127u, &ov);
        }
        printf("mixed %016llx\n", (unsigned long long)h);
    }

    h = seed;
    for (t = 0; t < 5; ++t) for (d = 0; d < 256; ++d) {
        float rt = rv2_target_rt(t, d);
        h = fnv(h, &rt, sizeof rt);
    }
    printf("target_rt %016llx\n", (unsigned long long)h);
    return 0;
}
