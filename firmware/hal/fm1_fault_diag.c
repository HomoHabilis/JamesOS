/* SPDX-License-Identifier: GPL-3.0-only
 * Read-only exception snapshot. Register offsets follow the pinned AC79 SDK
 * JL_TypeDef_corex2 / JL_TypeDef_q32DSP definitions in sdk-csfr.h. */
#include "fm1_fault_diag.h"
#include "fm1_fault_dual_code.h"
#include "fm1_power_read_diag.h"
#include "fm1_vendor360_diag.h"
#if defined(FM1_DEVICE)
#include "fm1_cpu1_worker.h"
#include "fm1_time.h"
extern uint32_t _ustack_lo[], _ustack_top[], _sstack_lo[], _sstack_top[];
#if FM1_FAULT_RAM_CODE_SIDECAR
extern uint32_t _rt_start[], _rt_end[], _ra_start[], _ra_end[];
#endif
#endif

#if defined(FM1_DEVICE)
volatile fm1_fault_diag_t fm1_fault_diag
    __attribute__((section(".noinit.cpu1_diag"), aligned(4), used));
#if FM1_FAULT_C0_POPFRAME_SIDECAR
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile fm1_fault_popframe_diag_t fm1_fault_popframe_diag
    __attribute__((section(".noinit.end"), aligned(4), used));
#else
volatile uint8_t fm1_fault_popframe_reserved[sizeof(fm1_fault_popframe_diag_t)]
    __attribute__((section(".noinit.end"), aligned(4), used));
#endif
#endif
#if FM1_FAULT_C1_TIMEOUT_SIDECAR
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile fm1_fault_c1_timeout_record_t fm1_fault_c1_timeout_record
    __attribute__((section(".noinit.end.c1_timeout"), aligned(4), used));
volatile fm1_fault_c0_stack_window_record_t fm1_fault_c0_stack_window_record
    __attribute__((section(".noinit.end.c0_stack_window"), aligned(4), used));
#else
volatile uint8_t fm1_fault_c1_timeout_reserved[sizeof(fm1_fault_c1_timeout_record_t)]
    __attribute__((section(".noinit.end.c1_timeout"), aligned(4), used));
volatile uint8_t fm1_fault_c0_stack_window_reserved[sizeof(fm1_fault_c0_stack_window_record_t)]
    __attribute__((section(".noinit.end.c0_stack_window"), aligned(4), used));
#endif
#endif
#if FM1_FAULT_RAM_CODE_SIDECAR
#if FM1_FAULT_RAM_CODE_CAPTURE_READ
volatile fm1_fault_ram_code_record_t fm1_fault_ram_code_record
    __attribute__((section(".noinit.end.ram_code"), aligned(4), used));
#else
/* Preserve the append-only retained layout without keeping a capture record. */
volatile uint8_t fm1_fault_ram_code_reserved[sizeof(fm1_fault_ram_code_record_t)]
    __attribute__((section(".noinit.end.ram_code"), aligned(4), used));
#endif
#endif
#if FM1_FAULT_C1_CONTEXT_SIDECAR
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile fm1_fault_c1_context_record_t fm1_fault_c1_context_record
    __attribute__((section(".noinit.end.c1_context"), aligned(4), used));
#else
volatile uint8_t fm1_fault_c1_context_reserved[sizeof(fm1_fault_c1_context_record_t)]
    __attribute__((section(".noinit.end.c1_context"), aligned(4), used));
#endif
#endif
volatile fm1_fault_diag_bundle_t fm1_fault_bundle
    __attribute__((section(".noinit.cpu1_fault_bundle"), aligned(4), used));
/* Per-core temporary slots are ordinary BSS, cleared before IRQ enable. They
 * are never shared between faulting cores. */
volatile fm1_fault_diag_t fm1_fault_temp[2]
    __attribute__((aligned(4), used));
volatile uint32_t fm1_fault_phase[2] __attribute__((aligned(4), used));
volatile uint8_t fm1_fault_owner_lock __attribute__((aligned(4), used));
#else
volatile fm1_fault_diag_t fm1_fault_diag;
#if FM1_FAULT_C0_POPFRAME_SIDECAR
volatile fm1_fault_popframe_diag_t fm1_fault_popframe_diag;
#endif
#if FM1_FAULT_C1_TIMEOUT_SIDECAR
volatile fm1_fault_c1_timeout_record_t fm1_fault_c1_timeout_record;
volatile fm1_fault_c0_stack_window_record_t fm1_fault_c0_stack_window_record;
#endif
#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ
volatile fm1_fault_ram_code_record_t fm1_fault_ram_code_record;
#endif
#if FM1_FAULT_C1_CONTEXT_SIDECAR
volatile fm1_fault_c1_context_record_t fm1_fault_c1_context_record;
#endif
volatile fm1_fault_diag_bundle_t fm1_fault_bundle;
volatile fm1_fault_diag_t fm1_fault_temp[2];
volatile uint32_t fm1_fault_phase[2];
volatile uint8_t fm1_fault_owner_lock;
#endif

