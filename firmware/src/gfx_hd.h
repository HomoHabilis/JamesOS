/* SPDX-License-Identifier: GPL-3.0-only */
/* JamesOS HD rendering layer. BR_UI_HD is always 1: the standard (pixel)
 * interface was removed, and BR_UI_HD 0 is a compile-time error.
 *
 * The strip canvas is unchanged: cv_px holds cv_w x cv_h RGB565 pixels,
 * byte-swapped for the panel, and cv_oy offsets graph drawing (cv_ox: the
 * graph column of canvas column 0, for a part of a wider picture). HD drawing
 * blends into that strip, so every primitive must clip to it. Integer math
 * only; no heap; no per-frame buffers beyond the strip. */
#ifndef GFX_HD_H_
#define GFX_HD_H_
#ifndef BR_UI_HD
#define BR_UI_HD 1
#endif
#if !BR_UI_HD
#error "the standard interface was removed"
#endif

/* ---- core: shared by shapes (gfx_hd.h) and text (gfx_hd_text.h) ---- */

/* Mix two RGB565 colours. t = 0 gives a, t = 255 gives b. */
static inline uint16_t hd_mix(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t ar = a >> 11, ag = (a >> 5) & 63u, ab = a & 31u;
    uint32_t br = b >> 11, bg = (b >> 5) & 63u, bb = b & 31u;
    t += t >> 7;                                  /* 0..256 */
    return (uint16_t)((((ar * (256u - t) + br * t) >> 8) << 11) |
                      (((ag * (256u - t) + bg * t) >> 8) << 5) |
                      ((ab * (256u - t) + bb * t) >> 8));
}

/* Read a canvas pixel in native RGB565. Coordinates are graph coordinates
 * (cv_oy applies), like cv_pset. Off-canvas reads return the background. */
static inline uint16_t hd_get(int32_t x, int32_t y)
{
    y += cv_oy; x -= cv_ox;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h) return cv_background;
    return swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
}

/* Blend colour c over the canvas with coverage a (0..255). */
static inline void hd_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    y += cv_oy; x -= cv_ox;
    if (!a || (uint32_t)x >= cv_w || (uint32_t)y >= cv_h) return;
    uint16_t *p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    *p = a >= 255u ? swap16(c) : swap16(hd_mix(swap16(*p), c, a));
}

/* ---- shapes: added below this line ---- */

/* Anti-aliased shapes. Each one works out a signed distance per pixel and
 * turns it into coverage (1 px ramp centred on the edge). They are stateless
 * and touch only the pixels of the current strip, so a shape drawn once per
 * strip comes out the same as one drawn on a single tall canvas.
 *
 * Units. Names ending in _q8, and hd_arc (a drop-in for bw_arc), take 24.8
 * fixed point where pixel p's centre is p*256+128, exactly bw_q8(p). All
 * others take whole pixels: hd_line, hd_circle, hd_glow_dot and hd_poly4 by
 * pixel centre (as cv_pset / cv_line), the rect family as x, y, w, h boxes
 * (as cv_rect). Angles are bw_sine phases: 65536 is one turn, 0 points up,
 * phase grows clockwise. Practical range: |coordinates| < 512 px, radius and
 * edge lengths < 250 px. Dither is keyed on graph coordinates, which do not
 * change from strip to strip. */

static int32_t bw_sine(uint32_t phase);       /* instrument_widgets.c */

static const uint8_t hd_bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static inline uint16_t hd_lighten(uint16_t c, uint32_t amount) { return hd_mix(c, 0xFFFFu, amount); }
static inline uint16_t hd_darken(uint16_t c, uint32_t amount) { return hd_mix(c, 0x0000u, amount); }

/* Blend with a coverage that may be out of range (clamped to 0..255). */
static __attribute__((noinline)) void hd__px(int32_t x, int32_t y, uint16_t c, int32_t a)
{
    if (a > 0) hd_blend(x, y, c, a > 255 ? 255u : (uint32_t)a);
}

static uint32_t hd__isqrt(uint32_t n)
{
    uint32_t r = 0, b;
    if (!n) return 0;
    b = 1u << ((31 - __builtin_clz(n)) & ~1u);         /* the largest power of 4 <= n */
    for (; b; b >>= 2) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
    }
    return r;
}

