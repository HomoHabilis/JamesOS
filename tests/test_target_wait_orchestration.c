/* SPDX-License-Identifier: GPL-3.0-only
 * Target-source wait selector and classification, extracted by the runner.
 * Queue-graph validators and HAL timing have separate real-source tests. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cpu1_worker.h"
#include "fm1_audio_rate.h"

#define BR_PARTS 4u
#ifndef TEST_BLOCK
#define TEST_BLOCK FM1_AUDIO_HALF_FRAMES
#endif
#define BR_BLOCK TEST_BLOCK
#define BR_AUDIO_FUSION_FIXED_PLAN 1
#define BR_AUDIO_C1_LATE_OWNER_KNOWN owner_known
#define BR_AUDIO_FUSION_ONLY_BUILD fusion_only
#define BR_AUDIO_FUSION_FOUR_WAY_KIT four_way_kit
#define BR_AUDIO_C1_LATE_VALID 0x43314c54u
enum { BR_AUDIO_QUEUE_FAILED = 0u, BR_AUDIO_QUEUE_OK = 1u,
       BR_AUDIO_QUEUE_COMPLETE_LATE = 2u,
       BR_AUDIO_QUEUE_PUBLISHED_COMPLETE = 3u,
       BR_AUDIO_QUEUE_CUTOFF_DRAINED = 4u };
enum { BR_AUDIO_C1_PREFLIGHT_NONE = 0u, BR_AUDIO_C1_PREFLIGHT_CLAIMED = 1u,
       BR_AUDIO_C1_PREFLIGHT_COMPLETE = 2u, BR_AUDIO_C1_PREFLIGHT_TERMINAL = 3u,
       BR_AUDIO_C1_PREFLIGHT_INVALID = 4u };

typedef struct { uint32_t index, returned_tick, cutoff_tick; } br_audio_source_late_t;
typedef struct { volatile uint32_t valid, index, returned_tick, cutoff_tick, deadline, sequence; }
    late_record_t;
typedef struct {
    uint32_t lock, completed, count, fixed_plan_active, deadline_tick;
    uint32_t fixed_plan_count[2], fixed_plan_completed[2], fixed_plan_cursor[2];
} queue_t;
typedef struct { uint32_t state, executor_core; } job_t;
static queue_t br_audio_source_queue;
static job_t br_audio_source_jobs[8];
static late_record_t br_audio_c1_late_record;
fm1_cpu1_worker_mailbox_t fm1_cpu1_worker_mailbox;
volatile uint32_t fm1_cpu1_worker_fault, fm1_cpu1_worker_uncontained_fault;
static uint32_t br_audio_source_fx_fusion_active, br_pipeline_enabled;
static uint32_t br_audio_source_worker_fault, br_audio_source_legacy_mode;
static uint32_t owner_known, fusion_only, four_way_kit, frames;
static uint32_t shape_claimed, shape_complete, fusion_complete;
static uint32_t soft_status, wait_status, wait_result, hard_status, hard_result;
static uint32_t preflight, poll_sets_fault, poll_sets_uncontained;
static uint32_t fixed_seal_status, fixed_published_ready, c0_late_ack_ready;
static uint32_t poll_calls, wait_calls, hard_calls, preflight_calls, fault_path;
static uint32_t terminal_notes, terminal_publishes_done;
static uint32_t last_wait_ticks, last_hard_deadline, current_tick;
static uint32_t wait_bad_done, wait_bad_result, hard_bad_done, hard_bad_result;
static uint32_t br_audio_source_publication_enabled, joined_graph_ok, joined_completed;
static uint32_t graph_calls, seal_calls, partial_cancel_calls, after_branch;
static uint32_t br_quiet_part[BR_PARTS];
static uint32_t br_audio_late_last_index, br_audio_late_last_count;
static uint32_t br_audio_late_last_deadline, br_audio_late_last_return;
static uint32_t br_audio_late_last_sequence, br_audio_late_last_cutoff;
static uint32_t br_audio_late_last_core, br_audio_late_drop_count;
static uint32_t br_audio_partial_drop_count, br_audio_late_render;
static int32_t bus[BR_PARTS][BR_BLOCK];

static uint32_t br_engine_pipeline_uncontained_fault_get(void)
{ return fm1_cpu1_worker_uncontained_fault; }
static uint32_t br_audio_source_now(void) { return current_tick; }
static void br_audio_source_publish(void) { FM1_CPU1_WORKER_BARRIER(); }
static void br_audio_source_fusion_cancel(void)
{ ++partial_cancel_calls; br_audio_source_fx_fusion_active = 0u; }
static uint32_t br_audio_fixed_plan_seal_completed(void)
{ ++seal_calls; return fixed_seal_status; }
static uint32_t br_audio_source_joined_graph_valid(
    int32_t b[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const fm1_cpu1_worker_mailbox_t *mailbox,
    uint32_t sequence, uint32_t status, uint32_t result, uint32_t faults,
    uint32_t *completed_out);
/* This harness models the eight-job fused graph only; the split graph's wider
 * rules are covered against the real unit by test_split_fx_queue.c. */
