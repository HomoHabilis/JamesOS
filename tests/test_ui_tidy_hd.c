/* SPDX-License-Identifier: GPL-3.0-only */
/* HD tidy details: rounded cue keys
 * (SAV square), the OCT- + SNAP cue clear of button and knob, the audition icon's five white keys and
 * raised black keys, the amount vessel's gutters at every level and glint phase, no blit for stable
 * page dots and the lit dot in place, the LFO header badge equal to the named badge, the compressor
 * meter centred, and the settings page disabling both OCT buttons and their LEDs. */
#define DINGHY_HOST 1
#define UI_REFRESH_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro(); fm1_ms = 5000u;
    {   /* Cue-button text keeps its foreground and gains a dark offset shadow. */
        uint16_t shadowed[32u * 20u]; uint32_t difference = 0u;
        cv_begin(32, 20, C_BLACK); hdc_key(1, 2, 30, 16);
        hdc_key_text(5, 6, "OCT-", C_WHITE); memcpy(shadowed, cv_px, sizeof shadowed);
        cv_begin(32, 20, C_BLACK); hdc_key(1, 2, 30, 16); br_widget_text(5, 6, "OCT-", C_WHITE);
        for (uint32_t i = 0; i < 32u * 20u; ++i) difference += shadowed[i] != cv_px[i];
        HD_CHECK(difference > 0u, "cue-button lettering has a visible shadow");
    }
    /* Button cues have rounded corners; SAV retains a square footprint. */
    cv_begin(32, 20, C_BLACK); br_cue_round_button(1, 2, 30, 16);
    HD_CHECK(cv_px[2u * cv_w + 1u] == swap16(C_BLACK), "cv_px[2u * cv_w + 1u] == swap16(C_BLACK)");
    HD_CHECK(cv_px[2u * cv_w + 3u] == swap16(BR_LINE), "cv_px[2u * cv_w + 3u] == swap16(BR_LINE)");
    br_ui.message[0] = 0; br_ui.message_until = 0;
    br_draw_cue_bar(2u, 1u);
    {
        uint32_t b1 = 22u;
        uint32_t x1 = (240u - (b1 + 5u + br_widget_text_width("NEXT"))) / 2u;
        /* HD key: rounded, so its corner pixels are darker than its edges a few pixels in */
        HD_CHECK(hd_luma(fb[217u * 240u + x1]) < hd_luma(fb[217u * 240u + x1 + 4u]) &&
                 hd_luma(fb[217u * 240u + x1 + 4u]) >= 100u, "SAV key: rounded top-left corner, lit top edge");
        HD_CHECK(hd_luma(fb[238u * 240u + x1 + b1 - 1u]) < hd_luma(fb[238u * 240u + x1 + b1 - 5u]) &&
                 hd_luma(fb[238u * 240u + x1 + b1 - 5u]) > hd_luma(fb[238u * 240u + x1 - 3u]),
                 "SAV key: rounded bottom-right corner, its (darker) bottom edge drawn");
    }
    /* The wider plus in OCT- + SNAP stays clear of both button and knob. */
    br_draw_cue_bar(8u, 1u);
    {
        int32_t width=30+6+br_widget_text_width("+")+6+12+6+br_widget_text_width("SNAP");
        int32_t button=(240-width)/2, plus=button+36, knob=plus+br_widget_text_width("+")+6;
        HD_CHECK(button>=0 && knob+18+br_widget_text_width("SNAP")<=240, "button>=0 && knob+18+br_widget_text_width('SNAP')<=240");
        HD_CHECK(plus-(button+30)>=6 && knob-(plus+br_widget_text_width("+"))>=6, "plus-(button+30)>=6 && knob-(plus+br_widget_text_width('+'))>=6");
        {   /* HD type: the plus is drawn inside its advance and nothing lights the gaps beside it */
            uint32_t inside = 0, gaps = 0;
            for (int32_t y = 218; y < 238; ++y) {
                for (int32_t x = plus; x < plus + br_widget_text_width("+"); ++x) inside += hd_luma(fb[y * 240 + x]) >= 400u;
                for (int32_t x = button + 31; x < plus - 1; ++x) gaps += hd_luma(fb[y * 240 + x]) >= 200u;
                for (int32_t x = plus + br_widget_text_width("+") + 1; x < knob - 1; ++x) gaps += hd_luma(fb[y * 240 + x]) >= 200u;
            }
            HD_CHECK(inside >= 5u && !gaps, "OCT- + SNAP: the plus is drawn (%u px) and clear of button and knob (%u px in the gaps)", inside, gaps);
        }
    }
    /* Five separate white keys and raised black keys in the audition icon. */
    br_sel.loaded = 0; br_draw_cue_bar(7u, 1u);
    {
#if BR_PRODUCT_SMK37
        /* SMK-37: the browser button is labelled with its panel name (PATCH), so the cue widens. */
        int32_t button = br_widget_text_width(BR_PANEL_BROWSER) + 8;
        HD_CHECK(!strcmp(BR_PANEL_BROWSER, "PATCH") && button > 22, "!strcmp(BR_PANEL_BROWSER, 'PATCH') && button > 22");
#else
        int32_t button = 22;
#endif
        int32_t w1 = button + 6 + br_widget_text_width("LOAD PRESET"), w2 = 24 + 6 + br_widget_text_width("AUDITION");
        int32_t x = ((240 - (w1 + 16 + w2)) / 2) & ~1, k = (x + w1 + 16) & ~1;
        /* HD: the button is a shaded key: lit across its width on its top rows, dark just past it. */
        {
            uint32_t lit = 0;
            for (int32_t y = 217; y <= 219; ++y) lit |= hd_luma(fb[y * 240 + x + button / 2]) >= 150u;
            HD_CHECK(lit, "the browser button's top edge is lit at x %d", x + button / 2);
            HD_CHECK(hd_luma(fb[218 * 240 + x + button + 3]) < 150u, "nothing lit just past the button (x %d)", x + button + 3);
        }
        /* HD audition icon: five white keys (3 px, white to light grey) with dark gaps between them
         * below the black keys, and three raised black keys (2 px) at k+4, k+8 and k+16 over the
         * white keys' upper half, none at k+12: a group of two plus one. */
        for (int32_t i = 0; i < 5; ++i) {
            HD_CHECK(hd_luma(fb[231 * 240 + k + 3 + i * 4]) >= 700u, "white key %d bright (x %d)", i + 1, k + 3 + i * 4);
            if (i < 4) HD_CHECK(hd_luma(fb[231 * 240 + k + 5 + i * 4]) < 500u, "a gap after white key %d (x %d)", i + 1, k + 5 + i * 4);
        }
        for (int32_t b = 4; b <= 16; b += 4) {
            uint32_t l = hd_luma(fb[223 * 240 + k + b]);
            if (b == 12) HD_CHECK(l >= 700u, "no black key at k+12 (E-F): white there (%u)", l);
            else HD_CHECK(l < 400u, "black key at k+%d dark (%u)", b, l);
        }
    }
    /* HD amount vessel: a lit wall, then glass, then the fill. Every level, with and without the
     * rising glint, leaves the glass column beside both walls untouched (a one-pixel gutter), the
     * walls lit, and the centre column filled exactly up to the level. */
    for (uint32_t size_index=0u;size_index<2u;++size_index) {
        uint32_t size=size_index ? 60u : 32u;
        int32_t cx=(int32_t)size/2, hw=((int32_t)size/6+2)&~1;
        int32_t top=((int32_t)size/6)&~1, bottom=((int32_t)size*5/6)&~1;
        int32_t inner_top=top+2, inner_bottom=bottom-2;
        static uint16_t empty[64u*64u];
        cv_begin(64u,64u,BR_BACKGROUND); cv_oy=0; bw_amount(0,0,size,0u,0u); memcpy(empty,cv_px,sizeof empty);
        for (uint32_t q=0u;q<=127u;++q) for (uint32_t phase=0u;phase<32u;++phase) {
            uint32_t bad=0u;
            cv_begin(64u,64u,BR_BACKGROUND); cv_oy=0;
            bw_amount(0,0,size,q,phase);
            int32_t fill=(int32_t)q*(inner_bottom-inner_top)/127, fill_top=inner_bottom-fill;
            for (int32_t y=inner_top+1;y<inner_bottom-1;++y) {
                bad += cv_px[(uint32_t)y*cv_w+cx-hw+2] != empty[(uint32_t)y*cv_w+cx-hw+2];
                bad += cv_px[(uint32_t)y*cv_w+cx+hw-3] != empty[(uint32_t)y*cv_w+cx+hw-3];
                bad += hd_luma(swap16(cv_px[(uint32_t)y*cv_w+cx-hw])) < hd_luma(BR_LINE)/2u;
                bad += hd_luma(swap16(cv_px[(uint32_t)y*cv_w+cx+hw-1])) < hd_luma(BR_LINE)/2u;
                if (fill > 1 && y >= fill_top && y < inner_bottom)
                    bad += cv_px[(uint32_t)y*cv_w+cx] == empty[(uint32_t)y*cv_w+cx];
                else if (y < fill_top - 1 && !(phase && q))
                    bad += cv_px[(uint32_t)y*cv_w+cx] != empty[(uint32_t)y*cv_w+cx];
            }
            HD_CHECK(!bad, "amount size %u q %u phase %u: %u gutter, wall or fill pixels wrong", size, q, phase, bad);
        }
    }
    /* No blit for stable dots, including after an unrelated forced body redraw. */
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0; br_ui.screen[BR_PAGE_HOME] = 0;
    br_ui.redraw = 1u; br_draw();
    ui_test_lcd_blits = 0; br_draw_page_dots(0); HD_CHECK(ui_test_lcd_blits == 0, "ui_test_lcd_blits == 0");
    { uint16_t dots_before[8u * 240u]; uint32_t old = br_dots_old;
      for (uint32_t y = 0; y < 8u; ++y) memcpy(dots_before + y * 240u, fb + (200u + y) * 240u, 240u * sizeof(uint16_t));
      for (uint32_t n = 0; n < 40u; ++n) {
          cv_begin(60u, 31u, BR_BACKGROUND);
          cv_blit((n & 1u) ? 60u : 120u, BR_PARAM_STANDARD_BODY_Y + br_card_band[9]);
          br_draw_page_dots(0u);
          for (uint32_t y = 0; y < 8u; ++y)
              HD_CHECK(!memcmp(dots_before + y * 240u, fb + (200u + y) * 240u, 240u * sizeof(uint16_t)), "HOME dots remain visible during card repaint");
          HD_CHECK(br_dots_old == old, "HOME card repaint restores dots");
      }
    }
    ui_test_lcd_blits = 0; br_ui.screen[BR_PAGE_HOME] = 1; br_draw_page_dots(0); HD_CHECK(ui_test_lcd_blits == 1, "ui_test_lcd_blits == 1");
    {   /* HOME has two dots; the second is centred near (124, 203). */
        int32_t n = 0, sx = 0, sy = 0;
        for (int32_t y = 196; y < 212; ++y) for (int32_t x = 100; x < 140; ++x)
            if (fb[y * 240 + x] == br_ui_accent) { ++n; sx += x; sy += y; }
        HD_CHECK(n > 0 && abs(sx / n - 124) <= 1 && abs(sy / n - 203) <= 1, "lit page dot at (%d, %d), %d px",
                 n ? sx / n : -1, n ? sy / n : -1, n);
        for (int32_t x = 100; x < 140; ++x) HD_CHECK(fb[229 * 240 + x] != br_ui_accent, "no dot on row 229 (x %d)", x);
    }
    /* The LFO page uses the current scaled named badge at the header position. */
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 0u;
    br_badge_lfo_phase[0][0] = 32u; br_badge_lfo_depth[0][0] = 127u;
    br_draw_page_route_badge(1u, 0u);
    {
        uint16_t line[240u * 20u];
        memcpy(line, fb + 46u * 240u, sizeof line);
        cv_begin(240u, 20u, BR_BACKGROUND);
        br_draw_named_badge_scaled(120, 1, "LFO1", br_route_badge_tint(0u, 0u, BR_BACKGROUND), 2);
        for (uint32_t y = 0; y < 20; ++y)
            for (uint32_t x = 0; x < 240; ++x)
                if (cv_px[y * 240u + x] != swap16(BR_BACKGROUND))
                    HD_CHECK(line[y * 240u + x] == swap16(cv_px[y * 240u + x]), "line[y * 240u + x] == swap16(cv_px[y * 240u + x])");
    }
    /* The compressor meter is centered at 120 and does not draw the old left label. */
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 4u;
    br_ui.redraw = 1u; br_draw();
    {   /* HD: the meter keeps its box (x 80..159); its rounded vessel is lit inside it, centred at 120 */
        int32_t left = -1, right = -1;
        for (int32_t x = 60; x < 180; ++x) if (hd_luma(fb[48 * 240 + x]) >= 100u) { if (left < 0) left = x; right = x; }
        HD_CHECK(left >= 80 && right <= 159 && right - left >= 74 && abs(left + right - 239) <= 1,
                 "compressor meter spans x %d..%d on row 48 (inside its 80..159 box, rounded, centred)", left, right);
    }
    for (uint32_t x = 6u; x < 70u; ++x)
        for (uint32_t y = 48u; y < 60u; ++y)
            HD_CHECK(fb[y * 240u + x] != BR_DIM, "fb[y * 240u + x] != BR_DIM");
    /* Settings page disables both OCT buttons and their LEDs. */
    br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 1u; br_ui.step_bank = 1u; br_ui.octave = 0;
    br_buttons((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP)); br_leds();
    HD_CHECK(br_ui.screen[BR_PAGE_SEQ] == 1u && br_ui.step_bank == 1u && br_ui.octave == 0, "br_ui.screen[BR_PAGE_SEQ] == 1u && br_ui.step_bank == 1u && br_ui.octave == 0");
    HD_CHECK(host_led_level_state[BTN_OCT_DOWN] == FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] == FM1_LED_OFF, "host_led_level_state[BTN_OCT_DOWN] == FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] == F");
    HD_CHECK(!*br_oct_hint(), "!*br_oct_hint()");
    return hd_finish("HD cue corners and keys, amount gutters, stable dots, LFO badge, compressor meter, disabled OCT");
}
