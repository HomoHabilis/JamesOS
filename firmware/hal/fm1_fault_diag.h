/* SPDX-License-Identifier: GPL-3.0-only
 * Read-only extended crash snapshot for private ID1057 diagnostics. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define FM1_FAULT_DIAG_MAGIC 0x46444731u /* "FDG1" */
#define FM1_FAULT_DIAG_VERSION 3u
#define FM1_FAULT_BUNDLE_MAGIC 0x46444231u /* "FDB1" */
#define FM1_FAULT_BUNDLE_VERSION 6u
#define FM1_FAULT_DIAG_UNKNOWN 0xffffffffu
#define FM1_FAULT_DIAG_C0_STACK_WORDS 8u

/* Optional, separate retained record for the active C0 callee-save pop frame.
 * The v6 bundle stays byte-for-byte unchanged. */
#ifndef FM1_FAULT_C0_POPFRAME_SIDECAR
#define FM1_FAULT_C0_POPFRAME_SIDECAR 1
#endif

/* Optional append-only snapshots used only to inspect contained worker faults.
 * Both records live after the original 80-byte C0 pop-frame sidecar. */
#ifndef FM1_FAULT_C1_TIMEOUT_SIDECAR
#define FM1_FAULT_C1_TIMEOUT_SIDECAR 1
#endif
/* Capture-only switch: retained layout and readback remain available. */
#ifndef FM1_FAULT_C1_TIMEOUT_CAPTURE
#define FM1_FAULT_C1_TIMEOUT_CAPTURE 0
#endif
#if defined(FM1_DEVELOPMENT_DIAGNOSTICS) && !FM1_DEVELOPMENT_DIAGNOSTICS && FM1_FAULT_C1_TIMEOUT_CAPTURE
#error "Distribution firmware cannot enable the diagnostic C1 timeout capture"
#endif
#if FM1_FAULT_C1_TIMEOUT_SIDECAR && !FM1_FAULT_C0_POPFRAME_SIDECAR
#error "C1 timeout sidecar requires the existing C0 pop-frame sidecar"
#endif

#if FM1_FAULT_C0_POPFRAME_SIDECAR
#define FM1_FAULT_POPFRAME_MAGIC 0x46504631u /* "FPF1" */
#define FM1_FAULT_POPFRAME_VERSION 1u
#define FM1_FAULT_POPFRAME_WORDS 13u /* r4-r15 and the saved return PC */
#define FM1_FAULT_POPFRAME_PC_WORD 12u
#define FM1_FAULT_POPFRAME_VALID 1u
#endif

typedef struct {
    uint32_t magic;                 /* published last */
    uint32_t version;
    uint32_t byte_size;
    uint32_t flags;                 /* bit 0: stack data checked while parked */
    uint32_t fault_count;
    uint32_t current_cpu_id;
    uint32_t current_pcrs;
    uint32_t dbg_msg;
    uint32_t axi_rd_inv_id;
    uint32_t axi_wr_inv_id;
    uint32_t prp_mmu_err_rid;
    uint32_t prp_mmu_err_wid;
    uint32_t c1_con;
    uint32_t worker_state;
    uint32_t worker_cpu_id;
    uint32_t worker_request_seq;
    uint32_t worker_done_seq;
    uint32_t worker_task_id;
    uint32_t worker_task_context;
    uint32_t worker_result;
    uint32_t worker_park_request;
    uint32_t worker_park_ack;
    uint32_t worker_heartbeat;
    uint32_t worker_fault;
    uint32_t wait_timeout_count;
    uint32_t last_job_status;
    uint32_t last_job_task;
    uint32_t last_job_context;
    uint32_t last_job_elapsed_ticks;
    uint32_t last_job_start_ticks;
    uint32_t last_job_sequence;
    uint32_t last_job_result;
    uint32_t last_wait_status, last_wait_task, last_wait_sequence;
    uint32_t last_wait_context, last_wait_elapsed_ticks;
    uint32_t stack_status;          /* 1 = guards intact, 0 = damaged, unknown otherwise */
    uint32_t user_stack_used;
    uint32_t system_stack_used;
    uint32_t fault_frame[26];
    /* SDK JL_TypeDef_q32DSP fields for both cores; offsets are captured from
     * the active AC79 definition (PCRS 0x1f, EMU_CON 0x34, EMU_MSG 0x35). */
    uint32_t cpu0_pcrs, cpu1_pcrs;
    uint32_t cpu0_emu_con, cpu1_emu_con;
    uint32_t cpu0_emu_msg, cpu1_emu_msg;
    /* v3 append-only CPU0 fault context. Stack words are sampled only from
     * fixed C0 stack ranges; no CPU1 stack is inspected. */
    uint32_t c0_stack_flags;          /* bit0 C0, bit1 SP dump, bit2 USP dump, bit3 SSP valid, bit4 ETM valid */
    uint32_t c0_saved_sp, c0_saved_usp, c0_saved_ssp;
    uint32_t c0_sp_window_base, c0_usp_window_base;
    uint32_t c0_sp_words[FM1_FAULT_DIAG_C0_STACK_WORDS];
    uint32_t c0_usp_words[FM1_FAULT_DIAG_C0_STACK_WORDS];
    uint32_t c0_etm_pc[4];             /* SDK ETM_PC0..3; for a CPU1 slot this stores CPU1's own ETM */
} fm1_fault_diag_t;

typedef char fm1_fault_diag_v2_prefix_is_288[(offsetof(fm1_fault_diag_t, c0_stack_flags) == 288u) ? 1 : -1];
typedef char fm1_fault_diag_v3_is_392[(sizeof(fm1_fault_diag_t) == 392u) ? 1 : -1];

/* Versioned dual-core capture. Header magic is published last. Each slot is
 * an unchanged V3 record and is valid only when its ready phase and
 * fault_count both match this bundle's epoch. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t byte_size;
    uint32_t epoch;
    uint32_t valid_mask;
    uint32_t owner_cpu;
    uint32_t cpu0_phase;
    uint32_t cpu1_phase;
    fm1_fault_diag_t cpu[2];
    /* Single-writer read-only globals sampled once before publication. */
    uint32_t context_in_audio;
    uint32_t context_ui_part;
    /* Per-core FX cursor, sampled only after that core's fault slot is ready. */
    uint32_t fx_progress[2];
} fm1_fault_diag_bundle_t;

