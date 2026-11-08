/* Test the actual one-shot renderer used by rescue, update and crash screens. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define DINGHY_HOST 1
static uint16_t captured[240u * 40u];
static uint32_t captured_x, captured_y, captured_w, captured_h, blits, syncs;
static void lcd_sync(void) { ++syncs; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *pixels)
{
    uint32_t i;
    assert(w * h <= sizeof captured / sizeof captured[0]);
    captured_x=x; captured_y=y; captured_w=w; captured_h=h; ++blits;
    for (i=0; i<w*h; ++i) captured[i]=pixels[i];
}
#include "../firmware/src/gfx.c"

static int width(const char *s, uint32_t scale)
{
    int n = 0;
    for (; *s; ++s) n += (int)((james_font_width((uint8_t)*s)+1u)*scale);
    return n;
}
static void check(const char *s, uint32_t scale,
                  uint32_t box, int align)
{
    uint16_t expected[240u * 40u] = {0};
    int x = align == 1 ? ((int)box-width(s,scale))/2 :
            align == 2 ? (int)box-width(s,scale) : 0;
    const char *p;
    uint32_t row, col, dx, dy, i;
    assert(width(s,scale) <= (int)box);
    for (p=s; *p; ++p) {
        uint32_t w=james_font_width((uint8_t)*p);
        for (row=0; row<8u; ++row) for (col=0; col<w; ++col)
            if (james_font_row((uint8_t)*p,row) & (1u << (w-1u-col)))
                for (dy=0; dy<scale; ++dy) for (dx=0; dx<scale; ++dx)
                    expected[(row*scale+dy)*box+(uint32_t)x+col*scale+dx]=swap16(C_WHITE);
        x+=(int)((w+1u)*scale);
    }
    cv_oy=-13;
    draw_text_box(0, 20, box, scale, s, C_WHITE, align);
    assert(cv_oy == -13 && captured_x == 0u && captured_y == 20u);
    assert(captured_w == box && captured_h == 8u*scale);
    for (i=0; i<box*captured_h; ++i) assert(captured[i] == expected[i]);
}
int main(void)
{
    const char *strings[] = {"JAMESOS USB RESCUE", "CONNECT USB", "OPEN THE INSTALLER",
        "UNKNOWN FLASH", "AUDIO OFF", "UPDATE", "PACKAGE", "CHECK HEAD", "LOADER",
        "CONFIRM", "RESTART", "DRY RUN OK", "FAILED", "UBOOT (USB)",
        "JAMESOS CRASH", "FFFFFFFF", "01234567"};
    uint32_t i;
    for (i=0; i<sizeof strings/sizeof strings[0]; ++i)
        check(strings[i], 2u, i >= 15u ? 220u : 240u, i >= 15u ? 0 : 1);
    check("JAMESOS USB RESCUE", 2u, 240u, 1);
    check("JAMESOS CRASH", 2u, 240u, 1);
    check("Qg", 4u, 240u, 2);
    assert(blits == sizeof strings/sizeof strings[0]+3u && syncs == 2u*blits);
    puts("PASS: update, rescue and crash text fits and matches the shared font");
    return 0;
}
