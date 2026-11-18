/* One shared reverb (instrument_reverb2.c, the reverb2 FDN) with per-track sends,
 * through the real app. Behaviour: sends, track-1 settings, retired ids, tails, idle,
 * restore, no click on TYPE or SIZE changes. Numbers: the prototype's measurements
 * (a measurement kept outside this repository, SIZE 64): mid-band RT60 per type at
 * DECAY 0/64/127 within 10%, wet level +6 dB +-1.5 over the dry track at SEND 127,
 * stereo tails (ICC < 0.5, mono sum not cancelling). The numeric checks feed the
 * prototype's own signals into track 1's FX input (br_fx_process: the send stage,
 * the shared reverb job, its return, master and limiter); the rest plays notes. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "hd_look.h"
#include "host_intro.h"

static void publish(uint32_t p) { RING_PUBLISH(); br_part[p].revision++; }

static void setup(void)
{
    br_app_init(); fm1_ms = 1000u;
    for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) {
        /* The factory project may intentionally leave some tracks silent.
         * Exercise shared sends with an audible, identical synth on each. */
        br_load_preset(p, BR_FIRST_PLAITS);
        br_part[p].revision++;
        br_part[p].sound.reverb = 0u; br_part[p].sound.delay = 0u; br_master.reverb_size = 64u;
        br_master.reverb_engine = BR_REVERB_SPACE; br_master.reverb_decay = 40u;
        publish(p);
    }
}
static void send(uint32_t p, uint32_t amount)
{
    br_part[p].revision++; br_part[p].sound.reverb = amount; publish(p);
}
static void reverb_settings(uint32_t p, uint32_t engine, uint32_t tone, uint32_t space)
{
    br_part[p].revision++;
    br_master.reverb_engine = engine; br_master.reverb_decay = tone; br_master.reverb_size = space;
    publish(p);
}
/* The return the reverb added to this block's mix (zeros when its job did not run). */
static const int32_t *wet_row(uint32_t ch)
{
    static const int32_t silent[BR_BLOCK];
    return br_gr_out_seq == br_fx_block_seq ? br_gr_send[br_gr_cur ^ 1u][BR_GR_WET_L_ROW + ch] : silent;
}
static double wet_energy;                 /* sum of squares of the return, both channels */
static double block_rms(int32_t *keep)
{
    int32_t audio[BR_BLOCK * 2u] = {0};
    double sum = 0.0;
    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    for (uint32_t i = 0u; i < BR_BLOCK * 2u; ++i) sum += (double)audio[i] * audio[i];
    for (uint32_t c = 0u; c < 2u; ++c) for (uint32_t i = 0u; i < BR_BLOCK; ++i) wet_energy += (double)wet_row(c)[i] * wet_row(c)[i];
    if (keep) memcpy(keep, audio, sizeof audio);
    return sqrt(sum / (BR_BLOCK * 2u));
}
/* Play a note on `part` for 15 blocks, release, and return the RMS of the tail
 * measured over blocks [skip, skip + count) after the release. */
static double tail_rms(uint32_t part, uint32_t skip, uint32_t count)
{
    double sum = 0.0;
    br_engine_note_on(part, 60u, 100u);
    for (uint32_t i = 0u; i < 15u; ++i) (void)block_rms(0);
    br_engine_note_off(part, 60u);
    for (uint32_t i = 0u; i < skip; ++i) (void)block_rms(0);
    for (uint32_t i = 0u; i < count; ++i) { double r = block_rms(0); sum += r * r; }
    return sqrt(sum / count);
}

/* ---- Feeding signals into track 1's FX input ---------------------------------- */
/* Track 1 at SEND 127, the others off, no delay/mod/drive; the effective controls are
 * settled by silent app blocks, then each block goes straight to br_fx_process. */
static void setup_inject(uint32_t engine, uint32_t size, uint32_t decay)
{
    int32_t audio[BR_BLOCK * 2u];
    setup();
    for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) reverb_settings(p, engine, decay, size);
    send(0u, 127u);
    for (uint32_t b = 0u; b < 200u; ++b) { memset(audio, 0, sizeof audio); assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
    assert(br_effective_sound[0].reverb == 127u);
    /* A room selected while the tank is idle must be fully configured before
     * its first input; silent blocks do not advance the tank's length slew. */
    assert(br_gr_rv.idle);
    for (uint32_t i = 0u; i < RV2_LINES; ++i)
        assert(br_gr_rv.base[i] == br_gr_rv.target[i] && RV2_LN(&br_gr_rv, i, d) == br_gr_rv.target[i]);
}
/* Run x[0..n) (send domain) through the app; wet[2*t + c] is the return to input frame t
 * (the shared reverb hears the send one block late). */