#if defined(FM1_DEVICE)
#define FM1_COREX2_BASE          0x01eee000u /* csfr_base + 0xe000 */
#define FM1_Q32DSP_BASE          0x01eef000u /* SDK csfr_base 0x01ee0000 + 0xf000 */
#define FM1_Q32DSP_CORE_STRIDE   0x200u
#define FM1_Q32DSP_PCRS_OFFSET   (0x1fu * 4u)
#define FM1_Q32DSP_EMU_CON_OFFSET (0x34u * 4u)
#define FM1_Q32DSP_EMU_MSG_OFFSET (0x35u * 4u)
#define FM1_DBG_MSG_ADDR         (FM1_COREX2_BASE + 0x91u * 4u)
#define FM1_C1_CON_ADDR          (FM1_COREX2_BASE + 0x01u * 4u)
#define FM1_AXI_RD_INV_ID_ADDR   (FM1_COREX2_BASE + 0xdcu * 4u)
#define FM1_AXI_WR_INV_ID_ADDR   (FM1_COREX2_BASE + 0xddu * 4u)
#define FM1_MMU_ERR_WID_ADDR     (FM1_COREX2_BASE + 0xdeu * 4u)
#define FM1_MMU_ERR_RID_ADDR     (FM1_COREX2_BASE + 0xdfu * 4u)

enum {
    FM1_DIAG_STATUS_IDLE = 0u,
    FM1_DIAG_STATUS_RUNNING = 1u,
    FM1_DIAG_STATUS_DONE = 2u,
    FM1_DIAG_STATUS_BAD_TASK = 3u,
    FM1_DIAG_STATUS_WAIT_TIMEOUT = 4u
};

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
extern volatile fm1_cpu1_worker_diag_t fm1_cpu1_worker_diag;
#endif
extern fm1_cpu1_worker_mailbox_t fm1_cpu1_worker_mailbox;
extern volatile uint32_t fm1_cpu1_worker_fault;
extern uint32_t fm1_cpu1_worker_stack_status(uint32_t *user_used, uint32_t *system_used);

static inline uint32_t fm1_fault_diag_read(uint32_t addr)
{
    return *(volatile const uint32_t *)addr;
}

static inline uint32_t fm1_fault_diag_cnum(void)
{
    uint32_t id;
    __asm__ volatile("%0 = cnum" : "=r"(id) ::);
    return id;
}

#if FM1_FAULT_C1_TIMEOUT_SIDECAR && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
void fm1_fault_c1_timeout_capture_before_reset(void)
{
    uint32_t cpu = fm1_fault_diag_cnum();
    fm1_cpu1_worker_mailbox_t *m = &fm1_cpu1_worker_mailbox;
    if (cpu != 0u) return;
    (void)fm1_fault_c1_timeout_capture(
        &fm1_fault_c1_timeout_record, cpu, fm1_fault_phase[1], m->state,
        m->cpu_id, m->request_seq, m->done_seq, m->task_id,
        m->task_context, fm1_ticks(), &fm1_fault_temp[1]);
}

void fm1_fault_c0_stack_window_capture_from_frame(uint32_t fault_count,
                                                  const uint32_t frame[26])
{
    uint32_t cpu, saved_sp, low, high;
    const volatile uint32_t *source;
    if (!frame) {
        fm1_fault_c0_stack_window_record.magic = 0u;
        fm1_fault_popframe_barrier();
        return;
    }
    cpu = fm1_fault_diag_cnum();
    saved_sp = frame[24];
    low = (uint32_t)(uintptr_t)_sstack_lo;
    high = (uint32_t)(uintptr_t)_sstack_top;
    if (saved_sp > 0xfffffffbu -
            (FM1_FAULT_C0_SYSTEM_STACK_OFFSET + FM1_FAULT_C1_TIMEOUT_C0_WORDS * 4u)) {
        fm1_fault_c0_stack_window_record.magic = 0u;
        fm1_fault_popframe_barrier();
        return;
    }
    source = (const volatile uint32_t *)(uintptr_t)
        (saved_sp + FM1_FAULT_C0_SYSTEM_STACK_OFFSET);
    (void)fm1_fault_c0_stack_window_capture_abi(
        &fm1_fault_c0_stack_window_record, cpu, fault_count, frame[16],
        saved_sp, low, high, source, FM1_FAULT_C0_SYSTEM_STACK_VERSION);
}
#else
void fm1_fault_c1_timeout_capture_before_reset(void) { }
void fm1_fault_c0_stack_window_capture_from_frame(uint32_t fault_count,
                                                  const uint32_t frame[26])
{ (void)fault_count; (void)frame; }
#endif

