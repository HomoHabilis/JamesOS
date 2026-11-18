/* DRIVE stage: the three shapers, gain, tone and send (a parallel, level-matched law: the dry stays at
 * unity and the wet is added, so drive never makes a track quieter), and its path through the real app. */
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
/* the block as the app uses it: a silent block leaves out alone and the app passes in on */
static void drive_blk(const int32_t *x, int32_t *y, uint32_t n)
{
    if (!br_fx_drive_block(0u, x, y, n)) memcpy(y, x, n * sizeof *y);
}
static void run(void) { for (int b = 0; b < 3; ++b) drive_blk(in, out, N); }   /* warm the filters */
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
static int32_t wet[N];
static const int32_t *wet_of(void) { for (int i = 0; i < N; ++i) wet[i] = out[i] - in[i]; return wet; }
static double bin_level(const int32_t *x, double hz)   /* |DFT| at hz (an integer number of cycles per N) */
{
    double c = 0, s = 0;
    for (int i = 0; i < N; ++i) { c += x[i] * cos(2 * M_PI * hz * i / 44100.0); s += x[i] * sin(2 * M_PI * hz * i / 44100.0); }
    return sqrt(c * c + s * s);
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
    for (int pass = 0; pass < 3; ++pass) drive_blk(big_in, big_out, M);   /* settle, input is periodic */
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
        d[mode] = distortion(wet_of(), 689.0625);
        assert(d[mode] > 0.02 && peak(out) <= 32768.0 * 1.2 && rms(out) > rms(in));
    }
    assert(d[1] > d[0] && d[2] > d[0]);                    /* harder modes distort more than overdrive */

    /* Gain: more gain, more distortion and a more clipped (lower crest) wave. */
    sine(3000, 689.0625);
    params(0, 10, 127, 127); run(); double low = distortion(wet_of(), 689.0625);
    params(0, 120, 127, 127); run(); double high = distortion(wet_of(), 689.0625);
    assert(high > low * 3.0);
    /* FUZZ's gain knob matters (2x to 100x): soft at 0, a square at 127. */
    sine(8000, 689.0625);
    params(2, 0, 127, 127); run(); low = distortion(wet_of(), 689.0625);
    params(2, 127, 127, 127); run(); high = distortion(wet_of(), 689.0625);
    assert(low < 0.02 && high > 0.15);
    /* DISTORT is asymmetric: clear even harmonics (H2 about -18 dB at gain 64, -12 dBFS), and no DC;
     * OVERDRIVE stays symmetric (no H2). */
    sine(8192, 689.0625);
    params(1, 64, 127, 127); run();
    { double h2 = 20.0 * log10(bin_level(wet_of(), 2 * 689.0625) / bin_level(wet, 689.0625)), dc = 0;
      for (int b = 0; b < 20; ++b) run();
      for (int i = 0; i < N; ++i) dc += (double)out[i] - in[i];
      assert(h2 > -22.0 && h2 < -13.0 && fabs(dc / N) < 4.0); }
    params(0, 64, 127, 127); run();
    assert(bin_level(wet_of(), 2 * 689.0625) < 1e-3 * bin_level(wet, 689.0625));

    /* Tone: darker settings remove high-frequency content from the driven signal. */
    sine(8000, 3012.5);
    params(1, 100, 4, 127); run(); double dark = hf_share(wet_of());
    params(1, 100, 127, 127); run(); double bright = hf_share(wet_of());
    assert(dark < 0.5 * bright);
    /* ... and TONE 127 is open (pole coefficient 0.987: -0.2 dB at 20 kHz); the table rises to it
     * without a jump. */
    for (int k = 0; k < 16; ++k) assert(br_drive_gain_q15[k] < br_drive_gain_q15[k + 1] && br_drive_gain_q15[k + 1] - br_drive_gain_q15[k] < 3500u);
    assert(br_drive_lerp17(br_drive_gain_q15, 127) >= 32300u && br_drive_lerp17(br_drive_gain_q15, 127) <= 32768u);

    /* Never quieter: the dry stays at unity and the level-matched wet is added, so on a hot input
     * (-1 dBFS, above every shaper's ceiling) and a quiet one (-24 dBFS) the level rises with the send,
     * in every mode and at every gain; on the hot one by +1 to +5.5 dB at full send (a quiet input at
     * high gain may rise more: M is limited to -6 dB). */
    for (int hot = 0; hot < 2; ++hot)
        for (uint32_t mode = 0; mode < 3; ++mode)
            for (uint32_t g = 0; g <= 127; g += 127) {
                double last = 0.0;
                sine(hot ? 29204.0 : 2068.0, 689.0625);
                for (uint32_t send = 32; send <= 128; send += 32) {
                    params(mode, g, 127, send > 127 ? 127 : send);
                    for (int b = 0; b < 16; ++b) drive_blk(in, out, N);   /* M settled */
                    double gain_db = 20.0 * log10(rms(out) / rms(in));
                    assert(gain_db > last - 0.05 && gain_db > 0.0);
                    if (getenv("DRIVE_VERBOSE")) printf("hot %d mode %u gain %u send %u: %+.2f dB\n", hot, mode, g, send, gain_db);
                    if (send > 127) assert(gain_db > 1.0 && (gain_db < 5.5 || !hot));
                    last = gain_db;
                }
            }

    /* The send ramps across a block: turned down between two blocks, the wet part falls steadily to
     * nothing instead of stepping, and turned up it rises the same way. */
    for (uint32_t mode = 0; mode < 3; ++mode)
        for (int up = 0; up < 2; ++up) {
            int32_t full[N], ramp[N];
            sine(9000, 689.0625); params(mode, 110, 127, 127);
            for (int b = 0; b < 12; ++b) drive_blk(in, out, N);    /* filters settled, full send */
            memcpy(full, out, sizeof full);
            if (up) { br_effective_sound[0].drive = 0u; for (int b = 0; b < 3; ++b) drive_blk(in, out, N); }   /* settled at no send */
            br_effective_sound[0].drive = up ? 127u : 0u;         /* turned between blocks */
            drive_blk(in, out, N);
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
        for (int b = 0; b < 40; ++b) drive_blk(in, out, N);
        assert(peak(out) < 2.0);
    }

    /* After a loud signal, silence in gives exactly zero out again, in every mode and tone setting
     * (a stuck offset would keep the effects after it running for ever). */
    for (uint32_t mode = 0; mode < 3; ++mode)
        for (uint32_t tone = 0; tone <= 127; tone += 127) {
            sine(20000, 441.0); params(mode, 100, tone, 127); run();
            memset(in, 0, sizeof in);
            for (int b = 0; b < 60; ++b) drive_blk(in, out, N);
            for (int i = 0; i < N; ++i) assert(out[i] == 0);
            params(mode, 100, tone, 100); sine(20000, 441.0); run();      /* and with the send part way */
            memset(in, 0, sizeof in);
            for (int b = 0; b < 60; ++b) drive_blk(in, out, N);
            for (int i = 0; i < N; ++i) assert(out[i] == 0);
        }

    /* Through the real app (the KNOWN-FAILURES case: a hot Plaits note, fuzz at gain 110): drive on is
     * louder than drive off, in RMS and at the peak, never quieter. */
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

    /* Through the real app, the send turned to 0 between blocks: the stage runs one more block and fades
     * the wet out over it (no step: no sample-to-sample change larger than the driven or plain note's
     * own), then stops (drive_send back at 0) and the output is exactly the plain note's. */
    {
        static int32_t plain[70][BR_BLOCK * 2u], cut[70][BR_BLOCK * 2u];
        double plain_step = 0.0, driven_step = 0.0, cut_step = 0.0;
        for (int pass = 0; pass < 2; ++pass) {
            br_app_init(); fm1_ms = 1000u;
            br_part[0].revision++; br_part[0].sound.drive = pass ? 127u : 0u;
            br_part[0].fx.drive_mode = 2u; br_part[0].fx.drive_gain = 110u; br_part[0].fx.drive_tone = 100u;
            RING_PUBLISH(); br_part[0].revision++;
            br_engine_note_on(0u, 60u, 100u);
            for (int b = 0; b < 70; ++b) {
                if (pass && b == 40) { br_part[0].revision++; br_part[0].sound.drive = 0u; RING_PUBLISH(); br_part[0].revision++; }
                int32_t *a = pass ? cut[b] : plain[b];
                memset(a, 0, sizeof plain[0]);
                assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
            }
            assert(br_fx_track[0].drive_send == 0 && (!pass || br_fx_track[0].drive_wet == 0.0f));
        }
        for (int b = 20; b < 70; ++b)
            for (uint32_t i = 2u; i < BR_BLOCK * 2u; i += 2u) {
                double dp = fabs((double)plain[b][i] - plain[b][i - 2u]), dc = fabs((double)cut[b][i] - cut[b][i - 2u]);
                if (dp > plain_step) plain_step = dp;
                if (b < 38 && dc > driven_step) driven_step = dc;
                if (b >= 38 && dc > cut_step) cut_step = dc;
            }
        assert(cut_step <= 1.05 * fmax(plain_step, driven_step));
        for (int b = 44; b < 70; ++b) assert(!memcmp(plain[b], cut[b], sizeof plain[b]));
    }

    /* Bias changes the sound of each drive engine while the legacy zero remains default. */
    for (uint32_t mode = 0; mode < 3; ++mode) {
        int32_t baseline[N];
        sine(8000, 689.0625);
        br_part_pending[0].drive_bias = 0; params(mode, 64, 127, 127); run(); memcpy(baseline, out, sizeof baseline);
        br_part_pending[0].drive_bias = 100; params(mode, 64, 127, 127); run();
        double difference = 0; for (int i = 0; i < N; ++i) difference += fabs((double)baseline[i] - out[i]);
        assert(difference > 1000);
    }
    br_part_pending[0].drive_bias = 0;
