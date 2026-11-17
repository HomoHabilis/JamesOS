/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter graphics, batch B: effects and punch. HD only.
 * Guide: PARAM-GRAPHICS-BUILD.md. Designs D01 BLEND (kind BR_WIDGET_MIX,
 * drawn by bw_blend), D02 SEND, D07 DRIVE GAIN, D08 TONE, D09 ECHO TRAIN,
 * D10 ORBIT, D11 STEREO DEPTH, D12 REVERB TAIL, D32 LOOP LENGTH,
 * D34 PUNCH FILTER, D35 TREMOLO.
 * Owns: this file; bw_blend, bw_send, bw_drive_gain, bw_drive_tone
 * (bw_tone is taken: the colour helper), bw_echo, bw_orbit, bw_stereo_depth,
 * bw_tail, bw_loop_len, bw_punch_filter, bw_tremolo; br_param_widget_b,
 * br_param_variant_b, br_param_env_b, br_param_animated_b. May revive or
 * edit the HD bodies of bw_mix and bw_drive in instrument_widgets.c.
 * ORBIT and SEND/BLEND are also returned by batch A's target mapping.
 *
 * Included twice: from instrument_widgets.c for the drawing half, and from
 * instrument_ui.c (BR_PARAM_HOOKS defined) for the mapping hooks, which may
 * use every mapping helper defined above br_control_widget. */
/* Punch slots get the punch settings in the variant (hashed with it): the
 * first four br_punch_cfg bytes as one word (bits 0-1 loop length, 8-9 echo
 * time, 16-17 filter shape, 24-25 tremolo rate), tremolo depth in bits
 * 10-14, PGB_PAGE2 on the second punch page, PGB_PUNCH always. */
#define PGB_PAGE2 (1u << 2)
#define PGB_PUNCH (1u << 3)
#ifndef BR_PARAM_HOOKS

/* Batch helpers. pgb_quad: the 17 q8 points of a quadratic through pixel
 * centres (the weights sum to 256, so the sums are already q8). */
static __attribute__((minsize, noinline)) void pgb_quad(int32_t *px, int32_t *py, int32_t x0, int32_t y0, int32_t cx, int32_t cy,
                     int32_t x1, int32_t y1)
{
    for (int32_t i = 0; i <= 16; ++i) {
        int32_t a = 16 - i;
        px[i] = a * a * x0 + 2 * a * i * cx + i * i * x1 + 128;
        py[i] = a * a * y0 + 2 * a * i * cy + i * i * y1 + 128;
    }
}
/* Arrow ending at pixel (x1, y1), pointing along the unit (ux, uy), with a
 * shaft of len px and a 3 px chevron: one path, the chevron retraced. */
static __attribute__((minsize, noinline)) void pgb_arrow(int32_t x1, int32_t y1, int32_t ux, int32_t uy,
                                                int32_t len, uint32_t w, uint16_t c)
{
    int32_t px[5], py[5], ex = bw_q8(x1), ey = bw_q8(y1);
    px[0] = ex - ux * len * 256; py[0] = ey - uy * len * 256;
    px[1] = px[3] = ex; py[1] = py[3] = ey;
    px[2] = ex - (3 * ux + 3 * uy) * 256; py[2] = ey + (3 * ux - 3 * uy) * 256;
    px[4] = ex - (3 * ux - 3 * uy) * 256; py[4] = ey - (3 * ux + 3 * uy) * 256;
    bw_path(px, py, 5u, w, c);
}
/* Beat ruler: a rule with posts at both ends and the middle. */
static __attribute__((minsize, noinline)) void pgb_ruler(int32_t x0, int32_t x1, int32_t y)
{
    hd_line(x0, y, x1, y, BR_LINE);
    for (int32_t i = 0; i < 3; ++i) {
        int32_t tx = x0 + (((x1 - x0) * i) >> 1);
        hd_line(tx, y - 1, tx, y + 3, BR_DIM);
    }
}
/* Two edges mirrored about y+30 over 41 columns from left (stereo strands,
 * tremolo band): offset A + B sin(i ph + ph0) up and down, a flat wash
 * between (none at alpha 0), then the lower edge (1 px, c2) and the upper
 * (w1, c1), each as two paths (points 0..20, 20..40). */