static uint32_t fm1_fault_diag_copy_stack_window(uint32_t sp, uint32_t low,
                                                 uint32_t high, volatile uint32_t *base,
                                                 volatile uint32_t out[FM1_FAULT_DIAG_C0_STACK_WORDS])
{
    uint32_t i;
    if (!base || !out || !fm1_fault_diag_stack_window_valid(sp, low, high))
        return 0u;
    *base = sp - 4u * (FM1_FAULT_DIAG_C0_STACK_WORDS / 2u);
    for (i = 0u; i < FM1_FAULT_DIAG_C0_STACK_WORDS; ++i)
        out[i] = ((volatile const uint32_t *)(uintptr_t)*base)[i];
    return 1u;
}

void fm1_fault_diag_capture_to(volatile fm1_fault_diag_t *out, uint32_t fault_count,
                               uint32_t cpu, const uint32_t frame[26],
                               const uint32_t fault_etm_pc[4])
{
    uint32_t worker_state, worker_active, checked = 0u, i;
    uint32_t user_used = FM1_FAULT_DIAG_UNKNOWN;
    uint32_t system_used = FM1_FAULT_DIAG_UNKNOWN;
    uint32_t stack_status = FM1_FAULT_DIAG_UNKNOWN;

    if (!out) return;
#if FM1_FAULT_C0_POPFRAME_SIDECAR && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    /* A new C0 capture starts a fresh optional sidecar. CPU1 never touches it. */
    if (cpu == 0u) {
        fm1_fault_popframe_diag.magic = 0u;
        fm1_fault_popframe_barrier();
    }
#endif
    /* Invalidate first and publish the magic only after every field is stored. */
    out->magic = 0u;
    __asm__ volatile("csync" ::: "memory");
    out->version = FM1_FAULT_DIAG_VERSION;
    out->byte_size = sizeof(fm1_fault_diag_t);
    out->flags = 0u;
    out->fault_count = fault_count;
    out->current_cpu_id = cpu;
    out->current_pcrs = cpu < 2u ?
        fm1_fault_diag_read(FM1_Q32DSP_BASE + cpu * FM1_Q32DSP_CORE_STRIDE +
                            FM1_Q32DSP_PCRS_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
    out->dbg_msg = fm1_fault_diag_read(FM1_DBG_MSG_ADDR);
    out->axi_rd_inv_id = fm1_fault_diag_read(FM1_AXI_RD_INV_ID_ADDR);
    out->axi_wr_inv_id = fm1_fault_diag_read(FM1_AXI_WR_INV_ID_ADDR);
    out->prp_mmu_err_rid = fm1_fault_diag_read(FM1_MMU_ERR_RID_ADDR);
    out->prp_mmu_err_wid = fm1_fault_diag_read(FM1_MMU_ERR_WID_ADDR);
    out->c1_con = fm1_fault_diag_read(FM1_C1_CON_ADDR);
    out->cpu0_pcrs = fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_PCRS_OFFSET);
    out->cpu0_emu_con = fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_EMU_CON_OFFSET);
    out->cpu0_emu_msg = fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_EMU_MSG_OFFSET);
    out->c0_stack_flags = 0u;
    out->c0_saved_sp = FM1_FAULT_DIAG_UNKNOWN;
    out->c0_saved_usp = FM1_FAULT_DIAG_UNKNOWN;
    out->c0_saved_ssp = FM1_FAULT_DIAG_UNKNOWN;
    out->c0_sp_window_base = FM1_FAULT_DIAG_UNKNOWN;
    out->c0_usp_window_base = FM1_FAULT_DIAG_UNKNOWN;
    for (i = 0u; i < FM1_FAULT_DIAG_C0_STACK_WORDS; ++i) {
        out->c0_sp_words[i] = FM1_FAULT_DIAG_UNKNOWN;
        out->c0_usp_words[i] = FM1_FAULT_DIAG_UNKNOWN;
    }
    for (i = 0u; i < 4u; ++i)
        out->c0_etm_pc[i] = fault_etm_pc ? fault_etm_pc[i] : FM1_FAULT_DIAG_UNKNOWN;
    /* Only capture CPU0 stack contents, and only from the fixed linker ranges.
     * f[24] is SP, f[22] is USP and f[23] is SSP in fm1_irq.h. */
    if (cpu == 0u && frame) {
        uint32_t user_low = (uint32_t)(uintptr_t)_ustack_lo;
        uint32_t user_high = (uint32_t)(uintptr_t)_ustack_top;
        uint32_t system_low = (uint32_t)(uintptr_t)_sstack_lo;
        uint32_t system_high = (uint32_t)(uintptr_t)_sstack_top;
        uint32_t sp = frame[24], usp = frame[22], ssp = frame[23];
#if FM1_FAULT_C0_POPFRAME_SIDECAR && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
        uint32_t pop_frame_sp;
        if (fm1_fault_popframe_range(sp, system_low, system_high, &pop_frame_sp))
            (void)fm1_fault_popframe_capture(
                &fm1_fault_popframe_diag, cpu, fault_count, sp, system_low,
                system_high, (const volatile uint32_t *)(uintptr_t)pop_frame_sp);
        else
            (void)fm1_fault_popframe_capture(&fm1_fault_popframe_diag, cpu,
                                             fault_count, sp, system_low,
                                             system_high, (const volatile uint32_t *)0);
#endif
        out->c0_stack_flags = 1u;
        out->c0_saved_sp = sp;
        out->c0_saved_usp = usp;
        out->c0_saved_ssp = ssp;
        if (ssp >= system_low && ssp <= system_high)
            out->c0_stack_flags |= 8u;
        if (fm1_fault_diag_copy_stack_window(sp, user_low, user_high,
                                             &out->c0_sp_window_base,
                                             out->c0_sp_words) ||
            fm1_fault_diag_copy_stack_window(sp, system_low, system_high,
                                             &out->c0_sp_window_base,
                                             out->c0_sp_words))
            out->c0_stack_flags |= 2u;
        if (fm1_fault_diag_copy_stack_window(usp, user_low, user_high,
                                             &out->c0_usp_window_base,
                                             out->c0_usp_words))
            out->c0_stack_flags |= 4u;
        if (fault_etm_pc) out->c0_stack_flags |= 16u;
    }
    worker_state = fm1_cpu1_worker_mailbox.state;
    worker_active = fm1_cpu1_worker_mailbox.cpu_id == 1u &&
        (worker_state == FM1_CPU1_WORKER_READY || worker_state == FM1_CPU1_WORKER_RUNNING ||
         worker_state == FM1_CPU1_WORKER_PARKED) && !(out->c1_con & 2u);
    out->cpu1_pcrs = worker_active ?
        fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_CORE_STRIDE + FM1_Q32DSP_PCRS_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
    out->cpu1_emu_con = worker_active ?
        fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_CORE_STRIDE + FM1_Q32DSP_EMU_CON_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
    out->cpu1_emu_msg = (worker_active || cpu == 1u) ?
        fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_CORE_STRIDE + FM1_Q32DSP_EMU_MSG_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
    out->worker_state = worker_state;
    out->worker_cpu_id = fm1_cpu1_worker_mailbox.cpu_id;
    out->worker_request_seq = fm1_cpu1_worker_mailbox.request_seq;
    out->worker_done_seq = fm1_cpu1_worker_mailbox.done_seq;
    out->worker_task_id = fm1_cpu1_worker_mailbox.task_id;
    out->worker_task_context = fm1_cpu1_worker_mailbox.task_context;
    out->worker_result = fm1_cpu1_worker_mailbox.result;
    out->worker_park_request = fm1_cpu1_worker_mailbox.park_request;
    out->worker_park_ack = fm1_cpu1_worker_mailbox.park_ack;
    out->worker_heartbeat = fm1_cpu1_worker_mailbox.heartbeat;
    out->worker_fault = fm1_cpu1_worker_fault;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    out->wait_timeout_count = fm1_cpu1_worker_diag.wait_timeout_count;
    out->last_job_status = fm1_cpu1_worker_diag.last_job_status;
    out->last_job_task = fm1_cpu1_worker_diag.last_job_task;
    out->last_job_context = fm1_cpu1_worker_diag.last_job_context;
    out->last_job_elapsed_ticks = fm1_cpu1_worker_diag.last_job_elapsed_ticks;
    out->last_job_start_ticks = fm1_cpu1_worker_diag.last_job_start_ticks;
    out->last_job_sequence = fm1_cpu1_worker_diag.last_job_sequence;
    out->last_job_result = fm1_cpu1_worker_diag.last_job_result;
    out->last_wait_status = fm1_cpu1_worker_diag.last_wait_status;
    out->last_wait_task = fm1_cpu1_worker_diag.last_wait_task;
    out->last_wait_sequence = fm1_cpu1_worker_diag.last_wait_sequence;
    out->last_wait_context = fm1_cpu1_worker_diag.last_wait_context;
    out->last_wait_elapsed_ticks = fm1_cpu1_worker_diag.last_wait_elapsed_ticks;