#if BR_FX_DELAY_RATE2
    /* Delay tone works on all four delay engines, including multitap and glitch. */
    for (uint32_t engine = 0; engine < BR_DELAY_COUNT; ++engine) {
        static int32_t reference[8192]; br_fx_controls_t controls;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            memset(&br_fx_track[0], 0, sizeof br_fx_track[0]); memset(BR_POLY_DELAY_LINE(0), 0, BR_FX_DELAY_SAMPLES * sizeof(int16_t));
            br_effective_fx[0].delay_engine = engine; br_effective_fx[0].delay_feedback = 57;
            br_part_pending[0].delay_tone = pass ? 0 : 127;
            br_fx_prepare_track_controls(0, &br_effective_sound[0], &br_effective_fx[0], &controls, 120);
            controls.delay_storage_shift = 0; controls.delay_time = 512;
            for (uint32_t t = 0; t < 4; ++t) controls.delay_tap[t] = 256 * (t+1);
            double difference = 0;
            for (int i = 0; i < 8192; ++i) {
                int32_t x = i < 512 ? (int32_t)(10000.0 * sin(2.0 * M_PI * 5000.0 * i / 22050.0)) : 0;
                int32_t y = br_fx_delay_loop(&br_fx_track[0], 0, x, &controls);
                if (!pass) reference[i] = y; else difference += fabs((double)y - reference[i]);
            }
            if (pass) assert(difference > 10000);
        }
    }
    br_part_pending[0].delay_tone = 127;