typedef char fm1_fault_bundle_header_is_32[(offsetof(fm1_fault_diag_bundle_t, cpu) == 32u) ? 1 : -1];
typedef char fm1_fault_bundle_v5_prefix_is_824[(offsetof(fm1_fault_diag_bundle_t, fx_progress) == 824u) ? 1 : -1];
typedef char fm1_fault_bundle_is_832[(sizeof(fm1_fault_diag_bundle_t) == 832u) ? 1 : -1];

#if FM1_FAULT_C0_POPFRAME_SIDECAR
/* C0-only record captured from the system stack after a fatal exception.
 * saved_frame_sp is the SP saved by the fatal stub, after its RETS push;
 * pop_frame_sp is therefore saved_frame_sp + 4. words[12] is the PC/RETS
 * slot in the compiler's {rets,r15-r4}/{pc,r15-r4} register group. The
 * target ABI stores the return slot last, at words[12]. */
typedef struct {
    uint32_t magic;             /* published last */
    uint32_t version;
    uint32_t byte_size;
    uint32_t fault_count;       /* must match the v6 bundle epoch */
    uint32_t flags;             /* FM1_FAULT_POPFRAME_VALID */
    uint32_t saved_frame_sp;
    uint32_t pop_frame_sp;
    uint32_t words[FM1_FAULT_POPFRAME_WORDS];
} fm1_fault_popframe_diag_t;

typedef char fm1_fault_popframe_record_is_80[(sizeof(fm1_fault_popframe_diag_t) == 80u) ? 1 : -1];

extern volatile fm1_fault_popframe_diag_t fm1_fault_popframe_diag;

/* Validate the precise 13-word pop range before any memory dereference.
 * Stack high is exclusive. saved_frame_sp is the SP saved after the fatal
 * stub's one-word RETS push. */
static inline uint32_t fm1_fault_popframe_range(uint32_t saved_frame_sp,
                                               uint32_t low,
                                               uint32_t high,
                                               uint32_t *pop_frame_sp)
{
    uint32_t base;
    if (!pop_frame_sp || high < low || (saved_frame_sp & 3u) ||
        (low & 3u) || (high & 3u) || saved_frame_sp > 0xfffffffbu ||
        saved_frame_sp < low || saved_frame_sp > high ||
        high - saved_frame_sp < (FM1_FAULT_POPFRAME_WORDS + 1u) * 4u)
        return 0u;
    base = saved_frame_sp + 4u;
    if (base > high || high - base < FM1_FAULT_POPFRAME_WORDS * 4u)
        return 0u;
    *pop_frame_sp = base;
    return 1u;
}

