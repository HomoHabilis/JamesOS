/* SPDX-License-Identifier: GPL-3.0-only
 * Scripted T4 and mailbox test of the extracted target wait implementations.
 * This verifies control flow and wrap arithmetic, not silicon bus timing. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cpu1_worker.h"
#include "fm1_audio_rate.h"

#define FM1_CPU1_WORKER_ABSOLUTE_WAIT_SPINS 8u
fm1_cpu1_worker_mailbox_t fm1_cpu1_worker_mailbox;
volatile fm1_cpu1_worker_diag_t fm1_cpu1_worker_diag;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
#define FM1_WORKER_TRACE_SET(field, value) (fm1_cpu1_worker_diag.field = (value))
#define FM1_WORKER_TRACE_INC(field) (++fm1_cpu1_worker_diag.field)
#else
#define FM1_WORKER_TRACE_SET(field, value) ((void)0)
#define FM1_WORKER_TRACE_INC(field) ((void)0)
#endif
volatile uint32_t fm1_cpu1_worker_fault, fm1_cpu1_worker_uncontained_fault;

static uint32_t times[32], time_count, time_index, aborts, reset_fails;
static uint32_t completed_at_call, completed_sequence;
static uint32_t fm1_ticks(void)
{
    uint32_t index = time_index++;
    if (completed_at_call && time_index == completed_at_call)
        fm1_cpu1_worker_mailbox.done_seq = completed_sequence;
    return times[index < time_count ? index : time_count - 1u];
}

uint32_t fm1_cpu1_worker_abort_hold_reset(void)
{
    ++aborts;
    fm1_cpu1_worker_fault = 1u;
    if (reset_fails) {
        fm1_cpu1_worker_uncontained_fault = 1u;
        return FM1_CPU1_WORKER_RESTORE_FAILED;
    }
    return FM1_CPU1_WORKER_OK;
}

#include "worker_wait_extracted.inc"

static void reset(const uint32_t *script, uint32_t count)
{
    memset(&fm1_cpu1_worker_mailbox, 0, sizeof fm1_cpu1_worker_mailbox);
    memset((void *)&fm1_cpu1_worker_diag, 0, sizeof fm1_cpu1_worker_diag);
    assert(count && count <= sizeof times / sizeof times[0]);
    memcpy(times, script, count * sizeof times[0]);
    time_count = count;
    time_index = aborts = reset_fails = completed_at_call = completed_sequence = 0u;
    fm1_cpu1_worker_fault = fm1_cpu1_worker_uncontained_fault = 0u;
    fm1_cpu1_worker_mailbox.task_id = FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH;
    fm1_cpu1_worker_mailbox.task_context = 0x1234u;
    fm1_cpu1_worker_mailbox.result = 2u;
}

int main(void)
{
    uint32_t result;
    const uint32_t early[] = {10u, 11u, 12u};
    reset(early, 3u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_poll_until(7u, 20u, &result) == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts && !fm1_cpu1_worker_diag.wait_timeout_count);
    assert(!fm1_cpu1_worker_diag.last_wait_sequence); /* poll is silent */

    /* Exactly one nominal half is a valid future absolute budget. */
    reset(early, 3u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_poll_until(7u, 10u + FM1_AUDIO_HALF_TICKS, &result)
           == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts);

    const uint32_t exact[] = {20u, 20u};
    reset(exact, 2u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_poll_until(7u, 20u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(result == 0xfeedu && !aborts && !fm1_cpu1_worker_fault);
    assert(!fm1_cpu1_worker_diag.wait_timeout_count);

    /* Running C1 may span several polls before an ordinary soft expiration. */
    const uint32_t waiting[] = {10u, 11u, 12u, 13u, 20u};
    reset(waiting, 5u);
    result = 0xfeedu;
    assert(fm1_cpu1_worker_poll_until(7u, 20u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(result == 0xfeedu && !aborts && !fm1_cpu1_worker_fault);
    assert(!fm1_cpu1_worker_diag.wait_timeout_count);

    /* A completion that crosses the cutoff during the barrier is rejected. */
    const uint32_t crossing[] = {18u, 19u, 20u, 20u};
    reset(crossing, 4u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_poll_until(7u, 20u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(result == 0xfeedu && !aborts);

    /* A successful hard wait publishes the same last-wait fields as wait(). */
    const uint32_t wrap[] = {0xfffffff8u, 0xfffffffeu, 2u, 3u};
    reset(wrap, 4u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_wait_until(7u, 8u, &result) == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts && !fm1_cpu1_worker_diag.wait_timeout_count);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_status == FM1_CPU1_WORKER_OK);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_task == FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_sequence == 7u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_context == 0x1234u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_elapsed_ticks == 10u);
#endif

    reset(crossing, 4u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_wait_until(7u, 20u, &result) == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts && !fm1_cpu1_worker_fault);
    assert(!fm1_cpu1_worker_diag.wait_timeout_count);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_status == FM1_CPU1_WORKER_OK);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_sequence == 7u);
#endif

    /* Already-completed ownership is accepted at or after the hard cutoff. */
    reset(exact, 2u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_wait_until(7u, 20u, &result) == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts);
    const uint32_t after[] = {21u, 21u, 21u};
    reset(after, 3u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_wait_until(7u, 20u, &result) == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts);

    /* No completion visible at an expired cutoff causes immediate reset. */
    reset(after, 3u);
    result = 0xfeedu;
    assert(fm1_cpu1_worker_wait_until(7u, 20u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(result == 0xfeedu && aborts == 1u && fm1_cpu1_worker_fault);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.wait_timeout_count == 1u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_status == FM1_CPU1_WORKER_TIMEOUT);
#endif

    /* A newly published completion before the hard deadline may be joined. */
    const uint32_t arriving[] = {10u, 11u, 12u, 13u, 14u};
    reset(arriving, 5u);
    completed_at_call = 3u;
    completed_sequence = 7u;
    result = 0xfeedu;
    assert(fm1_cpu1_worker_wait_until(7u, 20u, &result) == FM1_CPU1_WORKER_OK);
    assert(result == 2u && !aborts);

    /* Stalled T4 must not leave either polling phase unbounded. */
    const uint32_t stopped[] = {100u};
    reset(stopped, 1u);
    result = 0xfeedu;
    assert(fm1_cpu1_worker_poll_until(7u, 200u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(result == 0xfeedu && aborts == 1u && fm1_cpu1_worker_fault);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.wait_timeout_count == 1u);
#endif
    reset(stopped, 1u);
    assert(fm1_cpu1_worker_wait_until(7u, 200u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(result == 0xfeedu && aborts == 1u && fm1_cpu1_worker_fault);

    /* Terminal preflight records once without polling time/completion or
     * resetting; even a newly published result is not consumed here. */
    reset(early, 3u);
    fm1_cpu1_worker_mailbox.done_seq = 7u;
    result = 0xfeedu;
    fm1_cpu1_worker_note_terminal_timeout(7u);
    assert(!time_index && !aborts && !fm1_cpu1_worker_fault);
    assert(fm1_cpu1_worker_mailbox.done_seq == 7u && result == 0xfeedu);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.wait_timeout_count == 1u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_status == FM1_CPU1_WORKER_TIMEOUT);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_task == FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_sequence == 7u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_context == 0x1234u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_elapsed_ticks == UINT32_MAX);
#endif
    reset(early, 3u);
    fm1_cpu1_worker_mailbox.done_seq = 6u;
    fm1_cpu1_worker_note_terminal_timeout(7u);
    fm1_cpu1_worker_mailbox.done_seq = 7u; /* completion after terminal */
    assert(!time_index && !aborts && !fm1_cpu1_worker_fault);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.wait_timeout_count == 1u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_status == FM1_CPU1_WORKER_TIMEOUT);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_sequence == 7u);
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_cpu1_worker_diag.last_wait_elapsed_ticks == UINT32_MAX);
#endif

    /* A failed hold/reset confirmation remains uncontained and sticky. */
    reset(after, 3u);
    reset_fails = 1u;
    assert(fm1_cpu1_worker_wait_until(7u, 20u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(aborts == 1u && fm1_cpu1_worker_fault && fm1_cpu1_worker_uncontained_fault);

    /* Invalid pointers/sequences follow wait(); an unsafe budget fails closed. */
    reset(early, 3u);
    assert(fm1_cpu1_worker_wait_until(0u, 20u, &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(fm1_cpu1_worker_wait_until(7u, 20u, 0) == FM1_CPU1_WORKER_TIMEOUT);
    assert(!aborts && !fm1_cpu1_worker_diag.wait_timeout_count);
    assert(fm1_cpu1_worker_wait_until(7u, 10u + FM1_AUDIO_HALF_TICKS + 1u,
                                      &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(aborts == 1u && fm1_cpu1_worker_fault && fm1_cpu1_worker_diag.wait_timeout_count == FM1_DEVELOPMENT_DIAGNOSTICS);
    reset(early, 3u);
    assert(fm1_cpu1_worker_poll_until(7u, 10u + FM1_AUDIO_HALF_TICKS + 1u,
                                      &result) == FM1_CPU1_WORKER_TIMEOUT);
    assert(aborts == 1u && fm1_cpu1_worker_fault && fm1_cpu1_worker_diag.wait_timeout_count == FM1_DEVELOPMENT_DIAGNOSTICS);
    puts("PASS: absolute soft/hard waits, deadline races, wrap, stuck clock and reset semantics");
    return 0;
}
