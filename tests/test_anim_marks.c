/* Also: br_canvas_shade (worked out per row and per cell pair) gives exactly the pixels of the original
 * per-pixel form (reference copy below) for every track background, row 0..127 and canvas position.
 *
 * And hd__radial (rows culled to their reachable columns) against its original form (reference below).
 *
 * Animation marks: an evolving picture (br_widget_evolving) changes from one phase to another only inside the
 * boxes of its marks (BW_MARK) at the two phases, so instrument_ui.c may redraw just those boxes; and a box
 * drawn on its own (cv_ox, cv_oy) has exactly the pixels of the whole picture there. Every evolving kind and
 * variant, values across 0..127, every phase against the next one and against a phase far away. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main

#define CARD_W 60u
#define CARD_H 134u
static uint16_t pics[128][CARD_W * CARD_H];
static uint8_t marks[128][HD_MARKS][4], wide[HD_MARKS][4];
static uint32_t fails, checks;

static void draw_card(uint32_t kind, uint32_t q, uint32_t variant, uint32_t phase)
{
    static const uint32_t env[4] = {40u, 60u, 80u, 50u};
    br_widget_draw(kind, 0, BR_PARAM_ICON_Y, 60, q, variant, env, phase);
}
static int in_box(const uint8_t b[4], uint32_t x, uint32_t y)
{
    return b[0] != 0xffu && x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3];
}

/* The fade as it was written before (38cdc16), kept here as the reference. */
static uint32_t ref_component(uint32_t component, uint32_t y, uint32_t threshold, uint32_t end)
{
    uint32_t remaining = end - y, denominator = 2u * end * end;
    uint32_t n = component * remaining * (remaining + end);
    uint32_t q = n / denominator, rem = n % denominator;
    return q + (rem * 32u > (threshold * 2u + 1u) * denominator);
}
static uint16_t ref_color(uint16_t color, uint32_t y, uint32_t threshold)
{
    uint32_t end = color == br_track_background(0u) ? 92u : 96u;
    if (y >= end) return C_BLACK;
    return (uint16_t)((ref_component((color >> 11) & 31u, y, threshold, end) << 11) |
        (ref_component((color >> 5) & 63u, y, threshold, end) << 5) | ref_component(color & 31u, y, threshold, end));
}
static void ref_shade(uint32_t x, uint32_t y)
{
    uint16_t bg = BR_BACKGROUND, target = swap16(bg);
    if (cv_background != bg) return;
    for (uint32_t row = 0; row < cv_h; ++row) {
        uint16_t colors[16];
        uint32_t cell = 0xffffffffu, threshold = 0u;
        if (y + row >= (bg == br_track_background(0u) ? 92u : 96u)) {
            for (uint32_t col = 0; col < cv_w; ++col)
                if (cv_px[row * cv_w + col] == target) cv_px[row * cv_w + col] = 0;
            continue;
        }
        for (uint32_t t = 0; t < 16u; ++t) colors[t] = swap16(ref_color(bg, y + row, t));
        for (uint32_t col = 0; col < cv_w; ++col) {
            uint16_t *pixel = &cv_px[row * cv_w + col];
            if (*pixel == target) {
                uint32_t next = (x + col) >> 1;
                if (next != cell) {
                    uint32_t hash = next * 0x9e3779b1u ^ ((y + row) >> 1) * 0x85ebca6bu;
                    hash ^= hash >> 16; hash *= 0x7feb352du;
                    hash ^= hash >> 15; hash *= 0x846ca68bu;
                    hash ^= hash >> 16;
                    threshold = hash & 15u; cell = next;
                }
                *pixel = colors[threshold];
            }
        }
    }
}

