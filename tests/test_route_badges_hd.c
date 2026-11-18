/* SPDX-License-Identifier: GPL-3.0-only */
/* HD route indicators: the
 * badge logic against the live UI state and modulation IDs, the mixer and kit
 * mixer unchanged by level routes, the HD graphics for OFF PATCH source/target (PATCH OFF), ENABLE
 * (the HD enable switch) and RETRIGGER (envelope mode), the FX send well's place under the header,
 * and distinct, shared badge colours. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
int main(int argc, char **argv)
{
    if (argc > 1) hd_crop_dir = argv[1];
    /* Mixer pixels do not change when level routes are assigned, on either
     * the global mixer or the drum lane mixer. Other pages still show routes. */
    {
        static uint16_t before[240u * 240u];
        br_app_init(); host_after_intro(); br_load_preset(0, BR_FIRST_PLAITS);
        br_ui.part = 0u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u;
        br_draw_mixer(1u); memcpy(before, fb, sizeof before);
        br_part[0].mod.lfo[0].target = BR_TARGET_LEVEL;
        br_part[0].mod.route[BR_ROUTES] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_LEVEL, 80, 1u};
        HD_CHECK(br_receive_badges(0, BR_TARGET_LEVEL), "br_receive_badges(0, BR_TARGET_LEVEL)");
        br_draw_mixer(1u); HD_CHECK(!memcmp(before, fb, sizeof before), "!memcmp(before, fb, sizeof before)");
        br_ui.part = BR_DRUM_PART; br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        br_draw_drum_levels(1u); memcpy(before, fb, sizeof before);
        br_part[BR_DRUM_PART].mod.lfo[0].target = BR_TARGET_LEVEL;
        br_part[BR_DRUM_PART].mod.route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_LEVEL, 80, 1u};
        br_draw_drum_levels(1u); HD_CHECK(!memcmp(before, fb, sizeof before), "!memcmp(before, fb, sizeof before)");
    }
    /* FX send frame begins at the same screen row as the mixer badge,
     * with its former lower edge unchanged. */
    br_app_init(); host_after_intro(); br_ui.part=0u; br_ui.page=BR_PAGE_FX; br_ui.screen[BR_PAGE_FX]=BR_FX_SENDS;
    br_ui.edit_key=0xffffffffu; br_draw_fx_send(1u);
    {   /* HD: the send well's outline (column 14) spans rows 41..166, inside the standard frame's
         * 40..167, and starts on the row where the mixer's track badge starts. */
        int32_t top = -1, bottom = -1, badge = -1;
        /* the HD ground is shaded, so the outline is the first and last clearly lit row */
        for (int32_t y = 32; y < 180; ++y) if (hd_luma(fb[y * 240 + 14]) >= 100u) { if (top < 0) top = y; bottom = y; }
        HD_CHECK(top == 41 && bottom == 166, "FX send well outline rows %d..%d (41..166)", top, bottom);
        br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u; br_draw_layout_old = 0xFFFFFFFFu;
        br_ui.redraw = 1u; br_draw();
        for (int32_t y = 32; y < 80 && badge < 0; ++y) for (int32_t x = 4; x < 56; ++x)
            if (hd_near(fb[y * 240 + x], br_track_color(0u), 4u, 0)) { badge = y; break; }
        HD_CHECK(badge >= 0 && abs(badge - top) <= 1, "FX send well starts on the mixer badge row (%d vs %d)", top, badge);
    }
    br_app_init(); host_after_intro();
    br_load_preset(0, BR_FIRST_PLAITS);
    /* OFF PATCH source and target use the prohibition icon; ENABLE stays a switch. */
    br_ui.part = 0u; br_ui.page = BR_PAGE_ROUTE;
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        br_ui.screen[BR_PAGE_ROUTE] = slot;
        br_part[0].mod.route[BR_ROUTES + slot] = (br_route_config_t){BR_SOURCE_OFF, BR_TARGET_OFF, 0, 0u};
        br_control_t off = br_control(3u);
        HD_CHECK(br_control_widget(3u, &off) == BR_WIDGET_ENABLE, "slot %u: ENABLE is the HD enable switch (off)", slot);
        br_control_t source = br_control(0u), target = br_control(1u);
        HD_CHECK(br_control_widget(0u, &source) == BR_WIDGET_PATCH_OFF, "br_control_widget(0u, &source) == BR_WIDGET_PATCH_OFF");
        HD_CHECK(br_control_widget(1u, &target) == BR_WIDGET_PATCH_OFF, "br_control_widget(1u, &target) == BR_WIDGET_PATCH_OFF");
        br_part[0].mod.route[BR_ROUTES + slot] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_FILTER, 20, 1u};
        source = br_control(0u); target = br_control(1u);
        HD_CHECK(br_control_widget(0u, &source) != BR_WIDGET_PATCH_OFF, "br_control_widget(0u, &source) != BR_WIDGET_PATCH_OFF");
        HD_CHECK(br_control_widget(1u, &target) != BR_WIDGET_PATCH_OFF, "br_control_widget(1u, &target) != BR_WIDGET_PATCH_OFF");
        br_control_t on = br_control(3u);
        HD_CHECK(br_control_widget(3u, &on) == BR_WIDGET_ENABLE, "slot %u: ENABLE is the HD enable switch (on)", slot);
    }
    /* Both selectors resolve to the same canonical FILTER/RESONANCE artwork. */
    for (uint32_t target_id = BR_TARGET_FILTER; target_id <= BR_TARGET_RESONANCE; target_id += BR_TARGET_RESONANCE - BR_TARGET_FILTER) {
        uint32_t expected = target_id == BR_TARGET_FILTER ? BR_WIDGET_FILTER_TARGET : BR_WIDGET_RESONANCE_TARGET;
        br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0u;
        br_part[0].mod.lfo[0].target = target_id;
        br_control_t lfo = br_control(2u);
        HD_CHECK(br_control_widget(2u, &lfo) == expected, "br_control_widget(2u, &lfo) == expected");
        br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
        br_part[0].mod.route[BR_ROUTES].target = target_id;
        br_control_t patch = br_control(1u);
        HD_CHECK(br_control_widget(1u, &patch) == expected, "br_control_widget(1u, &patch) == expected");
    }
    br_ui.page = BR_PAGE_ENV; br_ui.screen[BR_PAGE_ENV] = 2u;
    br_part[0].mod.retrigger = 0u;
    { br_control_t other = br_control(3u); HD_CHECK(br_control_widget(3u, &other) == BR_WIDGET_ENV_MODE, "RETRIGGER is the HD envelope-mode graphic"); }
    br_app_init(); host_after_intro();
    br_load_preset(0, BR_FIRST_PLAITS);
    br_ui.part = 0; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0;
    br_part[0].sound.timbre_mod = br_part[0].sound.fm_mod = br_part[0].sound.morph_mod = 64;
    br_part[0].mod.lfo[0].target = BR_TARGET_TIMBRE;
    br_part[0].mod.lfo[1].target = BR_TARGET_TIMBRE;
    br_part[0].mod.route[BR_ROUTES] = (br_route_config_t){BR_SOURCE_LFO1, BR_TARGET_TIMBRE, 0, 1};
    br_part[0].mod.route[BR_ROUTES + 1u] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_TIMBRE, 20, 1};
    HD_CHECK(br_control_route_badges(0) == 15u, "br_control_route_badges(0) == 15u");
    HD_CHECK(br_control_route_badges(1) == 0u, "br_control_route_badges(1) == 0u");
    HD_CHECK(br_send_badges(0, BR_SOURCE_LFO1) == BR_BADGE_SEND1, "br_send_badges(0, BR_SOURCE_LFO1) == BR_BADGE_SEND1");
    HD_CHECK(br_send_badges(0, BR_SOURCE_ENV) == BR_BADGE_SEND2, "br_send_badges(0, BR_SOURCE_ENV) == BR_BADGE_SEND2");
    br_part[0].mod.route[BR_ROUTES].enabled = 0;
    HD_CHECK(br_control_route_badges(0) == 11u, "br_control_route_badges(0) == 11u");
    HD_CHECK(br_send_badges(0, BR_SOURCE_LFO1) == 0u, "br_send_badges(0, BR_SOURCE_LFO1) == 0u");
    br_part[0].sound.timbre_mod = 65;                 /* an attenuverter routes nothing by itself */
    br_part[0].mod.lfo[0].target = BR_TARGET_OFF;
    HD_CHECK(br_control_route_badges(0) == 10u, "br_control_route_badges(0) == 10u");
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0;
    HD_CHECK(br_page_route_badge() == 0u, "br_page_route_badge() == 0u");
    br_part[0].mod.route[BR_ROUTES].enabled = 1;
    HD_CHECK(br_control_route_badges(0) == 0u, "br_control_route_badges(0) == 0u");
    br_part[0].mod.lfo[0].target = BR_TARGET_TIMBRE;
    HD_CHECK(br_control_route_badges(2) == 0u, "br_control_route_badges(2) == 0u");
    br_ui.screen[BR_PAGE_LFO] = 1u;
    HD_CHECK(br_page_route_badge() == 1u, "br_page_route_badge() == 1u");
    HD_CHECK(br_control_route_badges(0) == 0u, "br_control_route_badges(0) == 0u");
    HD_CHECK(br_control_route_badges(2) == 0u, "br_control_route_badges(2) == 0u");
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    HD_CHECK(br_page_route_badge() == 2u, "br_page_route_badge() == 2u");
    HD_CHECK(br_control_route_badges(0) == 0u, "br_control_route_badges(0) == 0u");
    HD_CHECK(br_control_route_badges(1) == 0u, "br_control_route_badges(1) == 0u");
    br_ui.screen[BR_PAGE_ROUTE] = 1u;
    HD_CHECK(br_page_route_badge() == 3u, "br_page_route_badge() == 3u");
    HD_CHECK(br_control_route_badges(0) == 0u, "br_control_route_badges(0) == 0u");
    HD_CHECK(br_control_route_badges(1) == 0u, "br_control_route_badges(1) == 0u");
    br_ui.screen[BR_PAGE_ROUTE] = 2u;
    HD_CHECK(br_page_route_badge() == 6u, "br_page_route_badge() == 6u");
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0u;
    HD_CHECK(br_control_route_badges(0) == 15u, "br_control_route_badges(0) == 15u");
    br_ui.page = BR_PAGE_ENV; br_ui.screen[BR_PAGE_ENV] = 0;
    br_part[0].mod.lfo[0].target = BR_TARGET_ATTACK;
    HD_CHECK(br_control_route_badges(0) == BR_BADGE_LFO1, "br_control_route_badges(0) == BR_BADGE_LFO1");
    br_ui.screen[BR_PAGE_ENV] = 1;
    HD_CHECK(br_control_route_badges(0) == 0u, "br_control_route_badges(0) == 0u");
    br_part[1].sound.preset = BR_FIRST_PLAITS;
    br_part[1].mod.lfo[0].target = BR_TARGET_LEVEL;
    HD_CHECK(br_receive_badges(1, BR_TARGET_LEVEL) & BR_BADGE_LFO1, "br_receive_badges(1, BR_TARGET_LEVEL) & BR_BADGE_LFO1");
    /* FX sends show assigned routes, including zero-depth assignments. */
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
    br_part[0].mod.lfo[0].target = BR_TARGET_DELAY;
    br_part[0].mod.route[BR_ROUTES] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_MOD, 0, 1u};
    br_part[0].mod.route[BR_ROUTES + 1u] = (br_route_config_t){BR_SOURCE_LFO1, BR_TARGET_REVERB, 0, 1u};
    HD_CHECK(br_control_route_badges(0) == 0u, "br_control_route_badges(0) == 0u");
    HD_CHECK(br_control_route_badges(1) & BR_BADGE_LFO1, "br_control_route_badges(1) & BR_BADGE_LFO1");
    HD_CHECK(br_control_route_badges(2) & BR_BADGE_PATCH1, "br_control_route_badges(2) & BR_BADGE_PATCH1");
    HD_CHECK(br_control_route_badges(3) & BR_BADGE_PATCH2, "br_control_route_badges(3) & BR_BADGE_PATCH2");
    HD_CHECK(br_env_direct_overridden(0), "br_env_direct_overridden(0)");
    br_part[0].mod.route[BR_ROUTES].enabled = 0u;
    HD_CHECK(!br_env_direct_overridden(0), "!br_env_direct_overridden(0)");
    br_part[0].mod.route[7] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_DELAY, 0, 1u};
    HD_CHECK(br_env_direct_overridden(0), "br_env_direct_overridden(0)");
    br_part[0].mod.route[7].target = BR_TARGET_OFF;
    HD_CHECK(!br_env_direct_overridden(0), "!br_env_direct_overridden(0)");
    /* REVERB target depicts this track's send, independent of shared size. */
    br_part[0].sound.reverb = 23u; br_master.reverb_size = 99u;
    HD_CHECK(br_target_widget_value(BR_TARGET_REVERB) == 23u, "br_target_widget_value(BR_TARGET_REVERB) == 23u");
    br_master.reverb_size = 14u;
    HD_CHECK(br_target_widget_value(BR_TARGET_REVERB) == 23u, "br_target_widget_value(BR_TARGET_REVERB) == 23u");
    br_part[0].sound.preset = BR_KIT_808;
    HD_CHECK(br_receive_badges(0, BR_TARGET_ATTACK) == 0u, "br_receive_badges(0, BR_TARGET_ATTACK) == 0u");
    /* Display reads the same signed source-times-amount used by the matrix. */
    br_cached_sound[0].preset = BR_FIRST_PLAITS;
    br_cached_mod[0].route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_TIMBRE, 127, 1u};
    br_mod_state[0].env = 0u;
    HD_CHECK(br_audio_patch_value(0, 0) == 0, "br_audio_patch_value(0, 0) == 0");
    br_mod_state[0].env = 32767u;
    HD_CHECK(br_audio_patch_value(0, 0) == 127, "br_audio_patch_value(0, 0) == 127");
    br_cached_mod[0].route[0].amount = -64;
    HD_CHECK(br_audio_patch_value(0, 0) == -64, "br_audio_patch_value(0, 0) == -64");
    br_cached_mod[0].route[0].source = BR_SOURCE_LFO1;
    br_cached_mod[0].lfo[0].wave = BR_WAVE_SQUARE;
    br_mod_state[0].phase[0] = 0u;
    HD_CHECK(br_audio_patch_value(0, 0) == -64, "br_audio_patch_value(0, 0) == -64");
    br_mod_state[0].phase[0] = 0x80000000u;
    HD_CHECK(br_audio_patch_value(0, 0) == 64, "br_audio_patch_value(0, 0) == 64");
    br_cached_mod[0].route[0].enabled = 0u;
    HD_CHECK(br_audio_patch_value(0, 0) == 0, "br_audio_patch_value(0, 0) == 0");
    br_badge_patch_value[0][0] = -127;
    uint32_t low = br_badge_pulse_key(0, BR_BADGE_PATCH1);
    br_badge_patch_value[0][0] = 127;
    uint32_t high = br_badge_pulse_key(0, BR_BADGE_PATCH1);
    HD_CHECK(high > low, "high > low");
    HD_CHECK(br_badge_pulse_key(0, BR_BADGE_SEND1) == high, "br_badge_pulse_key(0, BR_BADGE_SEND1) == high");
    for (uint32_t part = 0; part < BR_PARTS; ++part) {
        for (uint32_t a = 0; a < 4u; ++a)
            for (uint32_t b = a + 1u; b < 4u; ++b)
                HD_CHECK(br_route_badge_color(part, a) != br_route_badge_color(part, b), "br_route_badge_color(part, a) != br_route_badge_color(part, b)");
        HD_CHECK(br_route_badge_color(part, 2u) == br_route_badge_color(part, 4u), "br_route_badge_color(part, 2u) == br_route_badge_color(part, 4u)");
        HD_CHECK(br_route_badge_color(part, 3u) == br_route_badge_color(part, 5u), "br_route_badge_color(part, 3u) == br_route_badge_color(part, 5u)");
        for (uint32_t a = 0; a < 4u; ++a)
            HD_CHECK(br_route_badge_color(part, a) == br_route_badge_color(0u, a), "br_route_badge_color(part, a) == br_route_badge_color(0u, a)");
    }
    return hd_finish("HD route badges, indicators, OFF and ENABLE graphics, send well and patch values");
}
