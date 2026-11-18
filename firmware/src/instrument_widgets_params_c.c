/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter graphics, batch C: global and dynamics. HD only.
 * Guide: PARAM-GRAPHICS-BUILD.md. Designs D14 DRUM SNAP, D15 MONITOR
 * (mapping to the existing BR_WIDGET_MONITOR), D16 COMP CURVE, D17 COMP TIME,
 * D18 HEAT AMOUNT, D19 MIDI CHANNEL (in bw_midi_channel), D20 CLOCK SOURCE.
 * Owns: this file; bw_snap, bw_comp_curve, bw_comp_time, bw_heat_amount,
 * bw_clock_source; br_param_widget_c, br_param_variant_c, br_param_env_c,
 * br_param_animated_c. Also edits the HD body of bw_midi_channel
 * (instrument_widgets.c); br_perform_heat_graph (instrument_ui.c, dead code)
 * is kept for the standard build only, its maths lives in bw_heat_amount.
 * COMP MIX maps to batch B's kind BR_WIDGET_MIX with B's PAR variant.
 *
 * Included twice: from instrument_widgets.c for the drawing half, and from
 * instrument_ui.c (BR_PARAM_HOOKS defined) for the mapping hooks, which may
 * use every mapping helper defined above br_control_widget. */
#ifndef BR_PARAM_HOOKS

/* Batch helpers (minsize, noinline; the drawing functions are noinline too:
 * measured smaller than inlined into br_widget_draw). pgc_path: a polyline of up to 45 points as two bw_path
 * calls (BW_PATH_MAX is 36), sharing the middle point. */
static __attribute__((minsize, noinline)) void pgc_path(const int32_t *px, const int32_t *py, uint32_t n,
                                                       uint32_t w, uint16_t c)
{
    uint32_t h = n >> 1;
    bw_path(px, py, h + 1u, w, c);
    bw_path(px + h, py + h, n - h, w, c);
}
/* Four 11 px humps on the rule base from x+8 (45 q8 points): hump j is
 * height[j] px tall, or h px with flat tops (half sine doubled, clipped). */
static __attribute__((minsize, noinline)) void pgc_humps(int32_t *px, int32_t *py, int32_t x, int32_t base,
                                                        int32_t h)
{
    static const uint8_t height[4] = {28, 18, 24, 14};
    for (int32_t i = 0; i <= 44; ++i) {
        int32_t j = i / 11, v = bw_sine((uint32_t)(i - 11 * j) * 2979u);
        if (h) { v *= 2; if (v > 16384) v = 16384; }
        px[i] = bw_q8(x + 8 + i);
        py[i] = bw_q8(base) - ((v * (h ? h : height[j > 3 ? 3 : j])) >> 6);
    }
}
/* D14 DRUM SNAP: a snare hit. The crack at the front grows taller, thicker
 * and whiter (dim grey at zero) with the snap; the ringing body after it
 * stays the same (the plan's crack rays were cut for flash). */
static __attribute__((noinline)) void bw_snap(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                    const uint32_t env[4], uint32_t phase)
{
    static const int8_t zig[6] = {0, -8, 7, -5, 3, 0};          /* crack shape, eighths of its height */
    int32_t px[30], py[30], e = 12 * 256, h = 1024 + (int32_t)q * 28, rule = bw_q8(y + 30);
    (void)s; (void)variant; (void)env; (void)phase;
    pg_rule(x, y + 30);
    for (int32_t i = 0; i < 30; ++i, e = (e * 240) >> 8) {      /* 2.5 decaying cycles */
        px[i] = bw_q8(x + 17) + i * 300;
        py[i] = rule - ((bw_sine((uint32_t)i * 5650u) * e) >> 14);
    }
    bw_path(px, py, 30u, 448u, bw_tone(BR_ACC, 200u));
    for (int32_t i = 0; i < 6; ++i) {
        px[i] = bw_q8(x + 7) + i * 512 - (i == 5) * 256;
        py[i] = rule + ((zig[i] * h) >> 3);
    }
    bw_path(px, py, 6u, 448u + q * 2u, bw_tone(C_WHITE, 110u + q));
}

