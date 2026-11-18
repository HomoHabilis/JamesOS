/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for the anti-aliased HD shapes in firmware/src/gfx_hd.h.
 * Built by tests/run_gfx_hd.py. Draws a demo scene into a
 * 240x240 image by six 240x40 strips (as the firmware does), writes it as a
 * PPM, and checks: strip output equals one tall canvas, narrow canvases do
 * not wrap, nothing is written outside the canvas, full-alpha shapes equal
 * cv_rect / cv_line, gradient ends are exact, circles are symmetric, line
 * weight does not depend on angle, and blends never leave the fg/bg range.
 *
 *   tests/run_gfx_hd.py [OUTDIR]   (compiles, runs, writes OUTDIR/gfx_hd.png and a 4x copy) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DINGHY_HOST 1
#define GUARD 4096u
#define CV_MAX (240u * 240u + GUARD)         /* room for a full-screen canvas plus a guard band */

static uint16_t screen[240u * 240u];          /* what the panel would show, byte-swapped like cv_px */
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    for (uint32_t r = 0; r < h; ++r)
        for (uint32_t c = 0; c < w; ++c)
            if (x + c < 240u && y + r < 240u) screen[(y + r) * 240u + x + c] = p[r * w + c];
}
#include "../firmware/src/gfx.c"
#include "bw_sine.inc"                         /* bw_sine, copied verbatim from instrument_widgets.c by run_gfx_hd.py */

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int32_t q8(int32_t p) { return p * 256 + 128; }
static const uint16_t T1 = RGB(8, 184, 104), T2 = RGB(160, 72, 224), T3 = RGB(32, 128, 240),
                      DK = RGB(240, 72, 40), GOLD = RGB(232, 168, 16), LINE = RGB(48, 56, 58),
                      DIM = RGB(150, 158, 162), BG = RGB(0, 4, 8);

/* bw_quadratic with hd_line_q8 segments (q8 points). */
static void quad_curve(int32_t x0, int32_t y0, int32_t cx, int32_t cy, int32_t x1, int32_t y1, uint32_t w, uint16_t c)
{
    int32_t px = x0, py = y0;
    for (int32_t i = 1; i <= 16; ++i) {
        int32_t a = 16 - i;
        int32_t nx = (a * a * x0 + 2 * a * i * cx + i * i * x1) / 256;
        int32_t ny = (a * a * y0 + 2 * a * i * cy + i * i * y1) / 256;
        hd_line_q8(px, py, nx, ny, w, c);
        px = nx; py = ny;
    }
}

/* bw_pot geometry with HD primitives: s is the widget size in px. */
static void knob(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t width, uint16_t acc)
{
    int32_t cx = q8(x) + (int32_t)s * 128, cy = q8(y) + (int32_t)s * 128;
    uint32_t radius = s * 95u, start = 40960u, sweep = q * 49152u / 127u;
    int32_t ax = cx + bw_sine(start + sweep) * (int32_t)(s * 24u) / 16384;
    int32_t ay = cy - bw_sine(start + sweep + 16384u) * (int32_t)(s * 24u) / 16384;
    int32_t bx = cx + bw_sine(start + sweep) * (int32_t)(s * 57u) / 16384;
    int32_t by = cy - bw_sine(start + sweep + 16384u) * (int32_t)(s * 57u) / 16384;
    hd_circle_q8(cx, cy, (int32_t)(s * 66u), 0, RGB(19, 24, 26));
    hd_arc(cx, cy, radius, width, start, 49152u, LINE);
    hd_arc(cx, cy, radius, width, start, sweep, acc);
    hd_line_q8(ax, ay, bx, by, width, C_WHITE);
}

/* Everything, in graph coordinates of a 240x240 screen. Many shapes cross
 * the strip seams at y = 40, 80, 120, 160, 200 on purpose. */
