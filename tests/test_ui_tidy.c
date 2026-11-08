#define DINGHY_HOST 1
#define UI_REFRESH_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

int main(void)
{
    br_app_init(); fm1_ms = 5000u;
    /* Button cues have rounded corners; SAV retains a square footprint. */
    cv_begin(32, 20, C_BLACK); br_cue_round_button(1, 2, 30, 16);
    assert(cv_px[2u * cv_w + 1u] == swap16(C_BLACK));
    assert(cv_px[2u * cv_w + 3u] == swap16(BR_LINE));
    br_ui.message[0] = 0; br_ui.message_until = 0;
    br_draw_cue_bar(2u, 1u);
    {
        uint32_t b1 = 22u;
        uint32_t x1 = (240u - (b1 + 5u + br_widget_text_width("NEXT"))) / 2u;
        assert(fb[217u * 240u + x1] != BR_LINE);
        assert(fb[217u * 240u + x1 + 2u] == BR_LINE);
        assert(fb[238u * 240u + x1 + b1 - 1u] != BR_LINE);
        assert(fb[238u * 240u + x1 + b1 - 3u] == BR_LINE);
    }
    /* The wider plus in OCT- + SNAP stays clear of both button and knob. */
    br_draw_cue_bar(8u, 1u);
    {
        int32_t width=30+6+br_widget_text_width("+")+6+12+6+br_widget_text_width("SNAP");
        int32_t button=(240-width)/2, plus=button+36, knob=plus+br_widget_text_width("+")+6;
        assert(button>=0 && knob+18+br_widget_text_width("SNAP")<=240);
        assert(plus-(button+30)>=6 && knob-(plus+br_widget_text_width("+"))>=6);
        for (uint32_t y=0;y<8u;++y) for (uint32_t x=0;x<5u;++x) {
            uint32_t expected = (james_font_row('+',y) & (1u << (4u-x))) != 0u;
            assert((fb[(224u+y)*240u+plus+x] == C_WHITE) == expected);
        }
    }
    /* Five separate white keys and raised black keys in the audition icon. */
    br_sel.loaded = 0; br_draw_cue_bar(7u, 1u);
    {
        int32_t w1 = 22 + 6 + br_widget_text_width("LOAD PRESET"), w2 = 24 + 6 + br_widget_text_width("AUDITION");
        int32_t x = ((240 - (w1 + 16 + w2)) / 2) & ~1, k = (x + w1 + 16) & ~1;
        for (int32_t i = 0; i < 5; ++i) assert(fb[234u * 240u + k + 2 + i * 4] == C_WHITE);
        assert(fb[222u * 240u + k + 4] == C_BLACK);
        assert(fb[222u * 240u + k + 8] == C_BLACK);
        assert(fb[222u * 240u + k + 16] == C_BLACK);
    }
    /* Every amount level leaves a one-pixel gutter beside both walls,
     * including while its animated highlight is on. */
    for (uint32_t size_index=0u;size_index<2u;++size_index) {
        uint32_t size=size_index ? 60u : 32u;
        int32_t cx=(int32_t)size/2, hw=((int32_t)size/6+2)&~1;
        int32_t top=((int32_t)size/6)&~1, bottom=((int32_t)size*5/6)&~1;
        int32_t inner_top=top+2, inner_bottom=bottom-2;
        for (uint32_t q=0u;q<=127u;++q) for (uint32_t phase=0u;phase<32u;++phase) {
            cv_begin(64u,64u,BR_BACKGROUND); cv_oy=0;
            bw_amount(0,0,size,q,phase);
            int32_t fill=((int32_t)q*(inner_bottom-inner_top)/127)&~1;
            for (int32_t y=inner_top;y<inner_bottom;++y) {
                assert(cv_px[(uint32_t)y*cv_w+cx-hw+2] == swap16(BR_BACKGROUND));
                assert(cv_px[(uint32_t)y*cv_w+cx+hw-3] == swap16(BR_BACKGROUND));
                assert(cv_px[(uint32_t)y*cv_w+cx-hw] == swap16(BR_LINE));
                assert(cv_px[(uint32_t)y*cv_w+cx+hw-1] == swap16(BR_LINE));
                if (y < inner_bottom-fill)
                    assert(cv_px[(uint32_t)y*cv_w+cx] == swap16(BR_BACKGROUND));
                else assert(cv_px[(uint32_t)y*cv_w+cx] == swap16(BR_ACC) ||
                            cv_px[(uint32_t)y*cv_w+cx] == swap16(C_WHITE));
            }
        }
    }
    /* No blit for stable dots, including after an unrelated forced body redraw. */
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0; br_ui.screen[BR_PAGE_HOME] = 0;
    br_ui.redraw = 1u; br_draw();
    ui_test_lcd_blits = 0; br_draw_page_dots(0); assert(ui_test_lcd_blits == 0);
    br_ui.screen[BR_PAGE_HOME] = 1; br_draw_page_dots(0); assert(ui_test_lcd_blits == 1);
    assert(fb[210u * 240u + 122u] == br_ui_accent);
    assert(fb[229u * 240u + 122u] != br_ui_accent);
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
                    assert(line[y * 240u + x] == swap16(cv_px[y * 240u + x]));
    }
    /* The compressor meter is centered at 120 and does not draw the old left label. */
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 4u;
    br_ui.redraw = 1u; br_draw();
    assert(fb[48u * 240u + 80u] == BR_LINE);
    assert(fb[48u * 240u + 159u] == BR_LINE);
    for (uint32_t x = 6u; x < 70u; ++x)
        for (uint32_t y = 48u; y < 60u; ++y)
            assert(fb[y * 240u + x] != BR_DIM);
    /* Settings page disables both OCT buttons and their LEDs. */
    br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 1u; br_ui.step_bank = 1u; br_ui.octave = 0;
    br_buttons((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP)); br_leds();
    assert(br_ui.screen[BR_PAGE_SEQ] == 1u && br_ui.step_bank == 1u && br_ui.octave == 0);
    assert(host_led_level_state[BTN_OCT_DOWN] == FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] == FM1_LED_OFF);
    assert(!*br_oct_hint());
    puts("UI tidy: cue corners and keys, stable dots, disabled sequencer OCT passed");
    return 0;
}