/* D16 COMP CURVE, one function for the COMPRESSOR page (variant = the knob;
 * RATIO takes its step from q):
 * THRESHOLD (1): loud humps (dim) cut by a white line; what pokes above it
 *   (what gets squashed) is filled with the accent. Low threshold: most
 *   peaks caught; 0 dB: the line over everything (solid: the plan's dashes
 *   were cut for flash).
 * RATIO (2): the input/output curve: unity to the knee, flatter above it
 *   the higher the ratio, over the unity ghost (the plan's axes and wash
 *   were cut for flash).
 * MAKEUP (3): flat-topped (squashed) humps lifted off their dim ghost by
 *   the gain, at least 3 px for any gain so 1 dB reads (the plan's white
 *   lift arrow was cut for flash). */
static __attribute__((noinline)) void bw_comp_curve(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                          const uint32_t env[4], uint32_t phase)
{
    static const uint8_t end[8] = {22, 11, 7, 5, 4, 3, 2, 1};    /* rise above the knee: 22 / ratio */
    int32_t px[45], py[45], top[45], base = y + 48;
    uint32_t k = variant;
    (void)s; (void)env; (void)phase;
    if (k == 2u) {                                               /* RATIO */
        px[0] = bw_q8(x + 8); py[0] = bw_q8(y + 52);
        px[1] = bw_q8(x + 30); py[1] = bw_q8(y + 30);
        px[2] = bw_q8(x + 52); py[2] = bw_q8(y + 8);
        bw_path(px + 1, py + 1, 2u, 256u, BR_LINE);              /* unity ghost */
        py[2] = bw_q8(y + 30 - end[(q * 7u + 63u) / 127u]);
        bw_path(px, py, 3u, 448u, BR_ACC);
        hd_circle_q8(px[1], py[1], 576, 0, C_WHITE);
        return;
    }
    {                                                            /* THRESHOLD, MAKEUP */
        int32_t lift = q ? 3 + (int32_t)((q * 21u) >> 7) : 0, ly = base - 8 - (int32_t)((q * 28u) >> 7), edge = bw_q8(base), i;
        uint32_t t = k == 1u;
        pg_rule(x, base);
        pgc_humps(px, py, x, base, t ? 0 : 10);                  /* the humps, or the ghost */
        pgc_humps(px, top, x, base, t ? 0 : 10 + lift);          /* lifted */
        if (t) {                                                 /* only the caught part */
            edge = bw_q8(ly);
            for (i = 0; i <= 44; ++i) if (top[i] > edge) top[i] = edge;
        }
        bw_area(px, top, 0, edge, 45u, BR_ACC, t ? 255 : 90, t ? 150 : 8);
        pgc_path(px, py, 45u, 256u, BR_DIM);
        if (t) {
            hd_line(x + 8, ly, x + 52, ly, C_WHITE);
            return;
        }
        pgc_path(px, top, 45u, 448u, BR_ACC);
    }
}

/* D17 COMP TIME, one function for ATTACK, HOLD, RELEASE (variant: the knob
 * 0..2 in bits 0-1, then attack, hold, release, 7 bits each from bit 2, so
 * the card hash sees every companion): the gain-reduction dip after a hit
 * (white marker): gain clamps down (attack), stays down (hold), recovers
 * (release).
 * Every segment sits at its real length; the knob's own segment is lit and
 * thick, the other two thin and dim (the plan's wash was cut for flash). */
