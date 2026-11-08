/* The master section skips settled silence: with the filter off centre and HEAT on, silence stays exactly
 * zero, a note still comes through, and after its release the state settles and the skip engages again. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static int32_t a[BR_BLOCK * 2u];
static int32_t block_peak(void)
{
    int32_t m = 0;
    memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    for (uint32_t i = 0; i < BR_BLOCK * 2u; ++i) if (abs(a[i]) > m) m = abs(a[i]);
    return m;
}
static int settled(void)
{
    return !br_master_state.filter_tail && !br_master_state.wow_tail && !br_master_state.low[0] &&
           !br_master_state.low[1] && br_master_state.svf_low[0] == 0.0f && br_master_state.band[0] == 0.0f;
}

int main(void)
{
    br_app_init(); fm1_ms = 1000u;
    br_master.revision++;
    br_master.fx.filter = 90u; br_master.fx.heat_type = BR_HEAT_SAT; br_master.fx.heat_amount = 87u;
    RING_PUBLISH(); br_master.revision++;
    for (int b = 0; b < 40; ++b) assert(block_peak() == 0);            /* silence in, silence out */
    br_engine_note_on(0u, 60u, 120u);
    int32_t loud = 0;
    for (int b = 0; b < 60; ++b) { int32_t m = block_peak(); if (m > loud) loud = m; }
    assert(loud > 50);                                                   /* the note reaches the output */
    assert(!settled());
    br_engine_note_off(0u, 60u);
    for (int b = 0; b < 172 * 6; ++b) (void)block_peak();               /* release, reverb and the 2 s filter tail */
    assert(settled());
    for (int b = 0; b < 40; ++b) assert(block_peak() == 0);
    printf("master silence: zero stays zero, the note passes (peak %d), state settles\n", (int)loud);
    return 0;
}