static __attribute__((minsize, noinline)) void pgb_strands(int32_t left, int32_t y, uint32_t ph, uint32_t ph0,
                                                  int32_t a, int32_t b, int32_t alpha, uint32_t w1, uint16_t c1, uint16_t c2)
{
    int32_t px[41], ty[41], by[41];
    for (int32_t i = 0; i <= 40; ++i) {
        int32_t h = a + ((b * bw_sine((uint32_t)i * ph + ph0)) >> 14);
        px[i] = bw_q8(left + i); ty[i] = bw_q8(y + 30) - h; by[i] = bw_q8(y + 30) + h;
    }
    if (alpha) bw_area(px, ty, by, 0, 41u, BR_ACC, alpha, alpha);
    for (uint32_t o = 0; o <= 20u; o += 20u) {
        bw_path(px + o, by + o, 21u, 256u, c2);
        bw_path(px + o, ty + o, 21u, w1, c1);
    }
}

/* All MIX controls share the homepage's artwork. */
static void bw_blend(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                     const uint32_t env[4], uint32_t phase)
{
    (void)variant; (void)env; (void)phase;
    bw_mix(x, y, s, q);
}

/* D02 SEND: the track runs past, a branch peels off into the effect's own
 * FX/SENDS tile. More send: a thicker, brighter branch and a fuller tile.
 * Zero: the branch dashed and stopping short of the tile, tile and tap dim. */
static void bw_send(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                    const uint32_t env[4], uint32_t phase)
{
    int32_t px[17], py[17];
    uint16_t col = variant == BR_SEND_REVERB ? BR_GLOBAL_ACC : BR_ACC;
    (void)s; (void)env; (void)phase;
    br_fx_icon(x + 23, y + 30, variant & 3u, hd_mix(BR_LINE, col, 64u + ((q * 193u) >> 7)));
    hd_line(x + 6, y + 14, x + 54, y + 14, BR_DIM);
    pgb_quad(px, py, x + 14, y + 14, x + 14, y + 38, x + 23, y + 40);
    if (q) bw_path(px, py, 17u, 192u + q * 4u + q / 2u, bw_tone(col, 70u + ((q * 187u) >> 7)));
    else for (uint32_t i = 0; i < 13u; i += 4u) bw_path(px + i, py + i, 3u, 384u, BR_DIM);   /* zero: dashed, open before the tile */
    hd_circle_q8(bw_q8(x + 14), bw_q8(y + 14), 512, 0, q ? C_WHITE : BR_DIM);
}

/* D07 DRIVE GAIN: the transfer curve (input across, output up) over a dim
 * unity ghost. Low gain is the clean diagonal; more gain bends OVERDRIVE into
 * an S, clips DISTORT flat at full scale, and squares FUZZ lopsidedly. */
static void bw_drive_gain(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                          const uint32_t env[4], uint32_t phase)
{
    int32_t px[33], py[33], g = 16 + (int32_t)((q * q * 177u) >> 14);   /* gain, q4: 1..12, eased */
    (void)s; (void)env; (void)phase;
    px[0] = bw_q8(x + 8); py[0] = bw_q8(y + 50); px[1] = bw_q8(x + 52); py[1] = bw_q8(y + 10);
    bw_path(px, py, 2u, 256u, RGB(30, 36, 38));                /* unity ghost */
    for (int32_t i = 0; i <= 32; ++i) {
        int32_t v = (i - 16) * g, o;                            /* 256 = full scale */
        if (variant == 1u) o = v;
        else if (variant == 2u) o = v > 141 ? 141 : v > 0 ? v : (v * 205) >> 8;
        else o = v * 256 * g / (256 * g + (v < 0 ? -v : v) * (g - 16));
        o = o > 256 ? 256 : o < -256 ? -256 : o;
        px[i] = bw_q8(x + 8) + i * 352;
        py[i] = bw_q8(y + 30) - o * 20;
    }
    bw_graph(px, py, 33u, y + 30, BR_ACC);
}

