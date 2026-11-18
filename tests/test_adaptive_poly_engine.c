/* SPDX-License-Identifier: GPL-3.0-only */
/* Real engine admission, voice allocation and MODEL-route reconfiguration. */
#define BR_POLY_SCHEDULER_EXPERIMENT 1
#define main reconstruction_fixture_main
#include "test_partial_frame_reconstruction.c"
#undef main
#include "../firmware/plaits/upstream/plaits/wavetable_pack.h"
int main(void)
{
#if FM1_PLAITS_PACK_WAVES
    assert(fm1_plaits_waves_init(NULL));
#endif
    for (unsigned model = 0; model < 24; ++model) {
        br_sound_t sound;
        configure(BR_KIT_808, 0u);
        br_preset_sound(BR_FIRST_PLAITS + model, &sound);
        sound.attack = 0; sound.decay = 0; sound.sustain = 127; sound.level = 100;
        br_part[0].sound = sound; br_part[0].revision += 2;
        br_snapshot(0); br_build_effective(0);
        fprintf(stderr, "testing model %u bytes=%u primary=%zu loan=%zu cap=%u\n", model,
                plaits_compact_state_bytes(model), sizeof br_dsp, sizeof br_delay_line - sizeof br_delay_line[0], br_poly_model_limit(model));
        assert(br_poly_scheduler_enter(0, 1));
        if (model == 0) {
            uint32_t before=br_mod_state[0].note_random;
            assert(br_poly_scheduler_event(BR_EVENT_ON,0,60,100,900));
            uint32_t first=br_mod_state[0].note_random; assert(first!=before);
            assert(br_poly_scheduler_event(BR_EVENT_ON,0,64,100,901));
            uint32_t second=br_mod_state[0].note_random; assert(second!=first);
            assert(br_poly_scheduler_event(BR_EVENT_OFF,0,64,0,901));
            assert(br_mod_state[0].note_random==second); /* newest note remains shared */
            assert(br_poly_scheduler_event(BR_EVENT_STOP,0,0,0,0));
        }
        uint32_t cap = br_poly_model_limit(model), state[4];
        br_poly_capacity_snapshot(state);
        assert(state[0] && state[1] == cap);
        unsigned next = 0, low = 8, high = 0;
        for (unsigned lane = 0; lane < 4; ++lane) {
            unsigned first = br_poly_lane_first(lane), count = br_poly_lane_count(lane);
            assert(first == next); next += count;
            if (count < low) low = count; if (count > high) high = count;
        }
        assert(next == cap && high - low <= 1);
        for (unsigned n = 0; n < cap; ++n) br_engine_note_on_tagged(0, 50 + n, 100, n + 1);
        br_engine_render(bus, BR_BLOCK);
        assert(br_audio_source_queue.count == 8u && br_audio_source_queue.completed == 8u);
        assert(br_fx_fusion_complete(bus, BR_BLOCK)); br_fx_fusion_cancel();
        br_poly_capacity_snapshot(state); assert(state[2] == cap);
        assert(br_poly_scheduler_leave(1));
    }
    configure(BR_KIT_808, 0u);
    br_part[0].mod.lfo[0].target = BR_TARGET_MODEL;
    br_part[0].mod.lfo[0].depth = 50;
    br_part[0].revision += 2; br_snapshot(0); br_build_effective(0);
    assert(br_poly_scheduler_enter(0, 1));
    uint32_t state[4]; br_poly_capacity_snapshot(state);
    assert(state[1] == 3 && br_poly_sweep_capable);
    assert(br_poly_scheduler_leave(1));
    /* A new MODEL route may not resize the pool until held notes are released. */
    configure(BR_KIT_808, 0u);
    br_part[0].sound.voice_mode = 2u;
    br_part[0].mod.lfo[0].target = BR_TARGET_PITCH;
    br_part[0].mod.lfo[0].depth = 50;
    br_part[0].revision += 2; br_snapshot(0); br_build_effective(0);
    assert(br_poly_scheduler_enter(0, 1) && !br_poly_sweep_capable);
    br_part[0].mod.lfo[0].target = BR_TARGET_MODEL;
    br_part[0].revision += 2; br_snapshot(0); br_build_effective(0);
    br_live_count[0] = 1;
    assert(br_poly_transition_needed(0, 0) == 0);
    br_live_count[0] = 0; br_env[0].stage = BR_ENV_OFF;
    assert(br_poly_transition_needed(0, 0) == 2);
    assert(br_poly_transition_commit(2, 0));
    assert(br_poly_transition_needed(0, 0) == 1);
    assert(br_poly_transition_commit(1, 0));
    br_poly_capacity_snapshot(state); assert(state[1] == 3 && br_poly_sweep_capable);
    assert(br_poly_scheduler_leave(1));
    puts("all model capacities and MODEL route admission passed");
    return 0;
}
