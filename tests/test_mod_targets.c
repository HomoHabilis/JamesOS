/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise the real UI, modulation arithmetic and v12 session roundtrip.
 * This host test does not model hardware scheduling or analog output. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void test_destinations(uint32_t preset)
{
    br_sound_t base, actual, expected;
    br_fx_config_t fx, result_fx, expected_fx;
    br_mod_config_t config;
    br_mod_state_t state;
    int32_t pitch;
    br_preset_sound(preset, &base);
    base.timbre = base.color = base.filter = base.level = base.decay = 64u;
    base.attack = base.sustain = base.release = base.filter_resonance = 64u;
    base.delay = base.reverb = base.plaits_harmonics = base.plaits_aux = 64u;
    for (uint32_t target = 0u; target < BR_TARGET_COUNT; ++target) {
        br_module_defaults(&config, &fx);
        fx.mod_mix = 64u;
        br_mod_init(&state, 1u);
        config.lfo[0].wave = BR_WAVE_SQUARE;
        config.lfo[0].target = target;
        config.lfo[0].depth = 32u;
        br_mod_effective(&state, &config, &base, &fx, 32767, &actual, &result_fx, &pitch);
        expected = base; expected_fx = fx;
        switch (target) {
        case BR_TARGET_PITCH:
            if (br_is_kit(preset)) expected.tune = 6;
            break;
        case BR_TARGET_TIMBRE: expected.timbre = 96u; break;
        case BR_TARGET_COLOR:
            if (preset != BR_KIT_808 && preset != BR_KIT_909) expected.color = 96u;
            break;
        case BR_TARGET_FILTER: expected.filter = 96u; break;
        case BR_TARGET_LEVEL: expected.level = 96u; break;
        case BR_TARGET_DECAY: expected.decay = 96u; break;
        case BR_TARGET_ATTACK: expected.attack = 96u; break;
        case BR_TARGET_SUSTAIN: expected.sustain = 96u; break;
        case BR_TARGET_RELEASE: expected.release = 96u; break;
        case BR_TARGET_RESONANCE: expected.filter_resonance = 96u; break;
        case BR_TARGET_DELAY: expected.delay = 96u; break;
        case BR_TARGET_REVERB: expected.reverb = 96u; break;
        case BR_TARGET_MOD: expected_fx.mod_mix = 96u; break;
        case BR_TARGET_HARMONICS:
            if (br_is_plaits(preset) || preset == BR_KIT_PLAITS) expected.plaits_harmonics = 96u;
            break;
        case BR_TARGET_OUTPUT:
            if (br_is_plaits(preset)) expected.plaits_aux = 96u;
            break;
        default: break; /* OFF and retired IDs must remain inert. */
        }
        if (target == BR_TARGET_PITCH && preset == BR_KIT_PLAITS) {
            fprintf(stderr, "Plaits kit pitch: tune=%d, expected=6\n", actual.tune);
        }
        assert(!memcmp(&actual, &expected, sizeof actual));
        assert(!memcmp(&result_fx, &expected_fx, sizeof result_fx));
        assert(pitch == (target == BR_TARGET_PITCH ? 32 * 24 * 128 / 127 : 0));
    }
    /* Matrix and envelope pitch must reach the same kit tuning consumer. */
    br_module_defaults(&config, &fx);
    br_mod_init(&state, 1u);
    config.route[0] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_PITCH, -64, 1u};
    state.velocity = 127u;
    br_mod_effective(&state, &config, &base, &fx, 0, &actual, &result_fx, &pitch);
    assert(pitch == -64 * 24 * 128 / 127);
    assert(actual.tune == (br_is_kit(preset) ? -12 : 0));
    config.route[0].enabled = 0u;
    config.target = BR_TARGET_PITCH; config.amount = 64; state.env = 32767u;
    br_mod_effective(&state, &config, &base, &fx, 0, &actual, &result_fx, &pitch);
    assert(actual.tune == (br_is_kit(preset) ? 12 : 0));
}

