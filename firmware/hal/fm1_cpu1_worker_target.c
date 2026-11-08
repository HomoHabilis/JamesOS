/* SPDX-License-Identifier: GPL-3.0-only
 * Private fixed-task CPU1 resident worker. All worker code and data are RAM-only. */
#include "fm1_cpu1_worker.h"
#include "fm1_cpu1_worker_ownership.h"
#include "fm1_cpu1_worker_target_hw.h"
#include "fm1_fault_diag.h"
#include "fm1_time.h"
#include "fm1_audio_rate.h"

uint8_t fm1_cpu1_worker_guard_user[0x100]
    __attribute__((section(".cpu1_worker_bss.00_guard_user"), aligned(16), used));
uint8_t fm1_cpu1_worker_ustack[0x2000]
    __attribute__((section(".cpu1_worker_bss.10_ustack"), aligned(16), used));
uint8_t fm1_cpu1_worker_guard_system[0x100]
    __attribute__((section(".cpu1_worker_bss.20_guard_system"), aligned(16), used));
uint8_t fm1_cpu1_worker_sstack[0x800]
    __attribute__((section(".cpu1_worker_bss.30_sstack"), aligned(16), used));
fm1_cpu1_worker_mailbox_t fm1_cpu1_worker_mailbox
    __attribute__((section(".cpu1_worker_bss.50_mailbox"), aligned(16), used));
volatile uint32_t fm1_cpu1_worker_fault
    __attribute__((section(".cpu1_worker_bss.55_fault"), aligned(4), used));
volatile uint32_t fm1_cpu1_worker_uncontained_fault
    __attribute__((section(".cpu1_worker_bss.57_uncontained"), aligned(4), used));
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile fm1_cpu1_worker_diag_t fm1_cpu1_worker_diag
    __attribute__((section(".cpu1_worker_bss.56_diag"), aligned(4), used));
#define FM1_WORKER_TRACE_SET(field, value) (fm1_cpu1_worker_diag.field = (value))
#define FM1_WORKER_TRACE_INC(field) (++fm1_cpu1_worker_diag.field)
#else
#define FM1_WORKER_TRACE_SET(field, value) ((void)0)
#define FM1_WORKER_TRACE_INC(field) ((void)0)
#endif
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* Written only on CPU0 with local IRQs excluded. Every fixed-task producer
 * runs on CPU0 and checks this word before publishing a request. */
static volatile uint32_t worker_project_fence, worker_project_resume_authorized;
#endif
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static fm1_cpu1_worker_smoke_result_t worker_smoke_result
    __attribute__((section(".cpu1_worker_bss.60_smoke_result"), aligned(4), used));
#endif
#if FM1_CPU1_STACK_MARCH_DIAG
static volatile uint32_t fm1_cpu1_stack_march_words[FM1_CPU1_STACK_MARCH_WORDS]
    __attribute__((aligned(4), used));
#endif

typedef char worker_mailbox_is_40_bytes[(sizeof(fm1_cpu1_worker_mailbox_t) == 40u) ? 1 : -1];
typedef char worker_stacks_within_budget[(0x2000u + 0x800u + 0x200u == 10752u) ? 1 : -1];
#if FM1_CPU1_STACK_MARCH_DIAG
typedef char stack_march_record_is_40_bytes[
    (sizeof(fm1_cpu1_stack_march_result_t) == FM1_CPU1_STACK_MARCH_WORDS * 4u) ? 1 : -1];

void fm1_cpu1_worker_stack_march_publish(
    const fm1_cpu1_stack_march_result_t *result)
{
    uint32_t words[FM1_CPU1_STACK_MARCH_WORDS], i;
    if (!result) return;
    words[0] = result->magic;
    words[1] = result->status;
    words[2] = result->flags;
    words[3] = result->first_bad_address;
    words[4] = result->expected;
    words[5] = result->actual;
    words[6] = result->phase;
    words[7] = result->stack_id;
    words[8] = result->bytes_checked;
    words[9] = result->completed_phase_mask;
    for (i = 0u; i < FM1_CPU1_STACK_MARCH_WORDS; ++i)
        fm1_cpu1_stack_march_words[i] = words[i];
    FM1_CPU1_WORKER_BARRIER();
}

void fm1_cpu1_worker_stack_march_snapshot(uint32_t out[FM1_CPU1_STACK_MARCH_WORDS])
{
    uint32_t i;
    if (!out) return;
    FM1_CPU1_WORKER_BARRIER();
    for (i = 0u; i < FM1_CPU1_STACK_MARCH_WORDS; ++i)
        out[i] = fm1_cpu1_stack_march_words[i];
}

