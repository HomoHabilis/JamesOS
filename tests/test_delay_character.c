/* Delay character through the real code at the device rate (44.1 kHz,
 * 256-sample blocks): the wet bandwidth and aliasing at every storage rate,
 * the first echo of every engine against the send, TAPE's wow and flutter,
 * per-repeat tone and saturation, self-oscillation, and WIDTH ping-pong.
 * Never accesses hardware.
 * Usage: test_delay_character [--measure]   (--measure prints the tables and
 * does not fail; the runner uses it on an older tree for before/after numbers). */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <math.h>
#include <stdlib.h>

#define BLK 256u
#define SECONDS 13u

static int strict = 1, failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (strict) { \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* TAPE's own wow/flutter, tone and drive (BR_FX_DELAY_WOW_RATE_DEFAULT) are
 * checked where the source has them; older trees are only measured. */
#ifdef BR_FX_DELAY_WOW_RATE_DEFAULT
#define TAPE_CHECK CHECK
#else
#define TAPE_CHECK(cond, ...) ((void)(cond))
#endif

static br_sound_t snd;
static br_fx_config_t cfg;
static br_mod_config_t modcfg;
static br_fx_controls_t *c;
static br_fx_track_t *st;
static uint32_t lfo;
static int32_t buf_in[SECONDS * 44100u], buf_wet[SECONDS * 44100u], buf_side[SECONDS * 44100u];

/* 14-entry TIME list: 1/8 is entry 5, 1/8D 7, 1/4 8, 1/2 11, 1 BAR 13. */
static void setup(uint32_t engine, uint32_t index, uint32_t fb, uint32_t bpm, uint32_t send)
{
    memset(&snd, 0, sizeof snd);
    br_module_defaults(&modcfg, &cfg);
    snd.delay = (uint8_t)send;
    cfg.delay_engine = engine; cfg.delay_time = br_fx_delay_note_store(index); cfg.delay_feedback = fb;
    c = &br_fx_controls[0];
    st = &br_fx_track[0];
    memset(c, 0, sizeof *c);
    memset(st, 0, sizeof *st);
    memset(br_delay_line, 0, sizeof br_delay_line);
    br_fx_prepare_track_controls(0u, &snd, &cfg, c, bpm);
    st->delay_storage_shift = c->delay_storage_shift;
    st->delay_last_time = c->delay_time;
    lfo = 0u;
}

/* Run n samples through the delay stage; wet = output - dry (the mono wet),
 * side = the second wet value the stage returns when WIDTH is on. */
static void run(const int32_t *in, int32_t *wet, uint32_t n)
{
    static int32_t zero[BLK], stereo[BLK][2];
    for (uint32_t base = 0u; base < n; base += BLK) {
        uint32_t len = n - base < BLK ? n - base : BLK;
        const int32_t *x = in ? in + base : zero;
        uint32_t final = lfo + len * c->mod_lfo_inc;
        br_fx_stage_delay_block(st, 0u, 0u, 0u, lfo, c->mod_lfo_inc, final, x, len, c, stereo);
        lfo = final;
        for (uint32_t i = 0u; i < len; ++i) {
            if (wet) wet[base + i] = stereo[i][0] - x[i];
#ifdef BR_FX_DELAY_WIDTH_DEFAULT
            buf_side[base + i] = br_fx_delay_side_of(stereo[i][1]);
#else
            buf_side[base + i] = 0;
#endif
        }
    }
}

static double rms(const int32_t *x, uint32_t n)
{
    double s = 0.0;
    for (uint32_t i = 0u; i < n; ++i) s += (double)x[i] * x[i];
    return n ? sqrt(s / n) : 0.0;
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

/* Track bus full scale: the meter shows bus/4 against 32768. */
#define FULL 131072.0

static void sine(int32_t *x, uint32_t n, double hz, double amp)
{
    for (uint32_t i = 0u; i < n; ++i) x[i] = (int32_t)lrint(amp * sin(2.0 * M_PI * hz * i / FS));
}

/* Steady-state wet gain for a sine (feedback 0), relative to the input. */
static double wet_gain(uint32_t index, uint32_t bpm, double hz)
{
    uint32_t n, win = FS / 10u;
    setup(BR_DELAY_DIGITAL, index, 0u, bpm, 127u);
    n = c->delay_time + FS / 5u;
    sine(buf_in, n, hz, 0.1 * FULL);
    run(buf_in, buf_wet, n);
    return rms(buf_wet + n - win, win) / rms(buf_in + n - win, win);
}

static double bandwidth(uint32_t index, uint32_t bpm)
{
    double ref = wet_gain(index, bpm, 100.0), lo = 100.0, hi = FS / 2.0;
    for (int it = 0; it < 14; ++it) {
        double mid = sqrt(lo * hi);
        if (wet_gain(index, bpm, mid) >= ref * M_SQRT1_2) lo = mid; else hi = mid;
    }
    return sqrt(lo * hi);
}

/* Worst wet output (dB re the input) for tones that fold into 0..fp at the
 * storage rate R: inputs from R - fp up to FS/2. */
static double worst_alias(uint32_t index, uint32_t bpm, double fp, double *at)
{
    double r = FS / (double)(2u << c->delay_storage_shift), worst = -200.0, lo = r - fp;
    for (int k = 0; k <= 40; ++k) {
        double hz = lo + (FS / 2.0 - 30.0 - lo) * k / 40.0, g = db(wet_gain(index, bpm, hz));
        if (g > worst) { worst = g; *at = hz; }
    }
    return worst;
}

/* Level of the first echo of a 20 ms 1 kHz burst against the burst itself. */
static double first_echo(uint32_t engine, double amp, uint32_t *where)
{
    uint32_t burst = FS / 50u, n, t, best = 0u, found = 0u;
    double dry, env, peak = 0.0;
    setup(engine, 5u, 64u, 120u, 127u);
    n = c->delay_time + 4u * burst;
    memset(buf_in, 0, n * sizeof buf_in[0]);
    for (t = 0u; t < burst; ++t)
        buf_in[t] = (int32_t)lrint(amp * sin(2.0 * M_PI * 1000.0 * t / FS));
    run(buf_in, buf_wet, n);
    dry = rms(buf_in, burst);
    for (t = burst; t + burst < n; t += 16u) {
        env = rms(buf_wet + t, burst);
        if (!found && env > 0.2 * dry) found = t;
        if (found && t > found + 2u * burst) break;
        if (found && env > peak) { peak = env; best = t; }
    }
    *where = best;
    return peak / dry;
}

/* Direct DFT magnitude of x[0..n) with a Hann window at hz. */
static double dft(const int32_t *x, uint32_t n, double hz)
{
    double re = 0.0, im = 0.0;
    for (uint32_t i = 0u; i < n; ++i) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / n), ph = 2.0 * M_PI * hz * i / FS;
        re += w * x[i] * cos(ph); im -= w * x[i] * sin(ph);
    }
    return sqrt(re * re + im * im);
}