static uint32_t br_audio_graph_count_ok(uint32_t count) { return count == 8u; }
static uint32_t br_audio_late_index_ok(uint32_t index, uint32_t count)
{ return count == 8u && index == 7u; }
static uint32_t br_audio_source_late_shape(int32_t b[BR_PARTS][BR_BLOCK],
    uint32_t n, uint32_t count, uint32_t deadline, uint32_t all_complete)
{
    (void)b; (void)n; (void)count; (void)deadline;
    return all_complete ? shape_complete : shape_claimed;
}
static uint32_t br_audio_call_fx_fusion_complete(int32_t b[BR_PARTS][BR_BLOCK], uint32_t n)
{ (void)b; (void)n; return fusion_complete; }
static uint32_t br_audio_source_c1_late_preflight(
    int32_t b[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count, uint32_t deadline,
    uint32_t hard_deadline, const volatile uint32_t *done_seq, uint32_t sequence)
{
    (void)b; (void)n; (void)count;
    assert(hard_deadline - deadline == FM1_AUDIO_HALF_TICKS);
    assert(done_seq == &fm1_cpu1_worker_mailbox.done_seq && sequence == 7u);
    ++preflight_calls;
    return preflight;
}
static uint32_t br_audio_source_fixed_published_ready(
    uint32_t n, uint32_t count, uint32_t deadline,
    const fm1_cpu1_worker_mailbox_t *mailbox, uint32_t sequence)
{
    (void)deadline;
    assert(n == BR_BLOCK && count == 8u &&
           mailbox == &fm1_cpu1_worker_mailbox && sequence == 7u);
    return fixed_published_ready;
}
static uint32_t br_audio_source_c0_late_ack_ready(
    int32_t b[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const fm1_cpu1_worker_mailbox_t *mailbox,
    uint32_t sequence, const br_audio_source_late_t *late)
{
    (void)b; (void)deadline;
    assert(n == BR_BLOCK && count == 8u && late &&
           mailbox == &fm1_cpu1_worker_mailbox && sequence == 7u);
    return c0_late_ack_ready;
}

uint32_t fm1_cpu1_worker_poll_until(uint32_t seq, uint32_t deadline, uint32_t *result)
{
    (void)seq; (void)deadline; ++poll_calls;
    if (poll_sets_fault) fm1_cpu1_worker_fault = 1u;
    if (poll_sets_uncontained) fm1_cpu1_worker_uncontained_fault = 1u;
    if (soft_status == FM1_CPU1_WORKER_OK) *result = wait_result;
    return soft_status;
}
uint32_t fm1_cpu1_worker_wait(uint32_t seq, uint32_t ticks, uint32_t *result)
{
    ++wait_calls; last_wait_ticks = ticks;
    if (wait_status == FM1_CPU1_WORKER_OK) {
        fm1_cpu1_worker_mailbox.done_seq = seq - wait_bad_done;
        fm1_cpu1_worker_mailbox.result = wait_result ^ wait_bad_result;
        *result = wait_result;
    }
    return wait_status;
}
uint32_t fm1_cpu1_worker_wait_until(uint32_t seq, uint32_t deadline, uint32_t *result)
{
    ++hard_calls; last_hard_deadline = deadline;
    if (hard_status == FM1_CPU1_WORKER_OK) {
        fm1_cpu1_worker_mailbox.done_seq = seq - hard_bad_done;
        fm1_cpu1_worker_mailbox.result = hard_result ^ hard_bad_result;
        *result = hard_result;
    } else if (seq) fm1_cpu1_worker_fault = 1u;
    return hard_status;
}
void fm1_cpu1_worker_note_terminal_timeout(uint32_t seq)
{
    assert(seq == 7u);
    ++terminal_notes;
    /* Simulate C1 completing immediately after preflight has terminated. */
    if (terminal_publishes_done) fm1_cpu1_worker_mailbox.done_seq = seq;
}

#include "wait_branch_helpers.inc"

/* The graph validator itself has separate actual-source tests. Model its
 * joined identity, geometry and fault contract here, and inject its result to
 * exercise the extracted target orchestration, including the partial return. */
static uint32_t br_audio_source_joined_graph_valid(
    int32_t b[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const fm1_cpu1_worker_mailbox_t *mailbox,
    uint32_t sequence, uint32_t status, uint32_t result, uint32_t faults,
    uint32_t *completed_out)
{
    ++graph_calls;
    if (!joined_graph_ok || !completed_out || !br_audio_source_publication_enabled ||
        !owner_known || !fusion_only || !four_way_kit ||
        !br_audio_source_fx_fusion_active || br_pipeline_enabled ||
        b != bus || n != BR_BLOCK || BR_BLOCK != FM1_AUDIO_HALF_FRAMES ||
        count != 8u || br_audio_source_queue.deadline_tick != deadline ||
        br_audio_source_queue.lock ||
        !br_audio_source_late_join_valid(mailbox, sequence, status, result,
                                         result, faults)) return 0u;
    *completed_out = joined_completed;
    return 1u;
}

typedef struct {
    uint32_t cpu0_ok, worker_status, worker_result, c1_extended;
    uint32_t late_accepted, complete_watch, late_sequence, late_executor;
    uint32_t exited_partial;
} outcome_t;

static outcome_t run(uint32_t initial_cpu0_ok, uint32_t source_job_count,
                     uint32_t sequence, uint32_t late_deadline)
{
    uint32_t cpu0_ok = initial_cpu0_ok, worker_status = FM1_CPU1_WORKER_NOT_READY;
    uint32_t worker_result = 0u, c1_extended = 0u, complete_watch = 0u;
    uint32_t late_sequence = 0u, late_executor = UINT32_MAX, late_accepted = 0u;
    uint32_t fixed_complete = fixed_seal_status, join_timeout = 91u, n = frames;
    br_audio_source_late_t late_job = {7u, late_deadline, late_deadline};
#include "wait_branch_body.inc"
    ++after_branch;
    return (outcome_t){cpu0_ok, worker_status, worker_result, c1_extended,
                       late_accepted, complete_watch, late_sequence, late_executor, 0u};
partial_exit:
    return (outcome_t){cpu0_ok, worker_status, worker_result, c1_extended,
                       late_accepted, complete_watch, late_sequence, late_executor, 1u};
}

static void reset(uint32_t deadline)
{
    memset(&br_audio_source_queue, 0, sizeof br_audio_source_queue);
    memset(br_audio_source_jobs, 0, sizeof br_audio_source_jobs);
    memset(&fm1_cpu1_worker_mailbox, 0, sizeof fm1_cpu1_worker_mailbox);
    memset(&br_audio_c1_late_record, 0, sizeof br_audio_c1_late_record);
    br_audio_source_queue.count = 8u;
    br_audio_source_queue.completed = 8u;
    br_audio_source_queue.deadline_tick = deadline;
    br_audio_source_jobs[7].state = 2u;
    br_audio_source_jobs[7].executor_core = 1u;
    fm1_cpu1_worker_mailbox.cpu_id = 1u;
    fm1_cpu1_worker_mailbox.request_seq = 7u;
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    fm1_cpu1_worker_mailbox.task_id = FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH;
    fm1_cpu1_worker_mailbox.task_context = (uint32_t)(uintptr_t)&br_audio_source_queue;
    fm1_cpu1_worker_mailbox.result = 1u;
    fm1_cpu1_worker_fault = fm1_cpu1_worker_uncontained_fault = 0u;
    br_audio_source_worker_fault = br_pipeline_enabled = br_audio_source_legacy_mode = 0u;
    owner_known = br_audio_source_fx_fusion_active = 1u;
    fusion_only = four_way_kit = 1u;
    fixed_seal_status = fixed_published_ready = c0_late_ack_ready = 1u;
    frames = BR_BLOCK;
    shape_claimed = shape_complete = fusion_complete = 1u;
    soft_status = wait_status = hard_status = FM1_CPU1_WORKER_OK;
    wait_result = hard_result = 1u;
    preflight = BR_AUDIO_C1_PREFLIGHT_NONE;
    poll_sets_fault = poll_sets_uncontained = 0u;
    poll_calls = wait_calls = hard_calls = preflight_calls = fault_path = 0u;
    terminal_notes = terminal_publishes_done = 0u;
    last_wait_ticks = last_hard_deadline = 0u;
    wait_bad_done = wait_bad_result = hard_bad_done = hard_bad_result = 0u;
    br_audio_source_publication_enabled = 0u;
    joined_graph_ok = 1u; joined_completed = 8u;
    graph_calls = seal_calls = partial_cancel_calls = after_branch = 0u;
    br_audio_late_last_index = br_audio_late_last_count = 0u;
    br_audio_late_last_deadline = br_audio_late_last_return = 0u;
    br_audio_late_last_sequence = br_audio_late_last_cutoff = 0u;
    br_audio_late_last_core = br_audio_late_drop_count = 0u;
    br_audio_partial_drop_count = br_audio_late_render = 0u;
    for (uint32_t p = 0u; p < BR_PARTS; ++p) {
        br_quiet_part[p] = 1u;
        for (uint32_t i = 0u; i < BR_BLOCK; ++i)
            bus[p][i] = (int32_t)(1000u + p * BR_BLOCK + i);
    }
    current_tick = deadline + 5u;
    br_audio_c1_late_record.valid = BR_AUDIO_C1_LATE_VALID;
    br_audio_c1_late_record.sequence = 7u;
    br_audio_c1_late_record.deadline = deadline;
    br_audio_c1_late_record.index = 7u;
    br_audio_c1_late_record.returned_tick = deadline + 1u;
    br_audio_c1_late_record.cutoff_tick = deadline + 2u;
}

int main(void)
{
    const uint32_t deadline = 1000u;
    outcome_t o;
#if TEST_BLOCK != FM1_AUDIO_HALF_FRAMES
    /* Defensive control: even a wrongly enabled flag cannot turn a non-half
     * block into a joined full or partial release. */
    reset(deadline); br_audio_source_publication_enabled = 1u;
    joined_completed = 7u; br_audio_source_queue.completed = 7u;
    wait_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
    assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
    assert(!br_audio_partial_drop_count && !partial_cancel_calls);
    reset(deadline);
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!poll_calls && !hard_calls && wait_calls == 1u && last_wait_ticks == 91u);
    assert(!o.late_accepted && !o.complete_watch);
    reset(deadline);
    o = run(BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);
    reset(deadline); wait_result = BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);
    reset(deadline);
    o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);
    reset(deadline); wait_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);
    puts("PASS: non-half block cannot enter the extended wait path");
    return 0;