uint32_t fm1_cpu1_worker_stack_march(fm1_cpu1_stack_march_result_t *result)
{
    extern uint32_t _bss_start[], _bss_end[];
    extern uint32_t _ustack_lo[], _ustack_top[], _sstack_lo[], _sstack_top[];
    uintptr_t user_lo = (uintptr_t)fm1_cpu1_worker_ustack;
    uintptr_t sys_lo = (uintptr_t)fm1_cpu1_worker_sstack;
    uintptr_t bss_lo = (uintptr_t)_bss_start, bss_hi = (uintptr_t)_bss_end;
    uintptr_t c0_user_lo = (uintptr_t)_ustack_lo;
    uintptr_t c0_user_hi = (uintptr_t)_ustack_top;
    uintptr_t c0_sys_lo = (uintptr_t)_sstack_lo;
    uintptr_t c0_sys_hi = (uintptr_t)_sstack_top;
    uint32_t safe_ranges;

    if (!result) return 0u;
    result->magic = FM1_CPU1_STACK_MARCH_MAGIC;
    result->status = FM1_CPU1_STACK_MARCH_RESET_NOT_HELD;
    result->flags = 0u;
    result->first_bad_address = 0u;
    result->expected = 0u;
    result->actual = 0u;
    result->phase = 0u;
    result->stack_id = 0xFFFFFFFFu;
    result->bytes_checked = 0u;
    result->completed_phase_mask = 0u;

    /* No stack read or write is allowed until this direct, side-effect-free
     * hardware read confirms C1 is stopped. */
    if (!fm1_cpu1_worker_hw_reset_held()) {
        fm1_cpu1_worker_stack_march_publish(result);
        return 0u;
    }
    result->flags |= FM1_CPU1_STACK_MARCH_HELD_BEFORE;

    safe_ranges = fm1_cpu1_stack_march_ranges_safe(
        user_lo, sizeof fm1_cpu1_worker_ustack,
        sys_lo, sizeof fm1_cpu1_worker_sstack,
        bss_lo, bss_hi,
        c0_user_lo, c0_user_hi, c0_sys_lo, c0_sys_hi);
    if (!safe_ranges) {
        result->status = FM1_CPU1_STACK_MARCH_BOUNDS_INVALID;
        fm1_cpu1_worker_stack_march_publish(result);
        return 0u;
    }
    result->flags |= FM1_CPU1_STACK_MARCH_BOUNDS_PROVED;

    (void)fm1_cpu1_stack_march_run(
        fm1_cpu1_worker_ustack, sizeof fm1_cpu1_worker_ustack,
        fm1_cpu1_worker_sstack, sizeof fm1_cpu1_worker_sstack, result);
    if (!fm1_cpu1_worker_hw_reset_held()) {
        result->status = FM1_CPU1_STACK_MARCH_RESET_LOST;
        fm1_cpu1_worker_stack_march_publish(result);
        return 0u;
    }
    result->flags |= FM1_CPU1_STACK_MARCH_HELD_AFTER;
    fm1_cpu1_worker_stack_march_publish(result);
    return 1u;
}
#endif

/* Fixed registered workload: deterministic integer and IEEE single-precision
 * result, no XIP calls, no mailbox-provided function pointer or address. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
__attribute__((section(".ram_audio"), noinline, used))
static uint32_t worker_smoke_callback(void *context)
{
    fm1_cpu1_worker_smoke_result_t *out = (fm1_cpu1_worker_smoke_result_t *)context;
    uint32_t x = 0x13579bdfu, i;
    volatile float a = 1.5f, b = 2.25f, c;
    union { float f; uint32_t u; } bits;
    for (i = 0; i < 4096u; ++i) x = x * 1664525u + 1013904223u;
    c = a + b;
    bits.f = c;
    out->integer_result = x;
    out->float_bits = bits.u;
    return x ^ bits.u;
}
#endif

__attribute__((section(".ram_audio"), noinline, used))
static uint32_t worker_dispatch_target(void *unused, uint32_t task_id, void *context)
{
    uint32_t result;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    uint32_t start = fm1_ticks();
#endif
    (void)unused;
    FM1_WORKER_TRACE_SET(last_job_status, 1u);
    FM1_WORKER_TRACE_SET(last_job_task, task_id);
    FM1_WORKER_TRACE_SET(last_job_context, (uint32_t)(uintptr_t)context);
    FM1_WORKER_TRACE_SET(last_job_start_ticks, start);
    FM1_WORKER_TRACE_SET(last_job_sequence, fm1_cpu1_worker_mailbox.request_seq);
    FM1_WORKER_TRACE_SET(last_job_elapsed_ticks, 0xffffffffu);
    FM1_CPU1_WORKER_BARRIER();
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (task_id == FM1_CPU1_WORKER_TASK_SMOKE) result = worker_smoke_callback(context);
#if defined(BR_CPU1_AUDIO) && BR_CPU1_AUDIO
    else result = fm1_cpu1_worker_dispatch_audio_task(task_id, context);
#else
    else result = 0xffffffffu;
#endif
#else
#if defined(BR_CPU1_AUDIO) && BR_CPU1_AUDIO
    result = fm1_cpu1_worker_dispatch_audio_task(task_id, context);
#else
    result = 0xffffffffu;
#endif
#endif
    FM1_WORKER_TRACE_SET(last_job_result, result);
    FM1_WORKER_TRACE_SET(last_job_elapsed_ticks, (uint32_t)(fm1_ticks() - start));
    FM1_WORKER_TRACE_SET(last_job_status, result == 0xffffffffu ? 3u : 2u);
    FM1_CPU1_WORKER_BARRIER();
    return result;
}

/* Called only by the opt-in early-boot path, before runtime guards are armed.
 * CPU0 remains responsible for all C1 control writes and the durable journal. */
