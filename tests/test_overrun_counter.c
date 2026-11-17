/* SPDX-License-Identifier: GPL-3.0-only
 * Audio overrun and load counters at both build levels (tests/run_overrun_counter.py).
 * OVERRUN_PLATFORM: the actual audio IRQ, render wrapper and counter functions extracted from
 *   firmware/src/platform.c, driven by a fake DMA and app with chosen render times.
 * OVERRUN_PROTOCOL: the actual app command handler (dinghy.c, BR_DIAGNOSTIC_TEST) reading the
 *   same extracted counters: exact 0x60/0x61/0xB2 replies; distribution refuses development-only
 *   commands. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t word_at(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

#if defined(OVERRUN_PLATFORM)
#include "audio_render_status.h"

#define FM1_DEVICE 1
#define BR_SPLIT_AUDIO 1
#define FM1_RUNTIME_IRQ_PROFILE 0
#define FM1_RUNTIME_CHUNK_PROFILE 0
#define FM1_DRUM_PROFILE 0
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
static volatile uint32_t fm1_audio_cpu1_join_tick, fm1_audio_cpu1_join_valid;
static volatile uint32_t fm1_audio_tail_last_wait_to_ack, fm1_audio_tail_max_wait_to_ack;
static volatile uint32_t fm1_audio_tail_last_half_duration, fm1_audio_tail_max_half_duration;
static volatile uint32_t fm1_audio_tail_valid_halves;
static uint32_t fake_tick, app_ticks, app_result, free_half, pending, fault, irq_depth;
static uint32_t app_calls, engine_resets, job_timing_resets;

static uint32_t br_engine_pipeline_uncontained_fault_get(void) { return fault; }
static uint32_t irq_save(void) { ++irq_depth; return 0u; }
static void irq_restore(uint32_t state) { (void)state; assert(irq_depth); --irq_depth; }
static uint32_t br_foreground_irq_save(void) { return 0u; }
static void br_foreground_irq_restore(uint32_t state) { (void)state; }
static uint32_t fm1_ticks(void) { return fake_tick; }
static uint8_t fm1_audio_pending(void) { return (uint8_t)pending; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return free_half; }
static void fm1_audio_ack_half(void) { pending = 0u; }
static void uac_render_start(void) { }
static void br_engine_pipeline_source_deadline_set(uint32_t deadline) { (void)deadline; }
static void br_engine_pipeline_source_deadline_clear(void) { }
static void fm1_audio_tail_metrics_reset(void) { }
static void br_audio_source_job_timing_reset(void) { assert(irq_depth); ++job_timing_resets; }
static void br_audio_late_drop_reset(void) { assert(irq_depth); ++engine_resets; }
static void br_engine_note_clock(uint32_t tick) { (void)tick; }   /* played-note timing: not under test */
static void platform_audio_block_done(int32_t *out, uint32_t frames)
{ (void)out; (void)frames; ++audio_block_commits; }
static uint32_t app_audio(int32_t *out, uint32_t frames)
{
    (void)out; (void)frames;
    ++app_calls;
    fake_tick += app_ticks;
    return app_result;
}

#include "overrun_counters.inc"
#include "overrun_irq.inc"

/* One DMA half: alternate the free half so the post-drop phase guard sees a fresh half. */
static void half(uint32_t ticks, uint32_t result)
{
    free_half ^= 1u;
    pending = FM1_AUDIO_HALF;
    app_ticks = ticks;
    app_result = result;
    fake_tick += 1000u;
    fm1_alnk0_irq();
    assert(!in_audio && !irq_depth);
}

/* The firmware's average: load += ticks - load / 64, reported as load / 64. */
static uint32_t model_load;
static void model(uint32_t ticks) { model_load = model_load + ticks - (model_load >> 6); }

