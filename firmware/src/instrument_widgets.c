/* SPDX-License-Identifier: GPL-3.0-only */
/* Compact control illustrations for instrument_ui.c; included after gfx/colors. */

enum br_widget_kind {
    BR_WIDGET_POT, BR_WIDGET_FADER, BR_WIDGET_PERCENT, BR_WIDGET_TOGGLE,
    BR_WIDGET_REVERB, BR_WIDGET_DELAY, BR_WIDGET_DELAY_FEEDBACK,
    BR_WIDGET_FX_ENGINE, BR_WIDGET_PAN, BR_WIDGET_ADSR,
    BR_WIDGET_FILTER, BR_WIDGET_GATE, BR_WIDGET_WAVE, BR_WIDGET_CHOICE, BR_WIDGET_DRIVE,
    BR_WIDGET_ROUTE, BR_WIDGET_TIMBRE, BR_WIDGET_COLOR, BR_WIDGET_HARMONICS,
    BR_WIDGET_MIX, BR_WIDGET_AMOUNT, BR_WIDGET_TRACK_PAN, BR_WIDGET_HEAT,
    BR_WIDGET_MOD_ATTEN, BR_WIDGET_RESONANCE, BR_WIDGET_SWING, BR_WIDGET_BPM,
    BR_WIDGET_POT_BIPOLAR, BR_WIDGET_COMP_SOURCE, BR_WIDGET_MONITOR,
    BR_WIDGET_ARP_MODE, BR_WIDGET_ARP_RATE, BR_WIDGET_PATCH_OFF,
    BR_WIDGET_FILTER_TARGET, BR_WIDGET_RESONANCE_TARGET, BR_WIDGET_MIDI_CHANNEL
    /* Parameter graphics (PARAM-GRAPHICS-BUILD.md), one group per batch file
     * instrument_widgets_params_a.c .. _d.c. Append only; never reorder. */
    , BR_WIDGET_ENV_AMOUNT, BR_WIDGET_KEYTRACK, BR_WIDGET_TUNE, BR_WIDGET_ENV_MODE,  /* A */
    BR_WIDGET_LFO_DEPTH, BR_WIDGET_ENABLE, BR_WIDGET_FOLLOWER,
    BR_WIDGET_GATE_SOURCE, BR_WIDGET_VELOCITY, BR_WIDGET_SMOOTH,
    BR_WIDGET_SEND, BR_WIDGET_DRIVE_GAIN, BR_WIDGET_TONE, BR_WIDGET_ECHO,           /* B */
    BR_WIDGET_ORBIT, BR_WIDGET_STEREO_DEPTH, BR_WIDGET_TAIL, BR_WIDGET_LOOP_LEN,
    BR_WIDGET_PUNCH_FILTER, BR_WIDGET_TREMOLO,
    BR_WIDGET_SNAP, BR_WIDGET_COMP_CURVE, BR_WIDGET_COMP_TIME,                     /* C */
    BR_WIDGET_HEAT_AMOUNT, BR_WIDGET_CLOCK_SOURCE,
    BR_WIDGET_STEPS, BR_WIDGET_SPEED, BR_WIDGET_QUANTIZE, BR_WIDGET_OCTAVES,       /* D */
    BR_WIDGET_ARP_GATE,
    BR_WIDGET_DRIVE_BIAS, BR_WIDGET_MOD_FEEDBACK,              /* B: FX knob 4 */
    BR_WIDGET_PRE_DELAY,
    BR_WIDGET_NONE,                           /* hook result: "this batch does not map the slot" */
    BR_WIDGET_PARAM_FIRST = BR_WIDGET_ENV_AMOUNT
};
/* br_param_variant_*: "this batch does not set the variant" (no real variant is all ones). */
#define BR_PARAM_UNHANDLED 0xFFFFFFFFu
/* br_param_animated_*: how a slot moves. 0 static; CLOCK = the 8 Hz clock of
 * the evolving widgets (still when the track is silent and stopped);
 * LFO1 / LFO2 = that LFO's live phase. */
enum { BR_PARAM_ANIM_NONE, BR_PARAM_ANIM_CLOCK, BR_PARAM_ANIM_LFO1, BR_PARAM_ANIM_LFO2, BR_PARAM_ANIM_GLOBAL_LFO };
/* Variants of kinds that one batch draws and another maps (fixed here so
 * neither has to edit the other's file). */
enum { BR_BLEND_OSC, BR_BLEND_PAR };                         /* BR_WIDGET_MIX (D01, B) */
enum { BR_SEND_DRIVE, BR_SEND_DELAY, BR_SEND_MOD, BR_SEND_REVERB };   /* BR_WIDGET_SEND (D02, B): br_fx_icon's k */
enum { BR_ORBIT_LFO, BR_ORBIT_TARGET_LFO1, BR_ORBIT_TARGET_LFO2 }; /* BR_WIDGET_ORBIT (D10, B) */
static void br_track_badge(int32_t x, int32_t y, uint32_t part, uint32_t compact);

#include "instrument_shapes.c"

/* Fixed-point circular geometry. A quarter-wave table positions needles and
 * waveforms before the renderer snaps the artwork to solid 2x2 cells. */
static const int16_t bw_sine_quarter[33] = {0,804,1606,2404,3196,3981,4756,5520,6270,7005,7723,8423,9102,9760,10394,11003,11585,12140,12665,13160,13623,14053,14449,14811,15137,15426,15679,15893,16069,16207,16305,16364,16384};
static uint32_t bw_clamp(uint32_t v, uint32_t hi) { return v > hi ? hi : v; }
static int32_t bw_sine_sample(uint32_t index)
{
    uint32_t at = index & 31u, quadrant = (index >> 5) & 3u;
    int32_t v = bw_sine_quarter[quadrant & 1u ? 32u - at : at];
    return quadrant >= 2u ? -v : v;
}
static int32_t bw_sine(uint32_t phase)
{
    uint32_t at = (phase >> 9) & 127u, fraction = phase & 511u;
    int32_t a = bw_sine_sample(at), b = bw_sine_sample((at + 1u) & 127u);
    return a + (b - a) * (int32_t)fraction / 512;
}
/* Both metronomes use the same eased half-cycle and beat direction. */
static int32_t br_metronome_swing(uint32_t phase, uint32_t beat)
{
    int32_t swing = -(bw_sine(16384u + (phase & 127u) * 256u) * 32 / 16384);
    return (beat & 1u) ? -swing : swing;
}
static int32_t bw_q8(int32_t pixel) { return pixel * 256 + 128; }
/* The evolving pictures (br_widget_evolving) draw everything that follows the animation phase inside
 * BW_MARK, and nothing else depends on the phase: a phase step changes only pixels inside the boxes of
 * the marks of the old and the new phase (instrument_ui.c redraws just those; tests/test_anim_marks.c
 * checks it for every phase, value and variant). */
static __attribute__((noinline)) void bw_mark_begin(void)
{
    if (hd_mark.n < HD_MARKS) ++hd_mark.n;
    hd_mark.on = 1u;
}
#define BW_MARK(...) do { bw_mark_begin(); __VA_ARGS__; hd_mark.on = 0u; } while (0)
/* Pixel-art strokes are one 2x2 cell wide at every widget size. */
static uint32_t bw_weight(uint32_t size) { (void)size; return 512u; }
static __attribute__((noinline)) uint16_t bw_tone(uint16_t color, uint32_t amount)
{
    return (uint16_t)((((color >> 11) * amount / 255u) << 11) |
        ((((color >> 5) & 63u) * amount / 255u) << 5) |
        ((color & 31u) * amount / 255u));
}
static __attribute__((noinline)) uint16_t bw_highlight(uint32_t pale)
{
    return br_palette_color((br_ui.page == BR_PAGE_MIX || br_ui.page == BR_PAGE_PUNCH) ? BR_PARTS : br_ui.part,
                            pale ? 2u : 1u);
}
/* HD: the same primitives with the same arguments, drawn as smooth
 * anti-aliased geometry at native resolution (gfx_hd.h) instead of 2x2 cells.
 * Pixel arguments name pixel centres; widths keep their q8 meaning, except
 * that the pixel-art cell widths (256..512, all drawn two pixels wide) map to
 * a fine 1..1.5 px stroke. */
static uint32_t bw_hd_width(uint32_t width)
{
    return width <= 256u ? 256u : width <= 512u ? 384u : width;
}
/* Distance (q8) from a q4 offset (vx, vy) to the segment 0..(dx, dy) whose
 * length is len (q4, hd__isqrt of its squared length), or lim when it is not
 * below lim; squared compares, no root or division, settle the far pixels. */
static __attribute__((noinline)) int32_t bw_seg_near(int32_t vx, int32_t vy, int32_t dx, int32_t dy,
                                                     int32_t len, int32_t lim)
{
    int32_t dot = vx * dx + vy * dy, e;
    if (dot <= 0 || dot >= dx * dx + dy * dy) {
        if (dot > 0) { vx -= dx; vy -= dy; }
        return hd__far(vx, vy, lim) ? lim : hd__len(vx, vy);
    }
    e = vx * dy - vy * dx;
    if (e < 0) e = -e;
    return e * 16 >= lim * len ? lim : e * 16 / len;
}
/* The same without a known length, or 2048 (8 px, beyond any stroke) when
 * at least that far. Strokes ask about one segment for many pixels, so its
 * length is kept from the previous call. */
static __attribute__((noinline)) int32_t bw_seg_dist(int32_t vx, int32_t vy, int32_t dx, int32_t dy)
{
    static int32_t len2_was = -1, len_was;
    int32_t len2 = dx * dx + dy * dy;
    if (len2 != len2_was) { len2_was = len2; len_was = (int32_t)hd__isqrt((uint32_t)len2); }
    return bw_seg_near(vx, vy, dx, dy, len_was, 2048);
}
/* Round-capped stroke, q8 ends. Callers draw polylines as chains of these;
 * where a segment starts at the end of the one drawn just before it (same
 * colour and width), the two are composited as a union, so the shared cap
 * is not blended twice and chains come out seamless instead of beaded. The
 * remembered segment only ever lowers coverage at that joint, so a stale
 * match from an earlier strip cannot add or move anything. */
static struct { int32_t x0, y0, x1, y1; uint32_t w; uint16_t c; } bw_last;
static __attribute__((noinline)) void bw_qline(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t width, uint16_t color)
{
    uint32_t w = bw_hd_width(width);
    int32_t hw = (int32_t)(w >> 1), ax = hd__q4(x0), ay = hd__q4(y0);
    int32_t dx = hd__q4(x1) - ax, dy = hd__q4(y1) - ay, b[4], x, y;
    int32_t chain = bw_last.w == w && bw_last.c == color && bw_last.x1 == x0 && bw_last.y1 == y0;
    int32_t px = hd__q4(bw_last.x0), py = hd__q4(bw_last.y0);
    int32_t pdx = hd__q4(bw_last.x1) - px, pdy = hd__q4(bw_last.y1) - py;
    bw_last.x0 = x0; bw_last.y0 = y0; bw_last.x1 = x1; bw_last.y1 = y1; bw_last.w = w; bw_last.c = color;
    b[0] = ((x0 < x1 ? x0 : x1) - hw - 256) >> 8; b[2] = ((x0 < x1 ? x1 : x0) + hw + 256) >> 8;
    b[1] = ((y0 < y1 ? y0 : y1) - hw - 256) >> 8; b[3] = ((y0 < y1 ? y1 : y0) + hw + 256) >> 8;
    if (!hd__clip(b)) return;
    for (y = b[1]; y <= b[3]; ++y)
        for (x = b[0]; x <= b[2]; ++x) {
            int32_t a = hw - bw_seg_dist(x * 16 + 8 - ax, y * 16 + 8 - ay, dx, dy) + 128, p;
            if (a <= 0) continue;
            if (a > 255) a = 255;
            if (chain && (p = hw - bw_seg_dist(x * 16 + 8 - px, y * 16 + 8 - py, pdx, pdy) + 128) > 0) {
                if (p >= a) continue;
                a = (a - p) * 255 / (255 - p);
            }
            hd__px(x, y, color, a);
        }
}
static __attribute__((noinline)) void bw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    uint32_t width, uint16_t color)
{
    bw_qline(bw_q8(x0), bw_q8(y0), bw_q8(x1), bw_q8(y1), width, color);
}
/* br_shape_circle twin: q8 centre and radius; fill gives a disc (at least
 * 1.5 px, like the cell version), otherwise a ring of the given width. */