void fm1_cpu1_worker_init_early(void)
{
    uint32_t i;
    for (i = 0; i < sizeof fm1_cpu1_worker_guard_user; ++i)
        fm1_cpu1_worker_guard_user[i] = 0xA7u;
    for (i = 0; i < sizeof fm1_cpu1_worker_ustack; ++i)
        fm1_cpu1_worker_ustack[i] = 0xCDu;
    for (i = 0; i < sizeof fm1_cpu1_worker_guard_system; ++i)
        fm1_cpu1_worker_guard_system[i] = 0xA7u;
    for (i = 0; i < sizeof fm1_cpu1_worker_sstack; ++i)
        fm1_cpu1_worker_sstack[i] = 0xCDu;
    fm1_cpu1_worker_mailbox.cpu_id = 0xffffffffu;
    fm1_cpu1_worker_mailbox.state = FM1_CPU1_WORKER_OFF;
    fm1_cpu1_worker_mailbox.request_seq = 0u;
    fm1_cpu1_worker_mailbox.done_seq = 0u;
    fm1_cpu1_worker_mailbox.task_id = 0u;
    fm1_cpu1_worker_mailbox.task_context = 0u;
    fm1_cpu1_worker_mailbox.result = 0u;
    fm1_cpu1_worker_mailbox.park_request = 0u;
    fm1_cpu1_worker_mailbox.park_ack = 0u;
    fm1_cpu1_worker_mailbox.heartbeat = 0u;
    fm1_cpu1_worker_fault = 0u;
    fm1_cpu1_worker_uncontained_fault = 0u;
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    worker_project_fence = worker_project_resume_authorized = 0u;
#endif
    FM1_WORKER_TRACE_SET(wait_timeout_count, 0u);
    FM1_WORKER_TRACE_SET(last_job_status, 0u);
    FM1_WORKER_TRACE_SET(last_job_task, 0u);
    FM1_WORKER_TRACE_SET(last_job_context, 0u);
    FM1_WORKER_TRACE_SET(last_job_elapsed_ticks, 0xffffffffu);
    FM1_WORKER_TRACE_SET(last_job_start_ticks, 0u);
    FM1_WORKER_TRACE_SET(last_job_sequence, 0u);
    FM1_WORKER_TRACE_SET(last_job_result, 0u);
    FM1_WORKER_TRACE_SET(last_wait_status, 0u);
    FM1_WORKER_TRACE_SET(last_wait_task, 0u);
    FM1_WORKER_TRACE_SET(last_wait_sequence, 0u);
    FM1_WORKER_TRACE_SET(last_wait_context, 0u);
    FM1_WORKER_TRACE_SET(last_wait_elapsed_ticks, 0xffffffffu);
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    worker_smoke_result.integer_result = 0u;
    worker_smoke_result.float_bits = 0u;
#endif
    FM1_CPU1_WORKER_BARRIER();
}

#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* Never wait while IRQs are masked. Pending or running work is BUSY; caller
 * retries only after the normal audio/worker paths have retired it. */