#else
    /* Ordinary 1/1 full completion before cutoff still arms the post-source
     * deadline watch after matching join and exact graph validation. */
    reset(deadline); br_audio_source_publication_enabled = 1u;
    current_tick = deadline - 1u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(o.complete_watch && !o.exited_partial && !fault_path);
    assert(graph_calls == 1u && wait_calls == 1u && !hard_calls && after_branch == 1u);

    /* 1494: the actual target selector accepts a joined full graph from any
     * eligible provisional status, but never trusts an invalid graph/result. */
    for (uint32_t fixed = 0u; fixed <= 1u; ++fixed) {
        const uint32_t statuses[] = {BR_AUDIO_QUEUE_OK,
            BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, BR_AUDIO_QUEUE_CUTOFF_DRAINED};
        for (uint32_t a = 0u; a < 3u; ++a) for (uint32_t b = 0u; b < 3u; ++b) {
            reset(deadline); br_audio_source_publication_enabled = 1u;
            br_audio_source_queue.fixed_plan_active = fixed;
            soft_status = FM1_CPU1_WORKER_TIMEOUT;
            hard_result = statuses[b];
            /* Fixed plans only need statuses the fixed runner can produce:
             * OK or DRAINED; result 3 is a dynamic publication status. */
            if (fixed && (statuses[a] == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                          statuses[b] == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE))
                continue;
            o = run(statuses[a], 8u, 7u, deadline);
            assert(o.complete_watch && !o.exited_partial && !fault_path);
            assert(o.cpu0_ok == BR_AUDIO_QUEUE_OK && o.worker_result == BR_AUDIO_QUEUE_OK);
            assert(hard_calls == 1u && !preflight_calls && graph_calls == 1u);
            assert(last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
            assert(seal_calls == fixed && after_branch == 1u && !partial_cancel_calls);
        }
    }

    /* A joined, genuinely expired partial graph requires a DRAINED status.
     * Run both fixed and dynamic selectors and each actor's drain order. The
     * real partial-drop helper must leave every bus canary unchanged and the
     * extracted render return must bypass the post-branch output sentinel. */
    for (uint32_t fixed = 0u; fixed <= 1u; ++fixed) {
        for (uint32_t drained = 0u; drained < 3u; ++drained) {
            int32_t before[BR_PARTS][BR_BLOCK];
            uint32_t cpu0 = drained == 1u ? BR_AUDIO_QUEUE_OK : BR_AUDIO_QUEUE_CUTOFF_DRAINED;
            uint32_t cpu1 = drained == 0u ? BR_AUDIO_QUEUE_OK : BR_AUDIO_QUEUE_CUTOFF_DRAINED;
            reset(deadline); br_audio_source_publication_enabled = 1u;
            br_audio_source_queue.fixed_plan_active = fixed;
            br_audio_source_queue.completed = fixed ? 0u : 7u;
            fixed_seal_status = 0u; joined_completed = 7u;
            if (fixed) {
                uint32_t missing = cpu0 == BR_AUDIO_QUEUE_OK ? 1u : 0u;
                br_audio_source_queue.fixed_plan_count[0] = 4u;
                br_audio_source_queue.fixed_plan_count[1] = 4u;
                br_audio_source_queue.fixed_plan_cursor[0] = missing == 0u ? 3u : 4u;
                br_audio_source_queue.fixed_plan_cursor[1] = missing == 1u ? 3u : 4u;
                br_audio_source_queue.fixed_plan_completed[0] = br_audio_source_queue.fixed_plan_cursor[0];
                br_audio_source_queue.fixed_plan_completed[1] = br_audio_source_queue.fixed_plan_cursor[1];
            }
            soft_status = FM1_CPU1_WORKER_TIMEOUT; hard_result = cpu1;
            memcpy(before, bus, sizeof before);
            o = run(cpu0, 8u, 7u, deadline);
            assert(o.exited_partial && !o.complete_watch && !fault_path);
            assert(graph_calls == 1u && hard_calls == 1u && !preflight_calls);
            assert(last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
            assert(seal_calls == fixed && !after_branch && partial_cancel_calls == 1u);
            assert(!br_audio_source_fx_fusion_active && !fm1_cpu1_worker_fault);
            assert(br_audio_late_render == 1u && br_audio_late_drop_count == 1u &&
                   br_audio_partial_drop_count == 1u);
            assert(br_audio_late_last_index == UINT32_MAX &&
                   br_audio_late_last_core == UINT32_MAX &&
                   br_audio_late_last_return == UINT32_MAX &&
                   br_audio_late_last_count == 8u &&
                   br_audio_late_last_deadline == deadline &&
                   br_audio_late_last_cutoff == current_tick &&
                   br_audio_late_last_sequence == 7u);
            for (uint32_t p = 0u; p < BR_PARTS; ++p) assert(!br_quiet_part[p]);
            assert(!memcmp(before, bus, sizeof before));
        }
    }

    /* No explicit drain, no observed expiry, an invalid graph, or a failure
     * result may be converted into a partial drop. */
    for (uint32_t bad = 0u; bad < 5u; ++bad) {
        reset(deadline); br_audio_source_publication_enabled = 1u;
        joined_completed = 7u; br_audio_source_queue.completed = 7u;
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        hard_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
        uint32_t cpu0 = BR_AUDIO_QUEUE_OK;
        if (bad == 0u) hard_result = BR_AUDIO_QUEUE_OK; /* no DRAINED */
        if (bad == 1u) { /* a fabricated drain before cutoff */
            soft_status = FM1_CPU1_WORKER_OK;
            wait_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
            current_tick = deadline - 1u;
        }
        if (bad == 2u) joined_graph_ok = 0u;
        if (bad == 3u) hard_result = BR_AUDIO_QUEUE_FAILED;
        if (bad == 4u) cpu0 = BR_AUDIO_QUEUE_FAILED;
        o = run(cpu0, 8u, 7u, deadline);
        assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
        assert(!br_audio_late_render && !br_audio_partial_drop_count && !partial_cancel_calls);
    }

    /* A complete-publication status asserts all eight jobs were already
     * complete; neither actor can legitimately report 3 if the immutable
     * joined graph is only 7/8, even when the other actor drained at cutoff. */
    for (uint32_t actor = 0u; actor < 2u; ++actor) {
        reset(deadline); br_audio_source_publication_enabled = 1u;
        joined_completed = 7u; br_audio_source_queue.completed = 7u;
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        uint32_t cpu0 = actor ? BR_AUDIO_QUEUE_CUTOFF_DRAINED :
                                BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
        hard_result = actor ? BR_AUDIO_QUEUE_PUBLISHED_COMPLETE :
                              BR_AUDIO_QUEUE_CUTOFF_DRAINED;
        o = run(cpu0, 8u, 7u, deadline);
        assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
        assert(!br_audio_partial_drop_count && !partial_cancel_calls);
    }

    /* A fixed actor can return OK only if its own cursor finished its
     * assigned prefix. A valid seven-job graph with OK on the short actor is
     * a contradictory status, even though the other actor reported DRAINED. */
    for (uint32_t ok_core = 0u; ok_core < 2u; ++ok_core) {
        reset(deadline); br_audio_source_publication_enabled = 1u;
        br_audio_source_queue.fixed_plan_active = 1u;
        br_audio_source_queue.completed = 0u;
        br_audio_source_queue.fixed_plan_count[0] = 4u;
        br_audio_source_queue.fixed_plan_count[1] = 4u;
        br_audio_source_queue.fixed_plan_cursor[0] = ok_core == 0u ? 3u : 4u;
        br_audio_source_queue.fixed_plan_cursor[1] = ok_core == 1u ? 3u : 4u;
        br_audio_source_queue.fixed_plan_completed[0] = br_audio_source_queue.fixed_plan_cursor[0];
        br_audio_source_queue.fixed_plan_completed[1] = br_audio_source_queue.fixed_plan_cursor[1];
        fixed_seal_status = 0u; joined_completed = 7u;
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        uint32_t cpu0 = ok_core == 0u ? BR_AUDIO_QUEUE_OK : BR_AUDIO_QUEUE_CUTOFF_DRAINED;
        hard_result = ok_core == 1u ? BR_AUDIO_QUEUE_OK : BR_AUDIO_QUEUE_CUTOFF_DRAINED;
        o = run(cpu0, 8u, 7u, deadline);
        assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
        assert(!br_audio_partial_drop_count && !partial_cancel_calls);
    }

    /* Fixed-plan actors never produce dynamic PUBLISHED_COMPLETE. A full
     * graph cannot legitimize that impossible status from either core. */
    for (uint32_t actor = 0u; actor < 2u; ++actor) {
        reset(deadline); br_audio_source_publication_enabled = 1u;
        br_audio_source_queue.fixed_plan_active = 1u;
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        uint32_t cpu0 = actor ? BR_AUDIO_QUEUE_OK : BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
        hard_result = actor ? BR_AUDIO_QUEUE_PUBLISHED_COMPLETE : BR_AUDIO_QUEUE_OK;
        o = run(cpu0, 8u, 7u, deadline);
        assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
        assert(!br_audio_partial_drop_count && !partial_cancel_calls);
    }

    /* Status 3/4 is produced only after the soft cutoff was observed. A
     * complete or partial graph paired with that status before cutoff is an
     * inconsistent result/clock state and must not be consumed. */
    for (uint32_t completed = 7u; completed <= 8u; ++completed) {
        for (uint32_t status = BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
             status <= BR_AUDIO_QUEUE_CUTOFF_DRAINED; ++status) {
            for (uint32_t actor = 0u; actor < 2u; ++actor) {
                reset(deadline); br_audio_source_publication_enabled = 1u;
                current_tick = deadline - 1u; joined_completed = completed;
                br_audio_source_queue.completed = completed;
                uint32_t cpu0 = actor ? BR_AUDIO_QUEUE_OK : status;
                wait_result = actor ? status : BR_AUDIO_QUEUE_OK;
                o = run(cpu0, 8u, 7u, deadline);
                assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
                assert(!br_audio_partial_drop_count && !partial_cancel_calls);
            }
        }
    }

    /* After a soft timeout, bad worker identity/faults and an unfinished hard
     * wait contain directly. They cannot be rescued by a graph stub or a
     * later completion. This applies to both fixed and dynamic paths. */
    for (uint32_t fixed = 0u; fixed <= 1u; ++fixed) {
        for (uint32_t bad = 0u; bad < 7u; ++bad) {
            reset(deadline); br_audio_source_publication_enabled = 1u;
            br_audio_source_queue.fixed_plan_active = fixed;
            soft_status = FM1_CPU1_WORKER_TIMEOUT;
            hard_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
            if (bad == 0u) fm1_cpu1_worker_mailbox.cpu_id = 0u;
            if (bad == 1u) fm1_cpu1_worker_mailbox.request_seq = 6u;
            if (bad == 2u) fm1_cpu1_worker_mailbox.task_id = FM1_CPU1_WORKER_TASK_SMOKE;
            if (bad == 3u) fm1_cpu1_worker_mailbox.task_context ^= 4u;
            if (bad == 4u) poll_sets_fault = 1u;
            if (bad == 5u) poll_sets_uncontained = 1u;
            if (bad == 6u) hard_status = FM1_CPU1_WORKER_TIMEOUT;
            o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
            assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
            assert(!preflight_calls && !wait_calls && !partial_cancel_calls);
            assert(hard_calls == (bad == 6u));
        }
    }

    /* Even a forged eligibility flag cannot make a partial frame, wrong job
     * count, bad queue lock or unsupported build/mode acceptable. */
    for (uint32_t bad = 0u; bad < 9u; ++bad) {
        reset(deadline); br_audio_source_publication_enabled = 1u;
        joined_completed = 7u; br_audio_source_queue.completed = 7u;
        wait_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
        uint32_t count = 8u;
        if (bad == 0u) frames = BR_BLOCK / 2u;
        if (bad == 1u) count = 7u;
        if (bad == 2u) br_audio_source_queue.lock = 1u;
        if (bad == 3u) owner_known = 0u;
        if (bad == 4u) fusion_only = 0u;
        if (bad == 5u) four_way_kit = 0u;
        if (bad == 6u) br_audio_source_fx_fusion_active = 0u;
        if (bad == 7u) br_pipeline_enabled = 1u;
        if (bad == 8u) br_audio_source_queue.deadline_tick = deadline + 1u;
        o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, count, 7u, deadline);
        assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
        assert(!br_audio_partial_drop_count && !partial_cancel_calls);
    }

    /* The mailbox sequence and result must still match after either wait;
     * a returned result alone cannot normalize a stale or changed mailbox. */
    for (uint32_t hard = 0u; hard <= 1u; ++hard) {
        for (uint32_t mismatch = 0u; mismatch < 2u; ++mismatch) {
            reset(deadline); br_audio_source_publication_enabled = 1u;
            if (hard) {
                soft_status = FM1_CPU1_WORKER_TIMEOUT;
                hard_bad_done = mismatch == 0u;
                hard_bad_result = mismatch == 1u;
            } else {
                wait_bad_done = mismatch == 0u;
                wait_bad_result = mismatch == 1u;
            }
            o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
            assert(!o.exited_partial && !o.complete_watch && fault_path == 1u);
            assert(graph_calls == 1u && !partial_cancel_calls);
            assert(hard_calls == hard && wait_calls == !hard);
        }
    }

    /* The selected hard cutoff uses modulo-32-bit addition across wrap. */
    reset(UINT32_MAX - 7000u); br_audio_source_publication_enabled = 1u;
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, UINT32_MAX - 7000u);
    assert(o.complete_watch && !fault_path && hard_calls == 1u && !preflight_calls);
    assert(last_hard_deadline == (uint32_t)(UINT32_MAX - 7000u + FM1_AUDIO_HALF_TICKS));

    reset(deadline);
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(o.worker_status == FM1_CPU1_WORKER_OK && o.complete_watch && !fault_path);
    assert(poll_calls == 1u && wait_calls == 1u && last_wait_ticks == 1u && !hard_calls);

    /* Either or both actors may report a provisional publication. Only the
     * matched mailbox, full frame and valid graph normalize it to success. */
    for (uint32_t cpu0 = 1u; cpu0 <= 3u; cpu0 += 2u) {
        for (uint32_t cpu1 = 1u; cpu1 <= 3u; cpu1 += 2u) {
            reset(deadline); wait_result = cpu1;
            o = run(cpu0, 8u, 7u, deadline);
            assert(o.complete_watch && !fault_path);
            assert(o.cpu0_ok == BR_AUDIO_QUEUE_OK && o.worker_result == BR_AUDIO_QUEUE_OK);
            assert(poll_calls == 1u && wait_calls == 1u);
        }
    }
    for (uint32_t invalid = 0u; invalid < 3u; ++invalid) {
        reset(deadline); wait_result = BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
        if (invalid == 0u) shape_complete = 0u;
        if (invalid == 1u) fusion_complete = 0u;
        if (invalid == 2u) br_audio_source_queue.lock = 1u;
        o = run(BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, 8u, 7u, deadline);
        assert(!o.complete_watch && fault_path == 1u);
    }
    reset(deadline); wait_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u); /* impossible 4/4 */
    for (uint32_t other = 1u; other <= 3u; other += 2u) {
        reset(deadline); wait_result = other;
        o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
        assert(o.complete_watch && !fault_path);
        reset(deadline); wait_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
        o = run(other, 8u, 7u, deadline);
        assert(o.complete_watch && !fault_path);
    }
    for (uint32_t typed = BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
         typed <= BR_AUDIO_QUEUE_CUTOFF_DRAINED; ++typed) {
        reset(deadline); frames = BR_BLOCK / 2u;
        o = run(typed, 8u, 7u, deadline);
        assert(!o.complete_watch && fault_path == 1u);
        reset(deadline); frames = BR_BLOCK / 2u; wait_result = typed;
        o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
        assert(!o.complete_watch && fault_path == 1u);
    }

    /* Exact 7/8 in-flight finalizer joins as typed result 2 and publishes once. */
    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    hard_result = BR_AUDIO_QUEUE_COMPLETE_LATE;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(o.late_accepted && !o.complete_watch && !fault_path && o.late_executor == 1u);
    assert(preflight_calls == 1u && hard_calls == 1u && !wait_calls);
    assert(last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
    assert(br_audio_source_queue.completed == 8u && br_audio_source_jobs[7].state == 2u);
    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    hard_result = BR_AUDIO_QUEUE_COMPLETE_LATE;
    o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
    assert(o.late_accepted && !fault_path && o.cpu0_ok == BR_AUDIO_QUEUE_OK);
    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    hard_result = BR_AUDIO_QUEUE_FAILED;
    o = run(BR_AUDIO_QUEUE_CUTOFF_DRAINED, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u);
    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    hard_result = BR_AUDIO_QUEUE_COMPLETE_LATE;
    o = run(BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u); /* contradictory 3/7-of-8 */

    /* Already-published 8/8 after soft cutoff needs no repair or reset. */
    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_COMPLETE;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(o.complete_watch && !o.late_accepted && !fault_path);
    assert(hard_calls == 1u && last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
    assert(br_audio_source_queue.completed == 8u && br_audio_source_jobs[7].state == 2u);

    /* Dynamic 8/8 is also allowed the same hard deadline while the mailbox
     * acknowledgement is in flight, for each valid C0 completion status. */
    {
        const uint32_t statuses[] = {BR_AUDIO_QUEUE_OK,
            BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, BR_AUDIO_QUEUE_CUTOFF_DRAINED};
        for (uint32_t i = 0u; i < 3u; ++i) {
            reset(deadline); soft_status = FM1_CPU1_WORKER_TIMEOUT;
            preflight = BR_AUDIO_C1_PREFLIGHT_COMPLETE;
            hard_result = BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
            o = run(statuses[i], 8u, 7u, deadline);
            assert(o.complete_watch && !fault_path && hard_calls == 1u &&
                   last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
        }
    }

    /* Fixed plans never get the dynamic 7/8 grace. Once both cores returned
     * successfully and the plan sealed, they do get the completion watch. */
    reset(deadline);
    br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_queue.fixed_plan_count[0] = 3u;
    br_audio_source_queue.fixed_plan_count[1] = 5u;
    br_audio_source_queue.fixed_plan_completed[0] = 3u;
    br_audio_source_queue.fixed_plan_completed[1] = 5u;
    br_audio_source_queue.fixed_plan_cursor[0] = 3u;
    br_audio_source_queue.fixed_plan_cursor[1] = 5u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(poll_calls == 1u && !hard_calls && wait_calls == 1u);
    assert(o.complete_watch && !fault_path);
    fixed_seal_status = 0u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);

    /* Only an already-published fixed plan can use the original hard cutoff
     * while its matching mailbox acknowledgement is still in flight. */
    reset(deadline); br_audio_source_queue.fixed_plan_active = 1u;
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(o.complete_watch && !fault_path && hard_calls == 1u &&
           last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
    reset(deadline); br_audio_source_queue.fixed_plan_active = 1u;
    soft_status = FM1_CPU1_WORKER_TIMEOUT; fixed_published_ready = 0u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u && !hard_calls);
    reset(deadline); br_audio_source_queue.fixed_plan_active = 1u;
    soft_status = FM1_CPU1_WORKER_TIMEOUT; hard_status = FM1_CPU1_WORKER_TIMEOUT;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u && hard_calls == 1u);
    reset(deadline); br_audio_source_queue.fixed_plan_active = 1u;
    soft_status = FM1_CPU1_WORKER_TIMEOUT; shape_complete = 0u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u && hard_calls == 1u);
    reset(deadline); br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_queue.fixed_plan_count[1] = 1u;
    br_audio_source_queue.fixed_plan_completed[1] = 1u;
    br_audio_source_queue.fixed_plan_cursor[1] = 0u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);
    reset(deadline); br_audio_source_queue.fixed_plan_active = 1u;
    wait_result = 0u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.complete_watch && fault_path == 1u);

    /* A stale sequence or inconsistent record may not publish or recover. */
    for (uint32_t which = 0u; which < 8u; ++which) {
        reset(deadline);
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
        br_audio_source_queue.completed = 7u;
        br_audio_source_jobs[7].state = 1u;
        hard_result = BR_AUDIO_QUEUE_COMPLETE_LATE;
        if (which == 0u) br_audio_c1_late_record.sequence = 6u;
        if (which == 1u) br_audio_c1_late_record.valid = 0u;
        if (which == 2u) br_audio_c1_late_record.deadline = deadline - 1u;
        if (which == 3u) br_audio_c1_late_record.cutoff_tick = current_tick + 1u;
        if (which == 4u) br_audio_source_jobs[7].executor_core = 0u;
        if (which == 5u) hard_result = 1u;
        if (which == 6u) br_audio_c1_late_record.index = 6u;
        if (which == 7u)
            br_audio_c1_late_record.returned_tick = deadline - FM1_AUDIO_HALF_TICKS - 1u;
        o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
        assert(!o.late_accepted && fault_path == 1u);
        assert(br_audio_source_queue.completed == 7u && br_audio_source_jobs[7].state == 1u);
    }

    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    hard_status = FM1_CPU1_WORKER_TIMEOUT;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u && fm1_cpu1_worker_fault);

    /* The inherited C0-late path remains a single accepted publication. */
    reset(deadline);
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    o = run(BR_AUDIO_QUEUE_COMPLETE_LATE, 8u, 7u, deadline);
    assert(o.late_accepted && !fault_path && !poll_calls && hard_calls == 1u &&
           last_hard_deadline == deadline + FM1_AUDIO_HALF_TICKS);
    assert(br_audio_source_queue.completed == 8u && br_audio_source_jobs[7].state == 2u);
    reset(deadline);
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    hard_result = BR_AUDIO_QUEUE_COMPLETE_LATE; /* C1 must return 1 or drained 4 */
    o = run(BR_AUDIO_QUEUE_COMPLETE_LATE, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u && br_audio_source_queue.completed == 7u);
    reset(deadline);
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    hard_result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    o = run(BR_AUDIO_QUEUE_COMPLETE_LATE, 8u, 7u, deadline);
    assert(o.late_accepted && !fault_path && o.worker_result == BR_AUDIO_QUEUE_OK);
    reset(deadline);
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 1u;
    o = run(BR_AUDIO_QUEUE_COMPLETE_LATE, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u);
    reset(deadline);
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    c0_late_ack_ready = 0u;
    o = run(BR_AUDIO_QUEUE_COMPLETE_LATE, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u && !hard_calls);
    reset(deadline);
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    hard_status = FM1_CPU1_WORKER_TIMEOUT;
    o = run(BR_AUDIO_QUEUE_COMPLETE_LATE, 8u, 7u, deadline);
    assert(!o.late_accepted && fault_path == 1u && hard_calls == 1u);

    /* A soft timer failure already caused containment: do not extend it. */
    reset(deadline);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    poll_sets_fault = 1u;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
    assert(o.worker_status == FM1_CPU1_WORKER_TIMEOUT && fault_path == 1u);
    assert(!preflight_calls && !hard_calls && !terminal_notes);

    /* No eligible finalizer, or invalid mailbox identity: no hard extension. */
    for (uint32_t which = 0u; which < 6u; ++which) {
        reset(deadline);
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        preflight = BR_AUDIO_C1_PREFLIGHT_NONE;
        hard_status = FM1_CPU1_WORKER_TIMEOUT;
        if (which == 1u) fm1_cpu1_worker_mailbox.cpu_id = 0u;
        if (which == 2u) fm1_cpu1_worker_mailbox.request_seq = 6u;
        if (which == 3u) fm1_cpu1_worker_mailbox.task_id = FM1_CPU1_WORKER_TASK_SMOKE;
        if (which == 4u) fm1_cpu1_worker_mailbox.task_context ^= 4u;
        if (which == 5u) br_audio_source_worker_fault = 1u;
        o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
        assert(!o.late_accepted && fault_path == 1u);
        assert(!hard_calls && !wait_calls && !terminal_notes);
        if (which) assert(!preflight_calls);
    }

    /* Terminal retries and malformed acquired graphs fail without another
     * wait, including when the mailbox may complete during the fault path. */
    for (uint32_t status = BR_AUDIO_C1_PREFLIGHT_TERMINAL;
         status <= BR_AUDIO_C1_PREFLIGHT_INVALID; ++status) {
        reset(deadline);
        soft_status = FM1_CPU1_WORKER_TIMEOUT;
        preflight = status;
        fm1_cpu1_worker_mailbox.done_seq = 6u;
        terminal_publishes_done = 1u;
        o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, deadline);
        assert(o.c1_extended == status && o.worker_status == FM1_CPU1_WORKER_TIMEOUT);
        assert(preflight_calls == 1u && !hard_calls && !wait_calls && fault_path == 1u);
        assert(!o.late_accepted && !o.complete_watch);
        assert(terminal_notes == (status == BR_AUDIO_C1_PREFLIGHT_TERMINAL));
        assert(fm1_cpu1_worker_mailbox.done_seq ==
               (status == BR_AUDIO_C1_PREFLIGHT_TERMINAL ? 7u : 6u));
    }

    /* Every excluded graph/configuration retains its original wait path. */
    for (uint32_t which = 0u; which < 9u; ++which) {
        reset(deadline);
        uint32_t count = 8u;
        if (which == 0u) owner_known = 0u;
        if (which == 1u) br_audio_source_fx_fusion_active = 0u;
        if (which == 2u) br_pipeline_enabled = 1u;
        if (which == 3u) br_audio_source_queue.fixed_plan_active = 1u;
        if (which == 4u) count = 7u;
        if (which == 6u) fusion_only = 0u;
        if (which == 7u) four_way_kit = 0u;
        if (which == 8u) frames = BR_BLOCK - 1u;
        o = run(which == 5u ? BR_AUDIO_QUEUE_FAILED : BR_AUDIO_QUEUE_OK,
                count, 7u, deadline);
        if (which == 3u)
            assert(poll_calls == 1u && !hard_calls && wait_calls == 1u);
        else
            assert(!poll_calls && !hard_calls && wait_calls == 1u && last_wait_ticks == 91u);
        assert(!o.late_accepted);
    }

    /* Addition of the one-half hard deadline is modulo 32-bit T4 wrap. */
    const uint32_t wrapped = UINT32_MAX - 7000u;
    reset(wrapped);
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    preflight = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].state = 1u;
    hard_result = BR_AUDIO_QUEUE_COMPLETE_LATE;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, wrapped);
    assert(o.late_accepted && !fault_path && last_hard_deadline == wrapped + FM1_AUDIO_HALF_TICKS);
    reset(wrapped); br_audio_source_queue.fixed_plan_active = 1u;
    soft_status = FM1_CPU1_WORKER_TIMEOUT;
    o = run(BR_AUDIO_QUEUE_OK, 8u, 7u, wrapped);
    assert(o.complete_watch && !fault_path && hard_calls == 1u &&
           last_hard_deadline == wrapped + FM1_AUDIO_HALF_TICKS);
    puts("PASS: extracted target wait/classification, partial return and negative controls");
    return 0;
#endif
}