static void test_filter_envelope_patch(void)
{
    br_sound_t base,out; br_fx_config_t fx,fxout; br_mod_config_t cfg;
    br_mod_state_t state;int32_t pitch;
    br_preset_sound(BR_FIRST_PLAITS,&base);base.filter=64;base.timbre=64;
    br_module_defaults(&cfg,&fx);br_mod_init(&state,1u);state.env=32767u;
    assert(cfg.target==BR_TARGET_FILTER && cfg.amount==0);
    cfg.amount=32;
#define EFFECTIVE() br_mod_effective(&state,&cfg,&base,&fx,0,&out,&fxout,&pitch)
    EFFECTIVE();assert(out.filter==96 && out.timbre==64);
    cfg.route[0]=(br_route_config_t){BR_SOURCE_ENV,BR_TARGET_TIMBRE,16,1u};
    EFFECTIVE();assert(out.filter==64 && out.timbre==80);
    cfg.route[0].amount=0;EFFECTIVE();assert(out.filter==64 && out.timbre==64);
    cfg.route[0].target=BR_TARGET_FILTER;cfg.route[0].amount=16;
    EFFECTIVE();assert(out.filter==80); /* no doubled direct filter */
    cfg.route[0].target=BR_TARGET_OFF;EFFECTIVE();assert(out.filter==96);
    cfg.route[0]=(br_route_config_t){BR_SOURCE_ENV,BR_TARGET_TIMBRE,16,0u};
    EFFECTIVE();assert(out.filter==96 && out.timbre==64);
    cfg.route[0]=(br_route_config_t){BR_SOURCE_VELOCITY,BR_TARGET_TIMBRE,16,1u};state.velocity=127u;
    EFFECTIVE();assert(out.filter==96 && out.timbre==80);
    cfg.route[0].enabled=0;cfg.target=BR_TARGET_PITCH;
    EFFECTIVE();assert(out.filter==64 && pitch==32*24*128/127); /* legacy direct target */
    cfg.route[0]=(br_route_config_t){BR_SOURCE_ENV,BR_TARGET_TIMBRE,16,1u};
    cfg.route[1]=(br_route_config_t){BR_SOURCE_ENV,BR_TARGET_LEVEL,-8,1u};base.level=64;
    EFFECTIVE();assert(pitch==0 && out.timbre==80 && out.level==56);
    /* An unavailable saved destination must not disconnect the filter. */
    base.preset=BR_KIT_808;cfg.target=BR_TARGET_FILTER;
    cfg.route[0]=(br_route_config_t){BR_SOURCE_ENV,BR_TARGET_OUTPUT,16,1u};
    cfg.route[1].enabled=0;EFFECTIVE();assert(out.filter==96);
#undef EFFECTIVE
}

static void test_added_target_routes(void)
{
    static const uint32_t targets[] = {BR_TARGET_ATTACK, BR_TARGET_SUSTAIN,
        BR_TARGET_RELEASE, BR_TARGET_RESONANCE};
    br_sound_t base, out; br_fx_config_t fx, fxout; br_mod_config_t cfg;
    br_mod_state_t state; int32_t pitch;
    static const uint32_t presets[] = {BR_FIRST_PLAITS, BR_KIT_808, BR_KIT_909};
    for (uint32_t preset = 0u; preset < 3u; ++preset) {
        br_preset_sound(presets[preset], &base);
        base.attack = base.sustain = base.release = base.filter_resonance = 64u;
        for (uint32_t i=0; i<4u; ++i) {
            br_module_defaults(&cfg,&fx); br_mod_init(&state,1u);
            cfg.route[0]=(br_route_config_t){BR_SOURCE_VELOCITY,targets[i],-64,1u};
            state.velocity=127u;
            br_mod_effective(&state,&cfg,&base,&fx,0,&out,&fxout,&pitch);
            uint32_t actual=i==0?out.attack:i==1?out.sustain:i==2?out.release:out.filter_resonance;
            assert(actual==0u);
            cfg.route[0].enabled=0u;cfg.target=targets[i];cfg.amount=32;state.env=32767u;
            br_mod_effective(&state,&cfg,&base,&fx,0,&out,&fxout,&pitch);
            actual=i==0?out.attack:i==1?out.sustain:i==2?out.release:out.filter_resonance;
            assert(actual==96u);
            if (i<3u) {
                br_env_t env; memset(&env,0,sizeof env);br_env_refresh(&env,&out,1u);
                assert(env.attack==br_time_samples(out.attack,3u));
                assert(env.sustain==br_q15(out.sustain));
                assert(env.release==br_time_samples(out.release,8u));
            }
        }
    }
}