static void inject(const float *x, long n, float *wet)
{
    static int32_t bus[BR_PARTS][BR_BLOCK], out[BR_BLOCK * 2u];
    for (long t0 = 0; t0 < n + (long)BR_BLOCK; t0 += BR_BLOCK) {
        memset(bus, 0, sizeof bus); memset(out, 0, sizeof out);
        for (uint32_t i = 0u; i < BR_BLOCK; ++i) { long t = t0 + i; bus[0][i] = t < n ? (int32_t)lrintf(x[t]) : 0; }
        br_fx_process(bus, out, BR_BLOCK);
        for (uint32_t c = 0u; c < 2u; ++c) {
            const int32_t *w = wet_row(c);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
                long t = t0 + i - (long)BR_BLOCK;
                if (t >= 0 && t < n) wet[2 * t + c] = (float)w[i];
            }
        }
    }
}
/* The prototype's measurement signals (harness/rv2lib.py), in C. */
static uint32_t lcg = 7u;
static double gauss(void)                 /* sum of 12 uniforms - 6: unit variance */
{
    double s = 0.0;
    for (int k = 0; k < 12; ++k) { lcg = lcg * 1664525u + 1013904223u; s += (lcg >> 8) * (1.0 / 16777216.0); }
    return s - 6.0;
}
static void burst(float *x, long n)       /* 30 ms noise at 70,000 RMS from frame 100 */
{
    memset(x, 0, n * sizeof *x);
    for (long t = 100; t < 100 + 1323; ++t) { double v = gauss() * 70000.0; x[t] = (float)(v > 250000 ? 250000 : v < -250000 ? -250000 : v); }
}
/* Held saw triad: band-limited saws, one-pole low-pass at 5 kHz, 5 ms attack, held
 * `hold` s then a 20 ms release, normalised to 24,000 RMS over 0.1..1 s. */
static void chord(float *x, long n, const uint8_t notes[3], double hold)
{
    const double fs = 44100.0, a = exp(-2.0 * M_PI * 5000.0 / fs);
    double s = 0.0, sum = 0.0; long ref = 0;
    for (long t = 0; t < n; ++t) x[t] = 0.0f;
    for (int k = 0; k < 3; ++k) {
        double f = 440.0 * pow(2.0, (notes[k] - 69) / 12.0);
        for (int h = 1; h * f < fs / 2 - 500; ++h) {
            double w = 2.0 * M_PI * h * f / fs, c = cos(w), sn = sin(w), re = cos(h * k), im = sin(h * k), g = 2.0 / M_PI / h;
            for (long t = 0; t < n; ++t) { x[t] += (float)(g * im); double r2 = re * c - im * sn; im = re * sn + im * c; re = r2; }
        }
    }
    for (long t = 0; t < n; ++t) { s = a * s + (1.0 - a) * x[t]; x[t] = (float)s; }
    for (long t = (long)(0.1 * fs); t < (long)(1.0 * fs); ++t) { sum += (double)x[t] * x[t]; ++ref; }
    for (long t = 0; t < n; ++t) {
        double env = t < 220 ? t / 220.0 : 1.0, h0 = hold * fs;
        if (t >= h0) env = t < h0 + 882 ? 1.0 - (t - h0) / 882.0 : 0.0;
        x[t] = (float)(x[t] / sqrt(sum / ref) * 24000.0 * env);
    }
}
/* 1 kHz octave band (two RBJ band-passes, 1 octave), Schroeder decay, T30 (-5..-35 dB;
 * T20 when the tail ends before -35 dB): the prototype's RT1k. */
