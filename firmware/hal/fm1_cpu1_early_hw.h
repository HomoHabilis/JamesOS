/* SPDX-License-Identifier: GPL-3.0-only
 * Narrow early-CPU1 HAL: only fixed diagnostic registers and the fixed startup
 * entry used before runtime guards are enabled. No arbitrary address API. */
#pragma once
#include <stdint.h>

#define FM1_EARLY_DBG_EN       (*(volatile uint32_t *)0x01eee340u)
#define FM1_EARLY_C0_WR_EN     (*(volatile uint32_t *)0x01eee348u)
#define FM1_EARLY_PC0_H        (*(volatile uint32_t *)0x01eee380u)
#define FM1_EARLY_PC0_L        (*(volatile uint32_t *)0x01eee384u)
#define FM1_EARLY_PC1_H        (*(volatile uint32_t *)0x01eee388u)
#define FM1_EARLY_PC1_L        (*(volatile uint32_t *)0x01eee38cu)
#define FM1_EARLY_C1_CON       (*(volatile uint32_t *)0x01eee004u)
#define FM1_EARLY_ENTRY        (*(volatile uint32_t *)0x01c7fff8u)

static inline void fm1_cpu1_early_hw_barrier(void)
{
    __asm__ volatile("csync" ::: "memory");
}

static inline void fm1_cpu1_early_hw_snapshot(uint32_t out[8])
{
    out[0] = FM1_EARLY_DBG_EN;
    out[1] = FM1_EARLY_C0_WR_EN;
    out[2] = FM1_EARLY_PC0_L;
    out[3] = FM1_EARLY_PC0_H;
    out[4] = FM1_EARLY_PC1_L;
    out[5] = FM1_EARLY_PC1_H;
    out[6] = FM1_EARLY_C1_CON;
    out[7] = FM1_EARLY_ENTRY;
}

static inline uint32_t fm1_cpu1_early_hw_top_write_guard(void)
{
    return FM1_EARLY_C0_WR_EN & 4u;
}

static inline uint32_t fm1_cpu1_early_hw_read_control(void) { return FM1_EARLY_C1_CON; }
static inline uint32_t fm1_cpu1_early_hw_read_entry(void) { return FM1_EARLY_ENTRY; }
static inline void fm1_cpu1_early_hw_write_control(uint32_t v) { FM1_EARLY_C1_CON = v; }
static inline void fm1_cpu1_early_hw_write_entry(uint32_t v) { FM1_EARLY_ENTRY = v; }
