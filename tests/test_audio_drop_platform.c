/* SPDX-License-Identifier: GPL-3.0-only
 * Compiles actual extracted platform functions against a fake DMA/IRQ model.
 * This tests firmware control flow; it does not model ALNK hardware timing. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_render_status.h"
#include "../firmware/src/midi_out_schedule.h"

#define FM1_DEVICE 1
#define BR_SPLIT_AUDIO 1
#define FM1_RUNTIME_IRQ_PROFILE FM1_DEVELOPMENT_DIAGNOSTICS
#define FM1_RUNTIME_CHUNK_PROFILE FM1_DEVELOPMENT_DIAGNOSTICS
#define FM1_DRUM_PROFILE FM1_DEVELOPMENT_DIAGNOSTICS
#define FELUCCA_UAC 0
#define FM1_AUDIO_HALF 0x80u
#define HALF_FRAMES 4u
#define HALF_WORDS (2u * HALF_FRAMES)
#define BR_AUDIO_BLOCK_FRAMES HALF_FRAMES
#define BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS 80u
#define FM1_AUDIO_HALF_TICKS 100u

static int32_t abuf[2u * HALF_WORDS];
static uint32_t audio_block_commits;
static volatile uint8_t in_audio;
typedef struct { uint32_t last_ticks, worst_ticks, overruns, blocks, window_sum, window_count; } metrics_t;
static volatile metrics_t fm1_audio_metrics;
static volatile uint32_t fm1_audio_late_drops, fm1_audio_late_resync_skips;
static volatile uint32_t fm1_audio_late_contained_faults;
static volatile uint32_t fm1_audio_late_over_one_half, fm1_audio_late_over_two_halves;
static volatile uint32_t fm1_audio_late_resync_active, fm1_audio_late_resync_baseline;
static volatile uint32_t fm1_audio_cpu1_join_tick, fm1_audio_cpu1_join_valid;
static volatile uint32_t fm1_audio_tail_last_wait_to_ack, fm1_audio_tail_max_wait_to_ack;
static volatile uint32_t fm1_audio_tail_last_half_duration, fm1_audio_tail_max_half_duration;
static volatile uint32_t fm1_audio_tail_valid_halves;

static uint32_t fault, app_result, app_commit, app_fault, app_calls, contained_latched;
static uint32_t fake_tick, app_ticks, free_half, toggle_half_on_ack;
static uint32_t pending, ack_count, uac_start_count, commit_count, uac_tap_count;
static uint32_t deadline_sets, deadline_clears, irq_seals, trace_seals, drum_seals;
static uint32_t engine_drop_count, engine_reset_count, irq_depth;

static uint32_t br_engine_pipeline_uncontained_fault_get(void) { return fault; }
static uint32_t irq_save(void) { ++irq_depth; return 0u; }
static void irq_restore(uint32_t state) { (void)state; assert(irq_depth); --irq_depth; }
static uint32_t br_foreground_irq_save(void) { return 0u; }
static void br_foreground_irq_restore(uint32_t state) { (void)state; }
static uint32_t fm1_ticks(void) { return fake_tick; }
static uint8_t fm1_audio_pending(void) { return (uint8_t)pending; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return free_half; }
static void fm1_audio_ack_half(void)
{
    ++ack_count;
    pending = 0u;
    if (toggle_half_on_ack) free_half ^= 1u;
}
static void uac_render_start(void) { ++uac_start_count; }
static fm1_midi_out_schedule_t br_midi_out_schedule;
static void br_engine_pipeline_source_deadline_set(uint32_t deadline)
{ (void)deadline; ++deadline_sets; }
static void br_engine_pipeline_source_deadline_clear(void) { ++deadline_clears; }
static void fm1_runtime_irq_profile_begin(void) {}
static void fm1_runtime_trace_half_begin(uint32_t tick) { (void)tick; }
static void fm1_drum_profile_half_begin(uint32_t tick) { (void)tick; }
static void fm1_runtime_irq_profile_seal(uint32_t a, uint32_t b)
{ (void)a; (void)b; ++irq_seals; }
static void fm1_runtime_trace_half_seal(uint32_t a, uint32_t b)
{ (void)a; (void)b; ++trace_seals; }
static void fm1_drum_profile_half_seal(uint32_t a, uint32_t b)
{ (void)a; (void)b; ++drum_seals; }
static void fm1_audio_tail_metrics_reset(void) {}
static void br_audio_source_job_timing_reset(void) {}
static void br_audio_late_drop_reset(void)
{ assert(irq_depth); engine_drop_count = 0u; ++engine_reset_count; }

static void platform_audio_block_done(int32_t *out, uint32_t frames)
{
    ++commit_count;
    ++uac_tap_count; /* represents the normal UAC tap inside a block commit */
    ++audio_block_commits;
    for (uint32_t i = 0u; i < 2u * frames; ++i) ++out[i];
}

