/* SPDX-License-Identifier: GPL-3.0-only
 * LFO rate law and modulation law through the real app code (dinghy.c):
 * measured LFO frequency vs the read-out, the exponential rate input.
 * Host only; never touches hardware. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double law_hz(double v) { return 0.02 * pow(1000.0, v / 127.0); }

static void advance(br_mod_state_t *s, const br_mod_config_t *c, uint32_t n, uint32_t part)
{
#if BR_MOD_LFO_PHASE_DELTA_CACHE
    br_mod_advance(s, c, n, part);
#else
    (void)part; br_mod_advance(s, c, n);
#endif
}

/* Run LFO 1 (sine) block by block through br_mod_advance and time its upward
 * zero crossings (linear interpolation within a block); returns Hz. */
static double measure_hz(uint32_t rate, int32_t rate_patch)
{
    br_mod_config_t cfg; br_fx_config_t fx, fxout; br_mod_state_t st;
    br_sound_t base, out; int32_t pitch;
    br_module_defaults(&cfg, &fx);
    cfg.lfo[0].rate = rate; cfg.lfo[0].wave = BR_WAVE_SINE;
    br_mod_init(&st, 1u);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
    br_mod_phase_delta_cache_reset(0u);
#endif
    if (rate_patch) {          /* full velocity into the LFO 1 RATE input */
        br_preset_sound(BR_FIRST_PLAITS, &base);
        cfg.route[0] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_LFO1_RATE, rate_patch, 1u};
        st.velocity = 127u;
        br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
    }
    double expect = law_hz(rate + rate_patch);
    double seconds = 6.0 / expect < 10.0 ? 10.0 : 6.0 / expect;   /* >= 6 cycles, >= 10 s */
    uint32_t blocks = (uint32_t)(seconds * FM1_AUDIO_RATE_HZ / BR_BLOCK) + 1u;
    double first = -1.0, last = -1.0; uint32_t crossings = 0u;
    int32_t prev = br_mod_lfo(&st, 0u, BR_WAVE_SINE);
    for (uint32_t b = 0u; b < blocks; ++b) {
        advance(&st, &cfg, BR_BLOCK, 0u);
        int32_t now = br_mod_lfo(&st, 0u, BR_WAVE_SINE);
        if (prev < 0 && now >= 0) {
            double t = (b + (double)-prev / (double)(now - prev)) * BR_BLOCK / FM1_AUDIO_RATE_HZ;
            if (first < 0.0) first = t; else ++crossings;
            last = t;
        }
        prev = now;
    }
    assert(crossings >= 2u);
    return crossings / (last - first);
}

/* The LFO page's RATE read-out for LFO 1 at knob value v, as a frequency. */
static double displayed_hz(uint32_t v, char *text)
{
    br_ui.part = 0u; br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0u;
    br_part[0].mod.lfo[0].rate = v;
    br_control_t c = br_control(1u);
    assert(!strcmp(c.label, "RATE") && c.value == (int32_t)v);
    text[0] = 0; br_control_text(1u, &c, text);
    double x = atof(text);
    size_t n = strlen(text);
    if (n > 2 && !strcmp(text + n - 2, "HZ")) return x;
    assert(n > 1 && text[n - 1] == 'S');
    return 1.0 / x;
}

