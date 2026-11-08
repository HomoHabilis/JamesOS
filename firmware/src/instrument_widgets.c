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
};
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
static __attribute__((noinline)) void bw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    uint32_t width, uint16_t color)
{
    if (width > 512u)
        br_shape_line(bw_q8(x0), bw_q8(y0), bw_q8(x1), bw_q8(y1), width, color);
    else
        br_art_line(x0, y0, x1, y1, color);
}
static int32_t bw_pixel_from_q8(int32_t value)
{
    return value >= 0 ? (value + 128) / 256 : (value - 128) / 256;
}
static __attribute__((noinline)) void bw_qline(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t width, uint16_t color)
{
    if (width > 512u)
        br_shape_line(x0, y0, x1, y1, width, color);
    else
        br_art_line(bw_pixel_from_q8(x0), bw_pixel_from_q8(y0),
                    bw_pixel_from_q8(x1), bw_pixel_from_q8(y1), color);
}
static __attribute__((noinline)) void bw_circle(int32_t cx, int32_t cy, int32_t r, uint16_t color, uint32_t fill)
{
    br_shape_circle(bw_q8(cx), bw_q8(cy), (uint32_t)(r > 0 ? r : 1) * 256u, 384u, color, fill);
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
    int32_t sx = bw_sine(start), sy = -bw_sine(start + 16384u);
    int32_t ex = bw_sine(start + sweep), ey = -bw_sine(start + sweep + 16384u);
    if (!sweep) return;
    br_shape_arc(cx, cy, radius, width, sx, sy, ex, ey, sweep > 32768u, color);
}
static void bw_quadratic(int32_t x0, int32_t y0, int32_t cx, int32_t cy,
                         int32_t x1, int32_t y1, uint32_t width, uint16_t color)
{
    int32_t px = x0, py = y0;
    for (int32_t i = 1; i <= 16; ++i) {
        int32_t a = 16 - i;
        int32_t nx = (a * a * x0 + 2 * a * i * cx + i * i * x1) / 256;
        int32_t ny = (a * a * y0 + 2 * a * i * cy + i * i * y1) / 256;
        bw_qline(px, py, nx, ny, width, color);
        px = nx; py = ny;
    }
}
/* Fill convex faces by complete 2x2 cells, sampling each cell center. */
static __attribute__((noinline)) void bw_face(const int32_t px[4], const int32_t py[4], uint16_t color)
{
    int32_t top = py[0], bottom = py[0];
    int32_t first_y;
    for (uint32_t i = 1; i < 4u; ++i) {
        if (py[i] < top) top = py[i];
        if (py[i] > bottom) bottom = py[i];
    }
    first_y = br_shape_floor_div(top + cv_oy, 2) * 2 - cv_oy;
    for (int32_t y = first_y; y + 1 < bottom; y += 2) {
        int32_t left = 100000, right = -100000, scan = y * 256 + 256;
        for (uint32_t i = 0; i < 4u; ++i) {
            uint32_t j = (i + 1u) & 3u;
            int32_t a = py[i] * 256, b = py[j] * 256;
            if (a == b || !((scan >= a && scan < b) || (scan >= b && scan < a))) continue;
            int32_t at = px[i] * 256 + (scan - a) * (px[j] - px[i]) / (py[j] - py[i]);
            if (at < left) left = at;
            if (at > right) right = at;
        }
        if (left <= right) {
            int32_t first = (left + 255) / 512;
            int32_t end = (right + 255) / 512;
            if (end > first) br_art_rect(first * 2, y, (end - first) * 2, 2, color);
        }
    }
}

#include "james_font.h"

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
    uint32_t radius = s * 95u, width = bw_weight(s), start = 40960u;
    uint32_t sweep = q * 49152u / 127u;
    int32_t ax, ay, bx, by;
    br_shape_circle(cx, cy, s * 66u, 0, RGB(19,24,26), 1);
    bw_arc(cx, cy, radius, width, start, 49152u, BR_LINE);
    if (sweep) bw_arc(cx, cy, radius, width, start, sweep, BR_ACC);
    bw_point(cx, cy, s * 24u, start + sweep, &ax, &ay);
    bw_point(cx, cy, s * 57u, start + sweep, &bx, &by);
    bw_qline(ax, ay, bx, by, width, C_WHITE);
}

/* A knob that rests at twelve o'clock: the lit arc runs from the top to the pointer, to the right
 * for positive values and to the left for negative ones. */
static void bw_pot_bipolar(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t cx = bw_q8(x) + (int32_t)s * 128, cy = bw_q8(y) + (int32_t)s * 128;
    uint32_t radius = s * 95u, width = bw_weight(s), start = 40960u, mid = start + 24576u;
    uint32_t at = start + q * 49152u / 127u;
    int32_t ax, ay, bx, by;
    br_shape_circle(cx, cy, s * 66u, 0, RGB(19,24,26), 1);
    bw_arc(cx, cy, radius, width, start, 49152u, BR_LINE);
    if (q > 64u) bw_arc(cx, cy, radius, width, mid, at - mid, BR_ACC);
    else if (q < 63u) bw_arc(cx, cy, radius, width, at, mid - at, BR_ACC);
    bw_point(cx, cy, s * 100u, mid, &ax, &ay);
    bw_point(cx, cy, s * 118u, mid, &bx, &by);
    bw_qline(ax, ay, bx, by, width, BR_DIM);                    /* the zero mark */
    bw_point(cx, cy, s * 24u, at, &ax, &ay);
    bw_point(cx, cy, s * 57u, at, &bx, &by);
    bw_qline(ax, ay, bx, by, width, C_WHITE);
}

/* Compressor source: an off symbol, the four track colours for ALL, or one track's own badge
 * (T1..T3, and DK for the kick drum). */
