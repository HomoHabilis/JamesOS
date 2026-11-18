/* SPDX-License-Identifier: GPL-3.0-only
 * Envelope times through the real code: br_time_samples (the time law and the
 * display), the amp envelope (br_env_*) and the mod/filter envelope
 * (br_mod_advance). Displayed time must equal measured time, the law must be
 * strictly increasing, and segment boundaries must not click. Host only. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* Measured segment end must equal the displayed (law) time within 2%, and
 * within one sample when the segment is short. */
static int time_ok(uint32_t measured, uint32_t displayed)
{
    uint32_t d = measured > displayed ? measured - displayed : displayed - measured;
    return d <= 1u || d * 50u <= displayed;
}

static double ms(uint32_t samples) { return samples * 1000.0 / FS; }

/* ---- amp envelope ------------------------------------------------------ */
static br_env_t *amp_env;
static br_sound_t amp_sound;
static uint32_t amp_clock;       /* samples since last refresh */
static uint32_t max_step;        /* largest |level step| since reset */

static void amp_start(uint32_t keep)
{
#if BR_POLY_SUPPORT
    static br_env_t env;
    amp_env = &env;
    br_env_start_state(amp_env, 60u, 127u, &amp_sound, keep);
#else
    amp_env = &br_env[0];
    br_env_start(0u, 60u, 127u, &amp_sound, keep);
#endif
    amp_clock = 0u;
}

static void amp_off(void)
{
    amp_env->stage = BR_ENV_RELEASE;
    amp_env->release_start = amp_env->amp;
    amp_clock = 0u;          /* a note-off lands between blocks */
}

static uint32_t amp_tick(void)
{
    uint32_t before = amp_env->amp, step;
    if (!amp_clock) br_env_refresh(amp_env, &amp_sound, 1u);
    amp_clock = (amp_clock + 1u) % BR_BLOCK;
    br_env_advance(amp_env);
    step = before > amp_env->amp ? before - amp_env->amp : amp_env->amp - before;
    if (step > max_step) max_step = step;
    return step;
}

static void amp_sound_set(uint32_t a, uint32_t d, uint32_t s, uint32_t r)
{
    memset(&amp_sound, 0, sizeof amp_sound);
    amp_sound.attack = a; amp_sound.decay = d; amp_sound.sustain = s; amp_sound.release = r;
    amp_sound.level = 127u;
}

/* Samples spent in the stage; the level at the end and the time to -60 dB. */
static uint32_t amp_stage(uint32_t stage, uint32_t *db60)
{
    uint32_t n = 0u;
    if (db60) *db60 = 0u;
    while (amp_env->stage == stage && n < 10u * FS * 60u) {
        amp_tick(); ++n;
        if (db60 && !*db60 && amp_env->amp <= 33u) *db60 = n;
    }
    return n;
}

static uint32_t amp_attack(uint32_t v) { amp_sound_set(v, 0u, 127u, 0u); amp_start(0u); return amp_stage(BR_ENV_ATTACK, 0); }
static uint32_t amp_decay(uint32_t v, uint32_t *db60)
{
    amp_sound_set(0u, v, 0u, 0u); amp_start(0u);
    amp_stage(BR_ENV_ATTACK, 0);
    uint32_t n = amp_stage(BR_ENV_DECAY, db60);
    CHECK(amp_env->amp == 0u && amp_env->stage == BR_ENV_SUSTAIN, "amp decay %u does not land on sustain", v);
    return n;
}
static uint32_t amp_release(uint32_t v, uint32_t sustain, uint32_t *db60)
{
    amp_sound_set(0u, 0u, sustain, v); amp_start(0u);
    while (amp_env->stage != BR_ENV_SUSTAIN) amp_tick();
    amp_off();
    uint32_t n = amp_stage(BR_ENV_RELEASE, db60);
    CHECK(amp_env->amp == 0u && amp_env->stage == BR_ENV_OFF, "amp release %u does not land on zero", v);
    return n;
}