/* -3 dB corner of one repeat of a click (relative to its 500 Hz..1 kHz level). */
static double repeat_corner(const int32_t *x, uint32_t n)
{
    double ref = 0.5 * (dft(x, n, 500.0) + dft(x, n, 1000.0)), hz;
    for (hz = 1000.0; hz < FS / 2.0; hz *= 1.02)
        if (dft(x, n, hz) < ref * M_SQRT1_2) return hz;
    return FS / 2.0;
}

/* Pitch of the wet sine from interpolated rising zero crossings, in cents
 * against the mean period: the largest deviation, and the peaks of its wow
 * (0.6 Hz) and flutter (6 Hz) parts from a lock-in over whole periods (the
 * parabolic LFOs give triangle pitch: peak = fundamental * pi^2 / 8). */
static double pitch_cents(uint32_t from, uint32_t to, double wow_hz, double *wow, double *flutter)
{
    static double cross[300000], cents[300000];
    uint32_t k = 0u, i;
    double mean, worst = 0.0;
    for (i = from + 1u; i < to && k < 300000u; ++i)
        if (buf_wet[i - 1u] < 0 && buf_wet[i] >= 0)
            cross[k++] = i - 1u + (double)-buf_wet[i - 1u] / (buf_wet[i] - buf_wet[i - 1u]);
    if (k < 10u) return -1.0;
    mean = (cross[k - 1u] - cross[0]) / (k - 1u);
    for (i = 1u; i < k; ++i) {
        cents[i] = 1200.0 * log2(mean / (cross[i] - cross[i - 1u]));
        if (fabs(cents[i]) > worst) worst = fabs(cents[i]);
    }
    for (int part = 0; part < 2; ++part) {
        double hz = part ? 6.0 : wow_hz, re = 0.0, im = 0.0, t0 = cross[0];
        uint32_t n = 0u;
        double span = floor((cross[k - 1u] - t0) / FS * hz) / hz * FS;
        for (i = 1u; i < k && cross[i] - t0 <= span; ++i, ++n) {
            double ph = 2.0 * M_PI * hz * (cross[i] - t0) / FS;
            re += cents[i] * cos(ph); im += cents[i] * sin(ph);
        }
        *(part ? flutter : wow) = 2.0 * sqrt(re * re + im * im) / n * M_PI * M_PI / 8.0;
    }
    return worst;
}

