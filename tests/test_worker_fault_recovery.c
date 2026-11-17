/* SPDX-License-Identifier: GPL-3.0-only
 * A contained CPU1 fault must not leave the instrument silent and unsaveable.
 *
 * Compiles the actual worker HAL functions (extracted from
 * firmware/hal/fm1_cpu1_worker_target.c by run_worker_fault_recovery.py) against
 * a scripted T4 timer, a simulated CPU1 that runs the real mailbox step
 * (fm1_cpu1_worker_step), and a fake reset-control register. Then drives the
 * real project park protocol (firmware/src/pattern_park_io.c) with the same
 * operations dinghy.c registers for SAVE. Control flow only, not silicon timing.
 *
 * Reproduces the SMK-37 incident of 14 Nov 2026: CPU1's streamed master FX
 * needs more than 18,415 ticks after CPU0's mix (the global ladder runs from
 * flash), CPU0's fixed wait expires, CPU1 is held in reset (a contained fault)
 * and, before this fix, the project fence refused every SAVE for good. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FM1_PATTERN_TARGET_EXPERIMENT 1
#define FM1_FAULT_C1_TIMEOUT_SIDECAR 0
#include "fm1_cpu1_worker.h"
#include "fm1_cpu1_worker_ownership.h"
#include "fm1_audio_rate.h"
#include "audio_deadline.h"
#include "pattern_park_io.h"

#define FM1_WORKER_TRACE_SET(field, value) ((void)0)
#define FM1_WORKER_TRACE_INC(field) ((void)0)
fm1_cpu1_worker_mailbox_t fm1_cpu1_worker_mailbox;
volatile uint32_t fm1_cpu1_worker_fault, fm1_cpu1_worker_uncontained_fault;
static volatile uint32_t worker_project_fence, worker_project_resume_authorized;

/* ---- simulated hardware: T4, CPU1, reset control */
static uint32_t now, c1_held, reset_fails, c1_job_ticks, c1_job_started, c1_dispatches;
#define C1_HUNG 0xffffffffu
static uint32_t fm1_cpu1_worker_hw_reset_held(void) { return c1_held; }
static uint32_t fm1_cpu1_worker_hw_hold_reset(void) { if (!reset_fails) c1_held = 1u; return c1_held; }
static uint32_t fm1_cpu1_worker_irq_save(void) { return 0u; }
static void fm1_cpu1_worker_irq_restore(uint32_t s) { (void)s; }
static int worker_context_is_internal(const void *p) { return p != 0; }
static uint32_t dispatch(void *unused, uint32_t id, void *context)
{ (void)unused; (void)id; (void)context; ++c1_dispatches; return 1u; }
/* CPU1 runs its loop between CPU0's timer reads. A job finishes c1_job_ticks
 * after CPU1 first sees it (C1_HUNG: never); a held CPU1 runs nothing. */
static void c1_run(void)
{
    uint32_t loops = 0u;
    if (c1_held || fm1_cpu1_worker_mailbox.cpu_id != 1u) return;
    if (fm1_cpu1_worker_mailbox.request_seq != fm1_cpu1_worker_mailbox.done_seq &&
        !fm1_cpu1_worker_mailbox.park_request) {
        if (!c1_job_started) { c1_job_started = now ? now : 1u; return; }
        if (c1_job_ticks == C1_HUNG || now - c1_job_started < c1_job_ticks) return;
    }
    fm1_cpu1_worker_step(&fm1_cpu1_worker_mailbox, dispatch, 0, &loops);
    if (fm1_cpu1_worker_mailbox.request_seq == fm1_cpu1_worker_mailbox.done_seq) c1_job_started = 0u;
}
static uint32_t fm1_ticks(void) { now += 50u; c1_run(); return now; }

#include "worker_fault_extracted.inc"

static void boot(void)
{
    memset(&fm1_cpu1_worker_mailbox, 0, sizeof fm1_cpu1_worker_mailbox);
    fm1_cpu1_worker_fault = fm1_cpu1_worker_uncontained_fault = 0u;
    worker_project_fence = worker_project_resume_authorized = 0u;
    now = 1000u; c1_held = reset_fails = c1_job_started = c1_dispatches = 0u;
    c1_job_ticks = 0u;
    fm1_cpu1_worker_mailbox.cpu_id = 1u;                 /* fm1_cpu1_worker_main */
    fm1_cpu1_worker_mailbox.state = FM1_CPU1_WORKER_READY;
}

