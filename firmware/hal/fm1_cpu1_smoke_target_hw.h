/* SPDX-License-Identifier: GPL-3.0-only
 * Fixed CPU1 smoke HAL: primary-SDK C1_CON/entry and startup instruction path.
 * This header is included only by fm1_cpu1_smoke_target.c. */
#pragma once
#include <stdint.h>

#define FM1_CPU1_SMOKE_C1_CON   (*(volatile uint32_t *)0x01eee004u)
#define FM1_CPU1_SMOKE_ENTRY    (*(volatile uint32_t *)0x01c7fff8u)
#define FM1_CPU1_SMOKE_DBG_EN   (*(volatile uint32_t *)0x01eee340u)
#define FM1_CPU1_SMOKE_C0_WR_LIMIT_EN (*(volatile uint32_t *)0x01eee348u)

static inline uint32_t fm1_cpu1_smoke_hw_read_control(void) { return FM1_CPU1_SMOKE_C1_CON; }
static inline uint32_t fm1_cpu1_smoke_hw_read_entry(void) { return FM1_CPU1_SMOKE_ENTRY; }
static inline uint32_t fm1_cpu1_smoke_hw_read_debug_enable(void) { return FM1_CPU1_SMOKE_DBG_EN; }
static inline uint32_t fm1_cpu1_smoke_hw_read_write_enable(void) { return FM1_CPU1_SMOKE_C0_WR_LIMIT_EN; }
static inline void fm1_cpu1_smoke_hw_write_control(uint32_t value) { FM1_CPU1_SMOKE_C1_CON = value; }
static inline void fm1_cpu1_smoke_hw_write_entry(uint32_t value) { FM1_CPU1_SMOKE_ENTRY = value; }

static inline void fm1_cpu1_smoke_hw_clear_mailbox(volatile uint32_t *ready,
                                                    volatile uint32_t *heartbeat,
                                                    volatile uint32_t *cpu_id)
{
    *ready = 0u;
    *heartbeat = 0u;
    *cpu_id = 0xffffffffu;
    __asm__ volatile("csync" ::: "memory");
}

static inline __attribute__((always_inline)) void fm1_cpu1_smoke_hw_barrier(void)
{
    __asm__ volatile("csync" ::: "memory");
}

static inline __attribute__((always_inline)) uint32_t fm1_cpu1_smoke_hw_cpu_id(void)
{
    uint32_t id;
    __asm__ volatile("%0 = cnum" : "=r"(id) ::);
    return id;
}

static inline uint32_t fm1_cpu1_smoke_hw_irq_save(void)
{
    uint32_t old;
    __asm__ volatile("%0 = icfg\n\tcli" : "=r"(old) :: "memory");
    return old;
}

static inline void fm1_cpu1_smoke_hw_irq_restore(uint32_t old)
{
    __asm__ volatile("icfg = %0" :: "r"(old) : "memory");
    if (old & 0x100u) __asm__ volatile("csync\n\tsti" ::: "memory");
}

void fm1_cpu1_smoke_worker(void);
extern uint8_t fm1_cpu1_smoke_ustack[0x300];
extern uint8_t fm1_cpu1_smoke_sstack[0x1000];
extern uint32_t _fm1_cpu1_smoke_ustack_top[];
extern uint32_t _fm1_cpu1_smoke_sstack_top[];

/* Exact four transfer operations from cpu-sdk.a startup.S.o, with CLI added
 * before the worker, so no CPU1 interrupt can enter an uninitialized vector. */
__attribute__((section(".ram_audio"), noinline, used, naked, noreturn))
static void fm1_cpu1_smoke_start(void)
{
    __asm__ volatile("cli\n\t"
                     "usp = _fm1_cpu1_smoke_ustack_top\n\t"
                     "sp = _fm1_cpu1_smoke_sstack_top\n\t"
                     "reti = fm1_cpu1_smoke_worker\n\t"
                     "rti\n\t");
}