static inline void fm1_fault_popframe_barrier(void)
{
#if defined(FM1_DEVICE)
    __asm__ volatile("csync" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/* source_words must be the mapped view of pop_frame_sp after the caller has
 * passed fm1_fault_popframe_range. The checks are repeated before reading.
 * The CPU argument keeps a CPU1 fault from touching this C0-only record. */
static inline uint32_t fm1_fault_popframe_capture(
    volatile fm1_fault_popframe_diag_t *out, uint32_t cpu, uint32_t epoch,
    uint32_t saved_frame_sp, uint32_t low, uint32_t high,
    const volatile uint32_t *source_words)
{
    uint32_t pop_frame_sp, i;
    if (!out || cpu != 0u) return 0u;
    out->magic = 0u;
    fm1_fault_popframe_barrier();
    if (!epoch || !source_words || ((uintptr_t)source_words & 3u) ||
        !fm1_fault_popframe_range(saved_frame_sp, low, high, &pop_frame_sp))
        return 0u;
    out->version = FM1_FAULT_POPFRAME_VERSION;
    out->byte_size = sizeof(fm1_fault_popframe_diag_t);
    out->fault_count = epoch;
    out->flags = FM1_FAULT_POPFRAME_VALID;
    out->saved_frame_sp = saved_frame_sp;
    out->pop_frame_sp = pop_frame_sp;
    for (i = 0u; i < FM1_FAULT_POPFRAME_WORDS; ++i)
        out->words[i] = source_words[i];
    fm1_fault_popframe_barrier();
    out->magic = FM1_FAULT_POPFRAME_MAGIC;
    fm1_fault_popframe_barrier();
    return 1u;
}

static inline uint32_t fm1_fault_popframe_chunk(
    const volatile fm1_fault_popframe_diag_t *snapshot, uint32_t expected_epoch,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)snapshot;
    uint32_t base, i;
    if (!snapshot || !out || !expected_epoch ||
        snapshot->magic != FM1_FAULT_POPFRAME_MAGIC)
        return 0u;
    fm1_fault_popframe_barrier();
    if (snapshot->version != FM1_FAULT_POPFRAME_VERSION ||
        snapshot->byte_size != sizeof(fm1_fault_popframe_diag_t) ||
        snapshot->fault_count != expected_epoch ||
        snapshot->flags != FM1_FAULT_POPFRAME_VALID ||
        (snapshot->saved_frame_sp & 3u) ||
        snapshot->saved_frame_sp > 0xfffffffbu)
        return 0u;
    base = snapshot->saved_frame_sp + 4u;
    if (snapshot->pop_frame_sp != base || (base & 3u) || !requested ||
        requested > 125u || offset > sizeof(fm1_fault_popframe_diag_t) ||
        requested > sizeof(fm1_fault_popframe_diag_t) - offset)
        return 0u;
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}
#endif

#if FM1_FAULT_C1_TIMEOUT_SIDECAR
#define FM1_FAULT_C1_TIMEOUT_MAGIC 0x46435431u /* "FCT1" */
#define FM1_FAULT_C1_TIMEOUT_VERSION 1u
#define FM1_FAULT_C1_TIMEOUT_WORKER_RUNNING 2u
#define FM1_FAULT_C1_TIMEOUT_TASK_MIN 2u
#define FM1_FAULT_C1_TIMEOUT_TASK_MAX 4u
#define FM1_FAULT_C1_TIMEOUT_C0_WORDS 10u
#define FM1_FAULT_C1_TIMEOUT_C0_OFFSET 104u
#define FM1_FAULT_C1_TIMEOUT_C0_LAST_OFFSET 140u
#define FM1_FAULT_C1_TIMEOUT_C0_STACK_MAGIC 0x46435331u /* "FCS1" */
#define FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION 1u
#define FM1_FAULT_C0_SYSTEM_STACK_VERSION 3u
#define FM1_FAULT_C0_SYSTEM_STACK_OFFSET 64u
#define FM1_FAULT_C0_SYSTEM_STACK_LAST_OFFSET 100u
#define FM1_FAULT_C0_SYSTEM_STACK_READ_KIND 4u

static inline uint32_t fm1_fault_c0_stack_window_layout(
    uint32_t version, uint32_t *first_offset, uint32_t *last_offset)
{
    if (!first_offset || !last_offset) return 0u;
    if (version == FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION) {
        *first_offset = FM1_FAULT_C1_TIMEOUT_C0_OFFSET;
        *last_offset = FM1_FAULT_C1_TIMEOUT_C0_LAST_OFFSET;
        return 1u;
    }
    if (version == FM1_FAULT_C0_SYSTEM_STACK_VERSION) {
        *first_offset = FM1_FAULT_C0_SYSTEM_STACK_OFFSET;
        *last_offset = FM1_FAULT_C0_SYSTEM_STACK_LAST_OFFSET;
        return 1u;
    }
    return 0u;
}

#ifndef FM1_FAULT_C1_TIMEOUT_BARRIER
#define FM1_FAULT_C1_TIMEOUT_BARRIER() fm1_fault_popframe_barrier()
#endif

/* Captured only by C0 immediately before a confirmed timeout reset. The
 * embedded C1 exception record is accepted only after phase 2 publication. */
typedef struct {
    uint32_t magic;             /* published last */
    uint32_t version;
    uint32_t byte_size;
    uint32_t epoch;
    uint32_t request_seq;
    uint32_t capture_tick;
    uint32_t capture_cpu_id;
    fm1_fault_diag_t cpu1;
} fm1_fault_c1_timeout_record_t;

typedef char fm1_fault_c1_timeout_record_is_420[
    (sizeof(fm1_fault_c1_timeout_record_t) == 420u) ? 1 : -1];

/* Version 1 stores USP; version 3 stores active SP in the same 72-byte ABI. */
typedef struct {
    uint32_t magic;             /* published last */
    uint32_t version;
    uint32_t byte_size;
    uint32_t fault_count;
    uint32_t current_cpu_id;
    uint32_t fault_pc;
    uint32_t saved_usp;         /* v1: USP; v3: active SP */
    uint32_t sample_base;
    uint32_t words[FM1_FAULT_C1_TIMEOUT_C0_WORDS];
} fm1_fault_c0_stack_window_record_t;

typedef char fm1_fault_c0_stack_window_record_is_72[
    (sizeof(fm1_fault_c0_stack_window_record_t) == 72u) ? 1 : -1];

extern volatile fm1_fault_c1_timeout_record_t fm1_fault_c1_timeout_record;
extern volatile fm1_fault_c0_stack_window_record_t fm1_fault_c0_stack_window_record;

static inline uint32_t fm1_fault_c1_timeout_identity_valid(
    const volatile fm1_fault_diag_t *cpu1, uint32_t epoch, uint32_t request_seq)
{
    return cpu1 && epoch && request_seq &&
        cpu1->magic == FM1_FAULT_DIAG_MAGIC &&
        cpu1->version == FM1_FAULT_DIAG_VERSION &&
        cpu1->byte_size == sizeof(fm1_fault_diag_t) &&
        cpu1->fault_count == epoch && cpu1->current_cpu_id == 1u &&
        cpu1->worker_state == FM1_FAULT_C1_TIMEOUT_WORKER_RUNNING &&
        cpu1->worker_cpu_id == 1u &&
        cpu1->worker_request_seq == request_seq &&
        cpu1->worker_done_seq != request_seq &&
        cpu1->worker_task_id >= FM1_FAULT_C1_TIMEOUT_TASK_MIN &&
        cpu1->worker_task_id <= FM1_FAULT_C1_TIMEOUT_TASK_MAX &&
        cpu1->worker_task_context != 0u;
}

static inline uint32_t fm1_fault_c1_timeout_record_valid(
    const volatile fm1_fault_c1_timeout_record_t *record)
{
    return record && record->magic == FM1_FAULT_C1_TIMEOUT_MAGIC &&
        record->version == FM1_FAULT_C1_TIMEOUT_VERSION &&
        record->byte_size == sizeof(fm1_fault_c1_timeout_record_t) &&
        record->capture_cpu_id == 0u &&
        fm1_fault_c1_timeout_identity_valid(&record->cpu1,
                                           record->epoch,
                                           record->request_seq);
}

static inline uint32_t fm1_fault_c1_timeout_capture(
    volatile fm1_fault_c1_timeout_record_t *out, uint32_t caller_cpu,
    uint32_t phase, uint32_t worker_state, uint32_t worker_cpu_id,
    uint32_t request_seq, uint32_t done_seq, uint32_t task_id,
    uint32_t task_context, uint32_t capture_tick,
    const volatile fm1_fault_diag_t *cpu1)
{
    volatile uint32_t *dst;
    const volatile uint32_t *src;
    uint32_t i, epoch;
    if (!out || !cpu1 || caller_cpu != 0u) return 0u;
    epoch = cpu1->fault_count;
    /* Abort can be re-entered after the first capture. Preserve the matching
     * sealed record even though the mailbox has since become ABORTED. */
    if (fm1_fault_c1_timeout_record_valid(out) &&
        out->epoch == epoch && out->request_seq == cpu1->worker_request_seq &&
        out->cpu1.magic == cpu1->magic)
        return 1u;
    out->magic = 0u;
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    if (phase != 2u || worker_state != FM1_FAULT_C1_TIMEOUT_WORKER_RUNNING ||
        worker_cpu_id != 1u || !request_seq || done_seq == request_seq ||
        task_id < FM1_FAULT_C1_TIMEOUT_TASK_MIN ||
        task_id > FM1_FAULT_C1_TIMEOUT_TASK_MAX || !task_context ||
        !fm1_fault_c1_timeout_identity_valid(cpu1, epoch, request_seq) ||
        cpu1->worker_done_seq != done_seq || cpu1->worker_task_id != task_id ||
        cpu1->worker_task_context != task_context)
        return 0u;
    out->version = FM1_FAULT_C1_TIMEOUT_VERSION;
    out->byte_size = sizeof(fm1_fault_c1_timeout_record_t);
    out->epoch = epoch;
    out->request_seq = request_seq;
    out->capture_tick = capture_tick;
    out->capture_cpu_id = caller_cpu;
    dst = (volatile uint32_t *)&out->cpu1;
    src = (const volatile uint32_t *)cpu1;
    for (i = 0u; i < sizeof(fm1_fault_diag_t) / sizeof(uint32_t); ++i)
        dst[i] = src[i];
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    out->magic = FM1_FAULT_C1_TIMEOUT_MAGIC;
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    return 1u;
}

static inline uint32_t fm1_fault_c1_timeout_chunk(
    const volatile fm1_fault_c1_timeout_record_t *record,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)record;
    uint32_t i;
    if (!fm1_fault_c1_timeout_record_valid(record) || !out || !requested ||
        requested > 125u || offset > sizeof(fm1_fault_c1_timeout_record_t) ||
        requested > sizeof(fm1_fault_c1_timeout_record_t) - offset)
        return 0u;
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}