static void scene(void)
{
    static const uint16_t hue[5] = {RGB(8, 184, 104), RGB(160, 72, 224), RGB(32, 128, 240), RGB(240, 72, 40), RGB(232, 168, 16)};
    int32_t i;
    /* header: bevelled badges in the track hues, a subtle gradient panel across the first seam */
    for (i = 0; i < 5; ++i) hd_bevel(4 + i * 47, 3, 44, 17, 8, hue[i]);
    hd_rrect_grad(4, 24, 232, 32, 6, RGB(44, 48, 52), RGB(10, 12, 14));
    hd_rrect_frame(4, 24, 232, 32, 6, RGB(70, 76, 80));
    hd_rrect(10, 30, 100, 20, 5, T1);                      /* list selection style */
    hd_rrect_frame(116, 30, 112, 20, 10, DIM);             /* pill outline */
    hd_bevel(120, 34, 50, 12, 6, RGB(60, 66, 70));         /* neutral key-cap */
    /* line fan, 1 px, every 7.5 degrees */
    for (i = 0; i < 48; ++i) {
        uint32_t ph = (uint32_t)i * 65536u / 48u;
        hd_line_q8(q8(44), q8(100), q8(44) + bw_sine(ph) * 38 / 64, q8(100) - bw_sine(ph + 16384u) * 38 / 64, 256u, C_WHITE);
    }
    /* stroke weights 1, 1.5, 2, 3 px */
    for (i = 0; i < 4; ++i) {
        static const uint32_t wq[4] = {256u, 384u, 512u, 768u};
        hd_line_q8(q8(90), q8(66 + i * 8), q8(150), q8(62 + i * 12), wq[i], DIM);
    }
    quad_curve(q8(90), q8(120), q8(120), q8(60), q8(150), q8(120), 384u, GOLD);
    hd_line(156, 64, 156, 120, LINE);
    hd_line(160, 64, 236, 64, LINE);
    /* knobs crossing the 120 seam: three widths, three values */
    knob(158, 92, 40, 0, 256u, T1);
    knob(198, 92, 40, 80, 384u, T2);
    knob(158, 132, 40, 127, 512u, T3);
    knob(198, 132, 40, 40, 384u, DK);
    /* discs (radius 1..7) and rings (2..12), whole- and half-pixel centres */
    for (i = 1; i <= 7; ++i) hd_circle(8 + i * i + 2 * i, 152, i, C_WHITE, 1u);
    for (i = 2; i <= 7; ++i) hd_circle_q8(q8(8 + i * i + 3 * i) + 128, q8(178) + 128, i * 256, 256, T3);
    hd_circle_q8(q8(120), q8(196), 12 * 256, 512, GOLD);
    hd_circle(120, 196, 7, T2, 0u);
    /* isometric cube (bw_reverb) with hd_poly4 faces and hd_line edges */
    {
        int32_t cx = 40, cy = 205, r = 20;
        int32_t tx[4] = {cx, cx - r, cx, cx + r}, ty[4] = {cy - r, cy - r / 2, cy, cy - r / 2};
        int32_t lx[4] = {cx - r, cx, cx, cx - r}, ly[4] = {cy - r / 2, cy, cy + r, cy + r / 2};
        int32_t rx[4] = {cx, cx + r, cx + r, cx}, ry[4] = {cy, cy - r / 2, cy + r / 2, cy + r};
        hd_poly4(tx, ty, hd_darken(T1, 35u));
        hd_poly4(lx, ly, hd_darken(T1, 110u));
        hd_poly4(rx, ry, hd_darken(T1, 160u));
        hd_line(tx[0], ty[0], tx[1], ty[1], C_WHITE); hd_line(tx[0], ty[0], tx[3], ty[3], C_WHITE);
        hd_line(tx[1], ty[1], tx[2], ty[2], T1); hd_line(tx[3], ty[3], tx[2], ty[2], T1);
        hd_line(lx[0], ly[0], lx[3], ly[3], T1); hd_line(lx[3], ly[3], lx[2], ly[2], T1);
        hd_line(rx[1], ry[1], rx[2], ry[2], LINE); hd_line(rx[2], ry[2], rx[3], ry[3], T1);
        hd_line(tx[2], ty[2], lx[2], ly[2], T1);
    }
    /* a triangle (repeated vertex), counter-clockwise */
    {
        int32_t px[4] = {76, 100, 64, 64}, py[4] = {186, 228, 222, 222};
        hd_poly4(px, py, DK);
    }
    /* LEDs */
    for (i = 0; i < 5; ++i) {
        hd_glow_dot(150 + i * 18, 186, 7, hue[i]);
        hd_circle(150 + i * 18, 186, 2, hd_lighten(hue[i], 150u), 1u);
    }
    for (i = 0; i < 5; ++i) hd_glow_dot(150 + i * 18, 206, 4 + i, C_WHITE);
    /* bevelled buttons and a fader cap on the last strip */
    hd_bevel(140, 218, 46, 18, 4, T3);
    hd_bevel(190, 218, 46, 18, 9, DK);
    hd_rrect_grad(110, 214, 24, 24, 3, RGB(90, 96, 100), RGB(30, 34, 36));
    /* off-canvas and partly off-canvas shapes */
    hd_circle(-6, 60, 14, T2, 1u);
    hd_circle(250, 60, 16, T2, 0u);
    hd_line(-40, 230, 30, 260, DIM);
    hd_rrect(225, 150, 40, 20, 6, GOLD);
    hd_rrect_frame(-20, 226, 40, 30, 8, DIM);
    hd_arc(q8(238), q8(238), 20 * 256, 384u, 0u, 65536u, DIM);
}

