/* Master output leveler (BR_MASTER_LEVELER, br_master_limit_block). Built
 * twice by run_master_leveler.py: with the switch off, (a) only; with it on,
 * (a) on the loud limiter signals plus (b)..(g). Never touches hardware. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define RATE 44100.0
#define BPS (RATE / BR_BLOCK)                 /* blocks per second, 172.3 */

/* The limiter as it was before the leveler (ede757e), verbatim, own state. */
static uint32_t ref_cut;
static void ref_limit_block(int32_t *stereo, uint32_t n)
{
    uint32_t i, a, peak = 0u, old = ref_cut, cut, gain, step, target = 0u;
    n *= 2u;
    for (i = 0u; i < n; ++i) {
        a = stereo[i] < 0 ? 0u - (uint32_t)stereo[i] : (uint32_t)stereo[i];
        if (a > peak) peak = a;
    }
    if (peak <= 24576u && !old) return;
    if (peak > 26000u)
        target = 32768u - (26000u << 15) / peak;
    cut = old - (old >> 5) - (old >> 7) - 8u;
    if ((int32_t)cut < (int32_t)target) cut = target;
    gain = ((32768u - old) << 16) | 0xFFFFu;
    step = (old - cut) * (65536u / BR_BLOCK);
    for (i = 0u; i < n; ++i) {
        int32_t x = stereo[i];
        uint32_t g;
        if (!(i & 1u)) gain += step;
        g = gain >> 16;
        a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
        a = (a >> 15) * g + (((a & 32767u) * g) >> 15);
        if (a > 24576u + 16384u) a = 24576u + 16384u;
        if (a > 24576u) {
            uint32_t u = a - 24576u;
            a -= (u * u) >> 15;
            if (a > 32767u) a = 32767u;
        }
        stereo[i] = x < 0 ? -(int32_t)a : (int32_t)a;
    }
    ref_cut = 32768u - (gain >> 16);
}

static double phase;
/* One block of a 220 Hz sine; right a quarter cycle later (as the limiter test). */
static void sine_block(int32_t *buf, double al, double ar)
{
    for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
        buf[2u * i] = (int32_t)lrint(al * sin(phase));
        buf[2u * i + 1u] = (int32_t)lrint(ar * sin(phase + M_PI / 2.0));
        phase += 2.0 * M_PI * 220.0 / RATE;
    }
}

