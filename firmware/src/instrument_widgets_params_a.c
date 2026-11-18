/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter graphics, batch A: track sound and modulation. HD only.
 * Guide: PARAM-GRAPHICS-BUILD.md. Designs D03 ENV AMOUNT, D04 KEY TRACK,
 * D05 TUNE, D06 ENV MODE, D13 SMOOTH, D27 LFO DEPTH, D28 attenuverter glyphs
 * (in bw_mod_atten), D29 PATCH AMOUNT, D30 ENABLE, D31 source pictures,
 * D36 target pictures (mapping).
 * Owns: this file; bw_env_amount, bw_keytrack, bw_tune, bw_env_mode,
 * bw_lfo_depth (also PATCH AMOUNT), bw_enable, bw_follower, bw_gate_source,
 * bw_velocity, bw_smooth; br_param_widget_a, br_param_variant_a,
 * br_param_env_a, br_param_animated_a. Also edits, in instrument_widgets.c,
 * the HD body of bw_mod_atten.
 *
 * Included twice: from instrument_widgets.c for the drawing half, and from
 * instrument_ui.c (BR_PARAM_HOOKS defined) for the mapping hooks, which may
 * use every mapping helper defined above br_control_widget. */
#ifndef BR_PARAM_HOOKS

/* Variants this batch's pictures take. */
#define PGA_MODE_RETRIG 1u                               /* ENV_MODE: bit 0 RETRIGGER (else LOOP), q = ON */

/* Signed amounts (ENV AMOUNT, PATCH AMOUNT) arrive exactly, as
 * value + 127 in the variant's upper bits, so +-1 does not read as zero. */
#define PGA_AMOUNT(v) ((int32_t)((v) >> 8) - 127)

/* Append n points (q8) of a fall from (x0, y0) to (x1, y1) that drops
 * steeply and then levels out (a quadratic with its control at a quarter of
 * the way along, at the end height): decays, releases, glides. */
static __attribute__((noinline)) uint32_t pga_fall(int32_t *px, int32_t *py, uint32_t at,
                                                   int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t n)
{
    int32_t cx = x0 + (x1 - x0) / 4, nn = n * n;
    for (int32_t i = 1; i <= n; ++i) {
        int32_t u = n - i;
        px[at] = (u * u * x0 + 2 * u * i * cx + i * i * x1) / nn;
        py[at++] = (u * u * y0 + (2 * u * i + i * i) * y1) / nn;
    }
    return at;
}
/* Load n points from a table of pixel offsets (x, y pairs) from the cell
 * origin, as q8 pixel centres. */
static __attribute__((noinline)) void pga_pts(int32_t *px, int32_t *py, int32_t x, int32_t y,
                                              const int8_t *t, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) { px[i] = bw_q8(x + t[2u * i]); py[i] = bw_q8(y + t[2u * i + 1u]); }
}

/* D04: a keyboard with the filter cutoff drawn over it. OFF (q 0): a flat
 * dim line, the same for every key; ON (q 127): rising with the keys. NOTE
 * (the patch source, q 96): a staircase, one step per key, the played key
 * lit. */
static void bw_keytrack(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    static const int8_t stair[16] = {10, 34, 20, 34, 20, 28, 30, 28, 30, 22, 40, 22, 40, 16, 50, 16};
    int32_t px[8], py[8], i, note = q == 96u, on = q > 96u;
    (void)s; (void)variant; (void)env; (void)phase;
    for (i = 0; i < 8; ++i) {                     /* white keys; each black key after its right neighbour */
        hd_rrect(x + 10 + 5 * i, y + 40, 4, 10, 1, note && i == 5 ? BR_ACC : hd_mix(BR_DIM, C_WHITE, 90u));
        if (i && i != 3 && i < 7) hd_rrect(x + 8 + 5 * i, y + 40, 3, 6, 1, RGB(12, 14, 15));
    }
    pga_pts(px, py, x, y, stair, 8u);
    if (!note) {                                  /* the cutoff line: from the stair's first and last points */
        py[0] = bw_q8(y + 26 + 8 * on); px[1] = px[7]; py[1] = bw_q8(y + 26 - 12 * on);
    }
    bw_path(px, py, note ? 8u : 2u, 512u, note || on ? BR_ACC : BR_DIM);
    hd_circle_q8(px[note ? 6 : 1], py[note ? 6 : 1] , 640, 0, note || on ? C_WHITE : BR_DIM);
}