int main(int argc, char **argv)
{
    static const char *const engines[4] = {"DIGITAL", "TAPE", "MULTITAP", "GLITCH"};
    static const struct { uint32_t index, bpm; const char *name; } rates[5] = {
        {5u, 120u, "1/8 @120"}, {8u, 120u, "1/4 @120"}, {11u, 120u, "1/2 @120"},
        {13u, 120u, "1 BAR @120"}, {13u, 60u, "1 BAR @60"}};
    uint32_t s, e;
    if (argc > 1 && !strcmp(argv[1], "--measure")) strict = 0;
    br_app_init();
    printf("FS %u, block %u\n", (unsigned)FS, (unsigned)BLK);

    /* 1. Wet path per storage rate: -3 dB corner (DIGITAL, feedback 0), the
     * worst output for tones that fold into the band below 7 kHz / 2^shift,
     * and how late the first echo of a click is. */
    printf("\nshift storage_Hz  entry        -3dB_Hz  worst_alias_dB (at Hz)  echo_late_samples\n");
    for (s = 0u; s < 5u; ++s) {
        double bw, at = 0.0, alias, fp;
        uint32_t late = 0u, t, n;
        int32_t peak = 0;
        setup(BR_DELAY_DIGITAL, rates[s].index, 0u, rates[s].bpm, 127u);
        CHECK(c->delay_storage_shift == s, "%s: storage shift %u, expected %u", rates[s].name,
              (unsigned)c->delay_storage_shift, (unsigned)s);
        n = c->delay_time + 4096u;
        memset(buf_in, 0, n * sizeof buf_in[0]);
        buf_in[0] = (int32_t)(0.5 * FULL);
        run(buf_in, buf_wet, n);
        for (t = 0u; t < n; ++t)
            if (abs(buf_wet[t]) > peak) { peak = abs(buf_wet[t]); late = t; }
        late -= c->delay_time;
        bw = bandwidth(rates[s].index, rates[s].bpm);
        fp = 7000.0 / (1u << s);
        setup(BR_DELAY_DIGITAL, rates[s].index, 0u, rates[s].bpm, 127u);
        alias = worst_alias(rates[s].index, rates[s].bpm, fp, &at);
        printf("%5u %10.0f  %-11s %8.0f  %14.1f (%5.0f)  %8u\n", (unsigned)s,
               FS / (double)(2u << s), rates[s].name, bw, alias, at, (unsigned)late);
        /* At least 7 kHz at the best storage rate and the same fraction of the
         * storage band at the slower ones; folded tones at least 40 dB down. */
        CHECK(bw >= 7000.0 / (1u << s),
              "%s: wet -3 dB at %.0f Hz", rates[s].name, bw);
        CHECK(alias <= -40.0, "%s: a %.0f Hz tone folds back at %.1f dB", rates[s].name, at, alias);
    }

    /* 2. First echo of every engine against the dry burst (SEND 127, feedback
     * 64, 1/8 at 120 BPM): within 1 dB of 0 dB at -24 and -12 dBFS. */
    printf("\nengine     first_echo_dB@-24dBFS  @-12dBFS  @-6dBFS  (at sample)\n");
    for (e = 0u; e < 4u; ++e) {
        uint32_t w1, w2, w3;
        double a = db(first_echo(e, FULL * 0.0631, &w1));
        double b = db(first_echo(e, FULL * 0.2512, &w2));
        double d = db(first_echo(e, FULL * 0.5012, &w3));
        printf("%-9s %21.2f %9.2f %8.2f  (%u)\n", engines[e], a, b, d, (unsigned)w1);
        CHECK(fabs(a) <= 1.0 && fabs(b) <= 1.0, "%s: first echo %.2f / %.2f dB", engines[e], a, b);
    }

    /* 3. TAPE: each repeat of a click gets darker (the -3 dB corner falls). */
    {
        uint32_t T, n, k, len = 2048u;
        double corner[7];
        setup(BR_DELAY_TAPE, 5u, 100u, 120u, 127u);
        T = c->delay_time;
        n = 7u * T + len;
        memset(buf_in, 0, n * sizeof buf_in[0]);
        buf_in[100] = (int32_t)(0.25 * FULL);
        run(buf_in, buf_wet, n);
        printf("\nTAPE repeat  -3dB_Hz  level_dB(500..1k)\n");
        for (k = 1u; k <= 6u; ++k) {
            const int32_t *x = buf_wet + 100u + k * T - len / 2u;
            corner[k] = repeat_corner(x, len);
            printf("%11u %8.0f %8.1f\n", (unsigned)k, corner[k], db(dft(x, len, 700.0)));
        }
        TAPE_CHECK(corner[3] < 0.85 * corner[1] && corner[6] < 0.85 * corner[3],
              "TAPE repeats do not darken: %.0f, %.0f, %.0f Hz", corner[1], corner[3], corner[6]);
    }

    /* THD of TAPE's first echo: -6 dBFS 1 kHz sine at SEND 127, feedback 0. */
    {
        uint32_t n, len = 8192u, h;
        double f0, harm = 0.0, thd;
        setup(BR_DELAY_TAPE, 5u, 0u, 120u, 127u);
        n = c->delay_time + 3u * len;
        sine(buf_in, n, 1000.0, 0.5012 * FULL);
        run(buf_in, buf_wet, n);
        f0 = dft(buf_wet + n - len, len, 1000.0);
        for (h = 2u; h <= 9u; ++h) {
            double best = 0.0;
            for (int d = -12; d <= 12; d += 3)       /* wow moves the harmonic a little */
                best = fmax(best, dft(buf_wet + n - len, len, 1000.0 * h + d * h / 6.0));
            harm += best * best;
        }
        thd = 100.0 * sqrt(harm) / f0;
        printf("\nTAPE first echo THD at -6 dBFS: %.2f %%\n", thd);
        TAPE_CHECK(thd >= 1.0 && thd <= 4.0, "TAPE THD %.2f %%", thd);
    }

    /* Wow and flutter: pitch of the first echo of a steady 1 kHz sine. */
    {
        static const uint32_t idx[3] = {2u, 5u, 11u};
        printf("\nTAPE pitch   TIME     peak_cents  wow_cents  flutter_cents\n");
        for (uint32_t k = 0u; k < 3u; ++k) {
            uint32_t n, T;
            double wow, fl, peak;
            setup(BR_DELAY_TAPE, idx[k], 0u, 120u, 127u);
            T = c->delay_time;
            n = T + 6u * FS;
            sine(buf_in, n, 1000.0, 0.1 * FULL);
            run(buf_in, buf_wet, n);
            peak = pitch_cents(T + FS / 10u, n, 0.6, &wow, &fl);
            printf("            %5u ms  %10.1f  %9.1f  %13.1f\n", (unsigned)(T * 1000u / FS), peak, wow, fl);
            TAPE_CHECK(wow >= 6.5 && wow <= 9.5 && fl >= 2.2 && fl <= 3.8,
                       "TAPE wow %.1f / flutter %.1f cents at %u ms", wow, fl, (unsigned)(T * 1000u / FS));
            TAPE_CHECK(peak <= 13.0, "TAPE pitch peak %.1f cents at %u ms", peak, (unsigned)(T * 1000u / FS));
        }
    }

    /* Self-oscillation at feedback 127: bounded, no DC, and it dies away. */
    for (e = 0u; e < 4u; ++e) {
        uint32_t n = 12u * FS, burst = FS / 10u, t;
        double mean = 0.0, late;
        int32_t peak = 0;
        setup(e, 5u, 127u, 120u, 127u);
        memset(buf_in, 0, n * sizeof buf_in[0]);
        for (t = 0u; t < burst; ++t)
            buf_in[t] = (int32_t)lrint(0.7 * FULL * sin(2.0 * M_PI * 220.0 * t / FS));
        run(buf_in, buf_wet, n);
        for (t = burst; t < n; ++t) if (abs(buf_wet[t]) > peak) peak = abs(buf_wet[t]);
        for (t = n - FS; t < n; ++t) mean += buf_wet[t];
        mean /= FS;
        late = rms(buf_wet + n - FS, FS);
        printf("%-9s fb 127: peak %.1f dBFS, last second %.1f dBFS rms, DC %.1f\n", engines[e],
               db(peak / FULL), db(late / FULL), mean);
        /* MULTITAP's four taps can add to 2.5 times one echo. */
        CHECK(peak <= (e == BR_DELAY_MULTITAP ? 2.5 : 1.05) * FULL, "%s self-oscillation peak %d",
              engines[e], peak);
        CHECK(fabs(mean) < 64.0, "%s self-oscillation DC %.1f", engines[e], mean);
    }

#ifdef BR_FX_DELAY_WIDTH_DEFAULT
    /* 4. WIDTH: 0 is the mono line; at 127 odd repeats are left, even right;
     * the mono sum of the wet keeps its RMS at every width (L = M + S and
     * R = M - S, so (L + R) / 2 is the mono wet M whatever the side). */
    {
        static const uint32_t widths[5] = {0u, 32u, 64u, 96u, 127u};
        static const struct { uint32_t index, bpm; } times[4] = {{2u, 120u}, {5u, 120u}, {7u, 120u}, {13u, 120u}};
        printf("\nWIDTH  time         mono_sum_dB  odd_side  even_side  shift\n");
        for (uint32_t t = 0u; t < 4u; ++t) {
            double ref = 0.0;
            for (uint32_t w = 0u; w < 5u; ++w) {
                uint32_t n, T, burst = FS / 50u, k;
                double sum, side_odd = 0.0, side_even = 0.0;
                br_part_pending[0].delay_width = (uint8_t)widths[w];
                setup(BR_DELAY_DIGITAL, times[t].index, 80u, times[t].bpm, 127u);
                T = c->delay_time;
                n = 6u * T + burst;
                memset(buf_in, 0, n * sizeof buf_in[0]);
                for (k = 0u; k < burst; ++k)
                    buf_in[k] = (int32_t)lrint(0.1 * FULL * sin(2.0 * M_PI * 700.0 * k / FS));
                run(buf_in, buf_wet, n);
                sum = rms(buf_wet, n);
                if (!w) ref = sum;
                for (k = 1u; k <= 4u; ++k) {
                    /* side / mono of repeat k: +1 = all left, -1 = all right */
                    double m = 0.0, sd = 0.0;
                    for (uint32_t i = k * T; i < k * T + 2u * burst && i < n; ++i) {
                        m += (double)buf_wet[i] * buf_wet[i];
                        sd += (double)buf_side[i] * buf_wet[i];
                    }
                    if (k & 1u) side_odd += m > 0.0 ? sd / m / 2.0 : 0.0;
                    else side_even += m > 0.0 ? sd / m / 2.0 : 0.0;
                }
                printf("%5u  %-3u@%3u %14.2f %9.2f %10.2f %6u\n", (unsigned)widths[w],
                       (unsigned)times[t].index, (unsigned)times[t].bpm, db(sum / ref), side_odd,
                       side_even, (unsigned)c->delay_storage_shift);
                CHECK(fabs(db(sum / ref)) <= 1.0, "width %u: mono sum %.2f dB", (unsigned)widths[w],
                      db(sum / ref));
                /* side / mono per repeat: +1 all left, -1 all right (the 3 ms pan
                 * slew takes a little from each 40 ms window) */
                if (widths[w] == 127u)
                    CHECK(side_odd > 0.8 && side_even < -0.8, "width 127: odd %.2f even %.2f",
                          side_odd, side_even);
            }
        }
        /* Through the track's output stage at PAN centre: at WIDTH 127 the odd
         * repeats leave the right channel nearly silent and the even ones the
         * left; at WIDTH 0 both channels are the same. */
        for (uint32_t w = 0u; w < 2u; ++w) {
            static int32_t stereo[BLK][2], lr[6u * 22050u][2];
            uint32_t n, T, burst = FS / 50u, k, base, tail = 0u;
            double e[5][2] = {{0}};
            br_part_pending[0].delay_width = w ? 127u : 0u;
            setup(BR_DELAY_DIGITAL, 5u, 80u, 120u, 127u);
            T = c->delay_time;
            n = 5u * T + burst;
            memset(buf_in, 0, n * sizeof buf_in[0]);
            for (k = 0u; k < burst; ++k)
                buf_in[k] = (int32_t)lrint(0.1 * FULL * sin(2.0 * M_PI * 700.0 * k / FS));
            for (base = 0u; base < n; base += BLK) {
                uint32_t len = n - base < BLK ? n - base : BLK, final = lfo + len * c->mod_lfo_inc;
                br_fx_stage_delay_block(st, 0u, 0u, 0u, lfo, c->mod_lfo_inc, final, buf_in + base, len, c, stereo);
                br_fx_stage_send_block(st, 0u, 0u, 0u, 64u, final, buf_in + base, buf_in + base, len,
                                       &snd, &cfg, c, &tail, stereo);
                lfo = final;
                memcpy(lr[base], stereo, len * sizeof stereo[0]);
            }
            for (k = 1u; k <= 4u; ++k)
                for (uint32_t i = k * T; i < k * T + 2u * burst; ++i)
                    for (uint32_t ch = 0u; ch < 2u; ++ch) e[k][ch] += (double)lr[i][ch] * lr[i][ch];
            printf("output stage, WIDTH %3u: repeat L/R dB  %+.1f  %+.1f  %+.1f  %+.1f\n", w ? 127u : 0u,
                   10.0 * log10(e[1][0] / e[1][1]), 10.0 * log10(e[2][0] / e[2][1]),
                   10.0 * log10(e[3][0] / e[3][1]), 10.0 * log10(e[4][0] / e[4][1]));
            if (w)
                CHECK(e[1][0] > 30.0 * e[1][1] && e[3][0] > 30.0 * e[3][1] &&
                      e[2][1] > 30.0 * e[2][0] && e[4][1] > 30.0 * e[4][0], "ping-pong through PAN");
            else
                CHECK(e[1][0] == e[1][1] && e[2][0] == e[2][1], "WIDTH 0: left and right differ");
        }
        br_part_pending[0].delay_width = BR_FX_DELAY_WIDTH_DEFAULT;
    }
#endif

    if (failures && strict) { printf("delay character: %d checks failed\n", failures); return 1; }
    printf("delay character: %s\n", strict ? "all checks passed" : "measured");
    return 0;
}