static void test_rate_law(void)
{
    static const uint32_t vs[] = {0u, 16u, 32u, 64u, 96u, 112u, 127u};
    uint32_t prev = 0u;
    char text[24];
    for (uint32_t v = 0u; v < 128u; ++v) {
        uint32_t q20 = br_lfo_rate_hz_q20(v << 8);
        double hz = q20 / 1048576.0;
        assert(q20 > prev);                                   /* strictly increasing */
        assert(fabs(hz / law_hz(v) - 1.0) < 0.001);          /* the law, within 0.1 % */
        double shown = displayed_hz(v, text);
        assert(fabs(shown / hz - 1.0) < 0.006);               /* read-out within 0.5 % (+rounding) */
        prev = q20;
    }
    for (uint32_t v = 0u; v + 1u < 128u * 256u; v += 37u)  /* Q8 inputs: monotonic too */
        assert(br_lfo_rate_hz_q20(v + 1u) >= br_lfo_rate_hz_q20(v));
    printf("LFO rate (FS %u, %u-frame blocks): v law_Hz measured_Hz shown err%%\n",
           FM1_AUDIO_RATE_HZ, (unsigned)BR_BLOCK);
    for (uint32_t i = 0u; i < sizeof vs / sizeof vs[0]; ++i) {
        double measured = measure_hz(vs[i], 0);
        double shown = displayed_hz(vs[i], text);
        printf("  v=%3u %9.4f %9.4f %-8s %+.3f%%\n", vs[i], law_hz(vs[i]), measured, text,
               100.0 * (shown / measured - 1.0));
        assert(fabs(shown / measured - 1.0) < 0.01);         /* displayed = measured within 1 % */
        assert(fabs(measured / law_hz(vs[i]) - 1.0) < 0.002);
    }
    /* Slowest cycle: 50 s within 1 %. */
    { double period = 1.0 / measure_hz(0u, 0); printf("  slowest cycle %.3f s\n", period);
      assert(fabs(period / 50.0 - 1.0) < 0.01); }
    /* Defaults keep their old speeds (5.05 / 7.57 Hz). */
    { br_mod_config_t m; br_fx_config_t f; br_module_defaults(&m, &f);
      assert(m.lfo[0].rate == 102u && m.lfo[1].rate == 109u); }
}

/* A patched LFO RATE input acts on the knob before the law: equal +/- amounts
 * give equal ratios. */
static void test_rate_input_exponential(void)
{
    double base = measure_hz(64u, 0), up = measure_hz(64u, 40), down = measure_hz(64u, -40);
    printf("rate input +/-40 at v=64: x%.4f / x%.4f (law x%.4f)\n", up / base, base / down,
           pow(1000.0, 40.0 / 127.0));
    assert(fabs((up / base) / (base / down) - 1.0) < 0.005);
    assert(fabs(up / base / pow(1000.0, 40.0 / 127.0) - 1.0) < 0.005);
}

/* LFO 1 triangle at rate 0 (50 s) into PITCH at depth d, one full cycle run
 * block by block through br_mod_advance/br_mod_effective, plus a fine phase
 * sweep: the output grid (oscillator input, Q7 semitones) and the peak. */