int fm1_cpu1_worker_project_fence_acquire(void)
{
    uint32_t irq = fm1_cpu1_worker_irq_save();
    int result = 1;
    FM1_CPU1_WORKER_BARRIER();
    if (fm1_cpu1_worker_fault || fm1_cpu1_worker_uncontained_fault) result = -1;
    else if (!worker_project_fence && fm1_cpu1_worker_mailbox.cpu_id == 1u &&
             fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY &&
             !fm1_cpu1_worker_mailbox.park_request && !fm1_cpu1_worker_mailbox.park_ack &&
             fm1_cpu1_worker_mailbox.request_seq == fm1_cpu1_worker_mailbox.done_seq) {
        worker_project_resume_authorized = 0u;
        worker_project_fence = 1u;
        FM1_CPU1_WORKER_BARRIER();
        /* C1 can progress independently, so certify again after publication. */
        if (!fm1_cpu1_worker_fault && !fm1_cpu1_worker_uncontained_fault &&
            fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY &&
            !fm1_cpu1_worker_mailbox.park_request && !fm1_cpu1_worker_mailbox.park_ack &&
            fm1_cpu1_worker_mailbox.request_seq == fm1_cpu1_worker_mailbox.done_seq)
            result = 0;
        else if (fm1_cpu1_worker_fault || fm1_cpu1_worker_uncontained_fault) result = -1;
        else { worker_project_fence = 0u; FM1_CPU1_WORKER_BARRIER(); }
    }
    fm1_cpu1_worker_irq_restore(irq);
    return result;
}
int fm1_cpu1_worker_project_fence_release(void)
{
    uint32_t irq = fm1_cpu1_worker_irq_save();
    int result = -1;
    FM1_CPU1_WORKER_BARRIER();
    if (worker_project_fence && worker_project_resume_authorized &&
        !fm1_cpu1_worker_fault && !fm1_cpu1_worker_uncontained_fault &&
        fm1_cpu1_worker_mailbox.cpu_id == 1u &&
        fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY &&
        !fm1_cpu1_worker_mailbox.park_request && !fm1_cpu1_worker_mailbox.park_ack &&
        fm1_cpu1_worker_mailbox.request_seq == fm1_cpu1_worker_mailbox.done_seq) {
        worker_project_resume_authorized = 0u;
        worker_project_fence = 0u; FM1_CPU1_WORKER_BARRIER(); result = 0;
    }
    fm1_cpu1_worker_irq_restore(irq);
    return result;
}
uint32_t fm1_cpu1_worker_project_fence_held(void)
{ FM1_CPU1_WORKER_BARRIER(); return worker_project_fence; }
#endif

uint32_t fm1_cpu1_worker_available(void)
{
    FM1_CPU1_WORKER_BARRIER();
    return !fm1_cpu1_worker_fault &&
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
           !worker_project_fence &&
#endif
           !fm1_cpu1_worker_mailbox.park_request &&
           fm1_cpu1_worker_mailbox.cpu_id == 1u &&
           fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY;
}

__attribute__((section(".ram_audio"), noinline, used, noreturn))
void fm1_cpu1_worker_main(void)
{
    uint32_t loops = 0u, id = fm1_cpu1_worker_hw_id();
    fm1_cpu1_worker_mailbox.cpu_id = id;
    FM1_CPU1_WORKER_BARRIER();
    fm1_cpu1_worker_mailbox.state = id == 1u ? FM1_CPU1_WORKER_READY : FM1_CPU1_WORKER_BAD_CPU;
    FM1_CPU1_WORKER_BARRIER();
    for (;;) fm1_cpu1_worker_step(&fm1_cpu1_worker_mailbox, worker_dispatch_target, 0, &loops);
}

static int worker_context_is_internal(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    extern uint32_t _bss_end[], _pool_start[], _pool_end[];
    return (v >= 0x01c08000u && v < (uintptr_t)_bss_end) ||
           (v >= (uintptr_t)_pool_start && v < (uintptr_t)_pool_end);
}

static uint32_t worker_wait_state(uint32_t wanted, uint32_t timeout_ticks)
{
    uint32_t start = fm1_ticks();
    if (!timeout_ticks || timeout_ticks > FM1_CPU1_WORKER_TIMEOUT_TICKS)
        return FM1_CPU1_WORKER_TIMEOUT;
    for (;;) {
        uint32_t state = fm1_cpu1_worker_mailbox.state;
        if (state == wanted) {
            FM1_CPU1_WORKER_BARRIER();
            return FM1_CPU1_WORKER_OK;
        }
        if (state == FM1_CPU1_WORKER_BAD_CPU) return FM1_CPU1_WORKER_BAD_TASK;
        if ((uint32_t)(fm1_ticks() - start) >= timeout_ticks) return FM1_CPU1_WORKER_TIMEOUT;
    }
}

uint32_t fm1_cpu1_worker_wait_ready(uint32_t timeout_ticks)
{
    uint32_t rc = worker_wait_state(FM1_CPU1_WORKER_READY, timeout_ticks);
    if (rc != FM1_CPU1_WORKER_OK) return rc;
    FM1_CPU1_WORKER_BARRIER();
    return fm1_cpu1_worker_mailbox.cpu_id == 1u ? FM1_CPU1_WORKER_OK : FM1_CPU1_WORKER_BAD_TASK;
}

