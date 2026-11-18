/* PATCH pages use the selected track's header and graphics. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

int main(int argc, char **argv)
{
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro();
    for (uint32_t part = 0; part < BR_PARTS; ++part) {
        for (uint32_t slot = 0; slot < 2u; ++slot) {
            uint16_t header[20u * 30u], badge[36u * 240u];
            br_ui.part = part; br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = slot;
            br_ui.redraw = 1u; br_draw_layout_old = 0xffffffffu; br_draw();
            HD_CHECK(br_ui_accent == br_track_color(part), "part %u patch %u: track accent", part, slot);
            for (uint32_t y = 0; y < 20u; ++y)
                memcpy(header + y * 30u, fb + (y + 2u) * 240u + 6u, 30u * sizeof(uint16_t));
            memcpy(badge, fb + 38u * 240u, sizeof badge);   /* the badge region: rows 38..73 */
            cv_begin(240u, 24u, br_track_background(part));
            br_track_badge(6, 2, part, 0u); cv_blit(0u, 0u);
            for (uint32_t y = 0; y < 20u; ++y)
                HD_CHECK(!memcmp(header + y * 30u, fb + (y + 2u) * 240u + 6u, 30u * sizeof(uint16_t)),
                         "part %u patch %u: track header", part, slot);
            cv_begin(240u, 36u, BR_BACKGROUND); cv_oy = 8;   /* as br_draw_page_route_badge: the badge 8 px down */
            uint32_t bit = 2u + slot;
            uint32_t pulse = (br_badge_pulse_key(part, 1u << bit) >> ((slot + 2u) * 7u)) & 127u;
            uint16_t color = br_badge_blend(BR_BACKGROUND, br_track_color(part), pulse);
            br_draw_named_badge_scaled(120, 1, br_route_badge_name[bit], color, 2); cv_blit(0u, 38u); cv_oy = 0;
            for (uint32_t y = 0; y < 36u; ++y)
                HD_CHECK(!memcmp(badge + y * 240u, fb + (y + 38u) * 240u, 240u * sizeof(uint16_t)),
                         "part %u patch %u: track-colored page badge", part, slot);
        }
    }
    return hd_finish("PATCH track icons and colors");
}
