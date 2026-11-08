/* Multi-channel kit releases through the app and real Plaits kit state. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static void render(void)
{
    int32_t audio[BR_BLOCK * 2u] = {0};
    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
}
static void setup(void)
{
    br_app_init();
    br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    for (uint32_t i = 0u; i < 7u; ++i) br_midi_channels[i] = (uint8_t)(i + 1u);
    render();
}
int main(void)
{
    setup();
    br_midi(0x93u, 36u, 100u); /* D1 */
    br_midi(0x94u, 38u, 100u); /* D2: most recent global modulation note */
    br_midi(0x83u, 36u, 0u);   /* D1 off must reach its own Plaits lane */
    br_take_events();
    assert(plaits_kit_quiet_mask(br_plaits_kit_lane_state(BR_DRUM_PART, 0u), 1u));
    assert(!plaits_kit_quiet_mask(br_plaits_kit_lane_state(BR_DRUM_PART, 1u), 2u));
    assert(br_mod_state[BR_DRUM_PART].note == br_drum_lane_note(1u));
    assert(br_mod_state[BR_DRUM_PART].stage != BR_MOD_ENV_RELEASE);
    render();
    br_midi(0x84u, 38u, 0u); br_take_events();
    assert(br_mod_state[BR_DRUM_PART].stage == BR_MOD_ENV_RELEASE);
    render();
    puts("Independent MIDI kit lane release checks passed");
    return 0;
}
