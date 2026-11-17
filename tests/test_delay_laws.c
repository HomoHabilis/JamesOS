/* Delay laws through the real code at the device rate (32 kHz): TIME list and
 * its displayed time, the feedback law (repeats to -60 dB), the wet-path
 * bandwidth of every entry, and a ringing tail across every adjacent TIME
 * change and across tempo steps.  Never accesses hardware.
 * Usage: test_delay_laws [--measure]   (--measure prints the tables and does
 * not fail; the runner uses it on an older tree for before/after numbers). */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdlib.h>

#ifndef BR_FX_DELAY_NOTES
#define BR_FX_DELAY_NOTES 11u                    /* the older list, for --measure */
#endif
#define BLK 160u

static int strict = 1, failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (strict) { \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static br_sound_t snd;
static br_fx_config_t cfg;
static br_mod_config_t modcfg;
static br_fx_controls_t *c;
static br_fx_track_t *st;
static uint32_t lfo;

static void setup(uint32_t engine, uint32_t stored, uint32_t fb, uint32_t bpm)
{
    memset(&snd, 0, sizeof snd);
    br_module_defaults(&modcfg, &cfg);
    snd.delay = 127u;
    cfg.delay_engine = engine; cfg.delay_time = stored; cfg.delay_feedback = fb;
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

/* A live edit: the controls are derived again while the line keeps running. */
static void retune(uint32_t stored, uint32_t bpm)
{
    cfg.delay_time = stored;
    br_fx_prepare_track_controls(0u, &snd, &cfg, c, bpm);
}

/* Run n samples through the delay stage; wet = output - dry. */
static void run(const int32_t *in, int32_t *wet, uint32_t n)
{
    static int32_t zero[BLK], stereo[BLK][2];
    for (uint32_t base = 0u; base < n; base += BLK) {
        uint32_t len = n - base < BLK ? n - base : BLK;
        const int32_t *x = in ? in + base : zero;
        uint32_t final = lfo + len * c->mod_lfo_inc;
        br_fx_stage_delay_block(st, 0u, 0u, 0u, lfo, c->mod_lfo_inc, final, x, len, c, stereo);
        lfo = final;
        if (wet) for (uint32_t i = 0u; i < len; ++i) wet[base + i] = stereo[i][0] - x[i];
    }
}

static double rms(const int32_t *x, uint32_t n)
{
    double s = 0.0;
    for (uint32_t i = 0u; i < n; ++i) s += (double)x[i] * x[i];
    return n ? sqrt(s / n) : 0.0;
}

static uint32_t store_of(uint32_t index) { return br_fx_delay_note_store(index); }

static double nominal(uint32_t index, uint32_t bpm)
{
    static const double beats11[11] = {1/8.0, 1/6.0, 1/4.0, 1/3.0, 1/2.0, 1, 1, 2, 2, 3, 4};
    static const double beats14[14] = {1/8.0, 1/6.0, 1/4.0, 1/3.0, 3/8.0, 1/2.0, 2/3.0,
                                       3/4.0, 1, 4/3.0, 3/2.0, 2, 3, 4};
    double beats = BR_FX_DELAY_NOTES == 14u ? beats14[index] : beats11[index];
    return (double)FS * 60.0 * beats / bpm;
}

static int32_t buf_in[15u * 32000u], buf_wet[15u * 32000u];

static double centroid(uint32_t from, uint32_t to)
{
    double num = 0.0, den = 0.0;
    for (uint32_t i = from; i < to; ++i) {
        double e = (double)buf_wet[i] * buf_wet[i];
        num += e * i; den += e;
    }
    return den > 0.0 ? num / den : -1.0;
}

/* Delay time = the loop period: the spacing of the first two echoes of a
 * tone burst (energy centroids).  *latency is how late the first echo is (the
 * send's anti-alias filters); it is constant, it does not accumulate. */
static double measure_time(uint32_t index, uint32_t bpm, uint32_t *shift, double *latency)
{
    double nom = nominal(index, bpm), c1, c2;
    uint32_t t0 = 1000u, n = (uint32_t)(2.5 * nom) + 2u * t0;
    setup(BR_DELAY_DIGITAL, store_of(index), 64u, bpm);
    *shift = c->delay_storage_shift;
    memset(buf_in, 0, n * sizeof buf_in[0]);
    {
        /* A six-cycle burst well inside the storage bandwidth, high enough that
         * the loop's 60 Hz high-pass adds well under a storage sample per pass. */
        double hz = 2000.0 / (1u << *shift);
        uint32_t len = (uint32_t)(6.0 * FS / hz), k;
        for (k = 0u; k < len; ++k)
            buf_in[t0 + k] = (int32_t)lrint(8000.0 * (0.5 - 0.5 * cos(2.0 * M_PI * k / len)) *
                                            sin(2.0 * M_PI * hz * k / FS));
        t0 += len / 2u;
    }
    run(buf_in, buf_wet, n);
    c1 = centroid(t0 + (uint32_t)(0.5 * nom), t0 + (uint32_t)(1.5 * nom));
    c2 = centroid(t0 + (uint32_t)(1.5 * nom), t0 + (uint32_t)(2.5 * nom));
    *latency = c1 - t0 - nom;
    return c2 - c1;
}

/* Wet gain of a steady sine (feedback 0), relative to the dry level. */
static double wet_gain(uint32_t index, uint32_t bpm, double hz)
{
    uint32_t n = (uint32_t)(nominal(index, bpm) + FS / 8u), win = FS / 20u;
    setup(BR_DELAY_DIGITAL, store_of(index), 0u, bpm);
    for (uint32_t i = 0u; i < n; ++i) buf_in[i] = (int32_t)lrint(8000.0 * sin(2.0 * M_PI * hz * i / FS));
    run(buf_in, buf_wet, n);
    return rms(buf_wet + n - win, win) / rms(buf_in + n - win, win);
}

/* -3 dB corner of the wet path relative to its 40 Hz gain. */
static double bandwidth(uint32_t index, uint32_t bpm)
{
    double ref = wet_gain(index, bpm, 40.0), lo = 40.0, hi = FS / 2.0;
    for (int it = 0; it < 14; ++it) {
        double mid = sqrt(lo * hi);
        if (wet_gain(index, bpm, mid) >= ref * M_SQRT1_2) lo = mid; else hi = mid;
    }
    return sqrt(lo * hi);
}

/* Repeats to -60 dB from the decay of successive echoes of a 1 kHz burst
 * (quiet enough to stay below the line's soft knee). */
static double measure_repeats(uint32_t fb, uint32_t index, uint32_t bpm)
{
    uint32_t T = (uint32_t)lrint(nominal(index, bpm)), burst = FS / 50u, k, last = 0u;
    double level[8];
    uint32_t n = 7u * T + FS / 10u;
    setup(BR_DELAY_DIGITAL, store_of(index), fb, bpm);
    if (!fb) return 0.0;
    memset(buf_in, 0, n * sizeof buf_in[0]);
    for (uint32_t i = 0u; i < burst; ++i)
        buf_in[i] = (int32_t)lrint(8000.0 * (0.5 - 0.5 * cos(2.0 * M_PI * i / burst)) *
                                   sin(2.0 * M_PI * 1000.0 * i / FS));
    run(buf_in, buf_wet, n);
    for (k = 1u; k < 8u; ++k) {
        level[k] = rms(buf_wet + k * T - FS / 200u, burst + FS / 100u);
        if (level[k] > 8.0) last = k;
    }
    if (last < 2u) return 6.9078 / -log(1e-4);   /* second echo inaudible: about one repeat */
    {
        uint32_t k2 = last > 6u ? 6u : last;
        double g = pow(level[k2] / level[1], 1.0 / (k2 - 1u));
        return 6.9078 / -log(g);
    }
}

/* The law the feedback control is meant to follow: repeats to -60 dB grow
 * geometrically from 1 at fb 1 to the safe maximum (0.9696) at 127. */
static double repeats_law(uint32_t fb)
{
    double top = 6.9078 / -log(31771.0 / 32768.0);
    return fb ? pow(top, (fb - 1.0) / 126.0) : 0.0;
}

/* A dense ringing tail (feedback 110) across a live change: RMS of the wet
 * output in the 50 ms before and after, and the largest sample step. */
typedef struct { double before, after, step_before, step_after; uint32_t shift_from, shift_to; } tail_t;
static tail_t tail_change(uint32_t from, uint32_t to, uint32_t bpm_from, uint32_t bpm_to)
{
    tail_t r;
    uint32_t T = (uint32_t)nominal(from, bpm_from), noise = 2u * T + FS / 20u, quiet = FS / 10u;
    uint32_t win = FS / 20u, seed = 1u, i;
    double sb = 0.0, sa = 0.0;
    setup(BR_DELAY_DIGITAL, store_of(from), 110u, bpm_from);
    r.shift_from = c->delay_storage_shift;
    for (i = 0u; i < noise; ++i) {
        seed = seed * 1664525u + 1013904223u;
        buf_in[i] = (int32_t)(seed >> 16) % 6000 - 3000;
    }
    run(buf_in, 0, noise);
    run(0, buf_wet, quiet);
    r.before = rms(buf_wet + quiet - win, win);
    for (i = quiet - win + 1u; i < quiet; ++i) sb = fmax(sb, fabs((double)buf_wet[i] - buf_wet[i - 1u]));
    {
        int32_t last = buf_wet[quiet - 1u];
        retune(store_of(to), bpm_to);
        r.shift_to = c->delay_storage_shift;
        run(0, buf_wet, win);
        sa = fabs((double)buf_wet[0] - last);
        for (i = 1u; i < win; ++i) sa = fmax(sa, fabs((double)buf_wet[i] - buf_wet[i - 1u]));
        r.after = rms(buf_wet, win);
    }
    r.step_before = sb; r.step_after = sa;
    return r;
}

static const char *const names11[11] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1", "1/2", "2", "3", "4"};
static const char *const names14[14] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T",
                                        "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 BAR"};

int main(int argc, char **argv)
{
    static const uint32_t tempos[4] = {60u, 120u, 180u, 240u};
    const char *const *names = BR_FX_DELAY_NOTES == 14u ? names14 : names11;
    uint32_t i, t, fb;
    if (argc > 1 && !strcmp(argv[1], "--measure")) strict = 0;
    br_app_init();
    /* Both effects use the same gentle send taper, with silent/full endpoints. */
    setup(BR_DELAY_DIGITAL, 0u, 0u, 120u);
    for (uint32_t amount = 0u; amount <= 127u; ++amount) {
        snd.delay = snd.reverb = amount;
        br_fx_prepare_track_controls(0u, &snd, &cfg, c, 120u);
        CHECK(c->delay_amount == c->reverb_wet, "send tapers differ at %u", amount);
        if (!amount) CHECK(!c->delay_amount, "zero sends must be silent");
        if (amount == 64u) CHECK(c->delay_amount == 32u, "midpoint send should be about a quarter of full");
        if (amount == 127u) CHECK(c->delay_amount == 127u, "full send must remain available");
    }
    printf("FS %u, line %u samples at FS/2, max shift %u\n", (unsigned)FS,
           (unsigned)BR_FX_DELAY_SAMPLES, (unsigned)BR_FX_DELAY_MAX_SHIFT);

    /* The visible reverb page selects and stores the synced note division. */
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB;
    CHECK(!strcmp(br_control(3u).text, "AUTO"), "reverb pre-delay starts AUTO");
    br_control_turn(3u, 1);
    CHECK(!strcmp(br_control(3u).text, "1/64T"), "reverb pre-delay selected via UI");
    CHECK((br_master.reverb_manual & 32u) && br_master.reverb_extra[0] == 1u, "reverb sync stored");
    CHECK(!(br_master.revision & 1u), "master revision is published after edit");
    br_control_turn(3u, -1);
    CHECK(!(br_master.reverb_manual & 32u), "AUTO restores derived pre-delay");
    CHECK(!(br_master.revision & 1u), "master revision is published after AUTO");

    /* Synced reverb choices must fit and follow tempo across the full range. */
    {
        static const uint32_t ticks[12] = {0u, 8u, 12u, 16u, 18u, 24u, 32u, 36u, 48u, 64u, 72u, 96u};
        for (i = 1u; i <= 11u; ++i) for (t = 0u; t < 4u; ++t) {
            uint32_t want = (RV2_CORE_FS * 5u * ticks[i] + 8u * tempos[t]) / (16u * tempos[t]);
            br_master.reverb_manual = 32u; br_master.reverb_extra[0] = (uint8_t)i;
            br_bpm = tempos[t]; br_gr_rv.idle = 1u; br_gr_prepare();
            CHECK((uint32_t)br_gr_rv.pre_target + br_gr_rv.sync_target == want, "reverb sync timing %u at %u", (unsigned)i, (unsigned)tempos[t]);
            CHECK(br_gr_rv.sync_target < (br_gr_rv.sync_mode ? 3u * RV2_SYNC_PRE_COMP_LEN - 6u : RV2_SYNC_PRE_LEN), "reverb sync fits %u at %u", (unsigned)i, (unsigned)tempos[t]);
            CHECK((uint32_t)br_gr_rv.pre_d + br_gr_rv.sync_d == want, "idle reverb settles before input");
        }
        br_master.reverb_manual = 0u; br_bpm = 120u; br_gr_prepare();
        /* A live change between storage modes fades out, changes mode, then fades in. */
        br_master.reverb_manual = 32u; br_master.reverb_extra[0] = 1u; br_bpm = 60u;
        br_gr_rv.idle = 1u; br_gr_prepare();
        CHECK(br_gr_rv.sync_mode == 0u, "short reverb uses full-rate storage");
        br_gr_rv.idle = 0u; br_gr_duck = 0u; br_master.reverb_extra[0] = 11u;
        br_gr_prepare();
        CHECK(br_gr_duck == 1u && br_gr_rv.sync_mode == 0u, "long reverb fades out before mode change");
        br_gr_prepare();
        CHECK(br_gr_duck == 2u && br_gr_rv.sync_mode == 1u, "long reverb changes mode before fade in");
        br_master.reverb_extra[0] = 1u; br_gr_prepare();
        CHECK(br_gr_duck == 1u && br_gr_rv.sync_mode == 1u, "short reverb fades out before mode change");
        br_gr_prepare();
        CHECK(br_gr_duck == 2u && br_gr_rv.sync_mode == 0u, "short reverb changes mode before fade in");
        br_gr_rv.idle = 1u; br_master.reverb_manual = 0u; br_gr_prepare();
    }

    /* Every stored legacy TIME keeps its meaning; all new UI choices round trip. */
    for (i = 0u; i < 128u; ++i) {
        uint32_t old = br_fx_delay_note_index(i);
        CHECK(br_fx_delay_choice(i, 0u) == (old < 2u ? old + 1u : old + 2u), "legacy TIME %u", (unsigned)i);
    }
    for (i = 0u; i < 16u; ++i) {
        uint32_t stored; uint8_t short_note;
        br_fx_delay_choice_set(i, &stored, &short_note);
        CHECK(br_fx_delay_choice(stored, short_note) == i, "TIME choice round trip %u", (unsigned)i);
    }
    br_bpm = 120u;
    CHECK(br_fx_delay_choice_samples(0u) == (FS + 12u) / 24u, "1/32T at 120 BPM");
    CHECK(br_fx_delay_choice_samples(3u) == (FS * 3u + 16u) / 32u, "1/32D at 120 BPM");

    /* Decode: even buckets, and store() lands inside each bucket. */
    for (i = 0u; i < BR_FX_DELAY_NOTES; ++i)
        CHECK(br_fx_delay_note_index(store_of(i)) == i, "store/index round trip %u", (unsigned)i);
    CHECK(br_fx_delay_note_index(0u) == 0u && br_fx_delay_note_index(127u) == BR_FX_DELAY_NOTES - 1u,
          "index ends");

    /* Time per entry and tempo, the displayed text, and the storage rate. */
    printf("\nTIME   bpm  nominal_ms measured_ms err_samples shift first_echo_late display\n");
    for (i = 0u; i < BR_FX_DELAY_NOTES; ++i) {
        for (t = 0u; t < 4u; ++t) {
            uint32_t shift;
            double latency, nom = nominal(i, tempos[t]), got = measure_time(i, tempos[t], &shift, &latency);
            double tol = (double)(2u << shift);
            char text[24] = "";
#if BR_FX_DELAY_NOTES == 14u
            br_bpm = tempos[t];
            br_time_text(text, br_fx_delay_samples(i) + FS / 2000u);   /* as the value text */
            {
                /* Whole milliseconds below 1 s, tenths of a second above. */
                double ms = got * 1000.0 / FS, shown = atof(text) * (strstr(text, "MS") ? 1.0 : 1000.0);
                double res = shown >= 1000.0 ? 50.0 : 1.0;
                CHECK(fabs(ms - shown) <= res, "%s at %u BPM shows %s, measured %.2f ms", names[i],
                      (unsigned)tempos[t], text, ms);
            }
#endif
            printf("%-6s %3u %10.2f %11.2f %+11.1f %5u %15.1f %s\n", names[i], (unsigned)tempos[t],
                   nom * 1000.0 / FS, got * 1000.0 / FS, got - nom, (unsigned)shift, latency, text);
            CHECK(fabs(got - nom) <= tol, "%s at %u BPM: %.1f samples off (shift %u)", names[i],
                  (unsigned)tempos[t], got - nom, (unsigned)shift);
        }
    }

    /* Wet-path bandwidth per entry at three tempos. */
    printf("\nTIME   -3dB@60  -3dB@120  -3dB@180 (Hz)\n");
    for (i = 0u; i < BR_FX_DELAY_NOTES; ++i) {
        double bw[3];
        for (t = 0u; t < 3u; ++t) bw[t] = bandwidth(i, tempos[t]);
        printf("%-6s %7.0f %9.0f %9.0f\n", names[i], bw[0], bw[1], bw[2]);
        /* Up to 1/8 at 120 BPM the line keeps its full half-rate storage: the
         * send's binomial half-band filter and output interpolation give
         * about 3.0 kHz (2.4 kHz where the time fell between storage samples,
         * before the time was rounded to whole storage samples). */
        if (nominal(i, 120u) <= nominal(BR_FX_DELAY_NOTES == 14u ? 5u : 4u, 120u)) {
            setup(BR_DELAY_DIGITAL, store_of(i), 0u, 120u);
            CHECK(c->delay_storage_shift == 0u && bw[1] > 2900.0, "%s at 120 BPM: bandwidth %.0f Hz",
                  names[i], bw[1]);
        }
    }

    /* Feedback law: repeats to -60 dB, at full storage rate (1/8, 120 BPM)
     * and at the fourth (1/2, 60 BPM, storage FS/8). */
    printf("\nfb  law_q15  gain    repeats_law  measured@1/8,120  measured@1/2,60\n");
    for (fb = 0u; fb <= 128u; fb += 16u) {
        uint32_t f = fb > 127u ? 127u : fb, i8 = BR_FX_DELAY_NOTES == 14u ? 5u : 4u;
        uint32_t i2 = BR_FX_DELAY_NOTES == 14u ? 11u : 7u, q;
        double law = repeats_law(f), got, got2;
        setup(BR_DELAY_DIGITAL, store_of(i8), f, 120u);
#if BR_FX_DELAY_NOTES == 14u
        q = br_fx_delay_feedback_law(f);
#else
        q = c->delay_feedback;
#endif
        got = measure_repeats(f, i8, 120u);
        got2 = measure_repeats(f, i2, 60u);
        printf("%3u %8u  %.4f %11.2f %17.2f %16.2f\n", (unsigned)f, (unsigned)q, q / 32768.0, law, got, got2);
#if BR_FX_DELAY_NOTES == 14u
        {
            double exact = f ? 6.9078 / -log(q / 32768.0) : 0.0;
            CHECK(fabs(exact - law) <= 0.03 * law + 0.05, "fb %u: table gives %.2f repeats, law %.2f",
                  (unsigned)f, exact, law);
            /* The loop's 60 Hz high-pass takes a little more at 1 kHz on long tails. */
            CHECK(fabs(got - law) <= 0.08 * law + 0.3, "fb %u: measured %.2f repeats, law %.2f",
                  (unsigned)f, got, law);
            CHECK(fabs(got2 - law) <= 0.08 * law + 0.3, "fb %u at 1/2, 60 BPM: measured %.2f repeats, law %.2f",
                  (unsigned)f, got2, law);
        }
#endif
    }
#if BR_FX_DELAY_NOTES == 14u
    {
        uint32_t prev = 0u, worst_fb = 0u;
        double worst = 0.0;
        for (fb = 0u; fb <= 127u; ++fb) {
            uint32_t q = br_fx_delay_feedback_law(fb);
            CHECK(fb == 0u ? q == 0u : q > prev, "feedback monotonic at %u", (unsigned)fb);
            if (fb) {
                double e = fabs(6.9078 / -log(q / 32768.0) / repeats_law(fb) - 1.0);
                if (e > worst) { worst = e; worst_fb = fb; }
            }
            prev = q;
        }
        printf("largest departure from the law: %.1f %% at %u\n", worst * 100.0, (unsigned)worst_fb);
        CHECK(worst < 0.05, "feedback law departs %.1f %% at %u", worst * 100.0, (unsigned)worst_fb);
        CHECK(prev == 31771u, "feedback 127 keeps the safe maximum (%u)", (unsigned)prev);
    }
#endif

    /* A ringing tail across every adjacent TIME change and tempo step. */
    printf("\nchange              shift  rms_before rms_after  dB   step_before step_after\n");
    for (t = 0u; t < 3u; ++t) {
        uint32_t bpm = t == 0u ? 120u : t == 1u ? 60u : 180u;
        if (t) {
            uint32_t bpm_from = t == 1u ? 120u : 60u;
            for (i = 0u; i < BR_FX_DELAY_NOTES; ++i) {
                tail_t r = tail_change(i, i, bpm_from, bpm);
                double db = 20.0 * log10(r.after / r.before);
                printf("%-6s %3u->%3u BPM  %u->%u %10.0f %9.0f %+5.1f %11.0f %10.0f\n", names[i],
                       (unsigned)bpm_from, (unsigned)bpm, (unsigned)r.shift_from, (unsigned)r.shift_to,
                       r.before, r.after, db, r.step_before, r.step_after);
                CHECK(fabs(db) <= 6.0, "%s tempo %u->%u: %.1f dB", names[i], (unsigned)bpm_from,
                      (unsigned)bpm, db);
                CHECK(r.step_after <= 2.0 * r.step_before + 64.0, "%s tempo %u->%u: step %.0f vs %.0f",
                      names[i], (unsigned)bpm_from, (unsigned)bpm, r.step_after, r.step_before);
            }
            continue;
        }
        for (i = 0u; i + 1u < BR_FX_DELAY_NOTES; ++i) {
            for (uint32_t dir = 0u; dir < 2u; ++dir) {
                uint32_t a = dir ? i + 1u : i, b = dir ? i : i + 1u;
                tail_t r = tail_change(a, b, bpm, bpm);
                double db = r.after > 0.0 ? 20.0 * log10(r.after / r.before) : -999.0;
                printf("%-6s->%-6s @%3u  %u->%u %10.0f %9.0f %+5.1f %11.0f %10.0f\n", names[a], names[b],
                       (unsigned)bpm, (unsigned)r.shift_from, (unsigned)r.shift_to, r.before, r.after, db,
                       r.step_before, r.step_after);
                CHECK(fabs(db) <= 6.0, "%s->%s: %.1f dB", names[a], names[b], db);
                CHECK(r.step_after <= 2.0 * r.step_before + 64.0, "%s->%s: step %.0f vs %.0f",
                      names[a], names[b], r.step_after, r.step_before);
            }
        }
    }

    /* GLITCH hold and step per entry. */
    printf("\nTIME   glitch_hold glitch_step (stored values in the bucket)\n");
    for (i = 0u; i < BR_FX_DELAY_NOTES; ++i) {
        uint32_t lo = 127u, hi = 0u, s, h0 = 0u, s0 = 0u, same = 1u;
        for (s = 0u; s < 128u; ++s) {
            if (br_fx_delay_note_index(s) != i) continue;
            setup(BR_DELAY_GLITCH, s, 0u, 120u);
            if (lo > s) { lo = s; h0 = c->glitch_hold; s0 = c->glitch_step; }
            if (c->glitch_hold != h0 || c->glitch_step != s0) same = 0u;
            if (hi < s) hi = s;
        }
        printf("%-6s %6u %11u   stored %3u..%3u%s\n", names[i], h0, s0, (unsigned)lo, (unsigned)hi,
               same ? "" : "  (varies inside the bucket)");
#if BR_FX_DELAY_NOTES == 14u
        CHECK(same, "%s: glitch varies inside its bucket", names[i]);
        if (i) {
            setup(BR_DELAY_GLITCH, store_of(i - 1u), 0u, 120u);
            uint32_t ph = c->glitch_hold, ps = c->glitch_step;
            setup(BR_DELAY_GLITCH, store_of(i), 0u, 120u);
            CHECK(c->glitch_hold >= ph && c->glitch_step >= ps, "%s: glitch not monotonic", names[i]);
        }
#endif
    }

    if (failures && strict) { printf("delay laws: %d checks failed\n", failures); return 1; }
    printf("delay laws: %s\n", strict ? "all checks passed" : "measured");
    return 0;
}