#ifndef MOD_TARGET_BASELINE_DSP
static void test_kit_filter_depth_text(void)
{
    char text[24];
    br_load_preset(BR_DRUM_PART, BR_KIT_808);
    br_ui.part = BR_DRUM_PART;
    br_ui.page = BR_PAGE_HOME;
    br_ui.screen[BR_PAGE_HOME] = 1u;
    br_part[BR_DRUM_PART].mod.target = BR_TARGET_FILTER;
    br_part[BR_DRUM_PART].mod.amount = 64;
    br_control_t c = br_control(2u);
    assert(!strcmp(c.label, "ENV DEPTH"));
    br_control_text(2u, &c, text);
    assert(!strcmp(text, "50%"));
}
#endif

static void test_unused_route_cache(void)
{
    br_sound_t base;
    br_fx_config_t fx;
    br_mod_config_t config;
    br_preset_sound(BR_KIT_PLAITS, &base);
    br_module_defaults(&config, &fx);
    config.lfo[0].target = BR_TARGET_OUTPUT;
    config.lfo[0].depth = 70u;
    assert(!br_mod_has_active_target(&config, &base));
    config.target = BR_TARGET_OUTPUT; config.amount = 64;
    config.route[0] = (br_route_config_t){BR_SOURCE_LFO1, BR_TARGET_OUTPUT, 64, 1u};
    assert(!br_mod_has_active_target(&config, &base));
    config.route[0].target = BR_TARGET_PITCH;
    assert(br_mod_has_active_target(&config, &base));
    config.route[0].enabled = 0u;
    base.preset = BR_FIRST_PLAITS;
    assert(br_mod_has_active_target(&config, &base));
}

