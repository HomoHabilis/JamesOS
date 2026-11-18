/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter graphics, batch D: sequencer and arp. HD only.
 * Guide: PARAM-GRAPHICS-BUILD.md. Designs D21 SEQUENCER TILES (in the HD
 * branch of br_draw_seq_control, instrument_ui.c; not widget kinds),
 * D22 STEPS, D23 SPEED, D24 QUANTIZE, D25 OCTAVES, D26 ARP GATE.
 * Owns: this file; bw_steps, bw_speed, bw_quantize, bw_octaves, bw_arp_gate;
 * br_param_widget_d, br_param_variant_d, br_param_env_d, br_param_animated_d.
 * Also edits the HD branch of br_draw_seq_control (the NOTE / LENGTH /
 * VELOCITY / CONDITION blocks).
 *
 * Included twice: from instrument_widgets.c for the drawing half, and from
 * instrument_ui.c (BR_PARAM_HOOKS defined) for the mapping hooks, which may
 * use every mapping helper defined above br_control_widget. */
#ifndef BR_PARAM_HOOKS

/* Flat rounded box (corner radius 1, none below 3 px): every rule, tick, cell and key here. */
static __attribute__((noinline)) void pgd_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    hd_rrect(x, y, w, h, 1, c);
}
/* A row of n cells over span px from x0 (cell i ends where cell i + 1 starts, less a 1 px gap), h tall at
 * row y; cell i is on (mask bit i) or off. Step rulers (SPEED, QUANTIZE STEP), loop cells and A:B cells. */
static __attribute__((noinline)) void pgd_row(int32_t x0, int32_t y, int32_t span, uint32_t n, int32_t h,
                                              uint32_t mask, uint16_t on, uint16_t off)
{
    for (uint32_t i = 0; i < n; ++i) {
        int32_t a = x0 + (int32_t)i * span / (int32_t)n, b = x0 + (int32_t)(i + 1u) * span / (int32_t)n - 1;
        pgd_box(a, y, b > a ? b - a : 1, h, mask >> i & 1u ? on : off);
    }
}

/* D22: the pattern's steps as the step screen lays them out: rows of eight split into two beats, banks of
 * two rows; the first N lit, the last one white. A count, so dots are honest. */
static void bw_steps(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                     const uint32_t env[4], uint32_t phase)
{
    (void)s; (void)q; (void)env; (void)phase;
    for (int32_t i = 0; i < 64; ++i) {
        int32_t c = i & 7, r = i >> 3;
        pgd_box(x + 9 + 5 * c + (c >> 2) * 2, y + 9 + 5 * r + (r >> 1), 4, 4,
                i + 1 < (int32_t)variant ? BR_ACC : i + 1 == (int32_t)variant ? C_WHITE : BR_LINE);
    }
}
/* D23: step cells per beat on a two-beat ruler: 1/8 two, 1/16 four, 1/32 eight; each beat's first pale. */
static void bw_speed(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                     const uint32_t env[4], uint32_t phase)
{
    uint32_t n = 4u << variant;
    (void)s; (void)q; (void)env; (void)phase;
    pgd_row(x + 8, y + 20, 45, n, 13, 1u | 1u << (n >> 1), bw_highlight(1u), BR_ACC);
    pgd_box(x + 8, y + 40, 44, 1, BR_LINE);
    for (int32_t i = 0; i < 3; ++i) pgd_box(x + 8 + 21 * i + (i >> 1), y + 37, 1, 4, BR_DIM);
}
/* D24: three notes played off the grid, drawn where they land. STEP: snapped to the step (step cells under
 * the grid); OFF: where played, no grid; 1/8..1/64: snapped to a grid that halves each time, a dim ghost at
 * the played spot; 1/128: the grid all but solid, the notes barely move. The span is one beat. */
static void bw_quantize(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    static const uint8_t played[3] = {7, 19, 34};
    int32_t g = variant >= 2u ? (44 * 256) >> (variant - 1u) : variant ? 0 : 11 * 256, i;
    (void)s; (void)q; (void)env; (void)phase;
    x += 8;
    for (i = 0; g && i * g <= 44 * 256; ++i) pgd_box(x + (i * g >> 8), y + 17, 1, 27, BR_LINE);
    if (!variant) pgd_row(x, y + 39, 45, 4u, 6, 0u, 0u, BR_LINE);
    for (i = 0; i < 3; ++i) {
        int32_t p = x + played[i], d = g ? x + ((played[i] * 256 + g / 2) / g * g >> 8) : p;
        if (d != p) hd_rrect(p - 2, y + 28, 5, 5, 2, RGB(64, 74, 78));
        hd_rrect(d - 3, y + 27, 7, 7, 3, BR_ACC);
    }
}
/* D25: one notehead per octave the arp climbs, stacked on shelves from the bottom; the top one white.
 * Octaves it does not reach are outlines. */