/* ---- mod / filter envelope ---------------------------------------------- */
static br_mod_config_t mod_config;
static br_mod_state_t mod_state;
static void mod_advance(uint32_t n)
{
#if BR_MOD_LFO_PHASE_DELTA_CACHE
    br_mod_advance(&mod_state, &mod_config, n, 0u);
#else
    br_mod_advance(&mod_state, &mod_config, n);
#endif
}
static void mod_set(uint32_t a, uint32_t d, uint32_t s, uint32_t r)
{
    br_fx_config_t fx;
    br_module_defaults(&mod_config, &fx);
    mod_config.attack = a; mod_config.decay = d; mod_config.sustain = s; mod_config.release = r;
    mod_config.loop = 0u; mod_config.retrigger = 1u;
    br_mod_init(&mod_state, 1u);
    br_mod_note_on(&mod_state, &mod_config, 60u, 100u);
}
static uint32_t mod_stage(uint32_t stage)
{
    uint32_t n = 0u;
    while (mod_state.stage == stage && n < 10u * FS * 60u) { mod_advance(1u); ++n; }
    return n;
}
static uint32_t mod_attack(uint32_t v) { mod_set(v, 0u, 127u, 0u); return mod_stage(BR_MOD_ENV_ATTACK); }
static uint32_t mod_decay(uint32_t v)
{
    mod_set(0u, v, 0u, 0u); mod_stage(BR_MOD_ENV_ATTACK);
    uint32_t n = mod_stage(BR_MOD_ENV_DECAY);
    CHECK(mod_state.env == 0u && mod_state.stage == BR_MOD_ENV_SUSTAIN, "mod decay %u end", v);
    return n;
}
static uint32_t mod_release(uint32_t v, uint32_t sustain)
{
    mod_set(0u, 0u, sustain, v);
    while (mod_state.stage != BR_MOD_ENV_SUSTAIN) mod_advance(1u);
    br_mod_note_off(&mod_state);
    uint32_t n = mod_stage(BR_MOD_ENV_RELEASE);
    CHECK(mod_state.env == 0u && mod_state.stage == BR_MOD_ENV_OFF, "mod release %u end", v);
    return n;
}

/* Whole-block advance must equal sample-by-sample advance. */
static void mod_block_equivalence(uint32_t a, uint32_t d, uint32_t s, uint32_t r, uint32_t loop)
{
    br_mod_state_t one, block;
    uint32_t i, k;
    mod_set(a, d, s, r); mod_config.loop = loop;
    one = block = mod_state;
    for (k = 0u; k < 400u; ++k) {
        if (k == 200u) { br_mod_note_off(&one); br_mod_note_off(&block); }
        mod_state = one; for (i = 0u; i < BR_BLOCK; ++i) mod_advance(1u); one = mod_state;
        mod_state = block; mod_advance(BR_BLOCK); block = mod_state;
        if (one.env != block.env || one.stage != block.stage || one.env_pos != block.env_pos) {
            CHECK(0, "mod block advance differs a=%u d=%u s=%u r=%u loop=%u block %u: %u/%u vs %u/%u",
                  a, d, s, r, loop, k, one.env, one.stage, block.env, block.stage);
            return;
        }
    }
}

/* Parse the display text back to samples (it shows three significant digits). */
static double text_ms(uint32_t samples, char *text)
{
    double v;
    text[0] = 0;
    br_env_time_text(text, samples);
    v = atof(text);
    return strstr(text, "MS") ? v : v * 1000.0;
}