static int32_t block_peak(const int32_t *buf)
{
    int32_t peak = 0;
    for (uint32_t i = 0u; i < 2u * BR_BLOCK; ++i) {
        int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    return peak;
}

static void reset(void)
{
    memset(&br_master_state, 0, sizeof br_master_state);
    ref_cut = 0u;
    phase = 0.0;
}

static double rms_amp(double dbfs) { return 32768.0 * pow(10.0, dbfs / 20.0) * sqrt(2.0); }

/* (a) The limiter test's signals: output equals the pre-change limiter. */
static void check_reference(void)
{
    static int32_t in[2u * BR_BLOCK], out[2u * BR_BLOCK], ref[2u * BR_BLOCK];
    static const double seq[][2] = {{20000, 20000}, {131000, 131000}, {10000, 10000},
                                    {20000, 20000}, {131000, 8000}};
    static const uint32_t len[] = {50u, 18u, 200u, 50u, 10u};
    uint32_t s, b, blocks = 0u;
    reset();
    for (s = 0u; s < sizeof len / sizeof len[0]; ++s) {
        if (s == 4u) reset();
        for (b = 0u; b < len[s]; ++b, ++blocks) {
            sine_block(in, seq[s][0], seq[s][1]);
            memcpy(out, in, sizeof in);
            memcpy(ref, in, sizeof in);
            br_master_limit_block(out, BR_BLOCK);
            ref_limit_block(ref, BR_BLOCK);
            if (memcmp(out, ref, sizeof out)) { for (uint32_t k = 0; k < 2u * BR_BLOCK; ++k) if (out[k] != ref[k]) { printf("s %u b %u k %u in %d out %d ref %d cut %u ref_cut %u\n", s, b, k, in[k], out[k], ref[k], br_master_state.limit_cut, ref_cut); break; } }
            assert(!memcmp(out, ref, sizeof out));
            assert(br_master_state.limit_cut == ref_cut);
        }
    }
    printf("(a) switch %s: %u blocks of the limiter test's signals bit-identical to the "
           "pre-change limiter\n", BR_MASTER_LEVELER ? "on" : "off", blocks);
}

#if BR_MASTER_LEVELER
static double boost_db(void) { return 20.0 * log10((32768.0 + br_master_state.lev_boost) / 32768.0); }

/* One block through, checking (f) on the way: every output sample equals the
 * input times the Q30 ramp from the previous block's applied gain to this
 * one's, with the same step every frame. Returns the output peak. */
static uint32_t zipper_blocks, zipper_frames;
static int32_t run_block(int32_t *buf)
{
    static int32_t in[2u * BR_BLOCK];
    uint32_t g0 = (uint32_t)(32768 + br_master_state.lev_applied), g1, i;
    memcpy(in, buf, sizeof in);
    br_master_limit_block(buf, BR_BLOCK);
    g1 = (uint32_t)(32768 + br_master_state.lev_applied);
    if (g0 != g1) {
        uint32_t step = (g1 - g0) * (32768u / BR_BLOCK), gain = (g0 << 15) | 0x7fffu;
        ++zipper_blocks;
        for (i = 0u; i < BR_BLOCK; ++i) {
            uint32_t g, ch;
            gain += step;
            g = gain >> 15;
            for (ch = 0u; ch < 2u; ++ch) {
                int32_t x = in[2u * i + ch];
                uint32_t a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
                a = (a >> 15) * g + (((a & 32767u) * g) >> 15);
                if (a > 24576u) continue;                 /* soft knee: not the ramp */
                assert(buf[2u * i + ch] == (x < 0 ? -(int32_t)a : (int32_t)a));
                ++zipper_frames;
            }
        }
        assert(gain >> 15 == g1);
        /* Smooth: under 0.1 dB per block (12 dB/s down is 0.07). */
        assert(fabs(20.0 * log10((double)g1 / g0)) < 0.1 || br_master_state.limit_cut);
    }
    return block_peak(buf);
}

static void steady(double dbfs, double seconds, double *peak_out)
{
    static int32_t buf[2u * BR_BLOCK];
    uint32_t b, blocks = (uint32_t)(seconds * BPS);
    int32_t worst = 0;
    double max_boost = 0.0;
    for (b = 0u; b < blocks; ++b) {
        int32_t p;
        sine_block(buf, rms_amp(dbfs), rms_amp(dbfs));
        p = run_block(buf);
        if (p > worst) worst = p;
        if (boost_db() > max_boost) max_boost = boost_db();
    }
    assert(max_boost <= 6.0 + 1e-9);
    if (peak_out) *peak_out = worst;
}
#endif

int main(void)
{
    br_app_init();
    assert(BR_BLOCK == 256u);
    check_reference();
#if BR_MASTER_LEVELER
    {
        static int32_t buf[2u * BR_BLOCK];
        uint32_t b;
        double t_reach = -1.0, peak;

        /* (b) -26 dBFS RMS: +6.0 dB after ~4 s and never more. */
        reset();
        for (b = 0u; b < (uint32_t)(8.0 * BPS); ++b) {
            sine_block(buf, rms_amp(-26.0), rms_amp(-26.0));
            run_block(buf);
            assert(boost_db() <= 6.0 + 1e-9);
            if (t_reach < 0.0 && boost_db() >= 5.7) t_reach = b / BPS;
            if (b == (uint32_t)(4.0 * BPS)) {
                printf("(b) -26 dBFS RMS: boost %.3f dB at 4 s", boost_db());
                assert(fabs(boost_db() - 6.0) <= 0.3);
            }
        }
        printf(" (within 0.3 dB at %.2f s), %.3f dB at 8 s; output RMS %.1f dBFS\n", t_reach, boost_db(),
               -26.0 + 20.0 * log10((32768.0 + br_master_state.lev_applied) / 32768.0));
        assert(fabs(boost_db() - 6.0) <= 0.3);

        /* (c) -8 dBFS RMS: 0 dB. */
        reset();
        steady(-8.0, 6.0, &peak);
        printf("(c) -8 dBFS RMS for 6 s: boost %.3f dB\n", boost_db());
        assert(br_master_state.lev_boost == 0u);
        /* -14 dBFS (the target): stays within one step of 0 dB too. */
        reset();
        steady(-14.0, 6.0, NULL);
        printf("    -14 dBFS RMS (target) for 6 s: boost %.3f dB\n", boost_db());
        assert(boost_db() < 0.1);
        reset();
        steady(-20.0, 8.0, NULL);
        printf("    -20 dBFS RMS for 8 s: boost %.3f dB (wanted 6.0)\n", boost_db());
        /* The louder of two quadrature channels per block reads ~0.3 dB high. */
        assert(fabs(boost_db() - 6.0) < 0.4);
        reset();
        steady(-17.0, 8.0, NULL);
        printf("    -17 dBFS RMS for 8 s: boost %.3f dB (wanted 3.0)\n", boost_db());
        assert(fabs(boost_db() - 3.0) < 0.4);

        /* (d) quiet (boosted +6 dB) to loud: never over the limiter's ceiling. */
        reset();
        steady(-30.0, 6.0, NULL);
        {
            double b0 = boost_db();
            int32_t worst = 0, settled_max = 0, settled_min = 1 << 30;
            for (b = 0u; b < (uint32_t)(3.0 * BPS); ++b) {
                int32_t p;
                sine_block(buf, 131000.0, 131000.0);
                p = run_block(buf);
                assert(p <= 32767);
                if (p > worst) worst = p;
                if (b >= 2u) {
                    if (p > settled_max) settled_max = p;
                    if (p < settled_min) settled_min = p;
                }
                if (b < 2u) printf("(d) boost %.2f dB, jump to peak 131000: block %u peak %d\n", b0, b, p);
            }
            assert(settled_min >= 24000 && settled_max <= 27000);
            printf("(d) worst %d (ceiling 32767); blocks 2.. peak %d..%d; boost after 3 s %.2f dB\n",
                   worst, settled_min, settled_max, boost_db());
        }

        /* (e) A decaying tail: -18 dBFS falling 40 dB/s. Once under the gate
         * the boost never exceeds what it was at the crossing. */
        reset();
        steady(-18.0, 6.0, NULL);
        {
            double start = boost_db(), held = -1.0, after = 0.0, at_relax = -1.0;
            uint32_t gated_at = 0u;
            for (b = 0u; b < (uint32_t)(8.0 * BPS); ++b) {
                double db = -18.0 - 40.0 * b / BPS;
                sine_block(buf, rms_amp(db), rms_amp(db));
                run_block(buf);
                if (held < 0.0 && br_master_state.lev_quiet) { held = boost_db(); gated_at = b; }
                if (held >= 0.0) {
                    assert(boost_db() <= held + 1e-9);
                    if (boost_db() > after) after = boost_db();
                    if (at_relax < 0.0 && boost_db() < held) at_relax = (b - gated_at) / BPS;
                }
            }
            assert(held >= 0.0 && held < 5.9 && after <= held + 1e-9);
            printf("(e) tail from -18 dBFS at -40 dB/s: boost %.2f dB at start, %.2f dB at the gate "
                   "(%.2f s, level %.1f dBFS); max after %.2f dB; relaxing after %.2f s; %.2f dB at 8 s\n",
                   start, held, gated_at / BPS, -18.0 - 40.0 * gated_at / BPS, after, at_relax, boost_db());
        }

        /* (g) From +6 dB, silence: exact silence out, back to 0 dB. */
        reset();
        steady(-26.0, 5.0, NULL);
        {
            double b0 = boost_db(), t0 = -1.0;
            for (b = 0u; b < (uint32_t)(10.0 * BPS); ++b) {
                uint32_t i;
                memset(buf, 0, sizeof buf);
                run_block(buf);
                for (i = 0u; i < 2u * BR_BLOCK; ++i) assert(buf[i] == 0);
                if (t0 < 0.0 && !br_master_state.lev_boost && !br_master_state.lev_applied) t0 = b / BPS;
            }
            assert(t0 > 0.0 && !br_master_state.lev_boost && !br_master_state.lev_applied &&
                   !br_master_state.limit_cut);
            printf("(g) silence after %.2f dB: output exactly 0 for 10 s; 0 dB after %.2f s; "
                   "level state %u\n", b0, t0, br_master_state.lev_ms);
        }
        printf("(f) zipper: %u ramped blocks, %u samples equal the linear per-block ramp exactly "
               "(constant step, continuous across blocks, < 0.1 dB per block)\n", zipper_blocks, zipper_frames);
        assert(zipper_frames > 100000u);
    }
#endif
    puts("master leveler: all checks passed");
    return 0;
}
