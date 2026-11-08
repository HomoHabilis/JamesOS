/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 interrupt / exception HAL.
 *
 *   fm1_irq_init()        first thing in cstart: all ICFG off, pendings and
 *                         exception causes cleared, all 128 vectors -> fatal
 *                         stubs (fm1_vec.S), vector 1 (CPU
 *                         exception) enabled at prio 7, ETM on, div0 trap
 *                         off (see below).
 *   fm1_irq_attach(n, h, prio)   h = asm wrapper (fm1_isr.S), prio 0..7
 *   fm1_irq_enable_all()  icfg bit 8 + sti, after every source is set up
 *
 * The application supplies fm1_fault(const fm1_crash_t *) which reports the
 * crash (LCD) and resets; the record survives in .noinit for the next boot.
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_fault_diag.h"
#include "fm1_fault_dual_code.h"
#include "fm1_vendor360_diag.h"
#include "fm1_power_read_diag.h"
#include "instrument_fx_progress.h"

#define FM1_VEC ((volatile uint32_t *)0x01C7FE00u)
#define FM1_ICFG(n) (*(volatile uint32_t *)(0x1EEF100u + 4u * ((n) >> 3)))
#define FM1_IPND(i) (*(volatile uint32_t *)(0x1EEF180u + 4u * (i)))
#define FM1_ILAT_SET (*(volatile uint32_t *)0x1EEF1A0u)
#define FM1_ILAT_CLR (*(volatile uint32_t *)0x1EEF1A4u)
#define FM1_EMU_CON (*(volatile uint32_t *)0x1EEF0D0u)
#define FM1_EMU_MSG (*(volatile uint32_t *)0x1EEF0D4u)
#define FM1_ETM_CON (*(volatile uint32_t *)0x1EEF1C0u)
#define FM1_ETM_PC(i) (*(volatile uint32_t *)(0x1EEF1C4u + 4u * (i)))
#define FM1_DBG_WR_EN (*(volatile uint32_t *)0x1EEE240u)
#define FM1_DBG_MSG (*(volatile uint32_t *)0x1EEE244u)
#define FM1_DBG_MSG_CLR (*(volatile uint32_t *)0x1EEE248u)
#define FM1_DBG_EN (*(volatile uint32_t *)0x1EEE340u)

enum { FM1_IRQ_EXCEPTION = 1, FM1_IRQ_ALNK0 = 11, FM1_IRQ_SPI1 = 16, FM1_IRQ_UART1 = 20,
       FM1_IRQ_SARADC = 24, FM1_IRQ_SPI2 = 37, FM1_IRQ_TIMER5 = 63, FM1_IRQ_SOFT0 = 120 };

static inline uint32_t fm1_icfg(void) { uint32_t v; __asm__ volatile("%0 = icfg" : "=r"(v)); return v; }
static inline void fm1_icfg_set(uint32_t v) { __asm__ volatile("icfg = %0" ::"r"(v) : "memory"); }
static inline void fm1_irq_off(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void fm1_irq_on(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }

#define FM1_CRASH_MAGIC 0x43525348u          /* "CRSH" */
/* Diagnostic-only ablation: avoid an unconditional peer wait when the other
 * core has not entered its fatal capture path. Default preserves old timing. */
#ifndef FM1_FAULT_PEER_WAIT_IF_STARTED
#define FM1_FAULT_PEER_WAIT_IF_STARTED 1
#endif
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;                          /* consecutive crashes < 3 s after boot */
} fm1_crash_t;
/* Not static: .noinit holds whatever the last run (or power-on) left, and a
 * file-local object would let the compiler assume its zero initial value and
 * fold the magic check away. */
fm1_crash_t fm1_crash __attribute__((section(".noinit")));
extern volatile fm1_fault_diag_t fm1_fault_temp[2];
extern volatile uint32_t fm1_fault_phase[2];
extern volatile uint8_t fm1_fault_owner_lock;

extern const char fm1_fatal_stubs[];
static void fm1_fault(const fm1_crash_t *c);   /* application: report, then reset */