static double rt_1k(const float *wet, long n, long onset)
{
    const double w0 = 2.0 * M_PI * 1000.0 / 44100.0, alpha = sin(w0) * sinh(log(2.0) / 2.0 * 1.0 * w0 / sin(w0));
    const double b0 = alpha / (1 + alpha), a1 = -2 * cos(w0) / (1 + alpha), a2 = (1 - alpha) / (1 + alpha);
    double *e = calloc((size_t)n, sizeof *e), total = 0.0, lo = -5.0, hi = -35.0;
    for (int c = 0; c < 2; ++c) {
        double z[2][2] = {{0, 0}, {0, 0}};
        for (long t = 0; t < n; ++t) {
            double v = wet[2 * t + c];
            for (int st = 0; st < 2; ++st) {      /* direct form II transposed, b = (b0, 0, -b0) */
                double y = b0 * v + z[st][0];
                z[st][0] = -a1 * y + z[st][1];
                z[st][1] = -b0 * v - a2 * y;
                v = y;
            }
            if (t >= onset) e[t] += v * v;
        }
    }
    for (long t = n - 1; t >= onset; --t) { total += e[t]; e[t] = total; }
    for (long t = onset; t < n; ++t) e[t] = 10.0 * log10(e[t] / total + 1e-30);
    if (e[n - 1] > hi) hi = -25.0;
    long i0 = onset, i1;
    while (i0 < n && e[i0] > lo) ++i0;
    i1 = i0;
    while (i1 < n && e[i1] > hi) ++i1;
    assert(i1 < n && i1 > i0 + 10);
    double st = 0, sy = 0, stt = 0, sty = 0, m = (double)(i1 - i0);
    for (long t = i0; t < i1; ++t) { double tt = t / 44100.0; st += tt; sy += e[t]; stt += tt * tt; sty += tt * e[t]; }
    free(e);
    return -60.0 / ((m * sty - st * sy) / (m * stt - st * st));
}
static double icc(const float *wet, long a, long b, double *mono_db)
{
    double ll = 0, rr = 0, lr = 0;
    for (long t = a; t < b; ++t) { double l = wet[2 * t], r = wet[2 * t + 1]; ll += l * l; rr += r * r; lr += l * r; }
    *mono_db = 10.0 * log10((ll + rr + 2 * lr) / (ll + rr + 1e-30));
    return lr / sqrt(ll * rr + 1e-30);
}

