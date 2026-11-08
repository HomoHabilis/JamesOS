/* SPDX-License-Identifier: GPL-3.0-only
 * Binary 2x2-cell artwork primitives for the small RGB565 canvas.
 * Include after gfx.c so cv_px/cv_w/cv_h/cv_oy/swap16 are in scope.
 * Public shape geometry is Q8 pixels; raster decisions use Q4 cell centers.
 */

#define BR_SHAPE_Q8_LIMIT (512 * 256)
#define BR_SHAPE_Q4_LIMIT (512 * 16)
#define BR_SHAPE_CELL_Q4 32

static int32_t br_shape_q8_to_q4(int64_t v)
{
    if (v > BR_SHAPE_Q8_LIMIT) v = BR_SHAPE_Q8_LIMIT;
    if (v < -BR_SHAPE_Q8_LIMIT) v = -BR_SHAPE_Q8_LIMIT;
    if (v >= 0) return (int32_t)((v + 8) / 16);
    return (int32_t)(-((-v + 8) / 16));
}

static int32_t br_shape_floor_div(int32_t value, int32_t divisor)
{
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int32_t br_shape_ceil_div(int32_t value, int32_t divisor)
{
    return -br_shape_floor_div(-value, divisor);
}

/* Fill a cell anchored to the canvas origin. Only a cell clipped by the
 * canvas edge can be smaller than 2x2; all interior art remains crisp. */
static void br_shape_put_cell(int32_t cell_x, int32_t cell_y, uint16_t color)
{
    int32_t x, y, x0 = cell_x * 2, y0 = cell_y * 2;
    uint16_t pixel = swap16(color);
    for (y = y0; y < y0 + 2 && (uint32_t)y < cv_h; ++y) {
        if (y < 0) continue;
        for (x = x0; x < x0 + 2 && (uint32_t)x < cv_w; ++x) {
            if (x >= 0) cv_px[(uint32_t)y * cv_w + (uint32_t)x] = pixel;
        }
    }
}

/* Integer-pixel artwork helpers. Rectangles cover every 2x2 cell touched by
 * their half-open bounds; lines use an 8-connected Bresenham cell path. */
static void br_art_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    int32_t x0, x1, y0, y1, cx, cy;
    if (!cv_w || !cv_h || w <= 0 || h <= 0) return;
    y += cv_oy;
    x0 = br_shape_floor_div(x, 2);
    x1 = br_shape_ceil_div(x + w, 2) - 1;
    y0 = br_shape_floor_div(y, 2);
    y1 = br_shape_ceil_div(y + h, 2) - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)(cv_w - 1u) / 2) x1 = (int32_t)(cv_w - 1u) / 2;
    if (y1 > (int32_t)(cv_h - 1u) / 2) y1 = (int32_t)(cv_h - 1u) / 2;
    for (cy = y0; cy <= y1; ++cy)
        for (cx = x0; cx <= x1; ++cx)
            br_shape_put_cell(cx, cy, color);
}

static void br_art_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    int32_t cx0 = br_shape_floor_div(x0, 2), cy0 = br_shape_floor_div(y0 + cv_oy, 2);
    int32_t cx1 = br_shape_floor_div(x1, 2), cy1 = br_shape_floor_div(y1 + cv_oy, 2);
    int32_t dx = cx1 >= cx0 ? cx1 - cx0 : cx0 - cx1;
    int32_t sx = cx0 < cx1 ? 1 : -1;
    int32_t dy = cy1 >= cy0 ? cy0 - cy1 : cy1 - cy0;
    int32_t sy = cy0 < cy1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        br_shape_put_cell(cx0, cy0, color);
        if (cx0 == cx1 && cy0 == cy1) break;
        {
            int32_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; cx0 += sx; }
            if (e2 <= dx) { err += dx; cy0 += sy; }
        }
    }
}