static void test_pitch_depth(void)
{
    static const uint32_t depths[] = {5u, 20u, 64u, 127u};
    br_sound_t base, out; br_fx_config_t fx, fxout; br_mod_config_t cfg; br_mod_state_t st;
    int32_t pitch;
    br_preset_sound(BR_FIRST_PLAITS, &base);
    printf("pitch: depth peak_st (law) grid_step_c max_step_per_block_c\n");
    for (uint32_t i = 0u; i < sizeof depths / sizeof depths[0]; ++i) {
        uint32_t d = depths[i];
        br_module_defaults(&cfg, &fx); cfg.target = BR_TARGET_OFF;
        cfg.lfo[0] = (br_lfo_config_t){BR_WAVE_TRIANGLE, 0u, d, BR_TARGET_PITCH};
        br_mod_init(&st, 1u);
        int32_t peak = 0, prev = 0, max_block = 0, max_gap = 0;
        uint32_t blocks = (uint32_t)(50.5 * FM1_AUDIO_RATE_HZ / BR_BLOCK);
        for (uint32_t b = 0u; b < blocks; ++b) {
            br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
            if (abs(pitch) > peak) peak = abs(pitch);
            if (b && abs(pitch - prev) > max_block) max_block = abs(pitch - prev);
            prev = pitch;
            advance(&st, &cfg, BR_BLOCK, 0u);
        }
        /* Grid: sweep the rising quarter finely; consecutive outputs differ by
         * at most one Q7 step (0.78 cent), i.e. no value is skipped. */
        prev = 0;
        for (uint32_t ph = 0u; ph <= 0x40000000u; ph += 0x4000u) {
            st.phase[0] = ph;
            br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
            if (pitch - prev > max_gap) max_gap = pitch - prev;
            assert(pitch >= prev);
            prev = pitch;
        }
        double law = 24.0 * (d / 127.0) * (d / 127.0);
        printf("  d=%3u %7.3f (%7.3f) %.2f %.2f\n", d, peak / 128.0, law,
               max_gap * 100.0 / 128.0, max_block * 100.0 / 128.0);
        assert(max_gap <= 1);                                 /* <= 0.78 cent: the Q7 input */
        assert(fabs(peak / 128.0 - law) <= 0.01 + law * 0.002);
    }
    /* A PATCH amount into PITCH has the same taper; signed. */
    br_module_defaults(&cfg, &fx); cfg.target = BR_TARGET_OFF; br_mod_init(&st, 1u);
    cfg.route[0] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_PITCH, -64, 1u};
    st.velocity = 127u; br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
    assert(abs(pitch + (int32_t)(24.0 * 64 * 64 / 16129.0 * 128.0 + 0.5)) <= 1);
    /* Kits tune in whole semitones only (sample player and drum notes take
     * integer semitones): 6.1 st rounds to 6. */
    br_preset_sound(BR_KIT_PLAITS, &base); base.tune = 0;
    cfg.route[0].amount = 64; br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
    assert(out.tune == 6);
}

/* FILTER crosses the open center and clamps at either endpoint. */
static void test_filter_side(void)
{
    br_sound_t base, out; br_fx_config_t fx, fxout; br_mod_config_t cfg; br_mod_state_t st;
    int32_t pitch;
    static const struct { uint32_t base; int32_t amount; uint32_t lo, hi; } c[] = {
        {30u, 127, 30u, 127u}, {30u, -127, 0u, 30u}, {100u, -127, 0u, 100u}, {100u, 127, 100u, 127u},
        {63u, 127, 63u, 127u}, {64u, -127, 0u, 64u}, {62u, 127, 62u, 127u}, {65u, -127, 0u, 65u},
    };
    for (uint32_t i = 0u; i < sizeof c / sizeof c[0]; ++i) {
        uint32_t lo = 127u, hi = 0u;
        br_preset_sound(BR_FIRST_PLAITS, &base); base.filter = c[i].base;
        br_module_defaults(&cfg, &fx); cfg.target = BR_TARGET_OFF; br_mod_init(&st, 1u);
        /* +/-127 constant patch plus a full-depth LFO on top: over-driven both ways */
        cfg.route[0] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_FILTER, c[i].amount, 1u};
        cfg.lfo[0] = (br_lfo_config_t){BR_WAVE_SINE, 0u, 127u, BR_TARGET_FILTER};
        st.velocity = 127u;
        for (uint32_t ph = 0u; ph < 64u; ++ph) {
            st.phase[0] = ph << 26;
            br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
            if (out.filter < lo) lo = out.filter;
            if (out.filter > hi) hi = out.filter;
        }
        printf("filter base %3u amount %+4d: control %u..%u\n", c[i].base, c[i].amount, lo, hi);
        assert(lo == c[i].lo && hi == c[i].hi);
    }
}