int main(void)
{
    double dry, wet, two, other;
    static const uint32_t types[3] = {BR_REVERB_ROOM, BR_REVERB_HALL, BR_REVERB_SPACE};
    /* Without a send there is nothing after the note; with one there is a tail. */
    setup();
    dry = tail_rms(0u, 60u, 40u);
    setup(); send(0u, 100u);
    wet = tail_rms(0u, 60u, 40u);
    assert(wet > 1.0 && dry < 0.01);
    {
        double midpoint, full;
        setup(); send(0u, 64u); wet_energy = 0; (void)tail_rms(0u, 60u, 40u); midpoint = wet_energy;
        setup(); send(0u, 127u); wet_energy = 0; (void)tail_rms(0u, 60u, 40u); full = wet_energy;
        assert(full > 0 && midpoint / full > 0.03 && midpoint / full < 0.15);
    }

    /* Sends add: two tracks sending the same note level are louder than one. */
    setup(); send(0u, 100u); send(1u, 100u);
    br_engine_note_on(0u, 60u, 100u); br_engine_note_on(1u, 60u, 100u);
    for (uint32_t i = 0u; i < 15u; ++i) (void)block_rms(0);
    br_engine_note_off(0u, 60u); br_engine_note_off(1u, 60u);
    for (uint32_t i = 0u; i < 60u; ++i) (void)block_rms(0);
    { double sum = 0.0; for (uint32_t i = 0u; i < 40u; ++i) { double r = block_rms(0); sum += r * r; }
      two = sqrt(sum / 40.0); }
    assert(two > 1.15 * wet);

    /* The reverb's type, decay and size are one global setting: it sounds the
     * same whichever track's screen set it, and size changes what is heard. */
    setup(); send(1u, 100u); reverb_settings(1u, BR_REVERB_HALL, 120u, 100u);
    other = tail_rms(1u, 60u, 40u);
    setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 120u, 100u);
    wet = tail_rms(1u, 60u, 40u);
    assert(fabs(wet - other) < 1e-6);
    {
        double small, big;
        setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 60u, 20u);
        small = tail_rms(1u, 30u, 30u);
        setup(); send(1u, 100u); reverb_settings(2u, BR_REVERB_HALL, 60u, 120u);
        big = tail_rms(1u, 30u, 30u);
        assert(fabs(big - small) > 0.02 * small);
    }

    /* The retired ids play exactly as their replacements: Spring as Room, Ethereal as Space. */
    {
        static const uint32_t pair[2][2] = {{BR_REVERB_SPRING, BR_REVERB_ROOM}, {BR_REVERB_ETHEREAL, BR_REVERB_SPACE}};
        for (uint32_t k = 0u; k < 2u; ++k) {
            double retired, kept;
            setup(); send(0u, 127u); reverb_settings(0u, pair[k][0], 100u, 64u); retired = tail_rms(0u, 20u, 60u);
            setup(); send(0u, 127u); reverb_settings(0u, pair[k][1], 100u, 64u); kept = tail_rms(0u, 20u, 60u);
            assert(retired > 0.2 && fabs(retired - kept) < 1e-9);
        }
    }

    /* Every type runs and gives a tail; no non-finite blow-ups, output bounded. */
    for (uint32_t k = 0u; k < 3u; ++k) {
        setup(); send(0u, 127u); reverb_settings(0u, types[k], 100u, 64u);
        double level = tail_rms(0u, 20u, 60u);
        assert(level > 0.2 && level < 20000.0);
    }

    /* The tail dies away within a bounded time (ROOM, DECAY 10: about 0.35 s, ended by
     * the reverb's own end-of-tail logic), the output is then exactly zero, the job
     * idle and the lines empty, and a later phrase starts clean. */
    setup(); send(0u, 100u); reverb_settings(0u, BR_REVERB_ROOM, 10u, 40u);
    wet_energy = 0.0;
    double first = tail_rms(0u, 5u, 20u), first_wet = wet_energy;
    uint32_t blocks = 0u;
    while (br_fx_quiet() == 0 && blocks < 4000u) { (void)block_rms(0); ++blocks; }
    assert(first > 0.5 && br_fx_quiet() && blocks < 400u);           /* under 2.3 s after the 20 measured blocks */
    { double r = 0.0; for (uint32_t i = 0u; i < 20u; ++i) r += block_rms(0); assert(r == 0.0); }
    assert(br_gr_run == 0u && br_gr_rv.idle);
    for (uint32_t i = 0u; i < RV2_POOL16_LEN; ++i) assert(br_gr_pool16[i] == 0);   /* lines emptied */
    for (uint32_t i = 0u; i < RV2_POOL32_LEN; ++i) assert(br_gr_pool32[i] == 0);
    /* the return of the same phrase again matches the first within 2 dB: the lines
     * start empty, only the reverb's LFOs moved on, which shifts its response to one
     * held tone by about a dB (the output itself can differ more, as the dry note and
     * the early return then add with another phase) */
    wet_energy = 0.0;
    (void)tail_rms(0u, 5u, 20u);
    assert(first_wet > 0.0 && fabs(10.0 * log10(wet_energy / first_wet)) < 2.0);

    /* Turning a send to zero lets the tail ring out rather than cutting it. */
    setup(); send(0u, 100u);
    br_engine_note_on(0u, 60u, 100u);
    for (uint32_t i = 0u; i < 15u; ++i) (void)block_rms(0);
    br_engine_note_off(0u, 60u);
    for (uint32_t i = 0u; i < 20u; ++i) (void)block_rms(0);
    send(0u, 0u);
    { double sum = 0.0; for (uint32_t i = 0u; i < 20u; ++i) { double r = block_rms(0); sum += r * r; }
      assert(sqrt(sum / 20.0) > 0.5); }

    /* Sends on silent tracks cost nothing: the reverb stays idle until sound arrives. */
    setup(); send(0u, 100u); send(1u, 127u);
    for (uint32_t i = 0u; i < 40u; ++i) (void)block_rms(0);
    assert(br_gr_run == 0u);
    br_engine_note_on(0u, 60u, 100u);
    { uint32_t seen = 0u; for (uint32_t i = 0u; i < 6u; ++i) { (void)block_rms(0); seen |= br_gr_run; } assert(seen); }
    br_engine_note_off(0u, 60u);
    for (uint32_t i = 0u; i < 2500u && !(br_fx_quiet() && br_engine_quiet()); ++i) (void)block_rms(0);
    assert(br_fx_quiet() && br_engine_quiet());
    for (uint32_t i = 0u; i < 4u; ++i) (void)block_rms(0);
    assert(br_gr_run == 0u);

    /* No click when TYPE or SIZE changes under a held chord: the largest sample step of
     * the return in the 10 blocks after the change stays within 1.25x the largest step
     * of the 50 blocks before or the 50 blocks after (TYPE changes fade the return out
     * and in over one block each; SIZE changes slew the line lengths). */
    {
        static const uint32_t change[][4] = {           /* type, size -> type, size */
            {BR_REVERB_ROOM, 64u, BR_REVERB_HALL, 64u}, {BR_REVERB_HALL, 64u, BR_REVERB_SPACE, 64u},
            {BR_REVERB_SPACE, 64u, BR_REVERB_ROOM, 64u}, {BR_REVERB_ROOM, 64u, BR_REVERB_SPACE, 64u},
            {BR_REVERB_ROOM, 64u, BR_REVERB_ROOM, 127u}, {BR_REVERB_ROOM, 127u, BR_REVERB_ROOM, 0u},
            {BR_REVERB_HALL, 0u, BR_REVERB_HALL, 127u}, {BR_REVERB_SPACE, 127u, BR_REVERB_SPACE, 0u}};
        for (uint32_t k = 0u; k < sizeof change / sizeof change[0]; ++k) {
            double before = 0.0, at = 0.0, after = 0.0;
            int32_t last[2] = {0, 0};
            setup();
            for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) reverb_settings(p, change[k][0], 64u, change[k][1]);
            send(0u, 127u);
            br_engine_note_on(0u, 48u, 100u); br_engine_note_on(0u, 55u, 100u); br_engine_note_on(0u, 64u, 100u);
            for (uint32_t b = 0u; b < 300u; ++b) {
                double m = 0.0;
                if (b == 200u) for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) reverb_settings(p, change[k][2], 64u, change[k][3]);
                (void)block_rms(0);
                for (uint32_t c = 0u; c < 2u; ++c) {
                    const int32_t *w = wet_row(c);
                    for (uint32_t i = 0u; i < BR_BLOCK; ++i) { double d = fabs((double)w[i] - last[c]); if (d > m) m = d; last[c] = w[i]; }
                }
                if (b >= 150u && b < 200u && m > before) before = m;
                if (b >= 200u && b < 210u && m > at) at = m;
                if (b >= 250u && m > after) after = m;
            }
            assert(before > 500.0 && at < 1.25 * (before > after ? before : after));
        }
    }

    /* Mid-band RT60 per type at DECAY 0/64/127 (SIZE 64) within 10% of the prototype's
     * measured RT1k, from a 30 ms noise burst; the tails are stereo (inter-channel
     * correlation below 0.5) and their mono sum does not cancel (above -3 dB). */
    {
        static const double rt_proto[3][3] = {{0.25, 0.88, 5.97}, {0.79, 2.76, 19.61}, {2.62, 8.38, 57.20}};
        static const double seconds[3][3] = {{3, 8, 20}, {5, 12, 35}, {10, 25, 40}};
        static const uint32_t decays[3] = {0u, 64u, 127u};
        for (uint32_t k = 0u; k < 3u; ++k) for (uint32_t d = 0u; d < 3u; ++d) {
            long n = (long)(seconds[k][d] * 44100.0), onset = 100 + 1323;
            float *x = malloc(n * sizeof *x), *w = calloc(2 * (size_t)n, sizeof *w);
            double rt, mono, corr;
            setup_inject(types[k], 64u, decays[d]);
            lcg = 7u; burst(x, n);
            inject(x, n, w);
            rt = rt_1k(w, n, onset);
            printf("type %u decay %3u: RT1k %.2f s (prototype %.2f)", types[k], decays[d], rt, rt_proto[k][d]);
            assert(fabs(rt / rt_proto[k][d] - 1.0) < 0.10);
            if (decays[d] == 64u) {
                long a = onset + (long)(0.08 * 44100.0), b = onset + (long)((rt * 0.5 > 0.25 ? rt * 0.5 : 0.25) * 44100.0);
                corr = icc(w, a, b, &mono);
                printf(", ICC %+.2f, mono sum %+.1f dB", corr, mono);
                assert(corr < 0.5 && mono > -3.0);
            }
            printf("\n");
            free(x); free(w);
        }
    }

    /* Calibration through the app: at SEND 127 a held saw triad returns +6 dB +-1.5 over
     * its dry track (dry = send / 4 per channel, checked against the track's output),
     * power mean of ten triads over 0.5..2 s, every type at SIZE 64, DECAY 64. */
    {
        static const uint8_t triads[10][3] = {{48,52,55},{50,53,57},{52,55,59},{55,59,62},{57,60,64},
                                              {59,62,65},{60,64,67},{62,65,69},{64,67,71},{65,69,72}};
        long n = (long)(2.5 * 44100.0);
        float *x = malloc(n * sizeof *x), *w = calloc(2 * (size_t)n, sizeof *w);
        for (uint32_t k = 0u; k < 3u; ++k) {
            double ratio = 0.0;
            for (uint32_t c = 0u; c < 10u; ++c) {
                double sw = 0.0, sd = 0.0;
                chord(x, n, triads[c], 2.0);
                setup_inject(types[k], 64u, 64u);
                inject(x, n, w);
                for (long t = (long)(0.5 * 44100.0); t < 2 * 44100; ++t) {
                    sw += (double)w[2 * t] * w[2 * t] + (double)w[2 * t + 1] * w[2 * t + 1];
                    sd += 2.0 * ((double)x[t] / 4.0) * ((double)x[t] / 4.0);
                }
                ratio += sw / sd / 10.0;
            }
            printf("type %u: held triads at SEND 127 return %+.2f dB over the dry track\n", types[k], 10.0 * log10(ratio));
            assert(fabs(10.0 * log10(ratio) - 6.0) < 1.5);
        }
        /* the dry track is a quarter of the send per channel */
        {
            static int32_t bus[BR_PARTS][BR_BLOCK], out[BR_BLOCK * 2u];
            setup_inject(BR_REVERB_ROOM, 64u, 64u);
            memset(bus, 0, sizeof bus);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i) bus[0][i] = (int32_t)lrintf(x[i + 22050]);
            br_fx_process(bus, out, BR_BLOCK);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i)
                assert(br_fx_track_stereo[0][i][0] == bus[0][i] >> 2 && br_fx_track_stereo[0][i][1] == bus[0][i] >> 2);
        }
        free(x); free(w);
    }

    /* The keyboard plays on the FX and GLO pages. */
    setup();
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.part = 0u;
    br_key_down(5u, 1);
    assert(br_ui.held_note[5]);
    { double level = 0.0; for (uint32_t i = 0u; i < 30u; ++i) { double r = block_rms(0); if (r > level) level = r; } assert(level > 50.0); }
    br_key_up(5u); assert(!br_ui.held_note[5]);
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u;
    br_key_down(5u, 1); assert(br_ui.held_note[5]);        /* the GLO pages play the selected track too */
    br_key_up(5u);

    /* A click leaves a dense wash, not a few separate echoes: the 4 ms envelope of the
     * return changes smoothly (the old comb reverb measured 0.5 to 0.8 log10 units per
     * bin without input diffusion). */
    for (uint32_t k = 0u; k < 3u; ++k) {
        static double env[200];
        setup(); send(0u, 127u); reverb_settings(0u, types[k], 64u, 64u);
        br_load_preset(0u, BR_FIRST_PLAITS + 23u);
        br_part[0].revision++; br_part[0].sound.decay = 0u; br_part[0].sound.sustain = 0u; br_part[0].sound.release = 0u; publish(0u);
        uint32_t onset = 0u, count = 0u; double sum = 0.0; uint32_t bin = 0u;
        for (uint32_t b = 0u; b < 80u; ++b) {                /* SPACE starts ~0.11 s late (pre-delay) */
            if (b == 4u) br_engine_note_on(0u, 60u, 127u);
            if (b == 5u) br_engine_note_off(0u, 60u);
            (void)block_rms(0);
            const int32_t *wl = wet_row(0u);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
                double v = wl[i];
                if (!onset && fabs(v) > 3.0) onset = 1u, count = 0u, sum = 0.0, bin = 0u;
                if (!onset) continue;
                sum += v * v;
                if (++count == 176u) { if (bin < 100u) env[bin++] = sqrt(sum / 176.0); count = 0u; sum = 0.0; }
            }
        }
        double rough = 0.0;
        printf("type %u: wash onset ok, %u envelope bins\n", types[k], bin);
        assert(bin >= 60u);
        for (uint32_t i = 1u; i < 60u; ++i) rough += fabs(log10(env[i] + 1e-3) - log10(env[i - 1u] + 1e-3));
        printf("type %u: return envelope roughness %.3f log10/bin\n", types[k], rough / 59.0);
        assert(rough / 59.0 < 0.25);
    }

    /* The modulation is slow in every type: both LFOs under 0.6 Hz. */
    for (uint32_t k = 0u; k < 3u; ++k) {
        rv2_coeffs_t co;
        rv2_design(types[k], 64u, 127u, 0, &co);
        assert(co.lfo_inc[0] * 22050.0 / 4294967296.0 < 0.6 && co.lfo_inc[1] * 22050.0 / 4294967296.0 < 0.6);
    }

    /* Every type stays bounded at its longest decay and extreme sizes, through loud notes
     * and switches of type: the return never exceeds the reverb's +-65,535 clamp. */
    {
        double peak = 0.0;
        setup(); send(0u, 127u); send(1u, 127u);
        for (uint32_t round = 0u; round < 6u; ++round) {
            uint32_t sizes[3] = {0u, 127u, 64u};
            reverb_settings(0u, BR_REVERB_SPACE, 127u, sizes[round % 3u]);
            if (round == 3u) reverb_settings(0u, BR_REVERB_HALL, 127u, 64u);
            if (round == 4u) reverb_settings(0u, BR_REVERB_ROOM, 127u, 100u);
            br_engine_note_on(0u, 48u, 127u); br_engine_note_on(1u, 55u, 127u);
            for (uint32_t i = 0u; i < 200u; ++i) {
                (void)block_rms(0);
                for (uint32_t c = 0u; c < 2u; ++c) {
                    const int32_t *wr = wet_row(c);
                    for (uint32_t j = 0u; j < BR_BLOCK; ++j) { double a = fabs((double)wr[j]); if (a > peak) peak = a; }
                }
            }
            br_engine_note_off(0u, 48u); br_engine_note_off(1u, 55u);
            for (uint32_t i = 0u; i < 100u; ++i) (void)block_rms(0);
        }
        assert(peak > 100.0 && peak <= 65535.0);
    }

    /* After a boot or project load (restore, FX init, warm controls) the reverb is heard at once. */
    for (uint32_t k = 0u; k < 3u; ++k) {
        br_session_t saved;
        setup(); send(0u, 127u); send(1u, 90u); reverb_settings(0u, types[k], 40u, 70u);
        assert(br_capture(&saved));
        setup();
        assert(br_restore(&saved));
        br_fx_init(); br_engine_init();
        if (!br_playing && br_engine_quiet() && br_fx_quiet()) br_fx_warm_controls_quiescent();
        br_engine_note_on(0u, 60u, 100u);
        double sum = 0.0; uint32_t seen = 0u;
        for (uint32_t i = 0u; i < 80u; ++i) {
            (void)block_rms(0);
            if (i >= 30u) for (uint32_t c = 0u; c < 2u; ++c) {
                const int32_t *wr = wet_row(c);
                for (uint32_t j = 0u; j < BR_BLOCK; ++j) sum += (double)wr[j] * wr[j];
            }
            seen |= br_gr_run;
        }
        assert(seen && br_gr_key_valid && br_gr_key[0] == types[k] && sqrt(sum / (50.0 * BR_BLOCK * 2.0)) > 100.0);
    }

