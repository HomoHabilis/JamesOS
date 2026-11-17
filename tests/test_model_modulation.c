/* SPDX-License-Identifier: GPL-3.0-only */
/* Experimental MODEL destination: real modulation, UI and session logic. */
#define DINGHY_HOST 1
#define BR_MODEL_MOD_EXPERIMENT 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

int main(void)
{
    br_sound_t base, actual, expected;
    br_fx_config_t fx, actual_fx;
    br_mod_config_t cfg;
    br_mod_state_t state;
    int32_t pitch;
    br_app_init();
    br_preset_sound(BR_FIRST_PLAITS + 11u, &base);
    br_module_defaults(&cfg, &fx); br_mod_init(&state, 1u);
    cfg.lfo[0].wave = BR_WAVE_SQUARE; cfg.lfo[0].target = BR_TARGET_MODEL;
    cfg.lfo[0].depth = 127u;
    br_mod_effective(&state, &cfg, &base, &fx, 0, &actual, &actual_fx, &pitch);
    expected = base; expected.preset = BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT - 1u;
    assert(!memcmp(&actual, &expected, sizeof actual) && !memcmp(&actual_fx, &fx, sizeof fx));
    state.phase[0] = 0x80000000u;
    br_mod_effective(&state, &cfg, &base, &fx, 0, &actual, &actual_fx, &pitch);
    expected.preset = BR_FIRST_PLAITS;
    assert(!memcmp(&actual, &expected, sizeof actual));
    cfg.lfo[0].depth = 0u;
    br_mod_effective(&state, &cfg, &base, &fx, 0, &actual, &actual_fx, &pitch);
    assert(!memcmp(&actual, &base, sizeof actual));
    assert(br_mod_target_supported(BR_FIRST_PLAITS, BR_TARGET_MODEL));
    assert(!br_mod_target_supported(BR_KIT_BRAIDS, BR_TARGET_MODEL));
    assert(!br_mod_target_supported(BR_KIT_PLAITS, BR_TARGET_MODEL));
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0u; br_ui.part = 0u;
    assert(br_target_ui_available(BR_TARGET_MODEL) && !strcmp(br_target_ui_name(BR_TARGET_MODEL), "MODEL"));
    br_ui.part = BR_DRUM_PART;
    assert(!br_target_ui_available(BR_TARGET_MODEL));
    br_part[0].mod.lfo[0].target = BR_TARGET_MODEL;
    static br_session_t saved;
    assert(br_capture(&saved));
    br_part[0].mod.lfo[0].target = BR_TARGET_OFF;
    assert(br_restore(&saved) && br_part[0].mod.lfo[0].target == BR_TARGET_MODEL);
    {
        static int32_t bus[BR_PARTS][BR_BLOCK];
        br_part[0].sound = base;
        br_part[0].mod.lfo[0].wave = BR_WAVE_SQUARE;
        br_part[0].mod.lfo[0].depth = 127u;
        br_part[0].mod.lfo[0].target = BR_TARGET_MODEL;
        br_part[0].revision += 2u;
        br_snapshot(0u); br_select_mode(0u);
        br_mod_state[0].phase[0] = 0u; br_build_effective(0u);
        br_start_note_now(0u, 60u, 100u, 0u);
        br_engine_render(bus, BR_BLOCK);
        assert(plaits_active_engine(br_dsp[0].plaits) == (int32_t)BR_PLAITS_MODEL_COUNT - 1);
        br_mod_state[0].phase[0] = 0x80000000u;
        br_engine_render(bus, BR_BLOCK);
        assert(plaits_active_engine(br_dsp[0].plaits) == 0);
        assert(br_part[0].sound.preset == base.preset);
    }
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) {
        br_model_mono_base[p] = br_model_mono_applied[p] = 255u;
        br_cached_sound[p].preset = BR_FIRST_PLAITS;
        br_effective_sound[p].preset = BR_FIRST_PLAITS + 18u;
    }
    for (unsigned frame = 0; frame < BR_SYNTH_PARTS; ++frame) {
        unsigned changed = 0;
        for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) br_effective_sound[p].preset = BR_FIRST_PLAITS + 18u;
        br_model_mono_limit();
        for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p)
            changed += br_effective_sound[p].preset == BR_FIRST_PLAITS + 18u;
        assert(changed == frame + 1u);
    }
    puts("experimental model target: depth, range, kit exclusion and session passed");
    return 0;
}