static __attribute__((noinline)) void bw_scircle(int32_t cx, int32_t cy, uint32_t radius, uint32_t width,
                       uint16_t color, uint32_t fill)
{
    if (fill) hd_circle_q8(cx, cy, radius < 384u ? 384 : (int32_t)radius, 0, color);
    else if (width) hd_circle_q8(cx, cy, (int32_t)radius, (int32_t)bw_hd_width(width), color);
}
static __attribute__((noinline)) void bw_circle(int32_t cx, int32_t cy, int32_t r, uint16_t color, uint32_t fill)
{
    bw_scircle(bw_q8(cx), bw_q8(cy), (uint32_t)(r > 0 ? r : 1) * 256u, 384u, color, fill);
}
static __attribute__((noinline)) void bw_point(int32_t cx, int32_t cy, uint32_t radius, uint32_t phase,
                     int32_t *px, int32_t *py)
{
    *px = cx + bw_sine(phase) * (int32_t)radius / 16384;
    *py = cy - bw_sine(phase + 16384u) * (int32_t)radius / 16384;
}
static __attribute__((noinline)) void bw_arc(int32_t cx, int32_t cy, uint32_t radius, uint32_t width,
                   uint32_t start, uint32_t sweep, uint16_t color)
{
    hd_arc(cx, cy, radius, bw_hd_width(width), start, sweep, color);
}
/* Polyline through n q8 points. Every pixel takes its distance to the
 * nearest segment, so joins are round and no pixel is blended twice (a chain
 * of separate strokes would bead where the anti-aliased caps overlap).
 * Row by row, each segment only claims the columns its stroke can reach on
 * that row (the part of the segment within reach of the row, widened by the
 * reach), so the work follows the path, not its bounding box. */
#define BW_PATH_MAX 36
static __attribute__((noinline)) void bw_path(const int32_t *px, const int32_t *py, uint32_t n,
                                              uint32_t width, uint16_t color)
{
    int32_t hw = (int32_t)(width >> 1), m = (hw + 128) / 16 + 2, b[4], x, y;   /* m: reach in q4, with margin */
    int16_t lo[BW_PATH_MAX], hi[BW_PATH_MAX];
    uint16_t len[BW_PATH_MAX];
    int32_t qx[BW_PATH_MAX], qy[BW_PATH_MAX];
    uint32_t i;
    if (n < 2u) return;
    if (n > BW_PATH_MAX) n = BW_PATH_MAX;
    b[0] = b[2] = px[0]; b[1] = b[3] = py[0];
    for (i = 1; i < n; ++i) {
        if (px[i] < b[0]) b[0] = px[i];
        if (px[i] > b[2]) b[2] = px[i];
        if (py[i] < b[1]) b[1] = py[i];
        if (py[i] > b[3]) b[3] = py[i];
    }
    b[0] = (b[0] - hw - 256) >> 8; b[1] = (b[1] - hw - 256) >> 8;
    b[2] = (b[2] + hw + 256) >> 8; b[3] = (b[3] + hw + 256) >> 8;
    if (!hd__clip(b)) return;                    /* (a strip this path misses costs nothing more) */
    for (i = 0; i < n; ++i) { qx[i] = hd__q4(px[i]); qy[i] = hd__q4(py[i]); len[i] = 0xffffu; }
    /* (segment lengths, q4 as bw_seg_dist takes them, are worked out for the segments a pixel asks about) */
    for (y = b[1]; y <= b[3]; ++y) {
        int32_t ry = y * 16 + 8, x0 = b[2] + 1, x1 = b[0] - 1, sorted = 1;
        uint32_t first = n, last = 0;
        for (i = 0; i + 1u < n; ++i) {
            int32_t ax = qx[i], ay = qy[i], ex = qx[i + 1u], ey = qy[i + 1u];
            int32_t ylo = ay < ey ? ay : ey, yhi = ay < ey ? ey : ay, xa = ax, xb = ex, t;
            lo[i] = hi[i] = -32768;                  /* not on this row */
            if (ry < ylo - m || ry > yhi + m) continue;
            if (ay != ey) {                 /* the segment's x where its y is within reach of the row */
                int32_t ya = ry - m < ylo ? ylo : ry - m, yb = ry + m > yhi ? yhi : ry + m;
                xa = ax + (ya - ay) * (ex - ax) / (ey - ay);
                xb = ax + (yb - ay) * (ex - ax) / (ey - ay);
            }
            if (xa > xb) { t = xa; xa = xb; xb = t; }
            xa = ((xa - m - 8) >> 4) - 1; xb = ((xb + m - 8) >> 4) + 1;
            if (xa < b[0]) xa = b[0];
            if (xb > b[2]) xb = b[2];
            if (xa > xb) continue;
            if (first < n && xa < lo[last]) sorted = 0;   /* sorted: the path runs left to right here */
            lo[i] = (int16_t)xa; hi[i] = (int16_t)xb;
            if (first == n) first = i;
            last = i;
            if (xa < x0) x0 = xa;
            if (xb > x1) x1 = xb;
        }
        for (x = x0; x <= x1; ++x) {
            int32_t d = hw + 128;                   /* bw_seg_dist of the nearest segment, if below this */
            while (first < last && hi[first] < x) ++first;     /* done with: x only grows */
            for (i = first; i <= last; ++i) {
                int32_t ax, ay;
                if (x < lo[i]) { if (sorted) break; continue; }
                if (x > hi[i]) continue;
                ax = qx[i]; ay = qy[i];
                if (len[i] == 0xffffu)
                    len[i] = (uint16_t)hd__isqrt((uint32_t)((qx[i + 1u] - ax) * (qx[i + 1u] - ax) + (qy[i + 1u] - ay) * (qy[i + 1u] - ay)));
                d = bw_seg_near(x * 16 + 8 - ax, ry - ay, qx[i + 1u] - ax, qy[i + 1u] - ay, len[i], d);
            }
            if (hw - d + 128 > 0) hd__px(x, y, color, hw - d + 128);
        }
    }
}
/* Wash between a polyline whose x rises (q8 points) and a second edge: the
 * row base (q8) or, when by is given, a second polyline sampled at the same
 * x. Coverage runs from a0 at the curve to a1 at the other edge (stepped
 * down each column exactly as the division a0 + (a1 - a0) * u / span). */
static __attribute__((noinline)) void bw_area(const int32_t *px, const int32_t *py, const int32_t *by,
                                              int32_t base, uint32_t n, uint16_t color,
                                              int32_t a0, int32_t a1)
{
    int32_t top = -cv_oy, bottom = top + (int32_t)cv_h - 1, k = a1 - a0 < 0 ? a0 - a1 : a1 - a0;
    for (uint32_t i = 0; i + 1u < n; ++i) {
        int32_t x0 = px[i], x1 = px[i + 1u], x = (x0 + 127) >> 8;
        int32_t ylo = py[i] < py[i + 1u] ? py[i] : py[i + 1u], yhi = py[i] + py[i + 1u] - ylo;
        int32_t elo = by ? (by[i] < by[i + 1u] ? by[i] : by[i + 1u]) : base, ehi = by ? by[i] + by[i + 1u] - elo : base;
        if (x1 <= x0) continue;
        /* (c and e stay between their ends: a segment whose wash lies wholly above or below the strip
         * draws nothing in it) */
        if (((ylo < elo ? ylo : elo) - 128) >> 8 > bottom || ((yhi > ehi ? yhi : ehi) + 127) >> 8 < top) continue;
        for (; x * 256 + 128 < x1; ++x) {
            int32_t t, c, e, span, dir, r0, r1, r, step, q, rem, dq, dr;
            if ((uint32_t)(x - cv_ox) >= cv_w) continue;              /* (a column off the canvas) */
            t = x * 256 + 128 - x0; c = py[i] + t * (py[i + 1u] - py[i]) / (x1 - x0);
            e = by ? by[i] + t * (by[i + 1u] - by[i]) / (x1 - x0) : base;
            span = e - c; dir = span < 0 ? -1 : 1;
            if (!span) continue;
            r0 = ((c < e ? c : e) + 127) >> 8; r1 = ((c < e ? e : c) - 128) >> 8;
            if (r0 < top) r0 = top;
            if (r1 > bottom) r1 = bottom;
            if (r0 > r1) continue;
            /* u = |row centre - c| grows by 256 a row walking away from the curve; q = k u / |span| */
            span *= dir; r = dir > 0 ? r0 : r1; step = dir;
            q = k * ((r * 256 + 128 - c) * dir); rem = q % span; q /= span;
            dq = k * 256 / span; dr = k * 256 % span;
            for (int32_t left = r1 - r0; left >= 0; --left, r += step) {
                int32_t a = a1 < a0 ? a0 - q : a0 + q;
                if (a > 0) hd__px(x, r, color, a);
                q += dq; rem += dr;
                if (rem >= span) { rem -= span; ++q; }
            }
        }
    }
}
static void bw_quadratic(int32_t x0, int32_t y0, int32_t cx, int32_t cy,
                         int32_t x1, int32_t y1, uint32_t width, uint16_t color)
{
    int32_t px[17], py[17];
    for (int32_t i = 0; i <= 16; ++i) {
        int32_t a = 16 - i;
        px[i] = (a * a * x0 + 2 * a * i * cx + i * i * x1) / 256;
        py[i] = (a * a * y0 + 2 * a * i * cy + i * i * y1) / 256;
    }
    bw_path(px, py, 17u, bw_hd_width(width), color);
}
static __attribute__((noinline)) void bw_face(const int32_t px[4], const int32_t py[4], uint16_t color)
{
    hd_poly4(px, py, color);
}
/* Recessed groove: dark top lip, darker body brightening toward a lit base. */
static __attribute__((noinline)) void bw_well(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)
{
    hd__rrect(x, y, w, h, r, RGB(20, 24, 25), RGB(46, 53, 55), RGB(8, 10, 10), RGB(72, 82, 85), 0);
}
/* Arc with round ends (q8 geometry, HD width), for dials and meters. */
static __attribute__((noinline)) void bw_round_arc(int32_t cx, int32_t cy, uint32_t radius, uint32_t width,
                                                   uint32_t start, uint32_t sweep, uint16_t color)
{
    int32_t ax, ay;
    if (!sweep) return;
    hd_arc(cx, cy, radius, width, start, sweep, color);
    bw_point(cx, cy, radius, start, &ax, &ay);
    hd_circle_q8(ax, ay, (int32_t)width / 2, 0, color);
    bw_point(cx, cy, radius, start + sweep, &ax, &ay);
    hd_circle_q8(ax, ay, (int32_t)width / 2, 0, color);
}
/* Shaded knob cap centred on a pixel centre (q8), radius in q8: a lit rim at
 * the top, a face that falls from mid grey to near black. */
static __attribute__((noinline)) void bw_knob_cap(int32_t cx, int32_t cy, int32_t radius)
{
    int32_t r = radius >> 8, x0 = (cx >> 8) - r, y0 = (cy >> 8) - r, w = 2 * r + 1;
    hd_circle_q8(cx, cy + 192, radius, 0, RGB(0, 0, 0));                    /* contact shadow */
    hd_rrect_grad(x0, y0, w, w, r, RGB(92, 102, 106), RGB(14, 16, 17));       /* rim */
    hd_rrect_grad(x0 + 1, y0 + 1, w - 2, w - 2, r - 1, RGB(48, 55, 58), RGB(16, 19, 20));
}
/* Knob pointer from the cap's centre region toward the rim. */
static void bw_knob_pointer(int32_t cx, int32_t cy, uint32_t s, uint32_t at)
{
    int32_t ax, ay, bx, by;
    bw_point(cx, cy, s * 24u, at, &ax, &ay);
    bw_point(cx, cy, s * 57u, at, &bx, &by);
    hd_line_q8(ax, ay, bx, by, 512u, C_WHITE);
}
/* Polish pass: a shaded ball on a pixel centre (radius in px): lit top, dark
 * base and a small specular spot, like the toggle knob. */