/* Length of a q4 vector, in q8. */
static int32_t hd__len(int32_t dx, int32_t dy)
{
    uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
    return (int32_t)(d2 < 0x1000000u ? hd__isqrt(d2 << 8) : hd__isqrt(d2) << 4);
}

/* q8 to q4, held to +-512 px so the products below stay in 32 bits. */
static int32_t hd__q4(int32_t v)
{
    if (v > (512 << 8)) v = 512 << 8;
    if (v < -(512 << 8)) v = -(512 << 8);
    return v >> 4;
}

/* Where hd__len(vx, vy) lies against two q8 radii, without the square root
 * (below 256 px hd__len is floor(sqrt(256 d2)), so squares compare exactly):
 * 0 when it is >= out, 1 when it is < in, otherwise (or too far to tell) 2. */
static __attribute__((noinline)) int hd__zone(int32_t vx, int32_t vy, int32_t out, int32_t in)
{
    uint32_t d2 = (uint32_t)(vx * vx) + (uint32_t)(vy * vy);
    if (d2 >= 0x1000000u) return 2;
    d2 <<= 8;
    if (out >= 0 && out < 65536 && d2 >= (uint32_t)out * (uint32_t)out) return 0;
    return in > 0 && in < 65536 && d2 < (uint32_t)in * (uint32_t)in ? 1 : 2;
}
#define hd__far(vx, vy, lim) (!hd__zone((vx), (vy), (lim), 0))     /* exactly hd__len >= lim */

/* Marks (animation): while hd_mark.on, every shape records its box before clipping, so a drawing
 * run with an empty strip (cv_h 0) finds where its marked shapes would land (instrument_ui.c). Each
 * mark (instrument_widgets.c BW_MARK) starts a box of its own; past the last box they share it. */
#define HD_MARKS 2
static struct { int32_t b[HD_MARKS][4]; uint8_t on, n; } hd_mark;
/* Clip an inclusive pixel box {x0, y0, x1, y1} (graph coordinates) to the
 * strip. Returns 0 when nothing is left. */
static int hd__clip(int32_t b[4])
{
    int32_t top = -cv_oy, bottom = top + (int32_t)cv_h - 1;
    if (hd_mark.on)
        for (int32_t i = 0, *m = hd_mark.b[hd_mark.n - 1u]; i < 4; ++i) if ((i < 2) == (b[i] < m[i])) m[i] = b[i];
    if (b[0] < cv_ox) b[0] = cv_ox;
    if (b[2] > cv_ox + (int32_t)cv_w - 1) b[2] = cv_ox + (int32_t)cv_w - 1;
    if (b[1] < top) b[1] = top;
    if (b[3] > bottom) b[3] = bottom;
    return b[0] <= b[2] && b[1] <= b[3];
}

/* Stroke from (x0, y0) to (x1, y1), q8 like bw_qline, width in q8 (256 =
 * 1 px), round caps so polylines join cleanly. */
static void hd_line_q8(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t width, uint16_t c)
{
    int32_t ax = hd__q4(x0), ay = hd__q4(y0), dx = hd__q4(x1) - ax, dy = hd__q4(y1) - ay;
    int32_t len = hd__len(dx, dy), hw = (int32_t)(width >> 1), ux = 0, uy = 0, b[4], x, y;
    if (len) { ux = dx * 65536 / len; uy = dy * 65536 / len; }        /* q12 direction */
    b[0] = ((dx < 0 ? ax + dx : ax) * 16 - hw - 256) >> 8;
    b[2] = ((dx < 0 ? ax : ax + dx) * 16 + hw + 256) >> 8;
    b[1] = ((dy < 0 ? ay + dy : ay) * 16 - hw - 256) >> 8;
    b[3] = ((dy < 0 ? ay : ay + dy) * 16 + hw + 256) >> 8;
    if (!width || !hd__clip(b)) return;
    for (y = b[1]; y <= b[3]; ++y)
        for (x = b[0]; x <= b[2]; ++x) {
            int32_t vx = x * 16 + 8 - ax, vy = y * 16 + 8 - ay, d;
            int32_t t = (vx * ux + vy * uy) >> 8;                       /* q8 along */
            if (t <= 0 || t >= len) {                                   /* round cap */
                if (t > 0) { vx -= dx; vy -= dy; }
                if (hd__far(vx, vy, hw + 128)) continue;
                d = hd__len(vx, vy);
            } else { d = (vx * uy - vy * ux) >> 8; if (d < 0) d = -d; }  /* q8 across */
            if (hw - d + 128 > 0) hd__px(x, y, c, hw - d + 128);
        }
}

