/* SMK fader pickup, mixer bank navigation and temporary display. */
#define DINGHY_HOST 1
#define DINGHY_SMK_PREVIEW 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "smk37_fader_logic.h"   /* src/smk37/ (unified) or src/ (JamesOS-K): -I in the runner */
#include <assert.h>
#include <string.h>

int main(void)
{
    smk_fader_state_t state = {0};
    const uint8_t positions[4] = {20, 30, 40, 50};
    smk_fader_reset_pickup(&state, positions);
    assert(smk_fader_move(&state, 0, 21, 80) == 1u);
    assert(smk_fader_move(&state, 0, 81, 80) == 2u);
    assert(smk_fader_move(&state, 0, 82, 81) == 2u);
    assert(smk_fader_move(&state, 0, 83, 20) == 1u);
    smk_fader_reset_pickup(&state, positions);
    assert(!state.picked[0] && state.previous[0] == 20u);

    br_app_init(); fm1_ms = 1000u; br_intro_until = 0u;
    static uint16_t original[240 * 240], mixer[240 * 240];
    br_ui.part = 1u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u;
    br_ui.redraw = 1u; br_draw(); memcpy(mixer, fb, sizeof mixer);
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0u;
    br_ui.redraw = 1u; br_draw();
    memcpy(original, fb, sizeof original);
    br_smk_fader_moved(0u); assert(br_smk_fader_peek.active);
    assert(br_ui.page == BR_PAGE_HOME && br_ui.part == 1u);
    br_draw(); assert(memcmp(original, fb, sizeof original));
    assert(!memcmp(mixer, fb, sizeof mixer));
    fm1_ms += 1599u; br_smk_fader_peek_tick(); assert(br_smk_fader_peek.active);
    br_smk_fader_moved(0u); fm1_ms += 1600u; br_smk_fader_peek_tick();
    assert(!br_smk_fader_peek.active && br_ui.page == BR_PAGE_HOME && br_ui.part == 1u);
    br_draw(); assert(!memcmp(original, fb, sizeof original));

    /* The pure decisions. */
    assert(smk_bank_press_decide(0u) == SMK_BANK_PAGE && smk_bank_press_decide(1u) == SMK_BANK_MESSAGE);
    assert(smk_fader_peek_wanted(0u, 0u) && !smk_fader_peek_wanted(0u, 1u) &&
           !smk_fader_peek_wanted(1u, 0u) && !smk_fader_peek_wanted(1u, 1u));

    /* FADER BANK: a tap opens the real mixer page of the bank now selected, with no peek; it stays. */
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
    assert(br_smk_fader_bank_select(1u));
    assert(br_ui.page == BR_PAGE_ROUTE && br_gate_page() && !br_smk_fader_peek.active);
    assert(br_ui.part == 1u);
    br_draw(); assert(br_gate_page());
    fm1_ms += 5000u; br_smk_fader_peek_tick(); assert(br_gate_page());          /* no fade */
    assert(br_smk_fader_bank_select(0u));                                        /* a second tap: the other bank */
    assert(br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX] && !br_smk_fader_peek.active);
    assert(br_smk_fader_bank_select(1u) && br_gate_page());
    /* OCT-/OCT+ on KIT LEVEL are the lane mute and solo, as on that page before. */
    { uint32_t mute = br_kit_mute_mask, lane = br_ui.lane; br_buttons(1u << BTN_OCT_DOWN);
      assert(br_kit_mute_mask == (mute ^ (1u << lane))); br_buttons(1u << BTN_OCT_DOWN); }
    /* A held step: the bank switches elsewhere, only the message shows, the page stays. */
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS;
    br_ui.step_edit_active = 1u;
    assert(br_smk_fader_bank_select(0u) && br_ui.page == BR_PAGE_FX && !br_smk_fader_peek.active);
    br_smk_fader_moved(0u); assert(!br_smk_fader_peek.active);
    br_ui.step_edit_active = 0u;

    /* A fader moved off a mixer page: the brief peek, restarted while it keeps moving. */
    br_ui.page = BR_PAGE_HOME; br_smk_fader_moved(0u);
    assert(br_smk_fader_peek.active && br_smk_fader_peek.bank == 0u);
    fm1_ms += 1000u; br_smk_fader_moved(0u); fm1_ms += 1000u; br_smk_fader_peek_tick();
    assert(br_smk_fader_peek.active);
    br_ui.page = BR_PAGE_ENV; br_smk_fader_peek_tick();
    assert(!br_smk_fader_peek.active && br_ui.page == BR_PAGE_ENV);
    /* On a mixer page (MIXER or KIT LEVEL, whichever bank) a fader move only updates the page. */
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u;
    br_smk_fader_moved(0u); assert(!br_smk_fader_peek.active);
    br_smk_fader_moved(1u); assert(!br_smk_fader_peek.active);
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 3u;
    br_smk_fader_moved(0u); assert(!br_smk_fader_peek.active);
    br_smk_fader_moved(1u); assert(!br_smk_fader_peek.active);
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 1u;                    /* GLOBAL / FILTER is not a mixer */
    br_smk_fader_moved(1u); assert(br_smk_fader_peek.active);
    return 0;
}