/* D08 TONE: the driven square after the tone filter (a one-pole low pass,
 * settled over two warm-up half cycles). Open: a hard square; closed:
 * corners rounded down toward a dull triangle (the plan's ghost square was
 * cut for flash). */
static void bw_drive_tone(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                          const uint32_t env[4], uint32_t phase)
{
    int32_t px[44], py[44], v = 0, k = 20 + (int32_t)((q * q * 239u) >> 14);   /* eased */
    (void)s; (void)variant; (void)env; (void)phase;
    for (int32_t i = -22; i < 44; ++i) {
        v += (((((i + 44) / 11) & 1 ? -3584 : 3584) - v) * k) >> 8;
        if (i >= 0) { px[i] = bw_q8(x + 8 + i); py[i] = bw_q8(y + 30) - v; }
    }
    bw_path(px, py, 23u, 448u, BR_ACC);
    bw_path(px + 22, py + 22, 22u, 448u, BR_ACC);
}

/* D09 ECHO TRAIN: the dry hit (white) and its repeats, each quieter.
 * Lollipops (stem and head). FX FEEDBACK (variant 0): 6 px apart on a plain
 * rule; more feedback = more, slower-fading repeats. Punch (variant = the packed punch settings, see the hooks):
 * lollipops on a two-beat ruler, spaced by the echo time; ECHO TIME (page 0)
 * fixed decay 0.7, FEEDBACK (page 1) the decay from the knob. */
static void bw_echo(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                    const uint32_t env[4], uint32_t phase)
{
    static const uint8_t eighths[4] = {1, 2, 4, 6};             /* of a beat: 1/32, 1/16, 1/8, dotted 1/8 */
    uint32_t punch = variant & PGB_PUNCH;
    int32_t r = punch && !(variant & PGB_PAGE2) ? 179 : (int32_t)((q * 237u) >> 7), h = 34 * 256, bx = bw_q8(x + 10);
    int32_t step = punch ? 704 * eighths[(variant >> 8) & 3u] : 6 * 256;
    (void)s; (void)env; (void)phase;
    if (punch) pgb_ruler(x + 8, x + 52, y + 48);
    else pg_rule(x, y + 48);
    for (uint32_t i = 0; h >= 512 && bx < bw_q8(x + 53); ++i, h = (h * r) >> 8, bx += step) {
        uint16_t c = i ? bw_tone(BR_ACC, i > 8u ? 110u : 255u - 18u * i) : C_WHITE;
        int32_t top = y + 48 - (h >> 8);
        hd_line(bx >> 8, y + 47, bx >> 8, top, i ? bw_tone(BR_ACC, 120u) : BR_DIM);
        hd_circle_q8(bx, bw_q8(top), 563, 0, c);
    }
}

/* D10 ORBIT: a head circles a ring with a comet trail; rate is trail length
 * (and speed when it moves). LFO and FX MOD RATE: one white head at the
 * phase (the LFO's, or the evolving clock's). Targets: a fixed mid trail,
 * head in the LFO's badge colour. */
static void bw_orbit(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                     const uint32_t env[4], uint32_t phase)
{
    int32_t cx = bw_q8(x + 30), cy = bw_q8(y + 30), hx, hy;
    uint32_t sweep, at = phase * 512u;
    uint16_t head = variant == BR_ORBIT_TARGET_LFO1 ? RGB(240, 72, 64) :
                    variant == BR_ORBIT_TARGET_LFO2 ? RGB(72, 160, 248) : C_WHITE;
    (void)s; (void)env;
    if (variant >= BR_ORBIT_TARGET_LFO1) q = 64u;
    sweep = 3641u + ((q * 54613u) >> 7);                        /* 20 .. 320 degrees */
    hd_circle_q8(cx, cy, 4096, 256, BR_LINE);
    BW_MARK(bw_round_arc(cx, cy, 4096u, 768u, at - sweep, sweep, bw_tone(BR_ACC, 130u));   /* the arcs and the head move round: one mark */
            bw_round_arc(cx, cy, 4096u, 768u, at - (sweep >> 1) + 512u, (sweep >> 1) - 512u, BR_ACC);
            bw_point(cx, cy, 4096u, at, &hx, &hy);
            hd_circle_q8(hx, hy, 768, 0, head));
}