/* D05: a note on a three-line staff, one step per semitone from its home
 * line (outlined when the note has left it). variant = semitones + 12. */
static void bw_tune(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                    const uint32_t env[4], uint32_t phase)
{
    int32_t st = (int32_t)bw_clamp(variant, 24u) - 12, ny = y + 30 - (st * 10 + (st < 0 ? -6 : 6)) / 12;
    (void)s; (void)q; (void)env; (void)phase;
    for (int32_t i = 20; i <= 40; i += 10) hd_line(x + 10, y + i, x + 50, y + i, i == 30 && !st ? BR_DIM : BR_LINE);
    if (st) hd_rrect_frame(x + 24, y + 27, 11, 7, 3, BR_DIM);           /* home */
    hd_rrect_grad(x + 24, ny - 3, 11, 7, 3, hd_lighten(BR_ACC, 120u), BR_ACC);
    hd_rrect(x + 33, ny - 15, 2, 14, 0, BR_ACC);                          /* stem */
}

/* Envelope trains: one byte per hump, its end at x+8 + 2 (b & 31), its end
 * level 2 (b >> 5) px above the rule; 0 ends the train. Each hump attacks
 * over 5 px to top, then falls. Trains: D06 LOOP OFF (0), LOOP ON (2),
 * RETRIGGER OFF (6), RETRIGGER ON (8); D03's single hump (11). */
static const uint8_t pga_humps[13] = {7, 0, 7, 15, 22, 0, 22 | 2 << 5, 0, 11 | 5 << 5, 22 | 4 << 5, 0, 22, 0};
static __attribute__((noinline)) uint32_t pga_train(int32_t *px, int32_t *py, int32_t x, int32_t base,
                                                    int32_t top, uint32_t at)
{
    const uint8_t *h = pga_humps + at;
    uint32_t n = 1u;
    px[0] = bw_q8(x + 8); py[0] = base;
    for (; *h; ++h) {
        if (py[n - 1u] != base) { px[n] = px[n - 1u]; py[n++] = base; }      /* restarted from zero */
        px[n] = px[n - 1u] + 1280; py[n] = top;
        n = pga_fall(px, py, n + 1u, px[n], top, bw_q8(x + 8 + 2 * (*h & 31u)), base - (int32_t)(*h >> 5) * 512, 6);
    }
    return n;
}
/* D03: an envelope hump pushed up (positive) or hanging down (negative)
 * from the centre rule by the amount; a white chevron beyond the peak says
 * which way. Zero: a flat line on the rule and a faint ghost of the hump. */
static void bw_env_amount(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                          const uint32_t env[4], uint32_t phase)
{
    int32_t a = PGA_AMOUNT(variant), sg = a < 0 ? -1 : 1, base = bw_q8(y + 30);
    int32_t top = base - (a ? sg * 512 + 4608 * a / 127 : 2048), px[12], py[12];
    uint32_t n = pga_train(px, py, x, base, top, 11u);
    (void)s; (void)q; (void)env; (void)phase;
    pg_rule(x, y + 30);
    hd_line(x + 8, y + 26, x + 8, y + 34, BR_DIM);                       /* the zero tick */
    if (!a) {
        bw_path(px, py, n, 256u, BR_LINE);
        hd_line_q8(px[0], base, px[n - 1u], base, 448u, BR_DIM);
        return;
    }
    hdp_curve(px, py, n, base, 448u, BR_ACC);
    px[0] = px[1] - 768; px[2] = px[1] + 768;                /* chevron 4 px beyond the peak */
    py[1] = top - sg * 1024; py[0] = py[2] = py[1] + sg * 768;
    bw_path(px, py, 3u, 448u, C_WHITE);
}

/* D06: LOOP turns one envelope hump into a cycle (OFF: one hump, then rest;
 * ON: repeated humps). RETRIGGER: two key presses; ON restarts the envelope at the second
 * (white drop), OFF lets one long decay ignore it (its key mark dims). */
