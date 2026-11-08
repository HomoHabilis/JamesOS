/* Detect body redraws that erase the cue bar before repainting it. */
#define DINGHY_HOST 1
#define DINGHY_EMULATOR_SAVE 1
#define UI_CUE_FLICKER_TEST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
int main(void)
{
    br_app_init();fm1_ms=2000;es.current=-1;es.slot=0;
    for(int mode=1;mode<=6;mode++) {
        es.mode=mode;host_blits_cross_cue=host_cue_blits=0;
        es_draw();es_draw();
        if (mode == 1) {
            assert(fb[128u * 240u + 8u] == BR_GLOBAL_ACC);
            assert(fb[124u * 240u + 3u] == C_WHITE); /* left rule */
            assert(fb[124u * 240u + 4u] == BR_GLOBAL_ACC); /* cursor touches it */
            assert(fb[122u * 240u + 4u] == fb[122u * 240u + 70u]); /* label/list gap */
            assert(fb[201u * 240u + 3u] == C_WHITE); /* rule reaches last row */
        }
        assert(host_blits_cross_cue==0);
        assert(host_cue_blits==2);
    }
    puts("PASS: SAVE redraw never clears the cue bar");
}
