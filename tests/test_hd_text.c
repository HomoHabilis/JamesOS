/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for the HD text renderer (firmware/src/gfx_hd_text.h).
 *
 * Built and run by tests/run_hd_text.py. The pixel renderer it compares with
 * (br_widget_text_width / br_widget_text_scaled) is the removed standard
 * interface's, kept below; the font data is the firmware's. Uses the real strip
 * canvas from gfx.c.
 *
 *   (a) metrics: for every character and scale, the HD advance is at most
 *       the pixel advance (so no string gets wider), digits share one
 *       advance, the HD glyph stays in its 8*scale rows and at most 1 px
 *       outside its pixel cell horizontally, and it has ink exactly when the
 *       pixel glyph has; hd_text_width_scaled agrees with what hd_text draws
 *       (the ink of a run ends on its measured width) and is never more than
 *       the pixel width; the next character lands where hd_text_advance says;
 *   (b) a specimen sheet (pixel line, HD line below) rendered strip by strip
 *       (240 x 40, as on the device) and checked pixel-identical to the same
 *       scene drawn on one tall canvas, so strip seams cannot show.
 *
 * Usage: test_hd_text SPECIMEN.ppm */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DINGHY_HOST 1
#define CV_MAX (240u * 1600u)
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    (void)x; (void)y; (void)w; (void)h; (void)px;
}
#include "../firmware/src/gfx.c"
#include "../firmware/src/gfx_hd_text.h"
/* br_widget_text_width, br_widget_text_scaled: the pixel font's renderer, the reference the
 * HD metrics are held to. This is the standard-interface code of instrument_widgets.c as it was
 * before that interface was removed (the firmware now draws only HD text), kept here verbatim. */