#ifndef MOD_TARGET_BASELINE_DSP
static void test_menu(uint32_t preset, uint32_t expected_count)
{
    uint32_t part = br_is_kit(preset) ? BR_DRUM_PART : 0u;
    br_load_preset(part, preset); br_ui.part = part;
    assert(br_target_ui_count() == expected_count);
    uint32_t seen = 0u;
    for (uint32_t ordinal = 0u; ordinal < expected_count; ++ordinal) {
        uint32_t target = br_target_ui_value(ordinal);
        assert(target < BR_TARGET_COUNT && !(seen & (1u << target)));
        seen |= 1u << target;
        assert(br_target_ui_ordinal(target) == ordinal);
        assert(target != BR_TARGET_DRIVE && target != BR_TARGET_GATE);
        assert(strcmp(br_target_ui_name(target), "N/A"));
        for (uint32_t page = 0u; page < 3u; ++page) {
            if (page == 2u && part == BR_DRUM_PART) continue; /* kit-level page */
            br_ui.page = page == 0u ? BR_PAGE_LFO : page == 1u ? BR_PAGE_ENV : BR_PAGE_ROUTE;
            br_ui.screen[br_ui.page] = page == 1u ? 2u : 0u;
            uint32_t knob = page == 0u ? 2u : 1u;
            if (!page) br_part[part].mod.lfo[0].target = BR_TARGET_OFF;
            else if (page == 1u) br_part[part].mod.target = BR_TARGET_OFF;
            else br_part[part].mod.route[0].target = BR_TARGET_OFF;
            br_control_turn(knob, (int32_t)ordinal);
            uint32_t stored = !page ? br_part[part].mod.lfo[0].target :
                page == 1u ? br_part[part].mod.target : br_part[part].mod.route[0].target;
            assert(stored == target);
            br_control_t c = br_control(knob);
            assert(c.high == (int32_t)expected_count - 1 && c.value == (int32_t)ordinal);
            assert(!strcmp(c.text, br_target_ui_name(target)));
            if (page != 1u) {
                static const uint8_t expected[BR_TARGET_COUNT] = {
                    BR_WIDGET_TOGGLE, BR_WIDGET_POT, BR_WIDGET_TIMBRE, BR_WIDGET_COLOR,
                    BR_WIDGET_FILTER_TARGET, BR_WIDGET_FADER, BR_WIDGET_ADSR, BR_WIDGET_TOGGLE,
                    BR_WIDGET_DELAY, BR_WIDGET_REVERB, BR_WIDGET_FX_ENGINE, BR_WIDGET_HARMONICS,
                    BR_WIDGET_MIX, BR_WIDGET_TOGGLE, BR_WIDGET_ADSR, BR_WIDGET_ADSR,
                    BR_WIDGET_ADSR, BR_WIDGET_RESONANCE_TARGET, BR_WIDGET_POT, BR_WIDGET_POT
                };
                assert(br_control_widget(knob, &c) == (page == 2u && target == BR_TARGET_OFF ? BR_WIDGET_PATCH_OFF : expected[target]));
                if (target == BR_TARGET_ATTACK || target == BR_TARGET_DECAY ||
                    target == BR_TARGET_SUSTAIN || target == BR_TARGET_RELEASE) {
                    uint32_t stage = target == BR_TARGET_ATTACK ? 0u : target == BR_TARGET_DECAY ? 1u :
                        target == BR_TARGET_SUSTAIN ? 2u : 3u;
                    assert(br_control_widget_variant(knob, &c, BR_WIDGET_ADSR) == stage);
                }
                if (page == 2u) {
                    br_control_t source = br_control(0u);
                    assert(br_control_widget(0u, &source) == (br_source_control_id(0u) == BR_SOURCE_OFF ? BR_WIDGET_PATCH_OFF : br_source_widget(br_source_control_id(0u))));
                }
            }
            br_ui.redraw = 1u; br_draw(); /* host asserts display bounds */
        }
    }
    assert(!!(seen & (1u << BR_TARGET_OUTPUT)) == !!br_is_plaits(preset));
    assert(!!(seen & (1u << BR_TARGET_HARMONICS)) ==
        !!(br_is_plaits(preset) || preset == BR_KIT_PLAITS));
    assert(!!(seen & (1u << BR_TARGET_COLOR)) ==
        (preset != BR_KIT_808 && preset != BR_KIT_909));
    if (br_is_plaits(preset) || preset == BR_KIT_PLAITS)
        assert(!strcmp(br_target_ui_name(BR_TARGET_COLOR), "MORPH"));
    assert(!strcmp(br_target_ui_name(BR_TARGET_DELAY), "DELAY MIX"));
    assert(!strcmp(br_target_ui_name(BR_TARGET_REVERB), "REVERB SEND"));
    assert(!strcmp(br_target_ui_name(BR_TARGET_DECAY), "AMP DECAY"));
    if (preset == BR_KIT_808 || preset == BR_KIT_909) {
        assert(!(seen & (1u << BR_TARGET_TIMBRE)));
        assert(seen & (1u << BR_TARGET_ATTACK));
        assert(seen & (1u << BR_TARGET_SUSTAIN));
        assert(seen & (1u << BR_TARGET_RELEASE));
        assert(br_mod_target_supported(preset, BR_TARGET_TIMBRE)); /* saved routes still work */
    }
    /* Inapplicable saved IDs remain intact and are visibly unavailable. */
    if (br_is_kit(preset)) {
        br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0u;
        br_part[part].mod.lfo[0].target = BR_TARGET_OUTPUT;
        assert(!strcmp(br_control(2u).text, "N/A"));
        br_control_t unavailable = br_control(2u);
        assert(br_control_widget(2u, &unavailable) == BR_WIDGET_TOGGLE);
        assert(br_target_widget_value(BR_TARGET_OUTPUT) == 0u);
        assert(br_part[part].mod.lfo[0].target == BR_TARGET_OUTPUT);
        br_control_turn(2u, -1);
        assert(br_part[part].mod.lfo[0].target == BR_TARGET_OFF);
    }
}

