/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 hardware guards. Every violation raises the CPU exception (vector 1) and
 * ends in the fault report of fm1_irq.h, so install that first.
 *
 *   EMU stack limit   sp outside [_ustack_lo, _sstack_top] -> EMU_MSG bit 3.
 *                     One window for both stacks: which of EMU_SSP/EMU_USP
 *                     the core applies to our supervisor-mode main loop is
 *                     not known, so both get the same range.
 *   write limits      CPU0 writes into the two 256-byte stack guard bands and
 *                     into 0x01C7FD50..0x01C7FFFF (boot info, mailbox, vectors)
 *                     -> DBG_MSG bit 13. The top window is armed separately
 *                     with fm1_guard_lock_top() once every vector is
 *                     attached; fm1_guard_unlock_top() reopens it.
 *   bus-invalid       fetch/read/write of unmapped space (NULL page, XIP
 *                     writes) -> DBG_MSG bits 4, 5, 16..21.
 *   PC limit          instruction fetch outside [0x02000120, _etext]
 *                     -> DBG_MSG bit 12.
 */
#pragma once
#include <stdint.h>
#include "fm1_irq.h"
#include "fm1_c1_stack_guard.h"

#define FM1_X2(off) (*(volatile uint32_t *)(0x1EEE000u + (off)))
#define FM1_WR_LIMIT_H(n) FM1_X2(0x280u + 4u * (n))
#define FM1_WR_LIMIT_L(n) FM1_X2(0x2C0u + 4u * (n))
#define FM1_C0_WR_LIMIT_EN FM1_X2(0x348u)
#define FM1_PC_LIMIT0_H FM1_X2(0x380u)
#define FM1_PC_LIMIT0_L FM1_X2(0x384u)
#define FM1_PC_LIMIT1_H FM1_X2(0x388u)
#define FM1_PC_LIMIT1_L FM1_X2(0x38Cu)
#define FM1_EMU_SSP_H (*(volatile uint32_t *)0x1EEF0D8u)
#define FM1_EMU_SSP_L (*(volatile uint32_t *)0x1EEF0DCu)
#define FM1_EMU_USP_H (*(volatile uint32_t *)0x1EEF0E0u)
#define FM1_EMU_USP_L (*(volatile uint32_t *)0x1EEF0E4u)

extern char _guard0[], _ustack_lo[], _guard1[], _sstack_lo[], _sstack_top[], _etext[];
extern uint32_t _rt_start[], _rt_end[], _ra_start[], _ra_end[];

enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };

static void fm1__dbg_unlock(void)
{
    if (!(FM1_DBG_WR_EN & 1u))
        FM1_DBG_WR_EN = 0xE7u;
}

static void fm1__dbg_lock(void)
{
    if (FM1_DBG_WR_EN & 1u)
        FM1_DBG_WR_EN = 0xE7u;
}

/* Install before fm1_cpu1_early_bootstrap starts C1. fm1_irq_init has
 * opened debug-register writes; no startup path relocks them before here. */
static uint32_t fm1_guard_c1_stack_install(void)
{
    return fm1_c1_stack_guard_apply(
        (uint32_t)(uintptr_t)_ustack_lo,
        (uint32_t)(uintptr_t)_sstack_top - 1u);
}

static void fm1_guard_enable(uint32_t which)
{
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    if (which & FM1_GUARD_STACK) {
        FM1_EMU_CON &= ~(1u << 3);
        FM1_EMU_SSP_L = (uint32_t)(uintptr_t)_ustack_lo;
        FM1_EMU_SSP_H = (uint32_t)(uintptr_t)_sstack_top;
        FM1_EMU_USP_L = (uint32_t)(uintptr_t)_ustack_lo;
        FM1_EMU_USP_H = (uint32_t)(uintptr_t)_sstack_top;
        FM1_EMU_CON |= 1u << 3;
    }
    if (which & FM1_GUARD_WRITE) {
        FM1_WR_LIMIT_L(0) = (uint32_t)(uintptr_t)_guard0;
        FM1_WR_LIMIT_H(0) = (uint32_t)(uintptr_t)_ustack_lo - 1u;
        FM1_WR_LIMIT_L(1) = (uint32_t)(uintptr_t)_guard1;
        FM1_WR_LIMIT_H(1) = (uint32_t)(uintptr_t)_sstack_lo - 1u;
        FM1_C0_WR_LIMIT_EN |= 3u;
    }
    if (which & FM1_GUARD_BUS)
        FM1_DBG_EN |= (0x3Fu << 16) | (0x3u << 4);
    if (which & FM1_GUARD_PC) {
        FM1_PC_LIMIT0_L = 0x02000120u;
        FM1_PC_LIMIT0_H = (uint32_t)(uintptr_t)_etext;
        {   /* Strict .ram_text followed by normal-render .ram_audio. */
            volatile uint32_t rs = (uint32_t)(uintptr_t)_rt_start;
            volatile uint32_t rt = (uint32_t)(uintptr_t)_rt_end;
            volatile uint32_t ra = (uint32_t)(uintptr_t)_ra_start;
            volatile uint32_t re = (uint32_t)(uintptr_t)_ra_end;
            if (rt != ra) re = rt;              /* fail closed if linker placement is not contiguous */
            FM1_PC_LIMIT1_L = re > rs ? rs : 0x02000120u;
            FM1_PC_LIMIT1_H = re > rs ? re - 1u : (uint32_t)(uintptr_t)_etext;
        }
    }
    fm1__dbg_lock();
}

