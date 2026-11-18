/* SPDX-License-Identifier: GPL-3.0-only */
/* FX knob 4: each effect page's fourth knob shows that effect's own parameter (DRIVE BIAS, DELAY
 * TONE, MOD FEEDBACK, REVERB PRE DELAY), and the reverb time page's PRE DELAY (knob 1) the pre-delay
 * picture too, while its SEND (knob 4) keeps the SEND picture. Each new picture, across its range
 * on its real page: stays inside its cell, differs from one value to the next, does not depend on
 * the animation phase.
 * Writes each card as PPM into the crops folder (argv[1]) for review sheets. No device. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <stdio.h>
#include <stdlib.h>

#define CW 60
#define CH 134
static uint16_t pic[16][CW * CH];

/* The picture alone on a card canvas, as br_draw_control draws it. */
static void draw_alone(uint16_t *out, uint32_t kind, uint32_t q, uint32_t variant, uint32_t phase)
{
    static const uint32_t env[4] = {0u, 0u, 0u, 0u};
    cv_begin(CW, CH, BR_BACKGROUND); cv_oy = 0; cv_ox = 0;
    br_widget_draw(kind, 0, BR_PARAM_ICON_Y, 60u, q, variant, env, phase);
    memcpy(out, cv_px, sizeof pic[0]);
}

static void set_value(uint32_t screen, uint32_t v)
{
    br_part_pending_t *pp = &br_part_pending[br_ui.part];
    if (screen == BR_FX_DRIVE) pp->drive_bias = (uint8_t)v;
    else if (screen == BR_FX_DELAY) pp->delay_tone = (uint8_t)v;
    else if (screen == BR_FX_MOD) pp->mod_feedback = (uint8_t)v;
    else {
        br_master.reverb_extra[0] = (uint8_t)v;
        br_master.reverb_manual = v ? (br_master.reverb_manual | 32u) : (br_master.reverb_manual & ~33u);
    }
}

/* One page, one knob, values vals[0..n): checks, and the cards written as name-tag-i.ppm. */
static void sweep(const char *name, uint32_t screen, uint32_t k, uint32_t want, const uint32_t *vals, uint32_t n)
{
    char path[512];
    for (uint32_t alt = 0u; alt < 1u; ++alt) {
        for (uint32_t i = 0u; i < n; ++i) {
            br_control_t c;
            uint32_t kind, variant, q;
            int32_t range;
            set_value(screen, vals[i]);
            br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = screen;
            c = br_control(k);
            kind = br_control_widget(k, &c);
            HD_CHECK(kind == want, "%s value %u: kind %u, want %u", name, vals[i], kind, want);
            HD_CHECK(br_param_animated(k, kind) == BR_PARAM_ANIM_NONE, "%s: a still picture", name);
            variant = br_control_widget_variant(k, &c, kind);
            range = c.high - c.low;
            q = range > 0 ? (uint32_t)((c.value - c.low) * 127 / range) : 0u;
            draw_alone(pic[i], kind, q, variant, 0u);
            {
                hd_box_t b = hd_box(pic[i], CW, CH, BR_BACKGROUND, 1);
                HD_CHECK(b.right >= 0, "%s value %u: draws something", name, vals[i]);
                HD_CHECK(b.left >= 2 && b.right <= CW - 3 && b.top >= BR_PARAM_ICON_Y + 2 &&
                         b.bottom <= BR_PARAM_ICON_Y + 56,
                         "%s alt %u value %u: inside its cell (box %d,%d..%d,%d)", name, alt, vals[i],
                         b.left, b.top, b.right, b.bottom);
            }
            for (uint32_t p = 1u; p < 128u; p += 21u) {
                static uint16_t other[CW * CH];
                draw_alone(other, kind, q, variant, p);
                HD_CHECK(!memcmp(other, pic[i], sizeof other), "%s value %u: same at phase %u", name, vals[i], p);
            }
            for (uint32_t j = 0u; j < i; ++j)
                HD_CHECK(memcmp(pic[i], pic[j], sizeof pic[i]) != 0, "%s alt %u: values %u and %u differ",
                         name, alt, vals[j], vals[i]);
            br_ui.redraw = 1u; br_draw_layout_old = 0xffffffffu; br_draw();
            snprintf(path, sizeof path, "%s/%s-%c-%02u.ppm", hd_crop_dir, name, alt ? 'a' : 'p', i);
            hd_write_crop(path, fb, 240, 0, (int32_t)k * 60, 84, 60, 84, 1);
            if (!i || i == n - 1u) {
                snprintf(path, sizeof path, "%s/page-%s-%c-%02u.ppm", hd_crop_dir, name, alt ? 'a' : 'p', i);
                hd_write_crop(path, fb, 240, 0, 0, 0, 240, 240, 1);
            }
        }
    }
}