static __attribute__((noinline)) void hdp_ball(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    hd_rrect_grad(cx - r, cy - r, 2 * r + 1, 2 * r + 1, r, hd_lighten(c, 120u), hd_darken(c, 90u));
    hd_circle_q8(bw_q8(cx) - r * 72, bw_q8(cy) - r * 72, r * 72, 0, hd_lighten(c, 190u));
}
/* A soft halo in a toned-down colour, under lit points. */
static __attribute__((noinline)) void hdp_halo(int32_t cx, int32_t cy, int32_t r, uint32_t tone)
{
    hd_glow_dot(cx, cy, r, bw_tone(br_ui_accent, tone));
}
/* Faint wash under a curve of q8 points down to a q8 base, then the curve. */
static __attribute__((noinline)) void hdp_curve(const int32_t *px, const int32_t *py, uint32_t n,
                                                int32_t base, uint32_t width, uint16_t color)
{
    bw_area(px, py, 0, base, n, color, 80, 6);
    bw_path(px, py, n, width, color);
}
/* Widget-local stand-ins for the shared cell helpers. */
static void bw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    cv_rect(x, y, w, h, color);
}

#include "james_font.h"
#include "gfx_hd_text.h"

static int32_t br_widget_text_width(const char *text)
{
    return hd_text_width(text);              /* proportional: what br_widget_text draws */
}

/* Width of text drawn at `scale`: HD advances do not scale linearly, so
 * callers that draw at scale 2 or 3 measure with this, not width * scale. */
static int32_t br_widget_text_width_scaled(const char *text, int32_t scale)
{
    return hd_text_width_scaled(text, scale);
}
/* Where to draw text so its right edge stays where the pixel font put it
 * when drawn at x (labels placed by a fixed x rather than by a measurement). */