/* 1 px line between pixel centres. Horizontal and vertical ones set the
 * same pixels as cv_line. */
static void hd_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    hd_line_q8(x0 * 256 + 128, y0 * 256 + 128, x1 * 256 + 128, y1 * 256 + 128, 256u, c);
}

/* Shared by circles, rings, arcs and glows (all q8):
 * w > 0 ring of stroke w centred on radius r; w == 0 disc of radius r;
 * w < 0 glow fading from the centre to radius r.
 * sweep 0 is the whole circle, otherwise the wedge from start clockwise. */
static void hd__radial(int32_t cx, int32_t cy, int32_t r, int32_t w,
                       uint32_t start, uint32_t sweep, uint16_t c)
{
    int32_t qx = hd__q4(cx), qy = hd__q4(cy), hw = w > 0 ? w >> 1 : 0, ext = r + hw + 256;
    int32_t sx = 0, sy = 0, ex = 0, ey = 0, inv = (1 << 24) / (r > 256 ? r : 256), b[4], x, y;
    if (sweep) {
        sx = bw_sine(start); sy = -bw_sine(start + 16384u);
        ex = bw_sine(start + sweep); ey = -bw_sine(start + sweep + 16384u);
    }
    /* Exact bounds (hd__zone): nothing at or beyond out; below in, a ring's
     * hole (nothing) or a disc's full coverage. */
    int32_t out = w < 0 ? r + 256 : r + hw + 128, in = w > 0 ? r - hw - 127 : w ? 0 : r - 126, k;
    b[0] = (cx - ext) >> 8; b[2] = (cx + ext) >> 8;
    b[1] = (cy - ext) >> 8; b[3] = (cy + ext) >> 8;
    /* An arc: the box of its ends (inner and outer radius) and the quarter points it passes. Only when its
     * hole is wide enough: the end lines' half-pixel margins meet behind the centre, 128 / sin(half sweep)
     * away, and nearer than the hole they reach the far side of the ring. */
    if (sweep && w > 0 && in > 0 && in * bw_sine((sweep > 32768u ? 65536u - sweep : sweep) >> 1) >> 14 > 384) {
        int32_t e[4], p[8] = {sx, sy, ex, ey, sx, sy, ex, ey};
        for (k = 0; k < 8; ++k) {
            p[k] = p[k] * (k < 4 ? out : in) >> 14;
            if (k < 2) e[k] = e[k + 2] = p[k];
            if (p[k] < e[k & 1]) e[k & 1] = p[k];
            if (p[k] > e[(k & 1) + 2]) e[(k & 1) + 2] = p[k];
        }
        for (k = 0; k < 4; ++k)           /* (up, right, down, left: the extreme on that side) */
            if ((((uint32_t)k * 16384u - start) & 65535u) <= sweep) e[(k + 1) & 3] = (k + 1) & 2 ? out : -out;
        for (k = 0; k < 4; ++k) {   /* (a pixel half a pixel past an end line still takes coverage: two px of room) */
            int32_t v = ((((k & 1) ? cy : cx) + e[k] + (k < 2 ? -384 : 384)) >> 8) + (k < 2 ? -1 : 1);
            if ((k < 2) == (v > b[k])) b[k] = v;
        }
    }
    if (r < 0 || !hd__clip(b)) return;
    for (y = b[1]; y <= b[3]; ++y) {
        /* Only the columns where (dx^2 + dy^2) * 256 < out^2 can be covered (hd__zone 0 elsewhere): the row's
         * span from an integer square root (dx^2 < ceil(out^2 / 256) - dy^2), widened by a column each side. */
        int32_t dy = y * 16 + 8 - qy, x0 = b[0], x1 = b[2];
        int32_t lim = out >= 0 && out < 65536 ? (int32_t)(((uint32_t)out * (uint32_t)out + 255u) >> 8) - dy * dy : 0x7fffffff;
        if (lim <= 0) continue;
        if (lim < 0x7fffffff) {
            int32_t reach = (int32_t)hd__isqrt((uint32_t)lim) + 1;
            if (x0 < ((qx - reach - 8) >> 4) - 1) x0 = ((qx - reach - 8) >> 4) - 1;
            if (x1 > ((qx + reach - 8) >> 4) + 1) x1 = ((qx + reach - 8) >> 4) + 1;
        }
        for (x = x0; x <= x1; ++x) {
            int32_t dx = x * 16 + 8 - qx, d, a, m = 256;
            if (!(k = hd__zone(dx, dy, out, in)) || (k == 1 && w > 0)) continue;
            if (sweep) {     /* q8 distances to the two end radii (q14 x q4 >> 10) */
                int32_t c1 = (sx * dy - sy * dx) >> 10, c2 = (dx * ey - dy * ex) >> 10;
                m = (sweep > 32768u ? (c1 > c2 ? c1 : c2) : (c1 < c2 ? c1 : c2)) + 128;
                if (m <= 0) continue;
            }
            if (k == 1) a = 255;
            else {
                d = hd__len(dx, dy);
                if (w > 0) { a = d - r; a = hw - (a < 0 ? -a : a) + 128; }
                else if (!w) a = r - d + 128;
                else if ((a = 256 - (d * inv >> 16)) > 0) a = a * a >> 7;  /* 2(1-d/r)^2 */
                if (a <= 0) continue;
                if (a > 255) a = 255;
            }
            if (m < 256) a = a * m >> 8;
            hd__px(x, y, c, a);
        }
    }
}

