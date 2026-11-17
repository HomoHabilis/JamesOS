/* Check the extension delays the entire reverb, including early reflections,
 * without changing its signal. Run directly against the real core. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/instrument_reverb2.c"
#define FRAMES 16000
static int16_t pool_a[RV2_POOL16_LEN], pool_b[RV2_POOL16_LEN], extra[RV2_SYNC_PRE_LEN];
static int32_t pool32_a[RV2_POOL32_LEN], pool32_b[RV2_POOL32_LEN];
static int32_t a[FRAMES], b[FRAMES];
int main(void)
{
    rv2_state_t x, y;
    rv2_coeffs_t c;
    rv2_overrides_t ov = {68, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE};
    int32_t in[2], al[2], ar[2], bl[2], br[2];
    const unsigned delay = 2048;
    unsigned n, energy = 0;
    rv2_init(&x, pool_a, pool32_a); rv2_init(&y, pool_b, pool32_b);
    y.sync_pre = extra; y.sync_d = delay;
    rv2_design(RV2_ROOM, 64, 64, &ov, &c); rv2_apply(&x, &c); rv2_apply(&y, &c);
    for (n = 0; n < FRAMES; n += 2) {
        in[0] = n == 0 ? 240000 : 0; in[1] = 0;
        rv2_core_run(&x, in, al, ar, 2); rv2_core_run(&y, in, bl, br, 2);
        a[n] = al[0]; a[n+1] = al[1]; b[n] = bl[0]; b[n+1] = bl[1];
    }
    for (n = 0; n < FRAMES; ++n) {
        int32_t expected = n < 2 * delay ? 0 : a[n - 2 * delay];
        if (b[n] != expected) { fprintf(stderr, "mismatch at %u: %d != %d\n", n, b[n], expected); return 1; }
        if (expected) ++energy;
    }
    if (!energy) { fputs("no reverb output\n", stderr); return 1; }
    puts("reverb extension: exact delayed impulse response passed");
    return 0;
}