static uint32_t app_audio(int32_t *out, uint32_t frames)
{
    ++app_calls;
    for (uint32_t i = 0u; i < 2u * frames; ++i) {
        assert(out[i] == 0); /* wrapper's existing preclear */
        out[i] = (int32_t)(100u + i); /* canary against later writes */
    }
    fake_tick += app_ticks;
    if (app_commit) {
        /* Exercise false-complete seals even if a caller reports a commit. */
        audio_block_commits = 1u;
        fm1_audio_cpu1_join_valid = 1u;
        fm1_audio_cpu1_join_tick = fake_tick;
    }
    if (app_fault) fault = 1u;
    if (app_result == FM1_AUDIO_RENDER_CONTAINED_FAULT) contained_latched = 1u;
    return app_result;
}

#include "audio_drop_extracted.inc"

static void test_reset_metrics(void)
{
#if FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_audio_metrics_reset();
#else
    /* Isolate test scenarios without adding a reset path to the firmware. */
    fm1_audio_late_drops = fm1_audio_late_contained_faults = 0u;
    fm1_audio_late_over_one_half = fm1_audio_late_over_two_halves = 0u;
    fm1_audio_late_resync_skips = 0u;
    { uint32_t irq = irq_save(); br_audio_late_drop_reset(); irq_restore(irq); }
#endif
}

static void assert_canary(uint32_t half, uint32_t shift, uint32_t add)
{
    for (uint32_t i = 0u; i < HALF_WORDS; ++i)
        assert(abuf[half * HALF_WORDS + i] == (int32_t)((100u + i + add) << shift));
}

