/* Cold starts (synth voices and the kit) are admitted one per half; warm voices are not delayed. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void setup(void)
{
    br_app_init(); fm1_ms = 2000u;
    for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) br_load_preset(p, BR_FIRST_PLAITS + p);
    br_load_preset(BR_DRUM_PART, BR_KIT_BRAIDS);
    for (uint32_t p = 0u; p < BR_PARTS; ++p) {
        br_snapshot(p); br_select_mode(p); br_build_effective(p);
        br_quiet_part[p] = 1u;                       /* as after a few seconds of silence */
    }
}
static int started(uint32_t p) { return br_env[p].stage != BR_ENV_OFF; }

int main(void)
{
    /* Three cold synth voices start one per half; the first is immediate. */
    setup();
    br_start_note(0, 60, 100, 0); br_start_note(1, 60, 100, 0); br_start_note(2, 60, 100, 0);
    assert(started(0) && !started(1) && !started(2));
    br_take_events(); assert(started(1) && !started(2));
    br_take_events(); assert(started(2));

    /* A warm voice retriggers immediately and takes no stagger slot. */
    br_start_note(0, 64, 100, 0);
    assert(!br_deferred_start[0].valid && started(0));

    /* A cold kit start takes a slot like a cold synth voice, and lanes struck together join it. */
    setup();
    br_start_note(0, 60, 100, 0); br_start_note(1, 60, 100, 0);
    br_start_note(BR_DRUM_PART, 36, 100, 0); br_start_note(BR_DRUM_PART, 38, 100, 0);
    assert(started(0) && !started(1) && !started(BR_DRUM_PART));
    br_take_events(); assert(started(1) && !started(BR_DRUM_PART));
    br_take_events(); assert(started(BR_DRUM_PART));
    for (uint32_t i = 0u; i < BR_DEFERRED_KIT_NOTES; ++i) assert(!br_deferred_kit[i].valid);

    /* With no other cold start waiting, a cold kit start is immediate, and so are its other lanes. */
    setup();
    br_start_note(BR_DRUM_PART, 36, 100, 0); assert(started(BR_DRUM_PART));
    br_start_note(BR_DRUM_PART, 46, 100, 0);
    for (uint32_t i = 0u; i < BR_DEFERRED_KIT_NOTES; ++i) assert(!br_deferred_kit[i].valid);

    /* A warm kit (not quiet) starts at once even when synth starts are waiting. */
    setup(); br_quiet_part[BR_DRUM_PART] = 0u;
    br_start_note(0, 60, 100, 0); br_start_note(1, 60, 100, 0);
    br_start_note(BR_DRUM_PART, 36, 100, 0);
    assert(started(BR_DRUM_PART) && !started(1));

    /* Stopping the kit drops its waiting notes. */
    setup();
    br_start_note(0, 60, 100, 0); br_start_note(BR_DRUM_PART, 36, 100, 0);
    br_stop_part(BR_DRUM_PART);
    br_take_events(); br_take_events();
    assert(!started(BR_DRUM_PART));

    /* A release flushes the deferred start first, so the note is not lost. */
    setup();
    br_start_note(0, 60, 100, 0); br_start_note(1, 60, 100, 0);
    br_release_note(1, 60);
    assert(!br_deferred_start[1].valid && started(1));

    /* Stopping a part drops its pending start. */
    setup();
    br_start_note(0, 60, 100, 0); br_start_note(1, 60, 100, 0);
    br_stop_part(1);
    br_take_events(); br_take_events();
    assert(!started(1));

    /* A cold start after the window has passed is admitted at once. */
    setup();
    br_start_note(0, 60, 100, 0);
    br_take_events(); br_take_events(); br_take_events();
    br_stop_part(0);
    br_load_preset(1, BR_FIRST_PLAITS + 1); br_snapshot(1); br_select_mode(1); br_build_effective(1);
    br_start_note(1, 60, 100, 0);
    assert(started(1));
    printf("cold stagger: synth and kit cold starts one per half, lanes together\n");
    return 0;
}