static inline uint32_t fm1_fault_c0_stack_window_capture_abi(
    volatile fm1_fault_c0_stack_window_record_t *out, uint32_t caller_cpu,
    uint32_t fault_count, uint32_t fault_pc, uint32_t saved_sp,
    uint32_t low, uint32_t high, const volatile uint32_t *source_words,
    uint32_t version)
{
    uint32_t i, sample_base, first_offset, last_offset;
    if (!out || caller_cpu != 0u) return 0u;
    out->magic = 0u;
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    if (!fault_count ||
        !fm1_fault_c0_stack_window_layout(version, &first_offset, &last_offset) ||
        !source_words || ((uintptr_t)source_words & 3u) ||
        (saved_sp & 3u) || high < low || saved_sp < low || saved_sp > high ||
        saved_sp > 0xfffffffbu - (last_offset + 4u) ||
        high - saved_sp < last_offset + 4u)
        return 0u;
    sample_base = saved_sp + first_offset;
    if (sample_base < saved_sp || sample_base < low || sample_base > high ||
        high - sample_base < FM1_FAULT_C1_TIMEOUT_C0_WORDS * 4u)
        return 0u;
    out->version = version;
    out->byte_size = sizeof(fm1_fault_c0_stack_window_record_t);
    out->fault_count = fault_count;
    out->current_cpu_id = caller_cpu;
    out->fault_pc = fault_pc;
    out->saved_usp = saved_sp;
    out->sample_base = sample_base;
    for (i = 0u; i < FM1_FAULT_C1_TIMEOUT_C0_WORDS; ++i)
        out->words[i] = source_words[i];
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    out->magic = FM1_FAULT_C1_TIMEOUT_C0_STACK_MAGIC;
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    return 1u;
}

static inline uint32_t fm1_fault_c0_stack_window_record_valid_abi(
    const volatile fm1_fault_c0_stack_window_record_t *record,
    uint32_t version, uint32_t low, uint32_t high)
{
    uint32_t sample_base, first_offset, last_offset;
    if (!record || record->magic != FM1_FAULT_C1_TIMEOUT_C0_STACK_MAGIC ||
        record->version != version ||
        !fm1_fault_c0_stack_window_layout(version, &first_offset, &last_offset) ||
        record->byte_size != sizeof(fm1_fault_c0_stack_window_record_t) ||
        !record->fault_count || record->current_cpu_id != 0u ||
        (record->saved_usp & 3u) || high < low || record->saved_usp < low ||
        record->saved_usp > high ||
        record->saved_usp > 0xfffffffbu - (last_offset + 4u) ||
        high - record->saved_usp < last_offset + 4u)
        return 0u;
    sample_base = record->saved_usp + first_offset;
    return record->sample_base == sample_base && sample_base >= low &&
        sample_base <= high &&
        high - sample_base >= FM1_FAULT_C1_TIMEOUT_C0_WORDS * 4u;
}

static uint32_t __attribute__((noinline)) fm1_fault_c0_stack_window_chunk_abi(
    const volatile fm1_fault_c0_stack_window_record_t *record,
    uint32_t version, uint32_t low, uint32_t high,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)record;
    uint32_t i;
    if (!fm1_fault_c0_stack_window_record_valid_abi(record, version, low, high) ||
        !out || !requested || requested > 125u ||
        offset > sizeof(fm1_fault_c0_stack_window_record_t) ||
        requested > sizeof(fm1_fault_c0_stack_window_record_t) - offset)
        return 0u;
    FM1_FAULT_C1_TIMEOUT_BARRIER();
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}

void fm1_fault_c1_timeout_capture_before_reset(void);
void fm1_fault_c0_stack_window_capture_from_frame(uint32_t fault_count,
                                                  const uint32_t frame[26]);
#endif

/* Optional retained bytes around a fatal PC in the two fixed executable RAM
 * sections. The default-off path adds no runtime code or retained storage.
 * Keep this after the existing 80/420/72-byte records in linker order. */
#ifndef FM1_FAULT_RAM_CODE_SIDECAR
#define FM1_FAULT_RAM_CODE_SIDECAR 1
#endif
/* Preserve the retained record ABI while allowing an audio-fit build to omit
 * RAM-code snippet capture/readback after its bytes have been archived. */
#ifndef FM1_FAULT_RAM_CODE_CAPTURE_READ
#define FM1_FAULT_RAM_CODE_CAPTURE_READ 0
#endif
#if FM1_FAULT_RAM_CODE_CAPTURE_READ != 0 && FM1_FAULT_RAM_CODE_CAPTURE_READ != 1
#error "FM1_FAULT_RAM_CODE_CAPTURE_READ must be zero or one"
#endif
#if defined(FM1_DEVELOPMENT_DIAGNOSTICS) && !FM1_DEVELOPMENT_DIAGNOSTICS && FM1_FAULT_RAM_CODE_CAPTURE_READ
#error "Distribution firmware cannot enable diagnostic RAM-code capture"
#endif
#if FM1_FAULT_RAM_CODE_SIDECAR
#if !FM1_FAULT_C0_POPFRAME_SIDECAR || !FM1_FAULT_C1_TIMEOUT_SIDECAR
#error "RAM-code sidecar must follow all existing retained fault records"
#endif
#define FM1_FAULT_RAM_CODE_MAGIC 0x46524331u /* "FRC1" */
#define FM1_FAULT_RAM_CODE_VERSION 2u
#define FM1_FAULT_RAM_CODE_BYTES 64u
#define FM1_FAULT_RAM_CODE_VALID 1u

