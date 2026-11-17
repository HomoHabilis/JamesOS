/* Detect body redraws that erase the cue bar before repainting it. */
#define DINGHY_HOST 1
#define DINGHY_EMULATOR_SAVE 1
#define UI_CUE_FLICKER_TEST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include "hd_look.h"
int main(void)
{
    br_app_init();fm1_ms=2000;es.current=-1;es.slot=0;
    for(int mode=1;mode<=6;mode++) {
        uint32_t first;
        es.mode=mode;host_blits_cross_cue=host_cue_blits=0;
        es_draw();first=host_cue_blits;es_draw();
        if (mode == 1) {
            /* HD layout of the same places (es_draw: hds_chip, hds_rule, hds_bar): the KNOB1 chip ends
             * at row 121; the selected QUICKSAVE bar is a rounded (radius 6) gradient of the global
             * accent over rows 125..144 from x 4; the left rule is a dim grey hd line at x 3 that
             * starts at the list (row 125, below the chip) and ends at row 201. The standard rule
             * also ran up beside the chip (row 124 and above); that segment does not exist in HD. */
            const uint16_t rule = RGB(96, 104, 108);
            const uint16_t bg = fb[100u * 240u + 120u]; /* empty body between the strip and the chip */
            /* The selected row is drawn in the global accent: its vertical middle (the top rows are
             * the lighter highlight of the gradient) is within dither/shading steps of the accent. */
            HD_CHECK(hd_near(fb[134u * 240u + 8u], BR_GLOBAL_ACC, 4u, 0),
                     "selected row bar at (8,134) %04x not near the global accent %04x",
                     fb[134u * 240u + 8u], BR_GLOBAL_ACC);
            /* Left rule beside the selected row, and reaching the last list row (201) but no further. */
            HD_CHECK(hd_near(fb[134u * 240u + 3u], rule, 1u, 0), "left rule at (3,134): %04x", fb[134u * 240u + 3u]);
            HD_CHECK(hd_near(fb[201u * 240u + 3u], rule, 1u, 0), "left rule at (3,201): %04x", fb[201u * 240u + 3u]);
            HD_CHECK(fb[202u * 240u + 3u] == bg, "left rule ends at row 201: (3,202) %04x", fb[202u * 240u + 3u]);
            /* The cursor bar touches the rule (no background column between them) where its rounded
             * corners do not cut x 4, i.e. its middle rows. */
            HD_CHECK(hd_near(fb[134u * 240u + 4u], BR_GLOBAL_ACC, 4u, 0),
                     "cursor bar touches the rule at (4,134): %04x", fb[134u * 240u + 4u]);
            /* Label/list gap: rows 122..124 are empty across the whole width (chip above, bar below). */
            {
                hd_box_t gap = hd_box(fb + 122u * 240u, 240, 3, bg, 0);
                HD_CHECK(gap.right < 0, "label/list gap rows 122..124 not empty: x %d..%d rows %d..%d",
                         gap.left, gap.right, 122 + gap.top, 122 + gap.bottom);
            }
            HD_CHECK(fb[121u * 240u + 8u] != bg && fb[125u * 240u + 10u] != bg,
                     "chip bottom edge (row 121) and bar top (row 125) bound the gap");
        }
        assert(host_blits_cross_cue==0);
        /* drawn once for the mode (in up to five 48-px chunks), not again when nothing changed */
        assert(first>=1u && first<=5u && host_cue_blits==first);
    }
    return hd_finish("SAVE redraw never clears the cue bar");
}
