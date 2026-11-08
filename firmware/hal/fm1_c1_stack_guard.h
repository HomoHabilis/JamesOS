/* SPDX-License-Identifier: GPL-3.0-only
 * C1-only diagnostic guard for the C0 main stack range. */
#pragma once
#include <stdint.h>

#define FM1_C1_STACK_GUARD_BIT 8u
#define FM1_C1_STACK_C0_WR_EN_ADDR 0x1EEE348u
#define FM1_C1_STACK_C1_WR_EN_ADDR 0x1EEE34Cu
#define FM1_C1_STACK_LIMIT_H3_ADDR 0x1EEE28Cu
#define FM1_C1_STACK_LIMIT_L3_ADDR 0x1EEE2CCu
#ifndef FM1_C1_STACK_GUARD_MMIO
#define FM1_C1_STACK_GUARD_MMIO(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif
#ifndef FM1_C1_STACK_GUARD_CSYNC
#define FM1_C1_STACK_GUARD_CSYNC() __asm__ volatile("csync" ::: "memory")
#endif

/* C1 is held reset at the only install call. Slot 3 is shared, but C0 bit 3
 * must be clear. Never replace an active conflicting range. */
static inline uint32_t fm1_c1_stack_guard_apply(uint32_t low, uint32_t high)
{
    uint32_t c0 = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_C0_WR_EN_ADDR);
    uint32_t c1, lo, hi;
    if (c0 & FM1_C1_STACK_GUARD_BIT) return 0u;
    c1 = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_C1_WR_EN_ADDR);
    if (c1 & FM1_C1_STACK_GUARD_BIT) {
        lo = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_L3_ADDR);
        hi = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_H3_ADDR);
        return lo == low && hi == high;
    }
    FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_L3_ADDR) = low;
    FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_H3_ADDR) = high;
    FM1_C1_STACK_GUARD_CSYNC();
    if (FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_L3_ADDR) != low ||
        FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_H3_ADDR) != high)
        return 0u;
    FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_C1_WR_EN_ADDR) =
        c1 | FM1_C1_STACK_GUARD_BIT;
    FM1_C1_STACK_GUARD_CSYNC();
    return (FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_C1_WR_EN_ADDR) &
            FM1_C1_STACK_GUARD_BIT) != 0u;
}

/* Read-only wire order: C1 enable, C0 enable, slot-3 low, slot-3 high. */
static inline void fm1_c1_stack_guard_snapshot(uint32_t out[4])
{
    out[0] = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_C1_WR_EN_ADDR);
    out[1] = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_C0_WR_EN_ADDR);
    out[2] = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_L3_ADDR);
    out[3] = FM1_C1_STACK_GUARD_MMIO(FM1_C1_STACK_LIMIT_H3_ADDR);
}