/* Disc (w == 0) or ring of stroke w centred on radius r; all q8. */
static void hd_circle_q8(int32_t cx, int32_t cy, int32_t r, int32_t w, uint16_t c)
{
    hd__radial(cx, cy, r, w > 0 ? w : 0, 0u, 0u, c);
}

/* Integer twin of bw_circle: centre pixel, radius in px; fill 0 gives a 1 px ring. */
static void hd_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t fill)
{
    hd__radial(cx * 256 + 128, cy * 256 + 128, r * 256, fill ? 0 : 256, 0u, 0u, c);
}

/* Drop-in for bw_arc: centre, radius and width in q8; start and sweep in
 * bw_sine phase (65536 = turn, 0 = up, clockwise). Flat anti-aliased ends. */
static void hd_arc(int32_t cx, int32_t cy, uint32_t radius, uint32_t width,
                   uint32_t start, uint32_t sweep, uint16_t c)
{
    if (!sweep || !width) return;
    hd__radial(cx, cy, (int32_t)radius, (int32_t)width, start, sweep >= 65536u ? 0u : sweep, c);
}

/* Soft LED dot: centre pixel, radius in px; opaque core, quadratic falloff. */
static void hd_glow_dot(int32_t cx, int32_t cy, int32_t radius, uint16_t c)
{
    hd__radial(cx * 256 + 128, cy * 256 + 128, radius * 256, -1, 0u, 0u, c);
}

/* Mix a toward b by t/256 with a 4x4 ordered dither on each channel. */
static uint16_t hd__dmix(uint16_t a, uint16_t b, int32_t t, int32_t x, int32_t y)
{
    int32_t k = hd_bayer4[((y & 3) << 2) | (x & 3)] * 16 + 8;
    int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    ar = ((ar << 8) + ((b >> 11) - ar) * t + k) >> 8;
    ag = ((ag << 8) + (((b >> 5) & 63) - ag) * t + k) >> 8;
    ab = ((ab << 8) + ((b & 31) - ab) * t + k) >> 8;
    return (uint16_t)((ar << 11) | (ag << 5) | ab);
}

/* Rounded box over pixels x..x+w-1, y..y+h-1 (cv_rect's box). Rows run
 * top -> bot (dithered), the first row is hi and the last lo. frame draws
 * only the 1 px outline. */
