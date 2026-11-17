/* SPDX-License-Identifier: GPL-3.0-only */
/* Render a real Plaits voice through the polyphonic LPG and amp envelope.
 * Compare actual output before and after note-off with the same release knob. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/plaits/upstream/plaits/wavetable_pack.h"
#include "instrument_poly_events.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t primary[(4u * PLAITS_STATE_BYTES + 7u) / 8u];
static uint64_t extra[(2u * PLAITS_STATE_BYTES + 7u) / 8u];
static uint64_t energy(const int32_t *samples, uint32_t n)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < n; ++i) total += (uint64_t)llabs((long long)samples[i]);
    return total;
}
static void run(uint32_t lpg, uint64_t *held, uint64_t *early, uint64_t *late, uint32_t *silent_at)
{
    br_poly_render_t synth;
    br_sound_t sound;
    uint8_t stolen, voice;
    uint32_t generation;
    int32_t output[64];
    br_preset_sound(BR_FIRST_PLAITS + 8u, &sound);
    sound.attack = 0; sound.decay = 0; sound.sustain = 127; sound.release = 75;
    sound.level = 127; sound.amp_mode = lpg;
    assert(br_poly_render_init(&synth, 8u, primary, sizeof primary, extra, sizeof extra));
    voice = br_poly_render_on(&synth, &sound, 60u, 60 * 128, 127u, &stolen, &generation);
    assert(voice != BR_POLY_NO_VOICE);
    *held = 0;
    for (uint32_t n = 0; n < 40u; ++n) {
        br_poly_render_voice(&synth, voice, &sound, 60 * 128, output, 64u);
        if (n >= 30u) *held += energy(output, 64u);
    }
    assert(br_poly_render_off(&synth, 60u, generation) == voice);
    *early = *late = 0; *silent_at = 0;
    for (uint32_t n = 0; n < FS / 64u; ++n) {
        br_poly_render_voice(&synth, voice, &sound, 60 * 128, output, 64u);
        uint32_t start = n * 64u;
        if (start < FS / 50u) *early += energy(output, 64u); /* first 20 ms */
        if (start >= FS / 10u && start < FS * 12u / 100u) *late += energy(output, 64u); /* 100–120 ms */
        if (!*silent_at && synth.envelope[voice].stage == BR_ENV_OFF) *silent_at = (n + 1u) * 64u;
    }
}
static void gate_test(uint32_t gate, uint32_t early_release)
{
    br_poly_render_t synth;
    br_sound_t sound;
    uint8_t stolen, voice;
    uint32_t generation, off_at = 0u, released = 0u;
    uint32_t gate_samples = br_lpg_gate_samples(gate), total = 0u;
    int32_t output[64];
    uint64_t heard = 0u;
    br_preset_sound(BR_FIRST_PLAITS + 8u, &sound);
    sound.attack = 0; sound.decay = 0; sound.sustain = 127; sound.release = 127;
    sound.level = 127; sound.amp_mode = 1; sound.lpg_gate = gate;
    assert(br_poly_render_init(&synth, 8u, primary, sizeof primary, extra, sizeof extra));
    voice = br_poly_render_on(&synth, &sound, 60u, 60 * 128, 127u, &stolen, &generation);
    assert(voice != BR_POLY_NO_VOICE);
    while (total < gate_samples + FS / 5u) {
        if (early_release && !released && total >= gate_samples / 2u) {
            assert(br_poly_render_off(&synth, 60u, generation) == voice); released = 1u;
        }
        br_poly_render_voice(&synth, voice, &sound, 60 * 128, output, 64u);
        total += 64u; heard += energy(output, 64u);
        if (synth.envelope[voice].stage == BR_ENV_OFF) { off_at = total; break; }
    }
    assert(heard && off_at);
    if (early_release) assert(off_at < gate_samples);
    else assert(off_at >= gate_samples && off_at <= gate_samples + br_lpg_release_samples() + 64u);
    if (!released) {
        assert(br_poly_render_off(&synth, 60u, generation) == voice);
        assert(synth.envelope[voice].stage == BR_ENV_OFF); /* late key-up does not retrigger */
    }
}
/* Exercise the mono post-source path with a steady oscillator waveform. */
static void mono_gate_test(uint32_t gate, uint32_t lpg, uint32_t early)
{
    br_sound_t sound;
    uint32_t limit = br_lpg_gate_samples(15u), total = 0u, off_at = 0u, released = 0u;
    uint64_t heard = 0u;
    int32_t row[64], key[64] = {0};
    br_app_init();
    br_preset_sound(0u, &sound);
    sound.attack = 0u; sound.decay = 0u; sound.sustain = 127u; sound.release = 75u;
    sound.level = 127u; sound.amp_mode = lpg; sound.lpg_gate = gate;
    br_cached_sound[0] = br_effective_sound[0] = sound; br_mode[0] = 0u;
    br_env_start(0u, 60u, 127u, &sound, 0u);
    while (total < limit + FS / 20u) {
        if (early && !released && total >= limit / 2u) { br_release_note(0u, 60u); released = 1u; }
        for (uint32_t i = 0; i < 64u; ++i) row[i] = ((total + i) / 16u & 1u) ? 12000 : -12000;
        br_engine_post_part_row(0u, row, key, 64u);
        total += 64u; heard += energy(row, 64u);
        if (br_env[0].stage == BR_ENV_OFF) { off_at = total; break; }
    }
    assert(heard);
    if (!gate || !lpg) assert(!off_at); /* held gate OFF, or envelope mode */
    else if (early) assert(off_at && off_at < limit);
    else {
        assert(off_at >= limit && off_at <= limit + br_lpg_release_samples() + 64u);
        br_release_note(0u, 60u); assert(br_env[0].stage == BR_ENV_OFF);
    }
}
int main(void)
{
    uint64_t lheld, learly, llate, eheld, eearly, elate;
    uint32_t loff, eoff;
#if FM1_PLAITS_PACK_WAVES
    assert(fm1_plaits_waves_init(NULL));
#endif
    run(1u, &lheld, &learly, &llate, &loff);
    run(0u, &eheld, &eearly, &elate, &eoff);
    printf("LPG: held=%llu early=%llu late=%llu off=%.2f ms; envelope: held=%llu early=%llu late=%llu off=%.2f ms\n",
           (unsigned long long)lheld, (unsigned long long)learly, (unsigned long long)llate,
           loff * 1000.0 / FS, (unsigned long long)eheld, (unsigned long long)eearly,
           (unsigned long long)elate, eoff * 1000.0 / FS);
    assert(lheld && learly && eheld && eearly && elate);
    assert(llate == 0u && loff && eoff && loff * 3u < eoff);
    assert(loff >= br_lpg_release_samples() && loff < br_lpg_release_samples() + 64u);
    assert(eoff >= br_time_samples(75u, 1u) && eoff < br_time_samples(75u, 1u) + 64u);
    gate_test(1u, 0u); gate_test(15u, 0u); gate_test(31u, 0u); gate_test(15u, 1u);
    mono_gate_test(15u, 1u, 0u); mono_gate_test(15u, 1u, 1u);
    mono_gate_test(0u, 1u, 0u); mono_gate_test(15u, 0u, 0u);
    puts("PASS: rendered LPG voice stops sooner after note-off");
    return 0;
}