int main(void)
{
    for (uint32_t i = 0u; i < 2u * HALF_WORDS; ++i) abuf[i] = 999;
    fake_tick = 1000u;
    pending = FM1_AUDIO_HALF;
    free_half = 0u;
    toggle_half_on_ack = 1u; /* a boundary occurs during ACK */
    app_result = FM1_AUDIO_RENDER_DROP_LATE;
    app_ticks = 150u;
    app_commit = 1u;
    fm1_audio_tail_last_half_duration = 77u;
    fm1_audio_tail_max_half_duration = 88u;
    fm1_audio_tail_valid_halves = 9u;
    fm1_alnk0_irq();
    assert_canary(0u, 0u, 0u);
    for (uint32_t i = HALF_WORDS; i < 2u * HALF_WORDS; ++i) assert(abuf[i] == 999);
    assert(app_calls == 1u && commit_count == 0u && uac_tap_count == 0u);
    assert(fm1_audio_late_drops == FM1_DEVELOPMENT_DIAGNOSTICS && fm1_audio_late_resync_active == 1u);
    assert(fm1_audio_late_over_one_half == FM1_DEVELOPMENT_DIAGNOSTICS && fm1_audio_late_over_two_halves == 0u);
    assert(fm1_audio_late_resync_baseline == 1u && ack_count == 1u);
    assert(fm1_audio_metrics.window_sum == 150u && fm1_audio_metrics.window_count == 1u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_audio_metrics.last_ticks == 150u && fm1_audio_metrics.worst_ticks == 150u);
    assert(fm1_audio_metrics.overruns == 1u && fm1_audio_metrics.blocks == 1u);
#endif
    assert(deadline_sets == 1u && deadline_clears == 1u && in_audio == 0u);
    assert(irq_seals == 0u && trace_seals == 0u && drum_seals == 0u);
    assert(fm1_audio_tail_last_half_duration == 77u && fm1_audio_tail_max_half_duration == 88u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_audio_tail_valid_halves == 9u && fm1_audio_cpu1_join_valid == 0u);
    assert(fm1_audio_tail_last_wait_to_ack == UINT32_MAX);
#endif

    /* Re-entry with the baseline phase is acknowledged without any render. */
    toggle_half_on_ack = 0u;
    pending = FM1_AUDIO_HALF;
    fm1_alnk0_irq();
    assert(fm1_audio_late_resync_skips == FM1_DEVELOPMENT_DIAGNOSTICS && fm1_audio_late_resync_active == 1u);
    assert(app_calls == 1u && commit_count == 0u && ack_count == 2u);
    assert_canary(0u, 0u, 0u);

    /* A diagnostics reset must never clear an armed output phase guard. */
    engine_drop_count = 3u;
    test_reset_metrics();
    assert(fm1_audio_late_drops == 0u && fm1_audio_late_resync_skips == 0u);
    assert(fm1_audio_late_over_one_half == 0u && fm1_audio_late_over_two_halves == 0u);
    assert(engine_drop_count == 0u && engine_reset_count == 1u && irq_depth == 0u);
    assert(fm1_audio_late_resync_active == 1u && fm1_audio_late_resync_baseline == 1u);

    /* An opposite phase permits an ordinary fresh callback and normal commit. */
    pending = FM1_AUDIO_HALF;
    free_half = 0u;
    app_result = FM1_AUDIO_RENDER_OK;
    app_ticks = 40u;
    app_commit = 0u;
    fm1_alnk0_irq();
    assert(fm1_audio_late_resync_active == 0u && app_calls == 2u);
    assert(commit_count == 1u && uac_tap_count == 1u && ack_count == 3u);
    assert_canary(0u, 7u, 1u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_audio_metrics.last_ticks == 40u && fm1_audio_metrics.blocks == 1u);
#endif

    /* Committed normal output keeps the successful-half profiling behavior. */
    pending = FM1_AUDIO_HALF;
    free_half = 1u;
    app_ticks = 200u;
    app_commit = 1u;
    fm1_alnk0_irq();
    assert_canary(1u, 7u, 0u);
    assert(commit_count == 1u && uac_tap_count == 1u); /* no fallback */
    assert(irq_seals == FM1_DEVELOPMENT_DIAGNOSTICS && trace_seals == 2u * FM1_DEVELOPMENT_DIAGNOSTICS && drum_seals == FM1_DEVELOPMENT_DIAGNOSTICS);

    /* Unknown status or a newly uncontained fault cannot reach output commit. */
    pending = FM1_AUDIO_HALF;
    free_half = 1u;
    app_result = 99u;
    app_ticks = 5u;
    app_commit = 0u;
    fm1_alnk0_irq();
    assert_canary(1u, 0u, 0u);
    assert(commit_count == 1u && fm1_audio_late_drops == 0u);
    pending = FM1_AUDIO_HALF;
    app_result = FM1_AUDIO_RENDER_DROP_LATE;
    app_fault = 1u;
    fm1_alnk0_irq();
    assert_canary(1u, 0u, 0u);
    assert(commit_count == 1u && fm1_audio_late_drops == 0u && fault == 1u);
    pending = FM1_AUDIO_HALF;
    fm1_alnk0_irq(); /* sticky ownership fault excludes even the preclear */
    assert_canary(1u, 0u, 0u);
    assert(app_calls == 5u && commit_count == 1u && in_audio == 0u);
    assert(irq_seals == FM1_DEVELOPMENT_DIAGNOSTICS && trace_seals == 2u * FM1_DEVELOPMENT_DIAGNOSTICS && drum_seals == FM1_DEVELOPMENT_DIAGNOSTICS);

    /* A drop spanning more than two halves records the extra duration and
     * uses the same conservative phase guard without another output write. */
    fault = app_fault = 0u;
    pending = FM1_AUDIO_HALF;
    free_half = 0u;
    app_result = FM1_AUDIO_RENDER_DROP_LATE;
    app_ticks = 250u;
    fm1_alnk0_irq();
    assert_canary(0u, 0u, 0u);
    assert(fm1_audio_late_drops == FM1_DEVELOPMENT_DIAGNOSTICS);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_audio_metrics.last_ticks == 250u);
