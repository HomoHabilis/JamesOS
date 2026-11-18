/* Exercise the actual intro draw and handoff using the host display buffer. */
#define DINGHY_HOST 1
#define UI_REFRESH_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdlib.h>

#if BR_PRODUCT_SMK37
/* SMK-37 Pro: the JamesOS-K text intro, no art. A 56 x 2 white rule at (92, 91), JAMESOS-K at scale
 * 2 centred on row 102, SMK-37 PRO at scale 1 centred on row 131, black everywhere else. Standard:
 * drawn straight to the panel (lcd_fill) plus one blit per text line; HD: two 40-row strips. */
static int32_t text_width(const char *t, int32_t scale)   /* (glyph width + 1) x scale, HD-SPEC rule 5 */
{
    int32_t w = 0;
    for (; *t; ++t) w += ((int32_t)james_font_width((uint8_t)*t) + 1) * scale;
    return w;
}
static void bounds(uint32_t y0, uint32_t y1, int32_t *left, int32_t *right)
{
    *left = 240; *right = -1;
    for (uint32_t y = y0; y < y1; ++y) for (int32_t x = 0; x < 240; ++x)
        if (fb[y * 240u + (uint32_t)x] != C_BLACK) { if (x < *left) *left = x; if (x > *right) *right = x; }
}
static void check_smk_intro(void)
{
    int32_t l, r;
    for (uint32_t y = 0; y < 240u; ++y) for (uint32_t x = 0; x < 240u; ++x) {
        uint16_t c = fb[y * 240u + x];
        /* HD draws the rule as a rounded rectangle (radius 1): its four corner pixels are blended. */
        uint32_t corner = (x == 92u || x == 147u);
        if (y >= 91u && y < 93u && x >= 92u && x < 148u) assert(c == C_WHITE || (corner && c != C_BLACK));
        else if (!((y >= 102u && y < 118u) || (y >= 131u && y < 139u))) assert(c == C_BLACK);
    }
    bounds(102u, 118u, &l, &r);   /* the name: text width minus the trailing gap, centred */
    /* HD type keeps the pixel font's advances (HD-SPEC rule 5); its ink may sit a pixel or two inside. */
    assert(r > l && abs(l + r - 239) <= 2 &&
           abs((r - l + 1) - (text_width("JAMESOS-K", 2) - 2)) <= 4);
    bounds(131u, 139u, &l, &r);
    assert(r > l && abs(l + r - 239) <= 2 && abs((r - l + 1) - (text_width("SMK-37 PRO", 1) - 1)) <= 4);
}
#define INTRO_BLITS 2u
#else
#define INTRO_BLITS 6u
#endif

int main(int argc, char **argv)
{
    FILE *f;
    assert(argc == 2);
    br_app_init();
    fm1_ms = 1000u;
    br_intro_until = fm1_ms + BR_INTRO_MS;
    br_ui.redraw = 1u;
    ui_test_lcd_blits = 0u;
    br_draw();
    assert(br_intro_until == 3000u && !br_ui.redraw);
    assert(ui_test_lcd_blits == INTRO_BLITS);
#if BR_PRODUCT_SMK37
    check_smk_intro();
#endif
    f = fopen(argv[1], "wb"); assert(f);
    assert(fwrite(fb, sizeof fb, 1u, f) == 1u); fclose(f);
    fm1_ms = 2999u;
    br_draw();
    assert(br_intro_until == 3000u && ui_test_lcd_blits == INTRO_BLITS);
    /* A user action can request a refresh without changing the frame or deadline. */
    br_ui.redraw = 1u;
    br_draw();
    assert(br_intro_until == 3000u && ui_test_lcd_blits == 2u * INTRO_BLITS);
#if BR_PRODUCT_SMK37
    check_smk_intro();
#endif
    fm1_ms = 3000u;
    br_draw();
    assert(br_intro_until == 0u && ui_test_lcd_blits > 2u * INTRO_BLITS);
#if BR_PRODUCT_SMK37
    puts("startup: SMK-37 text intro (rule, JAMESOS-K, SMK-37 PRO), redraw, and 2-second transition passed");
#else
    puts("startup: six bounded strips, redraw, and 2-second transition passed");
#endif
    return 0;
}
