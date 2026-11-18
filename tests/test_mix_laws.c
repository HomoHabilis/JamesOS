/* Mix laws through the real code at 44.1 kHz: track PAN (constant power, centre
 * bit-exact, ramped), track and kit lane LEVEL ((v/127)^2 and the dB read-out),
 * MOD RATE (exponential, Hz read-out) and MOD DEPTH (pitch deviation held as RATE
 * moves). Never accesses hardware. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static const uint32_t points[9] = {0u, 16u, 32u, 48u, 64u, 80u, 96u, 112u, 127u};
static double db(double x) { return x > 0.0 ? 20.0 * log10(x) : -999.0; }

/* ---- FX bus driven directly: track 0 carries the signal, the rest are silent. */
static int32_t fx_bus[BR_PARTS][BR_BLOCK], fx_out[2u * BR_BLOCK];
static double fx_phase, fx_hz = 689.0625;     /* 689 Hz: four whole cycles per block */

static void fx_setup(void)
{
    br_app_init(); fm1_ms = 1000u;
    for (uint32_t p = 0u; p < BR_PARTS; ++p) {
        br_effective_sound[p].pan = 64u; br_effective_sound[p].delay = 0u;
        br_effective_sound[p].reverb = 0u; br_effective_sound[p].drive = 0u;
        br_effective_fx[p].mod_mix = 0u;
        br_fx_track[p].drive_send = 0;
    }
    fx_phase = 0.0; fx_hz = 689.0625;
}
/* One block; dc != 0 feeds that constant instead of a 689 Hz sine of amplitude 20000. */
static void fx_block(int32_t dc)
{
    memset(fx_bus, 0, sizeof fx_bus);
    for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
        fx_bus[0][i] = dc ? dc : (int32_t)lrint(20000.0 * sin(fx_phase));
        fx_phase += 2.0 * M_PI * fx_hz / 44100.0;
        if (fx_phase > 2.0 * M_PI) fx_phase -= 2.0 * M_PI;
    }
    memset(fx_out, 0, sizeof fx_out);
    br_fx_process(fx_bus, fx_out, BR_BLOCK);
}
static void fx_rms(double *l, double *r)
{
    double sl = 0.0, sr = 0.0;
    for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
        sl += (double)fx_out[2u * i] * fx_out[2u * i];
        sr += (double)fx_out[2u * i + 1u] * fx_out[2u * i + 1u];
    }
    *l = sqrt(sl / BR_BLOCK); *r = sqrt(sr / BR_BLOCK);
}

