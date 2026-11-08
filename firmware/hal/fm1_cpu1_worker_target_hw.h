/* SPDX-License-Identifier: GPL-3.0-only
 * Fixed-register CPU1 worker HAL. */
#pragma once
#include <stdint.h>
#include "fm1_cpu1_worker.h"

#define FM1_CPU1_WORKER_CON   (*(volatile uint32_t *)0x01eee004u)
#define FM1_CPU1_WORKER_ENTRY (*(volatile uint32_t *)0x01c7fff8u)
#define FM1_CPU1_WORKER_RESET_BIT (1u << 1)
#define FM1_CPU1_WORKER_START_BIT (1u << 3)

/* Diagnostic-only startup experiment. Keep the existing RAM entry stub as
 * the default until the XIP entry and C1 cache-invalidation behavior are
 * verified on hardware. */
#ifndef FM1_C1_ICACHE_XIP_TRAMPOLINE
#define FM1_C1_ICACHE_XIP_TRAMPOLINE 1
#endif
#if FM1_C1_ICACHE_XIP_TRAMPOLINE != 0 && FM1_C1_ICACHE_XIP_TRAMPOLINE != 1
#error "FM1_C1_ICACHE_XIP_TRAMPOLINE must be 0 or 1"
#endif

static inline __attribute__((always_inline)) uint32_t fm1_cpu1_worker_hw_id(void)
{
    uint32_t id;
    __asm__ volatile("%0 = cnum" : "=r"(id) ::);
    return id;
}

static inline uint32_t fm1_cpu1_worker_irq_save(void)
{
    uint32_t old;
    __asm__ volatile("%0 = icfg\n\tcli" : "=r"(old) :: "memory");
    return old;
}

static inline void fm1_cpu1_worker_irq_restore(uint32_t old)
{
    __asm__ volatile("icfg = %0" :: "r"(old) : "memory");
    if (old & 0x100u) __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* Runtime emergency stop; keep all fixed C1 MMIO inside the HAL. */
static inline __attribute__((always_inline)) uint32_t fm1_cpu1_worker_hw_hold_reset(void)
{
    uint32_t irq = fm1_cpu1_worker_irq_save();
    uint32_t control = FM1_CPU1_WORKER_CON;
    FM1_CPU1_WORKER_CON = (control & ~FM1_CPU1_WORKER_START_BIT) |
                          FM1_CPU1_WORKER_RESET_BIT;
    __asm__ volatile("csync" ::: "memory");
    control = FM1_CPU1_WORKER_CON;
    fm1_cpu1_worker_irq_restore(irq);
    return (control & FM1_CPU1_WORKER_RESET_BIT) &&
           !(control & FM1_CPU1_WORKER_START_BIT);
}

static inline __attribute__((always_inline)) uint32_t fm1_cpu1_worker_hw_reset_held(void)
{
    uint32_t control = FM1_CPU1_WORKER_CON;
    return (control & FM1_CPU1_WORKER_RESET_BIT) &&
           !(control & FM1_CPU1_WORKER_START_BIT);
}

void fm1_cpu1_worker_main(void);
extern uint8_t fm1_cpu1_worker_ustack[0x2000];
extern uint8_t fm1_cpu1_worker_sstack[0x800];
extern uint32_t _fm1_cpu1_worker_ustack_top[];
extern uint32_t _fm1_cpu1_worker_sstack_top[];

__attribute__((section(".ram_audio"), noinline, used, naked, noreturn))
static void fm1_cpu1_worker_start_stub(void)
{
    __asm__ volatile("cli\n\t"
                     "usp = _fm1_cpu1_worker_ustack_top\n\t"
                     "sp = _fm1_cpu1_worker_sstack_top\n\t"
                     "reti = fm1_cpu1_worker_main\n\t"
                     "rti\n\t");
}
