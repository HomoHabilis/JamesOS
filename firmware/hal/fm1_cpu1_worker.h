/* SPDX-License-Identifier: GPL-3.0-only
 * Private fixed-task CPU1 worker prototype. */
#pragma once
#include <stdint.h>
#include "fm1_cpu1_stack_march.h"

#ifndef FM1_CPU1_WORKER_COHERENT_STATUS_SNAPSHOT
#define FM1_CPU1_WORKER_COHERENT_STATUS_SNAPSHOT 1
#endif

typedef uint32_t (*fm1_cpu1_worker_task_fn)(void *context);

enum {
    FM1_CPU1_WORKER_OFF = 0u,
    FM1_CPU1_WORKER_READY = 1u,
    FM1_CPU1_WORKER_RUNNING = 2u,
    FM1_CPU1_WORKER_PARKED = 3u,
    FM1_CPU1_WORKER_BAD_CPU = 4u,
    FM1_CPU1_WORKER_ABORTED = 5u,
    FM1_CPU1_WORKER_OK = 0u,
    FM1_CPU1_WORKER_BUSY = 1u,
    FM1_CPU1_WORKER_TIMEOUT = 2u,
    FM1_CPU1_WORKER_BAD_TASK = 3u,
    FM1_CPU1_WORKER_BAD_CONTEXT = 4u,
    FM1_CPU1_WORKER_RESTORE_FAILED = 5u,
    FM1_CPU1_WORKER_NOT_READY = 6u,
    FM1_CPU1_WORKER_GUARD_REFUSED = 7u
};

#define FM1_CPU1_WORKER_TIMEOUT_TICKS (50u * 1000u * 24u)
#define FM1_CPU1_WORKER_TASK_SMOKE 1u
#define FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH 2u
#define FM1_CPU1_WORKER_TASK_AUDIO_FX_BATCH 3u
#define FM1_CPU1_WORKER_TASK_AUDIO_FX_SOURCE_BATCH 4u

#if (defined(BR_CPU1_AUDIO) && BR_CPU1_AUDIO) || defined(BR_SOURCE_QUEUE_TEST)
uint32_t br_audio_worker_source_batch(void *context);
uint32_t br_fx_worker_track_batch(void *context);
uint32_t br_fx_worker_hybrid_batch(void *context);

/* The target dispatcher and host worker stub share this exact fixed registry.
 * Mailbox context is data only; no task can supply a callback address. */
static inline __attribute__((always_inline)) uint32_t
fm1_cpu1_worker_dispatch_audio_task(uint32_t task_id, void *context)
{
    if (task_id == FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH) {
        uint32_t (* volatile task_fn)(void *) = br_audio_worker_source_batch;
        return task_fn(context);
    }
    if (task_id == FM1_CPU1_WORKER_TASK_AUDIO_FX_BATCH)
        return br_fx_worker_track_batch(context);
    if (task_id == FM1_CPU1_WORKER_TASK_AUDIO_FX_SOURCE_BATCH) {
        uint32_t (* volatile task_fn)(void *) = br_fx_worker_hybrid_batch;
        return task_fn(context);
    }
    return 0xffffffffu;
}
#endif

/* SPSC mailbox: CPU0 is the sole producer, CPU1 the sole consumer. Every
 * cross-core slot is naturally aligned and exactly one 32-bit word. */
typedef struct __attribute__((aligned(4))) {
    volatile uint32_t cpu_id;
    volatile uint32_t state;
    volatile uint32_t request_seq;
    volatile uint32_t done_seq;
    volatile uint32_t task_id;
    volatile uint32_t task_context;
    volatile uint32_t result;
    volatile uint32_t park_request;
    volatile uint32_t park_ack;
    volatile uint32_t heartbeat;
} fm1_cpu1_worker_mailbox_t;
extern fm1_cpu1_worker_mailbox_t fm1_cpu1_worker_mailbox;
extern volatile uint32_t fm1_cpu1_worker_fault;
extern volatile uint32_t fm1_cpu1_worker_uncontained_fault;
typedef struct {
    volatile uint32_t wait_timeout_count;
    volatile uint32_t last_job_status, last_job_task, last_job_context;
    volatile uint32_t last_job_elapsed_ticks, last_job_start_ticks;
    volatile uint32_t last_job_sequence, last_job_result;
    volatile uint32_t last_wait_status, last_wait_task, last_wait_sequence;
    volatile uint32_t last_wait_context, last_wait_elapsed_ticks;
} fm1_cpu1_worker_diag_t;
extern volatile fm1_cpu1_worker_diag_t fm1_cpu1_worker_diag;

typedef struct {
    volatile uint32_t integer_result;
    volatile uint32_t float_bits;
} fm1_cpu1_worker_smoke_result_t;

typedef uint32_t (*fm1_cpu1_worker_dispatch_fn)(void *dispatch_context,
                                                 uint32_t task_id, void *context);

#if defined(FM1_DEVICE)
#define FM1_CPU1_WORKER_BARRIER() __asm__ volatile("csync" ::: "memory")
#define FM1_CPU1_WORKER_NOP() __asm__ volatile("nop\n\tnop\n\tnop\n\tnop" ::: "memory")
#else
#define FM1_CPU1_WORKER_BARRIER() __sync_synchronize()
#define FM1_CPU1_WORKER_NOP() ((void)0)
#endif

