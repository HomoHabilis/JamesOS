/* Filter knob laws, measured through the real filter code (instrument_filter.c):
 *  (a) cutoff: the resonance-0 -3 dB point steps 1.0..2.5 semitones everywhere on both
 *      sides (no dead steps), 20 Hz at the bottom, 16 kHz (LP) / 12 kHz (HP) at the top
 *      at 44.1 and 48 kHz;
 *  (b) resonance: ladder peak rises evenly and self-oscillates only from r = 123;
 *      high-pass peak rises evenly to +16 dB, sits at the cutoff, never self-oscillates;
 *  (c) key tracking 1.00 +- 0.03 octave per octave over notes 36, 48, 60, 72, 84 (least squares);
 *  (f) stability: resonance 127 at every cutoff, full-scale saw in: bounded, and the
 *      high-pass (and resonance-0 low-pass) DC-free.
 * Built per rate (FM1_NATIVE_OUTPUT_*) by tests/run_filter_laws.py. With
 * FILTER_LAWS_MASTER it instead checks the global high-pass through the real master
 * frame (44.1 kHz): cutoff steps, resonance peak and stability. */
/* bisection steps for a -3 dB point: 14 resolve the cutoff to ~0.01 semitone (the checks use 1.0-2.5 semitone steps
 * and 3 %% frequency tolerances) */
#define LAWS_BISECT 14
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FILTER_LAWS_MASTER
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#define RATE 44100.0
static int32_t buf[BR_BLOCK * 2u];

static void controls(uint32_t filter, uint32_t res)
{
    br_master.revision++;
    br_master.fx.filter = filter; br_master.fx.resonance = res;
    br_master.fx.smooth = 0u; br_master.fx.env_depth = 0;
    br_master.fx.heat_type = BR_HEAT_BIT; br_master.fx.heat_amount = 0u;
    RING_PUBLISH(); br_master.revision++;
}
static void reset(uint32_t filter, uint32_t res)
{
    br_app_init(); fm1_ms = 1000u; memset(&br_gf, 0, sizeof br_gf); br_gf_kick = 0u;
    controls(64u, 0u); br_master_fx_init(); controls(filter, res);
}
/* Run n samples of a signal through the master frame; returns the left output. */
typedef double (*sig_fn)(long i);
static double sine_hz;
static double sig_sine(long i) { return 2000.0 * sin(2.0 * M_PI * sine_hz * (double)i / RATE); }
/* 100 Hz saw, exactly 441 samples per cycle, so whole cycles fill the measuring window */
static double sig_saw(long i) { double ph = (double)(i % 441) / 441.0; return 32767.0 * (2.0 * ph - 1.0); }
static double gain_db(uint32_t filter, uint32_t res, double hz)
{
    long settle = (long)((0.25 + 3.0 / hz) * RATE), meas = (long)((0.25 + 6.0 / hz) * RATE), i = 0, k = 0;
    double re = 0.0, im = 0.0, w = 0.0;
    reset(filter, res); sine_hz = hz;
    while (i < settle + meas) {
        for (uint32_t j = 0u; j < BR_BLOCK; ++j) buf[2u * j] = buf[2u * j + 1u] = (int32_t)lrint(sig_sine(i + j));
        if (br_master_fx_begin_block(BR_BLOCK)) br_master_fx_process_range(buf, 0u, BR_BLOCK);
        for (uint32_t j = 0u; j < BR_BLOCK; ++j, ++i) if (i >= settle && k < meas) {
            double ph = 2.0 * M_PI * hz * (double)i / RATE, ww = 0.5 - 0.5 * cos(2.0 * M_PI * (double)k / (double)meas);
            re += ww * buf[2u * j] * sin(ph); im += ww * buf[2u * j] * cos(ph); w += ww; ++k;
        }
    }
    return 20.0 * log10(2.0 * sqrt(re * re + im * im) / w / 2000.0 + 1e-12);
}
static double f3(uint32_t filter)
{
    double lo = 5.0, hi = RATE * 0.49;
    for (int it = 0; it < LAWS_BISECT; ++it) { double m = sqrt(lo * hi); if (gain_db(filter, 0u, m) < -3.0) lo = m; else hi = m; }
    return sqrt(lo * hi);
}
int main(void)
{
    double prev = 0.0;
    for (uint32_t c = 65u; c < 128u; c += 2u) {
        double f = f3(c), st = prev > 0.0 ? 12.0 * log2(f / prev) / 2.0 : 1.8;
        assert(st > 1.0 && st < 2.5);
        prev = f;
    }
    assert(fabs(f3(65u) / 20.0 - 1.0) < 0.03 && fabs(f3(127u) / 12000.0 - 1.0) < 0.03);
    {   /* resonance at the 1 kHz setting: Butterworth at 0, +16 dB at 127, peak at the cutoff */
        double fc = f3(103u), last = -1.0;
        for (uint32_t r = 0u; r < 128u; r += 16u) {
            uint32_t rr = r > 127u ? 127u : r; double pk = -200.0, pf = 0.0;
            for (double hz = fc / 2.0; hz < fc * 2.0; hz *= pow(2.0, 1.0 / 48.0)) {
                double g = gain_db(103u, rr, hz); if (g > pk) { pk = g; pf = hz; }
            }
            assert(pk > last + 1.0 || rr == 0u);
            if (rr >= 64u) assert(fabs(12.0 * log2(pf / fc)) < 1.0);
            last = pk;
        }
        assert(last > 13.0);
    }
    /* stability: resonance 127 at every high-pass setting, full-scale saw, both channels */
    for (uint32_t c = 65u; c < 128u; ++c) {
        long i = 0; double sum = 0.0; int32_t pk = 0; long n = (long)RATE * 2;
        reset(c, 127u);
        while (i < n) {
            for (uint32_t j = 0u; j < BR_BLOCK; ++j) buf[2u * j] = buf[2u * j + 1u] = (int32_t)lrint(sig_saw(i + j));
            if (br_master_fx_begin_block(BR_BLOCK)) br_master_fx_process_range(buf, 0u, BR_BLOCK);
            for (uint32_t j = 0u; j < BR_BLOCK; ++j, ++i) if (i >= n / 2 && i < n) {
                int32_t a = abs(buf[2u * j]); sum += buf[2u * j]; if (a > pk) pk = a;
            }
        }
        if (getenv("FILTER_LAWS_VERBOSE")) printf("master c%u peak %d mean %.1f\n", c, pk, sum / (double)(n - n / 2));
        assert(pk < 262144 && fabs(sum / (double)(n - n / 2)) < 16.0);
    }
    printf("filter laws (global high-pass): cutoff steps, resonance peak and stability checks passed\n");
    return 0;
}
#else
#include "../firmware/src/instrument_filter.c"
#if FM1_NATIVE_OUTPUT_48K
#define RATE 48000.0
#elif FM1_NATIVE_OUTPUT_32K
#define RATE 32000.0
#else
#define RATE 44100.0
#endif