typedef struct {
    uint32_t magic;             /* published last */
    uint32_t version;
    uint32_t byte_size;
    uint32_t fault_count;
    uint32_t current_cpu_id;
    uint32_t fault_pc;
    uint32_t sample_start;
    uint32_t flags;
    uint8_t code[FM1_FAULT_RAM_CODE_BYTES];
} fm1_fault_ram_code_record_t;

typedef char fm1_fault_ram_code_record_is_96[
    (sizeof(fm1_fault_ram_code_record_t) == 96u) ? 1 : -1];

extern volatile fm1_fault_ram_code_record_t fm1_fault_ram_code_record;
void fm1_fault_ram_code_capture_owned(uint32_t epoch, uint32_t cpu,
                                      uint32_t fault_pc);

/* Bind the reader to the published phase-2 owner slot, never just to a
 * matching stale sidecar epoch. */
static inline uint32_t fm1_fault_ram_code_bundle_identity(
    const volatile fm1_fault_diag_bundle_t *bundle,
    uint32_t *epoch_out, uint32_t *owner_out, uint32_t *pc_out)
{
    uint32_t owner, epoch, valid, phase;
    const volatile fm1_fault_diag_t *slot;
    if (!bundle || !epoch_out || !owner_out || !pc_out ||
        bundle->magic != FM1_FAULT_BUNDLE_MAGIC ||
        bundle->version != FM1_FAULT_BUNDLE_VERSION ||
        bundle->byte_size != sizeof(fm1_fault_diag_bundle_t))
        return 0u;
    owner = bundle->owner_cpu;
    epoch = bundle->epoch;
    valid = bundle->valid_mask;
    if (!epoch || owner > 1u || (valid & ~3u) || !(valid & (1u << owner)))
        return 0u;
    phase = owner == 0u ? bundle->cpu0_phase : bundle->cpu1_phase;
    slot = &bundle->cpu[owner];
    if (phase != 2u || slot->magic != FM1_FAULT_DIAG_MAGIC ||
        slot->version != FM1_FAULT_DIAG_VERSION ||
        slot->byte_size != sizeof(fm1_fault_diag_t) ||
        slot->fault_count != epoch || slot->current_cpu_id != owner)
        return 0u;
    *epoch_out = epoch;
    *owner_out = owner;
    *pc_out = slot->fault_frame[16];
    return 1u;
}

/* The two ranges are [low, high). The 64-byte capture is centered on the
 * word-aligned fault PC and must remain wholly inside one executable range. */
static inline uint32_t fm1_fault_ram_code_window(uint32_t pc,
                                                  uint32_t text_low,
                                                  uint32_t text_high,
                                                  uint32_t audio_low,
                                                  uint32_t audio_high,
                                                  uint32_t *sample_start)
{
    uint32_t aligned_pc, start, owners = 0u, matches = 0u, candidate = 0u;
    uint32_t low[2] = { text_low, audio_low };
    uint32_t high[2] = { text_high, audio_high };
    uint32_t s;
    if (!sample_start) return 0u;
    for (s = 0u; s < 2u; ++s) {
        if (high[s] <= low[s] || pc < low[s] || pc >= high[s]) continue;
        ++owners;
        aligned_pc = pc & ~3u;
        if (aligned_pc < 32u) continue;
        start = aligned_pc - 32u;
        if ((start & 3u) || start < low[s] || start > high[s] ||
            high[s] - start < FM1_FAULT_RAM_CODE_BYTES)
            continue;
        candidate = start;
        ++matches;
    }
    if (owners != 1u || matches != 1u) return 0u;
    *sample_start = candidate;
    return 1u;
}

static inline void fm1_fault_ram_code_barrier(void)
{
    fm1_fault_popframe_barrier();
}

/* source_address must equal the checked sample_start. The source pointer is
 * word-aligned mapped memory from that fixed section; the copy stores bytes
 * in target little-endian instruction order. */
static inline uint32_t fm1_fault_ram_code_capture_words(
    volatile fm1_fault_ram_code_record_t *out, uint32_t cpu, uint32_t epoch,
    uint32_t pc, uint32_t text_low, uint32_t text_high,
    uint32_t audio_low, uint32_t audio_high, uint32_t source_address,
    const volatile uint32_t *source_words)
{
    uint32_t start, i, k;
    if (!out) return 0u;
    out->magic = 0u;
    fm1_fault_ram_code_barrier();
    if (cpu > 1u || !epoch || !source_words ||
        ((uintptr_t)source_words & 3u) ||
        !fm1_fault_ram_code_window(pc, text_low, text_high,
                                   audio_low, audio_high, &start) ||
        source_address != start)
        return 0u;
    out->version = FM1_FAULT_RAM_CODE_VERSION;
    out->byte_size = sizeof(fm1_fault_ram_code_record_t);
    out->fault_count = epoch;
    out->current_cpu_id = cpu;
    out->fault_pc = pc;
    out->sample_start = start;
    out->flags = FM1_FAULT_RAM_CODE_VALID;
    for (i = 0u; i < FM1_FAULT_RAM_CODE_BYTES / 4u; ++i) {
        uint32_t word = source_words[i];
        for (k = 0u; k < 4u; ++k)
            out->code[i * 4u + k] = (uint8_t)(word >> (8u * k));
    }
    fm1_fault_ram_code_barrier();
    out->magic = FM1_FAULT_RAM_CODE_MAGIC;
    fm1_fault_ram_code_barrier();
    return 1u;
}