static void test_pan(void)
{
    static int32_t centre_out[2u * BR_BLOCK], fxpath_out[2u * BR_BLOCK];
    double cl, cr;
    /* Centre with every track dry: the dry-centre fast path. */
    fx_setup(); fx_block(0); fx_block(0);
    memcpy(centre_out, fx_out, sizeof fx_out);
    fx_rms(&cl, &cr);
    /* Centre through the FX path (another track panned forces the FX jobs): bit-exact. */
    fx_setup(); br_effective_sound[1].pan = 0u; fx_block(0); fx_block(0);
    memcpy(fxpath_out, fx_out, sizeof fx_out);
    CHECK(!br_fx_dry_center, "track 2 panned did not force the FX path");
    CHECK(!memcmp(fxpath_out, centre_out, sizeof fx_out), "centre through the FX path is not bit-exact");
    printf("PAN  (gain re centre, dB; total power re centre)\n");
    printf("  v     L dB    R dB   law L   law R   power\n");
    for (uint32_t k = 0u; k < 9u; ++k) {
        uint32_t v = points[k];
        double l, r, theta, law_l, law_r, power;
        fx_setup(); br_effective_sound[0].pan = v;
        for (int b = 0; b < 3; ++b) fx_block(0);   /* the first PAN block ramps */
        fx_rms(&l, &r);
        l /= cl; r /= cr;
        theta = M_PI / 4.0 * (v <= 64u ? 1.0 - (64.0 - v) / 64.0 : 1.0 + (v - 64.0) / 63.0);
        law_l = sqrt(2.0) * cos(theta); law_r = sqrt(2.0) * sin(theta);
        power = 10.0 * log10((l * l + r * r) / 2.0);
        printf("  %3u  %6.2f  %6.2f  %6.2f  %6.2f  %+6.3f\n", v, db(l), db(r), db(law_l), db(law_r), power);
        CHECK(fabs(power) < 0.1, "pan %u total power %+.3f dB", v, power);
        CHECK(fabs(l - law_l) < 0.002 && fabs(r - law_r) < 0.002, "pan %u gains %.4f %.4f vs %.4f %.4f", v, l, r, law_l, law_r);
    }
    {   /* Hard pan: +3.0 dB and silence. */
        double l, r;
        fx_setup(); br_effective_sound[0].pan = 0u; for (int b = 0; b < 3; ++b) fx_block(0);
        fx_rms(&l, &r); CHECK(fabs(db(l / cl) - 3.01) < 0.02 && r == 0.0, "hard left %.3f dB, right %g", db(l / cl), r);
        fx_setup(); br_effective_sound[0].pan = 127u; for (int b = 0; b < 3; ++b) fx_block(0);
        fx_rms(&l, &r); CHECK(fabs(db(r / cr) - 3.01) < 0.02 && l == 0.0, "hard right %.3f dB, left %g", db(r / cr), l);
    }
    {   /* Sweep 0..127 one detent per block on a constant input: no step anywhere.
         * Unramped, one detent moves a gain by up to ~2.4 % in one sample. */
        const int32_t dc = 16000;
        int32_t last_l = 0, last_r = 0, worst = 0;
        uint32_t v;
        fx_setup(); br_effective_sound[0].pan = 0u;
        for (int b = 0; b < 4; ++b) fx_block(dc);
        last_l = fx_out[2u * (BR_BLOCK - 1u)]; last_r = fx_out[2u * (BR_BLOCK - 1u) + 1u];
        for (v = 1u; v <= 127u; ++v) {
            br_effective_sound[0].pan = v; fx_block(dc);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
                int32_t dl = abs(fx_out[2u * i] - last_l), dr = abs(fx_out[2u * i + 1u] - last_r);
                if (dl > worst) worst = dl;
                if (dr > worst) worst = dr;
                last_l = fx_out[2u * i]; last_r = fx_out[2u * i + 1u];
            }
        }
        /* dc/4 = 4000 per track; the largest per-detent gain change is ~0.025:
         * ~100 counts unramped, under 1 count per sample ramped (+ rounding). */
        printf("  sweep 0..127, one detent per block: largest sample-to-sample step %d (of 4000)\n", worst);
        CHECK(worst <= 3, "pan sweep step %d", worst);
    }
}


/* ---- LEVEL through the whole app: a held synth note on track 1, a kit hit on
 * lane 1 (808 kit); RMS of the track's own bus (the engine output before the
 * mix, full resolution) relative to LEVEL 127. Velocity stays linear. */
static void publish(uint32_t p) { RING_PUBLISH(); br_part[p].revision++; }

static double synth_rms(uint32_t level)
{
    double sum = 0.0;
    br_app_init(); fm1_ms = 1000u;
    br_part[0].revision++; br_part[0].sound.level = level; br_part[0].sound.reverb = 0u;
    br_part[0].sound.delay = 0u; publish(0u);
    br_engine_note_on(0u, 60u, 100u);
    for (uint32_t b = 0u; b < 24u; ++b) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        if (b >= 8u) for (uint32_t i = 0u; i < BR_BLOCK; ++i) sum += (double)bus[0][i] * bus[0][i];
    }
    return sqrt(sum);
}