/* D11 STEREO DEPTH: left and right copies drift apart around the centre:
 * two mirrored strands, a wide lattice at full depth. */
static void bw_stereo_depth(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                            const uint32_t env[4], uint32_t phase)
{
    (void)s; (void)variant; (void)env; (void)phase;
    pgb_strands(x + 10, y, 3277u, 0u, 0, 512 + (int32_t)q * 28, 50, 384u, BR_ACC, bw_highlight(0u));   /* 2 cycles, 2..16 px */
}

/* D12 REVERB TAIL: the direct sound (white) and a diffuse decaying tail
 * whose length follows the decay (the plan's reflection dots were cut for
 * flash). */
static void bw_tail(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                    const uint32_t env[4], uint32_t phase)
{
    int32_t px[17], py[17], e = 30 * 256, len = 160 + (int32_t)q * 4, base = bw_q8(y + 48);   /* 1/16 of 10..42 px, q8 */
    (void)s; (void)variant; (void)env; (void)phase;
    for (int32_t i = 0; i <= 16; ++i, e = (e * 211) >> 8) {
        px[i] = bw_q8(x + 11) + len * i; py[i] = base - e;
    }
    pg_rule(x, y + 48);
    bw_area(px, py, 0, base, 17u, BR_ACC, 110, 20);
    bw_path(px, py, 17u, 256u, bw_tone(BR_ACC, 200u));
    hd_rrect(x + 7, y + 12, 2, 36, 1, C_WHITE);
}

/* D32 LOOP LENGTH: a one-bar ruler with the looped slice lit, dim outlines of
 * its repeats filling the bar and a white return arc over the slice (its
 * arrowhead cut for flash). */
static void bw_loop_len(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    static const uint8_t peak[4] = {14, 11, 9, 7};              /* 4 + 10 sqrt(fraction) */
    uint32_t c = variant & 3u;
    int32_t w = (44 * 256) >> c, px[17], py[17], end = x + 6 + (w >> 8);
    (void)s; (void)q; (void)env; (void)phase;
    pgb_ruler(x + 8, x + 52, y + 42);
    for (int32_t j = (1 << c) - 1; j >= 0; --j) {
        int32_t a = (bw_q8(x + 8) + j * w) >> 8, b = (bw_q8(x + 8) + (j + 1) * w) >> 8;
        if (!j) hd_rrect(a, y + 30, b - a - 1, 9, 1, bw_tone(BR_ACC, 110u));
        hd_rrect_frame(a, y + 30, b - a - 1, 9, 1, j ? BR_LINE : BR_ACC);
    }
    pgb_quad(px, py, end, y + 28, (x + 8 + end) >> 1, y + 28 - 2 * peak[c], x + 8, y + 28);
    bw_path(px, py, 17u, 384u, C_WHITE);
}

/* D34 PUNCH FILTER: the HOME filter graph (bw_filter, the same response),
 * LP or HP. FILTER (page 0): knee at 52 (LP) or 75 (HP); the sweeps add a
 * white arrow at the knee (LP left, HP right). AMOUNT (page 1): the knee
 * moves with the amount (LP down from open, HP up from open). */
static void bw_punch_filter(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                            const uint32_t env[4], uint32_t phase)
{
    uint32_t shape = (variant >> 16) & 3u, hp = shape & 1u;
    int32_t cut = hp ? 75 : 52;
    (void)env; (void)phase;
    if (variant & PGB_PAGE2) { cut = (int32_t)((q * 111u) >> 7); if (!hp) cut = 127 - cut; }
    bw_filter(x, y, s, (uint32_t)((cut * 125) >> 8) + (hp ? 65u : 0u));
    if (shape & 2u) {
        int32_t d = hp ? 1 : -1;
        pgb_arrow(x + 5 + ((cut * 101) >> 8) + 7 * d, y + 32, d, 0, 14, 384u, C_WHITE);
    }
}

