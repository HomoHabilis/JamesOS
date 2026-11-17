/* SPDX-License-Identifier: GPL-3.0-only */
/* HD screen transitions:
 * a page drawn after the FX sends, the mixer, the SCALE page, the pattern page, the kit mixer or the
 * drum step grid must look exactly as it does when drawn onto a clean screen; the kit mixer replaces
 * the ARP message and a synth page completely; COMP KNOBS leads to SETTINGS and HEAT cleanly; and
 * every fader value redraws without leftovers, with its cap in its strip and climbing with the level. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

static uint16_t ref[240 * 240];

static uint32_t crops;
static void clean(void) { br_background_rect(0, 0, 240, 240); br_draw_layout_old = 0xFFFFFFFFu; }
static void show(uint32_t part, uint32_t page, uint32_t screen)
{
    br_ui.part = part; br_ui.page = page; br_ui.screen[page] = screen; br_ui.redraw = 1u; br_draw();
}

int main(int argc, char **argv)
{
    uint32_t checked = 0u;
    if (argc > 1) hd_crop_dir = argv[1];
    for (uint32_t part = 0u; part < 4u; ++part)
        for (uint32_t page = 0u; page < BR_PAGE_COUNT; ++page)
            for (uint32_t screen = 0u; screen < 8u; ++screen) {
                br_app_init(); host_after_intro(); br_ui.part = part; br_ui.page = page; br_ui.screen[page] = screen;
                if (screen >= br_screen_count()) continue;
                clean(); show(part, page, screen); memcpy(ref, fb, sizeof ref);
                for (uint32_t from = 0u; from < 10u; ++from) {
                    uint32_t n = 0u;
                    br_app_init(); host_after_intro(); clean();
                    if (from == 0u) show(part, BR_PAGE_FX, BR_FX_SENDS);
                    else if (from == 1u) show(part, BR_PAGE_MIX, 0u);
                    else if (from == 2u && part < BR_SYNTH_PARTS && !(page == BR_PAGE_SCALE && !screen)) show(part, BR_PAGE_SCALE, 0u);
                    else if (from == 3u) show(BR_DRUM_PART, BR_PAGE_PATTERN, 0u);
                    else if (from == 4u) show(BR_DRUM_PART, BR_PAGE_ROUTE, 3u);
                    else if (from == 9u) show(BR_DRUM_PART, BR_PAGE_SEQ, 0u);
                    else if (from >= 5u) show(part, BR_PAGE_FX, from - 4u);
                    else continue;
                    show(part, page, screen);
                    for (uint32_t i = 0u; i < 240u * 240u; ++i) n += fb[i] != ref[i];
                    if (n && !crops++) {
                        char path[512]; snprintf(path, sizeof path, "%s/leftover_p%u_g%u_s%u_from%u.ppm", hd_crop_dir, part, page, screen, from);
                        hd_write_crop(path, fb, 240, 0, 0, 0, 240, 240, 2);
                    }
                    HD_CHECK(!n, "part %u page %u screen %u from %u: %u px left behind", part, page, screen, from, n);
                    ++checked;
                }
            }
    /* The kit mixer must replace either the ARP message or a synth page completely. */
    for (uint32_t from = 0u; from < 2u; ++from) {
        br_app_init(); host_after_intro(); clean(); show(BR_DRUM_PART, BR_PAGE_ROUTE, 3u); memcpy(ref, fb, sizeof ref);
        br_app_init(); host_after_intro(); clean();
        if (from) show(0u, BR_PAGE_LFO, 0u); else show(BR_DRUM_PART, BR_PAGE_PATTERN, 0u);
        show(BR_DRUM_PART, BR_PAGE_ROUTE, 3u); HD_CHECK(!memcmp(ref, fb, sizeof ref), "kit mixer replaces %s completely", from ? "the LFO page" : "the ARP message");
    }
    /* In the Global page order, the compressor controls lead directly to Settings. */
    for (uint32_t part = 0u; part < 4u; ++part) {
        const uint32_t destinations[] = {2u, 6u}; /* settings and heat */
        for (uint32_t k = 0u; k < 2u; ++k) {
            br_app_init(); host_after_intro(); clean(); show(part, BR_PAGE_MIX, destinations[k]); memcpy(ref, fb, sizeof ref);
            br_app_init(); host_after_intro(); clean(); show(part, BR_PAGE_MIX, 5u); show(part, BR_PAGE_MIX, destinations[k]);
            HD_CHECK(!memcmp(ref, fb, sizeof ref), "part %u: COMP KNOBS -> GLO screen %u equals a clean draw", part, destinations[k]);
        }
    }
    /* Faders, HD: every value redraws without leftovers (a draw at level L after another level
     * equals a clean draw at L), and the cap (a neutral bevel) sits inside its strip and climbs
     * monotonically with the level, over most of the track. Global mixer (T1) and kit mixer (lane 1). */
    for (uint32_t kit = 0; kit < 2u; ++kit) {
        static uint16_t clean_fb[240u * 240u];
        int32_t prev = 1000, lowest = -1, highest = -1;
        for (uint32_t level = 0; level < 128u; ++level) {
            int32_t top = 240, bottom = -1;
            br_app_init(); host_after_intro(); clean();
            br_ui.part = kit ? BR_DRUM_PART : 0u;
            br_ui.page = kit ? BR_PAGE_ROUTE : BR_PAGE_MIX;
            br_ui.screen[br_ui.page] = kit ? 3u : 0u; br_ui.lane = 0u;
            if (kit) br_part[BR_DRUM_PART].lane_level[0] = level; else br_part[0].sound.level = level;
            br_ui.redraw = 1u; br_draw(); memcpy(clean_fb, fb, sizeof clean_fb);
            if (kit) br_part[BR_DRUM_PART].lane_level[0] = 127u - level; else br_part[0].sound.level = 127u - level;
            br_ui.redraw = 1u; br_draw();
            if (kit) br_part[BR_DRUM_PART].lane_level[0] = level; else br_part[0].sound.level = level;
            br_ui.redraw = 1u; br_draw();
            HD_CHECK(!memcmp(clean_fb, fb, sizeof clean_fb), "%s level %u after %u: equals a clean draw",
                     kit ? "kit lane 1" : "T1", level, 127u - level);
            /* the fader well of the first strip: below the badge and name, above the M/S keys */
            for (int32_t y = kit ? 72 : 60; y < 192; ++y) for (int32_t x = 10; x < 52; ++x) {
                uint16_t c = fb[y * 240 + x];
                if (((c >> 11) & 31u) >= 22u && ((c >> 5) & 63u) >= 46u && (c & 31u) >= 22u &&
                    abs((int32_t)((c >> 11) & 31u) * 2 - (int32_t)((c >> 5) & 63u)) <= 4) {
                    if (y < top) top = y; if (y > bottom) bottom = y;
                }
            }
            HD_CHECK(bottom >= top && bottom - top <= 8, "%s level %u: one cap in the strip (rows %d..%d)",
                     kit ? "kit lane 1" : "T1", level, top, bottom);
            if (bottom >= top) {
                int32_t centre = (top + bottom) / 2;
                HD_CHECK(centre <= prev, "%s level %u: the cap does not move down as the level rises (%d after %d)",
                         kit ? "kit lane 1" : "T1", level, centre, prev);
                prev = centre;
                if (!level) lowest = centre;
                if (level == 127u) highest = centre;
            }
        }
        HD_CHECK(lowest - highest >= 80, "%s: the cap travels %d px from level 0 to 127", kit ? "kit lane 1" : "T1", lowest - highest);
    }
    HD_CHECK(checked > 100u, "%u transitions checked", checked);
    return hd_finish("HD pages drawn after the FX sends, mixer and SEL list equal a clean draw; faders redraw cleanly");
}