/* write-protect 0x01C7FD50..0x01C7FFFF (boot info, mailbox, vectors):
 * call after the last fm1_irq_attach */
static void fm1_guard_lock_top(void)
{
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    FM1_WR_LIMIT_L(2) = 0x01C7FD50u;
    FM1_WR_LIMIT_H(2) = 0x01C7FFFFu;
    FM1_C0_WR_LIMIT_EN |= 4u;
    fm1__dbg_lock();
}

/* reopen the reserved top of RAM (mailbox write before a UBOOT reset) */
static void fm1_guard_unlock_top(void)
{
    fm1__dbg_unlock();
    FM1_C0_WR_LIMIT_EN &= ~4u;
    fm1__dbg_lock();
}

/* A narrowly scoped write to the CPU1 entry word inside the reserved-top
 * guard. The guard must already be armed. Interrupts are masked only for this
 * one store; the complete write-limit value and prior debug-lock state are
 * restored and checked before return. Returns 1 on success, 0 on safe refusal,
 * and -1 if restoration state cannot be proven. */
static int fm1_guard_write_cpu1_entry_once(uint32_t value)
{
    volatile uint32_t *entry = (volatile uint32_t *)0x01C7FFF8u;
    uint32_t old_icfg, old_limit, old_debug_locked, current_limit, ok;

    old_icfg = fm1_icfg();
    fm1_irq_off();
    old_limit = FM1_C0_WR_LIMIT_EN;
    old_debug_locked = !(FM1_DBG_WR_EN & 1u);
    if (!(old_limit & 4u)) {
        fm1_icfg_set(old_icfg);
        if (old_icfg & 0x100u) __asm__ volatile("csync\n\tsti" ::: "memory");
        return 0;
    }

    fm1__dbg_unlock();
    current_limit = FM1_C0_WR_LIMIT_EN;
    FM1_C0_WR_LIMIT_EN = current_limit & ~4u;
    __asm__ volatile("csync" ::: "memory");
    current_limit = FM1_C0_WR_LIMIT_EN;
    if ((current_limit & 4u) || ((current_limit & ~4u) != (old_limit & ~4u))) {
        /* Refuse the protected store unless the one-bit unlock took effect. */
        FM1_C0_WR_LIMIT_EN = old_limit;
        if (old_debug_locked) fm1__dbg_lock();
        else fm1__dbg_unlock();
        __asm__ volatile("csync" ::: "memory");
        ok = (FM1_C0_WR_LIMIT_EN == old_limit) &&
             ((!(FM1_DBG_WR_EN & 1u)) == old_debug_locked);
        fm1_icfg_set(old_icfg);
        if (old_icfg & 0x100u) __asm__ volatile("csync\n\tsti" ::: "memory");
        return ok ? 0 : -1;
    }
    *entry = value;
    __asm__ volatile("csync" ::: "memory");
    FM1_C0_WR_LIMIT_EN = old_limit;
    if (old_debug_locked) fm1__dbg_lock();
    else fm1__dbg_unlock();
    __asm__ volatile("csync" ::: "memory");

    ok = (*entry == value) && (FM1_C0_WR_LIMIT_EN == old_limit) &&
         ((!(FM1_DBG_WR_EN & 1u)) == old_debug_locked);
    fm1_icfg_set(old_icfg);
    if (old_icfg & 0x100u) __asm__ volatile("csync\n\tsti" ::: "memory");
    return ok ? 1 : -1;
}
