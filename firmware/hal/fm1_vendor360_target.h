/* SPDX-License-Identifier: GPL-3.0-only */
/* Checked pre-BSS target adapter. Include after fm1_sys.h, fm1_time.h,
 * fm1_irq.h and the CPU1 worker hardware declarations. */
#pragma once
#include "fm1_vendor360_policy.h"

typedef struct {
    fm1_p33_checked_t bus;
    uint32_t p3, rst_src;
} fm1_vp_target_t;

static inline void fm1_vp_target_init(fm1_vp_target_t *target)
{ target->bus.timeouts = 0u; target->p3 = target->rst_src = FM1_VP_UNKNOWN; }
static inline void fm1_vp_target_set_reset_profile(fm1_vp_target_t *target,
                                                   uint32_t p3, uint32_t rst_src)
{ target->p3 = p3; target->rst_src = rst_src; }

/* CLI and the SDK ICFG software gate are separate; clear both. */
static inline void fm1_vp_target_mask_irqs(void)
{
    fm1_irq_off();
    fm1_icfg_set(fm1_icfg() & ~0x100u);
    __asm__ volatile("csync" ::: "memory");
}

static int fm1_vp_target_read(void *opaque, uint32_t reg, uint32_t *out)
{
    fm1_vp_target_t *target = (fm1_vp_target_t *)opaque;
    uint32_t i;
    for (i = 0u; i < FM1_VP_COUNT; ++i)
        if (fm1_vp_reg(i) == reg) return fm1_p33_checked_read(&target->bus, reg, out);
    return 0;
}
static int fm1_vp_target_write(void *opaque, uint32_t reg, uint32_t value)
{
    fm1_vp_target_t *target = (fm1_vp_target_t *)opaque;
    uint8_t ignored;
    int ok;
    if ((reg != 0u && reg != 1u && reg != 2u && reg != 5u && reg != 6u &&
         reg != 7u && reg != 8u && reg != 9u && reg != 0x11u &&
         reg != 0x16u && reg != 0x17u) ||
        value > 0xffu) return 0;
    fm1__p33_cs(reg);
    ok = fm1_p33_checked_xfer(&target->bus, (uint8_t)((reg >> 8) & 3u), &ignored) &&
         fm1_p33_checked_xfer(&target->bus, (uint8_t)reg, &ignored) &&
         fm1_p33_checked_xfer(&target->bus, (uint8_t)value, &ignored);
    FM1_P33_CON &= ~((1u << 0) | (1u << 8));
    return ok;
}

/* The vendor uses the P33 atomic OR opcode, not a software read/OR/write,
 * for the VLVD pending-clear and enable operations. */
static int fm1_vp_target_or(void *opaque, uint32_t reg, uint32_t value)
{
    fm1_vp_target_t *target = (fm1_vp_target_t *)opaque;
    if (reg != 0x11u || (value != 0x40u && value != 0x01u)) return 0;
    return fm1_p33_checked_op(&target->bus, 1u, reg, (uint8_t)value);
}

/* The 1481 396 MHz trial used 1 ms crystal waits for the regulator handoff.
 * This is an adaptation, not the vendor's 1,000-iteration CPU loop or a
 * proof of analog settling. The iteration cap also bounds a stopped timer. */
static int fm1_vp_target_handoff_loop(void *opaque)
{
    uint32_t start, limit = 0x500000u;
    (void)opaque;
    if ((FM1_T4_CON & 0xffu) != 9u || FM1_T4_PRD != 0xffffffffu) return 0;
    start = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - start) < 1000u * FM1_TICKS_PER_US)
        if (--limit == 0u) return 0;
    return 1;
}
static int fm1_vp_target_held(void *opaque)
{ (void)opaque; return fm1_cpu1_worker_hw_reset_held() != 0u; }
static int fm1_vp_target_irq_disabled(void *opaque)
{ (void)opaque; return (fm1_icfg() & 0x100u) == 0u; }
static uint32_t fm1_vp_target_timeouts(void *opaque)
{ return ((fm1_vp_target_t *)opaque)->bus.timeouts; }

/* Rescue uses the crystal timer; fail closed instead of entering a rescue
 * path whose LCD/USB delays could otherwise wait forever on a stopped timer. */
static int fm1_vp_target_timer(void *opaque)
{
    uint32_t start, limit = 1000000u;
    (void)opaque;
    if ((FM1_T4_CON & 0xffu) != 9u || FM1_T4_PRD != 0xffffffffu) return 0;
    start = fm1_ticks();
    while (fm1_ticks() == start) if (--limit == 0u) return 0;
    return 1;
}

/* The fingerprint matched the early 1481 source-7 396 MHz gate. Register
 * checks do not measure frequency, analog rail voltage, or power-good. */
static int fm1_vp_target_eligible(void *opaque, const fm1_vp_sample_t *sample)
{
    fm1_vp_target_t *target = (fm1_vp_target_t *)opaque;
    if (!fm1_vp_entry_profile_eligible(target->p3, target->rst_src, sample)) return 0;
    if (!fm1_vp_clock_profile_eligible(
        *(volatile uint32_t *)(uintptr_t)0x10008u,
        *(volatile uint32_t *)(uintptr_t)0x1000cu,
        *(volatile uint32_t *)(uintptr_t)0x10014u,
        *(volatile uint32_t *)(uintptr_t)0x119a8u,
        *(volatile uint32_t *)(uintptr_t)0x119acu)) return 0;
    return 1;
}

static fm1_vp_io_t fm1_vp_target_ops(fm1_vp_target_t *target)
{
    fm1_vp_io_t io = { target, fm1_vp_target_read, fm1_vp_target_write,
        fm1_vp_target_handoff_loop, fm1_vp_target_held,
        fm1_vp_target_irq_disabled, fm1_vp_target_timeouts,
        fm1_vp_target_eligible, fm1_vp_target_or, fm1_vp_target_timer };
    return io;
}