static void bw_env_mode(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    int32_t px[24], py[24], on = q > 63u, rt = (int32_t)(variant & PGA_MODE_RETRIG);
    (void)s; (void)env; (void)phase;
    pg_rule(x, y + 42);
    if (rt) {
        hd_rrect(x + 8, y + 45, 5, 5, 1, BR_DIM);
        hd_rrect(x + 28, y + 45, 5, 5, 1, on ? BR_DIM : BR_LINE);           /* the second press */
    } else if (!on) hd_line_q8(bw_q8(x + 22), bw_q8(y + 42), bw_q8(x + 52), bw_q8(y + 42), 448u, BR_DIM);   /* then rest */
    hdp_curve(px, py, pga_train(px, py, x, bw_q8(y + 42), bw_q8(y + 17), (uint32_t)(rt * 6 + on * 2)), bw_q8(y + 42), 448u, BR_ACC);
    if (rt && on) hd_line(x + 30, y + 32, x + 30, y + 41, C_WHITE);
}

/* D13: the knob jumps (the dim step); the filter glides after it, slower
 * with more smoothing (exponential approach, one q8 factor per 2 px). */
static void bw_smooth(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                      const uint32_t env[4], uint32_t phase)
{
    static const int8_t step[8] = {8, 42, 18, 42, 18, 16, 52, 16};
    int32_t px[19], py[19], i, v = 26 * 256, tau = 5 + (int32_t)q * 480 / 127, f = 256 * tau / (tau + 32);
    (void)s; (void)variant; (void)env; (void)phase;
    pga_pts(px, py, x, y, step, 4u);
    bw_path(px, py, 4u, 448u, BR_LINE);
    px[0] = bw_q8(x + 8); py[0] = bw_q8(y + 42);
    for (i = 1; i < 19; ++i) { px[i] = bw_q8(x + 16 + 2 * i); py[i] = bw_q8(y + 16) + v; v = v * f >> 8; }
    hdp_curve(px, py, 19u, bw_q8(y + 42), 448u, BR_ACC);
}

/* D27: how far the LFO swings its target: a bracket around the centre of a
 * rail, a white dot riding it at the LFO's phase. PATCH AMOUNT (variant bit 0,
 * the signed amount above it, see PGA_AMOUNT): the same rail with the bracket
 * running from the centre mark to the amount (up positive, down negative) and
 * the dot parked at its end. */
static void bw_lfo_depth(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                         const uint32_t env[4], uint32_t phase)
{
    int32_t a = (variant & 1u) ? PGA_AMOUNT(variant) : 0, d = ((variant & 1u) ? (a < 0 ? -a : a) : (int32_t)q) * 22 / 127;
    int32_t cx = x + 30, cy = y + 30, top = cy - (a < 0 ? 0 : d), end = cy - (a < 0 ? -d : d);
    (void)s; (void)env;
    hd_line(cx, y + 7, cx, y + 53, BR_LINE);
    if (d) {
        hd_rrect(cx - 3, (variant & 1u) ? top : cy - d, 7, (variant & 1u) ? d + 1 : 2 * d + 1, 2, bw_tone(BR_ACC, 90u));   /* the swing, with end caps */
        hd_rrect(cx - 5, end - 1, 11, 2, 1, BR_ACC);
        if (!(variant & 1u)) hd_rrect(cx - 5, cy + d, 11, 2, 1, BR_ACC);
    }
    hd_line(cx - 5, cy, cx + 5, cy, BR_DIM);
    if (variant & 1u) hd_circle_q8(bw_q8(cx), bw_q8(end), 768, 0, C_WHITE);              /* parked at the amount */
    else BW_MARK(hd_circle_q8(bw_q8(cx), bw_q8(cy) - d * bw_sine(phase * 512u) / 64, 768, 0, C_WHITE));   /* moves: a mark */
}

/* D30: plugged in (lit socket, ACC cable) or lifted out (dim cable). */
static void bw_enable(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                      const uint32_t env[4], uint32_t phase)
{
    int32_t on = q > 63u, top = y + (on ? 22 : 12);
    (void)s; (void)variant; (void)env; (void)phase;
    hd_rrect(x + 22, y + 38, 16, 10, 3, RGB(28, 33, 36));
    hd_rrect(x + 27, y + 41, 6, 4, 1, on ? BR_ACC : RGB(3, 4, 4));   /* lit when plugged in */
    hd_rrect(x + 29, y + 6, 2, top - y - 6, 0, on ? BR_ACC : BR_DIM);
    hd_rrect_grad(x + 26, top, 8, 16, 2, RGB(214, 220, 224), RGB(120, 128, 132));
    if (!on) hd_rrect(x + 29, top + 16, 2, 4, 0, RGB(176, 184, 188));     /* the tip, unseated */
}