static int32_t br_shape_bound_cell_low(int32_t q4, uint32_t limit)
{
    int32_t cell = br_shape_floor_div(q4, BR_SHAPE_CELL_Q4) - 1;
    if (cell < 0) cell = 0;
    if (cell > (int32_t)limit) cell = (int32_t)limit;
    return cell;
}

static int32_t br_shape_bound_cell_high(int32_t q4, uint32_t limit)
{
    int32_t cell = br_shape_floor_div(q4, BR_SHAPE_CELL_Q4) + 1;
    int32_t max = ((int32_t)limit + 1) / 2 - 1;
    if (cell < -1) cell = -1;
    if (cell > max) cell = max;
    return cell;
}

static int32_t br_shape_cell_center_q4(int32_t cell)
{
    return cell * BR_SHAPE_CELL_Q4 + BR_SHAPE_CELL_Q4 / 2;
}

/* Put round geometry on cell boundaries on both axes. A fractional center
 * otherwise gives different top/bottom and left/right cell coverage. */
static int32_t br_shape_round_center_q4(int32_t v)
{
    return br_shape_floor_div(v + BR_SHAPE_CELL_Q4 / 2, BR_SHAPE_CELL_Q4) * BR_SHAPE_CELL_Q4;
}

static uint32_t br_shape_cell_width_q4(uint32_t widthq8)
{
    uint32_t widthq4 = (widthq8 + 8u) / 16u;
    uint32_t cells = (widthq4 + BR_SHAPE_CELL_Q4 - 1u) / BR_SHAPE_CELL_Q4;
    if (!cells) cells = 1u;
    return cells * BR_SHAPE_CELL_Q4;
}

