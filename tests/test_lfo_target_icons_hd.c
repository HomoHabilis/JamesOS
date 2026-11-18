/* SPDX-License-Identifier: GPL-3.0-only */
/* Every global LFO destination has its own graphic; MODEL has a graphic too.
 * This runs the real page renderer without touching a device. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <stdio.h>

static void set_target_value(uint32_t target, uint32_t high)
{
    uint32_t v = high ? 127u : 0u;
    switch (target) {
    case BR_GLOBAL_TARGET_FILTER: br_master.fx.filter = v; break;
    case BR_GLOBAL_TARGET_RESONANCE: br_master.fx.resonance = v; break;
    case BR_GLOBAL_TARGET_HEAT_AMOUNT: br_master.fx.heat_amount = v; break;
    case BR_GLOBAL_TARGET_HEAT_TYPE: br_master.fx.heat_type = high ? BR_HEAT_COUNT - 1u : 0u; break;
    case BR_GLOBAL_TARGET_COMP_MIX: br_master.comp.mix = v; break;
    case BR_GLOBAL_TARGET_COMP_THRESHOLD: br_master.comp.threshold = high ? 48u : 0u; break;
    case BR_GLOBAL_TARGET_COMP_MAKEUP: br_master.comp.makeup = high ? 24u : 0u; break;
    case BR_GLOBAL_TARGET_COMP_ATTACK: br_master.comp.attack = v; break;
    case BR_GLOBAL_TARGET_COMP_RELEASE: br_master.comp.release = v; break;
    case BR_GLOBAL_TARGET_COMP_HOLD: br_master.comp.hold = v; break;
    case BR_GLOBAL_TARGET_REVERB_SIZE: br_master.reverb_size = v; break;
    case BR_GLOBAL_TARGET_REVERB_DECAY: br_master.reverb_decay = v; break;
    }
}

int main(int argc, char **argv)
{
    static uint16_t icon[BR_GLOBAL_TARGET_UI_COUNT][60u * 60u];
    char path[512];
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro();
    br_ui.part = 0u; br_part[0].sound.preset = BR_FIRST_PLAITS + 8u;
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = BR_LFOS;
    br_master.global_lfo.depth = 0u;
    for (uint32_t t = 0u; t < BR_GLOBAL_TARGET_UI_COUNT; ++t) {
        br_master.global_lfo.target = br_global_target_id[t];
        set_target_value(br_global_target_id[t], 0u);
        br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 0u; y < 60u; ++y)
            memcpy(icon[t] + y * 60u, fb + (BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u, 60u * sizeof(uint16_t));
        for (uint32_t before = 0u; before < t; ++before)
            HD_CHECK(memcmp(icon[t], icon[before], sizeof icon[t]) != 0,
                     "global target %u differs from %u", t, before);
        snprintf(path, sizeof path, "%s/global-lfo-%02u.ppm", hd_crop_dir, t);
        hd_write_crop(path, fb, 240, 0, 0, 0, 240, 240, 2);
        if (t) {
            uint16_t changed[60u * 60u];
            set_target_value(br_global_target_id[t], 1u); br_ui.redraw = 1u; br_draw();
            for (uint32_t y = 0u; y < 60u; ++y)
                memcpy(changed + y * 60u, fb + (BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u, 60u * sizeof(uint16_t));
            HD_CHECK(memcmp(icon[t], changed, sizeof changed) != 0, "global target %u reflects parameter changes", t);
        }
    }
    /* Companion controls also affect the reused artwork even if its own
     * selected value is unchanged. */
    {
        uint16_t before[60u * 60u];
        br_master.global_lfo.target = BR_GLOBAL_TARGET_COMP_ATTACK;
        br_master.comp.attack = 50u; br_master.comp.hold = 0u; br_master.comp.release = 50u;
        br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 0; y < 60u; ++y) memcpy(before + y * 60u, fb + (BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u, 60u * sizeof(uint16_t));
        br_master.comp.hold = 127u; br_ui.redraw = 1u; br_draw();
        uint32_t different = 0u;
        for (uint32_t y = 0; y < 60u; ++y) for (uint32_t x = 0; x < 60u; ++x)
            different |= before[y * 60u + x] != fb[(BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u + x];
        HD_CHECK(different, "compressor LFO artwork follows companion time controls");
    }
    /* Audio LFO modulation changes the depicted effective value as its phase moves. */
    br_master.global_lfo.target = BR_GLOBAL_TARGET_FILTER; br_master.fx.filter = 64u;
    br_master.global_lfo.depth = 100u; br_master.global_lfo.wave = BR_WAVE_SINE;
    br_global_lfo_state.phase[0] = 0u; br_ui.redraw = 1u; br_draw();
    uint32_t before = br_badge_global_lfo_value;
    uint16_t initial[60u * 60u];
    for (uint32_t y = 0; y < 60u; ++y) memcpy(initial + y * 60u, fb + (BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u, 60u * sizeof(uint16_t));
    br_global_lfo_state.phase[0] = 1u << 30; br_ui.redraw = 1u; br_draw();
    HD_CHECK(br_badge_global_lfo_value != before, "global graphic samples changing LFO modulation");
    uint32_t moved = 0u;
    for (uint32_t y = 0; y < 60u; ++y) for (uint32_t x = 0; x < 60u; ++x)
        moved |= initial[y * 60u + x] != fb[(BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u + x];
    HD_CHECK(moved, "global target illustration visibly moves with the LFO");
    {
        static uint16_t full[240u * 240u];
        memcpy(full, fb, sizeof full);
        br_background_rect(0, 0, 240, 240); br_draw_layout_old = 0xffffffffu;
        br_draw_more = 0u; br_ui.redraw = 1u;
        uint32_t n = 0u;
        while ((br_ui.redraw || br_draw_more) && n++ < 400u)
            br_draw_more = (uint8_t)br_draw_pass_with(br_draw);
        HD_CHECK(n < 400u && !memcmp(full, fb, sizeof full), "moving target sliced redraw matches full frame");
    }
#if BR_MODEL_MOD_EXPERIMENT
    br_ui.screen[BR_PAGE_LFO] = 0u; br_part[0].mod.lfo[0].target = BR_TARGET_MODEL;
    br_ui.redraw = 1u; br_draw();
    HD_CHECK(br_target_control_id(2u) == BR_TARGET_MODEL && br_target_ui_available(BR_TARGET_MODEL),
             "MODEL target available for Plaits");
    uint16_t model[60u * 60u];
    for (uint32_t y = 0; y < 60u; ++y) memcpy(model + y * 60u, fb + (BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u, 60u * sizeof(uint16_t));
    br_part[0].sound.preset = BR_FIRST_PLAITS + 17u; br_ui.redraw = 1u; br_draw();
    uint32_t different = 0u;
    for (uint32_t y = 0; y < 60u; ++y) for (uint32_t x = 0; x < 60u; ++x)
        different |= model[y * 60u + x] != fb[(BR_PARAM_STANDARD_BODY_Y + BR_PARAM_ICON_Y + y) * 240u + 120u + x];
    HD_CHECK(different, "MODEL graphic follows selected model");
    snprintf(path, sizeof path, "%s/lfo-model.ppm", hd_crop_dir);
    hd_write_crop(path, fb, 240, 0, 0, 0, 240, 240, 2);
    br_part[0].mod.lfo[0].depth = 100u;
    br_effective_sound[0].preset = BR_FIRST_PLAITS + 3u;
    br_ui.redraw = 1u; br_draw();
    HD_CHECK(br_badge_model_target == 3u, "MODEL graphic follows effective modulated model");
#endif
    return hd_finish("LFO destinations each have distinct graphics");
}