#endif
    /* Modulation feedback changes the wet output for every engine, including the phaser. */
    for (uint32_t engine = 0; engine < BR_MOD_COUNT; ++engine) {
        static int32_t reference[8192];
        br_fx_controls_t controls;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            memset(&br_fx_track[0], 0, sizeof br_fx_track[0]);
            memset(br_mod_line[0], 0, sizeof br_mod_line[0]);
            br_effective_fx[0].mod_engine = engine; br_effective_fx[0].mod_depth = 64;
            br_effective_fx[0].mod_rate = 40; br_effective_fx[0].mod_mix = 127;
            br_part_pending[0].mod_feedback = pass ? 127 : 0;
            br_fx_prepare_track_controls(0, &br_effective_sound[0], &br_effective_fx[0], &controls, 120);
            double difference = 0;
            for (int i = 0; i < 8192; ++i) {
                int32_t x = (int32_t)(10000.0 * sin(2.0 * M_PI * 400.0 * i / 44100.0));
                int32_t y = br_fx_mod(&br_fx_track[0], 0, x, &controls);
                if (!pass) reference[i] = y; else difference += fabs((double)y - reference[i]);
            }
            if (pass) assert(difference > 10000);
        }
    }
    br_part_pending[0].mod_feedback = 0;
#if BR_FX_BLOCK_STAGE_PIPELINE && BR_FX_BLOCK_STAGE_SPECIALIZE_MOD
    /* The device's block-optimized path must respond too. */
    for (uint32_t engine = 0; engine < BR_MOD_COUNT; ++engine) {
        static int32_t reference[8192], block_in[N], block_out[N];
        br_fx_controls_t controls;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            memset(&br_fx_track[0], 0, sizeof br_fx_track[0]); memset(br_mod_line[0], 0, sizeof br_mod_line[0]);
            br_effective_fx[0].mod_engine = engine; br_effective_fx[0].mod_depth = 64;
            br_effective_fx[0].mod_rate = 40; br_effective_fx[0].mod_mix = 127;
            br_part_pending[0].mod_feedback = pass ? 127 : 0;
            br_fx_prepare_track_controls(0, &br_effective_sound[0], &br_effective_fx[0], &controls, 120);
            double difference = 0;
            for (int offset = 0; offset < 8192; offset += N) {
                for (int i = 0; i < N; ++i) block_in[i] = (int32_t)(10000.0 * sin(2.0 * M_PI * 400.0 * (offset+i) / 44100.0));
                br_fx_stage_mod_block(&br_fx_track[0], 0, 0, 0, block_in, N, &controls, block_out);
                for (int i = 0; i < N; ++i) if (!pass) reference[offset+i] = block_out[i]; else difference += fabs((double)block_out[i] - reference[offset+i]);
            }
            if (pass) assert(difference > 10000);
        }
    }
    br_part_pending[0].mod_feedback = 0;