static void test_patch_source_icons(void)
{
    static const uint8_t expected[BR_SOURCE_COUNT] = {
        BR_WIDGET_PATCH_OFF, BR_WIDGET_WAVE, BR_WIDGET_MIX, BR_WIDGET_ADSR,
        BR_WIDGET_ADSR, BR_WIDGET_WAVE, BR_WIDGET_WAVE, BR_WIDGET_TOGGLE,
        BR_WIDGET_FADER, BR_WIDGET_POT, BR_WIDGET_WAVE,
        BR_WIDGET_GATE, BR_WIDGET_WAVE, BR_WIDGET_WAVE, BR_WIDGET_WAVE, BR_WIDGET_WAVE
    };
    br_load_preset(0,BR_FIRST_PLAITS);br_ui.part=0;br_ui.page=BR_PAGE_ROUTE;
    br_part[0].mod.lfo[0].wave=BR_WAVE_TRIANGLE;
    br_part[0].mod.lfo[1].wave=BR_WAVE_SQUARE;
    for(uint32_t page=0;page<2;page++) {
        br_ui.screen[BR_PAGE_ROUTE]=page;
        for(uint32_t i=0;i<BR_SOURCE_UI_COUNT;i++) {
            uint32_t source=br_source_ui_id[i];
            br_part[0].mod.route[page].source=source;
            br_control_t c=br_control(0);
            assert(br_source_control_id(0)==source);
            if(source==BR_SOURCE_ENV)assert(!strcmp(c.text,"FILTER ENV"));
            assert(br_control_widget(0,&c)==expected[source]);
            if(source==BR_SOURCE_AMP || source==BR_SOURCE_ENV)
                assert(br_control_widget_variant(0,&c,BR_WIDGET_ADSR)==4u);
            if(source==BR_SOURCE_LFO1 || source==BR_SOURCE_LFO2)
                assert(br_control_widget_variant(0,&c,BR_WIDGET_WAVE)==br_part[0].mod.lfo[source-BR_SOURCE_LFO1].wave);
            if(source==BR_SOURCE_RANDOM)
                assert(br_control_widget_variant(0,&c,BR_WIDGET_WAVE)==BR_WAVE_RANDOM);
            if(source==BR_SOURCE_LFO1_TRI || source==BR_SOURCE_LFO2_TRI)
                assert(br_control_widget_variant(0,&c,BR_WIDGET_WAVE)==BR_WAVE_TRIANGLE);
            if(source==BR_SOURCE_LFO1_SQU || source==BR_SOURCE_LFO2_SQU)
                assert(br_control_widget_variant(0,&c,BR_WIDGET_WAVE)==BR_WAVE_SQUARE);
            br_ui.redraw=1;br_draw();
        }
    }
    br_part[0].mod.route[0].source=BR_SOURCE_LFO3;
    br_ui.screen[BR_PAGE_ROUTE]=0;
    assert(br_source_control_id(0)==BR_SOURCE_OFF);
    br_ui.screen[BR_PAGE_ROUTE]=2;
    assert(br_source_control_id(0)==BR_SOURCE_COUNT);
}

static void test_saved_ids(void)
{
    static br_session_t before, after;
    /* Literal wire IDs protect previously saved routes from menu reordering. */
    assert(BR_TARGET_COLOR == 3 && BR_TARGET_OUTPUT == 12 && BR_TARGET_GATE == 13);
    for (uint32_t id = 0u; id < BR_TARGET_COUNT; ++id) {
        br_part[0].mod.target = id;
        br_part[0].mod.lfo[0].target = id;
        br_part[0].mod.route[0] = (br_route_config_t){BR_SOURCE_LFO2, id, -42, 1u};
        assert(br_capture(&before));
        assert(br_restore(&before));
        assert(br_capture(&after));
        assert(!memcmp(&before, &after, sizeof before));
        assert(br_part[0].mod.target == id && br_part[0].mod.lfo[0].target == id);
        assert(br_part[0].mod.route[0].target == id);
    }
}