#else
    /* Retain the fault-record schema and mark absent development telemetry. */
    out->wait_timeout_count = out->last_job_status = out->last_job_task =
        out->last_job_context = out->last_job_elapsed_ticks =
        out->last_job_start_ticks = out->last_job_sequence = out->last_job_result =
        out->last_wait_status = out->last_wait_task = out->last_wait_sequence =
        out->last_wait_context = out->last_wait_elapsed_ticks = FM1_FAULT_DIAG_UNKNOWN;
#endif

    /* Stack contents are only inspected after CPU1 has already acknowledged PARKED. */
    if (worker_state == FM1_CPU1_WORKER_PARKED &&
        fm1_cpu1_worker_mailbox.cpu_id == 1u &&
        fm1_cpu1_worker_mailbox.park_request == 1u &&
        fm1_cpu1_worker_mailbox.park_ack == 1u &&
        fm1_cpu1_worker_mailbox.request_seq == fm1_cpu1_worker_mailbox.done_seq) {
        checked = 1u;
        stack_status = fm1_cpu1_worker_stack_status(&user_used, &system_used);
    }
    out->flags = checked;
    out->stack_status = stack_status;
    out->user_stack_used = user_used;
    out->system_stack_used = system_used;
    {
        uint32_t i;
        for (i = 0u; i < 26u; ++i)
            out->fault_frame[i] = frame ? frame[i] : FM1_FAULT_DIAG_UNKNOWN;
    }
    __asm__ volatile("csync" ::: "memory");
    out->magic = FM1_FAULT_DIAG_MAGIC;
    __asm__ volatile("csync" ::: "memory");
}