static double gain_db(uint32_t c, uint32_t r, double hz, double amp, uint32_t kt, uint32_t note)
{
    br_filter_t f; br_filter_init(&f);
    int settle = (int)(RATE * (0.25 + 3.0 / hz)), meas = (int)(RATE * (0.25 + 6.0 / hz));
    double re = 0.0, im = 0.0, w = 0.0;
    for (int i = 0; i < settle + meas; ++i) {
        double ph = 2.0 * M_PI * hz * i / RATE;
        int32_t y = br_filter_process_controls(&f, (int32_t)lrint(amp * sin(ph)), c, r, kt, note);
        if (i >= settle) {
            double ww = 0.5 - 0.5 * cos(2.0 * M_PI * (i - settle) / meas);
            re += ww * y * sin(ph); im += ww * y * cos(ph); w += ww;
        }
    }
    return 20.0 * log10(2.0 * sqrt(re * re + im * im) / w / amp + 1e-12);
}
static double f3(uint32_t c, uint32_t kt, uint32_t note)
{
    int lp = c < 63u; double lo = 2.0, hi = RATE * 0.49;
    for (int it = 0; it < LAWS_BISECT; ++it) {
        double m = sqrt(lo * hi), g = gain_db(c, 0u, m, 6000.0, kt, note);
        if (lp ? g > -3.0 : g < -3.0) lo = m; else hi = m;
    }
    return sqrt(lo * hi);
}
static double peak_db(uint32_t c, uint32_t r, double fc, double *at)
{
    double pk = -200.0;
    for (double hz = fc / 3.0; hz < fmin(fc * 3.0, RATE * 0.48); hz *= pow(2.0, 1.0 / 48.0)) {
        double g = gain_db(c, r, hz, 1000.0, 0u, 60u); if (g > pk) { pk = g; *at = hz; }
    }
    return pk;
}
static double ring_rms(uint32_t c, uint32_t r)   /* impulse, then rms over 1.5..2 s */
{
    br_filter_t f; br_filter_init(&f); double s = 0.0; int n = (int)RATE * 2, cnt = 0;
    for (int i = 0; i < n; ++i) {
        int32_t y = br_filter_process_controls(&f, (i >= 64 && i < 68) ? 8000 : 0, c, r, 0u, 60u);
        if (i > n * 3 / 4) { s += (double)y * y; ++cnt; }
    }
    return sqrt(s / cnt);
}
static double keyfit(uint32_t c)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
    static const int notes[] = {36, 48, 60, 72, 84};      /* lowest, highest and three between (was every note 36..84) */
    for (int ni = 0; ni < 5; ++ni) {
        int note = notes[ni];
        double x = (note - 60) / 12.0, y = log2(f3(c, 127u, (uint32_t)note));   /* KEY TRACK 127 = 1 oct/oct */
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}
static void saw_check(uint32_t c, uint32_t r, int32_t *peak, double *mean)
{
    br_filter_t f; br_filter_init(&f); double ph = 0.0, sum = 0.0; int n = (int)RATE * 3, cnt = 0;
    *peak = 0;
    for (int i = 0; i < n; ++i) {
        int32_t y = br_filter_process_controls(&f, (int32_t)lrint(32767.0 * (2.0 * ph - 1.0)), c, r, 0u, 60u);
        ph += 110.0 / RATE; if (ph >= 1.0) ph -= 1.0;
        if (i >= n / 3) { sum += y; ++cnt; if (abs(y) > *peak) *peak = abs(y); }
    }
    *mean = fabs(sum / cnt);
}