static void test_page_headings_and_attenuverters(void)
{
    static const struct { uint32_t page, screen; const char *title; } cases[] = {
        {BR_PAGE_FX,0,"FX / SENDS"},{BR_PAGE_FX,1,"FX / DRIVE"},{BR_PAGE_FX,2,"FX / DELAY"},{BR_PAGE_FX,3,"FX / MOD"},{BR_PAGE_FX,4,"FX / REVERB"},
        {BR_PAGE_PATTERN,0,"ARP"},
        {BR_PAGE_SEQ,0,"SEQUENCER"},{BR_PAGE_MIX,0,"MIXER"},{BR_PAGE_MIX,1,"GLOBAL / FILTER"},
        {BR_PAGE_MIX,2,"SETTINGS"},{BR_PAGE_MIX,3,"GLOBAL / PAN"},{BR_PAGE_MIX,4,"COMPRESSOR"},
        {BR_PAGE_MIX,5,"COMP KNOBS"},{BR_PAGE_ROUTE,0,"PATCH 1"},{BR_PAGE_ROUTE,1,"PATCH 2"},
        {BR_PAGE_ROUTE,2,"LFO DEPTHS"},{BR_PAGE_LFO,0,"LFO 1"},{BR_PAGE_LFO,1,"LFO 2"},
        {BR_PAGE_ENV,0,"ENV / AMP"},{BR_PAGE_ENV,1,"ENV / FILTER"},{BR_PAGE_ENV,2,"ENV / SETUP"},{BR_PAGE_SCALE,0,"SOUNDS"}
    };
    br_ui.part=0; br_load_preset(0,BR_FIRST_PLAITS);
    for(uint32_t i=0;i<sizeof cases/sizeof cases[0];++i){
        br_ui.page=cases[i].page;br_ui.screen[br_ui.page]=cases[i].screen;
        assert(!strcmp(br_header_title(),cases[i].title));
        br_ui.redraw=1;br_draw();
    }
    br_ui.page=BR_PAGE_HOME;br_ui.screen[BR_PAGE_HOME]=1u;
    br_control_t resonance=br_control(1u);
    assert(!strcmp(resonance.label,"RESONANCE"));
    assert(br_control_widget(1u,&resonance)==BR_WIDGET_RESONANCE);
    br_ui.page=BR_PAGE_LFO;assert(br_screen_count()==2u);
    br_ui.page=BR_PAGE_ROUTE;assert(br_screen_count()==3u);
    br_ui.screen[BR_PAGE_ROUTE]=2u;
    br_part[0].sound.timbre_mod=64u;br_part[0].sound.fm_mod=64u;br_part[0].sound.morph_mod=64u;
    br_route_config_t before=br_part[0].mod.route[0];
    assert(!strcmp(br_control(0).label,"TIMBRE"));
    assert(!strcmp(br_control(1).label,"PITCH"));
    assert(!strcmp(br_control(2).label,"MORPH"));
    br_control_turn(0,1);br_control_turn(1,2);br_control_turn(2,3);
    assert(br_part[0].sound.timbre_mod==65u && br_part[0].sound.fm_mod==66u && br_part[0].sound.morph_mod==67u);
    assert(!memcmp((const void *)&br_part[0].mod.route[0],&before,sizeof before));
    br_ui.part=BR_DRUM_PART;br_load_preset(BR_DRUM_PART,BR_KIT_808);
    br_ui.page=BR_PAGE_ROUTE;br_ui.screen[BR_PAGE_ROUTE]=3;
    assert(br_screen_count()==1u && !strcmp(br_header_title(),"KIT LEVEL"));
}
#endif

/* The patch panel's jacks: LFO TRI and LFO SQU (independent of each LFO's own wave), GATE, and the LFO
 * RATE inputs. */