void fm1_fault_diag_capture(uint32_t fault_count, const uint32_t frame[26],
                            const uint32_t c0_etm_pc[4])
{
    fm1_fault_diag_capture_to(&fm1_fault_diag, fault_count, fm1_fault_diag_cnum(),
                              frame, c0_etm_pc);
}

#if defined(FM1_DEVICE)
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile fm1_power_fault_record_t fm1_power_fault_record
    __attribute__((section(".noinit.end.power_read"), aligned(4), used));
void fm1_power_fault_capture_owned(uint32_t epoch, uint32_t cpu)
{
    if (fm1_power_read_entry_ready != FM1_POWER_READ_READY) {
        fm1_power_fault_record.magic = 0u;
        fm1_fault_ram_code_barrier();
        return;
    }
    (void)fm1_power_fault_publish(&fm1_power_fault_record, &fm1_fault_bundle,
                                  epoch, cpu, &fm1_power_read_entry);
}
#else
volatile uint8_t fm1_power_fault_reserved[sizeof(fm1_power_fault_record_t)]
    __attribute__((section(".noinit.end.power_read"), aligned(4), used));
#endif
#endif

#if defined(FM1_DEVICE)
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile fm1_vpd_fault_t fm1_vpd_fault
    __attribute__((section(".noinit.end.vendor360"), aligned(4), used));
#else
volatile uint8_t fm1_vpd_fault_reserved[sizeof(fm1_vpd_fault_t)]
    __attribute__((section(".noinit.end.vendor360"), aligned(4), used));
#endif
volatile fm1_vpd_attempt_t fm1_vpd_attempts[2]
    __attribute__((section(".noinit.end.vendor360"), aligned(4), used));
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
void fm1_vpd_fault_capture_owned(uint32_t epoch,uint32_t cpu)
{
    if(fm1_vpd_ready!=FM1_VPD_READY) {
        fm1_vpd_fault.magic=0u;fm1_fault_ram_code_barrier();return;
    }
    (void)fm1_vpd_fault_publish(&fm1_vpd_fault,&fm1_fault_bundle,epoch,cpu,&fm1_vpd_report);
}
#endif
#endif

#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ && defined(FM1_DEVICE)
volatile fm1_fault_dual_code_record_t fm1_fault_dual_code_record
    __attribute__((section(".noinit.end.dual_code"), aligned(4), used));

void fm1_fault_dual_code_capture_owned(uint32_t epoch, uint32_t cpu)
{
    uint32_t c, pc, start;
    uint32_t rt_low = (uint32_t)(uintptr_t)_rt_start;
    uint32_t rt_high = (uint32_t)(uintptr_t)_rt_end;
    uint32_t ra_low = (uint32_t)(uintptr_t)_ra_start;
    uint32_t ra_high = (uint32_t)(uintptr_t)_ra_end;
    uint32_t source_address[2] = { 0u, 0u };
    const volatile uint32_t *source[2] = { 0, 0 };
    fm1_fault_dual_code_record.magic = 0u;
    fm1_fault_ram_code_barrier();
    if (fm1_fault_diag_cnum() != cpu) return;
    for (c = 0u; c < 2u; ++c) {
        if (!fm1_fault_dual_slot_identity(&fm1_fault_bundle, c, epoch, &pc) ||
            !fm1_fault_ram_code_window(pc, rt_low, rt_high, ra_low, ra_high, &start)) continue;
        source_address[c] = start;
        source[c] = (const volatile uint32_t *)(uintptr_t)start;
    }
    (void)fm1_fault_dual_code_capture(&fm1_fault_dual_code_record, &fm1_fault_bundle,
            epoch, cpu, fm1_early_p33_snapshot, rt_low, rt_high, ra_low, ra_high,
            source_address, source);
}
#elif FM1_FAULT_RAM_CODE_SIDECAR && defined(FM1_DEVICE)
/* Keep subsequent retained power/fault records at their development addresses. */
volatile uint8_t fm1_fault_dual_code_reserved[sizeof(fm1_fault_dual_code_record_t)]
    __attribute__((section(".noinit.end.dual_code"), aligned(4), used));
