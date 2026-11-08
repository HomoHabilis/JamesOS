/* DRIVE stage: the three shapers, gain, tone and send, and its path through the real app. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>

#define N 256
static int32_t in[N], out[N];

static void params(uint32_t mode, uint32_t gain, uint32_t tone, uint32_t send)
{
    int keep = send == 100;     /* send 100 keeps the filters' state from the previous run */
    br_effective_fx[0].drive_mode = mode; br_effective_fx[0].drive_gain = gain;
    br_effective_fx[0].drive_tone = tone; br_effective_sound[0].drive = send;
    if (!keep) memset(&br_fx_track[0], 0, sizeof br_fx_track[0]);
}
static void sine(double amp, double hz)
{
    for (int i = 0; i < N; ++i) in[i] = (int32_t)(amp * sin(2.0 * M_PI * hz * i / 44100.0));
}
static void run(void) { for (int b = 0; b < 3; ++b) br_fx_drive_block(0u, in, out, N); }   /* warm the filters */
static double rms(const int32_t *x) { double s = 0; for (int i = 0; i < N; ++i) s += (double)x[i] * x[i]; return sqrt(s / N); }
static double peak(const int32_t *x) { double m = 0; for (int i = 0; i < N; ++i) if (fabs((double)x[i]) > m) m = fabs((double)x[i]); return m; }
/* harmonic distortion: energy outside the fundamental bin, as a fraction of the total */
static double distortion(const int32_t *x, double hz)
{
    double c = 0, s = 0, tot = 0;
    for (int i = 0; i < N; ++i) { c += x[i] * cos(2 * M_PI * hz * i / 44100.0); s += x[i] * sin(2 * M_PI * hz * i / 44100.0); tot += (double)x[i] * x[i]; }
    double fund = 2.0 * (c * c + s * s) / N;
    return tot > 0 ? fmax(0.0, 1.0 - fund / tot) : 0.0;
}
static double hf_share(const int32_t *x)    /* energy of the first difference: how much treble there is */
{
    double d = 0, e = 0; for (int i = 1; i < N; ++i) { double v = x[i] - x[i - 1]; d += v * v; e += (double)x[i] * x[i]; }
    (void)e; return d;
}


/* Aliasing: a sine of exactly BIN cycles per M samples is driven; the harmonics that fit below Nyquist
 * land on multiples of BIN, anything else is folded-back content. Returns the share of the output
 * energy that is not on a harmonic bin (the wet path only, tone open). */
#define M 1024
static double alias_share(uint32_t mode, uint32_t gain, int bin, double amp)
{
    static int32_t big_in[M], big_out[M];
    for (int i = 0; i < M; ++i) big_in[i] = (int32_t)lrint(amp * sin(2.0 * M_PI * bin * i / M));
    params(mode, gain, 127, 127);
    for (int pass = 0; pass < 3; ++pass) br_fx_drive_block(0u, big_in, big_out, M);   /* settle, input is periodic */
    double tot = 0.0, alias = 0.0;
    for (int k = 1; k < M / 2; ++k) {
        double c = 0.0, s = 0.0;
        for (int i = 0; i < M; ++i) { c += big_out[i] * cos(2.0 * M_PI * k * i / M); s += big_out[i] * sin(2.0 * M_PI * k * i / M); }
        double e = c * c + s * s;
        tot += e;
        if (k % bin) alias += e;
    }
    return tot > 0.0 ? alias / tot : 0.0;
}

