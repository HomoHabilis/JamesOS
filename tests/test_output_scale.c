/* CPU1's RAM copy of the output scaler must match the platform scaler exactly. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

int main(void)
{
    static const int32_t gains[] = {0, 1, 2047, 2048, 3000, 4095, 4096, 4097, 8192, 65535};
    uint32_t seed = 12345u;
    for (uint32_t g = 0u; g < sizeof gains / sizeof gains[0]; ++g) {
        for (uint32_t trial = 0u; trial < 200u; ++trial) {
            int32_t a[512], b[512];
            for (uint32_t i = 0u; i < 512u; ++i) {
                seed = seed * 1664525u + 1013904223u;
                int32_t v = (int32_t)(seed >> 8) - (1 << 23);   /* includes far out-of-range values */
                if (trial & 1u) v >>= 6;                         /* and in-range values */
                a[i] = b[i] = v;
            }
            volatile int32_t gain = gains[g];
            fm1_scale_q12_clip_block(a, 512u, &gain);
            br_fx_scale_q12_clip_range(b, 512u, (uint32_t)gains[g]);
            assert(!memcmp(a, b, sizeof a));
        }
    }
    return 0;
}