static uint16_t ref[CV_MAX];

/* Draw the scene into strips of `rows` rows and width w, through cv_blit. */
static void render_strips(uint32_t w, uint32_t rows)
{
    memset(screen, 0, sizeof screen);
    for (uint32_t y = 0; y < 240u; y += rows) {
        uint32_t n = 240u - y < rows ? 240u - y : rows;
        cv_begin(w, n, BG);
        for (uint32_t i = w * n; i < CV_MAX; ++i) cv_px[i] = 0xA5A5u;     /* sentinel past the strip */
        cv_oy = -(int32_t)y;
        scene();
        cv_oy = 0;
        for (uint32_t i = w * n, bad = 0; i < CV_MAX && !bad; ++i)
            if (cv_px[i] != 0xA5A5u) { bad = 1; CHECK(0, "write outside a %ux%u strip at oy %d (index %u)", w, n, -(int)y, i); }
        cv_blit(0, y);
    }
}

static int same_as_ref(uint32_t w)
{
    for (uint32_t y = 0; y < 240u; ++y)
        for (uint32_t x = 0; x < w; ++x)
            if (screen[y * 240u + x] != ref[y * 240u + x]) {
                printf("  first difference at %u,%u\n", x, y);
                return 0;
            }
    return 1;
}

static uint16_t px_at(int32_t x, int32_t y) { return swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]); }

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "P6\n240 240\n255\n");
    for (uint32_t i = 0; i < 240u * 240u; ++i) {
        uint16_t c = swap16(screen[i]);
        uint8_t r = (uint8_t)((c >> 11) * 255 / 31), g = (uint8_t)(((c >> 5) & 63) * 255 / 63), b = (uint8_t)((c & 31) * 255 / 31);
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

static void test_strips(void)
{
    cv_begin(240, 240, BG); cv_oy = 0;
    scene();
    memcpy(ref, cv_px, 240u * 240u * sizeof ref[0]);
    render_strips(240, 37);
    CHECK(same_as_ref(240), "37-row strips differ from one tall canvas");
    render_strips(60, 40);                       /* a 60 px column: x clipping must not wrap */
    CHECK(same_as_ref(60), "60-wide strips differ from the left of the full render");
    render_strips(240, 40);                      /* the firmware's strips; this is the demo image */
    CHECK(same_as_ref(240), "40-row strips differ from one tall canvas");
    /* strips far away from the scene: nothing at all may change */
    for (int32_t oy = -2000; oy <= 2000; oy += 4000) {
        cv_begin(240, 40, BG);
        for (uint32_t i = 240u * 40u; i < CV_MAX; ++i) cv_px[i] = 0xA5A5u;
        cv_oy = oy; scene(); cv_oy = 0;
        uint32_t bad = 0;
        for (uint32_t i = 0; i < CV_MAX; ++i) bad += cv_px[i] != (i < 240u * 40u ? swap16(BG) : 0xA5A5u);
        CHECK(!bad, "%u pixels changed with cv_oy %d", bad, oy);
    }
}

static void test_exact(void)
{
    uint16_t a[240u * 40u];
    uint32_t i, ok;
    /* full-alpha rect == cv_rect (also when clipped) */
    cv_begin(240, 40, BG); cv_oy = -3;
    cv_rect(13, 7, 50, 20, T1); cv_rect(200, -5, 60, 20, T2);
    memcpy(a, cv_px, sizeof a);
    cv_begin(240, 40, BG);
    hd_rrect(13, 7, 50, 20, 0, T1); hd_rrect(200, -5, 60, 20, 0, T2);
    CHECK(!memcmp(a, cv_px, sizeof a), "hd_rrect(r = 0) differs from cv_rect");
    cv_begin(240, 40, BG);
    hd_rrect_grad(13, 7, 50, 20, 0, T1, T1); hd_rrect_grad(200, -5, 60, 20, 0, T2, T2);
    CHECK(!memcmp(a, cv_px, sizeof a), "flat gradient differs from cv_rect");
    /* rounded rect: rows clear of the corners are exactly cv_rect */
    cv_begin(240, 40, BG); cv_oy = 0;
    hd_rrect(20, 5, 80, 30, 6, DK);
    ok = 1;
    for (int32_t y = 11; y < 29; ++y) for (int32_t x = 0; x < 240; ++x)
        ok &= px_at(x, y) == ((x >= 20 && x < 100) ? DK : BG);
    CHECK(ok, "rounded rect straight part is not exact");
    CHECK(px_at(21, 6) != DK && px_at(21, 6) != BG && px_at(20, 5) == BG, "rounded corner pixels are wrong");
    /* horizontal and vertical 1 px lines == cv_line */
    cv_begin(240, 40, BG); cv_oy = 0;
    cv_line(10, 5, 100, 5, C_WHITE); cv_line(120, 2, 120, 37, T3);
    memcpy(a, cv_px, sizeof a);
    cv_begin(240, 40, BG);
    hd_line(10, 5, 100, 5, C_WHITE); hd_line(120, 2, 120, 37, T3);
    CHECK(!memcmp(a, cv_px, sizeof a), "axis-aligned hd_line differs from cv_line");
    /* gradient ends are exact, middle row averages to the mix */
    cv_begin(240, 40, C_BLACK);
    hd_rrect_grad(0, 4, 240, 31, 0, RGB(200, 40, 10), RGB(20, 120, 250));
    ok = 1;
    for (i = 0; i < 240u; ++i) ok &= px_at((int32_t)i, 4) == RGB(200, 40, 10) && px_at((int32_t)i, 34) == RGB(20, 120, 250);
    CHECK(ok, "gradient end rows are not the end colours");
    double bias[3] = {0, 0, 0};
    for (int32_t row = 5; row < 34; ++row) {
        double sum[3] = {0, 0, 0}, want[3];
        uint16_t c0 = RGB(200, 40, 10), c1 = RGB(20, 120, 250);
        double t = (double)((row - 4) * 256 / 30) / 256.0;
        for (i = 0; i < 240u; ++i) {
            uint16_t c = px_at((int32_t)i, row);
            sum[0] += c >> 11; sum[1] += (c >> 5) & 63; sum[2] += c & 31;
        }
        want[0] = (c0 >> 11) + ((c1 >> 11) - (c0 >> 11)) * t;
        want[1] = ((c0 >> 5) & 63) + (((c1 >> 5) & 63) - ((c0 >> 5) & 63)) * t;
        want[2] = (c0 & 31) + ((c1 & 31) - (c0 & 31)) * t;
        for (int k = 0; k < 3; ++k) {   /* one row only meets 4 of the 16 thresholds */
            CHECK(sum[k] / 240.0 - want[k] < 0.4 && want[k] - sum[k] / 240.0 < 0.4,
                  "row %d channel %d mean %.3f, want %.3f", row, k, sum[k] / 240.0, want[k]);
            bias[k] += (sum[k] / 240.0 - want[k]) / 29.0;
        }
    }
    for (int k = 0; k < 3; ++k) CHECK(bias[k] < 0.06 && bias[k] > -0.06, "gradient channel %d is biased by %.3f LSB", k, bias[k]);
    /* a full arc is the ring; an empty sweep draws nothing */
    cv_begin(240, 40, BG); cv_oy = 0;
    hd_circle_q8(q8(30), q8(20), 3000, 384, GOLD);
    memcpy(a, cv_px, sizeof a);
    cv_begin(240, 40, BG);
    hd_arc(q8(30), q8(20), 3000u, 384u, 12345u, 65536u, GOLD);
    hd_arc(q8(60), q8(20), 3000u, 384u, 12345u, 0u, GOLD);
    CHECK(!memcmp(a, cv_px, sizeof a), "full-turn hd_arc differs from the ring");
    /* poly4: winding does not matter; a square between pixel centres 10..20 */
    {
        int32_t sx[4] = {10, 20, 20, 10}, sy[4] = {10, 10, 20, 20}, rx[4] = {10, 10, 20, 20}, ry[4] = {10, 20, 20, 10};
        cv_begin(40, 40, C_BLACK); hd_poly4(sx, sy, C_WHITE); memcpy(a, cv_px, 40u * 40u * 2u);
        cv_begin(40, 40, C_BLACK); hd_poly4(rx, ry, C_WHITE);
        CHECK(!memcmp(a, cv_px, 40u * 40u * 2u), "poly4 depends on winding");
        ok = 1;
        for (int32_t y = 0; y < 40; ++y) for (int32_t x = 0; x < 40; ++x) {
            uint32_t g = (px_at(x, y) >> 5) & 63u;
            int inside = x > 10 && x < 20 && y > 10 && y < 20, edge = x >= 10 && x <= 20 && y >= 10 && y <= 20;
            if (inside) ok &= g == 63u;
            else if (edge) ok &= g > 10u && g < 54u;
            else ok &= g == 0u;
        }
        CHECK(ok, "poly4 square coverage is wrong");
    }
}

static void test_symmetry_and_weight(void)
{
    uint32_t r, ok;
    /* discs and rings centred on a pixel are 8-fold symmetric */
    for (r = 1; r <= 18; ++r)
        for (uint32_t fill = 0; fill < 2u; ++fill) {
            cv_begin(41, 41, C_BLACK); cv_oy = 0;
            hd_circle(20, 20, (int32_t)r, C_WHITE, fill);
            ok = 1;
            for (int32_t y = 0; y < 41; ++y) for (int32_t x = 0; x < 41; ++x)
                ok &= px_at(x, y) == px_at(40 - x, y) && px_at(x, y) == px_at(x, 40 - y) && px_at(x, y) == px_at(y, x);
            CHECK(ok, "circle r %u fill %u is not symmetric", r, fill);
            if (!fill) {        /* a 1 px ring has about 2 pi r of ink */
                double ink = 0;
                for (uint32_t i = 0; i < 41u * 41u; ++i) ink += ((swap16(cv_px[i]) >> 5) & 63) / 63.0;
                CHECK(ink > 6.283 * r * 0.88 && ink < 6.283 * r * 1.12, "ring r %u has %.1f px of ink, want %.1f", r, ink, 6.283 * r);
            }
        }
    /* 1 px lines carry about the same ink at every angle. The distance ramp
     * gives a 45-degree line through pixel centres up to ~12% more code-value
     * ink than a flat one; in light (gamma 2.2) it is still a little dimmer. */
    for (uint32_t ph = 0; ph < 16384u; ph += 1024u) {
        double ink = 0, want = 60.0 + 3.14159 * 0.25;
        cv_begin(160, 160, C_BLACK); cv_oy = 0;
        hd_line_q8(q8(50), q8(50), q8(50) + bw_sine(ph) * 60 / 64, q8(50) + bw_sine(ph + 16384u) * 60 / 64, 256u, C_WHITE);
        for (uint32_t i = 0; i < 160u * 160u; ++i) ink += ((swap16(cv_px[i]) >> 5) & 63) / 63.0;
        CHECK(ink > want * 0.95 && ink < want * 1.13, "line at phase %u has %.1f px of ink, want %.1f", ph, ink, want);
    }
    /* blends never leave the range between background and colour (no dark fringes) */
    {
        uint16_t bg = RGB(90, 100, 110), fg = RGB(250, 200, 20);
        cv_begin(60, 60, bg); cv_oy = 0;
        hd_circle(30, 30, 12, fg, 1u); hd_circle_q8(q8(30) + 77, q8(30) + 33, 5000, 300, fg);
        hd_line_q8(q8(3), q8(5), q8(57) + 40, q8(50), 400u, fg);
        hd_arc(q8(30), q8(30), 6000u, 512u, 40960u, 30000u, fg);
        hd_glow_dot(10, 50, 6, fg);
        ok = 1;
        for (uint32_t i = 0; i < 3600u; ++i) {
            uint16_t c = swap16(cv_px[i]);
            uint32_t ch[3] = {c >> 11, (c >> 5) & 63u, c & 31u}, lo[3], hi[3];
            uint32_t b3[3] = {bg >> 11, (bg >> 5) & 63u, bg & 31u}, f3[3] = {fg >> 11, (fg >> 5) & 63u, fg & 31u};
            for (int k = 0; k < 3; ++k) {
                lo[k] = b3[k] < f3[k] ? b3[k] : f3[k]; hi[k] = b3[k] < f3[k] ? f3[k] : b3[k];
                ok &= ch[k] >= lo[k] && ch[k] <= hi[k];
            }
        }
        CHECK(ok, "a blended pixel left the fg/bg range");
    }
}

int main(int argc, char **argv)
{
    test_exact();
    test_symmetry_and_weight();
    test_strips();                   /* last: leaves the 40-row strip render in screen[] */
    write_ppm(argc > 1 ? argv[1] : "gfx_hd.ppm");
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("gfx_hd: all checks passed\n");
    return 0;
}