/* Read-outs: pitch depths in cents / semitones, as the engine applies them. */
static void test_depth_text(void)
{
    static const struct { int32_t d; const char *text; } lfo[] = {
        {0, "0CT"}, {5, "4CT"}, {16, "38CT"}, {32, "1.5ST"}, {64, "6.1ST"}, {127, "24.0ST"},
    };
    char text[24];
    br_ui.part = 0u; br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 1u;
    br_part[0].mod.lfo[1].target = BR_TARGET_PITCH;
    for (uint32_t i = 0u; i < sizeof lfo / sizeof lfo[0]; ++i) {
        br_part[0].mod.lfo[1].depth = (uint32_t)lfo[i].d;
        br_control_t c = br_control(3u); text[0] = 0; br_control_text(3u, &c, text);
        assert(!strcmp(c.label, "DEPTH"));
        if (strcmp(text, lfo[i].text)) { fprintf(stderr, "depth %d: %s\n", lfo[i].d, text); assert(0); }
    }
    br_part[0].mod.lfo[1].target = BR_TARGET_TIMBRE; br_part[0].mod.lfo[1].depth = 64u;
    { br_control_t c = br_control(3u); br_control_text(3u, &c, text); assert(!strcmp(text, "50%")); }
}

/* MOD DEPTHS: 64 is unity, 32 is half, 0 is zero, 127 is double. */
static void test_attenuverters(void)
{
    br_sound_t base, out; br_fx_config_t fx, fxout; br_mod_config_t cfg; br_mod_state_t st;
    int32_t pitch;
    static const uint32_t att[] = {64u, 32u, 0u, 127u};
    char text[24];
    for (uint32_t kind = 0u; kind < 3u; ++kind) {
        int32_t full = 0;
        for (uint32_t i = 0u; i < 4u; ++i) {
            br_preset_sound(BR_FIRST_PLAITS, &base);
            assert(base.timbre_mod == 64u && base.fm_mod == 64u && base.morph_mod == 64u);
            base.timbre = base.color = 64u;
            br_module_defaults(&cfg, &fx); cfg.target = BR_TARGET_OFF; br_mod_init(&st, 1u);
            int32_t got;
            if (kind == 0u) {          /* LFO 2 (square, high half) -> TIMBRE, depth 60 */
                cfg.lfo[1] = (br_lfo_config_t){BR_WAVE_SQUARE, 0u, 30u, BR_TARGET_TIMBRE};
                st.phase[1] = 0x10000000u; base.timbre_mod = att[i];
            } else if (kind == 1u) {   /* mod envelope -> PITCH, linear 32 = 6.05 st */
                cfg.target = BR_TARGET_PITCH; cfg.amount = 32; st.env = 32767u; base.fm_mod = att[i];
            } else {                   /* PATCH 1 (velocity) -> MORPH */
                cfg.route[0] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_COLOR, 30, 1u};
                st.velocity = 127u; base.morph_mod = att[i];
            }
            br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
            got = kind == 0u ? (int32_t)out.timbre - 64 : kind == 1u ? pitch : (int32_t)out.color - 64;
            if (!i) full = got;
            printf("attenuverter kind %u at %3u: %d\n", kind, att[i], got);
            if (att[i] == 64u) assert(full == (kind == 1u ? 774 : 30));
            else if (att[i] == 0u) assert(got == 0);
            else if (att[i] == 32u) assert(abs(got - full / 2) <= 1);
            else assert(abs(got - full * 2) <= 1);
        }
    }
    /* LFO 1 with its TARGET off reaches nothing, whatever the attenuverters say. */
    br_preset_sound(BR_FIRST_PLAITS, &base);
    br_module_defaults(&cfg, &fx); cfg.target = BR_TARGET_OFF; br_mod_init(&st, 1u);
    cfg.lfo[0] = (br_lfo_config_t){BR_WAVE_SQUARE, 0u, 127u, BR_TARGET_OFF};
    for (uint32_t v = 0u; v < 128u; v += 9u) {
        base.timbre_mod = base.fm_mod = base.morph_mod = v; st.phase[0] = 0x10000000u;
        br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
        assert(pitch == 0 && out.timbre == base.timbre && out.color == base.color);
    }
    /* Read-outs: signed percentages, +100 % at 127. */
    br_ui.part = 0u; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 2u;
    assert(br_sound_mod_page());
    br_part[0].sound.fm_mod = 127u; { br_control_t c = br_control(3u); br_control_text(3u, &c, text); assert(!strcmp(text, "+100%")); }
    br_part[0].sound.fm_mod = 64u;  { br_control_t c = br_control(3u); br_control_text(3u, &c, text); assert(!strcmp(text, "0%")); }
    br_part[0].sound.fm_mod = 0u;   { br_control_t c = br_control(3u); br_control_text(3u, &c, text); assert(!strcmp(text, "-100%")); }
}

