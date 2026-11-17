/* SPDX-License-Identifier: GPL-3.0-only */
/* Helpers for the HD look tests (tests/test_*_hd.c; include after firmware/src/dinghy.c). They
 * measure what the HD layer draws on a canvas (cv_px, byte-swapped RGB565) or on the host panel
 * (fb): the bounding box of what differs from the background, exact mirror symmetry about that
 * box's centre, a pixel's brightness, and PPM crops for defect reports. They hold no state. */
#ifndef BR_HD_LOOK_H
#define BR_HD_LOOK_H
#include <stdio.h>
#include <string.h>
typedef struct { int32_t left, top, right, bottom; } hd_box_t;

/* Checks report and continue, so one defect does not hide the others; the test exits 1 if any failed. */
static uint32_t hd_failures, hd_checks;
static const char *hd_crop_dir = "build";
#define HD_CHECK(cond, ...) do { ++hd_checks; if (!(cond)) { ++hd_failures; \
    printf("FAIL  %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); } } while (0)
static int hd_finish(const char *what)
{
    if (hd_failures) printf("%u of %u HD checks failed: %s\n", hd_failures, hd_checks, what);
    else printf("PASS: %s (%u HD checks)\n", what, hd_checks);
    return hd_failures ? 1 : 0;
}

/* Pixels of an RGB565 image (px, w x h; swapped: 1 for cv_px, 0 for fb) that differ from bg. */
static hd_box_t hd_box(const uint16_t *px, int32_t w, int32_t h, uint16_t bg, int swapped)
{
    hd_box_t b = {w, h, -1, -1};
    uint16_t want = swapped ? swap16(bg) : bg;
    for (int32_t y = 0; y < h; ++y) for (int32_t x = 0; x < w; ++x)
        if (px[y * w + x] != want) {
            if (x < b.left) b.left = x; if (x > b.right) b.right = x;
            if (y < b.top) b.top = y; if (y > b.bottom) b.bottom = y;
        }
    return b;
}
/* Two swapped or unswapped colours (same kind) within tol 565 steps on every channel: the HD
 * gradients use ordered dither (HD-SPEC), whose pattern does not mirror, so mirrored gradient
 * pixels may differ by one step. */
static int hd_near(uint16_t a, uint16_t b, uint32_t tol, int swapped)
{
    if (swapped) { a = swap16(a); b = swap16(b); }
    int32_t dr = (int32_t)((a >> 11) & 31u) - (int32_t)((b >> 11) & 31u);
    int32_t dg = (int32_t)((a >> 5) & 63u) - (int32_t)((b >> 5) & 63u);
    int32_t db = (int32_t)(a & 31u) - (int32_t)(b & 31u);
    return abs(dr) <= (int32_t)tol && abs(dg) <= (int32_t)tol && abs(db) <= (int32_t)tol;
}
/* Pixels that do not equal their mirror image about the box centre (exact colours). */
static uint32_t hd_mirror_x(const uint16_t *px, int32_t w, hd_box_t b)
{
    uint32_t n = 0;
    for (int32_t y = b.top; y <= b.bottom; ++y) for (int32_t x = b.left; x <= b.right; ++x)
        n += px[y * w + x] != px[y * w + b.left + b.right - x];
    return n;
}
static uint32_t hd_mirror_y(const uint16_t *px, int32_t w, hd_box_t b)
{
    uint32_t n = 0;
    for (int32_t y = b.top; y <= b.bottom; ++y) for (int32_t x = b.left; x <= b.right; ++x)
        n += px[y * w + x] != px[(b.top + b.bottom - y) * w + x];
    return n;
}
/* 0..1023: R + 2G + B of an unswapped RGB565 colour, each channel scaled to 0..255. */
static uint32_t hd_luma(uint16_t c)
{
    return ((c >> 11) & 31u) * 255u / 31u + 2u * (((c >> 5) & 63u) * 255u / 63u) + (c & 31u) * 255u / 31u;
}
/* A crop of fb (or of an unswapped image of width w), scaled up, as a binary PPM (defect reports). */
static void hd_write_crop(const char *path, const uint16_t *px, int32_t w, int swapped,
                          int32_t x0, int32_t y0, int32_t cw, int32_t ch, int32_t scale)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", cw * scale, ch * scale);
    for (int32_t y = 0; y < ch * scale; ++y) for (int32_t x = 0; x < cw * scale; ++x) {
        uint16_t c = px[(y0 + y / scale) * w + x0 + x / scale];
        if (swapped) c = swap16(c);
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31u) * 255u / 31u),
                                (unsigned char)(((c >> 5) & 63u) * 255u / 63u), (unsigned char)((c & 31u) * 255u / 31u)};
        fwrite(rgb, 3, 1, f);
    }
    fclose(f);
}
#endif