static uint32_t worker_submit_fixed(uint32_t task_id, void *context,
                                    uint32_t *sequence_out)
{
    uint32_t seq;
    if (fm1_cpu1_worker_fault) return FM1_CPU1_WORKER_NOT_READY;
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    if (worker_project_fence) return FM1_CPU1_WORKER_BUSY;
#endif
    if (!sequence_out || fm1_cpu1_worker_mailbox.state != FM1_CPU1_WORKER_READY ||
        fm1_cpu1_worker_mailbox.park_request ||
        fm1_cpu1_worker_mailbox.request_seq != fm1_cpu1_worker_mailbox.done_seq)
        return FM1_CPU1_WORKER_BUSY;
    if (!worker_context_is_internal(context)) return FM1_CPU1_WORKER_BAD_CONTEXT;
    seq = fm1_cpu1_worker_mailbox.request_seq + 1u;
    if (!seq) seq = 1u;
    fm1_cpu1_worker_mailbox.task_id = task_id;
    fm1_cpu1_worker_mailbox.task_context = (uint32_t)(uintptr_t)context;
    FM1_CPU1_WORKER_BARRIER();
    fm1_cpu1_worker_mailbox.request_seq = seq;
    FM1_CPU1_WORKER_BARRIER();
    *sequence_out = seq;
    return FM1_CPU1_WORKER_OK;
}

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
uint32_t fm1_cpu1_worker_submit_smoke(uint32_t *sequence_out)
{
    return worker_submit_fixed(FM1_CPU1_WORKER_TASK_SMOKE,
                               &worker_smoke_result, sequence_out);
}
#endif

#if defined(BR_CPU1_AUDIO) && BR_CPU1_AUDIO
uint32_t fm1_cpu1_worker_submit_audio_batch(void *context, uint32_t *sequence_out)
{
    return worker_submit_fixed(FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH,
                               context, sequence_out);
}
uint32_t fm1_cpu1_worker_submit_fx_batch(void *context, uint32_t *sequence_out)
{
    return worker_submit_fixed(FM1_CPU1_WORKER_TASK_AUDIO_FX_BATCH,
                               context, sequence_out);
}
uint32_t fm1_cpu1_worker_submit_audio_hybrid(void *context, uint32_t *sequence_out)
{
    return worker_submit_fixed(FM1_CPU1_WORKER_TASK_AUDIO_FX_SOURCE_BATCH,
                               context, sequence_out);
}
#endif

uint32_t fm1_cpu1_worker_wait(uint32_t sequence, uint32_t timeout_ticks,
                              uint32_t *result_out)
{
    uint32_t start = fm1_ticks(), rc;
    if (!sequence || !result_out || !timeout_ticks || timeout_ticks > FM1_CPU1_WORKER_TIMEOUT_TICKS)
        return FM1_CPU1_WORKER_TIMEOUT;
    for (;;)
        if (fm1_cpu1_worker_mailbox.done_seq == sequence) {
            FM1_CPU1_WORKER_BARRIER();
            *result_out = fm1_cpu1_worker_mailbox.result;
            rc = FM1_CPU1_WORKER_OK;
            FM1_WORKER_TRACE_SET(last_wait_status, rc);
            FM1_WORKER_TRACE_SET(last_wait_task, fm1_cpu1_worker_mailbox.task_id);
            FM1_WORKER_TRACE_SET(last_wait_sequence, sequence);
            FM1_WORKER_TRACE_SET(last_wait_context, fm1_cpu1_worker_mailbox.task_context);
            FM1_WORKER_TRACE_SET(last_wait_elapsed_ticks, (uint32_t)(fm1_ticks() - start));
            FM1_CPU1_WORKER_BARRIER();
            return FM1_CPU1_WORKER_OK;
        } else if ((uint32_t)(fm1_ticks() - start) >= timeout_ticks) {
            FM1_WORKER_TRACE_INC(wait_timeout_count);
            FM1_WORKER_TRACE_SET(last_wait_status, FM1_CPU1_WORKER_TIMEOUT);
            FM1_WORKER_TRACE_SET(last_wait_task, fm1_cpu1_worker_mailbox.task_id);
            FM1_WORKER_TRACE_SET(last_wait_sequence, sequence);
            FM1_WORKER_TRACE_SET(last_wait_context, fm1_cpu1_worker_mailbox.task_context);
            FM1_WORKER_TRACE_SET(last_wait_elapsed_ticks, (uint32_t)(fm1_ticks() - start));
            FM1_CPU1_WORKER_BARRIER();
            (void)fm1_cpu1_worker_abort_hold_reset();
            return FM1_CPU1_WORKER_TIMEOUT;
        }
}

/* Each phase is at most one 44.1 kHz/256-frame half on the selected 360 MHz
 * profile (about 2.09 million nominal core cycles). The independent, larger
 * iteration cap makes a stopped T4 fail closed; it is not a wall-clock. 0x500000
 * also avoids the target address audit's reserved immediate range if the
 * compiler emits this as a negative loop counter. */