/* D35 TREMOLO: a held tone's level band chopped into pulses over one beat
 * (1, 2, 4, 8 pulses for 1/4 .. 1/32). TREMOLO (page 0) at the current depth
 * on a beat ruler; DEPTH (page 1) troughs close as the knob turns, with a
 * white arrow from the band's edge down to the trough. */
static void bw_tremolo(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                       const uint32_t env[4], uint32_t phase)
{
    uint32_t depth = variant & PGB_PAGE2;
    int32_t d = depth ? (int32_t)q : (int32_t)((((variant >> 10) & 31u) * 33u) >> 3);
    int32_t left = x + (depth ? 6 : 10), a = (d * 13) >> 7, b = d * 13;
    (void)s; (void)env; (void)phase;
    /* half-height 13 (1 - d (1 - cos) / 2): 13 - 13d/2 + 13d/2 cos, n = 1 << rate pulses */
    if (!d)                                       /* depth 0: a dim ghost of the pulses full depth would cut */
        pgb_strands(left, y, 1638u << ((variant >> 24) & 3u), 16384u, 0, 3328, 0, 256u, BR_LINE, BR_LINE);
    pgb_strands(left, y, 1638u << ((variant >> 24) & 3u), 16384u, 3328 - b, b, d ? 80 : 0, 256u, BR_ACC, BR_ACC);
    if (!depth) pgb_ruler(left, left + 40, y + 48);
    else if (a) pgb_arrow(x + 52, y + 17 + a, 0, 1, a, 384u, C_WHITE);
}

/* FX knob 4 (each effect's own fourth parameter). Still pictures. */
/* The bias picture's soft limit (q8 px, A = 12 px): the identity below the
 * knee L - w, flat at L above L + w, a parabola between (no corner). */
static __attribute__((minsize, noinline)) int32_t pgb_soft_clip(int32_t v)
{
    int32_t a = v < 0 ? -v : v, d = a - 2496;                   /* L 1.05 A, w 0.24 A */
    if (d > 0) a = d >= 1472 ? 3232 : a - d * d / 2944;
    return v < 0 ? -a : a;
}

/* DRIVE BIAS: the input rides up off the centre line by the offset added
 * before the shaper (0 .. 0.8 of full scale); its tops flatten against the
 * limit while its troughs stay round. A white bracket at the right measures
 * the offset. 0: the clean, symmetric wave on the line. */
static void bw_drive_bias(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                          const uint32_t env[4], uint32_t phase)
{
    int32_t px[33], py[33], b = (int32_t)q * 155 >> 3, cy = y + 32;     /* bias: 0.8 A at 127, q8 px */
    (void)s; (void)variant; (void)env; (void)phase;
    for (int32_t i = 0; i <= 32; ++i) {                          /* one and a half cycles */
        px[i] = bw_q8(x + 8) + i * 336;
        py[i] = bw_q8(cy) - pgb_soft_clip((bw_sine((uint32_t)i * 3072u) * 3 >> 4) + b);
    }
    for (int32_t i = 8; i < 50; i += 5) hd_line(x + i, cy - 14, x + i + 2, cy - 14, BR_LINE);   /* the limit, dashed */
    bw_graph(px, py, 33u, cy, BR_ACC);
    if (b >= 512) {
        int32_t top = cy - ((b + 128) >> 8);
        hd_line(x + 54, cy, x + 54, top, C_WHITE);
        hd_line(x + 52, top, x + 54, top, C_WHITE);
        hd_line(x + 52, cy, x + 54, cy, C_WHITE);
    }
}

/* MOD FEEDBACK: the modulated line's response, a comb whose teeth sharpen
 * into resonant peaks as more of the output goes back in (1 / |1 - g e^-jw|,
 * g to 0.75 for the picture), with white glints on the peaks; at 0 the
 * response is flat and has none. */