int main(void)
{
    uint32_t w[FM1_AUDIO_METRICS_WORDS], t[3], i, calls;
    fm1_audio_metrics_snapshot(w);
    for (i = 0u; i < FM1_AUDIO_METRICS_WORDS; ++i) assert(!w[i]);

    /* On-time halves: counted, no overrun; worst and the window maximum follow. */
    for (i = 0u; i < 10u; ++i) { half(40u + i, FM1_AUDIO_RENDER_OK); model(40u + i); }
    fm1_audio_metrics_snapshot(w);
    assert(w[0] == 49u && w[1] == 49u && w[2] == 0u && w[3] == 10u);
    assert(w[8] == model_load >> 6 && w[9] == 0u);
    assert(fm1_audio_metrics.load_q6 == model_load && fm1_audio_metrics.window_max == 49u);

    /* A complete half that ran past its deadline: an overrun, not a late drop. */
    half(150u, FM1_AUDIO_RENDER_OK); model(150u);
    fm1_audio_metrics_snapshot(w);
    assert(w[0] == 150u && w[1] == 150u && w[2] == 1u && w[3] == 11u && w[9] == 1u);
    assert(fm1_audio_late_drops == 0u);

    /* A half the engine dropped late (more than two halves long): both counters, plus the spans. */
    half(250u, FM1_AUDIO_RENDER_DROP_LATE); model(250u);
    fm1_audio_metrics_snapshot(w);
    assert(w[1] == 250u && w[2] == 2u && w[3] == 12u && w[9] == 2u && w[8] == model_load >> 6);
    assert(fm1_audio_late_drops == 1u && fm1_audio_late_over_one_half == 1u &&
           fm1_audio_late_over_two_halves == 1u && fm1_audio_late_resync_active == 1u);
    /* A late drop inside the deadline counts as a drop only. */
    half(90u, FM1_AUDIO_RENDER_DROP_LATE); model(90u);
    fm1_audio_metrics_snapshot(w);
    assert(w[2] == 2u && fm1_audio_late_drops == 2u && fm1_audio_late_over_one_half == 1u);

    /* The phase guard's skipped indication renders nothing and counts nothing. */
    calls = app_calls;
    pending = FM1_AUDIO_HALF; fm1_alnk0_irq();
    assert(app_calls == calls && fm1_audio_late_resync_skips == 1u);
    fm1_audio_metrics_snapshot(w);
    assert(w[3] == 13u);
    /* An uncontained fault touches no counter. */
    fault = 1u; pending = FM1_AUDIO_HALF; app_ticks = 500u; fm1_alnk0_irq(); fault = 0u;
    fm1_audio_metrics_snapshot(w);
    assert(w[1] == 250u && w[3] == 13u && w[2] == 2u);

    /* MONITOR window: average, the largest half since the last take (then cleared), boot overruns. */
    fm1_audio_window_take(t);
    assert(t[0] == model_load >> 6 && t[1] == 250u && t[2] == 2u);
    assert(fm1_audio_metrics.window_max == 0u && fm1_audio_metrics.worst_ticks == 250u);
    half(30u, FM1_AUDIO_RENDER_OK); model(30u);
    fm1_audio_window_take(t);
    assert(t[1] == 30u && fm1_audio_metrics.worst_ticks == 250u);

    /* 0x61 reset: since-reset counters cleared; average and boot overruns kept. */
    fm1_audio_metrics_reset();
    assert(engine_resets == 1u && job_timing_resets == 1u && !irq_depth);
    fm1_audio_metrics_snapshot(w);
    assert(!w[0] && !w[1] && !w[2] && !w[3] && w[8] == model_load >> 6 && w[9] == 2u);
    assert(!fm1_audio_late_drops && !fm1_audio_late_over_one_half && !fm1_audio_late_over_two_halves &&
           !fm1_audio_late_resync_skips && !fm1_audio_late_contained_faults);
    assert(fm1_audio_metrics.window_max == 0u);
    half(60u, FM1_AUDIO_RENDER_OK); model(60u);   /* worst >= window maximum holds after a reset */
    assert(fm1_audio_metrics.worst_ticks == 60u && fm1_audio_metrics.window_max == 60u);
    half(20u, FM1_AUDIO_RENDER_OK); model(20u);
    assert(fm1_audio_metrics.worst_ticks == 60u && fm1_audio_metrics.last_ticks == 20u);

    /* A steady load: the average settles on it exactly. */
    for (i = 0u; i < 2000u; ++i) half(80u, FM1_AUDIO_RENDER_OK);
    fm1_audio_metrics_snapshot(w);
    assert(w[8] == 80u && w[2] == 0u);

    /* Saturation: neither overrun counter wraps. */
    fm1_audio_metrics.overruns = UINT32_MAX - 1u;
    fm1_audio_metrics.overruns_boot = UINT32_MAX - 1u;
    half(101u, FM1_AUDIO_RENDER_OK);
    half(101u, FM1_AUDIO_RENDER_OK);
    half(101u, FM1_AUDIO_RENDER_OK);
    fm1_audio_metrics_snapshot(w);
    assert(w[2] == UINT32_MAX && w[9] == UINT32_MAX);
    half(100u, FM1_AUDIO_RENDER_OK);              /* exactly the deadline is not an overrun */
    fm1_audio_metrics_snapshot(w);
    assert(w[2] == UINT32_MAX && w[0] == 100u);
    printf("PASS: overrun counter platform (%s)\n", FM1_DEVELOPMENT_DIAGNOSTICS ? "development" : "distribution");
    return 0;
}

