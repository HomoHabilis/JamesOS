/* Screen transitions leave nothing behind: a page drawn after the FX sends, the mixer or the SEL list
 * must look exactly as it does when drawn onto a clean screen. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static uint16_t ref[240 * 240];

static void clean(void) { br_background_rect(0, 0, 240, 240); br_draw_layout_old = 0xFFFFFFFFu; }
static void show(uint32_t part, uint32_t page, uint32_t screen)
{
    br_ui.part = part; br_ui.page = page; br_ui.screen[page] = screen; br_ui.redraw = 1u; br_draw();
}

int main(void)
{
    uint32_t checked = 0u;
    for (uint32_t part = 0u; part < 4u; ++part)
        for (uint32_t page = 0u; page < BR_PAGE_COUNT; ++page)
            for (uint32_t screen = 0u; screen < 8u; ++screen) {
                br_app_init(); br_ui.part = part; br_ui.page = page; br_ui.screen[page] = screen;
                if (screen >= br_screen_count()) continue;
                clean(); show(part, page, screen); memcpy(ref, fb, sizeof ref);
                for (uint32_t from = 0u; from < 10u; ++from) {
                    uint32_t n = 0u;
                    br_app_init(); clean();
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
                    if (n) printf("leftover: part %u page %u screen %u from %u: %u px\n", part, page, screen, from, n);
                    if (n) { for (uint32_t y=0;y<240u;++y) { uint32_t row=0; for(uint32_t x=0;x<240u;++x) row += fb[y*240u+x]!=ref[y*240u+x]; if(row)printf("row %u: %u px\n",y,row); } }
                    assert(!n);
                    ++checked;
                }
            }
    /* The kit mixer must replace either the ARP message or a synth page completely. */
    for (uint32_t from = 0u; from < 2u; ++from) {
        br_app_init(); clean(); show(BR_DRUM_PART, BR_PAGE_ROUTE, 3u); memcpy(ref, fb, sizeof ref);
        br_app_init(); clean();
        if (from) show(0u, BR_PAGE_LFO, 0u); else show(BR_DRUM_PART, BR_PAGE_PATTERN, 0u);
        show(BR_DRUM_PART, BR_PAGE_ROUTE, 3u); assert(!memcmp(ref, fb, sizeof ref));
    }
    /* In the Global page order, the compressor controls lead directly to Settings. */
    for (uint32_t part = 0u; part < 4u; ++part) {
        const uint32_t destinations[] = {2u, 6u}; /* settings and heat */
        for (uint32_t k = 0u; k < 2u; ++k) {
            br_app_init(); clean(); show(part, BR_PAGE_MIX, destinations[k]); memcpy(ref, fb, sizeof ref);
            br_app_init(); clean(); show(part, BR_PAGE_MIX, 5u); show(part, BR_PAGE_MIX, destinations[k]);
            assert(!memcmp(ref, fb, sizeof ref));
        }
    }
    /* Every fader value must draw the meter overlay flush with its cap. */
    for (uint32_t kit = 0; kit < 2u; ++kit) {
        br_app_init(); clean();
        br_ui.part = kit ? BR_DRUM_PART : 0u;
        br_ui.page = kit ? BR_PAGE_ROUTE : BR_PAGE_MIX;
        br_ui.screen[br_ui.page] = kit ? 3u : 0u; br_ui.lane = 0u;
        for (uint32_t level = 0; level < 128u; ++level) {
            int32_t cap;
            if (kit) {
                br_part[BR_DRUM_PART].lane_level[0] = level;
                cap = br_clip((103 - (int32_t)(level * 104u / 127u)) & ~1, 0, 100);
                cap += 80;
            } else {
                br_part[0].sound.level = level;
                cap = br_clip((111 - (int32_t)(level * 112u / 127u)) & ~1, 0, 108);
                cap += 70;
            }
            br_ui.redraw = 1u; br_draw();
            for (int32_t y = cap; y < cap + 6; ++y)
                assert(fb[y * 240 + 24] == fb[y * 240 + 30]);
        }
    }
    assert(checked > 100u);
    printf("screen transitions: %u pages drawn after the FX sends, mixer and SEL list match a clean draw\n", checked);
    return 0;
}