static int32_t br_widget_text_reright(int32_t x, const char *text, int32_t scale)
{
    int32_t n = 0;
    if (!text || !*text) return x;
    for (const char *t = text; *t; ++t) n += (int32_t)james_font_width((uint8_t)*t) + 1;
    return x + (n - 1) * scale - hd_text_width_scaled(text, scale);
}
/* Same, keeping its centre instead. */
static int32_t br_widget_text_recentre(int32_t x, const char *text, int32_t scale)
{
    return x + (br_widget_text_reright(x, text, scale) - x) / 2;
}
static void br_widget_text_scaled(int32_t x, int32_t y, const char *text, uint16_t color, int32_t scale)
{
    if (!text) return;
    hd_text(x, y, text, color, scale);       /* proportional advances, the pixel font's rows */
    return;
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
static void br_widget_text(int32_t x, int32_t y, const char *text, uint16_t color)
{
    br_widget_text_scaled(x, y, text, color, 1);
}
static void br_widget_text_2x(int32_t x, int32_t y, const char *text, uint16_t color)
{
    br_widget_text_scaled(x, y, text, color, 2);
}

static void bw_pot(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t cx = bw_q8(x) + (int32_t)s * 128, cy = bw_q8(y) + (int32_t)s * 128;
    uint32_t radius = s * 95u, start = 40960u, sweep = q * 49152u / 127u;
    bw_round_arc(cx, cy, radius, 640u, start, 49152u, BR_LINE);
    bw_round_arc(cx, cy, radius, 640u, start, sweep, BR_ACC);
    bw_knob_cap(cx, cy, (int32_t)s * 66);
    bw_knob_pointer(cx, cy, s, start + sweep);
}

/* Compressor source: an off symbol, the four track colours for ALL, or one track's own badge
 * (T1..T3, and DK for the kick drum). */
static void bw_comp_source(int32_t x, int32_t y, uint32_t s, uint32_t source)
{
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    if (source == BR_COMP_OFF) {
        hd_rrect_grad(cx - 11, cy - 11, 23, 23, 11, RGB(4, 5, 6), RGB(30, 35, 37));   /* a recessed socket */
        bw_circle(cx, cy, 11, BR_DIM, 0u);
        bw_line(cx - 8, cy + 8, cx + 8, cy - 8, 640u, BR_DIM);
    } else if (source == BR_COMP_ALL) {
        int32_t x0 = (cx - 14) & ~1, y0 = (cy - 10) & ~1;
        hd_bevel(x0, y0, 12, 8, 2, br_track_color(0u));
        hd_bevel(x0 + 16, y0, 12, 8, 2, br_track_color(1u));
        hd_bevel(x0, y0 + 12, 12, 8, 2, br_track_color(2u));
        hd_bevel(x0 + 16, y0 + 12, 12, 8, 2, br_track_color(BR_DRUM_PART));
    } else {
        br_track_badge((cx - 15) & ~1, (cy - 10) & ~1,
                       source >= BR_COMP_DRUM_1 ? BR_DRUM_PART : source - BR_COMP_TRACK_1, 0u);
    }
}

/* phase 0 is stopped; 1–16 are quantized positions within the beat. */
static void bw_bpm(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    int32_t top = cy - (int32_t)s / 5, base = cy + (int32_t)s / 5;
    int32_t half = (int32_t)s * 2 / 15, ticker = 0;
    int32_t bx[4] = {cx - half, cx - 4, cx + 4, cx + half}, by[4] = {base, top, top, base};
    uint32_t radius = s * 95u;
    const uint32_t start = 38229u, sweep = 54613u; /* 7 o'clock to 5 o'clock */
    if (cv_h) {                         /* (an empty strip only asks where the ticker is: br_anim_marks) */
        bw_round_arc(bw_q8(cx), bw_q8(cy), radius, 640u, start, sweep, BR_LINE);
        bw_round_arc(bw_q8(cx), bw_q8(cy), radius, 640u, start, q * sweep / 127u, BR_ACC);
        /* The metronome: a shaded case behind a crisp outline. */
        bw_area(bx, by, 0, bw_q8(base), 4u, BR_ACC, 40, 120);
        bx[0] = bw_q8(bx[0]); bx[1] = bw_q8(bx[1]); bx[2] = bw_q8(bx[2]); bx[3] = bw_q8(bx[3]);
        by[0] = by[3] = bw_q8(base); by[1] = by[2] = bw_q8(top);
        int32_t ox[5] = {bx[0], bx[1], bx[2], bx[3], bx[0]}, oy[5] = {by[0], by[1], by[2], by[3], by[0]};
        bw_path(ox, oy, 5u, 384u, BR_ACC);
    }
    if (phase) {
        uint32_t encoded = phase - 1u;
        ticker = br_metronome_swing((encoded & 15u) * 8u, encoded >> 4) * 4 / 32;
    }
    BW_MARK(bw_line(cx, base - 4, cx + ticker, top + 4, 512u, C_WHITE);   /* the ticker swings: a mark */
            hd_rrect(cx + ticker / 2 - 2, cy - 6, 5, 4, 1, C_WHITE));
}

static void bw_fader(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t cx = x + (int32_t)s / 2, top = y + (int32_t)s / 6, bottom = y + (int32_t)s * 5 / 6;
    int32_t handle = bottom - ((int32_t)q * (bottom - top) + 63) / 127;
    int32_t cap_w = 2 * ((int32_t)s / 7), cap_h = s >= 48u ? 6 : 4;
    for (int32_t i = 0; i <= 2; ++i) {
        int32_t ty = top + i * (bottom - top) / 2;
        cv_rect(cx - (int32_t)s / 5, ty, 4, 1, BR_LINE);
        cv_rect(cx + (int32_t)s / 5 - 3, ty, 4, 1, BR_LINE);
    }
    bw_well(cx - 2, top - 1, 4, bottom - top + 3, 2);
    if (q) hd_rrect_grad(cx - 1, handle, 2, bottom - handle + 1, 1, hd_lighten(BR_ACC, 60u), BR_ACC);
    hd_rrect(cx - cap_w / 2, handle - cap_h / 2 + 1, cap_w, cap_h, 2, RGB(0, 0, 0));
    hd_bevel(cx - cap_w / 2, handle - cap_h / 2, cap_w, cap_h, 2, RGB(208, 214, 216));
    cv_rect(cx - cap_w / 2 + 2, handle, cap_w - 4, 1, RGB(90, 98, 100));
}

static void bw_toggle(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t radius = (int32_t)s / 6, cy = y + (int32_t)s / 2;
    int32_t left = x + (int32_t)s / 4, right = x + (int32_t)s * 3 / 4;
    int32_t cx = left + (int32_t)q * (right - left) / 127, knob = radius > 2 ? radius - 2 : 1;
    int32_t tx = left - radius, ty = cy - radius, tw = right - left + 2 * radius + 1, th = 2 * radius + 1;
    if (q) hd__rrect(tx, ty, tw, th, radius, bw_tone(BR_ACC, 120u), bw_tone(BR_ACC, 190u),
                     bw_tone(BR_ACC, 70u), bw_tone(BR_ACC, 225u), 0);
    else bw_well(tx, ty, tw, th, radius);
    hd_circle_q8(bw_q8(cx), bw_q8(cy) + 256, knob * 256 + 64, 0, RGB(0, 0, 0));     /* shadow */
    hd_rrect_grad(cx - knob, cy - knob, 2 * knob + 1, 2 * knob + 1, knob, C_WHITE, RGB(176, 184, 188));
}

/* The CPU monitor switch: one meter bar in the colour of the screen overlay (cyan), full colour when on and
 * grey when off. */
static void bw_monitor(int32_t x, int32_t y, uint32_t s, uint32_t on)
{
    int32_t left = (x + (int32_t)s / 2 - 17) & ~1, top = (y + (int32_t)s / 2 - 8) & ~1;
    bw_well(left, top, 34, 16, 3);
    hd_bevel(left, top, on ? 26 : 6, 16, 3, on == 2u ? RGB(240, 60, 48) : on == 3u ? BR_ACC : on ? RGB(0, 200, 230) : RGB(70, 80, 84));   /* 3: the page's accent (KEY TRACK) */
}

static void bw_reverb(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    int32_t min_radius = (int32_t)s * 15 / 100;
    int32_t max_radius = (int32_t)s * 35 / 100;
    int32_t radius, tx[4], ty[4], lx[4], ly[4], rx[4], ry[4];

    /* SIZE is shown as a single isometric cube whose footprint grows with
     * the control, keeping the picture tied directly to the parameter. */
    if (min_radius < 2) min_radius = 2;
    if (max_radius < min_radius) max_radius = min_radius;
    radius = min_radius + (int32_t)q * (max_radius - min_radius) / 127;
    tx[0] = cx;          ty[0] = cy - radius;
    tx[1] = cx - radius; ty[1] = cy - radius / 2;
    tx[2] = cx;          ty[2] = cy;
    tx[3] = cx + radius; ty[3] = cy - radius / 2;
    lx[0] = cx - radius; ly[0] = cy - radius / 2;
    lx[1] = cx;          ly[1] = cy;
    lx[2] = cx;          ly[2] = cy + radius;
    lx[3] = cx - radius; ly[3] = cy + radius / 2;
    rx[0] = cx;          ry[0] = cy;
    rx[1] = cx + radius; ry[1] = cy - radius / 2;
    rx[2] = cx + radius; ry[2] = cy + radius / 2;
    rx[3] = cx;          ry[3] = cy + radius;

    bw_face(tx, ty, bw_tone(BR_ACC, 220u));
    bw_face(lx, ly, bw_tone(BR_ACC, 145u));
    bw_face(rx, ry, bw_tone(BR_ACC, 95u));
    {   /* A sheen down the lid; the sides darken toward the floor. */
        int32_t ax[3] = {bw_q8(cx - radius), bw_q8(cx), bw_q8(cx + radius)};
        int32_t ay[3] = {bw_q8(cy - radius / 2), bw_q8(cy - radius), bw_q8(cy - radius / 2)};
        int32_t ab[3] = {ay[0], bw_q8(cy), ay[2]}, ac[3] = {bw_q8(cy + radius / 2), bw_q8(cy + radius), bw_q8(cy + radius / 2)};
        bw_area(ax, ay, ab, 0, 3u, C_WHITE, 110, 0);
        bw_area(ax, ab, ac, 0, 3u, C_BLACK, 0, 130);
    }
    bw_line(tx[0], ty[0], tx[1], ty[1], 512u, C_WHITE);
    bw_line(tx[0], ty[0], tx[3], ty[3], 512u, C_WHITE);
    bw_line(tx[1], ty[1], tx[2], ty[2], 512u, BR_ACC);
    bw_line(tx[3], ty[3], tx[2], ty[2], 512u, BR_ACC);
    bw_line(lx[0], ly[0], lx[3], ly[3], 512u, BR_ACC);
    bw_line(lx[3], ly[3], lx[2], ly[2], 512u, BR_ACC);
    bw_line(rx[1], ry[1], rx[2], ry[2], 512u, BR_DIM);
    bw_line(rx[2], ry[2], rx[3], ry[3], 512u, BR_ACC);
    bw_line(tx[2], ty[2], lx[2], ly[2], 512u, BR_ACC);
}

/* Animation phase (0..127) as drawn. The card hash (br_draw_body) uses the
 * same value, so the phase drawn is always the phase hashed. Quantising the
 * phase here (about one pixel of motion per step) was measured and saved
 * nothing once paths were made cheap, while making the motion visibly
 * coarser, so the phase passes through unchanged. Kept as the single place
 * to coarsen animation if device timing ever needs it. */
static uint32_t bw_phase_step(uint32_t kind, uint32_t phase)
{
    (void)kind;
    return phase;
}
static void bw_delay(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase,
                     uint32_t note_index)
{
    int32_t cx = bw_q8(x) + (int32_t)s * 128, cy = bw_q8(y) + (int32_t)s * 128;
    uint32_t maxr = s * 103u, base = s * 28u;
    int32_t head_x = x + (int32_t)s / 2 - (int32_t)s / 20;
    int32_t head_y = y + (int32_t)s * 3 / 5;
    int32_t radius = s >= 48u ? 5 : 2;
    int32_t stem_top = y + (int32_t)s / 6;
    int32_t stem_x = head_x + radius - 1;
    /* The note value determines flags, dot and an open head. Triplets use
     * a small 3; one bar uses a whole note, with no stem. */
    static const char *const label[16] = {
        "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8",
        "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 BAR"
    };
    static const uint8_t glyph[16] = {
        3u, 3u, 2u, 3u | 4u, 2u, 1u, 2u | 4u, 1u,
        0u, 1u | 4u, 0u, 8u, 4u, 8u, 8u | 4u, 8u
    };
    uint32_t flags, open, triplet;
    note_index = note_index < 16u ? note_index : 15u;
    flags = glyph[note_index] & 3u;
    open = glyph[note_index] >> 3;
    triplet = note_index == 0u || note_index == 2u || note_index == 5u || note_index == 8u || note_index == 11u;
    phase = bw_phase_step(BR_WIDGET_DELAY, phase);
    hdp_halo(x + (int32_t)s / 2, y + (int32_t)s / 2, (int32_t)s / 5, 70u + q / 2u);
    for (int32_t i = 2; i >= 0; --i) {
        uint32_t at = (phase + (uint32_t)i * 43u) & 127u;
        uint32_t radius = base + at * (maxr - base) / 127u;
        uint32_t shade = (255u - at * 160u / 127u) * (100u + q) / 227u;
        bw_scircle(cx, cy, radius, bw_weight(s), bw_tone(BR_ACC, shade), 0);
    }
    bw_circle(head_x, head_y, radius, C_WHITE, open ? 0u : 1u);
    if (note_index != 15u)
        bw_line(stem_x, head_y, stem_x, stem_top, s >= 48u ? 512u : 256u, C_WHITE);
    for (uint32_t f = 0; f < flags; ++f)
        bw_line(stem_x, stem_top + (int32_t)f * (int32_t)s / 14,
                stem_x + (int32_t)s / 6, stem_top + (int32_t)f * (int32_t)s / 14 + (int32_t)s / 10,
                s >= 48u ? 384u : 256u, C_WHITE);
    if (triplet && s >= 48u) br_widget_text(stem_x + 5, stem_top - 8, "3", C_WHITE);
    if (glyph[note_index] & 4u)
        bw_circle(head_x + radius + 4, head_y, 1, C_WHITE, 1u);
    if (s >= 48u) {
        int32_t width = br_widget_text_width(label[note_index]);
        br_widget_text(x + ((int32_t)s - width) / 2,
                       y + (int32_t)s * 3 / 4, label[note_index], C_WHITE);
    }
}

/* OSC MIX (the 1.0a picture): two interwoven strands converge at the output.
 * Their relative brightness follows the mix (the brighter on top at each
 * crossing); the knob shifts both strands' phase a little and places the
 * glints. Still: no phase, nothing marked. */
static void bw_mix(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t left = (x + (int32_t)s / 6) & ~1;
    int32_t columns = (((x + (int32_t)s * 5 / 6) & ~1) - left) / 2;   /* 5..21 at sizes 16..64 */
    int32_t cy = (((y + (int32_t)s / 2) & ~1) + 1) * 256;
    int32_t amplitude = (int32_t)s / 12;
    int32_t px[BW_PATH_MAX], py[BW_PATH_MAX], off[BW_PATH_MAX];
    for (int32_t i = 0; i <= columns; ++i) {
        int32_t wave = bw_sine(16384u + (uint32_t)i * 81920u / (uint32_t)columns + q * 64u);
        off[i] = wave * amplitude * (columns - i) / columns / 32;       /* q8, two px per unit */
        px[i] = (left + i * 2 + 1) * 256;
    }
    for (int32_t pass = 0, sg = q >= 64u ? 1 : -1; pass < 2; ++pass, sg = -sg) {   /* sg 1: the lower strand */
        for (int32_t i = 0; i <= columns; ++i) py[i] = cy + sg * off[i];
        bw_path(px, py, (uint32_t)columns + 1u, 448u, bw_tone(BR_ACC, 90u + (sg > 0 ? 127u - q : q) * 165u / 127u));
    }
    for (uint32_t spark = 0; spark < (s >= 48u ? 3u : 2u); ++spark) {
        int32_t i = 2 + (int32_t)((q + spark * 7u) % (uint32_t)(columns - 2));
        static const uint8_t twinkle[8] = {255, 195, 125, 125, 125, 125, 125, 195};
        uint16_t color = bw_tone(bw_highlight(1u), twinkle[(q + spark * 3u) & 7u]);
        for (int32_t sg = -1; sg <= 1; sg += 2) hd__radial(px[i], cy + sg * off[i], 640, -1, 0u, 0u, color);
    }
}

/* Intensity/amount reservoir: a one-pixel background gutter separates the
 * fill from each wall. The animated highlight stays inside the fill. */
static void bw_amount(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    int32_t cx = x + (int32_t)s / 2, hw = ((int32_t)s / 6 + 2) & ~1;
    int32_t top = (y + (int32_t)s / 6) & ~1, bottom = (y + (int32_t)s * 5 / 6) & ~1;
    int32_t in_left = cx - hw + 3, in_w = 2 * hw - 6, in_top = top + 2, in_bottom = bottom - 2;
    int32_t in_h = in_bottom - in_top, fill = (int32_t)q * in_h / 127, fill_top = in_bottom - fill;
    for (int32_t i = 1; i < 4; ++i) {
        int32_t mark = (bottom - (bottom - top) * i / 4) & ~1;
        cv_rect(cx - hw - 4, mark, 3, 1, BR_DIM);                     /* scale, both sides */
        cv_rect(cx + hw + 1, mark, 3, 1, BR_DIM);
    }
    hd_rrect(cx - hw, top, 2 * hw, bottom - top, 4, BR_LINE);             /* the vessel */
    hd__rrect(cx - hw + 2, top + 2, 2 * hw - 4, bottom - top - 4, 2,
              RGB(6, 7, 8), RGB(16, 19, 20), RGB(2, 2, 3), RGB(26, 30, 31), 0);   /* empty glass */
    if (fill > 1 && in_w > 0) hd_bevel(in_left, fill_top, in_w, fill, 1, BR_ACC);
    if (phase && q && fill > 3) {
        int32_t gw = s >= 48u ? 8 : 4;
        if (gw > in_w - 2) gw = in_w - 2;
        int32_t gx = in_left + (in_w - gw) / 2;
        int32_t travel = fill - 3;
        int32_t gy = in_bottom - 3 - (int32_t)(phase & 31u) * travel / 31;
        if (gw > 0) BW_MARK(hd_rrect(gx, gy, gw, 2, 1, hd_lighten(BR_ACC, 170u)));
    }
}

/* A labeled stereo rail makes the position and direction unambiguous. */
static void bw_pan_rail(int32_t x, int32_t y, uint32_t s, uint32_t q, uint16_t color)
{
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t center = x + (int32_t)s / 2;
    int32_t cy = y + (int32_t)s / 2;
    int32_t puck = (left + ((int32_t)q * (right - left) + 63) / 127) & ~1;
    br_widget_text(br_widget_text_recentre(left - 2, "L", 1), y + (int32_t)s / 5, "L", BR_DIM);   /* over the rail's ends */
    br_widget_text(br_widget_text_recentre(right - 2, "R", 1), y + (int32_t)s / 5, "R", BR_DIM);
    bw_well(left - 2, cy - 3, right - left + 4, 6, 3);
    hd_rrect(center - 1, cy - 8, 2, 16, 1, BR_DIM);      /* two pixels wide, centred: neither side looks favoured */
    if (puck != center)
        hd_rrect(puck < center ? puck : center, cy - 2, puck < center ? center - puck : puck - center, 4, 2, color);
    hd_rrect(puck - 6, cy - 7, 12, 16, 4, RGB(0, 0, 0));                /* shadow */
    hd_bevel(puck - 6, cy - 8, 12, 16, 4, color);
    hd_rrect_grad(puck - 4, cy - 6, 8, 12, 3, C_WHITE, RGB(190, 196, 200));
}
static void bw_track_pan(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t track)
{
    bw_pan_rail(x, y, s, q, br_track_color(track));
}

static void bw_heat(int32_t x, int32_t y, uint32_t s, uint32_t type, uint32_t phase)
{
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2, px[11], py[11];
    if (type == BR_HEAT_BIT) {
        /* BIT: the staircase over a wash, the step now sounding lit white. */
        for (int32_t i = 0; i < 4; ++i) {
            px[2 * i] = bw_q8(left + (right - left) * i / 4);
            px[2 * i + 1] = bw_q8(left + (right - left) * (i + 1) / 4);
            py[2 * i] = py[2 * i + 1] = bw_q8(cy - 12 + i * 8);
        }
        hdp_curve(px, py, 8u, bw_q8(cy + 16), 448u, BR_ACC);
        phase /= 32u;
        BW_MARK(hd_line_q8(px[2 * phase], py[2 * phase], px[2 * phase + 1], py[2 * phase], 448u, C_WHITE));
    } else if (type == BR_HEAT_SAT) {
        /* SAT: the soft-clipped curve between two limit rails, warm at its knee. */
        hd_rrect(left, cy - 15, right - left + 1, 2, 1, BR_LINE);
        hd_rrect(left, cy + 13, right - left + 1, 2, 1, BR_LINE);
        hdp_halo(cx, cy, 8, 110u);
        bw_quadratic(bw_q8(left), bw_q8(cy + 12), bw_q8(left + 12), bw_q8(cy + 12),
                     bw_q8(cx), bw_q8(cy), 512u, BR_ACC);
        bw_quadratic(bw_q8(cx), bw_q8(cy), bw_q8(right - 12), bw_q8(cy - 12),
                     bw_q8(right), bw_q8(cy - 12), 512u, C_WHITE);
    } else {
        /* WOW: a shaded tape reel above a slowly wandering pitch ribbon. */
        int32_t sy = y + (int32_t)s / 3;
        bw_knob_cap(bw_q8(cx), bw_q8(sy), 7 * 256);
        hd_circle_q8(bw_q8(cx), bw_q8(sy), 7 * 256, 384, BR_ACC);
        hdp_ball(cx, sy, 2, C_WHITE);
        for (int32_t i = 0; i <= 10; ++i) {
            px[i] = bw_q8(left) + i * (right - left) * 256 / 10;
            py[i] = bw_q8(cy + 10) + bw_sine((uint32_t)(i * 5500 + (int32_t)phase * 512)) / 16;
        }
        BW_MARK(bw_path(px, py, 11u, 448u, BR_ACC));
    }
}

static void bw_timbre(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase);
static void bw_color(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase);
static void bw_mod_atten(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t kind,
                         uint32_t phase)
{
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    int32_t marker = cx + ((int32_t)q - 64) * (int32_t)s / 190;
    /* The pan rail's parts: a groove, the lit amount from the centre and a puck. */
    bw_well(x + 7, cy - 1, (int32_t)s - 14, 4, 2);
    bw_line(cx, cy - 9, cx, cy + 9, 512u, BR_DIM);
    if (marker != cx) hd_rrect(marker < cx ? marker : cx, cy, marker < cx ? cx - marker + 1 : marker - cx + 1, 2, 1, BR_ACC);
    hd_circle_q8(bw_q8(marker), bw_q8(cy) + 256, 4 * 256 + 64, 0, C_BLACK);
    hd_rrect_grad(marker - 4, cy - 4, 9, 9, 4, C_WHITE, RGB(176, 184, 188));
    {   /* D28: the target's own HOME picture in miniature (TIMBRE ribbon, MORPH prism; PITCH a note with a
         * vibrato tail), worked harder as the depth grows either way (the rail carries the sign). */
        uint32_t m = (q > 64u ? q - 64u : 64u - q) * 2u;                 /* 0..128 */
        if (!kind) bw_timbre(x + 19, y, 22u, m, phase);
        else if (kind == 2u) bw_color(x + 15, y - 6, 32u, m, phase);
        else if (kind == 1u) {
            for (int32_t i = 0; i < 5; ++i) {
                int32_t height = 3 + (int32_t)((m + 24u) * (uint32_t)(5 - i) / 60u);
                hd_rrect(x + 19 + i * 5, y + 18 - height, 3, height, 1, BR_ACC);
            }
        } else {
            int32_t px[13], py[13], a = 128 + (int32_t)m * 7;              /* 0.5 .. 4 px, q8 */
            for (int32_t i = 0; i < 13; ++i) {
                px[i] = bw_q8(x + 30 + i);
                py[i] = bw_q8(y + 11) + a * bw_sine((uint32_t)i * 16384u + phase * 4096u) / 16384;
            }
            BW_MARK(bw_path(px, py, 13u, 384u, BR_ACC));
            hd_rrect(x + 21, y + 8, 11, 7, 3, BR_ACC);
        }
    }
    if (phase && q != 64u)
        BW_MARK(hd_glow_dot(marker + ((int32_t)(phase & 7u) - 4) * (q < 64u ? -1 : 1), cy + 11, 2, BR_ACC));
}

static void bw_adsr_graph(int32_t x, int32_t y, uint32_t w, uint32_t h,
                          const uint32_t env[4], uint32_t stage)
{
    int32_t left = x + 5, right = x + (int32_t)w - 5, top = y + 7, base = y + (int32_t)h - 7;
    int32_t p[5], py[5], a, d, release, vx[BW_PATH_MAX], vy[BW_PATH_MAX];
    uint32_t expanded = w > 64u, n = 0, from[5];
    if (!env) return;
    a = 3 + (int32_t)bw_clamp(env[0],127u) * ((int32_t)w / 4 - 3) / 127;
    d = 3 + (int32_t)bw_clamp(env[1],127u) * ((int32_t)w / 5 - 3) / 127;
    release = 3 + (int32_t)bw_clamp(env[3],127u) * ((int32_t)w / 4 - 3) / 127;
    p[0]=left; p[1]=left+a; p[2]=p[1]+d; p[3]=right-release; p[4]=right;
    if (p[2] > p[3]) { p[2]=(p[1]+right)/2; p[3]=(p[2]+right)/2; }
    py[0]=base; py[1]=top; py[2]=base-(int32_t)bw_clamp(env[2],127u)*(base-top)/127;
    py[3]=py[2]; py[4]=base;
    /* One path through all four stages: decay and release bend as quadratics
     * (12 steps each), attack and sustain stay straight. */
    for (uint32_t i=0; i<4u; ++i) {
        int32_t x0 = bw_q8(p[i]), y0 = bw_q8(py[i]), x1 = bw_q8(p[i+1u]), y1 = bw_q8(py[i+1u]);
        from[i] = n;
        if (i == 1u || i == 3u) {
            int32_t cx = bw_q8((p[i]+p[i+1u])/2);
            for (int32_t k = 0; k < 12; ++k) {
                int32_t u = 12 - k;
                vx[n] = (u*u*x0 + 2*u*k*cx + k*k*x1) / 144;
                vy[n++] = (u*u*y0 + 2*u*k*y1 + k*k*y1) / 144 - 128;
            }
        } else { vx[n] = x0; vy[n++] = y0 - (i == 2u ? 128 : 0); }
    }
    from[4] = n; vx[n] = bw_q8(right); vy[n++] = bw_q8(base);
    if (!expanded) hd_line(left, base, right, base, BR_LINE);   /* (the large chart sits in a well with its own floor) */
    bw_area(vx, vy, 0, bw_q8(base), n, BR_ACC, expanded ? 64 : 80, 6);
    for (uint32_t i=0; i<4u; ++i) {
        uint16_t color = expanded ? (i == stage ? C_WHITE : BR_ACC) : (stage >= 4u || i == stage) ? BR_ACC : BR_DIM;
        uint32_t width = expanded || stage >= 4u || i == stage ? 448u : 256u;
        bw_path(vx + from[i], vy + from[i], from[i+1u] - from[i] + 1u, width, color);
    }
}

static void bw_adsr(int32_t x, int32_t y, uint32_t s, const uint32_t env[4], uint32_t stage)
{
    bw_adsr_graph(x, y, s, s, env, stage);
}

/* Three beamed notes: the middle onset moves later as swing increases. */
static void bw_swing(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t middle = (left + right) / 2 + (int32_t)(q * s / (127u * 8u));
    int32_t beam = y + (int32_t)s / 4, bottom = y + (int32_t)s * 3 / 4;
    int32_t stems[3] = {left & ~1, middle & ~1, right & ~1};
    hd_rrect(left, beam, right - left + 2, 2, 1, BR_ACC);
    hd_rrect(left, beam + 4, right - left + 2, 2, 1, BR_ACC);
    for (uint32_t i = 0; i < 3u; ++i) {
        cv_rect(stems[i], beam, 2, bottom - beam, BR_ACC);
        bw_circle(stems[i] - 3, bottom + 1, 4, BR_ACC, 1u);
    }
}

/* A centered, widening gate pulse. Two-pixel columns and shaded edges match
 * the other Plaits HOME illustrations; width represents the hit duration. */
static void bw_gate(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t open)
{
    int32_t center = x + (int32_t)s / 2;
    int32_t cy = y + (int32_t)s / 2;
    uint32_t amount = open ? 127u : bw_clamp(q, 127u);
    uint32_t maximum = (s > 12u ? (s - 12u) / 4u : 1u);
    uint32_t half = 1u + (maximum - 1u) * amount / 127u;
    hd_line(x + 6, cy, x + (int32_t)s - 7, cy, BR_LINE);
    /* The pulse lights the rule: a glow centred between pixels center - 1 and center (q8 center * 256),
     * the bars' mirror axis, so it mirrors with them. */
    hd__radial(center * 256, bw_q8(cy), (5 + (int32_t)(half > 8u ? 8u : half)) * 256, -1, 0u, 0u,
               bw_tone(br_ui_accent, 30u + amount / 3u));
    static const uint8_t heights[12] = {14, 8, 18, 10, 16, 6, 14, 8, 12, 6, 8, 6};
    for (uint32_t i = 0u; i < half; ++i) {
        uint32_t edge = half - 1u - i;
        uint32_t motion = (amount >> 4) % 12u;
        int32_t height = heights[(i + motion) % 12u];
        uint32_t tone = edge == 0u ? 110u : edge == 1u ? 165u : (i & 1u) ? 155u : 215u;
        uint16_t color = bw_tone(BR_ACC, tone + amount * 20u / 127u);
        uint16_t light = hd_lighten(color, 56u), dark = hd_darken(color, 40u);
        int32_t yy = cy - height / 2 + 1;
        /* Each column is a small rounded bar, lit at its crest. */
        hd_rrect_grad(center - 2 - (int32_t)i * 2, yy, 2, height, 1, light, dark);
        hd_rrect_grad(center + (int32_t)i * 2, yy, 2, height, 1, light, dark);
        if (!i || ((i + motion) % 3u == 0u && edge > 0u)) {
            uint16_t glint = bw_highlight(i == 0u);
            hd_rrect_grad(center - 2 - (int32_t)i * 2, yy + 1, 2, 5, 1, glint, light);
            hd_rrect_grad(center + (int32_t)i * 2, yy + 1, 2, 5, 1, glint, light);
        }
    }
}

/* Function graphs: a 1 px rule, a faint wash between the curve and the rule
 * in the control's colour, then the smooth stroke. Points are q8. */
static __attribute__((noinline)) void bw_graph(const int32_t *px, const int32_t *py, uint32_t n,
                                               int32_t rule_y, uint16_t color)
{
    hd_line(px[0] >> 8, rule_y, px[n - 1u] >> 8, rule_y, BR_LINE);
    hdp_curve(px, py, n, bw_q8(rule_y), 448u, color);
}
static void bw_filter(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t left=bw_q8(x+5), right=bw_q8(x+(int32_t)s-5);
    int32_t top=bw_q8(y+9), base=bw_q8(y+(int32_t)s-7), px[33], py[33];
    int32_t cutoff = q < 63u ? (int32_t)q * 127 / 62 : ((int32_t)q - 65) * 127 / 62;
    for (int32_t i=0; i<=32; ++i) {
        int32_t ny;
        if (q==63u || q==64u) ny=top;
        else {
            int32_t distance=i*127/32-cutoff;
            int32_t magnitude=distance<0?-distance:distance;
            int32_t response=distance*96/(magnitude+5);
            int32_t level=(response+96)*127/192;
            if (q<63u) level=127-level;
            ny=base-level*(base-top)/127;
        }
        px[i]=left+i*(right-left)/32; py[i]=ny;
    }
    bw_graph(px, py, 33u, y+(int32_t)s-7, BR_ACC);
}

/* Fixed selector illustrations use the same response equation as the HOME
 * filter graph. Resonance adds only a peak at the same cutoff. */
static void bw_filter_target(int32_t x, int32_t y, uint32_t s, uint32_t resonance)
{
    int32_t left=bw_q8(x+5), right=bw_q8(x+(int32_t)s-5);
    int32_t top=bw_q8(y+9), base=bw_q8(y+(int32_t)s-7), px[33], py[33];
    const int32_t cutoff=35*127/62;
    for (int32_t i=0;i<=32;++i) {
        int32_t distance=i*127/32-cutoff, magnitude=distance<0?-distance:distance;
        int32_t response=distance*96/(magnitude+5);
        int32_t level=90-(response+96)*90/192;
        if (resonance) level += 32/(1+magnitude*magnitude/90);
        if (level>127) level=127;
        px[i]=left+i*(right-left)/32; py[i]=base-level*(base-top)/127;
    }
    bw_graph(px, py, 33u, y+(int32_t)s-7, BR_ACC);
}

static void bw_wave(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant, uint32_t phase)
{
    int32_t left=bw_q8(x+5), right=bw_q8(x+(int32_t)s-5), mid=bw_q8(y+(int32_t)s/2);
    int32_t amp=(int32_t)(q*s*80u/127u), px[33], py[33];
    uint32_t n = 0;
    phase = bw_phase_step(BR_WIDGET_WAVE, phase);
    if (variant==4u) {
        static const int8_t hold[8]={6,-10,12,-4,3,-13,9,-2};
        for (uint32_t i=0;i<8u;++i) {
            int32_t ny=mid-(int32_t)hold[(i+phase/16u)&7u]*amp/15;
            px[n]=left+(int32_t)i*(right-left)/8; py[n++]=ny;
            px[n]=left+(int32_t)(i+1u)*(right-left)/8; py[n++]=ny;
        }
    } else {
        for (int32_t i=0;i<=32;++i) {
            uint32_t t=((uint32_t)i*2048u+phase*512u)&65535u;
            int32_t v;
            if (variant==1u) v=t<32768u?16384-(int32_t)t: (int32_t)t-49152;
            else if (variant==2u) v=16384-(int32_t)t/2;
            else if (variant==3u) v=t<32768u?16384:-16384;
            else v=bw_sine(t);
            px[n]=left+i*(right-left)/32; py[n++]=mid-v*amp/16384;
        }
    }
    BW_MARK(bw_graph(px, py, n, y+(int32_t)s/2, BR_ACC));   /* the scope scrolls with the phase: a mark */
}

static int32_t bw_drive_y(uint32_t t, uint32_t q, uint32_t variant)
{
    int32_t in=(int32_t)t-16,mag=in<0?-in:in,amount=(int32_t)q,out;
    if (variant==1u) { int32_t lim=16-amount*9/127; out=in<-lim?-lim:in>lim?lim:in; }
    else if (variant==2u) {
        int32_t lim=16-amount*7/127;
        out=in<0?-((mag<lim?mag:lim)*3/2):(mag<lim?mag:lim)*5/4;
        if(out<-16)out=-16;if(out>16)out=16;
    } else if (variant==3u) { int32_t step=1+amount/24;out=(in/step)*step; }
    else out=in*16/(16+mag*amount/16);
    return out;
}
static void bw_drive(int32_t x,int32_t y,uint32_t s,uint32_t q,uint32_t variant)
{
    int32_t left=bw_q8(x+5),right=bw_q8(x+(int32_t)s-5),top=bw_q8(y+7),bottom=bw_q8(y+(int32_t)s-7);
    int32_t px[33], py[33];
    for(int32_t i=0;i<=32;++i) {
        px[i]=left+i*(right-left)/32;
        py[i]=bottom-(bw_drive_y((uint32_t)i,q,variant)+16)*(bottom-top)/32;
    }
    bw_graph(px, py, 33u, y+(int32_t)s-7, BR_ACC);
}

/* HD: the same pictures with depth: washes under the waveforms, shaded balls
 * for reels, taps and planets, soft halos where something is lit. */
static void bw_fx_engine(int32_t x, int32_t y, uint32_t s, uint32_t variant)
{
    uint32_t family = (variant >> 8) & 3u, engine = variant & 255u, i;
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t top = y + (int32_t)s / 4, bottom = y + (int32_t)s * 3 / 4;
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2, ss = (int32_t)s;
    int32_t px[25], py[25], qy[25];
    if (!family) {                              /* the three DRIVE modes */
        static const int8_t wave[2][13] = {{0, 12, 15, 16, 15, 12, 0, -12, -15, -16, -15, -12, 0},
                                           {0, 16, 16, 16, 16, 16, 0, -16, -16, -16, -16, -16, 0}};
        int32_t amplitude = ss / 5;
        if (engine < 2u) {                        /* overdrive rounds off against soft rails; distortion squares off */
            for (i = 0; i < 13u; ++i) {
                px[i] = bw_q8(left) + (int32_t)i * (right - left) * 256 / 12;
                py[i] = bw_q8(cy) - wave[engine][i] * amplitude * (engine ? 21 : 16);
            }
            if (!engine) {
                hd_line(left, cy - amplitude - 3, right, cy - amplitude - 3, BR_LINE);
                hd_line(left, cy + amplitude + 3, right, cy + amplitude + 3, BR_LINE);
            }
            hdp_curve(px, py, 13u, bw_q8(cy), 448u, BR_ACC);
            for (i = 0; i < 4u; ++i) {            /* the clipped points glint (sparks fly from distortion's corners) */
                int32_t sx = left + (int32_t)(engine ? (i == 0u ? 1 : i == 1u ? 5 : i == 2u ? 7 : 11) : 3 + (i >> 1) * 6) * (right - left) / 12;
                int32_t dir = i < 2u ? -1 : 1, sy = cy + dir * amplitude * (engine ? 4 : 3) / 3;
                if (engine) {
                    bw_line(sx, sy, sx - 3, sy + dir * 3, 256u, C_WHITE);
                    bw_line(sx, sy, sx + 3, sy + dir * 3, 256u, C_WHITE);
                    bw_line(sx, sy, sx, sy + dir * 5, 256u, BR_DIM);
                } else if (i & 1u) continue;
                hd_glow_dot(sx, sy, 3, C_WHITE);
            }
        } else {                                  /* fuzz: a furry, spiky ball */
            static const int8_t unit[12][2] = {{16, 0}, {14, 8}, {8, 14}, {0, 16}, {-8, 14}, {-14, 8},
                                               {-16, 0}, {-14, -8}, {-8, -14}, {0, -16}, {8, -14}, {14, -8}};
            int32_t inner = ss / 8;
            hdp_halo(cx, cy, ss * 7 / 16, 70u);
            for (i = 0; i < 12u; ++i) {
                int32_t reach = (i & 1u) ? ss * 5 / 16 : ss * 7 / 16;
                bw_line(cx + inner * unit[i][0] / 16, cy + inner * unit[i][1] / 16,
                        cx + reach * unit[i][0] / 16, cy + reach * unit[i][1] / 16, 512u, (i & 1u) ? BR_DIM : BR_ACC);
            }
            hdp_ball(cx, cy, inner, BR_ACC);
        }
        return;
    }
    if (family == 1u) {
        engine &= 3u;
        if (engine == 0u) {                       /* clean digital repeats: two lit blocks */
            int32_t m = y + ss * 3 / 8;
            px[0] = px[1] = bw_q8(left); px[2] = px[3] = bw_q8(cx); px[4] = px[5] = bw_q8(right);
            py[0] = py[5] = bw_q8(bottom); py[1] = py[2] = bw_q8(top); py[3] = py[4] = bw_q8(m);
            bw_area(px + 1, py + 1, 0, bw_q8(bottom), 4u, BR_ACC, 90, 10);
            bw_path(px, py, 5u, 384u, BR_ACC);
            bw_line(right, m, right, bottom, 512u, BR_DIM);
            bw_line(cx, m, cx, bottom, 256u, BR_LINE);
        } else if (engine == 1u) {                /* tape reels and path */
            int32_t radius = ss / 7;
            hd_line(left, cy, right, cy, BR_LINE);
            for (i = 0; i < 2u; ++i) {
                int32_t rx = i ? right - radius : left + radius;
                bw_knob_cap(bw_q8(rx), bw_q8(cy), radius * 256);
                hd_circle_q8(bw_q8(rx), bw_q8(cy), radius * 256, 384, BR_ACC);
                hdp_ball(rx, cy, radius / 3, BR_DIM);
            }
            bw_line(left + radius, cy - radius, right - radius, cy - radius, 256u, BR_ACC);
        } else if (engine == 2u) {                /* four taps, fading down */
            for (i = 4u; i-- > 0u;) {
                int32_t tx = left + (int32_t)i * (right - left) / 3, ty = top + (int32_t)i * (bottom - top) / 3;
                bw_line(tx, top, tx, ty, 512u, i ? BR_DIM : BR_ACC);
                if (!i) hdp_halo(tx, ty, 10, 110u);
                hdp_ball(tx, ty, ss / (int32_t)(i + 9u), i ? BR_DIM : BR_ACC);
            }
        } else {                                  /* glitch steps */
            px[0] = bw_q8(left); py[0] = bw_q8(bottom);
            for (i = 1; i <= 8u; ++i) {
                px[2 * i - 1] = px[2 * i] = bw_q8(left + (int32_t)i * (right - left) / 8);
                py[2 * i - 1] = py[2 * i - 2];
                py[2 * i] = bw_q8((i & 1u) ? top + ss / 8 : bottom);
            }
            hdp_curve(px, py, 17u, bw_q8(bottom), 384u, BR_ACC);
        }
    } else if (family == 2u) {
        if (engine == 0u) {                       /* small room: a lit back wall in perspective */
            int32_t bl = cx - ss / 7, br = cx + ss / 7, bt = cy - ss / 7, bb = cy + ss / 7;
            hd_rrect_grad(bl, bt, br - bl + 1, bb - bt + 1, 1, bw_tone(BR_ACC, 80u), bw_tone(BR_ACC, 30u));
            bw_line(left, top, right, top, 512u, BR_LINE);
            bw_line(left, top, left, bottom, 512u, BR_ACC);
            bw_line(right, top, right, bottom, 512u, BR_ACC);
            bw_line(left, bottom, right, bottom, 512u, BR_LINE);
            bw_line(left, top, bl, bt, 256u, BR_DIM);
            bw_line(right, top, br, bt, 256u, BR_DIM);
            bw_line(left, bottom, bl, bb, 256u, BR_DIM);
            bw_line(right, bottom, br, bb, 256u, BR_DIM);
            hd_rrect_frame(bl - 1, bt - 1, br - bl + 3, bb - bt + 3, 1, BR_ACC);
            bw_line(cx, bottom, cx, bb, 256u, BR_LINE);
        } else if (engine == 1u) {                /* hall arch: a solid arch with a receding opening */
            int32_t outer = ss / 3, inner = ss / 6, base = y + ss * 3 / 5, floor = y + ss * 4 / 5;
            uint16_t body = bw_tone(BR_ACC, 70u);
            hd_arc(bw_q8(cx), bw_q8(base), (uint32_t)(outer + inner) * 128u, (uint32_t)(outer - inner) * 256u, 49152u, 32768u, body);
            hd_rrect_grad(cx - outer, base, outer - inner, floor - base, 0, body, bw_tone(BR_ACC, 30u));
            hd_rrect_grad(cx + inner + 1, base, outer - inner, floor - base, 0, body, bw_tone(BR_ACC, 30u));
            bw_arc(bw_q8(cx), bw_q8(base), (uint32_t)outer * 256u, 512u, 49152u, 32768u, BR_ACC);
            bw_line(cx - outer, base, cx - outer, floor, 512u, BR_ACC);
            bw_line(cx + outer, base, cx + outer, floor, 512u, BR_ACC);
            bw_arc(bw_q8(cx), bw_q8(base), (uint32_t)inner * 256u, 256u, 49152u, 32768u, BR_DIM);
            bw_line(cx - inner, base, cx - inner, floor, 256u, BR_DIM);
            bw_line(cx + inner, base, cx + inner, floor, 256u, BR_DIM);
            bw_line(cx - outer, floor, cx + outer, floor, 256u, BR_LINE);
        } else {                                  /* orbiting space: a lit planet and a twinkling star
                                                   * (Spring and Ethereal were retired: the UI offers ROOM, HALL, SPACE) */
            int32_t planet_x = cx + ss / 5, planet_y = cy - ss / 7, star_x = cx - ss / 4, star_y = cy + ss / 5;
            hd_circle_q8(bw_q8(cx), bw_q8(cy), ss * 256 / 3, 384, BR_ACC);
            hd_circle_q8(bw_q8(cx), bw_q8(cy), ss * 256 / 5, 256, BR_DIM);
            hdp_halo(planet_x, planet_y, ss / 10 + 5, 110u);
            hdp_ball(planet_x, planet_y, ss / 10, C_WHITE);
            hd_glow_dot(star_x, star_y, 4, BR_ACC);
            bw_line(star_x - 4, star_y, star_x + 4, star_y, 256u, BR_ACC);
            bw_line(star_x, star_y - 4, star_x, star_y + 4, 256u, BR_ACC);
        }
    } else {
        if (engine == 0u || engine == 3u) {      /* chorus / vibrato waves */
            for (i = 0; i <= 24u; ++i) {
                px[i] = bw_q8(left) + (int32_t)i * (right - left) * 256 / 24;
                py[i] = bw_q8(cy) - bw_sine(i * 2731u) * ss / 391;
                qy[i] = bw_q8(cy) - bw_sine(i * (engine ? 3072u : 2731u) + 8192u) * ss / 547;
            }
            if (!engine) bw_path(px, qy, 25u, 256u, BR_DIM);
            hdp_curve(px, py, 25u, bw_q8(cy), 448u, BR_ACC);
        } else if (engine == 1u) {                /* flange comb */
            hdp_halo(cx, cy, 8, 120u);
            for (i = 0; i < 5u; ++i) {
                int32_t tx = left + (int32_t)i * (right - left) / 4;
                if (i == 2u) hd_rrect_grad(tx - 1, top, 3, bottom - top + 1, 1, hd_lighten(BR_ACC, 80u), hd_darken(BR_ACC, 80u));
                else bw_line(tx, top, tx, bottom, 256u, BR_DIM);
            }
            bw_line(left, cy, right, cy, 512u, BR_ACC);
        } else {                                  /* phaser rings */
            bw_round_arc(bw_q8(cx), bw_q8(cy), (uint32_t)ss * 256u / 3u, 512u, 8192u, 49152u, BR_ACC);
            bw_round_arc(bw_q8(cx), bw_q8(cy), (uint32_t)ss * 256u / 5u, 320u, 0u, 49152u, BR_DIM);
            hdp_halo(cx, cy, 6, 120u);
            hdp_ball(cx, cy, 2, BR_ACC);
        }
    }
}
/* D19 MIDI CHANNEL (batch C): the DIN socket in the colour of the track it
 * feeds, ringed by the 16 channels; the lit dot is the channel (the value
 * text carries the number). Kit lanes add four pips, their own lit. OFF: a
 * grey socket struck through. variant = channel | destination << 8 (T1-T3
 * 0-2, D1-D4 3-6). */
static void bw_midi_channel(int32_t x, int32_t y, uint32_t s, uint32_t variant)
{
    uint32_t ch = variant & 31u, dest = variant >> 8;
    int32_t cx = bw_q8(x + 30), cy = bw_q8(y + 29), px, py;
    uint16_t hue = ch ? br_track_color(dest < 3u ? dest : BR_DRUM_PART) : BR_DIM;
    (void)s;
    hd_circle_q8(cx, cy, 11 * 256, 384, hue);
    for (uint32_t j = 0; j < 5u; ++j) {
        bw_point(cx, cy, 6 * 256 + 128, 49152u + j * 8192u, &px, &py);
        hd_circle_q8(px, py, 352, 0, ch ? C_WHITE : BR_DIM);
    }
    for (uint32_t i = 1; i <= 16u; ++i) {
        bw_point(cx, cy, 18 * 256, (i - 1u) * 4096u, &px, &py);
        hd_circle_q8(px, py, i == ch ? 640 : 288, 0, i == ch ? C_WHITE : bw_tone(hue, 150u));
    }
    if (!ch) hd_line_q8(cx - 1792, cy + 1792, cx + 1792, cy - 1792, 448u, BR_DIM);
    if (dest >= 3u) {                                            /* the four lanes, this one lit */
        hd_rrect(x + 23, y + 52, 15, 3, 0, BR_LINE);
        hd_rrect(x + 11 + 4 * (int32_t)dest, y + 52, 3, 3, 0, hue);
    }
}

/* A track-colored ribbon folds more often as timbre rises. Each sample is one full
 * 2x2 column. The shape follows the knob only; phase moves a point of light along it. */
static void bw_timbre(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    uint32_t columns = s >= 48u ? 24u : 10u;
    int32_t thick = 4 + (int32_t)(q * 4u / 127u / 2u) * 2;
    int32_t span = (int32_t)columns * 2;
    int32_t left = x + ((int32_t)s - span) / 2;
    int32_t middle = y + (int32_t)s / 2;
    int32_t amplitude = s >= 48u ? (int32_t)s / 5 : 4;
    uint32_t turn = (2u + q * 3u / 127u) * 32768u / columns;
    int32_t px[25], ty[25], by[25], tw[25];
    /* A twisted ribbon: its width follows the cosine of the twist, so it
     * pinches where the edge turns toward the eye; the face facing us is lit,
     * the back face darker, each shaded from its upper edge, and the two edges
     * (pale and dim) cross at every pinch. */
    for (uint32_t i = 0; i < columns; ++i) {
        uint32_t wave = (i * 65536u / columns) & 65535u;
        int32_t driven = bw_sine(wave) * (127 + (int32_t)q * 3) / 127;
        int32_t folded = (int32_t)((uint32_t)(driven + 16384) & 65535u), half;
        if (folded > 32768) folded = 65536 - folded;
        folded -= 16384;
        tw[i] = bw_sine(16384u + i * turn);
        half = (thick * 128 + 320) * tw[i] / 16384;
        px[i] = (left + (int32_t)i * 2 + 1) * 256;
        ty[i] = middle * 256 + 128 - folded * amplitude / 64 - half;
        by[i] = ty[i] + 2 * half;
    }
    for (uint32_t i = 0; i + 1u < columns; ++i) {
        int32_t t = tw[i] + tw[i + 1u], m = (t < 0 ? -t : t) * 3 / 1024;          /* 0..96 */
        bw_area(px + i, ty + i, by + i, 0, 2u,
                t >= 0 ? bw_tone(BR_ACC, 150u + (uint32_t)m + q * 9u / 127u) : bw_tone(BR_ACC, 40u + (uint32_t)m / 2u),
                255, 150);
    }
    bw_path(px, by, columns, 256u, bw_tone(BR_ACC, 120u));
    bw_path(px, ty, columns, 320u, bw_highlight(0u));
    /* The ribbon holds its shape (the knob's); while the track sounds, a point of light runs along
     * its lit edge. Only the few rows around the light change from frame to frame. */
    if (phase) {
        uint32_t at = phase * 3u % columns;
        BW_MARK(hd__radial(px[at], ty[at], 512, -1, 0u, 0u, bw_highlight(1u)));
    }
}

/* MORPH / COLOR: a faceted prism; the beam leaves it as a spectrum that opens with the value. The fan
 * starts a little inside the right corner (the girdle line stops there) as five fine lines, which
 * thicken with the value until they meet as five bands (about 50%), whose borders then soften and whose
 * outer bands fade (toward 100%). The fan is one pass, column by column: per column the band width and
 * its reciprocal, the lines' half widths and the fade; per pixel only the two nearest lines (a divide
 * only on band borders). While the picture moves (not on HOME), a glint runs out along one line of the
 * spectrum at a time and along the incoming beam. */
static int32_t bw_ease(int32_t v)            /* smoothstep, q8 in and out */
{
    if (v <= 0) return 0;
    if (v >= 256) return 256;
    return v * v * (768 - 2 * v) >> 16;
}
static __attribute__((noinline)) uint16_t bw_mix565(uint16_t a, uint16_t b, uint32_t t) { return hd_mix(a, b, t); }
static __attribute__((minsize, noinline)) void bw_color(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    /* per line (top to bottom): opacity as crisp bands, then as soft ones */
    static const uint8_t alpha[10] = {255, 245, 235, 220, 200, 60, 245, 240, 225, 70};
    int32_t cx = x + (int32_t)s * 7 / 15, d = (int32_t)s / 6, left = cx - d, right = cx + d;
    int32_t top = y + (int32_t)s / 4, bottom = y + (int32_t)s * 3 / 4, middle = (top + bottom) / 2;
    int32_t spread = (2 + (int32_t)q * ((int32_t)s * 3 / 8 - 2) / 127) * 256;
    int32_t ox = bw_q8(right - (int32_t)s / 10), oy = bw_q8(middle), len = bw_q8(x + (int32_t)s - 6) - ox;
    int32_t gx = bw_q8(cx), gw = d * 16, gh = (bottom - middle) * 16;             /* the prism, q4 */
    int32_t qn = (int32_t)q * 256 / 127, w = bw_ease((qn - 13) * 256 / 115), sg = bw_ease((qn - 150) * 256 / 106);
    int32_t al[5], b[4], i;
    uint32_t t = (phase * 4u) & 127u, ray = (phase >> 5) * 3u % 5u;
    uint16_t col[5];
    int32_t lpx[4] = {cx, left, cx, cx}, lpy[4] = {top, middle, bottom, middle};
    int32_t rpx[4] = {cx, right, cx, cx}, rpy[4] = {top, middle, bottom, middle};
    col[0] = bw_highlight(1u); col[1] = bw_highlight(0u); col[2] = bw_mix565(BR_ACC, col[1], 90u);
    col[3] = BR_ACC; col[4] = bw_tone(BR_ACC, 150u);

    bw_line(x + 6, middle, left, middle, 512u, col[1]);
    if (phase) BW_MARK(hd__radial(bw_q8(x + 6 + (left - (x + 6)) * (int32_t)t / 127), oy, 512, -1, 0u, 0u, col[0]));
    bw_face(lpx, lpy, bw_tone(BR_ACC, 128u));
    bw_face(rpx, rpy, bw_tone(BR_ACC, 220u));
    bw_line(cx, top, left, middle, 512u, col[1]);
    bw_line(left, middle, cx, bottom, 512u, bw_tone(BR_ACC, 170u));
    bw_line(cx, bottom, right, middle, 512u, bw_tone(BR_ACC, 195u));
    bw_line(right, middle, cx, top, 512u, col[0]);
    bw_qline(bw_q8(left), oy, ox, oy, 512u, bw_tone(BR_ACC, 115u));          /* the girdle stops where the light opens */

    for (i = 0; i < 5; ++i) al[i] = alpha[i] + (alpha[i + 5] - alpha[i]) * sg / 256;
    b[0] = ox >> 8; b[2] = (ox + len) >> 8; b[1] = (oy - spread - 512) >> 8; b[3] = (oy + spread + 512) >> 8;
    if (hd__clip(b))
        for (int32_t px = b[0]; px <= b[2]; ++px) {
            int32_t dx = px * 256 + 128 - ox, e0, bw, inv, along, tip, y0, y1, gu, h;
            if (dx <= 0) continue;
            e0 = -(spread * dx / len); bw = -2 * e0 / 5;
            if (bw < 64) bw = 64;
            inv = (256 << 12) / bw;
            tip = len - dx + 128;
            if (tip > 256) tip = 256;
            along = 256 - (100 + sg * 40 / 256) * dx / len;
            h = 154 + ((bw >> 1) - 154) * w / 256;      /* the lines' half width: 0.6 px, growing to meet */
            if (h < 154) h = 154;
            gu = px * 256 + 128 - gx; if (gu < 0) gu = -gu;
            gu = gw * gh - (gu >> 4) * gh;           /* inside the prism while |v| * gw is below this */
            y0 = (oy + e0 - 384) >> 8; y1 = (oy - e0 + 384) >> 8;
            if (y0 < b[1]) y0 = b[1];
            if (y1 > b[3]) y1 = b[3];
            for (int32_t py = y0; py <= y1; ++py) {
                int32_t p = (((py * 256 + 128 - oy - e0) * inv) >> 12) - 128, i0 = p >> 8, cv[2], a, f, lo, hi, gv;
                for (i = 0; i < 2; ++i) {             /* coverage of the two lines either side */
                    int32_t li = i0 + i, e;
                    cv[i] = 0;
                    if (li < 0 || li > 4) continue;
                    e = p - li * 256; if (e < 0) e = -e;
                    e = h - (e * bw >> 8) + 128;
                    cv[i] = e < 0 ? 0 : e > 256 ? 256 : e;
                }
                if (!(cv[0] | cv[1])) continue;
                lo = i0 < 0 ? 0 : i0; hi = i0 > 3 ? 4 : i0 + 1;
                f = cv[1] && cv[0] ? cv[1] * 256 / (cv[0] + cv[1]) : cv[1] ? 256 : 0;
                if (i0 >= 0 && i0 < 4) f += (p - i0 * 256 - f) * sg / 256;       /* soft: the tints run together */
                if (f > 255) f = 255;
                if (f < 0) f = 0;
                a = cv[0] + cv[1]; if (a > 256) a = 256;
                a = (a * (al[lo] + (al[hi] - al[lo]) * f / 256) >> 8) * along >> 8;
                a = a * tip >> 8;
                gv = py * 256 + 128 - oy; if (gv < 0) gv = -gv;
                if ((gv >> 4) * gw < gu) a = a * 200 >> 8;                         /* seen through the glass */
                if (a > 0) hd__px(px, py, bw_mix565(col[lo], col[hi], (uint32_t)f), a > 255 ? 255 : a);
            }
        }
    hd__radial(gx, oy, 640, -1, 0u, 0u, col[0]);
    if (phase) BW_MARK(hd__radial(ox + len * (int32_t)t / 127, oy + ((int32_t)ray * 2 - 4) * spread / 5 * (int32_t)t / 127,
                                  512, -1, 0u, 0u, (ray & 1u) ? col[1] : col[0]));
}

/* A compact harmonic spectrum. The fundamental stays present while the
 * control opens more partials and raises their bars; a track-colored spark travels
 * across the active spectrum and each bar breathes slightly with phase (marks: a step
 * redraws the spectrum's box only). */
static void bw_harmonics(int32_t x, int32_t y, uint32_t s, uint32_t q,
                         uint32_t phase)
{
    static const uint8_t partial[12] = {
        0u, 112u, 88u, 72u, 60u, 52u, 44u, 38u, 33u, 29u, 25u, 22u
    };
    uint32_t maximum = s >= 48u ? 12u : s >= 28u ? 5u : 3u;
    uint32_t active = 1u + q * (maximum - 1u) / 127u;
    int32_t span = (int32_t)(maximum - 1u) * 4 + 2;
    int32_t left = (x + ((int32_t)s - span) / 2) & ~1;
    int32_t top = y + (int32_t)s / 5;
    int32_t base = y + (int32_t)s * 4 / 5;
    int32_t max_height = base - top - 4;
    uint32_t spark = phase % active;
    uint16_t spark_color = bw_highlight(0u);

    if (max_height < 2) max_height = 2;
    hd_line(left - 2, base + 1, left + span, base + 1, BR_LINE);
    for (uint32_t i = 0; i < active; ++i) {
        uint32_t level;
        uint32_t wobble = (phase + i * 9u) & 31u;   /* each bar breathes with the phase (still: phase 0) */
        uint32_t pulse;
        int32_t height;
        int32_t at = left + (int32_t)i * 4;
        uint16_t color;

        if (i == 0u)
            level = 42u + q * 66u / 127u;
        else
            level = q * partial[i] / 127u;
        if (wobble > 16u) wobble = 32u - wobble;
        pulse = 88u + wobble * 2u;
        height = (int32_t)((uint32_t)max_height * level * pulse / (127u * 104u));
        if (height < 2) height = 2;
        if (height > max_height) height = max_height;

        color = i == 0u ? bw_tone(BR_ACC, 235u) :
            bw_tone(BR_ACC, 105u + q * 115u / 127u);
        /* Three-pixel bars with a rounded, lit cap on a one-pixel gap. */
        BW_MARK(hd__rrect(at - 1, base - height, 3, height + 1, 1,
                          hd_lighten(color, 40u), hd_darken(color, 60u), hd_lighten(color, 110u), hd_darken(color, 60u), 0));
        if (i == spark)
            BW_MARK(hd_glow_dot(at, base - height - 3, 2, spark_color));
    }
}

/* Pitch-order silhouettes and note subdivisions identify the arp controls. */
static void bw_arp_mode(int32_t x, int32_t y, uint32_t s, uint32_t mode)
{
    static const uint8_t pitches[5][4] = {
        {0,1,2,3}, {3,2,1,0}, {0,2,3,1},
        {1,3,0,2}, {2,0,3,1}
    };
    uint32_t m = mode < 5u ? mode : 0u;
    int32_t spacing = ((int32_t)s - 30) / 3;
    int32_t left = x + ((int32_t)s - (3 * spacing + 6)) / 2;
    int32_t pitch_step = ((int32_t)s - 39) / 3;
    int32_t base = y + ((int32_t)s + 3 * pitch_step + 12) / 2;
    if (pitch_step < 1) pitch_step = 1;
    for (uint32_t i = 0u; i < 4u; ++i) {
        int32_t nx = left + (int32_t)i * spacing;
        int32_t ny = base - (int32_t)pitches[m][i] * pitch_step;
        uint16_t color = i == 1u ? C_WHITE : BR_ACC;
        bw_circle(nx + 3, ny - 2, 3, color, 1u);
        bw_rect(nx + 4, ny - 12, 2, 10, color);
    }
}

static void bw_arp_rate(int32_t x, int32_t y, uint32_t s, uint32_t rate)
{
    uint32_t r = rate < 5u ? rate : 0u;
    uint32_t flags = r < 2u ? 1u : r < 4u ? 2u : 3u;
    int32_t hx = x + (int32_t)s / 2 - 8, hy = y + (int32_t)s * 3 / 4;
    int32_t stem = hx + 10, top = y + (int32_t)s / 5;
    /* A rounded notehead, pale on top and shaded at its base. */
    hd_rrect_grad(hx, hy - 5, 12, 10, 5, bw_highlight(1u), bw_tone(BR_ACC, 150u));
    hd_rrect(stem, top, 2, hy - top, 1, bw_highlight(0u));
    for (uint32_t i = 0u; i < flags; ++i) {
        int32_t fy = top + (int32_t)i * 8;
        bw_quadratic(bw_q8(stem + 1), bw_q8(fy), bw_q8(stem + 11), bw_q8(fy + 1),
                     bw_q8(stem + 10), bw_q8(fy + 8), 512u, i ? BR_ACC : bw_highlight(0u));
    }
    if (r == 1u || r == 3u) br_widget_text(stem + 6, top - 9, "3", BR_DIM);
}

/* Shared by the parameter graphics: the 1 px rule x+8..x+52 at row y. */
static __attribute__((minsize, noinline)) void pg_rule(int32_t x, int32_t y)
{
    hd_line(x + 8, y, x + 52, y, BR_LINE);
}
/* The sequencer's velocity lollipop: a 2 px stem from the floor up to a 7 px ball at top; lit: its glint. */
static __attribute__((minsize, noinline)) void pg_lollipop(int32_t cx, int32_t top, int32_t floor, uint16_t c, uint32_t lit)
{
    hd_rrect(cx - 1, top, 2, floor - top, 1, c);
    hd_rrect(cx - 3, top - 3, 7, 7, 3, c);
    if (lit) hd_rrect(cx - 2, top - 2, 2, 1, 1, bw_highlight(1u));
}
/* Parameter graphics, one file per batch. This first inclusion compiles each
 * file's drawing half; instrument_ui.c includes them again with
 * BR_PARAM_HOOKS defined for their mapping hooks. */
#include "instrument_widgets_params_a.c"
#include "instrument_widgets_params_b.c"
#include "instrument_widgets_params_c.c"
#include "instrument_widgets_params_d.c"

static void br_widget_draw(uint32_t kind, int32_t x, int32_t y, uint32_t size,
                           uint32_t q0_127, uint32_t variant,
                           const uint32_t env[4], uint32_t phase0_127)
{
    uint32_t q = bw_clamp(q0_127, 127u), phase = bw_clamp(phase0_127, 127u);
    size = size < 16u ? 16u : size > 64u ? 64u : size;
    switch (kind) {
    case BR_WIDGET_ARP_MODE: bw_arp_mode(x, y, size, variant); break;
    case BR_WIDGET_ARP_RATE: bw_arp_rate(x, y, size, variant); break;
    default:                       /* HD: POT is the single generic fallback */
    case BR_WIDGET_POT: bw_pot(x, y, size, q); break;
    case BR_WIDGET_COMP_SOURCE: bw_comp_source(x, y, size, variant); break;
    case BR_WIDGET_PATCH_OFF: bw_comp_source(x, y, size, BR_COMP_OFF); break;
    case BR_WIDGET_FILTER_TARGET: bw_filter_target(x, y, size, 0u); break;
    case BR_WIDGET_RESONANCE_TARGET: bw_filter_target(x, y, size, 1u); break;
    case BR_WIDGET_MONITOR: bw_monitor(x, y, size, variant); break;
    case BR_WIDGET_BPM: bw_bpm(x, y, size, q, phase); break;
    case BR_WIDGET_MIDI_CHANNEL: bw_midi_channel(x, y, size, variant); break;
    case BR_WIDGET_FADER: bw_fader(x, y, size, q); break;
    case BR_WIDGET_SWING: bw_swing(x, y, size, q); break;
    case BR_WIDGET_REVERB: bw_reverb(x, y, size, q); break;
    case BR_WIDGET_DELAY: bw_delay(x, y, size, q, phase, variant); break;
    case BR_WIDGET_FX_ENGINE: bw_fx_engine(x, y, size, variant); break;
    case BR_WIDGET_ADSR: bw_adsr(x, y, size, env, variant); break;
    case BR_WIDGET_GATE: bw_gate(x, y, size, q, variant != 0u); break;
    case BR_WIDGET_WAVE: bw_wave(x, y, size, q, variant, phase); break;
    /* DRIVE is a retired saved ID; no active UI control emits it. */
    case BR_WIDGET_TIMBRE: bw_timbre(x, y, size, q, phase); break;
    case BR_WIDGET_COLOR: bw_color(x, y, size, q, phase); break;
    case BR_WIDGET_HARMONICS: bw_harmonics(x, y, size, q, phase); break;
    case BR_WIDGET_MIX: bw_blend(x, y, size, q, variant, env, phase); break;   /* D01, batch B */
    case BR_WIDGET_TRACK_PAN: bw_track_pan(x, y, size, q, variant); break;
    case BR_WIDGET_HEAT: bw_heat(x, y, size, variant, phase); break;
    case BR_WIDGET_MOD_ATTEN: bw_mod_atten(x, y, size, q, variant, phase); break;
    /* Parameter graphics: one call each, the same arguments for all. */
    case BR_WIDGET_ENV_AMOUNT: bw_env_amount(x, y, size, q, variant, env, phase); break;     /* A */
    case BR_WIDGET_KEYTRACK: bw_keytrack(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_TUNE: bw_tune(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_ENV_MODE: bw_env_mode(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_LFO_DEPTH: bw_lfo_depth(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_ENABLE: bw_enable(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_FOLLOWER: bw_follower(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_GATE_SOURCE: bw_gate_source(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_VELOCITY: bw_velocity(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_SMOOTH: bw_smooth(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_SEND: bw_send(x, y, size, q, variant, env, phase); break;                 /* B */
    case BR_WIDGET_DRIVE_GAIN: bw_drive_gain(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_TONE: bw_drive_tone(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_ECHO: bw_echo(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_ORBIT: bw_orbit(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_STEREO_DEPTH: bw_stereo_depth(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_TAIL: bw_tail(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_DRIVE_BIAS: bw_drive_bias(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_MOD_FEEDBACK: bw_mod_feedback(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_PRE_DELAY: bw_pre_delay(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_LOOP_LEN: bw_loop_len(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_PUNCH_FILTER: bw_punch_filter(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_TREMOLO: bw_tremolo(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_SNAP: bw_snap(x, y, size, q, variant, env, phase); break;                 /* C */
    case BR_WIDGET_COMP_CURVE: bw_comp_curve(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_COMP_TIME: bw_comp_time(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_HEAT_AMOUNT: bw_heat_amount(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_CLOCK_SOURCE: bw_clock_source(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_STEPS: bw_steps(x, y, size, q, variant, env, phase); break;               /* D */
    case BR_WIDGET_SPEED: bw_speed(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_QUANTIZE: bw_quantize(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_OCTAVES: bw_octaves(x, y, size, q, variant, env, phase); break;
    case BR_WIDGET_ARP_GATE: bw_arp_gate(x, y, size, q, variant, env, phase); break;
    }
}