int main(void)
{
    static const uint32_t knobs[] = {0, 8, 16, 32, 48, 64, 80, 96, 112, 120, 127};
    uint32_t v, k, curve;
    char text[24];
    printf("FS=%u\n", (unsigned)FS);

    /* Time law: strictly increasing, endpoints, display text within 1%. */
    for (curve = 0u; curve < 2u; ++curve) {
        double t0 = curve ? 1.0 : 0.5, tmax = curve ? 12000.0 : 5000.0;
        for (v = 0u; v < 128u; ++v) {
            uint32_t t = br_time_samples(v, curve);
            double exact = t0 * pow(tmax / t0, v / 127.0) * FS / 1000.0;
            CHECK(fabs(t - exact) <= 0.5 + exact * 0.001, "law curve %u v %u: %u vs %.1f", curve, v, t, exact);
            if (v) CHECK(t > br_time_samples(v - 1u, curve), "law curve %u not increasing at %u", curve, v);
            double shown = text_ms(t, text);
            CHECK(fabs(shown - ms(t)) <= ms(t) * 0.01, "text %s vs %.3f ms", text, ms(t));
            if (v) {
                char prev[24];
                text_ms(br_time_samples(v - 1u, curve), prev);
                CHECK(strcmp(prev, text) != 0, "display repeats %s at curve %u v %u", text, curve, v);
            }
        }
    }

    printf("knob | ATTACK shown  amp  mod | DECAY shown  amp  mod  amp-60dB | RELEASE shown  amp  mod  amp-60dB  (ms)\n");
    for (k = 0u; k < sizeof knobs / sizeof knobs[0]; ++k) {
        uint32_t ta, td, tr, a1, a2, d1, d2, r1, r2, d60, r60;
        char sa[24], sd[24];
        v = knobs[k];
        ta = br_time_samples(v, 0u); td = tr = br_time_samples(v, 1u);
        a1 = amp_attack(v); a2 = mod_attack(v);
        d1 = amp_decay(v, &d60); d2 = mod_decay(v);
        r1 = amp_release(v, 127u, &r60); r2 = mod_release(v, 127u);
        text_ms(ta, sa); text_ms(td, sd);
        printf("%4u | %8s %7.2f %7.2f | %8s %8.2f %8.2f %8.2f | %8s %8.2f %8.2f %8.2f\n", v,
               sa, ms(a1), ms(a2), sd, ms(d1), ms(d2), ms(d60), sd, ms(r1), ms(r2), ms(r60));
        CHECK(time_ok(a1, ta) && time_ok(a2, ta), "attack %u: %u %u vs %u", v, a1, a2, ta);
        CHECK(time_ok(d1, td) && time_ok(d2, td), "decay %u: %u %u vs %u", v, d1, d2, td);
        CHECK(time_ok(r1, tr) && time_ok(r2, tr), "release %u: %u %u vs %u", v, r1, r2, tr);
    }
    /* Every value, both envelopes: measured equals the law (strictly increasing). */
    for (v = 0u; v < 128u; ++v) {
        uint32_t ta = br_time_samples(v, 0u), tdr = br_time_samples(v, 1u);
        CHECK(time_ok(amp_attack(v), ta) && time_ok(mod_attack(v), ta), "attack %u", v);
        CHECK(time_ok(amp_decay(v, 0), tdr) && time_ok(mod_decay(v), tdr), "decay %u", v);
        CHECK(time_ok(amp_release(v, 127u, 0), tdr) && time_ok(mod_release(v, 127u), tdr), "release %u", v);
    }

    /* LPG changes only the amp release: test its endpoints, a typical value,
     * several starting levels and the actual label shown on the amp page. */
    {
        static const uint32_t values[] = {0u, 75u, 127u};
        static const uint32_t levels[] = {32u, 64u, 127u};
        br_ui.part = 0u; br_ui.page = BR_PAGE_ENV; br_ui.screen[BR_PAGE_ENV] = 0u;
        br_part[0].sound.amp_mode = 1u;
        for (k = 0u; k < sizeof values / sizeof values[0]; ++k) {
            uint32_t expected = br_lpg_release_samples();
            char shown[24], wanted[24];
            br_part[0].sound.release = values[k];
            br_control_t c = br_control(3u);
            br_control_text(3u, &c, shown); br_env_time_text(wanted, expected);
            CHECK(!c.label[0], "LPG amp release control is inactive");
            for (uint32_t j = 0u; j < sizeof levels / sizeof levels[0]; ++j) {
                uint32_t first, n;
                amp_sound_set(0u, 0u, levels[j], values[k]); amp_sound.amp_mode = 1u;
                amp_start(0u); while (amp_env->stage != BR_ENV_SUSTAIN) amp_tick();
                amp_off(); max_step = 0u; first = amp_tick();
                n = 1u + amp_stage(BR_ENV_RELEASE, 0);
                CHECK(time_ok(n, expected), "LPG release %u sustain %u: %u vs %u", values[k], levels[j], n, expected);
                CHECK(first <= max_step, "LPG release %u begins with a discontinuity", values[k]);
                CHECK(amp_env->stage == BR_ENV_OFF && !amp_env->amp, "LPG release reaches silence");
            }
        }
        /* Switching modes during a release refreshes its cached duration. */
        amp_sound_set(0u, 0u, 127u, 75u); amp_start(0u);
        while (amp_env->stage != BR_ENV_SUSTAIN) amp_tick();
        amp_off(); amp_tick(); amp_sound.amp_mode = 1u; br_env_refresh(amp_env, &amp_sound, 1u);
        CHECK(amp_env->release == br_lpg_release_samples(), "LPG mode refreshes release duration");
        amp_sound.amp_mode = 0u; br_env_refresh(amp_env, &amp_sound, 1u);
        CHECK(amp_env->release == br_time_samples(75u, 1u), "envelope mode restores release duration");
        br_part[0].sound.amp_mode = 0u;
    }

    printf("release from sustain (constant time): knob  S=32  S=64  S=127 (amp ms / mod ms)\n");
    for (k = 0u; k < sizeof knobs / sizeof knobs[0]; k += 2u) {
        uint32_t s32, s64, s127, t = br_time_samples(knobs[k], 1u);
        v = knobs[k];
        s32 = amp_release(v, 32u, 0); s64 = amp_release(v, 64u, 0); s127 = amp_release(v, 127u, 0);
        printf("%4u  %9.2f %9.2f %9.2f / %9.2f %9.2f %9.2f\n", v, ms(s32), ms(s64), ms(s127),
               ms(mod_release(v, 32u)), ms(mod_release(v, 64u)), ms(mod_release(v, 127u)));
        CHECK(time_ok(s32, t) && time_ok(s64, t), "release from sustain %u", v);
    }

    /* Clicks: the step at a boundary is no larger than the largest steady step
     * of the faster adjoining segment (+1 LSB for the x^2 rounding). */
    {
        static const uint32_t pairs[][4] = {{0, 0, 64, 0}, {0, 30, 64, 20}, {20, 0, 0, 40},
                                            {40, 40, 100, 40}, {127, 127, 32, 127}, {10, 90, 64, 60}};
        uint32_t worst = 0u;
        for (k = 0u; k < sizeof pairs / sizeof pairs[0]; ++k) {
            const uint32_t *p = pairs[k];
            uint32_t sa, sd, sr, b, half;
            amp_sound_set(p[0], p[1], p[2], p[3]);
            /* steady steps of each segment on its own */
            amp_start(0u); max_step = 0u; amp_stage(BR_ENV_ATTACK, 0); sa = max_step;
            max_step = 0u; amp_stage(BR_ENV_DECAY, 0); sd = max_step;
            amp_off(); max_step = 0u; amp_stage(BR_ENV_RELEASE, 0); sr = max_step;
            /* attack -> decay */
            amp_start(0u);
            while (amp_env->stage == BR_ENV_ATTACK) amp_tick();
            b = amp_tick();
            CHECK(b <= (sa > sd ? sa : sd) + 1u, "attack->decay click %u (%u %u)", b, sa, sd);
            if (b > worst) worst = b;
            /* decay -> sustain */
            while (amp_env->stage == BR_ENV_DECAY) b = amp_tick();
            CHECK(b <= sd + 1u, "decay->sustain click %u (%u)", b, sd);
            /* note-off from sustain */
            amp_off(); b = amp_tick();
            CHECK(b <= sr + 1u, "note-off click %u (%u)", b, sr);
            /* retrigger half-way through release */
            half = amp_release(p[3], p[2], 0) / 2u;
            amp_sound_set(p[0], p[1], p[2], p[3]);
            amp_start(0u); while (amp_env->stage != BR_ENV_SUSTAIN) amp_tick();
            amp_off(); for (v = 0u; v < half; ++v) amp_tick();
            amp_start(1u); b = amp_tick();
            CHECK(b <= (sa > sr ? sa : sr) + 1u, "retrigger click %u (%u %u)", b, sa, sr);
            /* note-off during attack */
            amp_start(0u); for (v = 0u; v < br_time_samples(p[0], 0u) / 2u; ++v) amp_tick();
            amp_off(); b = amp_tick();
            CHECK(b <= (sa > sr ? sa : sr) + 1u, "attack note-off click %u (%u %u)", b, sa, sr);
        }
        printf("click check passed for %u envelopes (largest attack->decay step %u LSB)\n",
               (unsigned)(sizeof pairs / sizeof pairs[0]), worst);
    }

    mod_block_equivalence(0, 0, 64, 0, 0);
    mod_block_equivalence(40, 60, 64, 50, 0);
    mod_block_equivalence(10, 20, 30, 40, 1);
    mod_block_equivalence(90, 90, 127, 90, 0);
    mod_block_equivalence(5, 70, 0, 5, 1);

    if (failures) { printf("envelope times: %d failures\n", failures); return 1; }
    printf("envelope times: all checks passed\n");
    return 0;
}
