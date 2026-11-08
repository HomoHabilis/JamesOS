/* A normal note using the punch shift key must receive its release. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static void tick(void) { ++fm1_ms; br_app_tick(); }
static void audio(void) {
    int32_t out[BR_BLOCK * 2u] = {0};
    assert(app_audio(out, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
}
static void down(uint32_t key) {
    fm1_in.notes |= 1u << key; host_note_pressed |= 1u << key; tick(); audio();
}
static void up(uint32_t key) {
    fm1_in.notes &= ~(1u << key); tick(); audio();
}
int main(void) {
    static const uint32_t pages[] = {BR_PAGE_ENV, BR_PAGE_LFO, BR_PAGE_ROUTE, BR_PAGE_MIX};
    br_app_init(); fm1_ms = 2000u; br_ui.part = 0u; br_arp_on = br_hold_on = 0u;
    for (uint32_t i = 0; i < sizeof pages / sizeof pages[0]; ++i) {
        br_ui.page = pages[i]; br_ui.screen[pages[i]] = 0u;
        down(25u);
        assert(br_ui.page == BR_PAGE_PUNCH && br_punch_ks.shift && !br_ui.held_note[25u]);
        down(8u); assert(br_punch_effect == BR_PUNCH_ECHO);
        up(8u); assert(br_punch_effect == BR_PUNCH_NONE);
        up(25u); assert(br_ui.page == pages[i] && !br_punch_ks.shift && !br_live_count[0]);
        down(0u); assert(br_live_count[0] == 1u);
        up(0u); assert(!br_live_count[0]);
    }
    /* Return to the original subpage, including the global mixer. */
    { static const uint32_t screens[] = {2u, 1u, 2u, 6u};
      for (uint32_t i = 0; i < sizeof pages / sizeof pages[0]; ++i) {
        br_ui.page = pages[i]; br_ui.screen[pages[i]] = screens[i];
        down(25u); assert(br_ui.page == BR_PAGE_PUNCH);
        up(25u); assert(br_ui.page == pages[i] && br_ui.screen[pages[i]] == screens[i]);
      }
    }
    /* Browser owns an ordinary key press even if its release occurs on an eligible page. */
    br_ui.page = BR_PAGE_SCALE; br_ui.screen[BR_PAGE_SCALE] = 0u;
    down(25u); assert(br_ui.held_note[25u] && br_live_count[0] == 1u);
    br_ui.page = BR_PAGE_ENV; up(25u);
    assert(!br_ui.held_note[25u] && !br_live_count[0] && !br_punch_ks.shift);
    /* The drum mixer retains its inactive keyboard, including the Punch shift. */
    br_ui.part = BR_DRUM_PART; br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    down(25u); down(8u);
    assert(br_ui.page == BR_PAGE_ROUTE && !br_punch_ks.shift && br_punch_effect == BR_PUNCH_NONE);
    up(8u); up(25u); br_ui.part = 0u;
    /* A genuine punch shift release remains consumed after a page change. */
    br_ui.page = BR_PAGE_HOME; down(25u);
    assert(br_punch_ks.shift && (br_punch_ks.consumed & (1u << 25)));
    br_ui.page = BR_PAGE_ENV; up(25u);
    assert(!br_punch_ks.shift && !(br_punch_ks.consumed & (1u << 25)) && !br_live_count[0]);
    puts("PASS: punch shift and ordinary key releases");
    return 0;
}