#ifndef FM1_CPU1_WORKER_ABSOLUTE_WAIT_SPINS
#define FM1_CPU1_WORKER_ABSOLUTE_WAIT_SPINS 0x500000u
#endif

static uint32_t worker_wait_absolute(uint32_t sequence, uint32_t deadline_tick,
                                     uint32_t *result_out, uint32_t hard)
{
    uint32_t start, remaining, spins = 0u;
    if (!sequence || !result_out) return FM1_CPU1_WORKER_TIMEOUT;
    start = fm1_ticks();
    remaining = deadline_tick - start;
    if ((int32_t)remaining > 0 && remaining > FM1_AUDIO_HALF_TICKS)
        goto hard_fault; /* a phase cannot claim more than one nominal half */
    for (;;) {
        uint32_t now = fm1_ticks();
        uint32_t expired = (int32_t)(now - deadline_tick) >= 0;
        if (expired && !hard) return FM1_CPU1_WORKER_TIMEOUT;
        if (fm1_cpu1_worker_mailbox.done_seq == sequence) {
            FM1_CPU1_WORKER_BARRIER();
            /* The soft cutoff can cross while acquiring the result. For a
             * hard wait the matching completion already proves ownership. */
            if (!hard && (int32_t)(fm1_ticks() - deadline_tick) >= 0)
                return FM1_CPU1_WORKER_TIMEOUT;
            *result_out = fm1_cpu1_worker_mailbox.result;
            if (hard) {
                FM1_WORKER_TRACE_SET(last_wait_status, FM1_CPU1_WORKER_OK);
                FM1_WORKER_TRACE_SET(last_wait_task, fm1_cpu1_worker_mailbox.task_id);
                FM1_WORKER_TRACE_SET(last_wait_sequence, sequence);
                FM1_WORKER_TRACE_SET(last_wait_context, fm1_cpu1_worker_mailbox.task_context);
                FM1_WORKER_TRACE_SET(last_wait_elapsed_ticks, (uint32_t)(fm1_ticks() - start));
                FM1_CPU1_WORKER_BARRIER();
            }
            return FM1_CPU1_WORKER_OK;
        }
        /* No matching completion at an expired hard cutoff: do not poll it
         * again, even if C1 completes during the subsequent reset path. */
        if (expired) break;
        if (++spins >= FM1_CPU1_WORKER_ABSOLUTE_WAIT_SPINS) break;
    }
    /* Hard deadline or stalled T4. A spin-limit failure in the soft phase is
     * not a normal soft expiration: do not leave an unbounded owner running. */
hard_fault:
    FM1_WORKER_TRACE_INC(wait_timeout_count);
    FM1_WORKER_TRACE_SET(last_wait_status, FM1_CPU1_WORKER_TIMEOUT);
    FM1_WORKER_TRACE_SET(last_wait_task, fm1_cpu1_worker_mailbox.task_id);
    FM1_WORKER_TRACE_SET(last_wait_sequence, sequence);
    FM1_WORKER_TRACE_SET(last_wait_context, fm1_cpu1_worker_mailbox.task_context);
    FM1_WORKER_TRACE_SET(last_wait_elapsed_ticks, (uint32_t)(fm1_ticks() - start));
    FM1_CPU1_WORKER_BARRIER();
    (void)fm1_cpu1_worker_abort_hold_reset();
    return FM1_CPU1_WORKER_TIMEOUT;
}

uint32_t fm1_cpu1_worker_poll_until(uint32_t sequence, uint32_t deadline_tick,
                                    uint32_t *result_out)
{
    return worker_wait_absolute(sequence, deadline_tick, result_out, 0u);
}

uint32_t fm1_cpu1_worker_wait_until(uint32_t sequence, uint32_t deadline_tick,
                                    uint32_t *result_out)
{
    return worker_wait_absolute(sequence, deadline_tick, result_out, 1u);
}

void fm1_cpu1_worker_note_terminal_timeout(uint32_t sequence)
{
    FM1_WORKER_TRACE_INC(wait_timeout_count);
    FM1_WORKER_TRACE_SET(last_wait_status, FM1_CPU1_WORKER_TIMEOUT);
    FM1_WORKER_TRACE_SET(last_wait_task, fm1_cpu1_worker_mailbox.task_id);
    FM1_WORKER_TRACE_SET(last_wait_sequence, sequence);
    FM1_WORKER_TRACE_SET(last_wait_context, fm1_cpu1_worker_mailbox.task_context);
    FM1_WORKER_TRACE_SET(last_wait_elapsed_ticks, 0xffffffffu);
    FM1_CPU1_WORKER_BARRIER();
}