static void hd__rrect(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t r,
                      uint16_t top, uint16_t bot, uint16_t hi, uint16_t lo, int frame)
{
    int32_t b[4], x, y;
    b[0] = x0; b[1] = y0; b[2] = x0 + w - 1; b[3] = y0 + h - 1;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    /* Corner pixels (hd__zone): beyond out nothing, below in fully covered
     * (a frame: nothing). */
    int32_t out = r * 256 + 128, in = frame ? r * 256 - 383 : r * 256 - 126;
    if (w <= 0 || h <= 0 || !hd__clip(b)) return;
    for (y = b[1]; y <= b[3]; ++y) {
        int32_t j = y - y0, t = h > 1 ? j * 256 / (h - 1) : 0, cy = 0, m0 = b[0], m1 = b[2], step = 1;
        uint16_t ca = top, cb = bot, col[4];
        if (!j) ca = cb = hi;
        else if (j == h - 1) ca = cb = lo;
        if (j < r) cy = (r - j) * 16 - 8;                   /* q4 from the corner centre */
        else if (j >= h - r) cy = (j - h + r) * 16 + 8;
        for (x = 0; x < 4; ++x) col[x] = ca == cb ? ca : hd__dmix(ca, cb, t, x, y);   /* the dither repeats every 4 px */
        if (cy) {                                           /* anti-aliased corner pixels */
            for (x = b[0]; x <= b[2]; ++x) {
                int32_t i = x - x0, cx, a;
                if (i < r) cx = (r - i) * 16 - 8;
                else if (i >= w - r) cx = (i - w + r) * 16 + 8;
                else { x = x0 + w - r - 1; continue; }      /* skip to the right corner */
                if (!(a = hd__zone(cx, cy, out, in))) continue;
                if (a == 1) { if (!frame) hd__px(x, y, col[x & 3], 255); continue; }
                a = r * 256 + 128 - hd__len(cx, cy);
                if (frame) a = 256 - (a < 256 ? 256 - a : a - 256);  /* 1 px band inside the edge */
                hd__px(x, y, col[x & 3], a);
            }
            if (m0 < x0 + r) m0 = x0 + r;
            if (m1 > x0 + w - r - 1) m1 = x0 + w - r - 1;
        }
        if (frame && j && j != h - 1) {                     /* a frame's sides: the outer columns only */
            if (cy) continue;
            m0 = x0; m1 = x0 + w - 1; step = w > 1 ? w - 1 : 1;
        }
        for (x = m0; x <= m1; x += step) hd_blend(x, y, col[x & 3], 255u);   /* opaque run (hd_blend clips) */
    }
}

static void hd_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    hd__rrect(x, y, w, h, r, c, c, c, c, 0);
}

/* Vertical gradient, top row exactly top, bottom row exactly bottom. */
static void hd_rrect_grad(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t top, uint16_t bottom)
{
    hd__rrect(x, y, w, h, r, top, bottom, top, bottom, 0);
}

static void hd_rrect_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    hd__rrect(x, y, w, h, r, c, c, c, c, 1);
}

/* Raised surface: lighter top, darker bottom, 1 px highlight, darker base. */
static void hd_bevel(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t face)
{
    hd__rrect(x, y, w, h, r, hd_lighten(face, 24u), hd_darken(face, 36u),
              hd_lighten(face, 80u), hd_darken(face, 96u), 0);
}

/* Filled convex quadrilateral, either winding (a triangle may repeat a
 * vertex). Vertices are pixel centres, as hd_line, so faces and their
 * outlines meet. Replaces bw_face. */
static void hd_poly4(const int32_t px[4], const int32_t py[4], uint16_t c)
{
    int32_t nx[4], ny[4], b[4], area = 0, i, x, y;
    b[0] = b[2] = px[0]; b[1] = b[3] = py[0];
    for (i = 0; i < 4; ++i) {
        int32_t j = (i + 1) & 3;
        area += px[i] * py[j] - px[j] * py[i];
        if (px[i] < b[0]) b[0] = px[i];
        if (px[i] > b[2]) b[2] = px[i];
        if (py[i] < b[1]) b[1] = py[i];
        if (py[i] > b[3]) b[3] = py[i];
    }
    for (i = 0; i < 4; ++i) {    /* inward unit normals, q12 */
        int32_t j = (i + 1) & 3, dx = px[j] - px[i], dy = py[j] - py[i], len = hd__len(dx * 16, dy * 16);
        if (area < 0) { dx = -dx; dy = -dy; }
        nx[i] = len ? -dy * (1 << 20) / len : 0;
        ny[i] = len ? dx * (1 << 20) / len : 0;
    }
    --b[0]; --b[1]; ++b[2]; ++b[3];
    if (!area || !hd__clip(b)) return;
    for (y = b[1]; y <= b[3]; ++y)
        for (x = b[0]; x <= b[2]; ++x) {
            int32_t a = 1 << 20;
            for (i = 0; i < 4; ++i)
                if (nx[i] | ny[i]) {
                    int32_t d = ((x - px[i]) * nx[i] + (y - py[i]) * ny[i]) >> 4;
                    if (d < a) a = d;
                }
            hd__px(x, y, c, a + 128);
        }
}

#endif /* GFX_HD_H_ */
