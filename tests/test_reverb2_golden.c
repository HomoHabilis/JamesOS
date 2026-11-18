/* The firmware's global reverb (firmware/src/instrument_reverb2.c, included the way
 * instrument_fx.c includes it) reproduces the host prototype's golden hashes bit for bit
 * (a golden table kept outside this repository): 6 settings x 2 inputs,
 * FNV-1a 64 over int32 L,R (little-endian), 3 s each in 256-frame blocks.
 * Build options: the default (RV2_UPSAMPLE_TAPS=6, RV2_INT_READS=0, RV2_LINE_STRUCT=0)
 * or one of them changed with -D, as tests/run_reverb2_golden.py does.
 * Inputs (integer arithmetic): A, a 200,000 click at frame 100; B, 30 ms of LCG noise
 * (+-65,535), then a 3-voice integer saw chord held 1.5 s, then silence. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The same port hooks as instrument_fx.c (host: no sections; the core is still called
 * through a volatile pointer, as the XIP driver calls the RAM core on the device). */
#define RV2_API static
#define RV2_RAM __attribute__((noinline))
#define RV2_CORE_CALL(fn, ...) \
    ({ __typeof__(&(fn)) volatile rv2_core_fn_ = &(fn); rv2_core_fn_(__VA_ARGS__); })
#include "../firmware/src/instrument_reverb2.c"

static int16_t pool16[RV2_POOL16_LEN];
static int32_t pool32[RV2_POOL32_LEN];

static int32_t input(int which, long t, uint32_t *rng, uint32_t ph[3])
{
    if (which == 0) return t == 100 ? 200000 : 0;
    if (t < 1323) { *rng = *rng * 1664525u + 1013904223u; return (int32_t)(*rng >> 15) - 65536; }
    if (t < 1323 + 66150) {
        static const uint32_t inc[3] = {0x00C80000u, 0x00FC0000u, 0x012C0000u};
        int32_t s = 0; for (int k = 0; k < 3; ++k) { ph[k] += inc[k]; s += (int32_t)ph[k] >> 14; }
        return s / 4;
    }
    return 0;
}

/* out/golden.txt, in its order: per setting, input A then input B. */
static const unsigned set[6][3] = {{0,64,64},{0,127,110},{1,64,64},{1,127,110},{3,64,64},{3,127,127}};
#if RV2_UPSAMPLE_TAPS == 6 && !RV2_INT_READS
#define GOLDEN_BUILD "default (also _soa: RV2_LINE_STRUCT does not change the output)"
static const uint64_t golden[12] = {
    0x44f30bfd473e353full, 0x336bfa59a1b919eeull, 0x97cd17b774b565adull, 0xa5299da01298c13full,
    0x5cc9fce8816057ffull, 0x681b4ae21662013bull, 0x9e7ccfffe52a1ef2ull, 0x2ca864db192ddc06ull,
    0x4f0b28261385471bull, 0x4dc4d4a86923ce1eull, 0x3cda11bf322a8468ull, 0xe34be12fa00e6fbdull};
#elif RV2_UPSAMPLE_TAPS == 4 && !RV2_INT_READS
#define GOLDEN_BUILD "_up4 (RV2_UPSAMPLE_TAPS=4)"
static const uint64_t golden[12] = {
    0x1f3f4edd09cb8e6eull, 0x265a42209baec189ull, 0xd5ca3b1c5990d509ull, 0x180f205ecca25db4ull,
    0xe7cbfda48c803b60ull, 0x37d3a65e286854e0ull, 0x4192afca7b0e466aull, 0x1bed82bf5affef63ull,
    0xa812b651a73ee021ull, 0x5bc9c6b710920549ull, 0x7c6f1c9d98e03d83ull, 0xb55fdfa0697b01bfull};
#elif RV2_UPSAMPLE_TAPS == 6 && RV2_INT_READS
#define GOLDEN_BUILD "_intrd (RV2_INT_READS=1)"
static const uint64_t golden[12] = {
    0x0cc2b076fac8a4b7ull, 0x377bccfbbe08602eull, 0xd1c5baa90432c797ull, 0xb13d42b08d8e4de8ull,
    0x66e51ab263aeabb7ull, 0x8a5f192d07147d2cull, 0x4ccec14f6c15b108ull, 0x38af79bb470dcf84ull,
    0x77d9789972b2c659ull, 0xc54160807ceabdb3ull, 0xdd7030252c1eddbdull, 0xeeaa5d8d4185a550ull};
#else
#error "no golden hashes for this combination of build options"
#endif

int main(void)
{
    static rv2_state_t st;
    unsigned failed = 0u;
    for (int k = 0; k < 6; ++k) for (int which = 0; which < 2; ++which) {
        uint64_t h = 1469598103934665603ull; uint32_t rng = 1u, ph[3] = {0, 0, 0};
        rv2_coeffs_t co;
        int32_t in[256], l[256], r[256];
        rv2_init(&st, pool16, pool32);
        rv2_design(set[k][0], set[k][1], set[k][2], 0, &co); rv2_apply(&st, &co);
        for (long t0 = 0; t0 < 3 * 44100; t0 += 256) {
            for (int i = 0; i < 256; ++i) in[i] = input(which, t0 + i, &rng, ph);
            rv2_process(&st, in, l, r, 256);
            for (int i = 0; i < 256; ++i) {
                uint32_t w[2] = {(uint32_t)l[i], (uint32_t)r[i]};
                for (int j = 0; j < 2; ++j) for (int b = 0; b < 4; ++b) { h ^= (w[j] >> (8 * b)) & 255u; h *= 1099511628211ull; }
            }
        }
        if (h != golden[2 * k + which]) ++failed;
        printf("type %u size %3u decay %3u input %c : %016llx %s\n", set[k][0], set[k][1], set[k][2],
               which ? 'B' : 'A', (unsigned long long)h, h == golden[2 * k + which] ? "ok" : "MISMATCH");
    }
    if (failed) { printf("FAIL: reverb2 golden, build %s: %u of 12 differ\n", GOLDEN_BUILD, failed); return 1; }
    printf("reverb2 golden: 12 of 12 hashes match the prototype, build %s (line struct %d)\n",
           GOLDEN_BUILD, RV2_LINE_STRUCT);
    return 0;
}