uint32_t fm1_cpu1_worker_park(uint32_t timeout_ticks)
{
    if (fm1_cpu1_worker_mailbox.cpu_id != 1u) return FM1_CPU1_WORKER_NOT_READY;
    if (!timeout_ticks || timeout_ticks > FM1_CPU1_WORKER_TIMEOUT_TICKS)
        return FM1_CPU1_WORKER_TIMEOUT;
    fm1_cpu1_worker_mailbox.park_request = 1u;
    FM1_CPU1_WORKER_BARRIER();
    {
        uint32_t rc = worker_wait_state(FM1_CPU1_WORKER_PARKED, timeout_ticks);
        if (rc != FM1_CPU1_WORKER_OK && rc == FM1_CPU1_WORKER_TIMEOUT)
            (void)fm1_cpu1_worker_abort_hold_reset();
        return rc;
    }
}

#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
static uint32_t worker_resume_impl(uint32_t timeout_ticks)
#else
uint32_t fm1_cpu1_worker_resume(uint32_t timeout_ticks)
#endif
{
    uint32_t start = fm1_ticks();
    if (fm1_cpu1_worker_mailbox.state != FM1_CPU1_WORKER_PARKED ||
        !timeout_ticks || timeout_ticks > FM1_CPU1_WORKER_TIMEOUT_TICKS)
        return FM1_CPU1_WORKER_NOT_READY;
    fm1_cpu1_worker_mailbox.park_request = 0u;
    FM1_CPU1_WORKER_BARRIER();
    for (;;)
        if (!fm1_cpu1_worker_mailbox.park_ack &&
            fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY)
            return FM1_CPU1_WORKER_OK;
        else if ((uint32_t)(fm1_ticks() - start) >= timeout_ticks) {
            (void)fm1_cpu1_worker_abort_hold_reset();
            return FM1_CPU1_WORKER_TIMEOUT;
        }
}

#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
uint32_t fm1_cpu1_worker_resume(uint32_t timeout_ticks)
{
    /* A project transaction owns the park; nested legacy flash paths may
     * not release it. Only project_resume may do so after loan revocation. */
    if (worker_project_fence) return FM1_CPU1_WORKER_NOT_READY;
    return worker_resume_impl(timeout_ticks);
}

/* Caller invokes only after the project transaction has revoked every loan.
 * This records ordering; it cannot independently verify caller-owned loans. */
int fm1_cpu1_worker_project_authorize_resume(void)
{
    FM1_CPU1_WORKER_BARRIER();
    if (!worker_project_fence || fm1_cpu1_worker_fault || fm1_cpu1_worker_uncontained_fault ||
        fm1_cpu1_worker_mailbox.cpu_id != 1u ||
        fm1_cpu1_worker_mailbox.state != FM1_CPU1_WORKER_PARKED ||
        !fm1_cpu1_worker_mailbox.park_request || !fm1_cpu1_worker_mailbox.park_ack ||
        fm1_cpu1_worker_mailbox.request_seq != fm1_cpu1_worker_mailbox.done_seq) return -1;
    worker_project_resume_authorized = 1u;
    FM1_CPU1_WORKER_BARRIER(); return 0;
}
uint32_t fm1_cpu1_worker_project_resume(uint32_t timeout_ticks)
{
    FM1_CPU1_WORKER_BARRIER();
    if (!worker_project_resume_authorized || !worker_project_fence || fm1_cpu1_worker_fault || fm1_cpu1_worker_uncontained_fault ||
        fm1_cpu1_worker_mailbox.cpu_id != 1u ||
        fm1_cpu1_worker_mailbox.state != FM1_CPU1_WORKER_PARKED ||
        !fm1_cpu1_worker_mailbox.park_request || !fm1_cpu1_worker_mailbox.park_ack ||
        fm1_cpu1_worker_mailbox.request_seq != fm1_cpu1_worker_mailbox.done_seq)
        return FM1_CPU1_WORKER_NOT_READY;
    return worker_resume_impl(timeout_ticks);
}
#endif

/* Emergency bounded stop only: do not re-enter ROM or restore the entry word
 * at runtime (it is protected after guard setup). The IN_PROGRESS NOINIT
 * journal intentionally remains for early-boot entry restoration next reset. */