static inline uint32_t fm1_fault_ram_code_record_valid(
    const volatile fm1_fault_ram_code_record_t *record,
    uint32_t expected_epoch, uint32_t expected_cpu, uint32_t expected_pc,
    uint32_t text_low, uint32_t text_high,
    uint32_t audio_low, uint32_t audio_high)
{
    uint32_t start;
    if (!record || record->magic != FM1_FAULT_RAM_CODE_MAGIC) return 0u;
    fm1_fault_ram_code_barrier();
    return
        record->version == FM1_FAULT_RAM_CODE_VERSION &&
        record->byte_size == sizeof(fm1_fault_ram_code_record_t) &&
        record->fault_count == expected_epoch && expected_epoch &&
        record->current_cpu_id == expected_cpu && expected_cpu <= 1u &&
        record->fault_pc == expected_pc &&
        record->flags == FM1_FAULT_RAM_CODE_VALID &&
        fm1_fault_ram_code_window(expected_pc, text_low, text_high,
                                  audio_low, audio_high, &start) &&
        record->sample_start == start;
}

static inline uint32_t fm1_fault_ram_code_chunk(
    const volatile fm1_fault_ram_code_record_t *record,
    uint32_t expected_epoch, uint32_t expected_cpu, uint32_t expected_pc,
    uint32_t text_low, uint32_t text_high,
    uint32_t audio_low, uint32_t audio_high,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)record;
    uint32_t i;
    if (!out || !requested || requested > 125u ||
        offset > sizeof(fm1_fault_ram_code_record_t) ||
        requested > sizeof(fm1_fault_ram_code_record_t) - offset ||
        !fm1_fault_ram_code_record_valid(record, expected_epoch, expected_cpu,
                                         expected_pc, text_low, text_high,
                                         audio_low, audio_high))
        return 0u;
    fm1_fault_ram_code_barrier();
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}
#endif

/* Optional CPU1-owner-only postmortem context. This record is appended after
 * the existing RAM-code sidecar and is read through the existing quiet 0x96
 * command as record kind 3. It has no playback-path state or work. */
#ifndef FM1_FAULT_C1_CONTEXT_SIDECAR
#define FM1_FAULT_C1_CONTEXT_SIDECAR 1
#endif
/* Capture-only selector; record storage and readback remain stable. */
#ifndef FM1_FAULT_C1_CONTEXT_CAPTURE
#define FM1_FAULT_C1_CONTEXT_CAPTURE 0
#endif
#if defined(FM1_DEVELOPMENT_DIAGNOSTICS) && !FM1_DEVELOPMENT_DIAGNOSTICS && FM1_FAULT_C1_CONTEXT_CAPTURE
#error "Distribution firmware cannot enable diagnostic C1 context capture"
#endif
#if FM1_FAULT_C1_CONTEXT_SIDECAR
#if !FM1_FAULT_RAM_CODE_SIDECAR
#error "CPU1 context sidecar must follow the existing RAM-code sidecar"
#endif
#define FM1_FAULT_C1_CONTEXT_MAGIC 0x46314331u /* "F1C1" */
#define FM1_FAULT_C1_CONTEXT_VERSION 1u
#define FM1_FAULT_C1_CONTEXT_STACK_BYTES 512u
#define FM1_FAULT_C1_CONTEXT_STACK_OFFSET 0u
#define FM1_FAULT_C1_CONTEXT_CODE_BYTES 32u
#define FM1_FAULT_C1_CONTEXT_STACK_VALID 1u
#define FM1_FAULT_C1_CONTEXT_CODE_VALID 2u
#define FM1_FAULT_C1_CONTEXT_CRC_VALID 4u
#define FM1_FAULT_C1_CONTEXT_VALID_MASK 7u
/* The firmware raw reader may skip deep metadata checks only after its caller
 * has claimed a quiescent snapshot and matched this record to the phase-2
 * CPU1 bundle. The host decoder remains responsible for strict validation. */
#ifndef FM1_FAULT_C1_CONTEXT_SHALLOW_PAGE_READ
#define FM1_FAULT_C1_CONTEXT_SHALLOW_PAGE_READ 1
#endif
#if FM1_FAULT_C1_CONTEXT_SHALLOW_PAGE_READ != 0 && \
    FM1_FAULT_C1_CONTEXT_SHALLOW_PAGE_READ != 1
#error "FM1_FAULT_C1_CONTEXT_SHALLOW_PAGE_READ must be zero or one"
#endif
#define FM1_FAULT_C1_CONTEXT_RAM_LOW 0x01c00000u
/* The architecture RAM-code arena ends before the reserved status window.
 * Target247 currently links through 0x01c078d4; using the arena cap supports
 * later linked images while still excluding status RAM. */
#define FM1_FAULT_C1_CONTEXT_RAM_HIGH 0x01c07f00u

typedef struct {
    uint32_t magic;             /* published last */
    uint32_t version;
    uint32_t byte_size;
    uint32_t fault_count;
    uint32_t current_cpu_id;
    uint32_t flags;
    uint32_t saved_usp;
    uint32_t stack_sample_base;
    uint32_t stack_sample_bytes;
    uint32_t caller_rets;
    uint32_t code_sample_start;
    uint32_t ram_text_bytes;
    uint32_t ram_text_crc32;
    uint32_t ram_audio_bytes;
    uint32_t ram_audio_crc32;
    uint32_t stack[FM1_FAULT_C1_CONTEXT_STACK_BYTES / 4u];
    uint32_t code[FM1_FAULT_C1_CONTEXT_CODE_BYTES / 4u];
} fm1_fault_c1_context_record_t;

typedef char fm1_fault_c1_context_record_is_604[
    (sizeof(fm1_fault_c1_context_record_t) == 604u) ? 1 : -1];

extern volatile fm1_fault_c1_context_record_t fm1_fault_c1_context_record;
void fm1_fault_cpu1_context_capture_owned(uint32_t epoch, uint32_t cpu);

/* Capture starts at saved USP so both observed +212 and +416 spill slots are
 * inside the 512-byte window. Require the full range; never clip at a guard. */
static inline uint32_t fm1_fault_c1_context_stack_window(
    uint32_t saved_usp, uint32_t user_low, uint32_t user_high,
    uint32_t *sample_base)
{
    uint32_t base;
    if (!sample_base || user_high < user_low || (saved_usp & 3u) ||
        (user_low & 3u) || (user_high & 3u) || saved_usp < user_low ||
        saved_usp > user_high ||
        saved_usp > 0xffffffffu - FM1_FAULT_C1_CONTEXT_STACK_OFFSET)
        return 0u;
    base = saved_usp + FM1_FAULT_C1_CONTEXT_STACK_OFFSET;
    if (base < saved_usp || base < user_low || base > user_high ||
        user_high - base < FM1_FAULT_C1_CONTEXT_STACK_BYTES)
        return 0u;
    *sample_base = base;
    return 1u;
}

