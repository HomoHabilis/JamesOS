/* Exercise the SMK arrow track transitions against the real UI state. */
#define DINGHY_HOST 1
#define DINGHY_SMK_PREVIEW 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

int main(void)
{
    br_app_init();
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u;
    br_smk_track_turn(-1); assert(br_ui.part == 0u);
    for (uint32_t i = 1u; i < BR_PARTS; ++i) {
        br_smk_track_turn(1); assert(br_ui.part == i);
    }
    br_smk_track_turn(1); assert(br_ui.part == BR_DRUM_PART);
    br_smk_track_turn(-1); assert(br_ui.part == BR_DRUM_PART - 1u);
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    br_smk_track_turn(1);
    assert(br_ui.part == BR_DRUM_PART && br_ui.page == BR_PAGE_ROUTE && br_ui.screen[BR_PAGE_ROUTE] == 0u);
    br_smk_track_turn(-1);
    assert(br_ui.part == BR_DRUM_PART - 1u && !br_gate_page());
    br_ui.page = BR_PAGE_LFO; br_ui.screen[BR_PAGE_LFO] = 1u;
    br_smk_track_turn(1);
    assert(br_ui.part == BR_DRUM_PART && br_ui.page == BR_PAGE_LFO);
    br_ui.part = 0u; br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    /* The patch pages are the EDIT group; the kit mixer (KIT LEVEL) is opened by FADER BANK. */
    br_buttons(1u << BTN_EDIT);
    assert(br_ui.page == BR_PAGE_ROUTE && br_ui.screen[BR_PAGE_ROUTE] == 1u);
    br_smk_fader_bank_select(1u);
    assert(br_gate_page());
    br_buttons(1u << BTN_EDIT);
    assert(br_ui.page == BR_PAGE_ROUTE && br_ui.screen[BR_PAGE_ROUTE] == 0u);
    /* Abandon an audition before changing tracks. */
    br_ui.page = BR_PAGE_SCALE; br_ui.part = 0u;
    br_part[0].sound.timbre = 17u;
    br_sel_move(1);
    assert(br_sel.active && br_sel.part == 0u);
    br_smk_track_turn(1);
    assert(br_ui.part == 1u && !br_sel.active && br_part[0].sound.timbre == 17u);
    return 0;
}
