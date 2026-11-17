/* Check the real split-audio HOME cue renderer with polyphony enabled. */
#define POLY_UI_HOST_TEST 1
#include "poly_ui_host.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    static uint16_t actual[240u * 24u];
    const char *hint = "SCALES ALL MODULATION";
    br_app_init(); fm1_ms = 2001u;
    br_ui.part = 0u; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 2u;
    br_ui.message_until = 0u;
    br_draw_status(1u);
    memcpy(actual, fb + 216u * 240u, sizeof actual);
    cv_begin(240u, 24u, BR_BACKGROUND);
    br_widget_text((240 - br_widget_text_width(hint)) / 2, 8, hint, BR_DIM);
    cv_blit(0u, 216u);
    assert(!memcmp(actual, fb + 216u * 240u, sizeof actual));
    br_playing = 0u;
    br_part[0].sound.voice_mode = 1u;
    for (uint32_t page = 1u; page <= 2u; ++page) {
        br_ui.screen[BR_PAGE_HOME] = page;
        assert(strstr(br_oct_hint(), "OCT- + PLAY") == NULL);
    }
    br_ui.screen[BR_PAGE_HOME] = 0u;
    br_playing = 0u; br_part[0].sound.voice_mode = 2u;
    assert(!strcmp(br_oct_hint(), "OCT- + PLAY: POLYPHONY OFF"));
    br_part[0].sound.voice_mode = 1u;
    assert(!strcmp(br_oct_hint(), "OCT- + PLAY: POLYPHONY ON"));
    br_draw_status(1u);
    unsigned top_green=0, bottom_green=0, red=0, gray=0;
    for (uint32_t i=216u*240u; i<240u*240u; ++i) {
        if (fb[i] == RGB(93, 220, 139)) {
            if (i / 240u < 228u) ++top_green; else ++bottom_green;
        }
        red += fb[i] == RGB(234, 91, 86);
        gray += fb[i] == RGB(140, 145, 147);
    }
    assert(top_green && bottom_green && !red && gray);
    br_message("POLYPHONY ON"); br_draw_status(1u);
    memcpy(actual, fb + 216u * 240u, sizeof actual);
    cv_begin(240u, 24u, BR_BACKGROUND);
    br_widget_text((240 - br_widget_text_width("POLYPHONY ON")) / 2, 8, "POLYPHONY ON", BR_GLOBAL_ACC);
    cv_blit(0u, 216u);
    assert(!memcmp(actual, fb + 216u * 240u, sizeof actual));
#if defined(BR_ADAPTIVE_POLY) && BR_ADAPTIVE_POLY
    {   /* The real HOME badge reads the allocator's model-specific limit. */
        static uint16_t badge[240u * 28u];
        for (uint32_t voices = 3u; voices <= 8u; ++voices) {
            char label[24], n[8];
            br_poly_scheduler_enabled = 1u; br_poly_scheduler.source.allocator.limit = voices;
            br_ui_accent = br_track_color(0u); br_draw_page_route_badge(1u, 0u);
            memcpy(badge, fb + 38u * 240u, sizeof badge);
            cv_begin(240u, 28u, BR_BACKGROUND); br_text_copy(label, "POLY: ", sizeof label);
            br_number(n, (int32_t)voices); br_text_append(label, n, sizeof label);
            br_text_append(label, " VOICES", sizeof label); br_draw_page_badge(label, br_track_color(0u));
            cv_blit(0u, 38u); assert(!memcmp(badge, fb + 38u * 240u, sizeof badge));
        }
        br_poly_scheduler_enabled = 0u;
    }
#endif
    br_part[0].sound.voice_mode = 2u; br_playing = 1u;
    br_message("TEMPORARY STATUS"); br_draw_status(1u);
    red = 0u;
    for (uint32_t i = 216u * 240u; i < 240u * 240u; ++i)
        red += fb[i] == RGB(248, 208, 64);
    assert(red && !strcmp(br_oct_hint(), "POLYPHONY OFF DURING PLAYBACK"));
    br_ui.screen[BR_PAGE_HOME] = 2u; br_draw_status(1u); red = 0u;
    for (uint32_t i = 216u * 240u; i < 240u * 240u; ++i)
        red += fb[i] == RGB(248, 208, 64);
    assert(red && !strcmp(br_oct_hint(), "POLYPHONY OFF DURING PLAYBACK"));
    fm1_ms += 9000u; br_draw_status(1u); red = 0u;
    for (uint32_t i = 216u * 240u; i < 240u * 240u; ++i)
        red += fb[i] == RGB(248, 208, 64);
    assert(red);
    br_playing = 0u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 2u;
    assert(!strcmp(br_oct_hint(), br_build_text()));
    /* Exercise the real button dispatch: the chord toggles only on HOME 1.
     * On other pages PLAY keeps its transport behavior. */
    for (uint32_t page = 0u; page < 4u; ++page) {
        br_app_init(); fm1_ms = 2001u; br_ui.part = 0u;
        br_ui.page = page == 3u ? BR_PAGE_MIX : BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = page == 3u ? 0u : page;
        br_part[0].sound.voice_mode = 1u; br_playing = 0u;
        fm1_in.buttons = 1u << BTN_OCT_DOWN; host_pressed = 1u << BTN_OCT_DOWN; br_app_tick();
        fm1_in.buttons |= 1u << BTN_PLAY; host_pressed = 1u << BTN_PLAY; br_app_tick();
        assert(br_part[0].sound.voice_mode == (page == 0u ? 2u : 1u));
        assert(br_playing == (page == 0u ? 0u : 1u));
        fm1_in.buttons = 0u; host_released = (1u << BTN_OCT_DOWN) | (1u << BTN_PLAY); br_app_tick();
    }
    puts("HOME cues and page-restricted polyphony shortcut passed");
    return 0;
}
