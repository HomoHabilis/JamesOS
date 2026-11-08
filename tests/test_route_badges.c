/* Exercise route indicators against the live UI state and modulation IDs. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
int main(void)
{
    /* Mixer pixels do not change when level routes are assigned, on either
     * the global mixer or the drum lane mixer. Other pages still show routes. */
    {
        static uint16_t before[240u * 240u];
        br_app_init(); br_load_preset(0, BR_FIRST_PLAITS);
        br_ui.part = 0u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u;
        br_draw_mixer(1u); memcpy(before, fb, sizeof before);
        br_part[0].mod.lfo[0].target = BR_TARGET_LEVEL;
        br_part[0].mod.route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_LEVEL, 80, 1u};
        assert(br_receive_badges(0, BR_TARGET_LEVEL));
        br_draw_mixer(1u); assert(!memcmp(before, fb, sizeof before));
        br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        br_draw_drum_levels(1u); memcpy(before, fb, sizeof before);
        br_part[BR_DRUM_PART].mod.lfo[0].target = BR_TARGET_LEVEL;
        br_part[BR_DRUM_PART].mod.route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_LEVEL, 80, 1u};
        br_draw_drum_levels(1u); assert(!memcmp(before, fb, sizeof before));
    }
    /* FX send frame begins at the same screen row as the mixer badge,
     * with its former lower edge unchanged. */
    br_app_init(); br_ui.part=0u; br_ui.page=BR_PAGE_FX; br_ui.screen[BR_PAGE_FX]=BR_FX_SENDS;
    br_ui.edit_key=0xffffffffu; br_draw_fx_send(1u);
    assert(fb[40u*240u+14u] == BR_LINE);
    assert(fb[38u*240u+14u] != BR_LINE);
    assert(fb[167u*240u+14u] == BR_LINE);
    br_app_init();
    br_load_preset(0, BR_FIRST_PLAITS);
    /* OFF PATCH source and target use the prohibition icon; ENABLE stays a switch. */
    br_ui.part = 0u; br_ui.page = BR_PAGE_ROUTE;
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        br_ui.screen[BR_PAGE_ROUTE] = slot;
        br_part[0].mod.route[slot] = (br_route_config_t){BR_SOURCE_OFF, BR_TARGET_OFF, 0, 0u};
        br_control_t off = br_control(3u);
        assert(br_control_widget(3u, &off) == BR_WIDGET_TOGGLE);
        br_control_t source = br_control(0u), target = br_control(1u);
        assert(br_control_widget(0u, &source) == BR_WIDGET_PATCH_OFF);
        assert(br_control_widget(1u, &target) == BR_WIDGET_PATCH_OFF);
        br_part[0].mod.route[slot] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_FILTER, 20, 1u};
        source = br_control(0u); target = br_control(1u);
        assert(br_control_widget(0u, &source) != BR_WIDGET_PATCH_OFF);
        assert(br_control_widget(1u, &target) != BR_WIDGET_PATCH_OFF);
        br_control_t on = br_control(3u);
        assert(br_control_widget(3u, &on) == BR_WIDGET_TOGGLE);
    }
    /* Both selectors resolve to the same canonical FILTER/RESONANCE artwork. */
    for (uint32_t target_id = BR_TARGET_FILTER; target_id <= BR_TARGET_RESONANCE; target_id += BR_TARGET_RESONANCE - BR_TARGET_FILTER) {
        uint32_t expected = target_id == BR_TARGET_FILTER ? BR_WIDGET_FILTER_TARGET : BR_WIDGET_RESONANCE_TARGET;
        br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0u;
        br_part[0].mod.lfo[0].target = target_id;
        br_control_t lfo = br_control(2u);
        assert(br_control_widget(2u, &lfo) == expected);
        br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        br_part[0].mod.route[0].target = target_id;
        br_control_t patch = br_control(1u);
        assert(br_control_widget(1u, &patch) == expected);
    }
    br_ui.page = BR_PAGE_ENV; br_ui.screen[BR_PAGE_ENV] = 2u;
    br_part[0].mod.retrigger = 0u;
    { br_control_t other = br_control(3u); assert(br_control_widget(3u, &other) == BR_WIDGET_TOGGLE); }
    br_app_init();
    br_load_preset(0, BR_FIRST_PLAITS);
    br_ui.part = 0; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0;
    br_part[0].sound.timbre_mod = br_part[0].sound.fm_mod = br_part[0].sound.morph_mod = 64;
    br_part[0].mod.lfo[0].target = BR_TARGET_TIMBRE;
    br_part[0].mod.lfo[1].target = BR_TARGET_TIMBRE;
    br_part[0].mod.route[0] = (br_route_config_t){BR_SOURCE_LFO1, BR_TARGET_TIMBRE, 0, 1};
    br_part[0].mod.route[1] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_TIMBRE, 20, 1};
    assert(br_control_route_badges(0) == 15u);
    assert(br_control_route_badges(1) == 0u);
    assert(br_send_badges(0, BR_SOURCE_LFO1) == BR_BADGE_SEND1);
    assert(br_send_badges(0, BR_SOURCE_ENV) == BR_BADGE_SEND2);
    br_part[0].mod.route[0].enabled = 0;
    assert(br_control_route_badges(0) == 11u);
    assert(br_send_badges(0, BR_SOURCE_LFO1) == 0u);
    br_part[0].sound.timbre_mod = 65;
    br_part[0].mod.lfo[0].target = BR_TARGET_OFF;
    assert(br_control_route_badges(0) == 11u);
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0;
    assert(br_page_route_badge() == 0u);
    br_part[0].mod.route[0].enabled = 1;
    assert(br_control_route_badges(0) == 0u);
    br_part[0].mod.lfo[0].target = BR_TARGET_TIMBRE;
    assert(br_control_route_badges(2) == 0u);
    br_ui.screen[BR_PAGE_LFO] = 1u;
    assert(br_page_route_badge() == 1u);
    assert(br_control_route_badges(0) == 0u);
    assert(br_control_route_badges(2) == 0u);
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    assert(br_page_route_badge() == 2u);
    assert(br_control_route_badges(0) == 0u);
    assert(br_control_route_badges(1) == 0u);
    br_ui.screen[BR_PAGE_ROUTE] = 1u;
    assert(br_page_route_badge() == 3u);
    assert(br_control_route_badges(0) == 0u);
    assert(br_control_route_badges(1) == 0u);
    br_ui.screen[BR_PAGE_ROUTE] = 2u;
    assert(br_page_route_badge() == 6u);
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0u;
    assert(br_control_route_badges(0) == 15u);
    br_ui.page = BR_PAGE_ENV; br_ui.screen[BR_PAGE_ENV] = 0;
    br_part[0].mod.lfo[0].target = BR_TARGET_ATTACK;
    assert(br_control_route_badges(0) == BR_BADGE_LFO1);
    br_ui.screen[BR_PAGE_ENV] = 1;
    assert(br_control_route_badges(0) == 0u);
    br_part[1].sound.preset = BR_FIRST_PLAITS;
    br_part[1].mod.lfo[0].target = BR_TARGET_LEVEL;
    assert(br_receive_badges(1, BR_TARGET_LEVEL) & BR_BADGE_LFO1);
    /* FX sends show assigned routes, including zero-depth assignments. */
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
    br_part[0].mod.lfo[0].target = BR_TARGET_DELAY;
    br_part[0].mod.route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_MOD, 0, 1u};
    br_part[0].mod.route[1] = (br_route_config_t){BR_SOURCE_LFO1, BR_TARGET_REVERB, 0, 1u};
    assert(br_control_route_badges(0) == 0u);
    assert(br_control_route_badges(1) & BR_BADGE_LFO1);
    assert(br_control_route_badges(2) & BR_BADGE_PATCH1);
    assert(br_control_route_badges(3) & BR_BADGE_PATCH2);
    assert(br_env_direct_overridden(0));
    br_part[0].mod.route[0].enabled = 0u;
    assert(!br_env_direct_overridden(0));
    br_part[0].mod.route[7] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_DELAY, 0, 1u};
    assert(br_env_direct_overridden(0));
    br_part[0].mod.route[7].target = BR_TARGET_OFF;
    assert(!br_env_direct_overridden(0));
    /* REVERB target depicts this track's send, independent of shared size. */
    br_part[0].sound.reverb = 23u; br_part[0].sound.space = 99u;
    assert(br_target_widget_value(BR_TARGET_REVERB) == 23u);
    br_part[0].sound.space = 14u;
    assert(br_target_widget_value(BR_TARGET_REVERB) == 23u);
    br_part[0].sound.preset = BR_KIT_808;
    assert(br_receive_badges(0, BR_TARGET_ATTACK) == 0u);
    /* Display reads the same signed source-times-amount used by the matrix. */
    br_cached_sound[0].preset = BR_FIRST_PLAITS;
    br_cached_mod[0].route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_TIMBRE, 127, 1u};
    br_mod_state[0].env = 0u;
    assert(br_audio_patch_value(0, 0) == 0);
    br_mod_state[0].env = 32767u;
    assert(br_audio_patch_value(0, 0) == 127);
    br_cached_mod[0].route[0].amount = -64;
    assert(br_audio_patch_value(0, 0) == -64);
    br_cached_mod[0].route[0].source = BR_SOURCE_LFO1;
    br_cached_mod[0].lfo[0].wave = BR_WAVE_SQUARE;
    br_mod_state[0].phase[0] = 0u;
    assert(br_audio_patch_value(0, 0) == -64);
    br_mod_state[0].phase[0] = 0x80000000u;
    assert(br_audio_patch_value(0, 0) == 64);
    br_cached_mod[0].route[0].enabled = 0u;
    assert(br_audio_patch_value(0, 0) == 0);
    br_badge_patch_value[0][0] = -127;
    uint32_t low = br_badge_pulse_key(0, BR_BADGE_PATCH1);
    br_badge_patch_value[0][0] = 127;
    uint32_t high = br_badge_pulse_key(0, BR_BADGE_PATCH1);
    assert(high > low);
    assert(br_badge_pulse_key(0, BR_BADGE_SEND1) == high);
    for (uint32_t part = 0; part < BR_PARTS; ++part) {
        for (uint32_t a = 0; a < 4u; ++a)
            for (uint32_t b = a + 1u; b < 4u; ++b)
                assert(br_route_badge_color(part, a) != br_route_badge_color(part, b));
        assert(br_route_badge_color(part, 2u) == br_route_badge_color(part, 4u));
        assert(br_route_badge_color(part, 3u) == br_route_badge_color(part, 5u));
        for (uint32_t a = 0; a < 4u; ++a)
            assert(br_route_badge_color(part, a) == br_route_badge_color(0u, a));
    }
    return 0;
}
