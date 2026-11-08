/* Compare incremental track changes with a clean full redraw. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static uint16_t expected[240 * 240];

static void check(uint32_t page, uint32_t screen)
{
    br_ui.page = page;
    br_ui.screen[page] = screen;
    for (uint32_t from = 0; from < 3u; ++from) {
        for (uint32_t to = 0; to < 3u; ++to) {
            if (from == to) continue;
            br_ui.part = to; br_ui.redraw = 1; br_draw_layout_old = 0xffffffffu;
            lcd_fill(0, 0, 240, 240, BR_BACKGROUND);
            br_draw(); memcpy(expected, fb, sizeof expected);
            br_ui.part = from; br_ui.redraw = 1; br_draw_layout_old = 0xffffffffu;
            br_draw();
            br_ui.part = to; br_ui.redraw = 1; br_draw();
            if (memcmp(expected, fb, sizeof expected)) {
                fprintf(stderr, "track redraw differs: page %u screen %u %u->%u\n", page, screen, from, to);
                abort();
            }
        }
    }
}

static void transitions(void)
{
    static const struct { uint32_t page, screen, part; } views[] = {
        {BR_PAGE_HOME, 0, 0}, {BR_PAGE_ENV, 0, 0},
        {BR_PAGE_MIX, 0, 0}, {BR_PAGE_MIX, 1, 0},
        {BR_PAGE_SEQ, 0, 0}, {BR_PAGE_SEQ, 0, BR_DRUM_PART},
        {BR_PAGE_ROUTE, 0, BR_DRUM_PART}, {BR_PAGE_LFO, 0, 0}
    };
    for (uint32_t from = 0; from < sizeof views / sizeof views[0]; ++from) {
        for (uint32_t to = 0; to < sizeof views / sizeof views[0]; ++to) {
            if (from == to) continue;
            br_ui.page=views[to].page; br_ui.screen[br_ui.page]=views[to].screen;
            br_ui.part=views[to].part; br_ui.redraw=1; br_draw_layout_old=0xffffffffu;
            lcd_fill(0, 0, 240, 240, BR_BACKGROUND); br_draw();
            memcpy(expected, fb, sizeof expected);
            br_ui.page=views[from].page; br_ui.screen[br_ui.page]=views[from].screen;
            br_ui.part=views[from].part; br_ui.redraw=1; br_draw_layout_old=0xffffffffu;
            lcd_fill(0, 0, 240, 240, BR_BACKGROUND); br_draw();
            br_ui.page=views[to].page; br_ui.screen[br_ui.page]=views[to].screen;
            br_ui.part=views[to].part; br_ui.redraw=1; br_draw();
            if (memcmp(expected, fb, sizeof expected)) {
                fprintf(stderr, "layout redraw differs: %u -> %u\n", from, to);
                abort();
            }
        }
    }
}

int main(void)
{
    br_app_init();
    assert(!strcmp(br_sound_name(BR_KIT_BRAIDS), "BRAIDS KIT"));
    assert(!strcmp(br_sound_name(BR_KIT_808), "80S KIT"));
    assert(!strcmp(br_sound_name(BR_KIT_909), "90S KIT"));
    assert(!strcmp(br_sound_name(BR_KIT_PLAITS), "PLAITS KIT"));
    {
        const uint32_t env[4] = {30u, 60u, 0u, 80u};
        cv_begin(240, 40, BR_BACKGROUND); cv_oy = -24;
        bw_adsr_graph(9, 0, 222, 56, env, 4u);
        /* Baseline at y=49 occupies canvas rows 24–25 on the 2x2 grid. */
        for (uint32_t y = 26; y < 36; ++y)
            for (uint32_t x = 0; x < 240; ++x)
                assert(cv_px[y * cv_w + x] != swap16(BR_ACC));
        cv_oy = 0;
    }
    for (uint32_t p = 0; p < BR_PARTS; ++p) {
        uint16_t tint = br_track_background(p);
        for (uint32_t t = 0; t < 16; ++t) {
            assert(br_gradient_color(tint, 0, t) == tint);
            assert(br_gradient_color(tint, p == 0 ? 92u : 96u, t) == C_BLACK);
            for (uint32_t y = 1; y < 240; ++y) {
                uint16_t a = br_gradient_color(tint, y - 1, t);
                uint16_t b = br_gradient_color(tint, y, t);
                assert((a >> 11) >= (b >> 11));
                assert(((a >> 5) & 63u) >= ((b >> 5) & 63u));
                assert((a & 31u) >= (b & 31u));
            }
        }
    }
    br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    check(BR_PAGE_HOME, 0);
    check(BR_PAGE_ENV, 0);
    check(BR_PAGE_MIX, 0);
    check(BR_PAGE_MIX, 1);
    check(BR_PAGE_SEQ, 0);
    transitions();
    puts("PASS: track changes match clean redraws");
    return 0;
}
