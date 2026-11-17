/* SPDX-License-Identifier: GPL-3.0-only */
/* Standard cards and custom ENV/filter/COMP TIME columns share the same
 * lower row; the four-row gap above their knob chips is clear. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

int main(void)
{
    static const struct { uint8_t page, screen; } scenes[] = {
        {BR_PAGE_HOME, 0}, {BR_PAGE_HOME, 1}, {BR_PAGE_HOME, 2}, {BR_PAGE_HOME, 3},
        {BR_PAGE_ENV, 0}, {BR_PAGE_ENV, 1}, {BR_PAGE_ENV, 2},
        {BR_PAGE_FX, BR_FX_DRIVE}, {BR_PAGE_LFO, 0}, {BR_PAGE_LFO, BR_LFOS},
        {BR_PAGE_MIX, 1}, {BR_PAGE_MIX, 4}, {BR_PAGE_MIX, 5}, {BR_PAGE_MIX, 6}
    };
    br_app_init(); host_after_intro(); br_ui.part = 0u;
    for (uint32_t i = 0; i < sizeof scenes / sizeof scenes[0]; ++i) {
        uint32_t gap_ink = 0, knob_ink = 0;
        br_ui.page = scenes[i].page; br_ui.screen[br_ui.page] = scenes[i].screen;
        br_background_rect(0,0,240,240); br_draw_layout_old = 0xffffffffu;
        br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 70u; y < 74u; ++y) for (uint32_t x = 0; x < 240u; ++x)
            gap_ink += fb[y * 240u + x] != BR_BACKGROUND;
        for (uint32_t y = 74u; y < 84u; ++y) for (uint32_t x = 0; x < 60u; ++x)
            knob_ink += fb[y * 240u + x] != BR_BACKGROUND;
        HD_CHECK(!gap_ink && knob_ink, "page %u screen %u: %u pixels in gap, %u in knob row",
                 scenes[i].page, scenes[i].screen, gap_ink, knob_ink);
    }
    /* The page titles above the columns move with the controls. */
    {
        static const struct { uint8_t page, screen; const char *text; } titles[] = {
            {BR_PAGE_HOME, 0u, "MONO"}, {BR_PAGE_FX, BR_FX_DRIVE, "TRACK"},
            {BR_PAGE_LFO, BR_LFOS, "GLOBAL LFO"}
        };
        static uint16_t shown[240u * 20u];
        for (uint32_t i = 0; i < sizeof titles / sizeof titles[0]; ++i) {
            br_ui.page=titles[i].page; br_ui.screen[br_ui.page]=titles[i].screen;
            br_ui.redraw=1u; br_draw(); memcpy(shown,fb+46u*240u,sizeof shown);
            cv_begin(240u,20u,BR_BACKGROUND);
            br_draw_page_badge(titles[i].text, i == 2u ? BR_GLOBAL_ACC : br_track_color(0u));
            for (uint32_t y=0; y<20u; ++y) for (uint32_t x=0; x<240u; ++x)
                if (cv_px[y*240u+x] != swap16(BR_BACKGROUND))
                    HD_CHECK(shown[y*240u+x] == swap16(cv_px[y*240u+x]), "%s title is eight rows lower", titles[i].text);
        }
    }
    return hd_finish("standard control columns start eight rows lower");
}