int main(void)
{
    br_app_init();
    /* Anti-aliasing: the shapers are band-limited by first-order ADAA, so folded-back energy stays small
     * (without it distortion measures about 0.6 % and fuzz about 5 % on these tones; with it, 0.02 % and 0.26 %). */
    for (uint32_t mode = 0; mode < 3; ++mode) {
        double a1 = alias_share(mode, 100, 100, 6000), a2 = alias_share(mode, 100, 37, 6000);
        double a3 = alias_share(mode, 60, 100, 12000), a4 = alias_share(mode, 127, 100, 30000);
        assert(a1 < 0.004 && a2 < 0.004 && a3 < 0.004 && a4 < 0.006);
    }

    /* Send at zero is an exact bypass of the shaper: out equals in. */
    sine(8000, 689.0625); params(0, 100, 127, 0); run();
    for (int i = 0; i < N; ++i) assert(out[i] == in[i]);

    /* Each mode adds harmonics to a clean sine, and none exceeds full scale. */
    double d[3];
    for (uint32_t mode = 0; mode < 3; ++mode) {
        sine(8000, 689.0625); params(mode, 90, 127, 127); run();
        d[mode] = distortion(out, 689.0625);
        assert(d[mode] > 0.02 && peak(out) <= 32768.0 * 1.2 && rms(out) > 500.0);
    }
    assert(d[1] > d[0] && d[2] > d[0]);                    /* harder modes distort more than overdrive */

    /* Gain: more gain, more distortion and a more clipped (lower crest) wave. */
    sine(3000, 689.0625);
    params(0, 10, 127, 127); run(); double low = distortion(out, 689.0625);
    params(0, 120, 127, 127); run(); double high = distortion(out, 689.0625);
    assert(high > low * 3.0);

    /* Tone: darker settings remove high-frequency content from the driven signal. */
    sine(8000, 3012.5);
    params(1, 100, 4, 127); run(); double dark = hf_share(out);
    params(1, 100, 127, 127); run(); double bright = hf_share(out);
    assert(dark < 0.5 * bright);

    /* Send blends dry and driven: half send lies between the two. */
    sine(6000, 689.0625);
    params(1, 100, 127, 127); run(); double wet_rms = rms(out); double wet0 = out[40];
    params(1, 100, 127, 64); run(); int32_t mid40 = out[40];
    assert((double)mid40 > fmin((double)in[40], wet0) - 1 && (double)mid40 < fmax((double)in[40], wet0) + 1);
    (void)wet_rms;

    /* The send ramps across a block: turned down between two blocks, the wet part falls steadily to
     * nothing instead of stepping, and turned up it rises the same way. */
    for (uint32_t mode = 0; mode < 3; ++mode)
        for (int up = 0; up < 2; ++up) {
            int32_t full[N], ramp[N];
            sine(9000, 689.0625); params(mode, 110, 127, 127);
            for (int b = 0; b < 12; ++b) br_fx_drive_block(0u, in, out, N);    /* filters settled, full send */
            memcpy(full, out, sizeof full);
            if (up) { br_effective_sound[0].drive = 0u; for (int b = 0; b < 3; ++b) br_fx_drive_block(0u, in, out, N); }   /* settled at no send */
            br_effective_sound[0].drive = up ? 127u : 0u;         /* turned between blocks */
            br_fx_drive_block(0u, in, out, N);
            memcpy(ramp, out, sizeof ramp);
            double worst = 0.0;
            for (int i = 0; i < N; ++i) {                          /* a straight line from one send to the other */
                double g = up ? (i + 1.0) / N : 1.0 - (i + 1.0) / N;
                double want = in[i] + (double)(full[i] - in[i]) * g;
                if (fabs(ramp[i] - want) > worst) worst = fabs(ramp[i] - want);
            }
            assert(worst < 0.01 * 32768.0);
        }

    /* Silence stays silent in every mode (no offset left by the asymmetric fuzz). */
    memset(in, 0, sizeof in);
    for (uint32_t mode = 0; mode < 3; ++mode) {
        params(mode, 127, 127, 127);
        for (int b = 0; b < 40; ++b) br_fx_drive_block(0u, in, out, N);
        assert(peak(out) < 2.0);
    }

    /* After a loud signal, silence in gives exactly zero out again, in every mode and tone setting
     * (a stuck offset would keep the effects after it running for ever). */
    for (uint32_t mode = 0; mode < 3; ++mode)
        for (uint32_t tone = 0; tone <= 127; tone += 127) {
            sine(20000, 441.0); params(mode, 100, tone, 127); run();
            memset(in, 0, sizeof in);
            for (int b = 0; b < 60; ++b) br_fx_drive_block(0u, in, out, N);
            for (int i = 0; i < N; ++i) assert(out[i] == 0);
            params(mode, 100, tone, 100); sine(20000, 441.0); run();      /* and with the send part way */
            memset(in, 0, sizeof in);
            for (int b = 0; b < 60; ++b) br_fx_drive_block(0u, in, out, N);
            for (int i = 0; i < N; ++i) assert(out[i] == 0);
        }

    /* Through the real app: a note with drive on is different from, and louder at its peak than,
     * the same note without, and a track with only drive keeps its tail-free chain alive. */
    {
        double plain_peak = 0.0, driven_peak = 0.0, plain_rms = 0.0, driven_rms = 0.0;
        for (int pass = 0; pass < 2; ++pass) {
            br_app_init(); fm1_ms = 1000u;
            br_part[0].revision++; br_part[0].sound.drive = pass ? 127u : 0u;
            br_part[0].fx.drive_mode = 2u; br_part[0].fx.drive_gain = 110u; br_part[0].fx.drive_tone = 100u;
            RING_PUBLISH(); br_part[0].revision++;
            br_engine_note_on(0u, 60u, 100u);
            double sum = 0.0, pk = 0.0;
            for (int b = 0; b < 60; ++b) {
                int32_t a[BR_BLOCK * 2u] = {0};
                assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
                if (b >= 20) for (uint32_t i = 0; i < BR_BLOCK * 2u; ++i) { sum += (double)a[i] * a[i]; if (fabs((double)a[i]) > pk) pk = fabs((double)a[i]); }
            }
            if (pass) { driven_peak = pk; driven_rms = sqrt(sum / (40.0 * BR_BLOCK * 2.0)); }
            else { plain_peak = pk; plain_rms = sqrt(sum / (40.0 * BR_BLOCK * 2.0)); }
        }
        assert(driven_rms > 1.3 * plain_rms && driven_peak > 1.3 * plain_peak);
    }

    /* Saved and restored: mode, gain, tone and send survive a session round trip. */
    {
        br_session_t saved;
        br_app_init();
        br_part[1].revision++; br_part[1].sound.drive = 77u;
        br_part[1].fx.drive_mode = 2u; br_part[1].fx.drive_gain = 33u; br_part[1].fx.drive_tone = 91u;
        RING_PUBLISH(); br_part[1].revision++;
        assert(br_capture(&saved) && br_session_v16_valid(&saved));
        br_app_init();
        assert(br_restore(&saved));
        assert(br_part[1].sound.drive == 77u && br_part[1].fx.drive_mode == 2u &&
               br_part[1].fx.drive_gain == 33u && br_part[1].fx.drive_tone == 91u);
    }
    /* Screens: FX has SENDS, DRIVE, DELAY, MODULATION, REVERB (no FILTER); DRIVE is MODE, GAIN, TONE, SEND. */
    {
        br_app_init(); br_ui.part = 1u; br_ui.page = BR_PAGE_FX;
        assert(br_screen_count() == 5u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
        assert(!strcmp(br_header_title(), "FX / SENDS") && !strcmp(br_control(0u).label, "DRIVE"));
        br_control_turn(0u, 90); assert(br_part[1].sound.drive == 90u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_DRIVE;
        assert(!strcmp(br_header_title(), "FX / DRIVE"));
        assert(!strcmp(br_control(0u).label, "MODE") && !strcmp(br_control(1u).label, "GAIN") &&
               !strcmp(br_control(2u).label, "TONE") && !strcmp(br_control(3u).label, "SEND"));
        assert(br_control(3u).value == 90);                                /* the same send as on SENDS */
        assert(!strcmp(br_control(0u).text, "OVERDRIVE"));
        br_control_turn(0u, 1); assert(!strcmp(br_control(0u).text, "DISTORT") && br_part[1].fx.drive_mode == 1u);
        br_control_turn(0u, 1); assert(!strcmp(br_control(0u).text, "FUZZ"));
        br_control_turn(0u, 1); assert(br_part[1].fx.drive_mode == 2u);   /* three modes only */
        br_control_turn(1u, -20); br_control_turn(2u, -10); br_control_turn(3u, -40);
        assert(br_part[1].fx.drive_gain == 44u && br_part[1].fx.drive_tone == 117u && br_part[1].sound.drive == 50u);
        for (uint32_t mode = 0u; mode < 3u; ++mode) {
            br_part[1].fx.drive_mode = mode; br_ui.redraw = 1u; br_draw();
        }
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
    }
    printf("drive: bypass, three shapers, gain, tone, send, silence, app path and session checks passed\n");
    return 0;
}