/* Both executable RAM ranges must be within the architecture-bounded RAM-code
 * arena, ordered, aligned, and nonempty before any CRC/code read. */
static inline uint32_t fm1_fault_c1_context_ram_ranges_valid(
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high)
{
    return !(rt_low & 3u) && !(rt_high & 3u) && !(ra_low & 3u) &&
        !(ra_high & 3u) && rt_low >= FM1_FAULT_C1_CONTEXT_RAM_LOW &&
        rt_high <= FM1_FAULT_C1_CONTEXT_RAM_HIGH &&
        ra_low >= FM1_FAULT_C1_CONTEXT_RAM_LOW &&
        ra_high <= FM1_FAULT_C1_CONTEXT_RAM_HIGH && rt_low < rt_high &&
        ra_low < ra_high && rt_high <= ra_low;
}

static inline uint32_t fm1_fault_c1_context_crc32(
    const volatile uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xffffffffu;
    while (n--) {
        uint32_t bit;
        crc ^= *p++;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return ~crc;
}

/* Called only after each caller has invalidated the record and established
 * the payload guards. Keeping the fixed copies in one small helper avoids
 * duplicating this writer between the defensive host helper and device path. */
static inline void fm1_fault_c1_context_seal_payload(
    volatile fm1_fault_c1_context_record_t *out, uint32_t flags,
    const volatile uint32_t *stack_words,
    const volatile uint32_t *code_words)
{
    uint32_t i;
    for (i = 0u; i < FM1_FAULT_C1_CONTEXT_STACK_BYTES / 4u; ++i)
        out->stack[i] = (flags & FM1_FAULT_C1_CONTEXT_STACK_VALID) ?
            stack_words[i] : 0u;
    for (i = 0u; i < FM1_FAULT_C1_CONTEXT_CODE_BYTES / 4u; ++i)
        out->code[i] = (flags & FM1_FAULT_C1_CONTEXT_CODE_VALID) ?
            code_words[i] : 0u;
    fm1_fault_ram_code_barrier();
    out->magic = FM1_FAULT_C1_CONTEXT_MAGIC;
    fm1_fault_ram_code_barrier();
}

/* Defensive entry point for arbitrary callers. It validates the supplied
 * pointers and ranges, clears omitted payloads, then seals the record. */
static inline uint32_t fm1_fault_c1_context_capture_words(
    volatile fm1_fault_c1_context_record_t *out, uint32_t cpu, uint32_t epoch,
    uint32_t saved_usp, uint32_t caller_rets,
    uint32_t user_low, uint32_t user_high,
    const volatile uint32_t *stack_words,
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high,
    const volatile uint32_t *code_words,
    uint32_t ram_text_crc32, uint32_t ram_audio_crc32)
{
    uint32_t flags = 0u, stack_base = 0u, code_start = 0u;
    if (!out || cpu != 1u || !epoch) return 0u;
    out->magic = 0u;
    fm1_fault_ram_code_barrier();
    if (stack_words && !((uintptr_t)stack_words & 3u) &&
        fm1_fault_c1_context_stack_window(saved_usp, user_low, user_high,
                                          &stack_base))
        flags |= FM1_FAULT_C1_CONTEXT_STACK_VALID;
    if (code_words && !((uintptr_t)code_words & 3u) &&
        fm1_fault_ram_code_window(caller_rets, rt_low, rt_high,
                                  ra_low, ra_high, &code_start))
        flags |= FM1_FAULT_C1_CONTEXT_CODE_VALID;
    if (fm1_fault_c1_context_ram_ranges_valid(rt_low, rt_high,
                                             ra_low, ra_high))
        flags |= FM1_FAULT_C1_CONTEXT_CRC_VALID;

    out->version = FM1_FAULT_C1_CONTEXT_VERSION;
    out->byte_size = sizeof(fm1_fault_c1_context_record_t);
    out->fault_count = epoch;
    out->current_cpu_id = 1u;
    out->flags = flags;
    out->saved_usp = saved_usp;
    out->stack_sample_base = (flags & FM1_FAULT_C1_CONTEXT_STACK_VALID) ?
        stack_base : 0u;
    out->stack_sample_bytes = (flags & FM1_FAULT_C1_CONTEXT_STACK_VALID) ?
        FM1_FAULT_C1_CONTEXT_STACK_BYTES : 0u;
    out->caller_rets = caller_rets;
    out->code_sample_start = (flags & FM1_FAULT_C1_CONTEXT_CODE_VALID) ?
        code_start : 0u;
    out->ram_text_bytes = (flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) ?
        rt_high - rt_low : 0u;
    out->ram_text_crc32 = (flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) ?
        ram_text_crc32 : 0u;
    out->ram_audio_bytes = (flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) ?
        ra_high - ra_low : 0u;
    out->ram_audio_crc32 = (flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) ?
        ram_audio_crc32 : 0u;
    fm1_fault_c1_context_seal_payload(out, flags, stack_words, code_words);
    return 1u;
}

static inline uint32_t fm1_fault_c1_context_record_valid(
    const volatile fm1_fault_c1_context_record_t *record,
    uint32_t expected_epoch, uint32_t expected_rets,
    uint32_t user_low, uint32_t user_high,
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high)
{
    uint32_t sample_base, code_start;
    if (!record || record->magic != FM1_FAULT_C1_CONTEXT_MAGIC) return 0u;
    fm1_fault_ram_code_barrier();
    if (record->version != FM1_FAULT_C1_CONTEXT_VERSION ||
        record->byte_size != sizeof(fm1_fault_c1_context_record_t) ||
        !expected_epoch || record->fault_count != expected_epoch ||
        record->current_cpu_id != 1u ||
        (record->flags & ~FM1_FAULT_C1_CONTEXT_VALID_MASK) ||
        record->caller_rets != expected_rets)
        return 0u;
    if (record->flags & FM1_FAULT_C1_CONTEXT_STACK_VALID) {
        if (record->stack_sample_bytes != FM1_FAULT_C1_CONTEXT_STACK_BYTES ||
            !fm1_fault_c1_context_stack_window(record->saved_usp,
                user_low, user_high, &sample_base) ||
            record->stack_sample_base != sample_base)
            return 0u;
    } else if (record->stack_sample_base || record->stack_sample_bytes) {
        return 0u;
    }
    if (record->flags & FM1_FAULT_C1_CONTEXT_CODE_VALID) {
        if (!fm1_fault_ram_code_window(record->caller_rets, rt_low, rt_high,
                                       ra_low, ra_high, &code_start) ||
            record->code_sample_start != code_start)
            return 0u;
    } else if (record->code_sample_start) {
        return 0u;
    }
    if (record->flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) {
        if (!fm1_fault_c1_context_ram_ranges_valid(rt_low, rt_high,
                                                   ra_low, ra_high) ||
            record->ram_text_bytes != rt_high - rt_low ||
            record->ram_audio_bytes != ra_high - ra_low)
            return 0u;
    } else if (record->ram_text_bytes || record->ram_text_crc32 ||
               record->ram_audio_bytes || record->ram_audio_crc32) {
        return 0u;
    }
    return 1u;
}

static inline uint32_t fm1_fault_c1_context_chunk(
    const volatile fm1_fault_c1_context_record_t *record,
    uint32_t expected_epoch, uint32_t expected_rets,
    uint32_t user_low, uint32_t user_high,
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)record;
    uint32_t i;
    if (!out || !requested || requested > 125u ||
        offset > sizeof(fm1_fault_c1_context_record_t) ||
        requested > sizeof(fm1_fault_c1_context_record_t) - offset ||
        !fm1_fault_c1_context_record_valid(record, expected_epoch,
            expected_rets, user_low, user_high, rt_low, rt_high, ra_low,
            ra_high))
        return 0u;
    fm1_fault_ram_code_barrier();
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}