static inline __attribute__((always_inline)) void
fm1_cpu1_worker_step(fm1_cpu1_worker_mailbox_t *m,
                     fm1_cpu1_worker_dispatch_fn dispatch,
                     void *dispatch_context,
                     uint32_t *idle_loops)
{
    uint32_t seq, done, id, context, result;
    if (m->park_request) {
        if (!m->park_ack) {
            FM1_CPU1_WORKER_BARRIER();
            m->park_ack = 1u;
            m->state = FM1_CPU1_WORKER_PARKED;
            FM1_CPU1_WORKER_BARRIER();
        }
        FM1_CPU1_WORKER_NOP();
        return;
    }
    if (m->park_ack) {
        m->park_ack = 0u;
        m->state = FM1_CPU1_WORKER_READY;
        FM1_CPU1_WORKER_BARRIER();
    }
    seq = m->request_seq;
    done = m->done_seq;
    if (seq != done) {
        FM1_CPU1_WORKER_BARRIER();
        id = m->task_id;
        context = m->task_context;
        m->state = FM1_CPU1_WORKER_RUNNING;
        FM1_CPU1_WORKER_BARRIER();
        result = dispatch(dispatch_context, id, (void *)(uintptr_t)context);
        m->result = result;
        FM1_CPU1_WORKER_BARRIER();
        m->done_seq = seq;
        m->state = FM1_CPU1_WORKER_READY;
        FM1_CPU1_WORKER_BARRIER();
    }
    if (++*idle_loops == 256u) {
        *idle_loops = 0u;
        ++m->heartbeat;
    }
    FM1_CPU1_WORKER_NOP();
}

/* Internal main-context API. `submit` accepts only compile-time registered
 * callbacks; no SysEx message can supply a function or context address. */
void fm1_cpu1_worker_init_early(void);
uint32_t fm1_cpu1_worker_available(void);
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* CPU0 foreground only: acquire returns 0 fenced, 1 busy, -1 fault.
 * The caller must independently gate the audio transaction. */
int fm1_cpu1_worker_project_fence_acquire(void);
int fm1_cpu1_worker_project_fence_release(void);
int fm1_cpu1_worker_project_authorize_resume(void);
uint32_t fm1_cpu1_worker_project_resume(uint32_t timeout_ticks);
uint32_t fm1_cpu1_worker_project_fence_held(void);
#endif
uint32_t fm1_cpu1_worker_wait_ready(uint32_t timeout_ticks);
uint32_t fm1_cpu1_worker_submit_smoke(uint32_t *sequence_out);
#if defined(BR_CPU1_AUDIO) && BR_CPU1_AUDIO
uint32_t fm1_cpu1_worker_submit_audio_batch(void *context, uint32_t *sequence_out);
uint32_t fm1_cpu1_worker_submit_fx_batch(void *context, uint32_t *sequence_out);
uint32_t fm1_cpu1_worker_submit_audio_hybrid(void *context, uint32_t *sequence_out);
#endif
uint32_t fm1_cpu1_worker_wait(uint32_t sequence, uint32_t timeout_ticks,
                              uint32_t *result_out);
/* Absolute T4 deadlines. The soft poll is silent and leaves C1 untouched if
 * the deadline expires; the hard wait records timeout and holds C1 in reset.
 * The soft poll stops at its deadline; the hard wait first accepts a matching
 * completion already visible when it checks expiry. Both reject a future
 * deadline farther than one nominal audio half. A stalled T4 trips an
 * independent finite spin guard and follows the hard-fault path. */
uint32_t fm1_cpu1_worker_poll_until(uint32_t sequence, uint32_t deadline_tick,
                                    uint32_t *result_out);
uint32_t fm1_cpu1_worker_wait_until(uint32_t sequence, uint32_t deadline_tick,
                                    uint32_t *result_out);
/* The caller has already observed terminal budget/spin exhaustion. Record it
 * without resampling completion or resetting C1; its common failure path
 * remains responsible for exactly one hold/reset attempt. */
void fm1_cpu1_worker_note_terminal_timeout(uint32_t sequence);
uint32_t fm1_cpu1_worker_park(uint32_t timeout_ticks);
uint32_t fm1_cpu1_worker_resume(uint32_t timeout_ticks);
uint32_t fm1_cpu1_worker_abort_hold_reset(void);
uint32_t fm1_cpu1_worker_reset_held(void);
uint32_t fm1_cpu1_worker_uncontained_fault_get(void);
fm1_cpu1_worker_smoke_result_t *fm1_cpu1_worker_smoke_result(void);
uint32_t fm1_cpu1_worker_stack_status(uint32_t *user_used, uint32_t *system_used);
#if FM1_CPU1_WORKER_COHERENT_STATUS_SNAPSHOT
/* Bounded 0x78 status copy. CPU0 interrupts are masked only for the eight
 * mailbox words, so an audio IRQ cannot submit between request/done reads. */
void fm1_cpu1_worker_status_snapshot(uint32_t out[8]);
#endif