/* ---- the SAVE ownership protocol, with dinghy.c's operations */
static int op_fence(void *c) { (void)c; return fm1_cpu1_worker_project_fence_acquire(); }
static int op_unfence(void *c) { (void)c; return fm1_cpu1_worker_project_fence_release(); }
static int op_park(void *c)
{ (void)c; return fm1_cpu1_worker_park(FM1_CPU1_WORKER_TIMEOUT_TICKS) == FM1_CPU1_WORKER_OK ? 0 : -1; }
static int op_parked(void *c) { (void)c; return (int)fm1_cpu1_worker_project_parked(); }
static uint8_t flash[8192];
static int op_read(void *c, uint32_t off, void *dst, uint32_t n)
{ (void)c; if (!op_parked(0)) return -1; memcpy(dst, flash + off, n); return 0; }
static int op_program(void *c, uint32_t off, const void *src, uint32_t n)
{ (void)c; if (!op_parked(0)) return -1; memcpy(flash + off, src, n); return 0; }
static int op_erase(void *c, uint32_t off) { (void)c; memset(flash + off, 0xff, 4096u); return 0; }
static int op_may_erase(void *c, uint32_t off) { (void)c; (void)off; return 1; }
static int op_revoke(void *c) { (void)c; return 0; }
static int op_resume(void *c)
{
    (void)c;
    if (fm1_cpu1_worker_project_authorize_resume()) return -1;
    return fm1_cpu1_worker_project_resume(FM1_CPU1_WORKER_TIMEOUT_TICKS) == FM1_CPU1_WORKER_OK ? 0 : -1;
}
static void op_hold(void *c) { (void)c; (void)fm1_cpu1_worker_abort_hold_reset(); }
static int op_contained(void *c) { (void)c; return fm1_cpu1_worker_reset_held() == 1u; }
static const br_pattern_park_ops ops = {
    0, op_fence, op_unfence, op_park, op_parked, op_read, op_program, op_erase,
    op_may_erase, op_revoke, op_resume, op_hold, op_contained
};
static br_pattern_park_io io;

/* One project save: park, write and read back a page, end (resume + unfence). */
static int save_once(uint8_t value)
{
    uint8_t page[256], back[256];
    if (br_pattern_park_begin(&io, &ops, 0u, 8192u) != BR_PARK_OK) return 0;
    memset(page, value, sizeof page);
    if (br_pattern_park_program(&io, 4096u, page, sizeof page) != BR_PARK_OK) return 0;
    if (br_pattern_park_read(&io, 4096u, back, sizeof back) != BR_PARK_OK) return 0;
    if (memcmp(page, back, sizeof page)) return 0;
    return br_pattern_park_end(&io) == BR_PARK_OK;
}

/* CPU0's master-stream join, as br_fx_finish_block does it: submit at mix
 * start, mix for mix_ticks, then wait the given budget. */
static uint32_t stream_half(uint32_t budget, uint32_t c1_needs)
{
    uint32_t seq = 0u, result = 0u, rc;
    c1_job_ticks = c1_needs;
    if (!fm1_cpu1_worker_available()) return 0xffu;
    assert(worker_submit_fixed(FM1_CPU1_WORKER_TASK_AUDIO_FX_BATCH, &io, &seq) == FM1_CPU1_WORKER_OK);
    rc = fm1_cpu1_worker_wait(seq, budget, &result);
    return rc == FM1_CPU1_WORKER_OK && result == 1u ? 1u : 0u;
}