#endif

#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ && defined(FM1_DEVICE)
void fm1_fault_ram_code_capture_owned(uint32_t epoch, uint32_t cpu,
                                      uint32_t fault_pc)
{
    uint32_t published_epoch, published_owner, published_pc, sample_start;
    uint32_t text_low = (uint32_t)(uintptr_t)_rt_start;
    uint32_t text_high = (uint32_t)(uintptr_t)_rt_end;
    uint32_t audio_low = (uint32_t)(uintptr_t)_ra_start;
    uint32_t audio_high = (uint32_t)(uintptr_t)_ra_end;

    /* The elected owner calls only after the primary bundle and legacy crash
     * record are published. Invalidate the prior snippet before any RAM read. */
    fm1_fault_ram_code_record.magic = 0u;
    fm1_fault_ram_code_barrier();
    if (fm1_fault_diag_cnum() != cpu || cpu > 1u || !epoch ||
        !fm1_fault_ram_code_bundle_identity(
            &fm1_fault_bundle, &published_epoch, &published_owner,
            &published_pc) ||
        published_epoch != epoch || published_owner != cpu ||
        published_pc != fault_pc ||
        !fm1_fault_ram_code_window(fault_pc, text_low, text_high,
                                   audio_low, audio_high, &sample_start))
        return;
    (void)fm1_fault_ram_code_capture_words(
        &fm1_fault_ram_code_record, cpu, epoch, fault_pc,
        text_low, text_high, audio_low, audio_high, sample_start,
        (const volatile uint32_t *)(uintptr_t)sample_start);
}
#elif FM1_FAULT_RAM_CODE_SIDECAR
void fm1_fault_ram_code_capture_owned(uint32_t epoch, uint32_t cpu,
                                      uint32_t fault_pc)
{ (void)epoch; (void)cpu; (void)fault_pc; }
#endif

#if FM1_FAULT_C1_CONTEXT_SIDECAR && defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
extern uint8_t fm1_cpu1_worker_ustack[0x2000];
extern uint32_t _fm1_cpu1_worker_ustack_top[];