/* Global controls follow the shared oscillator; old track destinations remain inert. */
static void test_global_lfo(void)
{
    br_session_t saved;
    br_master.global_lfo = (br_lfo_config_t){BR_WAVE_SQUARE, 102u, 127u, BR_TARGET_LEVEL};
    assert(br_capture(&saved));
    br_master.global_lfo.depth = 0u;
    assert(br_restore(&saved));
    assert(br_master.global_lfo.target == BR_TARGET_LEVEL && br_master.global_lfo.depth == 127u);
    br_global_lfo_state.phase[0] = 0u;
    for (uint32_t p = 0; p < BR_PARTS; ++p) {
        br_cached_sound[p].level = 60u; br_effective_valid[p] = 0u;
        br_build_effective(p);
        assert(br_effective_sound[p].level == 60u); /* legacy target is inactive */
    }
    /* The shared waveform is available to a patch even with global DEPTH at zero. */
    br_load_preset(0u, BR_FIRST_PLAITS);
    br_part[0].sound.timbre = 64u; br_part[0].sound.timbre_mod = 64u;
    br_master.global_lfo.wave = BR_WAVE_SQUARE; br_master.global_lfo.depth = 0u;
    br_part[0].mod.route[BR_ROUTES] = (br_route_config_t){BR_SOURCE_GLOBAL_LFO, BR_TARGET_TIMBRE, 32, 1u};
    br_part[0].revision += 2u; br_snapshot(0u);
    br_global_lfo_state.phase[0] = 0u; br_build_effective(0u);
    assert(br_effective_sound[0].timbre == 96u);
    br_global_lfo_state.phase[0] = 0x80000000u; br_build_effective(0u);
    assert(br_effective_sound[0].timbre == 32u);
    br_load_preset(1u, BR_FIRST_PLAITS);
    br_part[1].sound.timbre = 64u; br_part[1].sound.timbre_mod = 64u;
    br_part[1].revision += 2u; br_snapshot(1u); br_build_effective(1u);
    assert(br_effective_sound[1].timbre == 64u); /* no patch on track 2 */
    br_part[1].mod.route[BR_ROUTES] = (br_route_config_t){BR_SOURCE_GLOBAL_LFO, BR_TARGET_TIMBRE, 32, 1u};
    br_part[1].revision += 2u; br_snapshot(1u); br_build_effective(1u);
    assert(br_effective_sound[1].timbre == 32u); /* same phase, independently assigned */
    br_master.global_lfo.wave=BR_WAVE_TRIANGLE; br_global_lfo_state.phase[0]=0x40000000u;
    br_build_effective(0u); br_build_effective(1u);
    assert(br_effective_sound[0].timbre==96u && br_effective_sound[1].timbre==96u);
    assert(br_capture(&saved)); br_part[0].mod.route[BR_ROUTES].source = BR_SOURCE_OFF;
    assert(br_restore(&saved) && br_part[0].mod.route[BR_ROUTES].source == BR_SOURCE_GLOBAL_LFO);
    br_part[0].mod.route[BR_ROUTES].enabled = 0u;
    br_master.global_lfo = (br_lfo_config_t){BR_WAVE_SQUARE, 102u, 127u, BR_TARGET_LEVEL};
    br_global_lfo_state.phase[0] = 0u;
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = BR_LFOS; br_ui.part = 0u;
    assert(!strcmp(br_control(2u).text, "LEGACY OFF"));
    for (uint32_t i = 0u; i < BR_GLOBAL_TARGET_UI_COUNT; ++i) {
        uint32_t id = br_global_target_id[i];
        assert(id == BR_GLOBAL_TARGET_OFF || id >= BR_GLOBAL_TARGET_FILTER);
        br_master.global_lfo.target = id;
        assert(br_control(2u).value == (int32_t)i);
    }
    br_master.fx.filter = 64u; br_master.fx.resonance = 40u;
    br_master.fx.heat_amount = 40u; br_master.fx.heat_type = BR_HEAT_SAT;
    br_master.fx.smooth = 0u; br_master.fx.env_depth = 0; br_master.revision = 0u;
    br_master.global_lfo.target = BR_GLOBAL_TARGET_FILTER;
    br_master_snapshot_ram(); br_master_apply_lfo(); br_gf.valid = 0u; br_gf_apply(BR_BLOCK); assert(br_master_cached.filter == 127u);
    assert(br_master.fx.filter == 64u);
    br_global_lfo_state.phase[0] = 0x80000000u;
    br_master_snapshot_ram(); br_master_apply_lfo(); br_gf.valid = 0u; br_gf_apply(BR_BLOCK); assert(br_master_cached.filter == 0u);
    br_master.global_lfo.target = BR_GLOBAL_TARGET_HEAT_TYPE;
    br_master_snapshot_ram(); br_master_apply_lfo(); assert(br_master_cached.heat_type == BR_HEAT_BIT);
    br_global_lfo_state.phase[0] = 0u;
    br_master_snapshot_ram(); br_master_apply_lfo(); assert(br_master_cached.heat_type == BR_HEAT_WOW);
    assert(br_master.fx.heat_type == BR_HEAT_SAT);
#if !BR_AUDIO_FUSION_ONLY_BUILD
    { br_pipeline_controls_t captured;
      br_fx_capture_pipeline_controls(&captured);
      assert(captured.master_fx.heat_type == BR_HEAT_WOW);
      br_fx_pipeline_controls = &captured;
      br_global_lfo_state.phase[0] = 0x80000000u;
      br_master_snapshot_ram(); br_master_apply_lfo(); assert(br_master_cached.heat_type == BR_HEAT_WOW);
      br_fx_pipeline_controls = 0;
      br_global_lfo_state.phase[0] = 0u; }
#endif
    br_master.global_lfo.target = BR_GLOBAL_TARGET_HEAT_AMOUNT;
    br_master_snapshot_ram(); br_master_apply_lfo(); assert(br_master_cached.heat_amount == 127u);
    br_master.global_lfo.target = BR_GLOBAL_TARGET_RESONANCE;
    br_master_snapshot_ram(); br_master_apply_lfo(); assert(br_master_cached.resonance == 127u);
    br_master.comp = (br_compressor_config_t)BR_COMP_DEFAULTS;
    br_master.comp.mix = 64u;
    br_master.global_lfo.target = BR_GLOBAL_TARGET_COMP_MIX;
    br_snapshot_source_compressor(); assert(br_source_compressor.mix == 127u);
    uint32_t revision = br_source_compressor_revision;
    br_global_lfo_state.phase[0] = 0x80000000u;
    br_snapshot_source_compressor(); assert(br_source_compressor.mix == 0u);
    assert(br_source_compressor_revision != revision && !(br_source_compressor_revision & 1u));
    br_comp_snapshot(&br_source_compressor, br_source_compressor_revision);
    assert(br_comp_cached.mix == 0u);
    assert(br_master.comp.mix == 64u);
    { const uint32_t ids[] = { BR_GLOBAL_TARGET_COMP_THRESHOLD, BR_GLOBAL_TARGET_COMP_MAKEUP,
          BR_GLOBAL_TARGET_COMP_ATTACK, BR_GLOBAL_TARGET_COMP_RELEASE, BR_GLOBAL_TARGET_COMP_HOLD };
      const uint32_t maxima[] = {48u, 24u, 127u, 127u, 127u};
      for (uint32_t i = 0u; i < 5u; ++i) {
          br_master.global_lfo.target = ids[i]; br_global_lfo_state.phase[0] = 0u;
          br_snapshot_source_compressor();
          uint32_t value = i == 0u ? br_source_compressor.threshold : i == 1u ? br_source_compressor.makeup :
              i == 2u ? br_source_compressor.attack : i == 3u ? br_source_compressor.release : br_source_compressor.hold;
          assert(value == maxima[i]);
      } }
    br_master.global_lfo.target = BR_GLOBAL_TARGET_COMP_MIX;
    br_master.global_lfo.depth = 0u;
    br_snapshot_source_compressor(); assert(br_source_compressor.mix == 64u);
#if BR_FX_GLOBAL_REVERB
    /* Shared reverb uses the captured block's phase, and bounds the expensive redesign rate. */
    br_master.global_lfo.depth = 127u;
    br_master.reverb_size = 64u; br_master.reverb_decay = 64u;
    br_master.global_lfo.target = BR_GLOBAL_TARGET_REVERB_SIZE;
    br_global_lfo_state.phase[0] = 0u; br_gr_key_valid = 0u; br_gr_duck = 0u;
    br_gr_rv.idle = 1u; br_gr_prepare(); assert(br_gr_key[2] == 127u);
    br_global_lfo_state.phase[0] = 0x80000000u; br_gr_lfo_frames = 0u;
    br_gr_prepare(); assert(br_gr_key[2] == 127u); /* deferred */
    br_gr_lfo_frames = FS / 20u; br_gr_prepare(); assert(br_gr_key[2] == 0u);
    assert(br_master.reverb_size == 64u);
    br_master.global_lfo.target = BR_GLOBAL_TARGET_REVERB_DECAY;
    br_global_lfo_state.phase[0] = 0u; br_gr_lfo_frames = FS / 20u;
    br_gr_prepare(); assert(br_gr_key[1] == 127u && br_gr_key[2] == 64u);
    br_global_lfo_state.phase[0] = 0x80000000u; br_gr_lfo_frames = FS / 20u;
    br_gr_prepare(); assert(br_gr_key[1] == 0u && br_master.reverb_decay == 64u);
#if !BR_AUDIO_FUSION_ONLY_BUILD
    { br_pipeline_controls_t captured;
      br_global_lfo_state.phase[0] = 0u; br_fx_capture_pipeline_controls(&captured);
      assert(captured.reverb_decay == 127u && captured.reverb_lfo_active);
      br_global_lfo_state.phase[0] = 0x80000000u; br_fx_pipeline_controls = &captured;
      br_gr_lfo_frames = FS / 20u; br_gr_prepare(); assert(br_gr_key[1] == 127u);
      br_fx_pipeline_controls = 0; }
#endif
    assert(!strcmp(br_global_target_label(BR_GLOBAL_TARGET_REVERB_SIZE), "REVERB SIZE"));
    assert(!strcmp(br_global_target_label(BR_GLOBAL_TARGET_REVERB_DECAY), "REVERB DECAY"));
#endif
    br_master.global_lfo.target = BR_GLOBAL_TARGET_HEAT_TYPE;
    assert(br_capture(&saved)); br_master.global_lfo.target = BR_GLOBAL_TARGET_OFF;
    assert(br_restore(&saved)); assert(br_master.global_lfo.target == BR_GLOBAL_TARGET_HEAT_TYPE);
    assert(!strcmp(br_control(2u).text, "HEAT TYPE"));
    assert(br_control(2u).high == (int32_t)BR_GLOBAL_TARGET_UI_COUNT - 1);
    uint32_t before = br_global_lfo_state.phase[0];
    br_global_lfo_advance(256u); assert(br_global_lfo_state.phase[0] != before);
    assert(!strcmp(br_header_title(), "GLOBAL LFO"));
    assert(br_control(1u).value == 102); br_control_turn(1u, 1);
    assert(br_master.global_lfo.rate == 103u);
    br_ui.part = 1u; assert(br_control(1u).value == 103);
    /* The shared RATE/DEPTH pictures use the shared oscillator, not a track LFO. */
    assert(br_param_animated(1u, BR_WIDGET_ORBIT) == BR_PARAM_ANIM_GLOBAL_LFO);
    assert(br_param_animated(3u, BR_WIDGET_LFO_DEPTH) == BR_PARAM_ANIM_GLOBAL_LFO);
    br_global_lfo_state.phase[0] = 0u;
    br_ui.redraw = 1u; br_draw();
    { static uint16_t first_frame[240u * 240u];
      memcpy(first_frame, fb, sizeof first_frame);
      br_global_lfo_state.phase[0] = 0x60000000u;
      br_ui.redraw = 0u; br_draw();
      assert(memcmp(first_frame, fb, sizeof first_frame) != 0); }
    assert(br_badge_global_lfo_phase == 48u && br_audio_global_lfo_phase() == 48u);
    br_ui.screen[BR_PAGE_LFO] = 0u;
    assert(br_param_animated(1u, BR_WIDGET_ORBIT) == BR_PARAM_ANIM_LFO1);
    br_ui.screen[BR_PAGE_LFO] = 1u;
    assert(br_param_animated(1u, BR_WIDGET_ORBIT) == BR_PARAM_ANIM_LFO2);
    /* Even with no destination, the audio engine keeps the shared oscillator moving. */
    { int32_t bus[BR_PARTS][BR_BLOCK] = {{0}};
      br_master.global_lfo.target = BR_GLOBAL_TARGET_OFF;
      br_global_lfo_state.phase[0] = 0u;
      br_master.global_lfo.rate = 32u; br_engine_render(bus, BR_BLOCK); br_engine_post_join_finish();
      uint32_t slow = br_global_lfo_state.phase[0];
      br_global_lfo_state.phase[0] = 0u;
      br_master.global_lfo.rate = 100u; br_engine_render(bus, BR_BLOCK); br_engine_post_join_finish();
      assert(slow > 0u && br_global_lfo_state.phase[0] > slow); }
}

