/* SPDX-License-Identifier: GPL-3.0-only */
/* HD MIX icon. Two anti-aliased
 * strands converge at the output: their shape mirrors top-bottom about the strand axis (q8
 * y = cy * 256 with cy = ((y + s/2) & ~1) + 1, the boundary between rows cy-1 and cy) for every knob
 * value, at both sizes. The two strands differ in brightness, and the dimmer
 * one's faintest anti-aliased fringe rounds to black in RGB565, so the shape test is: every clearly
 * visible pixel (luma > 48) has a lit mirror pixel within one row. At the left end, where the
 * strands start apart, the strand the knob favours is the brighter (q 0: the lower, q 127: the
 * upper, as the standard icon tones 90 and 255). The strands and the glints move with the knob;
 * the picture is still (OSC MIX through br_widget_draw is the same at every phase), and COMP MIX
 * (the PAR variant) keeps its own picture. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

/* The brightest pixel of column x above (upper) or below the axis between rows cy-1 and cy. */
static uint32_t peak(int32_t x, int32_t cy, int upper)
{
    uint32_t best = 0;
    for (int32_t y = upper ? 0 : cy; y < (upper ? cy : 60); ++y) {
        uint32_t l = hd_luma(swap16(cv_px[y * 60 + x]));
        if (l > best) best = l;
    }
    return best;
}
static uint32_t lit_at(int32_t x, int32_t y)
{
    return y >= 0 && y < 60 && cv_px[y * 60 + x] != swap16(C_BLACK);
}

int main(int argc, char **argv)
{
    static uint16_t low[60 * 60];
    uint32_t env[4] = {0};
    if (argc > 1) hd_crop_dir = argv[1];
    for (uint32_t n = 0; n < 2; n++) {
        int32_t x = n ? 16 : 0, y = n ? 16 : 0; uint32_t size = n ? 28 : 60;
        int32_t cy = ((y + (int32_t)size / 2) & ~1) + 1;
        for (uint32_t q = 0; q <= 127u; ++q) {
            uint32_t odd = 0;
            cv_begin(60, 60, C_BLACK); bw_mix(x, y, size, q);
            for (int32_t py = 0; py < 60; ++py) for (int32_t px = 0; px < 60; ++px) {
                int32_t my = 2 * cy - 1 - py;
                if (hd_luma(swap16(cv_px[py * 60 + px])) > 48u &&
                    !lit_at(px, my) && !lit_at(px, my - 1) && !lit_at(px, my + 1)) ++odd;
            }
            HD_CHECK(!odd, "size %u q %u: %u visible pixels without a lit mirror about the axis "
                     "between rows %d and %d", size, q, odd, cy - 1, cy);
            memcpy(low, cv_px, sizeof low);
            for (uint32_t phase = 0; phase < 128u; phase += 37u) {
                cv_begin(60, 60, C_BLACK); br_widget_draw(BR_WIDGET_MIX, x, y, size, q, BR_BLEND_OSC, env, phase);
                HD_CHECK(!memcmp(low, cv_px, sizeof low), "size %u q %u phase %u: OSC MIX draws the still "
                         "1.0a picture", size, q, phase);
            }
        }
        {
            int32_t left = ((x + (int32_t)size / 6) & ~1) + 1;          /* the first column's centre */
            cv_begin(60, 60, C_BLACK); bw_mix(x, y, size, 0); memcpy(low, cv_px, sizeof low);
            HD_CHECK(peak(left, cy, 0) > peak(left, cy, 1), "size %u q 0: the lower strand is the brighter "
                     "(%u vs %u)", size, peak(left, cy, 0), peak(left, cy, 1));
            cv_begin(60, 60, C_BLACK); bw_mix(x, y, size, 127);
            HD_CHECK(peak(left, cy, 1) > peak(left, cy, 0), "size %u q 127: the upper strand is the brighter "
                     "(%u vs %u)", size, peak(left, cy, 1), peak(left, cy, 0));
        }
        {
            int moved = 0;
            for (int32_t i = 0; i < 60 * 60; ++i)
                moved |= (cv_px[i] != swap16(C_BLACK)) != (low[i] != swap16(C_BLACK));
            HD_CHECK(moved, "size %u: the strands move with the knob", size);
        }
        memcpy(low, cv_px, sizeof low);
        cv_begin(60, 60, C_BLACK); bw_mix(x, y, size, 126);
        HD_CHECK(memcmp(low, cv_px, sizeof low), "size %u: turning the knob moves the glints", size);
        cv_begin(60, 60, C_BLACK); br_widget_draw(BR_WIDGET_MIX, x, y, size, 127, BR_BLEND_PAR, env, 0);
        HD_CHECK(!memcmp(low, cv_px, sizeof low), "size %u: COMP MIX uses the homepage MIX picture", size);
        {
            hd_box_t b = hd_box(low, 60, 60, C_BLACK, 1);
            HD_CHECK(b.left >= x && b.right < x + (int32_t)size && b.top >= y && b.bottom < y + (int32_t)size,
                     "size %u: the icon stays inside its %u px cell (x %d..%d, y %d..%d)", size, size,
                     b.left, b.right, b.top, b.bottom);
        }
    }
    return hd_finish("HD OSC MIX strands mirror, follow the knob and hold still at every phase");
}