/* hd__radial as it was before its rows were culled to their reachable columns (38cdc16), the reference. */
static void ref_radial(int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t start, uint32_t sweep, uint16_t c)
{
    int32_t qx = hd__q4(cx), qy = hd__q4(cy), hw = w > 0 ? w >> 1 : 0, ext = r + hw + 256;
    int32_t sx = 0, sy = 0, ex = 0, ey = 0, inv = (1 << 24) / (r > 256 ? r : 256), b[4], x, y;
    if (sweep) {
        sx = bw_sine(start); sy = -bw_sine(start + 16384u);
        ex = bw_sine(start + sweep); ey = -bw_sine(start + sweep + 16384u);
    }
    int32_t out = w < 0 ? r + 256 : r + hw + 128, in = w > 0 ? r - hw - 127 : w ? 0 : r - 126, k;
    b[0] = (cx - ext) >> 8; b[2] = (cx + ext) >> 8;
    b[1] = (cy - ext) >> 8; b[3] = (cy + ext) >> 8;
    if (r < 0 || !hd__clip(b)) return;
    for (y = b[1]; y <= b[3]; ++y)
        for (x = b[0]; x <= b[2]; ++x) {
            int32_t dx = x * 16 + 8 - qx, dy = y * 16 + 8 - qy, d, a, m = 256;
            if (!(k = hd__zone(dx, dy, out, in)) || (k == 1 && w > 0)) continue;
            if (sweep) {
                int32_t c1 = (sx * dy - sy * dx) >> 10, c2 = (dx * ey - dy * ex) >> 10;
                m = (sweep > 32768u ? (c1 > c2 ? c1 : c2) : (c1 < c2 ? c1 : c2)) + 128;
                if (m <= 0) continue;
            }
            if (k == 1) a = 255;
            else {
                d = hd__len(dx, dy);
                if (w > 0) { a = d - r; a = hw - (a < 0 ? -a : a) + 128; }
                else if (!w) a = r - d + 128;
                else if ((a = 256 - (d * inv >> 16)) > 0) a = a * a >> 7;
                if (a <= 0) continue;
                if (a > 255) a = 255;
            }
            if (m < 256) a = a * m >> 8;
            hd__px(x, y, c, a);
        }
}
static void check_radial(void)
{
    static uint16_t got[64 * 64];
    uint32_t seed = 7u, n = 0;
    for (uint32_t t = 0; t < 20000u; ++t) {
        int32_t cx, cy, r, w; uint32_t start, sweep;
        seed = seed * 1103515245u + 12345u; cx = (int32_t)(seed >> 8) % (80 * 256) - 8 * 256;
        seed = seed * 1103515245u + 12345u; cy = (int32_t)(seed >> 8) % (80 * 256) - 8 * 256;
        seed = seed * 1103515245u + 12345u; r = (int32_t)((seed >> 8) % (40 * 256));
        seed = seed * 1103515245u + 12345u; w = (int32_t)((seed >> 8) % 3u) == 0 ? -1 : (int32_t)((seed >> 12) % 4u) * 256 * (int32_t)((seed >> 10) & 1u);
        seed = seed * 1103515245u + 12345u; start = seed >> 16; sweep = (seed >> 8) & 1u ? (seed & 65535u) : 0u;
        cv_begin(64, 64, BR_BACKGROUND); cv_oy = -(int32_t)(t % 9u); cv_ox = (int32_t)(t % 7u);
        hd__radial(cx, cy, r, w, start, sweep, C_WHITE);
        memcpy(got, cv_px, sizeof got);
        cv_begin(64, 64, BR_BACKGROUND);
        ref_radial(cx, cy, r, w, start, sweep, C_WHITE);
        cv_oy = 0; cv_ox = 0; ++n;
        if (memcmp(got, cv_px, sizeof got)) {
            ++fails; fprintf(stderr, "FAIL radial: centre (%d, %d) r %d w %d start %u sweep %u differs from the reference\n", cx, cy, r, w, start, sweep);
            return;
        }
    }
    printf("radial: %u shapes equal the reference\n", n);
}
static uint16_t shade_ref[240 * 40];
static void check_shade(void)
{
    uint32_t seed = 1u, n = 0;
    for (uint32_t part = 0; part < BR_PARTS; ++part) {
        br_ui.part = part; br_ui.page = BR_PAGE_HOME;
        for (uint32_t y = 0; y < 128u; ++y)
            for (uint32_t shape = 0; shape < 6u; ++shape) {
                uint32_t w = shape == 0u ? 240u : shape == 1u ? 60u : 3u + (seed >> 8) % 77u;
                uint32_t h = shape < 2u ? 10u : 1u + (seed >> 16) % 30u, x = shape == 0u ? 0u : (seed >> 4) % (241u - w);
                seed = seed * 1103515245u + 12345u;
                cv_begin(w, h, BR_BACKGROUND);
                for (uint32_t i = 0; i < w * h; ++i) { seed = seed * 1103515245u + 12345u; if (!((seed >> 20) & 7u)) cv_px[i] = (uint16_t)(seed >> 8); }
                memcpy(shade_ref, cv_px, w * h * 2u);
                br_canvas_shade(x, y);
                {
                    static uint16_t got[240 * 40];
                    memcpy(got, cv_px, w * h * 2u);
                    memcpy(cv_px, shade_ref, w * h * 2u);
                    ref_shade(x, y);
                    ++n;
                    if (memcmp(got, cv_px, w * h * 2u)) {
                        ++fails; fprintf(stderr, "FAIL shade: part %u canvas %ux%u at (%u, %u) differs from the reference\n", part, w, h, x, y);
                        return;
                    }
                }
            }
    }
    printf("shade: %u canvases equal the reference\n", n);
}