uint32_t fm1_cpu1_worker_abort_hold_reset(void)
{
    uint32_t reset_confirmed;
    fm1_cpu1_worker_fault = 1u;
    FM1_CPU1_WORKER_BARRIER();
#if FM1_FAULT_C1_TIMEOUT_SIDECAR && FM1_FAULT_C1_TIMEOUT_CAPTURE && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    /* C1's completed phase-2 slot is copied before reset can clear its BSS.
     * The helper is C0-only and declines incomplete or mismatched jobs. */
    fm1_fault_c1_timeout_capture_before_reset();
#endif
    reset_confirmed = fm1_cpu1_worker_note_reset_confirmation(
        &fm1_cpu1_worker_uncontained_fault,
        fm1_cpu1_worker_hw_hold_reset());
    fm1_cpu1_worker_mailbox.state = reset_confirmed ?
        FM1_CPU1_WORKER_ABORTED : FM1_CPU1_WORKER_BAD_CPU;
    fm1_cpu1_worker_fault = 1u;
    FM1_CPU1_WORKER_BARRIER();
    return fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_ABORTED ?
           FM1_CPU1_WORKER_OK : FM1_CPU1_WORKER_RESTORE_FAILED;
}

uint32_t fm1_cpu1_worker_reset_held(void)
{
    uint32_t held = fm1_cpu1_worker_hw_reset_held();
    held = fm1_cpu1_worker_note_reset_held(
        &fm1_cpu1_worker_uncontained_fault, fm1_cpu1_worker_fault, held);
    FM1_CPU1_WORKER_BARRIER();
    return held;
}

uint32_t fm1_cpu1_worker_uncontained_fault_get(void)
{
    FM1_CPU1_WORKER_BARRIER();
    return fm1_cpu1_worker_uncontained_fault;
}

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
fm1_cpu1_worker_smoke_result_t *fm1_cpu1_worker_smoke_result(void)
{
    return &worker_smoke_result;
}
#endif

uint32_t fm1_cpu1_worker_stack_status(uint32_t *user_used, uint32_t *system_used)
{
    uint32_t i, user_unused = 0u, system_unused = 0u, guards_ok = 1u;
    if (!user_used || !system_used) return 0u;
    for (i = 0; i < sizeof fm1_cpu1_worker_guard_user; ++i)
        if (fm1_cpu1_worker_guard_user[i] != 0xA7u) guards_ok = 0u;
    for (i = 0; i < sizeof fm1_cpu1_worker_guard_system; ++i)
        if (fm1_cpu1_worker_guard_system[i] != 0xA7u) guards_ok = 0u;
    for (i = 0; i < sizeof fm1_cpu1_worker_ustack && fm1_cpu1_worker_ustack[i] == 0xCDu; ++i)
        ++user_unused;
    for (i = 0; i < sizeof fm1_cpu1_worker_sstack && fm1_cpu1_worker_sstack[i] == 0xCDu; ++i)
        ++system_unused;
    *user_used = (uint32_t)sizeof fm1_cpu1_worker_ustack - user_unused;
    *system_used = (uint32_t)sizeof fm1_cpu1_worker_sstack - system_unused;
    return guards_ok;
}

#if FM1_CPU1_WORKER_COHERENT_STATUS_SNAPSHOT
void fm1_cpu1_worker_status_snapshot(uint32_t out[8])
{
    uint32_t irq, state, cpu_id, heartbeat, request_seq, done_seq;
    uint32_t park_ack, worker_fault, user_used = 0xffffffffu;
    uint32_t system_used = 0xffffffffu, stack_checked = 0u, guards_ok = 0u;
    uint32_t flags;
    if (!out) return;

    /* The SysEx foreground handler may be interrupted by app_audio(), which
     * is the sole writer of request_seq. Keep the pair bounded and coherent
     * with respect to that producer; never hold IRQs masked during reply IO. */
    irq = fm1_cpu1_worker_irq_save();
    FM1_CPU1_WORKER_BARRIER();
    state = fm1_cpu1_worker_mailbox.state;
    cpu_id = fm1_cpu1_worker_mailbox.cpu_id;
    heartbeat = fm1_cpu1_worker_mailbox.heartbeat;
    request_seq = fm1_cpu1_worker_mailbox.request_seq;
    done_seq = fm1_cpu1_worker_mailbox.done_seq;
    park_ack = fm1_cpu1_worker_mailbox.park_ack;
    worker_fault = fm1_cpu1_worker_fault;
    FM1_CPU1_WORKER_BARRIER();
    fm1_cpu1_worker_irq_restore(irq);

    /* Stack memory is read only after C1 has already acknowledged PARKED. */
    if (state == FM1_CPU1_WORKER_PARKED) {
        stack_checked = 1u;
        guards_ok = fm1_cpu1_worker_stack_status(&user_used, &system_used);
    }
    flags = park_ack | (worker_fault << 1) |
        ((stack_checked && !guards_ok ? 1u : 0u) << 2);
    out[0] = state;
    out[1] = cpu_id;
    out[2] = heartbeat;
    out[3] = request_seq;
    out[4] = done_seq;
    out[5] = flags;
    out[6] = user_used;
    out[7] = system_used;
}
#endif