/* D31 OSC AVG / MIX AVG: audio (one dim wave, or the three tracks' waves)
 * with its envelope follower traced over the top. variant 1 = MIX. */
static void bw_follower(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    int32_t px[36], py[36], e[36], i, mid = bw_q8(y + 31);
    (void)s; (void)q; (void)env; (void)phase;
    for (i = 0; i < 36; ++i) {                    /* MIX: a slower follower over an uneven sum */
        e[i] = 2304 + 1280 * bw_sine((uint32_t)i * (3744u >> variant) + 49152u) / 16384;
        px[i] = bw_q8(x + 8) + i * 11264 / 35;
        py[i] = mid - e[i] * bw_sine((uint32_t)i * (26000u + variant * 9000u)) / (16384 + (int32_t)variant * (i % 3) * 8192);
    }
    bw_path(px, py, 36u, 256u, variant ? bw_highlight(0u) : BR_DIM);
    for (i = 0; i < 18; ++i) { px[i] = px[2 * i]; py[i] = mid - e[2 * i] - 128; }
    px[17] = bw_q8(x + 52);
    bw_path(px, py, 18u, 448u, BR_ACC);
}

/* D31 GATE: one key press as one gate: a single rectangular pulse (high
 * while the key under it is held), not the repeating LFO square. */
static void bw_gate_source(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                           const uint32_t env[4], uint32_t phase)
{
    static const int8_t pulse[12] = {8, 40, 16, 40, 16, 16, 44, 16, 44, 40, 52, 40};
    int32_t px[6], py[6];
    (void)s; (void)q; (void)variant; (void)env; (void)phase;
    pga_pts(px, py, x, y, pulse, 6u);
    hdp_curve(px, py, 6u, bw_q8(y + 40), 448u, BR_ACC);
    hd_rrect(x + 16, y + 44, 29, 7, 1, hd_mix(BR_DIM, C_WHITE, 90u));    /* the key, held for the gate */
}
/* D31 VELOCITY: the sequencer's velocity lollipops (the step tile's stem and
 * ball) over a floor: each note its own height, the newest lit. */
static void bw_velocity(int32_t x, int32_t y, uint32_t s, uint32_t q, uint32_t variant,
                        const uint32_t env[4], uint32_t phase)
{
    static const uint8_t h[3] = {14, 26, 33};
    (void)s; (void)q; (void)variant; (void)env; (void)phase;
    pg_rule(x, y + 46);
    for (int32_t i = 0; i < 3; ++i)
        pg_lollipop(x + 16 + 14 * i, y + 46 - h[i], y + 46, i == 2 ? BR_ACC : bw_tone(BR_ACC, 140u), i == 2);
}

#else /* BR_PARAM_HOOKS */

/* The kind for slot k, or BR_WIDGET_NONE to leave it to the next batch and
 * then to br_control_widget's own mapping. */