#endif
    /* Saved and restored: mode, gain, tone, bias and send survive a session round trip. */
    {
        br_session_t saved;
        br_app_init();
        br_part[1].revision++; br_part[1].sound.drive = 77u;
        br_part[1].fx.drive_mode = 2u; br_part[1].fx.drive_gain = 33u; br_part[1].fx.drive_tone = 91u; br_part_pending[1].drive_bias = 87u;
        RING_PUBLISH(); br_part[1].revision++;
        assert(br_capture(&saved) && br_session_valid(&saved));
        br_app_init();
        assert(br_restore(&saved));
        assert(br_part[1].sound.drive == 77u && br_part[1].fx.drive_mode == 2u &&
               br_part[1].fx.drive_gain == 33u && br_part[1].fx.drive_tone == 91u && br_part_pending[1].drive_bias == 87u);
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
               !strcmp(br_control(2u).label, "TONE") && !strcmp(br_control(3u).label, "BIAS"));
        assert(br_control(3u).value == 0);
        assert(!strcmp(br_control(0u).text, "OVERDRIVE"));
        br_control_turn(0u, 1); assert(!strcmp(br_control(0u).text, "DISTORT") && br_part[1].fx.drive_mode == 1u);
        br_control_turn(0u, 1); assert(!strcmp(br_control(0u).text, "FUZZ"));
        br_control_turn(0u, 1); assert(br_part[1].fx.drive_mode == 2u);   /* three modes only */
        br_control_turn(1u, -20); br_control_turn(2u, -10); br_control_turn(3u, 40);
        assert(br_part[1].fx.drive_gain == 44u && br_part[1].fx.drive_tone == 117u && br_part_pending[1].drive_bias == 40u && br_part[1].sound.drive == 90u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_DELAY;
        assert(!strcmp(br_control(3u).label, "TONE"));
        br_control_turn(3u, -30); assert(br_part_pending[1].delay_tone == 97u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_MOD;
        assert(!strcmp(br_control(3u).label, "FEEDBACK"));
        br_control_turn(3u, 40); assert(br_part_pending[1].mod_feedback == 40u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB;
        assert(!strcmp(br_control(3u).label, "PRE DELAY"));
        br_control_turn(3u, 11); assert(br_master.reverb_extra[0] == 11u && (br_master.reverb_manual & 32u));
        assert(!strcmp(br_control(3u).text, "1/8"));
        br_ui.screen[BR_PAGE_FX] = BR_FX_DRIVE;
        for (uint32_t mode = 0u; mode < 3u; ++mode) {
            br_part[1].fx.drive_mode = mode; br_ui.redraw = 1u; br_draw();
        }
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
    }
    printf("drive: bypass, three shapers, gain, tone, never-quieter send, send-to-zero fade, silence, app path and session checks passed\n");
    return 0;
}