#elif defined(OVERRUN_PROTOCOL)
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls;
uint32_t br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
#define BR_SPLIT_AUDIO 1
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static uint32_t irq_save(void) { return 0u; }
static void irq_restore(uint32_t state) { (void)state; }
void br_audio_late_drop_reset(void);
void br_audio_source_job_timing_reset(void);
#include "overrun_counters.inc"     /* the real counters and their snapshot/reset functions */
#define DINGHY_HOST 1
#define BR_DIAGNOSTIC_TEST 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/instrument_audio_unit.c"

int main(void)
{
    static const uint8_t expect60[40] = {
        11, 0, 0, 0, 22, 0, 0, 0, 33, 0, 0, 0, 44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        55, 0, 0, 0, 0x78, 0x56, 0x34, 0x12};
    uint8_t reply[128], body[4] = {'B', 'R', 1u, 0u}, bad[4] = {'B', 'R', 1u, 1u};
    uint32_t n, i;
    br_app_init();

    fm1_audio_metrics.last_ticks = 11u;
    fm1_audio_metrics.worst_ticks = 22u;
    fm1_audio_metrics.overruns = 33u;
    fm1_audio_metrics.blocks = 44u;
    fm1_audio_metrics.window_max = 22u;
    fm1_audio_metrics.load_q6 = (55u << 6) | 63u;
    fm1_audio_metrics.overruns_boot = 0x12345678u;
    memset(reply, 0xee, sizeof reply);
    assert(fm1_app_command(0x60u, 0, 0u, reply, &n) && n == 40u);
    assert(!memcmp(reply, expect60, sizeof expect60));
    assert(!fm1_app_command(0x60u, body, 1u, reply, &n));       /* only the empty read */

    /* 0x61 needs exactly BR 01 00 and clears only the since-reset counters. */
    assert(!fm1_app_command(0x61u, bad, 4u, reply, &n));
    assert(!fm1_app_command(0x61u, body, 3u, reply, &n));
    assert(fm1_audio_metrics.worst_ticks == 22u);
    assert(fm1_app_command(0x61u, body, 4u, reply, &n) && n == 1u && reply[0] == 1u);
    assert(!fm1_audio_metrics.last_ticks && !fm1_audio_metrics.worst_ticks && !fm1_audio_metrics.overruns &&
           !fm1_audio_metrics.blocks && !fm1_audio_metrics.window_max);
    assert(fm1_audio_metrics.load_q6 == ((55u << 6) | 63u) && fm1_audio_metrics.overruns_boot == 0x12345678u);
    assert(fm1_app_command(0x60u, 0, 0u, reply, &n) && n == 40u);
    for (i = 0u; i < 8u; ++i) assert(!word_at(reply + 4u * i));
    assert(word_at(reply + 32u) == 55u && word_at(reply + 36u) == 0x12345678u);

    /* 0xB2, schema 3, 17 words (host: the CPU0 output words 7-10 and 13-15 are device-only zeros). */
    br_audio_late_drop_count = 4u; br_audio_partial_drop_count = 2u;
    br_audio_late_last_index = 7u; br_audio_late_last_count = 8u;
    br_audio_late_last_deadline = 1000u; br_audio_late_last_return = 999u;
    br_audio_late_last_sequence = 42u; br_audio_late_last_cutoff = 1001u; br_audio_late_last_core = 1u;
    {
        /* Schema 4: words 17..21 are the worker-fault words (zero on the host). */
        static const uint32_t expect[22] = {4u, 4u, 7u, 8u, 1000u, 999u, 42u, 0u, 0u, 0u, 0u, 1001u, 1u, 0u, 0u, 0u, 2u};
        assert(fm1_app_command(0xb2u, 0, 0u, reply, &n) && n == 88u);
        for (i = 0u; i < 22u; ++i) assert(word_at(reply + 4u * i) == expect[i]);
    }
    assert(!fm1_app_command(0xb2u, body, 1u, reply, &n));
    /* 0x61 also clears the engine's late-drop words. */
    assert(fm1_app_command(0x61u, body, 4u, reply, &n));
    assert(fm1_app_command(0xb2u, 0, 0u, reply, &n) && word_at(reply) == 4u);
    for (i = 1u; i < 22u; ++i) assert(!word_at(reply + 4u * i));

    /* Development-only reads stay development-only. */
    assert(fm1_app_command(0xb3u, 0, 0u, reply, &n) == FM1_DEVELOPMENT_DIAGNOSTICS);
    printf("PASS: overrun counter protocol (%s)\n", FM1_DEVELOPMENT_DIAGNOSTICS ? "development" : "distribution");
    return 0;
}
#else
#error "build with OVERRUN_PLATFORM or OVERRUN_PROTOCOL"
#endif