static __attribute__((noinline)) void bw_comp_time(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                         const uint32_t env[4], uint32_t phase)
{
    int32_t e[4], top = bw_q8(y + 14), bot = bw_q8(y + 38), hx[4], hy[4];
    uint32_t k = variant & 3u;
    (void)s; (void)q; (void)env; (void)phase;
    e[0] = bw_q8(x + 8);
    e[1] = e[0] + 512 + (int32_t)((variant >> 2) & 127u) * 20;   /* attack 2..12 px */
    e[2] = e[1] + 192 + (int32_t)((variant >> 9) & 127u) * 24;   /* hold 0..12 px (+0.75: a lit nub at 0) */
    e[3] = e[2] + 1024 + (int32_t)((variant >> 16) & 127u) * 36; /* release 4..22 px */
    pg_rule(x, y + 14);
    for (uint32_t j = 1; j <= 3u; ++j) {                         /* the lit segment last */
        uint32_t i = (k + j) % 3u;
        int32_t y1 = i == 2u ? top : bot;
        bw_quadratic(e[i], i ? bot : top, e[i] + ((e[i + 1] - e[i]) >> 2), y1, e[i + 1], y1,
                     i == k ? 640u : 256u, i == k ? BR_ACC : BR_DIM);
    }
    hx[0] = hx[1] = x + 8; hx[2] = x + 5; hx[3] = x + 11;       /* the hit */
    hy[0] = hy[1] = y + 12; hy[2] = hy[3] = y + 8;
    hd_poly4(hx, hy, C_WHITE);
}

/* D18 HEAT AMOUNT: the chosen heat applied to one cycle of a sine, harder
 * as the amount rises (the plan's clean ghost behind it was cut for flash).
 * BIT: sample-and-hold steps 1..7 px wide, levels quantised 1..9 px. SAT: y = x(1 + 7a) / (1 + 7a|x|),
 * flattening into a square (a eased as a squared). WOW: the phase warps (up
 * to 0.19 cycle; the plan's level flutter was cut for flash). The maths of
 * the old br_perform_heat_graph. */
static __attribute__((noinline)) void bw_heat_amount(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                           const uint32_t env[4], uint32_t phase)
{
    int32_t px[45], py[45], d = (int32_t)((q * q * 7u) >> 7), g = 128 + d, mid = bw_q8(y + 30);
    uint32_t hold = 1u + ((q * 6u) >> 7);
    int32_t step = 256 + (int32_t)q * 16;
    (void)s; (void)env; (void)phase;
    pg_rule(x, y + 30);
    for (uint32_t i = 0, held = 0; i <= 44u; ++i) {
        uint32_t ph = i * 1489u;
        int32_t v, o;
        if (i >= held + hold) held = i;                          /* sample and hold */
        if (variant == BR_HEAT_BIT) ph = held * 1489u;
        if (variant == BR_HEAT_WOW) ph += (uint32_t)(((int32_t)q * 96 * bw_sine(i * 2234u)) >> 14);
        v = bw_sine(ph);
        o = (v * 9) >> 5;                                        /* 18 px, q8 */
        if (variant == BR_HEAT_BIT) o = o / step * step;
        else if (variant == BR_HEAT_SAT) o = o * g / (128 + ((d * (v < 0 ? -v : v)) >> 14));
        px[i] = bw_q8(x + 8 + (int32_t)i);
        py[i] = mid - o;
    }
    pgc_path(px, py, 45u, 448u, BR_ACC);
}

/* D20 CLOCK SOURCE: where the tempo comes from. OFF: the internal crystal
 * (a can on two legs, accent core) beside a dimmer pulse train; USB/MIDI: a
 * plug feeding the pulses, at full brightness (its cable cut for flash). */
static __attribute__((noinline)) void bw_clock_source(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                            const uint32_t env[4], uint32_t phase)
{
    uint32_t on = q > 63u;
    (void)s; (void)variant; (void)env; (void)phase;
    pg_rule(x, y + 39);
    for (int32_t i = 0; i < 5; ++i)
        hd_rrect(x + 24 + 6 * i, y + 26, 2, 12, 0, on ? BR_ACC : bw_tone(BR_ACC, 150u));
    for (int32_t i = 0; i < 2; ++i)                              /* prongs / legs */
        hd_rrect(on ? x + 16 : x + 10 + 5 * i, on ? y + 29 + 4 * i : y + 40, on ? 4 : 1, on ? 2 : 6, 0, BR_DIM);
    if (on) {
        hd_rrect(x + 7, y + 27, 9, 10, 2, RGB(200, 206, 208));   /* plug body */
        return;
    }
    hd_rrect_frame(x + 8, y + 22, 10, 18, 2, BR_DIM);           /* crystal can */
    hd_rrect(x + 11, y + 25, 4, 12, 1, BR_ACC);                 /* and its core */
}

