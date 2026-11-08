/* SPDX-License-Identifier: GPL-3.0-only */
#include "fm1_cpu1_smoke.h"
#include "fm1_cpu1_smoke_target_hw.h"
#include "fm1_time.h"

volatile uint32_t fm1_cpu1_smoke_ready __attribute__((aligned(4), used));
volatile uint32_t fm1_cpu1_smoke_heartbeat __attribute__((aligned(4), used));
volatile uint32_t fm1_cpu1_smoke_cpu_id __attribute__((aligned(4), used));
uint8_t fm1_cpu1_smoke_ustack[0x300] __attribute__((aligned(16), used));
uint8_t fm1_cpu1_smoke_sstack[0x1000] __attribute__((aligned(16), used));

/* Entire routine is internal RAM, no function calls, no FPU/DSP use, no XIP. */
__attribute__((section(".ram_audio"), noinline, used, noreturn))
void fm1_cpu1_smoke_worker(void)
{
    fm1_cpu1_smoke_cpu_id = fm1_cpu1_smoke_hw_cpu_id();
    fm1_cpu1_smoke_hw_barrier();
    fm1_cpu1_smoke_ready = 1u;
    for (;;) fm1_cpu1_smoke_heartbeat++;
}

static uint32_t c1_read_control(void *context) { (void)context; return fm1_cpu1_smoke_hw_read_control(); }
static uint32_t c1_read_entry(void *context) { (void)context; return fm1_cpu1_smoke_hw_read_entry(); }
static uint32_t c1_read_debug_enable(void *context) { (void)context; return fm1_cpu1_smoke_hw_read_debug_enable(); }
static uint32_t c1_read_write_enable(void *context) { (void)context; return fm1_cpu1_smoke_hw_read_write_enable(); }
static void c1_write_control(void *context, uint32_t value) { (void)context; fm1_cpu1_smoke_hw_write_control(value); }
static int c1_write_entry(void *context, uint32_t value)
{
    (void)context;
    fm1_cpu1_smoke_hw_write_entry(value);
    fm1_cpu1_smoke_hw_barrier();
    return fm1_cpu1_smoke_hw_read_entry() == value;
}
static void c1_barrier(void *context) { (void)context; fm1_cpu1_smoke_hw_barrier(); }
static void c1_clear_mailbox(void *context)
{
    (void)context;
    fm1_cpu1_smoke_hw_clear_mailbox(&fm1_cpu1_smoke_ready, &fm1_cpu1_smoke_heartbeat,
                                    &fm1_cpu1_smoke_cpu_id);
}
static uint32_t c1_read_ready(void *context) { (void)context; return fm1_cpu1_smoke_ready; }
static uint32_t c1_read_heartbeat(void *context) { (void)context; return fm1_cpu1_smoke_heartbeat; }
static uint32_t c1_read_cpu_id(void *context) { (void)context; return fm1_cpu1_smoke_cpu_id; }
static uint32_t c1_ticks(void *context) { (void)context; return fm1_ticks(); }
static uint32_t c1_irq_save(void *context)
{
    (void)context;
    return fm1_cpu1_smoke_hw_irq_save();
}
static void c1_irq_restore(void *context, uint32_t old)
{
    (void)context;
    fm1_cpu1_smoke_hw_irq_restore(old);
}

uint32_t fm1_cpu1_smoke_run_target(fm1_cpu1_smoke_result_t *result)
{
    static const fm1_cpu1_smoke_ops_t ops = {
        0, c1_read_control, c1_read_entry, c1_read_debug_enable, c1_read_write_enable,
        c1_write_control, c1_write_entry, c1_barrier,
        c1_clear_mailbox, c1_read_ready, c1_read_heartbeat, c1_read_cpu_id, c1_ticks,
        c1_irq_save, c1_irq_restore
    };
    return fm1_cpu1_smoke_run(&ops, (uint32_t)(uintptr_t)fm1_cpu1_smoke_start,
                              FM1_CPU1_SMOKE_LIMIT, result);
}