static void bw_octaves(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                       const uint32_t env[4], uint32_t phase)
{
    int32_t n = (int32_t)variant, i;
    (void)s; (void)q; (void)env; (void)phase;
    for (i = 0; i < 4; ++i) {
        int32_t ty = y + 43 - 10 * i;
        pgd_box(x + 19, ty + 3, 23, 1, BR_LINE);
        if (i < n) hd_rrect(x + 25, ty, 11, 7, 3, i == n - 1 ? C_WHITE : BR_ACC);
        else hd_rrect_frame(x + 25, ty, 11, 7, 3, BR_LINE);
    }
}
/* D26: four arp steps; in each a note bar as long as the gate, stepping up and down like a run. Short gate:
 * staccato blips; full: the notes touch. Time runs over the rule. */
static void bw_arp_gate(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    int32_t w = 1 + (int32_t)variant * 10 / 127, i;
    (void)s; (void)q; (void)env; (void)phase;
    for (i = 0; i < 4; ++i) {
        int32_t cx = x + 8 + 11 * i;
        hd_rrect_frame(cx, y + 18, 12, 17, 0, BR_LINE);
        hd_rrect(cx + 1, y + 25 - (i & 1) * 2 - (i == 2) * 2, w, 7, 2, BR_ACC);
    }
    pgd_box(x + 8, y + 40, 45, 1, BR_LINE);
}

/* D21: the picture on a sequencer step tile (NOTE, LENGTH, VELOCITY, CONDITION; tonal and drum, new steps
 * and held step), drawn on the tile's 54 x 52 canvas inside the face at (10, 0, 34, 32). v is the exact
 * value, q its 0..127 position in the range, low the range's bottom (0 only for a drum LENGTH: TAIL). */