#else /* BR_PARAM_HOOKS */

/* BR_PAGE_HOME (kit): SNAP. BR_PAGE_MIX: TEMPO (2) CLOCK SOURCE and MONITOR, COMPRESSOR (4)
 * THRESHOLD, RATIO, MAKEUP, COMP KNOBS (5) ATTACK, HOLD, RELEASE, HEAT (6)
 * AMOUNT. COMP MIX stays batch B's MIX; the
 * channel slots keep MIDI_CHANNEL (variant below). */
static uint32_t br_param_widget_c(uint32_t k, const br_control_t *c)
{
    static const uint8_t kind[4][4] = {                          /* pages 4..7 */
        {BR_WIDGET_NONE, BR_WIDGET_COMP_CURVE, BR_WIDGET_COMP_CURVE, BR_WIDGET_COMP_CURVE},
        {BR_WIDGET_COMP_TIME, BR_WIDGET_COMP_TIME, BR_WIDGET_COMP_TIME, BR_WIDGET_NONE},
        {BR_WIDGET_NONE, BR_WIDGET_HEAT_AMOUNT, BR_WIDGET_NONE, BR_WIDGET_NONE},
        {BR_WIDGET_NONE, BR_WIDGET_NONE, BR_WIDGET_NONE, BR_WIDGET_NONE}};
    uint32_t page = br_ui.screen[br_ui.page];
    (void)c;
    if (br_ui.page == BR_PAGE_HOME && !page && k == 2u && br_part[br_ui.part].sound.preset != BR_KIT_PLAITS &&
        br_is_kit(br_part[br_ui.part].sound.preset)) return BR_WIDGET_SNAP;   /* the drum kit's SNAP */
    if (br_ui.page != BR_PAGE_MIX) return BR_WIDGET_NONE;
    if (page == 2u) return k == 2u ? BR_WIDGET_CLOCK_SOURCE : k == 3u ? BR_WIDGET_MONITOR : BR_WIDGET_NONE;
    return page - 4u < 4u ? kind[page - 4u][k & 3u] : BR_WIDGET_NONE;
}
/* HEAT AMOUNT: the heat type; COMP TIME: the knob and the three times; the
 * other kinds of this batch: the knob (COMP CURVE uses it); MIDI channel
 * slots: channel | destination << 8 (T1-T3 = 0-2, D1-D4 = 3-6). MONITOR
 * keeps its own rule. */
static uint32_t br_param_variant_c(uint32_t k, const br_control_t *c, uint32_t kind)
{
    uint32_t page = br_ui.screen[br_ui.page];
    if (kind == BR_WIDGET_HEAT_AMOUNT) return br_master.fx.heat_type;
    if (kind == BR_WIDGET_COMP_TIME)
        return k | (uint32_t)br_master.comp.attack << 2 | (uint32_t)br_master.comp.hold << 9 |
            (uint32_t)br_master.comp.release << 16;
    if (kind >= BR_WIDGET_SNAP && kind <= BR_WIDGET_CLOCK_SOURCE) return k;   /* the knob */
    if (kind == BR_WIDGET_MIDI_CHANNEL && br_ui.page == BR_PAGE_MIX)
        return (uint32_t)c->value | (page == 7u ? k : 3u + k) << 8;
    return BR_PARAM_UNHANDLED;
}
/* No env: COMP TIME's companions travel in its variant. */
static uint32_t br_param_env_c(uint32_t k, const br_control_t *c, uint32_t kind, uint32_t env[4])
{
    (void)k; (void)c; (void)kind; (void)env;
    return 0u;
}
/* All static. */
static uint32_t br_param_animated_c(uint32_t k, uint32_t kind)
{
    (void)k; (void)kind;
    return BR_PARAM_ANIM_NONE;
}

#endif /* BR_PARAM_HOOKS */
