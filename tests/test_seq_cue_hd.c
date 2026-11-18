/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer's bottom cue (screen rows 216..239) alternates every 3 s between "OCT-/+ + REC SHIFT NOTES" and the
 * step-bank paging cue "OCT-/+: STEP BANK" when the track has more than one bank of 16 steps; a one-bank track,
 * the step-edit nudge cue and a timed message do not alternate. Writes the two cues as PPM crops when given a
 * directory (always, argv[1]). */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"

#define ROWS (24u * 240u)
static void cue_rows(uint32_t ms, uint16_t *out)
{
    fm1_ms = ms; br_ui.redraw = 1u; br_draw();
    memcpy(out, &fb[216u * 240u], ROWS * sizeof(uint16_t));
}

int main(int argc, char **argv)
{
    static uint16_t a[ROWS], b[ROWS], c[ROWS];
    char path[512];
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro();
    br_ui.part = 0u; br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0u; br_ui.message[0] = 0;
    br_ui.message_until = 0u; br_ui.step_edit_active = 0u;

    br_part[0].length = 32u;
    HD_CHECK(br_seq_pages(), "32 steps: two banks page");
    cue_rows(6000u, a);          /* phase 0: SHIFT NOTES */
    cue_rows(7000u, b);
    HD_CHECK(!br_seq_cue_phase() && !memcmp(a, b, sizeof a), "same phase: same rows");
    cue_rows(8999u, b);
    HD_CHECK(!br_seq_cue_phase() && !memcmp(a, b, sizeof a), "phase 0 holds to 8999 ms");
    cue_rows(9000u, b);          /* phase 1: STEP BANK */
    HD_CHECK(br_seq_cue_phase() == 1u && memcmp(a, b, sizeof a), "3 s later the rows differ");
    cue_rows(11999u, c);
    HD_CHECK(!memcmp(b, c, sizeof b), "phase 1 holds to 11999 ms");
    cue_rows(12000u, c);
    HD_CHECK(!memcmp(a, c, sizeof a), "and back to SHIFT NOTES");
    HD_CHECK(!strcmp(br_oct_hint(), "OCT-/+ + REC SHIFT NOTES"), "br_oct_hint stays the SHIFT NOTES text in both phases");
    snprintf(path, sizeof path, "%s/cue-shift-notes.ppm", hd_crop_dir); hd_write_crop(path, a, 240, 0, 0, 0, 240, 24, 3);
    snprintf(path, sizeof path, "%s/cue-step-bank.ppm", hd_crop_dir); hd_write_crop(path, b, 240, 0, 0, 0, 240, 24, 3);

    /* the 16-step track: one bank, no flip */
    br_part[0].length = 16u;
    HD_CHECK(!br_seq_pages(), "16 steps: one bank, nothing to page");
    cue_rows(6000u, a); cue_rows(9000u, b);
    HD_CHECK(!br_seq_cue_phase() && !memcmp(a, b, sizeof a), "one bank: no flip");

    /* step edit: the nudge cue stays */
    br_part[0].length = 32u;
    br_ui.step_edit_active = 1u;
    HD_CHECK(br_seq_grid_page() && br_seq_step_edit(), "step edit is on");
    cue_rows(6000u, a); cue_rows(9000u, b);
    HD_CHECK(!br_seq_cue_phase() && !memcmp(a, b, sizeof a), "step edit: no flip");
    br_ui.step_edit_active = 0u;

    /* a timed message: it stays whatever the phase */
    fm1_ms = 8800u; br_message("TEST MESSAGE");   /* on for about 1.1 s: across the flip at 9000 */
    cue_rows(8950u, a); cue_rows(9050u, b);
    HD_CHECK(!strcmp(br_ui.message, "TEST MESSAGE") && (int32_t)(br_ui.message_until - 9050u) > 0, "the message is still on");
    HD_CHECK(!memcmp(a, b, sizeof a), "message: no flip");
    br_ui.message[0] = 0; br_ui.message_until = 0u;

    /* the settings screen has no cue; other pages are untouched */
    br_ui.screen[BR_PAGE_SEQ] = 1u;
    HD_CHECK(!br_seq_pages() && !br_seq_cue_phase(), "settings screen: no paging cue");
    return hd_finish("Sequencer cue alternation");
}
