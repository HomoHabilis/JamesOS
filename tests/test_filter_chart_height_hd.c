/* SPDX-License-Identifier: GPL-3.0-only */
/* Filter graph wells share the ENV chart's screen rows; changing pages and
 * filter controls must repaint those rows without leaving old pixels. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <stdio.h>

static uint16_t shot[240u * 240u];
static void show(uint32_t page, uint32_t screen)
{
    br_ui.page = page; br_ui.screen[page] = screen; br_ui.redraw = 1u; br_draw();
}
static void save_image(const char *name)
{
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", hd_crop_dir, name);
    hd_write_crop(path, fb, 240, 0, 0, 0, 240, 240, 2);
}
int main(int argc, char **argv)
{
    const uint32_t rows[] = {85u, 86u, 147u, 148u};
    uint16_t border[4];
    static uint16_t track_cue[240u * 24u];
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro(); br_ui.part = 0u;
    show(BR_PAGE_ENV, 0u); save_image("envelope");
    for (uint32_t k = 0; k < 4u; ++k) border[k] = fb[rows[k] * 240u + 60u];
    for (uint32_t which = 0; which < 2u; ++which) {
        uint32_t page = which ? BR_PAGE_MIX : BR_PAGE_HOME;
        show(page, 1u); save_image(which ? "global-filter" : "track-filter");
        for (uint32_t k = 0; k < 4u; ++k)
            HD_CHECK(fb[rows[k] * 240u + 60u] == border[k], "%s filter chart border row %u matches ENV", which ? "global" : "track", rows[k]);
        if (!which) memcpy(track_cue, fb + 216u * 240u, sizeof track_cue);
        else HD_CHECK(!memcmp(track_cue, fb + 216u * 240u, sizeof track_cue), "global and track filter show the same snap cue");
        memcpy(shot, fb, sizeof shot);
        show(BR_PAGE_ENV, 0u); show(page, 1u);
        HD_CHECK(!memcmp(shot, fb, sizeof shot), "%s filter redraw after ENV", which ? "global" : "track");
    }
    return hd_finish("global and track filter chart heights match ENV");
}