#endif
    assert(fm1_audio_late_over_one_half == FM1_DEVELOPMENT_DIAGNOSTICS && fm1_audio_late_over_two_halves == FM1_DEVELOPMENT_DIAGNOSTICS);
    assert(fm1_audio_late_resync_active == 1u && fm1_audio_late_resync_baseline == 0u);
    assert(commit_count == 1u && uac_tap_count == 1u);
    pending = FM1_AUDIO_HALF;
    fm1_alnk0_irq();
    assert(fm1_audio_late_resync_active == 1u && fm1_audio_late_resync_skips == FM1_DEVELOPMENT_DIAGNOSTICS);
    assert_canary(0u, 0u, 0u);
    pending = FM1_AUDIO_HALF;
    free_half = 1u;
    app_result = FM1_AUDIO_RENDER_OK;
    app_ticks = 10u;
    fm1_alnk0_irq();
    assert(fm1_audio_late_resync_active == 0u);
    assert(fm1_audio_late_over_two_halves == FM1_DEVELOPMENT_DIAGNOSTICS);
    assert_canary(1u, 7u, 1u);

    /* A contained, latched fault may also arrive after rollover. Keep its
     * count distinct from successful recovery and never write the old half. */
    test_reset_metrics();
    uint32_t commits_before = commit_count, taps_before = uac_tap_count;
    uint32_t acks_before = ack_count, calls_before = app_calls;
    uint32_t seals_before = irq_seals, traces_before = trace_seals, drums_before = drum_seals;
    pending = FM1_AUDIO_HALF;
    free_half = 0u;
    toggle_half_on_ack = 1u;
    app_result = FM1_AUDIO_RENDER_CONTAINED_FAULT;
    app_ticks = 250u;
    app_commit = 1u; /* even a reported partial commit cannot cause sealing */
    fm1_alnk0_irq();
    assert(contained_latched && !fault && app_calls == calls_before + 1u);
    assert_canary(0u, 0u, 0u);
    assert(commit_count == commits_before && uac_tap_count == taps_before);
    assert(fm1_audio_late_contained_faults == FM1_DEVELOPMENT_DIAGNOSTICS && fm1_audio_late_drops == 0u);
    assert(!fm1_audio_late_over_one_half && !fm1_audio_late_over_two_halves);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_audio_metrics.last_ticks == 250u && fm1_audio_metrics.overruns == 1u);
#endif
    assert(ack_count == acks_before + 1u && fm1_audio_late_resync_active == 1u);
    assert(fm1_audio_late_resync_baseline == 1u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_audio_cpu1_join_valid == 0u);
#endif
    assert(irq_seals == seals_before && trace_seals == traces_before && drum_seals == drums_before);
    assert(in_audio == 0u);

    /* Resetting metrics preserves the armed guard and the separate sticky
     * application fault. Same-phase re-entry cannot touch either half. */
    test_reset_metrics();
    assert(fm1_audio_late_contained_faults == 0u && contained_latched == 1u);
    assert(fm1_audio_late_resync_active == 1u && fm1_audio_late_resync_baseline == 1u);
    pending = FM1_AUDIO_HALF;
    toggle_half_on_ack = 0u;
    fm1_alnk0_irq();
    assert(app_calls == calls_before + 1u && ack_count == acks_before + 2u);
    assert(fm1_audio_late_resync_skips == FM1_DEVELOPMENT_DIAGNOSTICS && contained_latched == 1u);
    assert_canary(0u, 0u, 0u);

    /* A new uncontained ownership fault takes priority over contained status. */
    free_half = 0u;
    pending = FM1_AUDIO_HALF;
    app_fault = 1u;
    fm1_alnk0_irq();
    assert(fault == 1u && fm1_audio_late_contained_faults == 0u);
    assert(commit_count == commits_before && uac_tap_count == taps_before);
    assert_canary(0u, 0u, 0u);
    puts(FM1_DEVELOPMENT_DIAGNOSTICS ?
         "PASS: platform late-drop, ownership, resync, metrics and development profile seals" :
         "PASS: distribution platform late-drop, ownership, resync and metrics");
    return 0;
}
