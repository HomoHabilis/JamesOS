/* Master output limiter (br_master_limit_block): pass-through, limiting, release, stereo. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define RATE 44100.0
#define BLOCKS_PER_SECOND ((uint32_t)(RATE / BR_BLOCK))   /* 172: within 1 s */

static double phase_l, phase_r;

/* One block of a 220 Hz sine, left amplitude al, right amplitude ar (right is
 * a quarter cycle later so the two channels differ). */
static void sine_block(int32_t *buf, double al, double ar)
{
    for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
        buf[2u * i] = (int32_t)lrint(al * sin(phase_l));
        buf[2u * i + 1u] = (int32_t)lrint(ar * sin(phase_r));
        phase_l += 2.0 * M_PI * 220.0 / RATE;
        phase_r = phase_l + M_PI / 2.0;
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
    phase_l = 0.0; phase_r = M_PI / 2.0;
}

/* Hann-windowed single-bin DFT magnitude of the left channel at freq. */
static double tone(const double *x, uint32_t n, double freq)
{
    double re = 0.0, im = 0.0;
    for (uint32_t i = 0u; i < n; ++i) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1u));
        re += w * x[i] * cos(2.0 * M_PI * freq * i / RATE);
        im -= w * x[i] * sin(2.0 * M_PI * freq * i / RATE);
    }
    return sqrt(re * re + im * im);
}

int main(void)
{
    static int32_t in[2u * BR_BLOCK], out[2u * BR_BLOCK];
    enum { SETTLED = 16u };
    static double left[SETTLED * BR_BLOCK];
    uint32_t b, i;

    br_app_init();
    assert(BR_BLOCK == 256u);
    assert(br_master_state.limit_cut == 0u);       /* br_fx_init leaves unity */

    /* (a) A signal peaking at 20000 is passed through bit-identically. */
    reset();
    for (b = 0u; b < 50u; ++b) {
        sine_block(in, 20000.0, 20000.0);
        memcpy(out, in, sizeof in);
        br_master_limit_block(out, BR_BLOCK);
        assert(!memcmp(out, in, sizeof in));
        assert(br_master_state.limit_cut == 0u);
    }
    printf("(a) pass-through: 50 blocks peaking at %d, bit-identical\n", block_peak(in));

    /* (b) +12 dB over: a 220 Hz sine peaking at ~131000. */
    reset();
    {
        int32_t settled_min = 1 << 30, settled_max = 0, worst = 0;
        double h1, h3;
        for (b = 0u; b < 2u + SETTLED; ++b) {
            int32_t peak;
            sine_block(in, 131000.0, 131000.0);
            memcpy(out, in, sizeof in);
            br_master_limit_block(out, BR_BLOCK);
            peak = block_peak(out);
            if (peak > worst) worst = peak;
            assert(peak <= 32767);
            if (b < 2u) printf("(b) block %u (attack; soft clip catches it): peak %d\n", b, peak);
            if (b >= 2u) {
                if (peak < settled_min) settled_min = peak;
                if (peak > settled_max) settled_max = peak;
                for (i = 0u; i < BR_BLOCK; ++i)
                    left[(b - 2u) * BR_BLOCK + i] = out[2u * i];
            }
        }
        assert(settled_min >= 24000 && settled_max <= 27000);
        h1 = tone(left, SETTLED * BR_BLOCK, 220.0);
        h3 = tone(left, SETTLED * BR_BLOCK, 660.0);
        printf("(b) input peak %d: worst output %d; blocks 2..%u peak %d..%d; "
               "gain %u/32768; H3 %.1f dB\n", block_peak(in), worst, 1u + SETTLED,
               settled_min, settled_max, 32768u - br_master_state.limit_cut,
               20.0 * log10(h3 / h1));
        assert(20.0 * log10(h3 / h1) < -30.0);
    }

    /* (c) After the loud signal stops, unity within 1 s; then bit-identical. */
    {
        uint32_t blocks = 0u;
        while (br_master_state.limit_cut) {
            sine_block(in, 10000.0, 10000.0);
            memcpy(out, in, sizeof in);
            br_master_limit_block(out, BR_BLOCK);
            assert(block_peak(out) <= 10000);
            ++blocks;
            assert(blocks <= BLOCKS_PER_SECOND);
        }
        sine_block(in, 20000.0, 20000.0);
        memcpy(out, in, sizeof in);
        br_master_limit_block(out, BR_BLOCK);
        assert(!memcmp(out, in, sizeof in));
        printf("(c) release: exactly unity after %u blocks (%.0f ms; limit %u blocks = 1 s), "
               "then bit-identical\n", blocks, blocks * BR_BLOCK * 1000.0 / RATE, BLOCKS_PER_SECOND);
    }

    /* (d) Stereo: a hot left and a quiet right get the same gain. */
    reset();
    {
        double worst = 0.0;
        uint32_t compared = 0u;
        int32_t right_peak = 0;
        for (b = 0u; b < 10u; ++b) {
            sine_block(in, 131000.0, 8000.0);
            memcpy(out, in, sizeof in);
            br_master_limit_block(out, BR_BLOCK);
            for (i = 0u; i < BR_BLOCK; ++i) {
                int32_t il = in[2u * i], ir = in[2u * i + 1u];
                int32_t ol = out[2u * i], or_ = out[2u * i + 1u];
                double gl, gr;
                if (abs(or_) > right_peak) right_peak = abs(or_);
                if (abs(il) < 4000 || abs(ir) < 4000 || abs(ol) > 24576) continue;
                gl = (double)ol / il; gr = (double)or_ / ir;
                /* Each is the same Q15 gain truncated: within 1/|in| each. */
                if (fabs(gl - gr) > 1.0 / abs(il) + 1.0 / abs(ir) + 1e-12) {
                    printf("(d) block %u frame %u: left gain %.6f right gain %.6f\n", b, i, gl, gr);
                    assert(0);
                }
                if (fabs(gl - gr) > worst) worst = fabs(gl - gr);
                ++compared;
            }
        }
        assert(compared > 500u);
        printf("(d) stereo: %u frames compared, worst left/right gain difference %.6f "
               "(truncation only); right output peak %d (input 8000)\n", compared, worst, right_peak);
    }
    puts("master limiter: all checks passed");
    return 0;
}