static void fm1_irq_init(void)
{
    uint32_t i;
    for (i = 0; i < 32u; i++)
        *(volatile uint32_t *)(0x1EEF100u + 4u * i) = 0;   /* every source off (CPU0 bank) */
    FM1_ILAT_CLR = 0xFFu;
    FM1_EMU_MSG = 0xFFFFFFFFu;
    if (!(FM1_DBG_WR_EN & 1u))
        FM1_DBG_WR_EN = 0xE7u;                              /* unlock */
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    for (i = 0; i < 128u; i++)
        FM1_VEC[i] = (uint32_t)(uintptr_t)(fm1_fatal_stubs + 6u * i);
    FM1_ICFG(1) = (FM1_ICFG(1) & ~0xF0u) | 0xF0u;           /* exception: enable, prio 7 */
    /* div0 trap off (cleared, in case a reset kept it from a build that armed it), as SLOOP 2.4 / Felucca 1.0.3.1 (#61,
     * after PR #111 by spinkham): this compiler treats a divide as harmless and may run a loop-invariant one ahead of the
     * test that guards it. Off, such a divide gives a value nobody uses; the cost: a real divide by 0 gives a wrong value
     * instead of a crash report (the host tests run with -fsanitize=integer-divide-by-zero to catch one) */
    FM1_EMU_CON &= ~(1u << 2);
    FM1_ETM_CON |= 1u;                                      /* branch trace for the report */
}

static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)   /* IRQs off */
{
    uint32_t sh = (n & 7u) * 4u;
    FM1_VEC[n] = (uint32_t)(uintptr_t)h;
    FM1_ICFG(n) = (FM1_ICFG(n) & ~(0xFu << sh)) | ((((prio & 7u) << 1) | 1u) << sh);
}
static void fm1_irq_mask(uint32_t n) { FM1_ICFG(n) &= ~(1u << ((n & 7u) * 4u)); }

static void fm1_irq_enable_all(void)
{
    fm1_icfg_set(fm1_icfg() | 0x100u);
    fm1_irq_on();
}

static inline uint32_t fm1_fault_cpu_id(void)
{
    uint32_t cpu;
    __asm__ volatile("%0 = cnum" : "=r"(cpu) :: "memory");
    return cpu;
}

static inline void fm1_fault_copy_diag(volatile fm1_fault_diag_t *dst,
                                       const volatile fm1_fault_diag_t *src)
{
    uint32_t i;
    volatile uint32_t *d = (volatile uint32_t *)dst;
    const volatile uint32_t *s = (const volatile uint32_t *)src;
    dst->magic = 0u;
    __asm__ volatile("csync" ::: "memory");
    for (i = 1u; i < sizeof(fm1_fault_diag_t) / sizeof(uint32_t); ++i) d[i] = s[i];
    __asm__ volatile("csync" ::: "memory");
    dst->magic = FM1_FAULT_DIAG_MAGIC;
    __asm__ volatile("csync" ::: "memory");
}

/* Called from fm1_fatal_common with the saved frame:
 * f[0..15] r0..r15, f[16] reti, f[17] rete, f[18] retx, f[19] stub+6,
 * f[20] psr, f[21] icfg, f[22] usp, f[23] ssp, f[24] sp, f[25] interrupted rets.
 * Every core captures into its private BSS slot before one core wins the
 * shared legacy-record/reboot path. */