int main(void)
{
    /* 1. Healthy CPU1: the ordinary park protocol, unchanged. */
    boot();
    assert(!fm1_cpu1_worker_contained());
    assert(save_once(0x11));
    assert(fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY && !worker_project_fence);
    assert(stream_half(BR_AUDIO_STREAM_WAIT_MIN_TICKS, 5000u) == 1u);

    /* 2. The incident. CPU0's mix ends 95,000 ticks into the half; CPU1 still
     * needs 20,000 ticks of master FX. The fixed 18,415-tick wait (old code) takes
     * it for a hang and holds CPU1 in reset: a contained fault. */
    boot();
    {
        uint32_t half_start = now, deadline = half_start + BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS;
        now = half_start + 95000u;
        assert(stream_half(BR_AUDIO_STREAM_WAIT_MIN_TICKS, 20000u) == 0u);
        assert(fm1_cpu1_worker_fault && c1_held && !fm1_cpu1_worker_uncontained_fault);
        assert(fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_ABORTED);
        /* Fix B: the same half with the budget until the source deadline. */
        boot();
        half_start = now; deadline = half_start + BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS;
        now = half_start + 95000u;
        assert(br_audio_stream_wait_ticks(1u, deadline, now) == 35103u);
        assert(stream_half(br_audio_stream_wait_ticks(1u, deadline, now), 20000u) == 1u);
        assert(!fm1_cpu1_worker_fault && now - half_start < BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS);
        /* The minimum holds when the deadline is near, past or not this half's. */
        assert(br_audio_stream_wait_ticks(1u, deadline, deadline - 100u) == BR_AUDIO_STREAM_WAIT_MIN_TICKS);
        assert(br_audio_stream_wait_ticks(1u, deadline, deadline + 100u) == BR_AUDIO_STREAM_WAIT_MIN_TICKS);
        assert(br_audio_stream_wait_ticks(0u, deadline, half_start) == BR_AUDIO_STREAM_WAIT_MIN_TICKS);
        assert(br_audio_stream_wait_ticks(1u, half_start + 3u * FM1_AUDIO_HALF_TICKS, half_start) ==
               BR_AUDIO_STREAM_WAIT_MIN_TICKS);
        /* A really hung CPU1 still faults at the deadline: Fix A takes over. */
        boot();
        half_start = now; deadline = half_start + BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS;
        now = half_start + 95000u;
        assert(stream_half(br_audio_stream_wait_ticks(1u, deadline, now), C1_HUNG) == 0u);
        assert((int32_t)(now - deadline) >= 0 && (int32_t)(now - deadline) < 1000);
    }

    /* 3. Fix A, after the contained fault: CPU1 is held, nothing is submitted
     * (CPU0 renders alone), and SAVE works, repeatedly, without ever releasing
     * CPU1 from reset. Before the fix fence_acquire returned -1 here: the park
     * protocol failed, stayed FAILED, and SAVE was refused until power-off. */
    assert(fm1_cpu1_worker_contained());
    assert(!fm1_cpu1_worker_available());
    {
        uint32_t seq = 0u;
        assert(worker_submit_fixed(FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH, &io, &seq) ==
               FM1_CPU1_WORKER_NOT_READY);
    }
    assert(stream_half(BR_AUDIO_STREAM_WAIT_MIN_TICKS, 0u) == 0xffu);   /* master FX stays on CPU0 */
    c1_dispatches = 0u;
    assert(save_once(0x22) && save_once(0x33));
    assert(!worker_project_fence && c1_held && c1_dispatches == 0u);
    assert(fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_ABORTED);
    /* The poly switch path (dinghy.c br_poly_foreground_transition) uses the same
     * calls, then always ends its transaction when CPU1 is contained. */
    assert(fm1_cpu1_worker_project_fence_acquire() == 0);
    assert(fm1_cpu1_worker_park(FM1_CPU1_WORKER_TIMEOUT_TICKS) == FM1_CPU1_WORKER_OK);
    assert(fm1_cpu1_worker_project_parked());
    assert(!fm1_cpu1_worker_project_authorize_resume());
    assert(fm1_cpu1_worker_project_resume(FM1_CPU1_WORKER_TIMEOUT_TICKS) == FM1_CPU1_WORKER_OK);
    assert(!fm1_cpu1_worker_project_fence_release() && !worker_project_fence && c1_held);

    /* 4. Half-way through an abort (fault set, reset not yet confirmed) is not
     * contained: audio stays silent for that half, nothing is fenced. */
    boot();
    fm1_cpu1_worker_fault = 1u;
    assert(!fm1_cpu1_worker_contained() && fm1_cpu1_worker_project_fence_acquire() == -1);
    assert(!fm1_cpu1_worker_uncontained_fault);    /* contained() leaves no sticky mark */

    /* 5. Uncontained (reset not confirmed): every protection stays. CPU1 may
     * still own memory, so no fence, no park, no SAVE, no CPU0-only audio. */
    boot();
    reset_fails = 1u;
    assert(stream_half(BR_AUDIO_STREAM_WAIT_MIN_TICKS, C1_HUNG) == 0u);
    assert(fm1_cpu1_worker_fault && fm1_cpu1_worker_uncontained_fault && !c1_held);
    assert(!fm1_cpu1_worker_contained());
    assert(fm1_cpu1_worker_project_fence_acquire() == -1 && !worker_project_fence);
    assert(!fm1_cpu1_worker_project_parked());
    memset(&io, 0, sizeof io);
    assert(!save_once(0x44));
    assert(br_pattern_park_end(&io) == BR_PARK_IO);   /* fail closed, loans kept */

    puts("PASS: contained CPU1 fault: CPU0-only, SAVE works; stream wait to the deadline; uncontained stays locked");
    return 0;
}