static int32_t br_widget_text_width(const char *text)
{
    int32_t n = 0;
    if (!text) return 0;
    while (*text) n += (int32_t)james_font_width((uint8_t)*text++) + 1;
    return n ? n - 1 : 0;
}
static void br_widget_text_scaled(int32_t x, int32_t y, const char *text, uint16_t color, int32_t scale)
{
    if (!text) return;
    while (*text) {
        uint8_t ch = (uint8_t)*text++;
        uint32_t width = james_font_width(ch);
        for (uint32_t row = 0; row < 8u; ++row) {
            uint8_t bits = james_font_row(ch, row);
            for (uint32_t col = 0; col < width; ++col)
                if (bits & (1u << (width - 1u - col)))
                    cv_rect(x + (int32_t)col * scale, y + (int32_t)row * scale, scale, scale, color);
        }
        x += ((int32_t)width + 1) * scale;
    }
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

typedef struct { int x0, y0, x1, y1, n; } ink_t;

static ink_t ink_box(void)
{
    ink_t b = {1 << 20, 1 << 20, -1, -1, 0};
    for (uint32_t y = 0; y < cv_h; ++y)
        for (uint32_t x = 0; x < cv_w; ++x)
            if (cv_px[y * cv_w + x] != swap16(cv_background)) {
                if ((int)x < b.x0) b.x0 = (int)x;
                if ((int)x > b.x1) b.x1 = (int)x;
                if ((int)y < b.y0) b.y0 = (int)y;
                if ((int)y > b.y1) b.y1 = (int)y;
                b.n++;
            }
    return b;
}

static void test_metrics(void)
{
    static const char *const runs[] = {"ANALOG VCF", "KNOB1", "KICK1", "AUDITION", "QUICKSAVE", "60%", "743MS", "1/16",
                                       "1/64", "OCT-/+", "-30 dB", "SAVE?", "LTAVY", "T1 LFO1", "I", "", 0};
    int hd_glyphs[4] = {0, 0, 0, 0};
    CHECK(hd_text_width(0) == 0 && hd_text_width("") == 0, "width of NULL / empty");
    for (int32_t s = 1; s <= 3; ++s) {
        int32_t digit = -1;
        /* runs: never wider than the pixel font; the ink ends on the measured width */
        for (unsigned i = 0; runs[i]; ++i) {
            const int32_t w = hd_text_width_scaled(runs[i], s), X = 8;
            ink_t ink;
            CHECK(w <= br_widget_text_width(runs[i]) * s, "\"%s\" scale %d: HD width %d > pixel %d",
                  runs[i], (int)s, (int)w, (int)(br_widget_text_width(runs[i]) * s));
            if (s == 1) CHECK(hd_text_width(runs[i]) == w, "hd_text_width(\"%s\")", runs[i]);
            if (!*runs[i]) continue;
            cv_begin(240, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
            hd_text(X, 4, runs[i], C_WHITE, s);
            ink = ink_box();
            CHECK(ink.x0 >= X - 1 && ink.x0 <= X + 1 && ink.x1 - X + 1 <= w + 1 && ink.x1 - X + 1 >= w - 1,
                  "\"%s\" scale %d: ink %d..%d, measured width %d", runs[i], (int)s, ink.x0 - X, ink.x1 - X, (int)w);
        }
        for (int ch = 32; ch <= 126; ++ch) {
            const char one[2] = {(char)ch, 0};
            const int32_t pixel_adv = (int32_t)(james_font_width((uint8_t)ch) + 1u) * s;
            const int32_t adv = hd_text_advance((uint8_t)ch, 0, s);
            const int32_t X = 16, Y = 4;
            const uint8_t *bits;
            ink_t px, hd;
            if (hd_text_glyph((uint8_t)ch, s, &bits)) hd_glyphs[s]++;
            CHECK(adv >= s && adv <= pixel_adv, "advance '%c' scale %d: %d (pixel %d)", ch, (int)s, (int)adv, (int)pixel_adv);
            CHECK(hd_text_width_scaled(one, s) == adv - s, "width '%c' scale %d", ch, (int)s);
            if (ch >= '0' && ch <= '9') {
                if (digit < 0) digit = adv;
                CHECK(adv == digit, "digit '%c' scale %d: advance %d, '0' has %d (digits are tabular)", ch, (int)s, (int)adv, (int)digit);
            }
            /* pixel glyph alone */
            cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
            br_widget_text_scaled(X, Y, one, C_WHITE, s);
            px = ink_box();
            /* HD glyph alone */
            cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
            hd_text(X, Y, one, C_WHITE, s);
            hd = ink_box();
            CHECK((px.n > 0) == (hd.n > 0), "ink presence '%c' scale %d (pixel %d, hd %d)", ch, (int)s, px.n, hd.n);
            if (hd.n) {
                CHECK(hd.x0 >= X - 1 && hd.x1 <= X + pixel_adv, "'%c' scale %d: columns %d..%d outside cell %d..%d (+-1)",
                      ch, (int)s, hd.x0 - X, hd.x1 - X, 0, (int)pixel_adv - 1);
                CHECK(hd.y0 >= Y && hd.y1 <= Y + 8 * s - 1, "'%c' scale %d: rows %d..%d outside 0..%d",
                      ch, (int)s, hd.y0 - Y, hd.y1 - Y, (int)(8 * s - 1));
            }
            /* advance: whatever follows starts exactly where hd_text_advance puts it */
            {
                const char two[3] = {(char)ch, 'H', 0};
                const int32_t step = hd_text_advance((uint8_t)ch, 'H', s);
                static uint16_t a[80 * 40];
                cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
                hd_text(X, Y, two, C_WHITE, s);
                memcpy(a, cv_px, cv_w * cv_h * 2u);
                cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
                hd_text(X + step, Y, "H", C_WHITE, s);
                for (uint32_t y = 0; y < cv_h; ++y)
                    for (uint32_t x = (uint32_t)(X + pixel_adv + 1); x < cv_w; ++x)
                        if (a[y * cv_w + x] != cv_px[y * cv_w + x]) {
                            CHECK(0, "advance after '%c' scale %d", ch, (int)s);
                            y = cv_h; break;
                        }
            }
        }
        printf("scale %d: %d characters have HD glyphs, the rest use the pixel form; digit advance %d\n",
               (int)s, hd_glyphs[s], (int)digit);
    }
    /* kerning: only letter and punctuation pairs, and a kerned pair's inks
     * never overlap or touch (a background column always separates them) */
    for (uint32_t k = 0; k < JAMES_FONT_HD_KERN; ++k) {
        const uint8_t l = james_hd_kern[k][0], r = james_hd_kern[k][1];
        CHECK(!(l >= '0' && l <= '9') && !(r >= '0' && r <= '9'), "kerned pair %c%c has a digit", l, r);
        for (int32_t s = 1; s <= 2; ++s) {
            const char pair[3] = {(char)l, (char)r, 0};
            const int32_t al = hd_text_advance(l, r, s);
            int touching = 0;
            cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
            hd_text(16, 4, pair, C_WHITE, s);
            {   /* draw each alone and look for an overlap or contact */
                static uint16_t a[80 * 40];
                const char lone[2] = {(char)l, 0}, rone[2] = {(char)r, 0};
                cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
                hd_text(16, 4, lone, C_WHITE, s);
                memcpy(a, cv_px, cv_w * cv_h * 2u);
                cv_begin(80, 8u * (uint32_t)s + 8u, C_BLACK); cv_oy = 0;
                hd_text(16 + al, 4, rone, C_WHITE, s);
                for (uint32_t y = 0; y < cv_h; ++y)
                    for (uint32_t x = 0; x + 1 < cv_w; ++x) {
                        const int lx = a[y * cv_w + x] != swap16(cv_background);
                        const int rx = cv_px[y * cv_w + x] != swap16(cv_background);
                        const int rn = cv_px[y * cv_w + x + 1] != swap16(cv_background);
                        if (lx && (rx || rn)) touching = 1;
                    }
            }
            CHECK(!touching, "kerned pair %c%c touches at scale %d", l, r, (int)s);
        }
    }
    printf("kerning: %u pairs\n", (unsigned)JAMES_FONT_HD_KERN);
}

/* ---- specimen ---- */

#define SPEC_W 240
static const char *const spec_lines[] = {
    "ANALOG VCF", "KNOB1 TIMBRE", "HARMONICS 60%", "743MS 1/16", "SEQUENCER", "01 REESE",
    "OCT-/+ STEP BANK", "LOAD PRESET", "0123456789", "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ",
    "-30 dB 24 dB", "SAVE? (USB) !:.",
};
static const char *const spec_lines3[] = {"60%", "743MS", "01234", "56789", "1/16"};
#define NLINES ((int)(sizeof spec_lines / sizeof spec_lines[0]))
#define NLINES3 ((int)(sizeof spec_lines3 / sizeof spec_lines3[0]))
static const uint16_t hues[] = {RGB(90, 220, 110), RGB(176, 120, 255), RGB(90, 170, 255), RGB(255, 90, 80), RGB(255, 196, 64)};

/* Everything in graph coordinates; called once per strip. */
static int32_t spec_scene(void)
{
    int32_t y = 4;
    for (int32_t s = 1; s <= 3; ++s) {
        const int n = s == 1 ? NLINES : s == 2 ? NLINES : NLINES3;
        const char *const *lines = s == 3 ? spec_lines3 : spec_lines;
        for (int i = 0; i < n; ++i) {
            const char *t = lines[i];
            uint16_t c = C_WHITE;
            if (s == 2 && (i & 1)) continue;                  /* half the lines at 2x keeps the sheet short */
            if (i % 4 == 3) c = hues[(i / 4) % 5];
            br_widget_text_scaled(6, y, t, c, s);
            y += 8 * s + 2;
            hd_text(6, y, t, c, s);
            y += 8 * s + 4 + (s == 3 ? 2 : 0);
        }
        /* coloured surfaces: black text on a hue (badges) and hue on dark grey */
        for (int k = 0; k < 2; ++k) {
            const char *t = s == 3 ? "60%" : "T1 LFO1";
            const int32_t w = br_widget_text_width(t) * s + 8 * s;
            const uint16_t bg = k ? RGB(28, 28, 32) : hues[s - 1];
            const uint16_t fg = k ? hues[s] : C_BLACK;
            cv_rect(4, y, w, 9 * s + 2, bg);
            br_widget_text_scaled(4 + 4 * s, y + 1 + s / 2, t, fg, s);   /* left pixel, right HD */
            cv_rect(4 + w + 6, y, w, 9 * s + 2, bg);
            hd_text(4 + w + 6 + 4 * s, y + 1 + s / 2, t, fg, s);
            y += 9 * s + 5;
        }
        y += 6;
    }
    return y;
}

static int spec_height(void)
{
    int32_t h;
    cv_begin(SPEC_W, 1u, C_BLACK);
    cv_oy = -100000;                       /* measure only: nothing is visible */
    h = spec_scene();
    return (int)h;
}

static void write_ppm(const char *path, const uint16_t *img, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        uint16_t c = img[i];
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                                (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void test_specimen(const char *path)
{
    const int h = spec_height();
    uint16_t *strips = calloc((size_t)SPEC_W * (size_t)h, 2);
    assert(strips && (uint32_t)(SPEC_W * h) <= CV_MAX);
    /* strip by strip, 240 x 40 */
    for (int oy = 0; oy < h; oy += 40) {
        const int rows = h - oy < 40 ? h - oy : 40;
        cv_begin(SPEC_W, (uint32_t)rows, C_BLACK);
        cv_oy = -oy;
        spec_scene();
        for (int i = 0; i < SPEC_W * rows; ++i) strips[oy * SPEC_W + i] = swap16(cv_px[i]);
    }
    /* one tall canvas */
    cv_begin(SPEC_W, (uint32_t)h, C_BLACK);
    cv_oy = 0;
    spec_scene();
    {
        int diff = 0;
        for (int i = 0; i < SPEC_W * h; ++i) diff += strips[i] != swap16(cv_px[i]);
        CHECK(diff == 0, "strip rendering differs from single-canvas rendering in %d pixels", diff);
    }
    /* and with an odd strip height, so seams fall mid-glyph everywhere */
    {
        int diff = 0;
        for (int oy = 0; oy < h; oy += 13) {
            const int rows = h - oy < 13 ? h - oy : 13;
            cv_begin(SPEC_W, (uint32_t)rows, C_BLACK);
            cv_oy = -oy;
            spec_scene();
            for (int i = 0; i < SPEC_W * rows; ++i) diff += strips[oy * SPEC_W + i] != swap16(cv_px[i]);
        }
        CHECK(diff == 0, "13-row strips differ from 40-row strips in %d pixels", diff);
    }
    write_ppm(path, strips, SPEC_W, h);
    free(strips);
}

int main(int argc, char **argv)
{
    test_metrics();
    test_specimen(argc > 1 ? argv[1] : "hd_text_specimen.ppm");
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