static void bw_mod_feedback(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                            const uint32_t env[4], uint32_t phase)
{
    int32_t px[31], py[31], g = (int32_t)(q * 192u / 127u);
    (void)s; (void)variant; (void)env; (void)phase;
    for (int32_t i = 0; i <= 30; ++i) {                          /* three teeth, troughs at the ends */
        int32_t c = bw_sine((uint32_t)i * 6554u + 49152u) >> 6;   /* cos, q8; peaks at i = 5, 15, 25 */
        int32_t m = 65536 / (int32_t)hd__isqrt((uint32_t)(65536 + g * g - 2 * g * c));
        px[i] = bw_q8(x + 8) + i * 375; py[i] = bw_q8(y + 48) - m * 7;
    }
    bw_graph(px, py, 31u, y + 48, BR_ACC);
    if (q) for (int32_t i = 5; i <= 25; i += 10) hd_circle_q8(px[i], py[i], 384, 0, C_WHITE);
}

/* REVERB PRE DELAY: an impulse response: the direct spike (white), the gap
 * bracketed above, then a dense decaying cluster of reflections. The gap is
 * the note value at 7 px per 1/32; AUTO (variant bits 8..15: where the
 * reverb puts it for the current TYPE, SIZE and tempo, in 1/192 beats) has a
 * dashed bracket. */
static void bw_pre_delay(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                         const uint32_t env[4], uint32_t phase)
{
    static const uint8_t ticks[12] = {0, 8, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96};   /* 1/192 beats */
    uint32_t note = variant & 15u;
    int32_t t = note ? ticks[note < 12u ? note : 11u] : (int32_t)((variant >> 8) & 255u);
    int32_t gap = t * 75, sx = bw_q8(x + 10) + gap;
    (void)s; (void)q; (void)env; (void)phase;
    pg_rule(x, y + 48);
    static const uint8_t jit[8] = {9, 4, 7, 2, 8, 5, 3, 6};
    hd_line(x + 9, y + 47, x + 9, y + 14, C_WHITE);
    hd_line_q8(sx, bw_q8(y + 47), sx, bw_q8(y + 47) - 28 * 256 * 17 / 16, 256u, bw_tone(BR_ACC, 230u));   /* the first, where it falls */
    for (int32_t i = 1, h = 28 * 228; (sx >> 8) + 2 * i <= x + 52; ++i, h = h * 228 >> 8)
        hd_line((sx >> 8) + 2 * i, y + 47, (sx >> 8) + 2 * i, y + 47 - (h * (8 + jit[i & 7]) >> 12), bw_tone(BR_ACC, 230u - 6u * (uint32_t)i));
    if ((sx >> 8) - 9 >= 3) {
        for (int32_t i = x + 10; i < (sx >> 8); i += note ? 1 : 3) hd_line(i, y + 14, i + (note ? 0 : 1), y + 14, BR_DIM);
        hd_line(sx >> 8, y + 12, sx >> 8, y + 16, BR_DIM);
    }
}

#else /* BR_PARAM_HOOKS */

/* FX pages (DRIVE, DELAY, MOD, REVERB; FX/SENDS keeps its strips) and both
 * punch pages. HOME OSC MIX and COMP MIX keep kind MIX (variant below). */