static uint32_t br_shape_isqrt32(uint32_t n)
{
    uint32_t root = 0;
    uint32_t bit = (uint32_t)1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static uint32_t br_shape_line_cell(int32_t px, int32_t py,
                                   int32_t x0, int32_t y0,
                                   int32_t dx, int32_t dy,
                                   int32_t len2, uint32_t len,
                                   int32_t radius)
{
    int32_t vx = px - x0, vy = py - y0;
    int32_t dot = vx * dx + vy * dy;
    int32_t ex, ey, cross;
    if (dot <= 0) {
        ex = vx; ey = vy;
        return ex * ex + ey * ey <= radius * radius;
    }
    if (dot >= len2) {
        ex = px - (x0 + dx); ey = py - (y0 + dy);
        return ex * ex + ey * ey <= radius * radius;
    }
    cross = vx * dy - vy * dx;
    if (cross < 0) cross = -cross;
    return (uint32_t)cross <= (uint32_t)radius * len;
}

/* Round-cap stroke rasterized as solid 2x2 cells. */
static void br_shape_line(int32_t x0q8, int32_t y0q8,
                          int32_t x1q8, int32_t y1q8,
                          uint32_t widthq8, uint16_t color)
{
    int32_t x0, y0, x1, y1, dx, dy, len2, radius;
    int32_t xa, xb, ya, yb, x, y;
    uint32_t len;
    if (!cv_w || !cv_h || !widthq8) return;
    x0 = br_shape_q8_to_q4(x0q8); x1 = br_shape_q8_to_q4(x1q8);
    y0 = br_shape_q8_to_q4((int64_t)y0q8 + (int64_t)cv_oy * 256);
    y1 = br_shape_q8_to_q4((int64_t)y1q8 + (int64_t)cv_oy * 256);
    /* Axis-aligned rounded tracks need matching end caps on the cell grid. */
    if (x0 == x1 || y0 == y1) {
        x0 = br_shape_round_center_q4(x0); x1 = br_shape_round_center_q4(x1);
        y0 = br_shape_round_center_q4(y0); y1 = br_shape_round_center_q4(y1);
    }
    dx = x1 - x0; dy = y1 - y0;
    len2 = dx * dx + dy * dy;
    len = len2 ? br_shape_isqrt32((uint32_t)len2) : 0u;
    radius = (int32_t)(br_shape_cell_width_q4(widthq8) / 2u);
    xa = br_shape_bound_cell_low((x0 < x1 ? x0 : x1) - radius, cv_w);
    xb = br_shape_bound_cell_high((x0 > x1 ? x0 : x1) + radius, cv_w);
    ya = br_shape_bound_cell_low((y0 < y1 ? y0 : y1) - radius, cv_h);
    yb = br_shape_bound_cell_high((y0 > y1 ? y0 : y1) + radius, cv_h);
    if (xa > xb || ya > yb) return;
    for (y = ya; y <= yb; ++y) {
        int32_t py = br_shape_cell_center_q4(y);
        for (x = xa; x <= xb; ++x) {
            if (br_shape_line_cell(br_shape_cell_center_q4(x), py, x0, y0,
                                   dx, dy, len2, len, radius))
                br_shape_put_cell(x, y, color);
        }
    }
}

static int32_t br_shape_dist2_q4(int32_t px, int32_t py, int32_t cx, int32_t cy)
{
    int32_t dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy;
}

static void br_shape_circle(int32_t cxq8, int32_t cyq8,
                            uint32_t radiusq8, uint32_t widthq8,
                            uint16_t color, uint32_t fill)
{
    int32_t cx, cy, radius, halfwidth, outer, inner = 0;
    int32_t xa, xb, ya, yb, x, y;
    if (!cv_w || !cv_h) return;
    cx = br_shape_round_center_q4(br_shape_q8_to_q4(cxq8));
    cy = br_shape_round_center_q4(br_shape_q8_to_q4((int64_t)cyq8 + (int64_t)cv_oy * 256));
    radius = br_shape_q8_to_q4(radiusq8);
    if (fill) {
        outer = radius;
        if (outer < 24) outer = 24; /* keep small 4px dot marks visible */
    } else {
        if (!widthq8) return;
        halfwidth = (int32_t)(br_shape_cell_width_q4(widthq8) / 2u);
        outer = radius + halfwidth;
        inner = radius - halfwidth;
        if (inner < 0) inner = 0;
    }
    xa = br_shape_bound_cell_low(cx - outer, cv_w);
    xb = br_shape_bound_cell_high(cx + outer, cv_w);
    ya = br_shape_bound_cell_low(cy - outer, cv_h);
    yb = br_shape_bound_cell_high(cy + outer, cv_h);
    if (xa > xb || ya > yb) return;
    for (y = ya; y <= yb; ++y) {
        int32_t py = br_shape_cell_center_q4(y);
        for (x = xa; x <= xb; ++x) {
            int32_t px = br_shape_cell_center_q4(x);
            int32_t d2 = br_shape_dist2_q4(px, py, cx, cy);
            if (d2 <= outer * outer && (fill || !inner || d2 >= inner * inner))
                br_shape_put_cell(x, y, color);
        }
    }
}

/* The same circular geometry can also use native 1x1 cells for compact UI
 * details. The 2x2 version remains the default for instrument artwork. */
static void br_shape_circle_grid(int32_t cxq8, int32_t cyq8,
                                 uint32_t radiusq8, uint32_t widthq8,
                                 uint16_t color, uint32_t fill, uint32_t cell_pixels)
{
    int32_t cx, cy, radius, outer, inner=0, xa, xb, ya, yb;
    if (cell_pixels == 2u) {
        br_shape_circle(cxq8,cyq8,radiusq8,widthq8,color,fill);
        return;
    }
    if (cell_pixels != 1u || !cv_w || !cv_h || (!fill && !widthq8)) return;
    cx=br_shape_floor_div(br_shape_q8_to_q4(cxq8),16)*16+8;
    cy=br_shape_floor_div(br_shape_q8_to_q4((int64_t)cyq8+(int64_t)cv_oy*256),16)*16+8;
    radius=br_shape_q8_to_q4(radiusq8);
    if(fill) outer=radius<8?8:radius;
    else {
        int32_t halfwidth=(int32_t)((widthq8+255u)/256u)*8;
        outer=radius+halfwidth;inner=radius-halfwidth;
        if(inner<0)inner=0;
    }
    xa=br_shape_floor_div(cx-outer,16)-1;xb=br_shape_floor_div(cx+outer,16)+1;
    ya=br_shape_floor_div(cy-outer,16)-1;yb=br_shape_floor_div(cy+outer,16)+1;
    if(xa<0)xa=0;if(ya<0)ya=0;
    if(xb>=(int32_t)cv_w)xb=(int32_t)cv_w-1;
    if(yb>=(int32_t)cv_h)yb=(int32_t)cv_h-1;
    for(int32_t y=ya;y<=yb;++y)for(int32_t x=xa;x<=xb;++x) {
        int32_t d2=br_shape_dist2_q4(x*16+8,y*16+8,cx,cy);
        if(d2<=outer*outer && (fill || !inner || d2>=inner*inner))
            cv_px[(uint32_t)y*cv_w+(uint32_t)x]=swap16(color);
    }
}

static uint32_t br_shape_arc_cell(int32_t px, int32_t py,
                                  int32_t cx, int32_t cy,
                                  int32_t sx, int32_t sy,
                                  int32_t ex, int32_t ey,
                                  int32_t outer, int32_t inner,
                                  uint32_t large_arc)
{
    int32_t dx = px - cx, dy = py - cy;
    int32_t c1 = sx * dy - sy * dx;
    int32_t c2 = dx * ey - dy * ex;
    uint32_t in_sweep = large_arc ? (c1 >= 0 || c2 >= 0) : (c1 >= 0 && c2 >= 0);
    int32_t d2 = dx * dx + dy * dy;
    return in_sweep && d2 <= outer * outer && (!inner || d2 >= inner * inner);
}

/* Annular sweep. Vectors use screen coordinates; selection matches the
 * previous minor/major arc orientation, with clipped binary cells. */
static void br_shape_arc(int32_t cxq8, int32_t cyq8,
                         uint32_t radiusq8, uint32_t widthq8,
                         int32_t start_xq8, int32_t start_yq8,
                         int32_t end_xq8, int32_t end_yq8,
                         uint32_t large_arc, uint16_t color)
{
    int32_t cx, cy, radius, halfwidth, outer, inner;
    int32_t sx, sy, ex, ey, xa, xb, ya, yb, x, y;
    if (!cv_w || !cv_h || !widthq8) return;
    cx = br_shape_round_center_q4(br_shape_q8_to_q4(cxq8));
    cy = br_shape_round_center_q4(br_shape_q8_to_q4((int64_t)cyq8 + (int64_t)cv_oy * 256));
    sx = br_shape_q8_to_q4(start_xq8); sy = br_shape_q8_to_q4(start_yq8);
    ex = br_shape_q8_to_q4(end_xq8); ey = br_shape_q8_to_q4(end_yq8);
    if ((!sx && !sy) || (!ex && !ey)) return;
    radius = br_shape_q8_to_q4(radiusq8);
    halfwidth = (int32_t)(br_shape_cell_width_q4(widthq8) / 2u);
    outer = radius + halfwidth;
    inner = radius - halfwidth;
    if (inner < 0) inner = 0;
    xa = br_shape_bound_cell_low(cx - outer, cv_w);
    xb = br_shape_bound_cell_high(cx + outer, cv_w);
    ya = br_shape_bound_cell_low(cy - outer, cv_h);
    yb = br_shape_bound_cell_high(cy + outer, cv_h);
    if (xa > xb || ya > yb) return;
    for (y = ya; y <= yb; ++y) {
        int32_t py = br_shape_cell_center_q4(y);
        for (x = xa; x <= xb; ++x) {
            if (br_shape_arc_cell(br_shape_cell_center_q4(x), py, cx, cy,
                                  sx, sy, ex, ey, outer, inner, large_arc != 0u))
                br_shape_put_cell(x, y, color);
        }
    }
}