/* Compact raw reader for the fixed retained record. The diagnostic command
 * has already held a quiescent transaction and matched the CPU1 bundle epoch;
 * this helper checks only the record identity/header and the page bounds. It
 * deliberately copies opaque captured metadata without following any address
 * stored in the record. The host postmortem reader performs deep validation. */
static inline uint32_t fm1_fault_c1_context_chunk_shallow(
    const volatile fm1_fault_c1_context_record_t *record,
    uint32_t expected_epoch, uint32_t offset, uint32_t requested,
    uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)record;
    uint32_t i;
    if (!record || !out || !expected_epoch || !requested || requested > 125u ||
        offset > sizeof(fm1_fault_c1_context_record_t) ||
        requested > sizeof(fm1_fault_c1_context_record_t) - offset)
        return 0u;
    if (record->magic != FM1_FAULT_C1_CONTEXT_MAGIC) return 0u;
    fm1_fault_ram_code_barrier();
    if (record->version != FM1_FAULT_C1_CONTEXT_VERSION ||
        record->byte_size != sizeof(fm1_fault_c1_context_record_t) ||
        record->fault_count != expected_epoch || record->current_cpu_id != 1u ||
        (record->flags & ~FM1_FAULT_C1_CONTEXT_VALID_MASK))
        return 0u;
    fm1_fault_ram_code_barrier();
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}
#endif

/* `high` is exclusive. The eight-word window is centered on `sp`, with four
 * words on each side; require the complete aligned window to stay in bounds. */
static inline uint32_t fm1_fault_diag_stack_window_valid(uint32_t sp,
                                                          uint32_t low,
                                                          uint32_t high)
{
    if (high < low || (sp & 3u) || low >= 0xfffffff0u || high < 16u)
        return 0u;
    return sp >= low + 16u && sp <= high - 16u;
}

extern volatile fm1_fault_diag_t fm1_fault_diag;
extern volatile fm1_fault_diag_bundle_t fm1_fault_bundle;
extern volatile fm1_fault_diag_t fm1_fault_temp[2];
extern volatile uint32_t fm1_fault_phase[2];
extern volatile uint8_t fm1_fault_owner_lock;

/* Read the active PI32 core ID at the call site; kept in HAL so non-HAL
 * sources need not embed target-specific inline assembly. */
static inline __attribute__((always_inline)) uint32_t fm1_fault_diag_read_cpu_id(void)
{
#if defined(FM1_DEVICE)
    uint32_t id;
    __asm__ volatile("%0 = cnum" : "=r"(id) :: "memory");
    return id;
#else
    return 0u;
#endif
}

/* One-attempt SDK test-and-set arbitration. The host branch lets the
 * dual-arrival state-machine test exercise the same winner/loser contract. */
static inline __attribute__((always_inline)) uint32_t
fm1_fault_owner_try_acquire(volatile uint8_t *lock)
{
#if defined(FM1_DEVICE)
    uint32_t won;
    __asm__ volatile(
        "csync\n\t"
        "testset b[%1]\n\t"
        "ifeq goto 1f\n\t"
        "%0 = 1\n\t"
        "goto 2f\n"
        "1:\n\t"
        "%0 = 0\n"
        "2:\n\t"
        "csync\n\t"
        : "=&r"(won) : "r"(lock) : "memory");
    return won;
#else
    return __sync_lock_test_and_set(lock, 0x80u) == 0u;
#endif
}
void fm1_fault_diag_capture(uint32_t fault_count, const uint32_t frame[26],
                            const uint32_t c0_etm_pc[4]);
void fm1_fault_diag_capture_to(volatile fm1_fault_diag_t *out, uint32_t fault_count,
                               uint32_t cpu, const uint32_t frame[26],
                               const uint32_t fault_etm_pc[4]);
void fm1_fault_bundle_publish(uint32_t epoch, uint32_t owner_cpu,
                              uint32_t phase0, uint32_t phase1,
                              uint32_t context_in_audio, uint32_t context_ui_part,
                              uint32_t fx_progress_cpu0, uint32_t fx_progress_cpu1);
void fm1_fault_diag_live(uint32_t worker_cpu_id, uint32_t worker_state, uint32_t out[8]);

/* Pure bounded reader shared by firmware and host tests. Total SysEx reply
 * size includes the three-byte offset/count header, so each chunk is <=128. */
uint32_t fm1_fault_diag_chunk(const volatile fm1_fault_diag_t *snapshot,
                              uint32_t offset, uint32_t requested,
                              uint8_t out[128]);
uint32_t fm1_fault_bundle_chunk(const volatile fm1_fault_diag_bundle_t *snapshot,
                                uint32_t offset, uint32_t requested,
                                uint8_t out[128]);