static void pgd_seq_tile(uint32_t k, int32_t v, uint32_t q, int32_t low)
{
    uint16_t acc = BR_ACC;
    int32_t i;
    if (k == 0u) {                      /* NOTE: one octave of keys, the note's lit; a locator over the range */
        uint32_t n = (uint32_t)v % 12u, white = (n + (n > 4u)) >> 1, black = 0x54Au >> n & 1u;
        pgd_box(13, 5, 27, 1, BR_LINE);
        pgd_box(13 + (int32_t)(q * 24u / 127u), 4, 3, 3, C_WHITE);
        for (i = 0; i < 7; ++i) {
            pgd_box(13 + 4 * i, 9, 3, 18, !black && i == (int32_t)white ? acc : RGB(176, 184, 188));
            if (0x3Bu >> i & 1u) pgd_box(15 + 4 * i, 9, 3, 10, black && i == (int32_t)white ? acc : RGB(14, 17, 18));
        }
    } else if (k == 1u) {               /* LENGTH: piano-roll bars on two rulers: sixteenths of the first step
                                         * (ticks: quarters), then whole steps beyond (ticks: 16 steps) */
        uint32_t first = v > 16 ? 16u : (uint32_t)v, rest = v > 16 ? (uint32_t)v - 16u : 0u;
        int32_t tw = (int32_t)((rest * 24u + BR_STEP_LENGTH_MAX - 17u) / (BR_STEP_LENGTH_MAX - 16u));
        for (i = 0; i < 10; ++i) {
            int32_t t = i % 5;
            if (!t) hd_rrect(14, 7 + 13 * (i / 5), 26, 7, 3, RGB(10, 12, 13));
            pgd_box(15 + t * 23 / 4, 4 + 13 * (i / 5), 1, 2, t % 4 ? BR_LINE : BR_DIM);
        }
        if (!v && !low) for (i = 0; i < 4; ++i) pgd_box(15 + 6 * i, 8 + (i + 1) / 2, 6, 5 - i, acc);  /* drum TAIL: the hit rings out */
        if (first) pgd_box(15, 8, (int32_t)(first * 24u + 15u) / 16, 5, acc);
        if (tw) pgd_box(15, 21, tw < 3 ? 3 : tw, 5, acc);
    } else if (k == 2u) {               /* VELOCITY: a lollipop over the step's floor */
        int32_t top = 25 - (int32_t)(q * 18u / 127u);
        pgd_box(13, 27, 28, 1, BR_LINE);
        if (!q) acc = BR_DIM;
        pg_lollipop(27, top, 27, acc, 1u);
    } else if ((uint32_t)v - 1u < 100u || !v) {   /* ALWAYS, 1-100 %: that share of 16 dots, dithered */
        static const uint8_t order[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        uint32_t lit = v ? ((uint32_t)v * 16u + 50u) / 100u : 16u;
        for (i = 0; i < 16; ++i) {
            int32_t dx = 17 + 6 * (i & 3), dy = 6 + 6 * (i >> 2);
            if (order[i] < (lit ? lit : 1u)) pgd_box(dx - 1, dy - 1, 3, 3, acc);
            else pgd_box(dx, dy, 1, 1, RGB(78, 88, 92));
        }
    } else if (v <= BR_STEP_COND_NOT_FIRST) {     /* FIRST PASS / REPEATS: from the start, which passes */
        pgd_box(12, 6, 2, 18, C_WHITE);
        pgd_row(16, 8, 24, 4u, 14, v == BR_STEP_COND_FIRST ? 1u : 14u, acc, BR_LINE);
    } else if (v <= BR_STEP_COND_NOT_NEI) {       /* AFTER HIT / MISS: the step before -> this one;
                                                   * NEIGHBOR / ALONE: the lane above -> this one */
        int32_t nei = v >= BR_STEP_COND_NEI;
        int32_t ax = nei ? 21 : 13, ay = nei ? 3 : 10;
        if (v & 1) hd_rrect(ax, ay, 10, 10, 2, C_WHITE);
        else hd_rrect_frame(ax, ay, 10, 10, 2, BR_DIM);
        hd_rrect(nei ? 21 : 31, nei ? 19 : 10, 10, 10, 2, acc);
        pgd_box(nei ? 25 : 24, 14 + !nei, nei ? 2 : 5, nei ? 4 : 2, BR_DIM);
    } else if (v <= BR_STEP_COND_NOT_FILL) {      /* FILL: a hatched frame; NO FILL: the hatching unlit */
        uint16_t c = v == BR_STEP_COND_FILL ? acc : BR_LINE;
        hd_rrect_frame(13, 6, 28, 20, 3, BR_DIM);
        for (i = 0; i < 4; ++i) hd_line(15 + 6 * i, 23, 20 + 6 * i, 8, c);
    } else {                                      /* A:B: B passes in a loop, pass A lit */
        uint32_t b = 1u, a = (uint32_t)v - BR_STEP_COND_AB_BASE;
        while (a >= b) a -= b++;
        hd_rrect_frame(14, 17, 26, 9, 2, BR_DIM);  /* the loop back: its top hides under the cells */
        pgd_row(14, 7, 27, b, 13, 1u << a, acc, BR_LINE);
    }
}

#else /* BR_PARAM_HOOKS */

/* SEQ settings (page 1) STEPS, SPEED, QUANTIZE; ARP (tonal tracks) OCTAVES, GATE. */
static uint32_t br_param_widget_d(uint32_t k, const br_control_t *c)
{
    if (br_ui.page == BR_PAGE_SEQ && br_ui.screen[br_ui.page] == 1u && k < 3u) return BR_WIDGET_STEPS + k;
    if (br_ui.page == BR_PAGE_PATTERN && !br_retrig_page() && k >= 2u && c->label[0])
        return k == 2u ? BR_WIDGET_OCTAVES : BR_WIDGET_ARP_GATE;
    return BR_WIDGET_NONE;
}
/* Each picture takes the exact value (step count, choice index, octaves, gate). */
static uint32_t br_param_variant_d(uint32_t k, const br_control_t *c, uint32_t kind)
{
    uint32_t target = br_target_control_id(k);
    if (target == BR_TARGET_ARP_OCTAVES) return br_part[br_ui.part].arp_octaves;
    if (target == BR_TARGET_ARP_GATE) return br_part[br_ui.part].arp_gate;
    return kind >= BR_WIDGET_STEPS && kind <= BR_WIDGET_ARP_GATE ? (uint32_t)c->value : BR_PARAM_UNHANDLED;
}
static uint32_t br_param_env_d(uint32_t k, const br_control_t *c, uint32_t kind, uint32_t env[4])
{
    (void)k; (void)c; (void)kind; (void)env;
    return 0u;
}
static uint32_t br_param_animated_d(uint32_t k, uint32_t kind)
{
    (void)k; (void)kind;
    return BR_PARAM_ANIM_NONE;
}

#endif /* BR_PARAM_HOOKS */
