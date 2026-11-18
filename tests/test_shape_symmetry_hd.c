/* SPDX-License-Identifier: GPL-3.0-only */
/* HD shapes (built by run_shape_symmetry_hd.py). The HD primitives draw the same arguments as smooth anti-aliased
 * geometry at native resolution, so symmetry is checked on exact colours, not on a 2x2 grid:
 *  - filled and ring circles (br_shape_circle, br_shape_circle_grid), every centre 27..30 and
 *    radius 2..14: each mirrors exactly left-right and top-bottom about its own extent;
 *  - a round-capped line mirrors both ways; symmetric arcs mirror left-right;
 *  - the toggle: its track mirrors left-right about the control centre (outside the puck), and the
 *    puck sits exactly on the track end (q 0: left, q 127: right), itself symmetric;
 *  - the gate pulse mirrors left-right about the centre of its 60-pixel cell for every amount,
 *    open or not, and widens with the amount.
 * Gradient surfaces are ordered-dithered (HD-SPEC), and a dither pattern does not mirror: there a
 * mirrored pixel may differ by one RGB565 step per channel, never more. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

static void mirrored(const char *what, int horizontal, int vertical)
{
    hd_box_t b = hd_box(cv_px, 60, 60, C_BLACK, 1);
    HD_CHECK(b.right >= b.left && b.bottom >= b.top, "%s draws something", what);
    if (horizontal) HD_CHECK(!hd_mirror_x(cv_px, 60, b), "%s: %u pixels differ from their left-right mirror",
                             what, hd_mirror_x(cv_px, 60, b));
    if (vertical) HD_CHECK(!hd_mirror_y(cv_px, 60, b), "%s: %u pixels differ from their top-bottom mirror",
                           what, hd_mirror_y(cv_px, 60, b));
}

static int neutral_bright(uint16_t swapped)
{
    uint16_t c = swap16(swapped);
    return ((c >> 11) & 31u) >= 19u && ((c >> 5) & 63u) >= 38u && (c & 31u) >= 19u;
}

int main(int argc, char **argv)
{
    char what[96];
    if (argc > 1) hd_crop_dir = argv[1];
    for (int cx = 27; cx <= 30; cx++) for (int cy = 27; cy <= 30; cy++) for (int r = 2; r <= 14; r++) {
        cv_begin(60, 60, C_BLACK); br_shape_circle(bw_q8(cx), bw_q8(cy), r * 256u, 0, C_WHITE, 1);
        snprintf(what, sizeof what, "filled circle (%d,%d) r%d", cx, cy, r); mirrored(what, 1, 1);
        cv_begin(60, 60, C_BLACK); br_shape_circle(bw_q8(cx), bw_q8(cy), r * 256u, 512u, C_WHITE, 0);
        snprintf(what, sizeof what, "ring (%d,%d) r%d", cx, cy, r); mirrored(what, 1, 1);
        cv_begin(60, 60, C_BLACK); br_shape_circle_grid(bw_q8(cx), bw_q8(cy), r * 256u, 0, C_WHITE, 1, 1);
        snprintf(what, sizeof what, "fine filled circle (%d,%d) r%d", cx, cy, r); mirrored(what, 1, 1);
        cv_begin(60, 60, C_BLACK); br_shape_circle_grid(bw_q8(cx), bw_q8(cy), r * 256u, 256u, C_WHITE, 0, 1);
        snprintf(what, sizeof what, "fine ring (%d,%d) r%d", cx, cy, r); mirrored(what, 1, 1);
        cv_begin(60, 60, C_BLACK); br_shape_circle_grid(bw_q8(cx), bw_q8(cy), r * 256u, 512u, C_WHITE, 0, 2);
        snprintf(what, sizeof what, "grid ring (%d,%d) r%d", cx, cy, r); mirrored(what, 1, 1);
    }
    cv_begin(60, 60, C_BLACK); bw_line(15, 30, 45, 30, 5120u, C_WHITE); mirrored("round-capped line", 1, 1);
    for (int r = 5; r <= 22; r++) for (int center = 27; center <= 30; center++) {
        cv_begin(60, 60, C_BLACK); bw_arc(bw_q8(center), bw_q8(center), r * 256u, 512u, 40960u, 49152u, C_WHITE);
        snprintf(what, sizeof what, "knob arc c%d r%d", center, r); mirrored(what, 1, 0);
        cv_begin(60, 60, C_BLACK); bw_arc(bw_q8(center), bw_q8(center), r * 256u, 512u, 49152u, 32768u, C_WHITE);
        snprintf(what, sizeof what, "upper arc c%d r%d", center, r); mirrored(what, 1, 0);
    }
    /* Toggle (size 60): track from x 15 to 45 (radius 10), puck radius 8 centred on an end. */
    for (int q = 0; q <= 127; q += 127) {
        const int32_t cx = q ? 45 : 15, knob = 8;
        cv_begin(60, 60, C_BLACK); bw_toggle(0, 0, 60, (uint32_t)q);
        hd_box_t track = hd_box(cv_px, 60, 60, C_BLACK, 1), puck = {60, 60, -1, -1};
        uint32_t odd = 0;
        HD_CHECK(track.left == 5 && track.right == 55, "toggle %d: track spans x %d..%d (5..55)", q, track.left, track.right);
        for (int32_t y = track.top; y <= track.bottom; ++y) for (int32_t x = track.left; x <= track.right; ++x) {
            int32_t m = track.left + track.right - x;
            if (abs(x - cx) <= knob + 2 || abs(m - cx) <= knob + 2) continue;
            odd += !hd_near(cv_px[y * 60 + x], cv_px[y * 60 + m], 1u, 1);   /* dithered gradient */
        }
        HD_CHECK(!odd, "toggle %d: %u track pixels (outside the puck) differ from their mirror by more than "
                 "one dither step", q, odd);
        for (int32_t y = 0; y < 60; ++y) for (int32_t x = 0; x < 60; ++x) if (neutral_bright(cv_px[y * 60 + x])) {
            if (x < puck.left) puck.left = x; if (x > puck.right) puck.right = x;
            if (y < puck.top) puck.top = y; if (y > puck.bottom) puck.bottom = y;
        }
        HD_CHECK(puck.left + puck.right == 2 * cx && puck.right - puck.left <= 2 * knob + 2 && puck.right - puck.left >= 2 * knob - 2,
                 "toggle %d: puck x %d..%d is centred on the track end %d", q, puck.left, puck.right, cx);
        HD_CHECK(puck.top + puck.bottom == 60 || puck.top + puck.bottom == 61,
                 "toggle %d: puck y %d..%d is centred on the track (y 30)", q, puck.top, puck.bottom);
    }
    /* Gate pulse: bars at centre-2-2i and centre+2i mirror about x 29.5, so must everything else. */
    {
        uint32_t worst = 0, worst_q = 0, worst_open = 0;
        static uint16_t worst_px[60 * 60];
        for (uint32_t q = 0u; q <= 127u; ++q) for (uint32_t open = 0u; open <= 1u; ++open) {
            uint32_t n = 0;
            cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, q, open);
            for (uint32_t y = 0u; y < 60u; ++y) for (uint32_t x = 0u; x < 60u; ++x)
                n += !hd_near(cv_px[y * 60u + x], cv_px[y * 60u + 59u - x], 1u, 1);   /* dithered bars */
            if (n > worst) { worst = n; worst_q = q; worst_open = open; memcpy(worst_px, cv_px, sizeof worst_px); }
        }
        if (worst) {
            char path[512];
            snprintf(path, sizeof path, "%s/gate_q%u_open%u_x8.ppm", hd_crop_dir, worst_q, worst_open);
            hd_write_crop(path, worst_px, 60, 1, 0, 16, 60, 28, 8);
        }
        HD_CHECK(!worst, "gate pulse is not left-right symmetric about x 29.5: up to %u pixels differ by more "
                 "than one dither step "
                 "(q %u, open %u; crop written)", worst, worst_q, worst_open);
    }
    {
        static uint16_t narrow[60u * 60u], medium[60u * 60u];
        hd_box_t a, b, c;
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, 8u, 0u); memcpy(narrow, cv_px, sizeof narrow);
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, 64u, 0u); memcpy(medium, cv_px, sizeof medium);
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, 120u, 0u);
        HD_CHECK(memcmp(narrow, medium, sizeof narrow) && memcmp(medium, cv_px, sizeof medium),
                 "gate: amounts 8, 64 and 120 draw differently");
        a = hd_box(narrow, 60, 60, C_BLACK, 1); b = hd_box(medium, 60, 60, C_BLACK, 1); c = hd_box(cv_px, 60, 60, C_BLACK, 1);
        HD_CHECK(a.bottom - a.top <= b.bottom - b.top && b.bottom - b.top <= c.bottom - c.top,
                 "gate: the pulse grows with the amount (heights %d, %d, %d)",
                 a.bottom - a.top, b.bottom - b.top, c.bottom - c.top);
    }
    return hd_finish("HD circles, rings, line, arcs, toggle and gate symmetry");
}