static uint32_t br_param_widget_a(uint32_t k, const br_control_t *c)
{
    /* D31: PATCH source pictures, by source id (0: keep the existing one). */
    static const uint8_t source[16] = {
        [BR_SOURCE_OFF] = BR_WIDGET_PATCH_OFF, [BR_SOURCE_OSC] = BR_WIDGET_FOLLOWER,
        [BR_SOURCE_MIX] = BR_WIDGET_FOLLOWER, [BR_SOURCE_KEY] = BR_WIDGET_KEYTRACK,
        [BR_SOURCE_GATE] = BR_WIDGET_GATE_SOURCE, [BR_SOURCE_VELOCITY] = BR_WIDGET_VELOCITY};
    uint32_t page = br_ui.screen[br_ui.page], t = br_target_control_id(k), s = br_source_control_id(k);
    if (t != BR_TARGET_COUNT)                       /* D36: LFO, PATCH and ENV/SETUP targets */
        return t == BR_TARGET_OFF || !br_target_ui_available(t) ? BR_WIDGET_PATCH_OFF :
            t == BR_TARGET_PITCH ? BR_WIDGET_TUNE :
            (t >= BR_TARGET_LFO1_RATE && t <= BR_TARGET_LFO2_RATE) ? BR_WIDGET_ORBIT :
            (t - BR_TARGET_DELAY <= 2u || t == BR_TARGET_DIST_SEND) ? BR_WIDGET_SEND : BR_WIDGET_NONE;
    if (s != BR_SOURCE_COUNT) return s < 16u && source[s] ? source[s] : BR_WIDGET_NONE;
    if (br_home_filter_page() || (br_ui.page == BR_PAGE_MIX && page == 1u)) {
        if (k < 2u) return BR_WIDGET_NONE;
        return k == 2u ? BR_WIDGET_ENV_AMOUNT : br_ui.page == BR_PAGE_HOME ? BR_WIDGET_MONITOR : BR_WIDGET_SMOOTH;
    }
    if (br_ui.page == BR_PAGE_ENV && page == 2u && k != 1u) return k ? BR_WIDGET_ENV_MODE : BR_WIDGET_ENV_AMOUNT;
    if (br_ui.page == BR_PAGE_LFO && (k & 1u)) return k == 1u ? BR_WIDGET_ORBIT : BR_WIDGET_LFO_DEPTH;
    if (br_ui.page == BR_PAGE_ROUTE && page < 2u && !br_gate_page() && k >= 2u)
        return k == 2u ? BR_WIDGET_LFO_DEPTH : BR_WIDGET_ENABLE;
    if (br_ui.page == BR_PAGE_HOME && !page && !k && br_widget_is(c->label, "PITCH")) return BR_WIDGET_TUNE;
    return BR_WIDGET_NONE;
}
/* The variant for slot k drawn as kind, or BR_PARAM_UNHANDLED. */
static uint32_t br_param_variant_a(uint32_t k, const br_control_t *c, uint32_t kind)
{
    uint32_t t = br_target_control_id(k), s = br_source_control_id(k);
    if (kind == BR_WIDGET_ORBIT)                    /* LFO RATE on the LFO page, or an LFO RATE target */
        return t != BR_TARGET_COUNT ? BR_ORBIT_TARGET_LFO1 + t - BR_TARGET_LFO1_RATE :
            br_ui.page == BR_PAGE_LFO ? BR_ORBIT_LFO : BR_PARAM_UNHANDLED;
    if (kind == BR_WIDGET_SEND)                     /* DELAY MIX, REVERB SEND, MOD FX MIX targets */
        return t != BR_TARGET_COUNT ? t == BR_TARGET_DIST_SEND ? BR_SEND_DRIVE : t == BR_TARGET_DELAY ? BR_SEND_DELAY : t == BR_TARGET_REVERB ? BR_SEND_REVERB :
            BR_SEND_MOD : BR_PARAM_UNHANDLED;
    if (kind < BR_WIDGET_ENV_AMOUNT || kind > BR_WIDGET_SMOOTH) return BR_PARAM_UNHANDLED;
    if (kind == BR_WIDGET_TUNE) return t != BR_TARGET_COUNT ? 17u : (uint32_t)(c->value + 12);   /* target: +5 st */
    if (kind == BR_WIDGET_ENV_AMOUNT) return (uint32_t)(c->value + 127) << 8;
    if (kind == BR_WIDGET_LFO_DEPTH && br_ui.page == BR_PAGE_ROUTE)       /* PATCH AMOUNT: signed, bit 0 marks it */
        return ((uint32_t)(c->value + 127) << 8) | 1u;
    if (kind == BR_WIDGET_ENV_MODE) return k == 3u ? PGA_MODE_RETRIG : 0u;
    if (kind == BR_WIDGET_FOLLOWER) return s == BR_SOURCE_MIX;
    return 0u;
}
/* Fill env[0..3] with companion values for slot k and return 1, or return 0
 * and leave env untouched. ENV AMOUNT draws the mod envelope's own shape. */
static uint32_t br_param_env_a(uint32_t k, const br_control_t *c, uint32_t kind, uint32_t env[4])
{
    (void)k; (void)c; (void)kind; (void)env;
    return 0u;
}
/* LFO RATE and DEPTH follow the oscillator shown on the current page. */
static uint32_t br_param_animated_a(uint32_t k, uint32_t kind)
{
    uint32_t screen = br_ui.screen[BR_PAGE_LFO];
    if (br_ui.page != BR_PAGE_LFO || screen > BR_LFOS ||
        (kind != BR_WIDGET_ORBIT && kind != BR_WIDGET_LFO_DEPTH) ||
        br_target_control_id(k) != BR_TARGET_COUNT) return BR_PARAM_ANIM_NONE;
    return screen == BR_LFOS ? BR_PARAM_ANIM_GLOBAL_LFO : BR_PARAM_ANIM_LFO1 + screen;
}

#endif /* BR_PARAM_HOOKS */
