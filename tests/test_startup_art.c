/* Exercise the actual intro draw and handoff using the host display buffer. */
#define DINGHY_HOST 1
#define UI_REFRESH_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

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
    assert(ui_test_lcd_blits == 6u);
    f = fopen(argv[1], "wb"); assert(f);
    assert(fwrite(fb, sizeof fb, 1u, f) == 1u); fclose(f);
    fm1_ms = 2999u;
    br_draw();
    assert(br_intro_until == 3000u && ui_test_lcd_blits == 6u);
    /* A user action can request a refresh without changing the frame or deadline. */
    br_ui.redraw = 1u;
    br_draw();
    assert(br_intro_until == 3000u && ui_test_lcd_blits == 12u);
    fm1_ms = 3000u;
    br_draw();
    assert(br_intro_until == 0u && ui_test_lcd_blits > 12u);
    puts("startup: six bounded strips, redraw, and 2-second transition passed");
    return 0;
}