/* Keep one CRC implementation in XIP instead of inlining two copies into the
 * owner path. It runs only after the fatal snapshot is sealed. */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static uint32_t fm1_fault_c1_context_crc32_owned(
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

/* Same reflected CRC-32/ISO-HDLC used by the boot image verifier. This runs
 * only after a fatal C1 exception, over fixed linker ranges, and is kept in
 * XIP; it is never called from an audio callback. */
void fm1_fault_cpu1_context_capture_owned(uint32_t epoch, uint32_t cpu)
{
    uint32_t published_epoch, owner, fault_pc, saved_usp, caller_rets;
    uint32_t user_low, user_high, rt_low, rt_high, ra_low, ra_high;
    uint32_t rt_crc = 0u, ra_crc = 0u, stack_base = 0u, code_start = 0u;
    uint32_t flags = 0u;
    const volatile uint32_t *stack_words = 0;
    const volatile uint32_t *code_words = 0;
    const volatile fm1_fault_diag_t *slot;
    volatile fm1_fault_c1_context_record_t *out =
        &fm1_fault_c1_context_record;

    if (cpu != 1u || fm1_fault_diag_cnum() != 1u || !epoch) return;
    fm1_fault_c1_context_record.magic = 0u;
    fm1_fault_ram_code_barrier();
    if (!fm1_fault_ram_code_bundle_identity(
            &fm1_fault_bundle, &published_epoch, &owner, &fault_pc) ||
        published_epoch != epoch || owner != 1u)
        return;

    slot = &fm1_fault_bundle.cpu[1];
    saved_usp = slot->fault_frame[22];
    caller_rets = slot->fault_frame[25];
    user_low = (uint32_t)(uintptr_t)fm1_cpu1_worker_ustack;
    user_high = (uint32_t)(uintptr_t)_fm1_cpu1_worker_ustack_top;
    rt_low = (uint32_t)(uintptr_t)_rt_start;
    rt_high = (uint32_t)(uintptr_t)_rt_end;
    ra_low = (uint32_t)(uintptr_t)_ra_start;
    ra_high = (uint32_t)(uintptr_t)_ra_end;

    if (fm1_fault_c1_context_stack_window(saved_usp, user_low, user_high,
                                          &stack_base)) {
        stack_words = (const volatile uint32_t *)(uintptr_t)stack_base;
    }
    if (fm1_fault_ram_code_window(caller_rets, rt_low, rt_high,
                                  ra_low, ra_high, &code_start)) {
        code_words = (const volatile uint32_t *)(uintptr_t)code_start;
    }
    if (fm1_fault_c1_context_ram_ranges_valid(rt_low, rt_high,
                                             ra_low, ra_high)) {
        rt_crc = fm1_fault_c1_context_crc32_owned(
            (const volatile uint8_t *)(uintptr_t)rt_low, rt_high - rt_low);
        ra_crc = fm1_fault_c1_context_crc32_owned(
            (const volatile uint8_t *)(uintptr_t)ra_low, ra_high - ra_low);
        flags |= FM1_FAULT_C1_CONTEXT_CRC_VALID;
    }

    /* The owner just proved each non-null range above. Do not pass those
     * values through capture_words: its public defensive helper intentionally
     * repeats the same windows/range checks for arbitrary host callers. */
    if (stack_words && !((uintptr_t)stack_words & 3u))
        flags |= FM1_FAULT_C1_CONTEXT_STACK_VALID;
    if (code_words && !((uintptr_t)code_words & 3u))
        flags |= FM1_FAULT_C1_CONTEXT_CODE_VALID;

    /* Keep the existing invalidate/barrier before writing, and publish the
     * same 604-byte record with its magic word last. */
    out->magic = 0u;
    fm1_fault_ram_code_barrier();
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
        rt_crc : 0u;
    out->ram_audio_bytes = (flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) ?
        ra_high - ra_low : 0u;
    out->ram_audio_crc32 = (flags & FM1_FAULT_C1_CONTEXT_CRC_VALID) ?
        ra_crc : 0u;
    fm1_fault_c1_context_seal_payload(out, flags, stack_words, code_words);
    (void)fault_pc; /* The RAM-code sidecar records this; context binds RETS. */
}
#elif FM1_FAULT_C1_CONTEXT_SIDECAR
void fm1_fault_cpu1_context_capture_owned(uint32_t epoch, uint32_t cpu)
{ (void)epoch; (void)cpu; }
#endif

void fm1_fault_bundle_publish(uint32_t epoch, uint32_t owner_cpu,
                              uint32_t phase0, uint32_t phase1,
                              uint32_t context_in_audio, uint32_t context_ui_part,
                              uint32_t fx_progress_cpu0, uint32_t fx_progress_cpu1)
{
    uint32_t i, valid = 0u;
    volatile uint32_t *dst;
    const volatile uint32_t *src;
    if (owner_cpu > 1u || !epoch) return;
    fm1_fault_bundle.magic = 0u;
    __asm__ volatile("csync" ::: "memory");
    fm1_fault_bundle.version = FM1_FAULT_BUNDLE_VERSION;
    fm1_fault_bundle.byte_size = sizeof(fm1_fault_diag_bundle_t);
    fm1_fault_bundle.epoch = epoch;
    fm1_fault_bundle.valid_mask = 0u;
    fm1_fault_bundle.owner_cpu = owner_cpu;
    fm1_fault_bundle.cpu0_phase = phase0;
    fm1_fault_bundle.cpu1_phase = phase1;
    fm1_fault_bundle.context_in_audio = context_in_audio;
    fm1_fault_bundle.context_ui_part = context_ui_part;
    fm1_fault_bundle.fx_progress[0] = fx_progress_cpu0;
    fm1_fault_bundle.fx_progress[1] = fx_progress_cpu1;
    if (phase0 == 2u && fm1_fault_temp[0].fault_count == epoch &&
        fm1_fault_temp[0].current_cpu_id == 0u &&
        fm1_fault_temp[0].magic == FM1_FAULT_DIAG_MAGIC) {
        dst = (volatile uint32_t *)&fm1_fault_bundle.cpu[0];
        src = (const volatile uint32_t *)&fm1_fault_temp[0];
        for (i = 0u; i < sizeof(fm1_fault_diag_t) / sizeof(uint32_t); ++i) dst[i] = src[i];
        valid |= 1u;
    } else {
        dst = (volatile uint32_t *)&fm1_fault_bundle.cpu[0];
        for (i = 0u; i < sizeof(fm1_fault_diag_t) / sizeof(uint32_t); ++i)
            dst[i] = FM1_FAULT_DIAG_UNKNOWN;
    }
    if (phase1 == 2u && fm1_fault_temp[1].fault_count == epoch &&
        fm1_fault_temp[1].current_cpu_id == 1u &&
        fm1_fault_temp[1].magic == FM1_FAULT_DIAG_MAGIC) {
        dst = (volatile uint32_t *)&fm1_fault_bundle.cpu[1];
        src = (const volatile uint32_t *)&fm1_fault_temp[1];
        for (i = 0u; i < sizeof(fm1_fault_diag_t) / sizeof(uint32_t); ++i) dst[i] = src[i];
        valid |= 2u;
    } else {
        dst = (volatile uint32_t *)&fm1_fault_bundle.cpu[1];
        for (i = 0u; i < sizeof(fm1_fault_diag_t) / sizeof(uint32_t); ++i)
            dst[i] = FM1_FAULT_DIAG_UNKNOWN;
    }
    if (!(valid & (1u << owner_cpu))) return;
    fm1_fault_bundle.valid_mask = valid;
    __asm__ volatile("csync" ::: "memory");
    fm1_fault_bundle.magic = FM1_FAULT_BUNDLE_MAGIC;
    __asm__ volatile("csync" ::: "memory");
}

/* Bounded read-only live snapshot. CPU1 q32 registers are read only while the
 * resident worker has identified itself and the hardware reset bit is clear. */
void fm1_fault_diag_live(uint32_t worker_cpu_id, uint32_t worker_state, uint32_t out[8])
{
    uint32_t c1_con, active;
    if (!out) return;
#if defined(FM1_DEVICE)
    c1_con = fm1_fault_diag_read(FM1_C1_CON_ADDR);
    active = worker_cpu_id == 1u &&
        (worker_state == FM1_CPU1_WORKER_READY || worker_state == FM1_CPU1_WORKER_RUNNING ||
         worker_state == FM1_CPU1_WORKER_PARKED) && !(c1_con & 2u);
    out[0] = fm1_fault_diag_cnum();
    out[1] = c1_con;
    out[2] = fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_PCRS_OFFSET);
    out[3] = active ? fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_CORE_STRIDE + FM1_Q32DSP_PCRS_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
    out[4] = fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_EMU_CON_OFFSET);
    out[5] = active ? fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_CORE_STRIDE + FM1_Q32DSP_EMU_CON_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
    out[6] = fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_EMU_MSG_OFFSET);
    out[7] = active ? fm1_fault_diag_read(FM1_Q32DSP_BASE + FM1_Q32DSP_CORE_STRIDE + FM1_Q32DSP_EMU_MSG_OFFSET) : FM1_FAULT_DIAG_UNKNOWN;