int main(int argc, char **argv)
{
    static const uint32_t pct[5] = {0u, 32u, 64u, 95u, 127u};
    static const uint32_t notes[12] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u};
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro();
    br_ui.part = 0u;
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_DRIVE;
    br_ui.redraw = 1u; br_draw();
    sweep("bias", BR_FX_DRIVE, 3u, BR_WIDGET_DRIVE_BIAS, pct, 5u);
    sweep("tone", BR_FX_DELAY, 3u, BR_WIDGET_TONE, pct, 5u);
    br_part[0].fx.delay_engine = BR_DELAY_TAPE;
    sweep("tonetape", BR_FX_DELAY, 3u, BR_WIDGET_TONE, pct, 5u);
    br_part[0].fx.delay_engine = BR_DELAY_DIGITAL;
    sweep("feedback", BR_FX_MOD, 3u, BR_WIDGET_MOD_FEEDBACK, pct, 5u);
    sweep("predelay", BR_FX_REVERB, 3u, BR_WIDGET_PRE_DELAY, notes, 12u);
    {
        /* AUTO follows the reverb's own choice: TYPE and SIZE (and the tempo). */
        uint32_t engine = br_master.reverb_engine, size = br_master.reverb_size;
        static const uint32_t auto0[1] = {0u};
        br_master.reverb_engine = BR_REVERB_SPACE; br_master.reverb_size = 127u;
        sweep("predelayauto", BR_FX_REVERB, 3u, BR_WIDGET_PRE_DELAY, auto0, 1u);
        memcpy(pic[15], pic[0], sizeof pic[0]);
        br_master.reverb_engine = BR_REVERB_HALL; br_master.reverb_size = 64u;
        sweep("predelayauto2", BR_FX_REVERB, 3u, BR_WIDGET_PRE_DELAY, auto0, 1u);
        HD_CHECK(memcmp(pic[15], pic[0], sizeof pic[0]) != 0, "AUTO pre-delay follows TYPE and SIZE");
        br_master.reverb_engine = engine; br_master.reverb_size = size;
    }
    /* The SEND picture stays where a send is the control. */
    {
        br_control_t c;
        br_ui.page = BR_PAGE_FX;
        HD_CHECK(br_target_widget(BR_TARGET_DIST_SEND) == BR_WIDGET_SEND, "the drive send target keeps the SEND picture");
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
        for (uint32_t k = 0u; k < 4u; ++k) {
            uint32_t kind;
            c = br_control(k); kind = br_control_widget(k, &c);
            HD_CHECK(kind < BR_WIDGET_DRIVE_BIAS || kind > BR_WIDGET_PRE_DELAY, "FX / SENDS knob %u: unchanged", k + 1u);
        }
        for (uint32_t s = BR_FX_DRIVE; s <= BR_FX_REVERB; ++s) {
            br_ui.screen[BR_PAGE_FX] = s;
            c = br_control(3u);
            HD_CHECK(br_control_widget(3u, &c) != BR_WIDGET_SEND, "FX page %u knob 4 is not a send", s);
        }
    }
    return hd_finish("FX knob 4 pictures");
}
