/* SPDX-License-Identifier: GPL-3.0-only */
/* The experimental MODEL path through the real source-job queue. */
#define BR_MODEL_MOD_EXPERIMENT 1
#define BR_POLY_SCHEDULER_EXPERIMENT 1
#define main reconstruction_fixture_main
#include "test_partial_frame_reconstruction.c"
#undef main
#if BR_MODEL_PARALLEL
#include <pthread.h>
static void *model_worker(void *unused)
{
    (void)unused; test_core_id = 1u;
    return (void *)(uintptr_t)br_audio_source_queue_run(1u);
}
#endif
static void run_source_jobs(void)
{
#if BR_MODEL_PARALLEL
    pthread_t worker; void *result;
    assert(!pthread_create(&worker, 0, model_worker, 0));
    assert(br_audio_source_queue_run(1u));
    assert(!pthread_join(worker, &result) && (uintptr_t)result == 1u);
#else
    assert(br_audio_source_queue_run(1u));
#endif
}
static uint64_t audio_hash = UINT64_C(1469598103934665603);
static void hash_audio(void)
{
    for (unsigned i = 0; i < BR_BLOCK; ++i) {
        audio_hash ^= (uint32_t)bus[0][i];
        audio_hash *= UINT64_C(1099511628211);
    }
}

int main(void)
{
    br_sound_t sound;
    configure(BR_KIT_808, 0u);
    br_preset_sound(BR_FIRST_PLAITS, &sound);
    sound.attack = 0u; sound.decay = 0u; sound.sustain = 127u; sound.level = 100u;
    br_part[0].sound = sound; br_part[0].revision += 2u;
    br_snapshot(0u); br_build_effective(0u);
    assert(br_poly_scheduler_enter(0u, 1));
    assert(br_fx_poly_loan_active());
    for (unsigned i = 0; i < BR_POLY_VOICES; ++i)
        br_engine_note_on_tagged(0u, 60u + i, 100u, i + 1u);
    br_take_events();
    assert(br_poly_scheduler_events.count == BR_POLY_VOICES);
    br_part[0].mod.lfo[0].wave = BR_WAVE_SQUARE;
    br_part[0].mod.lfo[0].target = BR_TARGET_MODEL;
    br_part[0].mod.lfo[0].depth = 99u;
    br_part[0].revision += 2u;
    br_mod_state[0].phase[0] = 0u;
    br_snapshot(0u); br_build_effective(0u);
    assert(br_effective_sound[0].preset == BR_FIRST_PLAITS + 18u);
    for (unsigned frame = 0; frame < 7u; ++frame) {
        unsigned changed = 0u;
        assert(br_fx_fusion_prepare(bus, BR_BLOCK));
        br_audio_source_fx_fusion_active = 1u;
        memset(&br_audio_source_queue, 0, sizeof br_audio_source_queue);
        br_audio_source_queue.count = br_poly_scheduler_build_jobs(bus, BR_BLOCK);
        br_audio_source_queue.deadline_tick = 100000u;
        run_source_jobs();
        assert(br_audio_source_queue.completed == 8u && br_fx_fusion_complete(bus, BR_BLOCK));
        hash_audio();
        for (unsigned i = 0; i < BR_POLY_VOICES; ++i) {
            uint8_t model = br_poly_scheduler.source.voice_model[i];
            assert(model == 0u || model == 18u);
            changed += model == 18u;
            assert(br_poly_scheduler.envelope[i].stage != BR_ENV_OFF);
        }
        assert(changed == (frame < 6u ? frame + 1u : 6u));
        br_fx_fusion_cancel();
    }
    /* Rapid reversals coalesce: never switch more than one held voice per
     * worker frame, even if the requested model changes every frame. */
    for (unsigned frame = 0; frame < 24u; ++frame) {
        uint8_t before[BR_POLY_VOICES]; unsigned changes = 0u;
        memcpy(before, br_poly_scheduler.source.voice_model, sizeof before);
        br_mod_state[0].phase[0] = frame & 1u ? 0x80000000u : 0u;
        br_build_effective(0u);
        assert(br_fx_fusion_prepare(bus, BR_BLOCK));
        br_audio_source_fx_fusion_active = 1u;
        memset(&br_audio_source_queue, 0, sizeof br_audio_source_queue);
        br_audio_source_queue.count = br_poly_scheduler_build_jobs(bus, BR_BLOCK);
        br_audio_source_queue.deadline_tick = 100000u;
        run_source_jobs();
        assert(br_audio_source_queue.completed == 8u && br_fx_fusion_complete(bus, BR_BLOCK));
        hash_audio();
        for (unsigned i = 0; i < BR_POLY_VOICES; ++i) {
            changes += before[i] != br_poly_scheduler.source.voice_model[i];
            assert(br_poly_scheduler.envelope[i].stage != BR_ENV_OFF);
        }
        assert(changes <= 1u);
        br_fx_fusion_cancel();
    }
    br_mod_state[0].phase[0] = 0x80000000u;
    br_build_effective(0u);
    assert(br_effective_sound[0].preset == BR_FIRST_PLAITS);
    unsigned previous = 0u;
    for (unsigned i = 0; i < BR_POLY_VOICES; ++i) previous += br_poly_scheduler.source.voice_model[i] == 0u;
    for (unsigned frame = 0; frame < 7u; ++frame) {
        unsigned changed = 0u;
        assert(br_fx_fusion_prepare(bus, BR_BLOCK));
        br_audio_source_fx_fusion_active = 1u;
        memset(&br_audio_source_queue, 0, sizeof br_audio_source_queue);
        br_audio_source_queue.count = br_poly_scheduler_build_jobs(bus, BR_BLOCK);
        br_audio_source_queue.deadline_tick = 100000u;
        run_source_jobs();
        assert(br_audio_source_queue.completed == 8u && br_fx_fusion_complete(bus, BR_BLOCK));
        hash_audio();
        for (unsigned i = 0; i < BR_POLY_VOICES; ++i) {
            changed += br_poly_scheduler.source.voice_model[i] == 0u;
            assert(br_poly_scheduler.envelope[i].stage != BR_ENV_OFF);
        }
        assert(changed >= previous && changed <= previous + 1u);
        previous = changed;
        br_fx_fusion_cancel();
    }
    assert(previous == BR_POLY_VOICES);
    br_engine_note_off_tagged(0u, 63u, 4u); br_take_events();
    assert(br_poly_scheduler_events.count == 5u);
    assert(br_poly_scheduler.envelope[3].stage == BR_ENV_RELEASE);
    assert(br_poly_scheduler_leave(1));
    assert(!br_fx_poly_loan_active());
    printf("MODEL poly: six held voices, note-off and loan restored; audio %016llx\n", (unsigned long long)audio_hash);
    return 0;
}