static double kit_energy(uint32_t lane_level, uint32_t part_level)
{
    const uint32_t lane = 0u;
    double energy = 0.0;
    br_app_init(); fm1_ms = 2000u;
    br_load_preset(BR_DRUM_PART, BR_KIT_BRAIDS);
    br_part[BR_DRUM_PART].revision++;
    br_part[BR_DRUM_PART].lane_level[lane] = lane_level;
    br_part[BR_DRUM_PART].sound.level = part_level;
    br_part[BR_DRUM_PART].sound.reverb = 0u; br_part[BR_DRUM_PART].sound.delay = 0u;
    br_part[BR_DRUM_PART].step[0] = 1u << lane;
    br_seq_kit_velocity[0][lane] = 60u;
    br_seq_kit_length[0][lane] = 0u;
    br_seq_kit_condition[0][lane] = BR_STEP_COND_ALWAYS;
    br_seq_kit_note[0][lane] = (uint8_t)br_drum_voice_id(br_part[BR_DRUM_PART].sound.drum_voice_mask, lane);
    br_seq_kit_pitch[0][lane] = BR_STEP_PITCH_DEFAULT;
    br_seq_kit_micro[0][lane] = 0; br_seq_kit_retrig[0][lane] = 0;
    publish(BR_DRUM_PART);
    /* Let level smoothing reach the requested setting before the drum hit. */
    for (uint32_t b = 0u; b < 4u; ++b) {
        int32_t warm[BR_BLOCK * 2u] = {0};
        assert(app_audio(warm, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    }
    br_playing = 1u;
    for (uint32_t b = 0u; b < 8u; ++b) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        for (uint32_t i = 0u; i < BR_BLOCK; ++i) energy += (double)bus[BR_DRUM_PART][i] * bus[BR_DRUM_PART][i];
    }
    return sqrt(energy);
}

/* The read-out text back to dB ("-INF" -> -999). */
static double text_db(const char *t)
{
    return !strcmp(t, "-INF") ? -999.0 : atof(t);
}

static void test_level(void)
{
    static const uint32_t lv[7] = {0u, 16u, 32u, 64u, 96u, 112u, 127u};
    double ref_synth = synth_rms(127u), ref_kit = kit_energy(127u, 127u), ref_part = ref_kit;
    printf("LEVEL  (dB re 127; law 40 log10(v/127))\n");
    printf("    v     law   track   lane    kit track   read-out\n");
    for (uint32_t k = 0u; k < 7u; ++k) {
        uint32_t v = lv[k];
        double law = v ? 40.0 * log10(v / 127.0) : -999.0;
        double track = db(synth_rms(v) / ref_synth), lane = db(kit_energy(v, 127u) / ref_kit);
        double part = db(kit_energy(127u, v) / ref_part);
        char text[16];
        br_level_db_text(text, br_level_law(v));
        printf("  %3u  %6.2f  %6.2f  %6.2f  %6.2f   %s\n", v, law, track, lane, part, text);
        if (!v) {
            CHECK(track == -999.0 && lane == -999.0 && part == -999.0, "level 0 is not silent");
            CHECK(!strcmp(text, "-INF"), "level 0 read-out %s", text);
            continue;
        }
        CHECK(fabs(track - law) <= 0.1, "track level %u: %.3f dB vs law %.3f", v, track, law);
        /* Synthesized kit output has additional fixed-point quantization at
         * very low gains. Keep the measured level within 1 dB at 16/32 and
         * 0.2 dB elsewhere; the track and read-out checks remain stricter. */
        CHECK(fabs(lane - law) <= (v <= 32u ? 1.0 : 0.2), "lane level %u: %.3f dB vs law %.3f", v, lane, law);
        CHECK(fabs(part - law) <= (v <= 32u ? 1.0 : 0.2), "kit track level %u: %.3f dB vs law %.3f", v, part, law);
        /* Read-out: one decimal above -10 dB, whole dB below; it shows the engine. */
        CHECK(fabs(text_db(text) - track) <= (track > -10.0 ? 0.051 : 0.51),
              "read-out %s vs engine %.3f dB", text, track);
    }
    CHECK(br_level_law(127u) == 127u && br_level_law(0u) == 0u, "law endpoints");
    {   /* The law never steps down. */
        uint32_t ok = 1u;
        for (uint32_t v = 1u; v < 128u; ++v)
            if (br_level_law(v) < br_level_law(v - 1u)) ok = 0u;
        CHECK(ok, "law not monotonic");
    }
}

/* ---- MOD: a 1 kHz sine through the real MOD effect (MIX 127, wet only).
 * Rising zero crossings give each carrier cycle's frequency; a 9-cycle moving
 * average shows the triangle LFO's square-wave pitch deviation. The LFO rate
 * is the spacing of the upward crossings of 1 kHz; the deviation is the
 * plateau, in cents (half the up/down span). */
typedef struct { double rate_hz, cents; } mod_measure_t;
static double *mm_t, *mm_f;
static mod_measure_t mod_measure(uint32_t engine, uint32_t rate, uint32_t depth, double seconds)
{
    mod_measure_t m = {0.0, 0.0};
    uint32_t blocks = (uint32_t)(seconds * 44100.0 / BR_BLOCK) + 40u, nt = 0u, cap = (uint32_t)(seconds * 1000.0) + 4000u;
    int32_t prev = 0;
    double hi = 0.0, lo = 1e9, last_up = -1.0, sum_period = 0.0;
    uint32_t periods = 0u, n = 0u;
    int armed = 0;
    if (!mm_t) { mm_t = malloc(sizeof(double) * 300000u); mm_f = malloc(sizeof(double) * 300000u); }
    assert(cap < 300000u);
    fx_setup(); fx_hz = 1000.0;
    br_effective_fx[0].mod_engine = engine; br_effective_fx[0].mod_rate = rate;
    br_effective_fx[0].mod_depth = depth; br_effective_fx[0].mod_mix = 127u;
    for (uint32_t b = 0u; b < blocks; ++b) {
        fx_block(0);
        if (b < 40u) continue;                       /* line fill and settle */
        for (uint32_t i = 0u; i < BR_BLOCK; ++i, ++n) {
            int32_t x = fx_out[2u * i];
            if (prev < 0 && x >= 0 && nt < cap)
                mm_t[nt++] = n - 1u + (double)(-prev) / (double)(x - prev);
            prev = x;
        }
    }
    for (uint32_t k = 0u; k + 1u < nt; ++k) mm_f[k] = 44100.0 / (mm_t[k + 1u] - mm_t[k]);
    /* 9-cycle moving average (in place, one behind), then its span. */
    for (uint32_t k = 0u; k + 9u < nt; ++k) {
        double f = 0.0;
        for (uint32_t j = k; j < k + 9u; ++j) f += mm_f[j];
        mm_f[k] = f / 9.0;
        if (mm_f[k] > hi) hi = mm_f[k];
        if (mm_f[k] < lo) lo = mm_f[k];
    }
    {   /* Upward crossings of the middle, with hysteresis at 30 % of the half span. */
        double mid = 0.5 * (hi + lo), band = 0.3 * 0.5 * (hi - lo);
        for (uint32_t k = 1u; k + 9u < nt; ++k) {
            if (mm_f[k] < mid - band) armed = 1;
            if (armed && mm_f[k - 1u] < mid && mm_f[k] >= mid) {
                if (last_up >= 0.0) { sum_period += mm_t[k + 4u] - last_up; ++periods; }
                last_up = mm_t[k + 4u]; armed = 0;
            }
        }
    }
    m.rate_hz = periods ? 44100.0 * periods / sum_period : 0.0;
    m.cents = 600.0 * log2(hi / lo);
    return m;
}
static double mod_law_hz(uint32_t v)
{
    double f40 = 249856.0 * 44100.0 / 4294967296.0;
    return v <= 40u ? f40 * pow(0.05 / f40, (40.0 - v) / 40.0) : f40 * pow(10.0 / f40, (v - 40.0) / 87.0);
}
/* br_rate_text back to Hz: "20.0S" is seconds per cycle, "2.57HZ" is Hz. */
static double rate_text_hz(const char *t)
{
    double x = atof(t);
    return strchr(t, 'S') && !strstr(t, "HZ") ? 1.0 / x : x;
}

static void test_mod_rate(void)
{
    double last = 0.0;
    uint32_t v, ok = 1u;
    printf("MOD RATE  (Hz; measured from the vibrato's pitch on a 1 kHz sine)\n");
    printf("    v      law  measured  display\n");
    for (uint32_t k = 0u; k < 9u; ++k) {
        char text[24] = "";
        double law, shown;
        mod_measure_t m;
        v = points[k];
        law = mod_law_hz(v);
        m = mod_measure(BR_MOD_VIBRATO, v, 96u, 4.2 / law + 0.4);
        br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_MOD;
        {
            br_control_t c; memset(&c, 0, sizeof c); c.label = "RATE"; c.value = (int32_t)v;
            br_control_text(1u, &c, text);
        }
        shown = rate_text_hz(text);
        printf("  %3u  %7.3f  %7.3f   %s\n", v, law, m.rate_hz, text);
        CHECK(fabs(m.rate_hz / shown - 1.0) <= 0.01, "mod rate %u: measured %.4f Hz, shown %s", v, m.rate_hz, text);
        CHECK(fabs(m.rate_hz / law - 1.0) <= 0.01, "mod rate %u: measured %.4f Hz, law %.4f", v, m.rate_hz, law);
        CHECK(m.rate_hz > last, "mod rate %u not above the previous point", v);
        last = m.rate_hz;
    }
    for (v = 1u; v < 128u; ++v) if (br_mod_rate_inc(v) <= br_mod_rate_inc(v - 1u)) ok = 0u;
    CHECK(ok, "MOD RATE steps are not strictly increasing");
    CHECK(br_mod_rate_inc(40u) == 4096u + 40u * 6144u, "default 40 moved: %u", br_mod_rate_inc(40u));
    printf("  default 40: phase step %u (the old law's 249856); per step x%.5f below 40, x%.5f above\n",
           br_mod_rate_inc(40u), (double)br_mod_rate_inc(40u) / br_mod_rate_inc(39u),
           (double)br_mod_rate_inc(41u) / br_mod_rate_inc(40u));
}

static void test_mod_depth(void)
{
    static const uint32_t depths[3] = {32u, 64u, 127u}, rates[4] = {16u, 40u, 96u, 127u};
    static const uint32_t engines[2] = {BR_MOD_CHORUS, BR_MOD_VIBRATO};
    printf("MOD DEPTH  (peak pitch deviation, cents: measured / read-out; * = sweep at the line's end)\n");
    for (uint32_t e = 0u; e < 2u; ++e) {
        printf("  %s    rate 16          40          96          127\n", e ? "VIBRATO" : "CHORUS ");
        for (uint32_t d = 0u; d < 3u; ++d) {
            double at40 = 0.0;
            printf("   depth %3u", depths[d]);
            for (uint32_t r = 0u; r < 4u; ++r) {
                uint32_t rate = rates[r], depth = depths[d];
                uint32_t span = br_mod_depth_span(engines[e], depth, rate);
                uint32_t clamped = span == BR_FX_MOD_REACH - (engines[e] == BR_MOD_VIBRATO ? 48u : 320u);
                double law = mod_law_hz(rate);
                mod_measure_t m = mod_measure(engines[e], rate, depth, 3.2 / law + 0.4);
                uint32_t shown = br_mod_depth_cents(engines[e], depth, rate);
                printf("   %6.1f/%3u%s", m.cents, shown, clamped ? "*" : " ");
                CHECK(fabs(m.cents - shown) <= 0.05 * shown + 1.0, "%s depth %u rate %u: measured %.1f cents, shown %u",
                      e ? "vibrato" : "chorus", depth, rate, m.cents, shown);
                if (rate == 40u) at40 = m.cents;
                else if (r && !clamped)
                    CHECK(fabs(m.cents / at40 - 1.0) <= 0.10, "%s depth %u rate %u: %.1f cents vs %.1f at rate 40",
                          e ? "vibrato" : "chorus", depth, rate, m.cents, at40);
            }
            /* rate 16 is measured before rate 40: check it now. */
            {
                uint32_t span = br_mod_depth_span(engines[e], depths[d], 16u);
                uint32_t clamped = span == BR_FX_MOD_REACH - (engines[e] == BR_MOD_VIBRATO ? 48u : 320u);
                if (!clamped) {
                    mod_measure_t m = mod_measure(engines[e], 16u, depths[d], 3.2 / mod_law_hz(16u) + 0.4);
                    CHECK(fabs(m.cents / at40 - 1.0) <= 0.10, "depth %u rate 16: %.1f vs %.1f", depths[d], m.cents, at40);
                }
            }
            printf("\n");
        }
    }
    {   /* Defaults keep their sound: CHORUS depth 48 at rate 40 is the old sweep. */
        CHECK(br_mod_depth_span(BR_MOD_CHORUS, 48u, 40u) == 193u, "default chorus span moved");
        CHECK(br_mod_depth_span(BR_MOD_FLANGE, 64u, 127u) == 257u, "flanger span must not follow RATE");
        printf("  default CHORUS 48 / rate 40: span 193 samples, %u cents\n", br_mod_depth_cents(BR_MOD_CHORUS, 48u, 40u));
    }
    {   /* The tape echo's wow reads this LFO's phase step: what it sees, old law vs new. */
        static const uint32_t r3[3] = {0u, 40u, 127u};
        for (uint32_t k = 0u; k < 3u; ++k) {
            uint32_t old_inc = 4096u + r3[k] * 6144u, inc;
            fx_setup(); br_effective_fx[0].mod_rate = r3[k]; br_effective_fx[0].mod_mix = 127u;
            fx_block(0);
            inc = br_fx_controls[0].mod_lfo_inc;
            printf("  tape wow at MOD RATE %3u: LFO step %6u (%.3f Hz), was %6u (%.3f Hz)\n", r3[k], inc,
                   inc * 44100.0 / 4294967296.0, old_inc, old_inc * 44100.0 / 4294967296.0);
            if (r3[k] == 40u) CHECK(inc == old_inc, "tape wow at the default rate changed");
        }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    test_pan();
    test_level();
    test_mod_rate();
    test_mod_depth();
    if (failures) { printf("mix laws: %d check(s) failed\n", failures); return 1; }
    printf("mix laws: all checks passed\n");
    return 0;
}