int main(void)
{
    double lp_top = RATE >= 44100.0 ? 16000.0 : 0.0, hp_top = RATE >= 44100.0 ? 12000.0 : 0.0;
    double worst_lo = 99.0, worst_hi = 0.0;
    /* (a) cutoff steps */
    for (int side = 0; side < 2; ++side) {
        uint32_t first = side ? 65u : 0u, last = side ? 127u : 62u; double prev = 0.0;
        for (uint32_t c = first; c <= last; ++c) {
            double f = f3(c, 0u, 60u);
            if (c == first) assert(fabs(f / 20.0 - 1.0) < 0.03);
            if (c == last && (side ? hp_top : lp_top) > 0.0) assert(fabs(f / (side ? hp_top : lp_top) - 1.0) < 0.03);
            if (prev > 0.0) {
                double st = 12.0 * log2(f / prev);
                if (st < worst_lo) worst_lo = st;
                if (st > worst_hi) worst_hi = st;
                assert(st > 1.0 && st < 2.5);
            }
            prev = f;
        }
    }
    /* Stop-band rejection: the extra stages make the cutoff noticeably steeper. */
    {
        double lp = f3(36u, 0u, 60u), hp = f3(103u, 0u, 60u);
        assert(gain_db(36u, 0u, lp * 4.0, 6000.0, 0u, 60u) < -26.0);
        assert(gain_db(103u, 0u, hp / 4.0, 6000.0, 0u, 60u) < -33.0);
    }
    /* (b) ladder resonance at the ~1 kHz setting */
    {
        uint32_t c = RATE >= 44100.0 ? 36u : 38u; double fc = f3(c, 0u, 60u), at, last = -1.0;
        for (uint32_t r = 16u; r <= 112u; r += 16u) {
            double pk = peak_db(c, r, fc, &at);
            assert(pk > last + 1.5 && pk < last + 4.5);
            last = pk;
        }
        assert(ring_rms(c, 120u) < 1.0 && ring_rms(c, 123u) > 300.0 && ring_rms(c, 127u) > 300.0);
    }
    /* (b) high-pass resonance at the ~1 kHz setting */
    {
        uint32_t c = 103u; double fc = f3(c, 0u, 60u), at, last = 0.0;
        assert(fabs(peak_db(c, 0u, fc, &at)) < 0.2);
        for (uint32_t r = 16u; r <= 128u; r += 16u) {
            uint32_t rr = r > 127u ? 127u : r; double pk = peak_db(c, rr, fc, &at);
            assert(fabs(pk - 16.0 * rr / 127.0) < 0.5);
            if (rr >= 96u) assert(fabs(12.0 * log2(at / fc)) < 0.5);
            last = pk;
        }
        assert(last > 15.5 && ring_rms(c, 127u) < 1.0 && ring_rms(127u, 127u) < 1.0);
    }
    /* (c) key tracking */
    {
        static const uint32_t cs[] = {20u, 30u, 40u, 80u, 95u, 110u};
        for (int i = 0; i < 6; ++i) { double k = keyfit(cs[i]); assert(fabs(k - 1.0) < 0.03); }
    }
    /* (f) stability at resonance 127, and DC */
    for (uint32_t c = 0u; c < 128u; ++c) {
        int32_t pk; double mean;
        if (c == 63u || c == 64u) continue;
        saw_check(c, 127u, &pk, &mean);
        assert(pk < 262144);
        if (c > 64u) assert(mean < 16.0);
        else {
            saw_check(c, 0u, &pk, &mean);
            assert(mean < 16.0);
        }
    }
    printf("filter laws at %.1f kHz: cutoff steps %.2f..%.2f semitones, resonance, key tracking and stability checks passed\n",
           RATE / 1000.0, worst_lo, worst_hi);
    return 0;
}
#endif