void fm1_fault_c(uint32_t *f)
{
    uint32_t i, cpu, other, count, early, start, now, context[2];
    uint32_t etm[4];
    volatile fm1_fault_diag_t *own;
    volatile fm1_crash_t *crash = &fm1_crash;

    fm1_irq_off();
    cpu = fm1_fault_cpu_id();
    if (cpu > 1u) for (;;) ;
    if (fm1_fault_phase[cpu] != 0u) {
        /* A true same-core recursive exception must not overwrite the first
         * frame or steal the other core's reboot role. Let the existing WDT
         * recovery reset the device. */
        fm1_fault_phase[cpu] = 3u;
        __asm__ volatile("csync" ::: "memory");
        for (;;) ;
    }
    fm1_fault_phase[cpu] = 1u;
    __asm__ volatile("csync" ::: "memory");
    count = (crash->magic == FM1_CRASH_MAGIC ? crash->count : 0u) + 1u;
    for (i = 0u; i < 4u; ++i)
        etm[i] = *(volatile const uint32_t *)(0x01eef000u + cpu * 0x200u + (0x71u + i) * 4u);
    own = &fm1_fault_temp[cpu];
    fm1_fault_diag_capture_to(own, count, cpu, f, etm);
#if FM1_FAULT_C1_TIMEOUT_SIDECAR && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    /* Save the interrupted C0 user-stack locals while the frame and stack
     * range are still available. The capture helper checks the exact bounds. */
    if (cpu == 0u)
        fm1_fault_c0_stack_window_capture_from_frame(count, f);
#endif
    __asm__ volatile("csync" ::: "memory");
    fm1_fault_phase[cpu] = 2u;             /* slot complete */
    __asm__ volatile("csync" ::: "memory");

    if (!fm1_fault_owner_try_acquire(&fm1_fault_owner_lock)) {
        /* The elected owner alone may publish legacy state or reset. */
        fm1_irq_off();
        for (;;) ;
    }

    other = cpu ^ 1u;
#if FM1_FAULT_PEER_WAIT_IF_STARTED
    /* A peer in phase 1 has begun its private capture, so give it the bounded
     * window to publish. Phase 0 needs no wait; phase 2 is already complete. */
    if (fm1_fault_phase[other] == 1u) {
#endif
        start = fm1_ticks();
        while ((uint32_t)(fm1_ticks() - start) < 24000u) {
            if (fm1_fault_phase[other] == 2u &&
                fm1_fault_temp[other].fault_count == count &&
                fm1_fault_temp[other].current_cpu_id == other)
                break;
        }
#if FM1_FAULT_PEER_WAIT_IF_STARTED
    }
#endif
    __asm__ volatile("csync" ::: "memory");
    fm1_fault_diag_context_snapshot(context);
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_fault_bundle_publish(count, cpu, fm1_fault_phase[0], fm1_fault_phase[1],
                             context[0], context[1],
                             fm1_fault_phase[0] == 2u ? br_fx_progress_get(0u) : 0u,
                             fm1_fault_phase[1] == 2u ? br_fx_progress_get(1u) : 0u);
#else
    fm1_fault_bundle_publish(count, cpu, fm1_fault_phase[0], fm1_fault_phase[1],
                             context[0], context[1], 0u, 0u);
#endif
    fm1_fault_copy_diag(&fm1_fault_diag, own);

    /* Populate the old public crash record from the elected core's private
     * frame. Publish its magic last so an interrupted write is not accepted. */
    early = crash->magic == FM1_CRASH_MAGIC ? crash->early : 0u;
    crash->magic = 0u;
    crash->count = count;
    crash->vec = (own->fault_frame[19] - (uint32_t)(uintptr_t)fm1_fatal_stubs) / 6u - 1u;
    crash->pc = own->fault_frame[16];
    crash->rets = own->fault_frame[25];
    crash->emu = cpu == 1u ? own->cpu1_emu_msg : own->cpu0_emu_msg;
    crash->dbg = own->dbg_msg;
    crash->sp = own->fault_frame[24];
    crash->psr = own->fault_frame[20];
    crash->icfg = own->fault_frame[21];
    now = fm1_ticks() / 24000u;
    crash->uptime_ms = now;
    for (i = 0u; i < 4u; ++i) crash->etm[i] = etm[i];
    crash->early = now < 3000u ? early + 1u : 0u;
    __asm__ volatile("csync" ::: "memory");
    crash->magic = FM1_CRASH_MAGIC;
    __asm__ volatile("csync" ::: "memory");
    /* Fixed BSS-to-retained copy after primary records, before code-window reads. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_power_fault_capture_owned(count, cpu);
    fm1_vpd_fault_capture_owned(count, cpu);
#endif
#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ
    /* Preserve the authoritative bundle and legacy crash record before the
     * optional bounded RAM-code read can encounter another exception. */
    fm1_fault_ram_code_capture_owned(count, cpu, own->fault_frame[16]);
    fm1_fault_dual_code_capture_owned(count, cpu);
#endif
#if FM1_FAULT_C1_CONTEXT_SIDECAR && FM1_FAULT_C1_CONTEXT_CAPTURE && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    /* CPU1-only extended capture follows the existing published diagnostics;
     * it has no effect on owner/reset arbitration or the legacy crash record. */
    fm1_fault_cpu1_context_capture_owned(count, cpu);
#endif
    fm1_fault((const fm1_crash_t *)crash);
    for (;;) ;
}