static void test_mono_voice_modes(void)
{
    int32_t row[BR_BLOCK], key[BR_BLOCK] = {0}; int64_t energy = 0;
    br_app_init();
    br_part[0].sound.portamento = 100u; br_part[0].sound.glide_mode = BR_GLIDE_LEGATO;
    br_part[0].sound.amp_mode = 1u; br_part[0].revision += 2u;
    br_snapshot(0u); br_build_effective(0u);
    br_start_note_now(0u, 60u, 100u, 0u);
    assert(br_mono_glide[0].duration == 0u);
    br_start_note_now(0u, 72u, 100u, 1u);
    assert(br_mono_glide[0].duration > 0u);
    for (unsigned b=0; b<16u; ++b) {
        br_engine_source_part(0u, row, key, BR_BLOCK, 1u);
        br_engine_post_part_row(0u, row, key, BR_BLOCK);
        for (unsigned i=0; i<BR_BLOCK; ++i) energy += row[i] < 0 ? -row[i] : row[i];
    }
    assert(energy > 0 && br_mono_glide[0].elapsed > 0u &&
           br_mono_glide[0].elapsed < br_mono_glide[0].duration && br_mono_lpg[0] != 0);
    br_start_note_now(0u, 48u, 100u, 0u);
    assert(br_mono_glide[0].duration == 0u); /* non-overlap LEGATO is immediate */
}

int main(void)
{
    br_app_init();
    fm1_ms = 2001u; /* draw the application after the SMK startup screen */
    test_global_lfo();
    test_rate_law();
    test_rate_input_exponential();
    test_pitch_depth();
    test_filter_side();
    test_depth_text();
    test_attenuverters();
    test_mono_voice_modes();
    puts("lfo mod law: all checks passed");
    return 0;
}