int main(void)
{
    static const uint32_t kinds[] = {BR_WIDGET_TIMBRE, BR_WIDGET_COLOR, BR_WIDGET_HARMONICS, BR_WIDGET_MIX,
                                     BR_WIDGET_AMOUNT, BR_WIDGET_HEAT, BR_WIDGET_MOD_ATTEN, BR_WIDGET_ORBIT, BR_WIDGET_LFO_DEPTH, BR_WIDGET_WAVE,
                                     BR_WIDGET_BPM};
    static const uint32_t variants[] = {1u, 1u, 1u, 2u, 1u, 3u, 4u, 4u, 1u, 5u, 1u};
    static const uint32_t env[4] = {40u, 60u, 80u, 50u};
    static br_control_t card;                         /* a used knob (a label), as br_card_art expects */
    static struct br_card_view view;
    card.label = "TEST";
    host_init_resources();
    br_app_init(); fm1_ms = 5000; br_intro_until = 0;
    check_shade();
    check_radial();
    for (uint32_t ki = 0; ki < sizeof kinds / sizeof kinds[0]; ++ki) {
        uint32_t kind = kinds[ki];
        if (!br_widget_marked(kind)) { ++fails; fprintf(stderr, "FAIL kind %u is not marked\n", kind); }
        for (uint32_t variant = 0; variant < variants[ki]; ++variant)
            for (uint32_t q = 0; q < 128u; q += (q < 8u || q > 119u) ? 1u : 5u) {
                uint32_t bad = 0;
                for (uint32_t p = 0; p < 128u; ++p) {
                    cv_begin(CARD_W, CARD_H, BR_BACKGROUND); cv_oy = 0; cv_ox = 0;
                    draw_card(kind, q, variant, p);
                    memcpy(pics[p], cv_px, sizeof pics[p]);
                    br_view = &view; br_cc.c = &card; br_cc.kind = kind; br_cc.q = q; br_cc.variant = variant; br_cc.phase = p;
                    memcpy(br_cc.env, env, sizeof br_cc.env); br_art = br_card_art;
                    br_anim_marks(marks[p], 60, 134);         /* as a card finds them */
                    br_anim_marks(wide, 1000, 1000);          /* (unbounded: every mark must lie within the card) */
                    for (uint32_t m = 0; m < HD_MARKS; ++m)
                        if ((wide[m][0] != 0xffu || marks[p][m][0] != 0xffu) && memcmp(wide[m], marks[p][m], 4u) && !bad) {
                            ++fails; bad = 1;
                            fprintf(stderr, "FAIL kind %u variant %u q %u phase %u: mark %u reaches outside the card\n", kind, variant, q, p, m);
                        }
                }
                for (uint32_t p = 0; p < 128u && !bad; ++p)
                    for (uint32_t t = 0; t < 2u && !bad; ++t) {
                        uint32_t o = t ? (p + 37u + q) & 127u : (p + 1u) & 127u;
                        ++checks;
                        for (uint32_t y = 0; y < CARD_H && !bad; ++y)
                            for (uint32_t x = 0; x < CARD_W; ++x) {
                                uint32_t i = y * CARD_W + x, m, inside = 0;
                                if (pics[p][i] == pics[o][i]) continue;
                                for (m = 0; m < HD_MARKS; ++m) inside |= in_box(marks[p][m], x, y) | in_box(marks[o][m], x, y);
                                if (!inside) {
                                    ++fails; bad = 1;
                                    fprintf(stderr, "FAIL kind %u variant %u q %u: phases %u and %u differ at (%u, %u), outside their marks\n",
                                            kind, variant, q, p, o, x, y);
                                    break;
                                }
                            }
                    }
                /* A mark's box drawn alone equals the whole picture there (and a box around the whole card too). */
                for (uint32_t p = 0; p < 128u && !bad; p += 9u)
                    for (uint32_t m = 0; m <= HD_MARKS && !bad; ++m) {
                        uint8_t b[4] = {0u, 0u, CARD_W - 1u, CARD_H - 1u};
                        if (m < HD_MARKS) {
                            if (marks[p][m][0] == 0xffu) continue;
                            memcpy(b, marks[p][m], 4u);
                        } else { b[0] = (uint8_t)(3u + p % 7u); b[1] = (uint8_t)(9u + p % 5u); b[2] = 50u; b[3] = 80u; }
                        cv_begin(b[2] - b[0] + 1u, b[3] - b[1] + 1u, BR_BACKGROUND); cv_oy = -(int32_t)b[1]; cv_ox = b[0];
                        draw_card(kind, q, variant, p);
                        cv_oy = 0; cv_ox = 0;
                        ++checks;
                        for (uint32_t y = b[1]; y <= b[3] && !bad; ++y)
                            for (uint32_t x = b[0]; x <= b[2]; ++x)
                                if (cv_px[(y - b[1]) * cv_w + (x - b[0])] != pics[p][y * CARD_W + x]) {
                                    ++fails; bad = 1;
                                    fprintf(stderr, "FAIL kind %u variant %u q %u phase %u: box %u,%u..%u,%u drawn alone differs at (%u, %u)\n",
                                            kind, variant, q, p, b[0], b[1], b[2], b[3], x, y);
                                    break;
                                }
                    }
            }
    }
    if (fails) { printf("animation marks: %u checks failed (of %u)\n", fails, checks); return 1; }
    printf("animation marks: all %u checks passed\n", checks);
    return 0;
}