#if defined(BR_REVERB_AUDIO_ONLY) && BR_REVERB_AUDIO_ONLY
    puts("PASS: shared reverb audio checks (screen checks not run)");
    return 0;
#endif
    /* The mixer and the FX send bars are different screens: switching between them repaints.
     * (SMK-37 Pro: after its startup intro, as on the device.) */
    setup(); host_after_intro();
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u; br_ui.redraw = 1u; br_draw();
    uint32_t mixer_layout = br_draw_layout_old;
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
    assert(br_draw_layout_old != mixer_layout);
    br_ui.page = BR_PAGE_MIX; br_ui.redraw = 1u; br_draw();
    assert(br_draw_layout_old == mixer_layout);

    /* Entering FX sends from the mixer must match entry from another page;
     * the current FX sends screen has its own cue in the bottom rows. */
    {
        uint32_t white_mixer = 0u, white_drive = 0u;
        uint16_t fx_from_mixer[13u * 240u];
        br_ui.part = 0u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u; br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 227u; y < 240u; ++y) for (uint32_t x = 0u; x < 240u; ++x) white_mixer += fb[y * 240u + x] == 0xffffu;
        br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
        memcpy(fx_from_mixer, &fb[227u * 240u], sizeof fx_from_mixer);
        br_ui.page = BR_PAGE_MIX; br_ui.redraw = 1u; br_draw();
        br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_DRIVE; br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 227u; y < 240u; ++y) for (uint32_t x = 0u; x < 240u; ++x) white_drive += fb[y * 240u + x] == 0xffffu;
        assert(white_mixer > 40u && white_drive == 0u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
        assert(!memcmp(fx_from_mixer, &fb[227u * 240u], sizeof fx_from_mixer));
    }

    /* The REVERB screen has a goldenrod GLOBAL badge in its header. HD draws the badge as a raised
     * chip (hdc_chip: a dithered gradient from a lighter to a darker shade of its colour), so its
     * pixels are counted within 6 RGB565 steps of the colour (hd_look.h hd_near), not exactly. */
    {
        uint32_t gold_tag = 0u, gold_track = 0u;
        uint16_t gold = BR_GLOBAL_ACC;
        br_ui.part = 1u; br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.redraw = 1u; br_draw();
        assert(br_ui_accent == BR_GLOBAL_ACC);
        for (uint32_t y = 46u; y < 62u; ++y) for (uint32_t x = 80u; x < 160u; ++x)
            gold_tag += hd_near(fb[y * 240u + x], gold, 6u, 0);
        assert(gold_tag > 200u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_DELAY; br_ui.redraw = 1u; br_draw();
        assert(br_ui_accent == br_track_color(1u) && br_ui_accent != BR_GLOBAL_ACC);
        for (uint32_t y = 46u; y < 62u; ++y) for (uint32_t x = 80u; x < 160u; ++x)
            gold_track += hd_near(fb[y * 240u + x], gold, 6u, 0);
        assert(gold_track == 0u);
        /* The track's own effect screens carry a TRACK tag in the track's colour. */
        for (uint32_t screen = BR_FX_DRIVE; screen <= BR_FX_MOD; ++screen) {
            uint32_t tag = 0u; uint16_t c = br_track_color(1u);
            br_ui.screen[BR_PAGE_FX] = screen; br_ui.redraw = 1u; br_draw();
            for (uint32_t y = 46u; y < 62u; ++y) for (uint32_t x = 80u; x < 160u; ++x)
                tag += hd_near(fb[y * 240u + x], c, 6u, 0);
            assert(tag > 200u);
        }
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
    }

    /* Send levels walk to a new value a step per block, so a quick knob turn is a ramp, not a click. */
    {
        int32_t a[BR_BLOCK * 2u];
        uint32_t last[3], now[3], reached[3] = {0u, 0u, 0u};
        setup(); br_engine_note_on(0u, 60u, 100u);
        for (int blk = 0; blk < 6; ++blk) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
        br_part[0].revision++; br_part[0].sound.delay = 100u; br_part[0].sound.reverb = 100u; br_part[0].fx.mod_mix = 90u;
        publish(0);
        last[0] = br_effective_sound[0].delay; last[1] = br_effective_sound[0].reverb; last[2] = br_effective_fx[0].mod_mix;
        assert(!last[0] && !last[1] && !last[2]);
        for (int blk = 0; blk < 140; ++blk) {
            memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
            now[0] = br_effective_sound[0].delay; now[1] = br_effective_sound[0].reverb; now[2] = br_effective_fx[0].mod_mix;
            assert(now[0] - last[0] <= 1u && now[1] - last[1] <= 2u && now[2] - last[2] <= 1u);   /* one step at most */
            for (int k = 0; k < 3; ++k) { if (now[k] == (k == 2 ? 90u : 100u) && !reached[k]) reached[k] = (uint32_t)blk + 1u; last[k] = now[k]; }
        }
        assert(reached[0] > 90u && reached[1] > 40u && reached[2] > 80u);                             /* a ramp, not a jump */
        assert(last[0] == 100u && last[1] == 100u && last[2] == 90u);                                 /* and it arrives */
        br_part[0].revision++; br_part[0].sound.delay = 0u; publish(0);                              /* down again, to exactly zero */
        for (int blk = 0; blk < 140; ++blk) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
        assert(br_effective_sound[0].delay == 0u);
    }

    /* The REVERB screen edits one shared setting from any track; only SEND is per track. */
    setup();
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.part = 2u;
    {
        br_control_turn(1u, 7); br_control_turn(2u, -5);
        assert(br_master.reverb_size == 71u && br_master.reverb_decay == 35u);   /* one global home */
        br_control_turn(0u, 1);
        assert(br_master.reverb_engine != BR_REVERB_ETHEREAL);
        br_control_turn(3u, 3);
        assert(!strcmp(br_control(3u).label, "PRE DELAY") && br_control(3u).value == 3);
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
        br_control_turn(3u, 40);
        assert(br_part[2].sound.reverb == 40u && !br_part[0].sound.reverb && !br_part[1].sound.reverb);
        br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB;
        br_ui.part = 0u;                      /* every track shows the same shared values */
        assert(br_control(1u).value == 71 && br_control(2u).value == 35);
        br_ui.part = 2u;
        assert(br_control(1u).value == 71 && !strcmp(br_control(3u).label, "PRE DELAY"));
        br_ui.redraw = 1u; br_draw();
    }

    printf("shared reverb: send, sum, track-1 settings, every type, tail and quiet checks passed\n");
    return 0;
}