#else
    (void)worker_cpu_id; (void)worker_state;
    for (uint32_t i = 0; i < 8u; ++i) out[i] = FM1_FAULT_DIAG_UNKNOWN;
#endif
}
#endif

uint32_t fm1_fault_diag_chunk(const volatile fm1_fault_diag_t *snapshot,
                              uint32_t offset, uint32_t requested,
                              uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)snapshot;
    uint32_t i, count;
    if (!snapshot || !out || snapshot->magic != FM1_FAULT_DIAG_MAGIC ||
        snapshot->version != FM1_FAULT_DIAG_VERSION ||
        snapshot->byte_size != sizeof(fm1_fault_diag_t) ||
        !requested || requested > 125u || offset > sizeof(fm1_fault_diag_t) ||
        requested > sizeof(fm1_fault_diag_t) - offset)
        return 0u;
    count = requested;
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)count;
    for (i = 0; i < count; ++i) out[3u + i] = bytes[offset + i];
    return count + 3u;
}

uint32_t fm1_fault_bundle_chunk(const volatile fm1_fault_diag_bundle_t *snapshot,
                                uint32_t offset, uint32_t requested,
                                uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)snapshot;
    uint32_t i, count, valid;
    if (!snapshot || !out || snapshot->magic != FM1_FAULT_BUNDLE_MAGIC ||
        snapshot->version != FM1_FAULT_BUNDLE_VERSION ||
        snapshot->byte_size != sizeof(fm1_fault_diag_bundle_t) ||
        snapshot->owner_cpu > 1u || !snapshot->epoch ||
        !requested || requested > 125u || offset > sizeof(fm1_fault_diag_bundle_t) ||
        requested > sizeof(fm1_fault_diag_bundle_t) - offset)
        return 0u;
    valid = snapshot->valid_mask;
    if (valid & ~3u) return 0u;
    if ((valid & 1u) &&
        (snapshot->cpu0_phase != 2u ||
         snapshot->cpu[0].magic != FM1_FAULT_DIAG_MAGIC ||
         snapshot->cpu[0].version != FM1_FAULT_DIAG_VERSION ||
         snapshot->cpu[0].byte_size != sizeof(fm1_fault_diag_t) ||
         snapshot->cpu[0].fault_count != snapshot->epoch ||
         snapshot->cpu[0].current_cpu_id != 0u))
        return 0u;
    if ((valid & 2u) &&
        (snapshot->cpu1_phase != 2u ||
         snapshot->cpu[1].magic != FM1_FAULT_DIAG_MAGIC ||
         snapshot->cpu[1].version != FM1_FAULT_DIAG_VERSION ||
         snapshot->cpu[1].byte_size != sizeof(fm1_fault_diag_t) ||
         snapshot->cpu[1].fault_count != snapshot->epoch ||
         snapshot->cpu[1].current_cpu_id != 1u))
        return 0u;
    if (!valid || !(valid & (1u << snapshot->owner_cpu))) return 0u;
    count = requested;
    out[0] = (uint8_t)offset;
    out[1] = (uint8_t)(offset >> 8);
    out[2] = (uint8_t)count;
    for (i = 0; i < count; ++i) out[3u + i] = bytes[offset + i];
    return count + 3u;
}