static uint32_t br_param_widget_b(uint32_t k, const br_control_t *c)
{
    static const uint8_t fx[4][2] = {{BR_WIDGET_DRIVE_GAIN, BR_WIDGET_TONE}, {BR_WIDGET_NONE, BR_WIDGET_ECHO},
                                     {BR_WIDGET_ORBIT, BR_WIDGET_STEREO_DEPTH}, {BR_WIDGET_NONE, BR_WIDGET_TAIL}};
    static const uint8_t own[4] = {BR_WIDGET_DRIVE_BIAS, BR_WIDGET_TONE, BR_WIDGET_MOD_FEEDBACK, BR_WIDGET_PRE_DELAY}   /* DELAY TONE shares the drive TONE picture */;
    static const uint8_t punch[4] = {BR_WIDGET_LOOP_LEN, BR_WIDGET_ECHO, BR_WIDGET_PUNCH_FILTER, BR_WIDGET_TREMOLO};
    uint32_t page = br_ui.screen[br_ui.page];
    (void)c;
    if (br_ui.page == BR_PAGE_FX && page != BR_FX_SENDS && k)    /* knob 4: each effect's own parameter */
        return k == 3u ? own[page - 1u] : fx[page - 1u][k - 1u];
    if (br_ui.page == BR_PAGE_PUNCH && (!page || k)) return punch[k];
    return BR_WIDGET_NONE;
}
/* Every MIX slot (OSC MIX, its target, COMP MIX); this batch's slots. A
 * answers the shared kinds on its own pages. */
static uint32_t br_param_variant_b(uint32_t k, const br_control_t *c, uint32_t kind)
{
    uint32_t page = br_ui.screen[br_ui.page];
    if (kind == BR_WIDGET_MIX) return BR_BLEND_OSC;
    if (br_ui.page == BR_PAGE_PUNCH) {
        const uint8_t *u = (const uint8_t *)&br_punch_cfg;   /* loop, echo time, filter shape, tremolo rate */
        return (uint32_t)u[0] | (uint32_t)u[1] << 8 | (uint32_t)u[2] << 16 | (uint32_t)u[3] << 24 |
            PGB_PUNCH | (page ? PGB_PAGE2 : 0u) | (uint32_t)(br_punch_cfg.trem_depth & 31u) << 10;
    }
    if (br_ui.page != BR_PAGE_FX || kind < BR_WIDGET_SEND || (kind > BR_WIDGET_TAIL && kind < BR_WIDGET_DRIVE_BIAS))
        return BR_PARAM_UNHANDLED;
    if (kind == BR_WIDGET_PRE_DELAY) {
        /* The note (0 AUTO, 1..11) and, for AUTO, where the reverb puts the onset: its TYPE's
         * pre-delay over SIZE (instrument_reverb2.c rv2_modes pre0..pre127, ms) at the tempo
         * the reverb uses, in 1/192 beats. */
        static const uint8_t pre[3][2] = {{0, 12}, {10, 45}, {20, 60}};
        uint32_t t = br_master.reverb_engine, bpm = br_bpm;   /* retired types as instrument_fx.c maps them (the audio unit's helper is not linked here) */
        t = t == BR_REVERB_SPRING ? BR_REVERB_ROOM : t >= BR_REVERB_ETHEREAL ? BR_REVERB_SPACE : t;
        const uint8_t *m = pre[t == 1u ? 1u : t == 3u ? 2u : 0u];
        bpm = bpm < BR_BPM_MIN ? BR_BPM_MIN : bpm > BR_BPM_MAX ? BR_BPM_MAX : bpm;
        t = (m[0] * 127u + (uint32_t)(m[1] - m[0]) * bw_clamp(br_master.reverb_size, 127u)) * bpm * 4u;
        return (uint32_t)c->value | ((t + 79375u) / 158750u) << 8;
    }
    if (kind == BR_WIDGET_DRIVE_GAIN) return br_part[br_ui.part].fx.drive_mode > 2u ? 0u : br_part[br_ui.part].fx.drive_mode;
    return 0u;
}
/* Companions travel in the variant (hashed with it), so no env. */
static uint32_t br_param_env_b(uint32_t k, const br_control_t *c, uint32_t kind, uint32_t env[4])
{
    (void)k; (void)c; (void)kind; (void)env;
    return 0u;
}
/* FX/MOD RATE turns with the evolving clock (still when silent and stopped). */
static uint32_t br_param_animated_b(uint32_t k, uint32_t kind)
{
    (void)k;
    return kind == BR_WIDGET_ORBIT && br_ui.page == BR_PAGE_FX ? BR_PARAM_ANIM_CLOCK : BR_PARAM_ANIM_NONE;
}

#endif /* BR_PARAM_HOOKS */