static void test_panel_jacks(void)
{
    br_sound_t base, out; br_fx_config_t fx, fxout; br_mod_config_t cfg; br_mod_state_t st; int32_t pitch;
    br_preset_sound(BR_FIRST_PLAITS, &base); base.timbre = 64u; base.level = 64u;
    br_module_defaults(&cfg, &fx); br_mod_init(&st, 1u);
#define EFFECTIVE() br_mod_effective(&st, &cfg, &base, &fx, 0, &out, &fxout, &pitch)
    cfg.target = BR_TARGET_OFF;
    /* Triangle and square outputs follow the LFO's phase whatever its wave is set to. */
    cfg.lfo[0].wave = BR_WAVE_SAW; cfg.lfo[1].wave = BR_WAVE_SAW;
    cfg.route[0] = (br_route_config_t){BR_SOURCE_LFO1_SQU, BR_TARGET_TIMBRE, 32, 1u};
    st.phase[0] = 0x10000000u; EFFECTIVE(); assert(out.timbre == 96u);          /* square, high half */
    st.phase[0] = 0xC0000000u; EFFECTIVE(); assert(out.timbre == 32u);          /* square, low half */
    cfg.route[0].source = BR_SOURCE_LFO1_TRI;
    st.phase[0] = 0x40000000u; EFFECTIVE(); assert(out.timbre == 96u);          /* triangle peak */
    st.phase[0] = 0x20000000u; EFFECTIVE(); assert(out.timbre > 64u && out.timbre < 96u);   /* mid-rise: not a square */
    cfg.route[0].source = BR_SOURCE_LFO2_SQU; st.phase[1] = 0x10000000u; st.phase[0] = 0xC0000000u;
    EFFECTIVE(); assert(out.timbre == 96u);                                      /* LFO 2 has its own */
    cfg.route[0].source = BR_SOURCE_LFO2_TRI; st.phase[1] = 0x40000000u; EFFECTIVE(); assert(out.timbre == 96u);
    /* GATE is up while the note is held and down in release. */
    cfg.route[0] = (br_route_config_t){BR_SOURCE_GATE, BR_TARGET_LEVEL, 32, 1u};
    st.stage = BR_MOD_ENV_ATTACK;  EFFECTIVE(); assert(out.level == 96u);
    st.stage = BR_MOD_ENV_SUSTAIN; EFFECTIVE(); assert(out.level == 96u);
    st.stage = BR_MOD_ENV_RELEASE; EFFECTIVE(); assert(out.level == 64u);
    st.stage = BR_MOD_ENV_OFF;     EFFECTIVE(); assert(out.level == 64u);
    /* The LFO RATE inputs speed an LFO up or slow it down, a block later. */
    {
        br_mod_state_t a, b, c2;
        uint32_t base_step, fast_step, slow_step;
        br_module_defaults(&cfg, &fx); cfg.target = BR_TARGET_OFF; cfg.lfo[0].rate = 64u;
        br_mod_init(&a, 1u); br_mod_init(&b, 1u); br_mod_init(&c2, 1u);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
        br_mod_advance(&a, &cfg, 256u, 0u);
#else
        br_mod_advance(&a, &cfg, 256u);
#endif
        base_step = a.phase[0];
        cfg.route[0] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_LFO1_RATE, 40, 1u};
        b.velocity = 127u; br_mod_effective(&b, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
        assert(b.lfo_rate_mod[0] == 40 && b.lfo_rate_mod[1] == 0);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
        br_mod_advance(&b, &cfg, 256u, 1u);
#else
        br_mod_advance(&b, &cfg, 256u);
#endif
        fast_step = b.phase[0];
        cfg.route[0].amount = -40;
        c2.velocity = 127u; br_mod_effective(&c2, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
        br_mod_advance(&c2, &cfg, 256u, 2u);
#else
        br_mod_advance(&c2, &cfg, 256u);
#endif
        slow_step = c2.phase[0];
        assert(fast_step > base_step && slow_step < base_step && c2.phase[1] != 0u);
        cfg.route[0].enabled = 0u; br_mod_effective(&b, &cfg, &base, &fx, 0, &out, &fxout, &pitch);
        assert(b.lfo_rate_mod[0] == 0);                                          /* patch removed: back to the knob */
    }
#undef EFFECTIVE
    /* A route into the LFO RATE input reaches the right LFO from the UI and through a saved session. */
    br_app_init();
    br_ui.part = 0u; br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 1u;
    br_part[0].mod.route[0] = (br_route_config_t){BR_SOURCE_GATE, BR_TARGET_LFO2_RATE, 10, 1u};
    assert(br_control_route_badges(1u) != 0u);                                   /* LFO 2's RATE knob shows the route */
    br_ui.screen[BR_PAGE_LFO] = 0u;
    assert(br_control_route_badges(1u) == 0u);                                   /* LFO 1's does not */
    for (uint32_t source = BR_SOURCE_GATE; source <= BR_SOURCE_LFO2_SQU; ++source)
        for (uint32_t target = BR_TARGET_LFO1_RATE; target <= BR_TARGET_LFO2_RATE; ++target) {
            static br_session_t before, after;
            br_part[0].mod.route[3] = (br_route_config_t){source, target, -20, 1u};
            assert(br_capture(&before) && br_restore(&before) && br_capture(&after));
            assert(!memcmp(&before, &after, sizeof before));
            assert(br_part[0].mod.route[3].source == source && br_part[0].mod.route[3].target == target);
        }
}

int main(void)
{
    assert(!strcmp(br_source_name[BR_SOURCE_RANDOM], "RANDOM"));
    br_app_init(); fm1_ms = 2000u; br_ui.message_until = 0u;
    const uint32_t presets[] = {BR_FIRST_PLAITS, BR_KIT_BRAIDS, BR_KIT_808, BR_KIT_909, BR_KIT_PLAITS};
    for (uint32_t i = 0; i < 5u; ++i) test_destinations(presets[i]);
#ifndef MOD_TARGET_BASELINE_DSP
    test_filter_envelope_patch();
    test_added_target_routes();
    test_unused_route_cache();
    test_kit_filter_depth_text();
    for (uint32_t preset = BR_FIRST_PLAITS; preset < BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT; ++preset)
        test_menu(preset, 18u);
    test_menu(BR_KIT_BRAIDS, 16u); test_menu(BR_KIT_808, 14u);
    test_menu(BR_KIT_909, 14u); test_menu(BR_KIT_PLAITS, 17u);
    test_patch_source_icons();
    test_saved_ids();
    test_panel_jacks();
    test_page_headings_and_attenuverters();
#endif
    puts("PASS: modulation destinations, all current engine menus, and saved routing IDs");
    return 0;
}
