/* SPDX-License-Identifier: GPL-3.0-only */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/plaits/upstream/plaits/wavetable_pack.h"
#include "instrument_poly_events.h"   /* src/ or src/poly/: -I in the runner */
#include <assert.h>

int main(void)
{
    static uint64_t primary[(4u * PLAITS_STATE_BYTES + 7u) / 8u];
    static uint64_t extra[(2u * PLAITS_STATE_BYTES + 7u) / 8u];
    br_poly_render_t synth;
    br_sound_t sound;
    uint8_t stolen;
    uint32_t generation[6];
#if FM1_PLAITS_PACK_WAVES
    /* The device decodes the packed Plaits resources (wavetables, sine table, DX7 banks, Braids
     * waves) at boot (platform.c), before any voice renders. */
    assert(fm1_plaits_waves_init(NULL));
#endif
    br_preset_sound(BR_FIRST_PLAITS + 8u, &sound);
    sound.attack = 0; sound.decay = 0; sound.sustain = 127; sound.release = 0; sound.level = 100;
    assert(br_poly_render_init(&synth, 8u, primary, sizeof primary, extra, sizeof extra));
    for (uint8_t i = 0; i < 6; ++i)
        assert(br_poly_render_on(&synth, &sound, 60u + i, (60 + i) * 128, 100u,
                                 &stolen, &generation[i]) == i);
    for (unsigned block = 0; block < 20; ++block)
        for (unsigned i = 0; i < 6; ++i) {
            int32_t output[64];
            br_poly_render_voice(&synth, i, &sound, (60 + i) * 128, output, 64u);
        }
    assert(br_poly_render_off(&synth, 63u, generation[3]) == 3u);
    for (unsigned block = 0; block < 200; ++block) {
        int32_t output[64];
        br_poly_render_voice(&synth, 3u, &sound, 63 * 128, output, 64u);
    }
    assert(synth.envelope[3].stage == BR_ENV_OFF);
    for (unsigned i = 0; i < 6; ++i)
        if (i != 3u) assert(synth.envelope[i].stage != BR_ENV_OFF);
    assert(br_poly_render_off(&synth, 63u, generation[3]) == BR_POLY_NO_VOICE);
    br_poly_events_t events = {0};
    assert(br_poly_render_init(&synth, 8u, primary, sizeof primary, extra, sizeof extra));
    for (unsigned i = 0; i < 7; ++i)
        assert(br_poly_events_on(&events, &synth, &sound, i + 1u, 60u + i, (60 + i) * 128, 100u));
    /* Pitch 60 was stolen. Re-strike it before the old input releases. */
    assert(br_poly_events_on(&events, &synth, &sound, 1u, 60u, 60 * 128, 100u));
    uint32_t new_generation = events.held[events.count - 1u].generation;
    assert(br_poly_events_off(&events, &synth, 1u, 60u));
    uint32_t active = 0;
    for (unsigned i = 0; i < BR_POLY_VOICES; ++i)
        if (synth.source.allocator.voice[i].count && synth.source.allocator.voice[i].note == 60u &&
            synth.source.allocator.voice[i].age == new_generation) active = 1;
    assert(active);
    assert(br_poly_events_off(&events, &synth, 1u, 60u));
    for (unsigned i = 0; i < BR_POLY_VOICES; ++i)
        assert(!synth.source.allocator.voice[i].count || synth.source.allocator.voice[i].note != 60u);
}
