/* SPDX-License-Identifier: GPL-3.0-only */
/* Tonal and drum grids keep the same clear gap under the step bar. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <stdio.h>

static uint16_t expected[240u * 240u];
static void draw_page(uint32_t part, uint32_t page, uint32_t lane)
{
    br_ui.part = part; br_ui.page = page; br_ui.screen[page] = 0u; br_ui.lane = lane;
    br_ui.redraw = 1u; br_draw();
}
static void save_image(const char *name)
{
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", hd_crop_dir, name);
    hd_write_crop(path, fb, 240, 0, 0, 0, 240, 240, 2);
}
int main(int argc, char **argv)
{
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro();
    for (uint32_t part = 0; part <= BR_DRUM_PART; part += BR_DRUM_PART) {
        uint32_t lanes = part == BR_DRUM_PART ? BR_DRUM_LANES : 1u;
        for (uint32_t lane = 0; lane < lanes; ++lane) {
            br_background_rect(0, 0, 240, 240); br_draw_layout_old = 0xffffffffu;
            draw_page(part, BR_PAGE_SEQ, lane); memcpy(expected, fb, sizeof expected);
            if (!lane) save_image(part ? "drums" : "track");
            for (uint32_t y = 32u; y < 36u; ++y) for (uint32_t x = 0; x < 240u; ++x)
                HD_CHECK(fb[y * 240u + x] == BR_BACKGROUND, "part %u lane %u top gap at %u,%u",part,lane,x,y);
            draw_page(part ? 0u : BR_DRUM_PART, BR_PAGE_SEQ, 0u);
            draw_page(part, BR_PAGE_SEQ, lane);
            HD_CHECK(!memcmp(expected + 32u * 240u, fb + 32u * 240u, 184u * 240u * sizeof(uint16_t)),
                     "part %u lane %u redraw after other sequencer",part,lane);
        }
    }
    return hd_finish("drum and track sequencers share the top gap");
}