static void bw_comp_source(int32_t x, int32_t y, uint32_t s, uint32_t source)
{
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    if (source == BR_COMP_OFF) {
        bw_circle(cx, cy, 11, BR_DIM, 0u);
        bw_line(cx - 8, cy + 8, cx + 8, cy - 8, 640u, BR_DIM);
    } else if (source == BR_COMP_ALL) {
        int32_t x0 = (cx - 14) & ~1, y0 = (cy - 10) & ~1;
        br_art_rect(x0, y0, 12, 8, br_track_color(0u));
        br_art_rect(x0 + 16, y0, 12, 8, br_track_color(1u));
        br_art_rect(x0, y0 + 12, 12, 8, br_track_color(2u));
        br_art_rect(x0 + 16, y0 + 12, 12, 8, br_track_color(BR_DRUM_PART));
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
    int32_t half = (int32_t)s * 2 / 15;
    int32_t ticker = 0;
    uint32_t radius = s * 95u;
    const uint32_t start = 38229u, sweep = 54613u; /* 7 o'clock to 5 o'clock */
    bw_arc(bw_q8(cx), bw_q8(cy), radius, 512u, start, sweep, BR_LINE);
    if (q) bw_arc(bw_q8(cx), bw_q8(cy), radius, 512u, start, q * sweep / 127u, BR_ACC);
    br_art_line(cx - half, base, cx + half, base, BR_ACC);
    br_art_line(cx - half, base, cx - 4, top, BR_ACC);
    br_art_line(cx - 4, top, cx + 4, top, BR_ACC);
    br_art_line(cx + 4, top, cx + half, base, BR_ACC);
    if (phase) {
        uint32_t encoded = phase - 1u;
        ticker = br_metronome_swing((encoded & 15u) * 8u, encoded >> 4) * 4 / 32;
    }
    br_art_line(cx, base - 4, cx + ticker, top + 4, C_WHITE);
    bw_circle(cx + ticker / 2, cy - 4, 2, C_WHITE, 1u);
}

static void bw_fader(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t cx = x + (int32_t)s / 2, top = y + (int32_t)s / 6, bottom = y + (int32_t)s * 5 / 6;
    int32_t handle = bottom - ((int32_t)q * (bottom - top) + 63) / 127;
    uint32_t weight = bw_weight(s);
    bw_line(cx, top, cx, bottom, weight, BR_LINE);
    if (q) bw_line(cx, bottom, cx, handle, weight, BR_ACC);
    for (int32_t i = 0; i <= 2; ++i) {
        int32_t ty = top + i * (bottom - top) / 2;
        br_art_rect(cx - (int32_t)s / 5, ty, 4, 2, BR_LINE);
    }
    br_art_rect(cx - (int32_t)s / 7, handle - (s >= 48u ? 3 : 2),
                2 * ((int32_t)s / 7), s >= 48u ? 6 : 4, C_WHITE);
}

static void bw_percent(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t left = x + 6, right = x + (int32_t)s - 6, mid = y + (int32_t)s / 2;
    uint32_t width = s >= 48u ? 1536u : 768u;
    bw_line(left, mid, right, mid, width, BR_LINE);
    if (q) {
        int32_t at = bw_q8(left) + (int32_t)q * (right - left) * 256 / 127;
        bw_qline(bw_q8(left), bw_q8(mid), at, bw_q8(mid), width, BR_ACC);
    }
}

static void bw_toggle(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t radius = (int32_t)s / 6, cy = y + (int32_t)s / 2;
    int32_t left = x + (int32_t)s / 4, right = x + (int32_t)s * 3 / 4;
    int32_t cx = left + (int32_t)q * (right - left) / 127;
    bw_line(left, cy, right, cy, (uint32_t)radius * 512u, q ? bw_tone(BR_ACC, 170u) : BR_LINE);
    br_shape_circle(bw_q8(cx), bw_q8(cy), (uint32_t)(radius > 2 ? radius - 2 : 1) * 256u, 0, C_WHITE, 1);
}

/* The CPU monitor switch: one meter bar in the colour of the screen overlay (cyan), full colour when on and
 * grey when off. */
static void bw_monitor(int32_t x, int32_t y, uint32_t s, uint32_t on)
{
    int32_t left = (x + (int32_t)s / 2 - 17) & ~1, top = (y + (int32_t)s / 2 - 8) & ~1;
    br_art_rect(left, top, 34, 16, RGB(24, 30, 32));
    br_art_rect(left, top, on ? 26 : 6, 16, on ? RGB(0, 200, 230) : BR_LINE);
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

static void bw_delay(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase,
                     uint32_t note_index)
{
    int32_t cx = bw_q8(x) + (int32_t)s * 128, cy = bw_q8(y) + (int32_t)s * 128;
    uint32_t maxr = s * 103u, base = s * 28u;
    int32_t head_x = x + (int32_t)s / 2 - (int32_t)s / 10;
    int32_t head_y = y + (int32_t)s * 2 / 5;
    int32_t stem_top = y + (int32_t)s / 5;
    static const char *const label[11] = {
        "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4",
        "1", "1/2", "2", "3", "4"
    };
    uint32_t flags = note_index == 0u ? 3u :
                     note_index == 1u || note_index == 2u ? 2u :
                     note_index == 3u || note_index == 4u ? 1u : 0u;
    note_index = note_index < 11u ? note_index : 10u;
    /* Three restrained echoes expand from one source. Coverage carries the
     * curvature; brightness falls with distance instead of extra fine rings. */
    for (int32_t i = 2; i >= 0; --i) {
        uint32_t at = (phase + (uint32_t)i * 43u) & 127u;
        uint32_t radius = base + at * (maxr - base) / 127u;
        uint32_t shade = (255u - at * 160u / 127u) * (100u + q) / 227u;
        br_shape_circle(cx, cy, radius, bw_weight(s), bw_tone(BR_ACC, shade), 0);
    }
    br_shape_circle(cx, cy, s >= 48u ? 704u : 384u, 0, C_WHITE, 1);
    /* A small note glyph makes the selected tempo division visible in the
     * artwork itself; the textual value below the main card stays unchanged. */
    bw_circle(head_x, head_y, s >= 48u ? 3 : 2, C_WHITE, 1u);
    bw_line(head_x + (int32_t)s / 12, head_y, head_x + (int32_t)s / 12,
            stem_top, s >= 48u ? 512u : 256u, C_WHITE);
    if (flags > 0u) {
        uint32_t f;
        for (f = 0; f < flags; ++f)
            bw_line(head_x + (int32_t)s / 12, stem_top + (int32_t)f * (int32_t)s / 14,
                    head_x + (int32_t)s / 5, stem_top + (int32_t)f * (int32_t)s / 14 + (int32_t)s / 10,
                    256u, BR_ACC);
    }
    if (note_index == 1u || note_index == 3u)
        bw_circle(head_x + (int32_t)s / 4, head_y, 1, BR_ACC, 1u);
    if (s >= 48u) {
        int32_t width = br_widget_text_width(label[note_index]);
        br_widget_text(x + ((int32_t)s - width) / 2,
                       y + (int32_t)s * 3 / 4, label[note_index], C_WHITE);
    }
}

static void bw_delay_feedback(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    uint32_t count = q * 4u / 128u;
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    uint32_t i;
    /* Give the feedback rings a visible point of origin, including at zero. */
    bw_circle(cx, cy, 2, C_WHITE, 1u);
    for (i = 0; i < count; ++i) {
        uint32_t shade = 112u + i * 60u;
        /* Three rings must stay well inside the 60 px control card. */
        uint32_t radius = s * (17u + i * 10u) / 100u;
        br_shape_circle(bw_q8(cx), bw_q8(cy), radius * 256u, bw_weight(s),
                        bw_tone(BR_ACC, shade), 0u);
    }
}

/* Both strands share a small phase shift as the mix knob turns. */
static int32_t bw_mix_wave(uint32_t column, uint32_t columns, uint32_t mix)
{
    return bw_sine(16384u + column * 81920u / columns + mix * 64u);
}

/* Two interwoven strands converge at the output. Their relative brightness
 * follows the mix; the brighter strand is drawn on top at each crossing. */
static void bw_mix(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    int32_t left = (x + (int32_t)s / 6) & ~1;
    int32_t right = (x + (int32_t)s * 5 / 6) & ~1;
    int32_t cy = (y + (int32_t)s / 2) & ~1;
    int32_t columns = (right - left) / 2;
    int32_t amplitude = (int32_t)s / 12;
    uint16_t upper = bw_tone(BR_ACC, 90u + q * 165u / 127u);
    uint16_t lower = bw_tone(BR_ACC, 90u + (127u - q) * 165u / 127u);
    if (columns < 1) columns = 1;
    if (amplitude < 1) amplitude = 1;
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        uint32_t strand = (q >= 64u) ? 1u - pass : pass;
        uint16_t color = strand ? lower : upper;
        int32_t previous = cy / 2;
        for (int32_t i = 0; i <= columns; ++i) {
            int32_t wave = bw_mix_wave((uint32_t)i, (uint32_t)columns, q);
            int32_t offset = wave * amplitude * (columns - i) / (16384 * columns);
            int32_t row = cy / 2 + (strand ? offset : -offset);
            int32_t from = i ? previous : row;
            int32_t first = from < row ? from : row, last = from > row ? from : row;
            for (int32_t at = first; at <= last; ++at)
                br_art_rect(left + i * 2, at * 2, 2, 2, color);
            previous = row;
        }
    }
    /* The knob positions the glints; playback phase moves their sparkle. */
    if (columns > 3) {
        uint32_t sparks = s >= 48u ? 3u : 2u;
        for (uint32_t spark = 0; spark < sparks; ++spark) {
            int32_t i = 2 + (int32_t)((q + phase + spark * 7u) % (uint32_t)(columns - 2));
            int32_t wave = bw_mix_wave((uint32_t)i, (uint32_t)columns, q);
            int32_t offset = wave * amplitude * (columns - i) / (16384 * columns);
            int32_t at_x = left + i * 2;
            uint32_t twinkle = (q + phase + spark * 3u) & 7u;
            uint32_t brightness = twinkle == 0u ? 255u :
                (twinkle == 1u || twinkle == 7u) ? 195u : 125u;
            uint16_t color = bw_tone(bw_highlight(1u), brightness);
            br_art_rect(at_x, cy - offset * 2, 2, 2, color);
            br_art_rect(at_x, cy + offset * 2, 2, 2, color);
        }
    }
}

/* Intensity/amount reservoir: a one-pixel background gutter separates the
 * fill from each wall. The animated highlight stays inside the fill. */
static void bw_amount(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    int32_t cx = x + (int32_t)s / 2, hw = ((int32_t)s / 6 + 2) & ~1;
    int32_t top = (y + (int32_t)s / 6) & ~1, bottom = (y + (int32_t)s * 5 / 6) & ~1;
    int32_t in_left = cx - hw + 3, in_w = 2 * hw - 6, in_top = top + 2, in_bottom = bottom - 2;
    int32_t in_h = in_bottom - in_top, fill = (int32_t)q * in_h / 127 & ~1, fill_top = in_bottom - fill;
    br_art_rect(cx - hw, top, 2, bottom - top, BR_LINE);                 /* walls */
    br_art_rect(cx + hw - 2, top, 2, bottom - top, BR_LINE);
    br_art_rect(cx - hw, top, 2 * hw, 2, BR_LINE);                       /* lid */
    br_art_rect(cx - hw, bottom - 2, 2 * hw, 2, BR_LINE);                /* base */
    if (fill && in_w > 0) cv_rect(in_left, fill_top, in_w, fill, BR_ACC);
    for (int32_t i = 1; i < 4; ++i) {
        int32_t mark = (bottom - (bottom - top) * i / 4) & ~1;
        br_art_rect(cx - hw - 4, mark, 4, 2, BR_DIM);                    /* scale, both sides */
        br_art_rect(cx + hw, mark, 4, 2, BR_DIM);
    }
    if (phase && q && fill > 2) {
        int32_t gw = s >= 48u ? 8 : 4;
        if (gw > in_w) gw = in_w;
        int32_t gx = in_left + (in_w - gw) / 2;
        int32_t travel = fill - 2;
        int32_t gy = (in_bottom - 2 - (int32_t)(phase & 31u) * travel / 31) & ~1;
        if (gw > 0) cv_rect(gx, gy, gw, 2, C_WHITE);
    }
}

/* A labeled stereo rail makes the position and direction unambiguous. */
static void bw_pan_rail(int32_t x, int32_t y, uint32_t s, uint32_t q, uint16_t color)
{
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t center = x + (int32_t)s / 2;
    int32_t cy = y + (int32_t)s / 2;
    int32_t puck = (left + ((int32_t)q * (right - left) + 63) / 127) & ~1;
    br_widget_text(left - 2, y + (int32_t)s / 5, "L", BR_DIM);
    br_widget_text(right - 2, y + (int32_t)s / 5, "R", BR_DIM);
    bw_line(left, cy, right, cy, 512u, BR_LINE);
    cv_rect(center - 1, cy - 8, 2, 16, BR_DIM);          /* two pixels wide, centred: neither side looks favoured */
    bw_line(center, cy, puck, cy, 512u, color);
    br_art_rect(puck - 4, cy - 6, 8, 12, color);
    br_art_rect(puck - 2, cy - 4, 4, 8, C_WHITE);
}
static void bw_track_pan(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t track)
{
    bw_pan_rail(x, y, s, q, br_track_color(track));
}

static void bw_heat(int32_t x, int32_t y, uint32_t s, uint32_t type, uint32_t phase)
{
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    if (type == BR_HEAT_BIT) {
        /* BIT: a descending digital staircase. */
        for (uint32_t i = 0u; i < 4u; ++i) {
            int32_t a = left + (right - left) * (int32_t)i / 4;
            int32_t b = left + (right - left) * (int32_t)(i + 1u) / 4;
            int32_t level = cy - 12 + (int32_t)i * 8;
            bw_line(a, level, b, level, 512u, i == phase / 32u ? C_WHITE : BR_ACC);
            if (i < 3u) bw_line(b, level, b, level + 8, 512u, BR_ACC);
        }
    } else if (type == BR_HEAT_SAT) {
        /* SAT: a soft-clipped transfer curve between two limit rails. */
        bw_line(left, cy - 14, right, cy - 14, 512u, BR_LINE);
        bw_line(left, cy + 14, right, cy + 14, 512u, BR_LINE);
        bw_quadratic(bw_q8(left), bw_q8(cy + 12), bw_q8(left + 12), bw_q8(cy + 12),
                     bw_q8(cx), bw_q8(cy), 512u, BR_ACC);
        bw_quadratic(bw_q8(cx), bw_q8(cy), bw_q8(right - 12), bw_q8(cy - 12),
                     bw_q8(right), bw_q8(cy - 12), 512u, C_WHITE);
    } else {
        /* WOW: a tape spindle above a slowly wandering pitch ribbon. */
        bw_circle(x + (int32_t)s / 2, y + (int32_t)s / 3, 7, BR_ACC, 0u);
        bw_circle(x + (int32_t)s / 2, y + (int32_t)s / 3, 2, C_WHITE, 1u);
        for (int32_t i = 0; i < 5; ++i) {
            int32_t a = left + i * (right - left) / 5;
            int32_t b = left + (i + 1) * (right - left) / 5;
            int32_t ya = cy + 10 + bw_sine((uint32_t)(i * 11000 + phase * 512)) / 4096;
            int32_t yb = cy + 10 + bw_sine((uint32_t)((i + 1) * 11000 + phase * 512)) / 4096;
            bw_line(a, ya, b, yb, 512u, BR_ACC);
        }
    }
}

static void bw_mod_atten(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t kind,
                         uint32_t phase)
{
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    int32_t marker = cx + ((int32_t)q - 64) * (int32_t)s / 190;
    const char *label = kind == 0u ? "T" : kind == 1u ? "P" : kind == 3u ? "F" : "M";
    bw_line(x + 8, cy, x + (int32_t)s - 8, cy, 512u, BR_LINE);
    bw_line(cx, cy - 9, cx, cy + 9, 512u, BR_DIM);
    bw_line(cx, cy, marker, cy, 768u, BR_ACC);
    bw_circle(marker, cy, 4, C_WHITE, 1u);
    br_widget_text(cx - br_widget_text_width(label) / 2, y + 7, label, BR_ACC);
    if (phase && q != 64u) {
        int32_t pulse = marker + ((int32_t)(phase & 7u) - 4) *
            (q < 64u ? -1 : 1);
        bw_circle(pulse, cy + 11, 1, BR_ACC, 1u);
    }
}

static void bw_pan(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    bw_pan_rail(x, y, s, q, BR_ACC);
}

static void bw_adsr_graph(int32_t x, int32_t y, uint32_t w, uint32_t h,
                          const uint32_t env[4], uint32_t stage)
{
    int32_t left = x + 5, right = x + (int32_t)w - 5, top = y + 7, base = y + (int32_t)h - 7;
    int32_t p[5], py[5], a, d, release;
    uint32_t expanded = w > 64u;
    if (!env) return;
    a = 3 + (int32_t)bw_clamp(env[0],127u) * ((int32_t)w / 4 - 3) / 127;
    d = 3 + (int32_t)bw_clamp(env[1],127u) * ((int32_t)w / 5 - 3) / 127;
    release = 3 + (int32_t)bw_clamp(env[3],127u) * ((int32_t)w / 4 - 3) / 127;
    p[0]=left; p[1]=left+a; p[2]=p[1]+d; p[3]=right-release; p[4]=right;
    if (p[2] > p[3]) { p[2]=(p[1]+right)/2; p[3]=(p[2]+right)/2; }
    py[0]=base; py[1]=top; py[2]=base-(int32_t)bw_clamp(env[2],127u)*(base-top)/127;
    py[3]=py[2]; py[4]=base;
    bw_line(left, base, right, base, 512u, BR_LINE);
    for (uint32_t i=0; i<4u; ++i) {
        uint16_t color = expanded ? (i == stage ? C_WHITE : BR_ACC) : (stage >= 4u || i == stage) ? BR_ACC : BR_DIM;
        uint32_t width = expanded || stage >= 4u || i == stage ? bw_weight(w) : 320u;
        if (i == 1u || i == 3u)
            bw_quadratic(bw_q8(p[i]),bw_q8(py[i])-128,bw_q8((p[i]+p[i+1])/2),bw_q8(py[i+1])-128,
                          bw_q8(p[i+1]),bw_q8(py[i+1])-128,width,color);
        else bw_line(p[i],py[i],p[i+1],py[i+1],width,color);
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
    br_art_rect(left, beam, right - left + 2, 2, BR_ACC);
    br_art_rect(left, beam + 4, right - left + 2, 2, BR_ACC);
    for (uint32_t i = 0; i < 3u; ++i) {
        br_art_line(stems[i], beam, stems[i], bottom, BR_ACC);
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
    br_art_rect(x + 6, cy, (int32_t)s - 12, 2, BR_LINE);
    static const uint8_t heights[12] = {14, 8, 18, 10, 16, 6, 14, 8, 12, 6, 8, 6};
    for (uint32_t i = 0u; i < half; ++i) {
        uint32_t edge = half - 1u - i;
        uint32_t motion = (amount >> 4) % 12u;
        int32_t height = heights[(i + motion) % 12u];
        uint32_t tone = edge == 0u ? 110u : edge == 1u ? 165u : (i & 1u) ? 155u : 215u;
        uint16_t color = bw_tone(BR_ACC, tone + amount * 20u / 127u);
        int32_t yy = (cy - height / 2) & ~1;
        br_art_rect(center - 2 - (int32_t)i * 2, yy, 2, height, color);
        br_art_rect(center + (int32_t)i * 2, yy, 2, height, color);
        if (!i || ((i + motion) % 3u == 0u && edge > 0u)) {
            uint16_t light = bw_highlight(i == 0u);
            br_art_rect(center - 2 - (int32_t)i * 2, yy + 2, 2, 2, light);
            br_art_rect(center + (int32_t)i * 2, yy + 2, 2, 2, light);
        }
    }
}

static void bw_filter(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t left=bw_q8(x+5), right=bw_q8(x+(int32_t)s-5);
    int32_t top=bw_q8(y+9), base=bw_q8(y+(int32_t)s-7), px=left, py=top;
    int32_t cutoff = q < 63u ? (int32_t)q * 127 / 62 : ((int32_t)q - 65) * 127 / 62;
    bw_line(x+5,y+(int32_t)s-7,x+(int32_t)s-5,y+(int32_t)s-7,512u,BR_LINE);
    for (int32_t i=0; i<=32; ++i) {
        int32_t nx=left+i*(right-left)/32, ny;
        if (q==63u || q==64u) ny=top;
        else {
            int32_t distance=i*127/32-cutoff;
            int32_t magnitude=distance<0?-distance:distance;
            int32_t response=distance*96/(magnitude+14);
            int32_t level=(response+96)*127/192;
            if (q<63u) level=127-level;
            ny=base-level*(base-top)/127;
        }
        if (i) bw_qline(px,py,nx,ny,bw_weight(s),BR_ACC);
        px=nx; py=ny;
    }
}

/* Filter response with a variable resonant peak at the cutoff. */
static void bw_resonance(int32_t x, int32_t y, uint32_t s, uint32_t q)
{
    int32_t left=x+5, right=x+(int32_t)s-5, top=y+9, base=y+(int32_t)s-7;
    int32_t px=left, py=base-(base-top)*2/5;
    bw_line(left,base,right,base,512u,BR_LINE);
    for (int32_t i=1;i<=32;++i) {
        int32_t distance=i-18; if(distance<0)distance=-distance;
        int32_t peak=(int32_t)q * 60 / (distance*distance+60);
        int32_t level=i<19 ? 50+peak*65/127 : (50+peak*65/127)*(32-i)/13;
        int32_t nx=left+i*(right-left)/32, ny=base-level*(base-top)/127;
        bw_line(px,py,nx,ny,bw_weight(s),BR_ACC);px=nx;py=ny;
    }
}

/* Fixed selector illustrations use the same response equation as the HOME
 * filter graph. Resonance adds only a peak at the same cutoff. */
static void bw_filter_target(int32_t x, int32_t y, uint32_t s, uint32_t resonance)
{
    int32_t left=bw_q8(x+5), right=bw_q8(x+(int32_t)s-5);
    int32_t top=bw_q8(y+9), base=bw_q8(y+(int32_t)s-7), px=left, py=top;
    const int32_t cutoff=35*127/62;
    bw_line(x+5,y+(int32_t)s-7,x+(int32_t)s-5,y+(int32_t)s-7,512u,BR_LINE);
    for (int32_t i=0;i<=64;++i) {
        int32_t distance=i*127/64-cutoff, magnitude=distance<0?-distance:distance;
        int32_t response=distance*96/(magnitude+14);
        int32_t level=90-(response+96)*90/192;
        if (resonance) level += 32/(1+magnitude*magnitude/90);
        if (level>127) level=127;
        int32_t nx=left+i*(right-left)/64, ny=base-level*(base-top)/127;
        if (i) bw_qline(px,py,nx,ny,bw_weight(s),BR_ACC);
        px=nx; py=ny;
    }
}

static void bw_wave(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant, uint32_t phase)
{
    int32_t left=bw_q8(x+5), right=bw_q8(x+(int32_t)s-5), mid=bw_q8(y+(int32_t)s/2);
    int32_t amp=(int32_t)(q*s*80u/127u), px=left, py=mid;
    bw_line(x+5,y+(int32_t)s/2,x+(int32_t)s-5,y+(int32_t)s/2,512u,BR_LINE);
    if (variant==4u) {
        static const int8_t hold[8]={6,-10,12,-4,3,-13,9,-2};
        for (uint32_t i=0;i<8u;++i) {
            int32_t nx=left+(int32_t)(i+1u)*(right-left)/8;
            int32_t ny=mid-(int32_t)hold[(i+phase/16u)&7u]*amp/15;
            if (i) bw_qline(px,py,px,ny,bw_weight(s),BR_ACC);
            bw_qline(px,ny,nx,ny,bw_weight(s),BR_ACC);
            px=nx;py=ny;
        }
        return;
    }
    for (int32_t i=0;i<=32;++i) {
        uint32_t t=((uint32_t)i*2048u+phase*512u)&65535u;
        int32_t v;
        if (variant==1u) v=t<32768u?16384-(int32_t)t: (int32_t)t-49152;
        else if (variant==2u) v=16384-(int32_t)t/2;
        else if (variant==3u) v=t<32768u?16384:-16384;
        else v=bw_sine(t);
        int32_t nx=left+i*(right-left)/32,ny=mid-v*amp/16384;
        if (i) bw_qline(px,py,nx,ny,bw_weight(s),BR_ACC);
        px=nx;py=ny;
    }
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
    int32_t px=left,py=bottom;
    bw_line(x+5,y+(int32_t)s-7,x+(int32_t)s-5,y+(int32_t)s-7,512u,BR_LINE);
    for(int32_t i=0;i<=32;++i) {
        int32_t nx=left+i*(right-left)/32;
        int32_t ny=bottom-(bw_drive_y((uint32_t)i,q,variant)+16)*(bottom-top)/32;
        if(i)bw_qline(px,py,nx,ny,bw_weight(s),BR_ACC);
        px=nx;py=ny;
    }
}

static void bw_fx_engine(int32_t x, int32_t y, uint32_t s, uint32_t variant)
{
    uint32_t family = (variant >> 8) & 3u, engine = variant & 255u;
    int32_t left = x + (int32_t)s / 5, right = x + (int32_t)s * 4 / 5;
    int32_t top = y + (int32_t)s / 4, bottom = y + (int32_t)s * 3 / 4;
    int32_t cx = x + (int32_t)s / 2, cy = y + (int32_t)s / 2;
    uint32_t weight = bw_weight(s), i;

    if (!family) {                              /* the three DRIVE modes */
        static const int8_t unit[12][2] = {{16, 0}, {14, 8}, {8, 14}, {0, 16}, {-8, 14}, {-14, 8},
                                           {-16, 0}, {-14, -8}, {-8, -14}, {0, -16}, {8, -14}, {14, -8}};
        int32_t amplitude = (int32_t)s / 5, px = left, py = cy;
        if (engine == 0u) {                       /* overdrive: a sine rounding off against soft rails */
            static const int8_t soft[13] = {0, 12, 15, 16, 15, 12, 0, -12, -15, -16, -15, -12, 0};
            bw_line(left, cy - amplitude - 3, right, cy - amplitude - 3, 256u, BR_LINE);
            bw_line(left, cy + amplitude + 3, right, cy + amplitude + 3, 256u, BR_LINE);
            for (i = 0; i < 13u; ++i) {
                int32_t nx = left + (int32_t)i * (right - left) / 12;
                int32_t ny = cy - soft[i] * amplitude / 16;
                if (i) bw_line(px, py, nx, ny, weight, BR_ACC);
                px = nx; py = ny;
            }
            bw_circle(left + 3 * (right - left) / 12, cy - amplitude, 2, C_WHITE, 1u);
            bw_circle(left + 9 * (right - left) / 12, cy + amplitude, 2, C_WHITE, 1u);
        } else if (engine == 1u) {                /* distortion: a squared-off wave with sparks at the corners */
            static const int8_t hard[13] = {0, 16, 16, 16, 16, 16, 0, -16, -16, -16, -16, -16, 0};
            for (i = 0; i < 13u; ++i) {
                int32_t nx = left + (int32_t)i * (right - left) / 12;
                int32_t ny = cy - hard[i] * amplitude / 12;
                if (i) bw_line(px, py, nx, ny, weight, BR_ACC);
                px = nx; py = ny;
            }
            for (i = 0; i < 4u; ++i) {            /* sparks at the four clipped corners */
                int32_t sx = left + (i == 0u ? 1 : i == 1u ? 5 : i == 2u ? 7 : 11) * (right - left) / 12;
                int32_t sy = cy + (i < 2u ? -1 : 1) * amplitude * 4 / 3;
                int32_t dir = i < 2u ? -1 : 1;
                bw_line(sx, sy, sx - 3, sy + dir * 3, 256u, C_WHITE);
                bw_line(sx, sy, sx + 3, sy + dir * 3, 256u, C_WHITE);
                bw_line(sx, sy, sx, sy + dir * 5, 256u, BR_DIM);
            }
        } else {                                  /* fuzz: a furry, spiky ball */
            int32_t inner = (int32_t)s / 8;
            for (i = 0; i < 12u; ++i) {
                int32_t reach = (i & 1u) ? (int32_t)s * 5 / 16 : (int32_t)s * 7 / 16;
                bw_line(cx + inner * unit[i][0] / 16, cy + inner * unit[i][1] / 16,
                        cx + reach * unit[i][0] / 16, cy + reach * unit[i][1] / 16,
                        weight, (i & 1u) ? BR_DIM : BR_ACC);
            }
            bw_circle(cx, cy, inner, BR_ACC, 1u);
            bw_circle(cx, cy, inner / 2, C_BLACK, 1u);
        }
        return;
    }
    if (family == 1u) {
        if ((engine & 3u) == 0u) {               /* clean digital repeats */
            bw_line(left, bottom, left, top, weight, BR_ACC);
            bw_line(left, top, cx, top, weight, BR_ACC);
            bw_line(cx, top, cx, y + (int32_t)s * 3 / 8, weight, BR_ACC);
            bw_line(cx, y + (int32_t)s * 3 / 8, right, y + (int32_t)s * 3 / 8,
                    weight, BR_ACC);
            bw_line(right, y + (int32_t)s * 3 / 8, right, bottom, weight, BR_DIM);
            bw_line(cx, y + (int32_t)s * 3 / 8, cx, bottom, 256u, BR_LINE);
        } else if ((engine & 3u) == 1u) {        /* tape reels and path */
            int32_t radius = (int32_t)s / 7;
            bw_line(left, cy, right, cy, weight, BR_LINE);
            bw_circle(left + radius, cy, radius, BR_ACC, 0u);
            bw_circle(right - radius, cy, radius, BR_ACC, 0u);
            bw_circle(left + radius, cy, (uint32_t)radius / 3u + 1u, BR_DIM, 1u);
            bw_circle(right - radius, cy, (uint32_t)radius / 3u + 1u, BR_DIM, 1u);
            bw_line(left + radius, cy - radius, right - radius, cy - radius,
                    256u, BR_ACC);
        } else if ((engine & 3u) == 2u) {        /* four taps, fading down */
            for (i = 0; i < 4u; ++i) {
                int32_t px = left + (int32_t)i * (right - left) / 3;
                int32_t py = top + (int32_t)i * (bottom - top) / 3;
                bw_line(px, top, px, py, weight, i ? BR_DIM : BR_ACC);
                bw_circle(px, py, (int32_t)s / (i + 9u), i ? BR_DIM : BR_ACC, 1u);
            }
        } else {                                  /* glitch steps */
            int32_t px = left, py = bottom;
            for (i = 1; i <= 8u; ++i) {
                int32_t nx = left + (int32_t)i * (right - left) / 8;
                int32_t ny = (i & 1u) ? top + (int32_t)s / 8 : bottom;
                bw_line(px, py, nx, py, weight, BR_ACC);
                bw_line(nx, py, nx, ny, weight, BR_ACC);
                px = nx; py = ny;
            }
        }
    } else if (family == 2u) {
        if (engine == 0u) {                       /* small room */
            int32_t back_left = cx - (int32_t)s / 7;
            int32_t back_right = cx + (int32_t)s / 7;
            int32_t back_top = cy - (int32_t)s / 7;
            int32_t back_bottom = cy + (int32_t)s / 7;
            /* A compact room in perspective: the inset back wall and floor
             * lines make this read as a space with depth, not a flat box. */
            bw_line(left, top, right, top, weight, BR_LINE);
            bw_line(left, top, left, bottom, weight, BR_ACC);
            bw_line(right, top, right, bottom, weight, BR_ACC);
            bw_line(left, bottom, right, bottom, weight, BR_LINE);
            bw_line(left, top, back_left, back_top, 256u, BR_DIM);
            bw_line(right, top, back_right, back_top, 256u, BR_DIM);
            bw_line(left, bottom, back_left, back_bottom, 256u, BR_DIM);
            bw_line(right, bottom, back_right, back_bottom, 256u, BR_DIM);
            bw_line(back_left, back_top, back_right, back_top, weight, BR_ACC);
            bw_line(back_left, back_top, back_left, back_bottom, weight, BR_ACC);
            bw_line(back_right, back_top, back_right, back_bottom, weight, BR_ACC);
            bw_line(back_left, back_bottom, back_right, back_bottom, weight, BR_ACC);
            bw_line(cx, bottom, cx, back_bottom, 256u, BR_LINE);
        } else if (engine == 1u) {                /* hall arch */
            uint32_t outer = s / 3u, inner = s / 6u;
            int32_t base = y + (int32_t)s * 3 / 5;
            int32_t floor = y + (int32_t)s * 4 / 5;
            bw_arc(bw_q8(cx), bw_q8(base), outer * 256u, weight,
                   49152u, 32768u, BR_ACC);
            bw_line(cx - (int32_t)outer, base, cx - (int32_t)outer, floor,
                    weight, BR_ACC);
            bw_line(cx + (int32_t)outer, base, cx + (int32_t)outer, floor,
                    weight, BR_ACC);
            bw_arc(bw_q8(cx), bw_q8(base), inner * 256u, 256u,
                   49152u, 32768u, BR_DIM);
            bw_line(cx - (int32_t)inner, base, cx - (int32_t)inner, floor,
                    256u, BR_DIM);
            bw_line(cx + (int32_t)inner, base, cx + (int32_t)inner, floor,
                    256u, BR_DIM);
            bw_line(cx - (int32_t)outer, floor, cx + (int32_t)outer, floor,
                    256u, BR_LINE);
            bw_line(cx - (int32_t)inner, floor, cx + (int32_t)inner, floor,
                    256u, BR_LINE);
        } else if (engine == 2u) {                /* spring coil */
            int32_t coil_left = left + (int32_t)s / 10;
            int32_t coil_right = right - (int32_t)s / 10;
            int32_t amplitude = (int32_t)s / 6;
            int32_t px = coil_left, py = cy;
            bw_line(left, top, left, bottom, weight, BR_DIM);
            bw_line(right, top, right, bottom, weight, BR_DIM);
            bw_line(left - 2, top, left + 2, top, weight, BR_ACC);
            bw_line(right - 2, bottom, right + 2, bottom, weight, BR_ACC);
            for (i = 1; i <= 8u; ++i) {
                int32_t nx = coil_left + (int32_t)i * (coil_right - coil_left) / 8;
                int32_t ny = cy + ((i & 1u) ? -amplitude : amplitude);
                bw_line(px, py, nx, ny, weight, BR_ACC);
                px = nx; py = ny;
            }
            bw_line(coil_left, cy, left, cy, 256u, BR_DIM);
            bw_line(coil_right, cy, right, cy, 256u, BR_DIM);
        } else if (engine == 3u) {                /* orbiting space */
            int32_t planet_x = cx + (int32_t)s / 5;
            int32_t planet_y = cy - (int32_t)s / 7;
            int32_t star_x = cx - (int32_t)s / 4;
            int32_t star_y = cy + (int32_t)s / 5;
            br_shape_circle(bw_q8(cx), bw_q8(cy), s * 256u / 3u,
                            weight, BR_ACC, 0u);
            br_shape_circle(bw_q8(cx), bw_q8(cy), s * 256u / 5u,
                            256u, BR_DIM, 0u);
            br_shape_circle(bw_q8(planet_x), bw_q8(planet_y),
                            s * 256u / 10u, 256u, C_WHITE, 1u);
            bw_line(star_x - 3, star_y, star_x + 3, star_y, 512u, BR_ACC);
            bw_line(star_x, star_y - 3, star_x, star_y + 3, 512u, BR_ACC);
            bw_line(star_x - 2, star_y - 2, star_x + 2, star_y + 2,
                    256u, BR_DIM);
        } else {                                  /* diffuse halo */
            int32_t upper_x = cx - (int32_t)s / 4, upper_y = cy - (int32_t)s / 4;
            int32_t lower_x = cx + (int32_t)s / 4, lower_y = cy + (int32_t)s / 4;
            bw_circle(cx, cy, (int32_t)s / 3, BR_ACC, 0u);
            bw_circle(cx, cy, (int32_t)s / 5, BR_DIM, 0u);
            bw_circle(cx, cy, (int32_t)s / 10, C_WHITE, 1u);
            bw_line(cx, top, cx, cy - (int32_t)s / 3, 256u, BR_LINE);
            bw_line(cx, cy + (int32_t)s / 3, cx, bottom, 256u, BR_LINE);
            bw_line(left, cy, cx - (int32_t)s / 3, cy, 256u, BR_LINE);
            bw_line(cx + (int32_t)s / 3, cy, right, cy, 256u, BR_LINE);
            bw_line(upper_x - 2, upper_y, upper_x + 2, upper_y, 512u, C_WHITE);
            bw_line(upper_x, upper_y - 2, upper_x, upper_y + 2, 512u, C_WHITE);
            bw_line(lower_x - 2, lower_y, lower_x + 2, lower_y, 512u, BR_ACC);
            bw_line(lower_x, lower_y - 2, lower_x, lower_y + 2, 512u, BR_ACC);
        }
    } else {
        if (engine == 0u || engine == 3u) {      /* chorus / vibrato waves */
            int32_t px0 = left, py0 = cy, px1 = left, py1 = cy;
            for (i = 1; i <= 24u; ++i) {
                int32_t nx = left + (int32_t)i * (right - left) / 24;
                int32_t p0 = bw_sine(i * 2731u);
                int32_t p1 = bw_sine(i * (engine ? 3072u : 2731u) + 8192u);
                int32_t ny0 = cy - p0 * (int32_t)s / 100000;
                int32_t ny1 = cy - p1 * (int32_t)s / 140000;
                bw_line(px0, py0, nx, ny0, weight, BR_ACC);
                if (!engine) bw_line(px1, py1, nx, ny1, 256u, BR_DIM);
                px0 = nx; py0 = ny0; px1 = nx; py1 = ny1;
            }
        } else if (engine == 1u) {                /* flange comb */
            for (i = 0; i < 5u; ++i) {
                int32_t px = left + (int32_t)i * (right - left) / 4;
                bw_line(px, top, px, bottom, i == 2u ? weight : 256u,
                        i == 2u ? BR_ACC : BR_DIM);
            }
            bw_line(left, cy, right, cy, weight, BR_ACC);
        } else {                                  /* phaser rings */
            bw_arc(bw_q8(cx), bw_q8(cy), s * 256u / 3u, weight, 8192u, 49152u, BR_ACC);
            bw_arc(bw_q8(cx), bw_q8(cy), s * 256u / 5u, 256u, 0u, 49152u, BR_DIM);
            bw_circle(cx, cy, 2, BR_ACC, 1u);
        }
    }
}
static void bw_choice(int32_t x,int32_t y,uint32_t s,uint32_t q,uint32_t options)
{
    int32_t cx=x+(int32_t)s/2,cy=y+(int32_t)s/2;
    uint32_t radius=s*22u/64u, inner=s*17u/64u;
    /* Match the arc's snapped center before positioning the marks. */
    cx=br_shape_round_center_q4(br_shape_q8_to_q4(bw_q8(cx)))/16;
    cy=br_shape_round_center_q4(br_shape_q8_to_q4(bw_q8(cy)))/16;
    options=options<2u?2u:options>8u?8u:options;
    uint32_t selected=q*options/128u;
    uint32_t sweep=49152u, start=40960u;
    if(selected>=options)selected=options-1u;
    /* A segmented selector dial gives the chosen position a clear pointer;
     * the value text below the artwork supplies the context-specific name. */
    bw_arc(bw_q8(cx),bw_q8(cy),radius*256u,512u,start,sweep,BR_LINE);
    if(selected)
        bw_arc(bw_q8(cx),bw_q8(cy),radius*256u,768u,start,
               sweep*selected/(options-1u),BR_ACC);
    for(uint32_t i=0;i<options;++i) {
        uint32_t at=start+sweep*i/(options-1u);
        int32_t ax,ay,bx,by;
        /* Short, one-cell-wide ticks sit inside the ring rather than
         * protruding across its outside edge. */
        bw_point(cx,cy,radius,at,&ax,&ay);
        bw_point(cx,cy,radius-5u,at,&bx,&by);
        bw_line(ax,ay,bx,by,512u,i==selected?C_WHITE:BR_DIM);
    }
    {
        int32_t px,py;
        uint32_t at=start+sweep*selected/(options-1u);
        bw_point(cx,cy,inner-2u,at,&px,&py);
        bw_line(cx,cy,px,py,512u,BR_ACC);
        bw_circle(cx,cy,s>=48u?3:2,C_WHITE,1u);
    }
}
/* Five-pin DIN connector and the actual channel, rather than a normalized dial. */
static void bw_midi_channel(int32_t x, int32_t y, uint32_t s, uint32_t channel)
{
    int32_t cx = x + (int32_t)s * 23 / 100, cy = y + (int32_t)s / 2;
    int32_t r = (int32_t)s * 19 / 100;
    int32_t unit = s >= 48u ? 2 : 1;
    int32_t outer_pin = ((r * 2 / 3 + 1) / 2) * 2;
    int32_t inner_pin = ((r / 2 + 1) / 2) * 2;
    int32_t number_left = x + (int32_t)s * 48 / 100;
    int32_t number_width = (int32_t)s - (number_left - x);
    char number[4];
    channel = channel > 16u ? 16u : channel;
    if (!channel) { number[0] = 'O'; number[1] = 'F'; number[2] = 'F'; number[3] = 0; unit = 1; }
    else if (channel >= 10u) { number[0] = '1'; number[1] = (char)('0' + channel - 10u); number[2] = 0; }
    else { number[0] = (char)('0' + channel); number[1] = 0; }
    /* Circles snap to a two-pixel grid; align the center and mirrored pin
     * offsets to that grid before drawing so both halves stay symmetric. */
    cx = br_shape_round_center_q4(br_shape_q8_to_q4(bw_q8(cx))) / 16 + 2;
    /* Move the connector toward the number and lift the pin group by one 2px grid cell. */
    bw_circle(cx, cy, r + (s >= 48u ? 2 : 1), BR_ACC, 0u);
    cv_rect(cx - 2, cy + r - 2, 4, 4, BR_ACC);
    /* A pin is one 2x2 pixel mark, centered on the connector grid. */
    cv_rect(cx - outer_pin - 1, cy + r / 4 - 3, 2, 2, C_WHITE);
    cv_rect(cx - inner_pin - 1, cy - r / 2 - 3, 2, 2, C_WHITE);
    cv_rect(cx - 1, cy - r * 2 / 3 - 3, 2, 2, C_WHITE);
    cv_rect(cx + inner_pin - 1, cy - r / 2 - 3, 2, 2, C_WHITE);
    cv_rect(cx + outer_pin - 1, cy + r / 4 - 3, 2, 2, C_WHITE);
    br_widget_text_scaled(number_left + (number_width - br_widget_text_width(number) * unit) / 2,
                          cy - 4 * unit, number, C_WHITE, unit);
}
static void bw_route(int32_t x,int32_t y,uint32_t s,uint32_t q,uint32_t active)
{
    int32_t left=x+8,right=x+(int32_t)s-8,cy=y+(int32_t)s/2;
    int32_t bend=(int32_t)q*((int32_t)s/4)/127;
    uint16_t color=active?BR_ACC:BR_LINE;
    bw_quadratic(bw_q8(left),bw_q8(cy),bw_q8((left+right)/2),bw_q8(cy-bend*2),
                  bw_q8(right),bw_q8(cy),bw_weight(s),color);
    bw_circle(left,cy,(int32_t)s/12,active?BR_DIM:BR_LINE,1);
    bw_circle(right,cy,(int32_t)s/12,active?BR_ACC:BR_LINE,1);
}

/* A track-colored ribbon folds more often as timbre rises. Each sample is one full
 * 2x2 column; phase moves the ribbon without changing its overall bounds. */
static void bw_timbre(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    uint32_t columns = s >= 48u ? 24u : 10u;
    uint32_t thick = 4u + (q * 4u / 127u / 2u) * 2u;
    int32_t span = (int32_t)columns * 2;
    int32_t left = x + ((int32_t)s - span) / 2;
    int32_t middle = y + (int32_t)s / 2;
    int32_t amplitude = s >= 48u ? (int32_t)s / 5 : 4;
    uint16_t back = bw_tone(BR_ACC, 92u);
    uint16_t body = bw_tone(BR_ACC, 150u + q * 95u / 127u);
    uint16_t front = bw_highlight(0u);
    int32_t previous_top = 0, previous_bottom = 0;

    for (uint32_t i = 0; i < columns; ++i) {
        uint32_t wave = (i * 65536u / columns + phase * 2048u) & 65535u;
        int32_t driven = bw_sine(wave) * (127 + (int32_t)q * 3) / 127;
        int32_t folded = (int32_t)((uint32_t)(driven + 16384) & 65535u);
        if (folded > 32768) folded = 65536 - folded;
        folded -= 16384;
        int32_t center = middle - folded * amplitude / 16384;
        int32_t top = center - (int32_t)thick / 2;
        int32_t bottom = top + (int32_t)thick - 2;
        int32_t at = left + (int32_t)i * 2;
        int32_t fill_top = i && previous_top < top ? previous_top : top;
        int32_t fill_bottom = i && previous_bottom > bottom ? previous_bottom : bottom;
        br_art_rect(at, fill_top, 2, fill_bottom - fill_top + 2, body);
        if (i) {
            bw_line(at - 2, previous_top, at, top, 512u, back);
            bw_line(at - 2, previous_bottom, at, bottom, 512u, front);
        } else {
            br_art_rect(at, top, 2, 2, back);
            br_art_rect(at, bottom, 2, 2, front);
        }
        previous_top = top; previous_bottom = bottom;
    }
}

/* A faceted prism separates an incoming beam into five spreading rays.
 * Little square glints travel along the rays; the crystal itself stays faceted. */
static void bw_color(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t phase)
{
    int32_t cx = x + (int32_t)s * 7 / 15;
    int32_t left = cx - (int32_t)s / 6;
    int32_t right = cx + (int32_t)s / 6;
    int32_t top = y + (int32_t)s / 4, bottom = y + (int32_t)s * 3 / 4;
    int32_t middle = (top + bottom) / 2;
    int32_t ray_end = x + (int32_t)s - 6;
    int32_t max_spread = (int32_t)s * 3 / 8;
    int32_t spread = 2 + (int32_t)q * (max_spread - 2) / 127;
    static const uint8_t ray_shade[5] = {105, 145, 190, 225, 255};
    uint16_t highlight = bw_highlight(0u);
    uint16_t glint = bw_highlight(1u);
    int32_t lpx[4] = {cx, left, cx, cx}, lpy[4] = {top, middle, bottom, middle};
    int32_t rpx[4] = {cx, right, cx, cx}, rpy[4] = {top, middle, bottom, middle};

    /* Incoming light and outgoing harmonics sit behind the prism. */
    bw_line(x + 6, middle, left, middle, 512u, highlight);
    for (uint32_t ray = 0; ray < 5u; ++ray) {
        int32_t offset = ((int32_t)ray - 2) * spread / 2;
        uint16_t shade = bw_tone(highlight, ray_shade[ray]);
        int32_t end_y = middle + offset;
        bw_line(right, middle, ray_end, end_y, 512u, shade);
        uint32_t t = (phase * 4u + ray * 23u) & 127u;
        int32_t glint_x = right + (ray_end - right) * (int32_t)t / 127;
        int32_t glint_y = middle + offset * (int32_t)t / 127;
        br_art_rect(glint_x & ~1, glint_y & ~1, 2, 2,
                    (ray & 1u) ? highlight : glint);
    }
    {
        uint32_t t = (phase * 4u) & 127u;
        int32_t glint_x = x + 6 + (left - (x + 6)) * (int32_t)t / 127;
        br_art_rect(glint_x & ~1, middle & ~1, 2, 2, glint);
    }

    bw_face(lpx, lpy, bw_tone(BR_ACC, 128u));
    bw_face(rpx, rpy, bw_tone(BR_ACC, 220u));
    bw_line(cx, top, left, middle, 512u, highlight);
    bw_line(left, middle, cx, bottom, 512u, bw_tone(BR_ACC, 170u));
    bw_line(cx, bottom, right, middle, 512u, bw_tone(BR_ACC, 195u));
    bw_line(right, middle, cx, top, 512u, glint);
    bw_line(left, middle, right, middle, 512u, bw_tone(BR_ACC, 115u));
    br_art_rect(cx & ~1, middle & ~1, 2, 2, glint);
}

/* A compact harmonic spectrum. The fundamental stays present while the
 * control opens more partials and raises their bars; a track-colored spark travels
 * across the active spectrum and each bar breathes slightly with phase. */
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
    bw_line(left - 2, base, left + span, base, 512u, BR_LINE);
    for (uint32_t i = 0; i < active; ++i) {
        uint32_t level;
        uint32_t wobble = (phase + i * 9u) & 31u;
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
        br_art_rect(at, base - height, 2, height, color);
        if (i == spark)
            br_art_rect(at, base - height - 2, 2, 2, spark_color);
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
        br_art_rect(nx + 4, ny - 12, 2, 10, color);
    }
}

static void bw_arp_rate(int32_t x, int32_t y, uint32_t s, uint32_t rate)
{
    uint32_t r = rate < 5u ? rate : 0u;
    uint32_t flags = r < 2u ? 1u : r < 4u ? 2u : 3u;
    int32_t hx = x + (int32_t)s / 2 - 8, hy = y + (int32_t)s * 3 / 4;
    int32_t stem = hx + 10, top = y + (int32_t)s / 5;
    /* A tilted rounded notehead, with a pale upper edge and shaded base. */
    cv_rect(hx + 3, hy - 5, 6, 1, bw_highlight(1u));
    cv_rect(hx + 1, hy - 4, 10, 2, bw_highlight(0u));
    cv_rect(hx, hy - 2, 12, 4, BR_ACC);
    cv_rect(hx + 1, hy + 2, 10, 2, bw_tone(BR_ACC, 165u));
    cv_rect(hx + 3, hy + 4, 6, 1, bw_tone(BR_ACC, 125u));
    br_art_rect(stem, top, 2, hy - top, bw_highlight(0u));
    for (uint32_t i = 0u; i < flags; ++i) {
        int32_t fy = top + (int32_t)i * 8;
        br_art_rect(stem + 2, fy, 4, 2, bw_highlight(0u));
        br_art_rect(stem + 6, fy + 2, 4, 2, BR_ACC);
        br_art_rect(stem + 10, fy + 4, 2, 4, bw_tone(BR_ACC, 165u));
    }
    if (r == 1u || r == 3u) br_widget_text(stem + 6, top - 9, "3", BR_DIM);
}

static void br_widget_draw(uint32_t kind, int32_t x, int32_t y, uint32_t size,
                           uint32_t q0_127, uint32_t variant,
                           const uint32_t env[4], uint32_t phase0_127)
{
    uint32_t q = bw_clamp(q0_127, 127u), phase = bw_clamp(phase0_127, 127u);
    size = size < 16u ? 16u : size > 64u ? 64u : size;
    switch (kind) {
    case BR_WIDGET_ARP_MODE: bw_arp_mode(x, y, size, variant); break;
    case BR_WIDGET_ARP_RATE: bw_arp_rate(x, y, size, variant); break;
    case BR_WIDGET_POT: bw_pot(x, y, size, q); break;
    case BR_WIDGET_POT_BIPOLAR: bw_pot_bipolar(x, y, size, q); break;
    case BR_WIDGET_COMP_SOURCE: bw_comp_source(x, y, size, variant); break;
    case BR_WIDGET_PATCH_OFF: bw_comp_source(x, y, size, BR_COMP_OFF); break;
    case BR_WIDGET_FILTER_TARGET: bw_filter_target(x, y, size, 0u); break;
    case BR_WIDGET_RESONANCE_TARGET: bw_filter_target(x, y, size, 1u); break;
    case BR_WIDGET_MONITOR: bw_monitor(x, y, size, variant); break;
    case BR_WIDGET_BPM: bw_bpm(x, y, size, q, phase); break;
    case BR_WIDGET_MIDI_CHANNEL: bw_midi_channel(x, y, size, variant); break;
    case BR_WIDGET_FADER: bw_fader(x, y, size, q); break;
    case BR_WIDGET_PERCENT: bw_percent(x, y, size, q); break;
    case BR_WIDGET_SWING: bw_swing(x, y, size, q); break;
    case BR_WIDGET_TOGGLE: bw_toggle(x, y, size, q); break;
    case BR_WIDGET_REVERB: bw_reverb(x, y, size, q); break;
    case BR_WIDGET_DELAY: bw_delay(x, y, size, q, phase, variant); break;
    case BR_WIDGET_DELAY_FEEDBACK: bw_delay_feedback(x, y, size, q); break;
    case BR_WIDGET_FX_ENGINE: bw_fx_engine(x, y, size, variant); break;
    case BR_WIDGET_PAN: bw_pan(x, y, size, q); break;
    case BR_WIDGET_ADSR: bw_adsr(x, y, size, env, variant); break;
    case BR_WIDGET_FILTER: bw_filter(x, y, size, q); break;
    case BR_WIDGET_RESONANCE: bw_resonance(x, y, size, q); break;
    case BR_WIDGET_GATE: bw_gate(x, y, size, q, variant != 0u); break;
    case BR_WIDGET_WAVE: bw_wave(x, y, size, q, variant, phase); break;
    case BR_WIDGET_CHOICE: bw_choice(x, y, size, q, variant); break;
    /* DRIVE is a retired saved ID; no active UI control emits it. */
    case BR_WIDGET_ROUTE: bw_route(x, y, size, q, variant != 0u); break;
    case BR_WIDGET_TIMBRE: bw_timbre(x, y, size, q, phase); break;
    case BR_WIDGET_COLOR: bw_color(x, y, size, q, phase); break;
    case BR_WIDGET_HARMONICS: bw_harmonics(x, y, size, q, phase); break;
    case BR_WIDGET_MIX: bw_mix(x, y, size, q, phase); break;
    case BR_WIDGET_AMOUNT: bw_amount(x, y, size, q, phase); break;
    case BR_WIDGET_TRACK_PAN: bw_track_pan(x, y, size, q, variant); break;
    case BR_WIDGET_HEAT: bw_heat(x, y, size, variant, phase); break;
    case BR_WIDGET_MOD_ATTEN: bw_mod_atten(x, y, size, q, variant, phase); break;
    default: break;
    }
}
