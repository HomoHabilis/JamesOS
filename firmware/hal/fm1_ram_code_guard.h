/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared CPU0/CPU1 write protection for the reserved RAM-code allocation. */
#pragma once
#include <stdint.h>
#include "fm1_cpu1_worker.h"

#define FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR 0x01EEE240u
#define FM1_RAM_CODE_GUARD_C1_CON_ADDR    0x01EEE004u
#define FM1_RAM_CODE_GUARD_C0_EN_ADDR     0x01EEE348u
#define FM1_RAM_CODE_GUARD_C1_EN_ADDR     0x01EEE34Cu
#define FM1_RAM_CODE_GUARD_L4_ADDR        0x01EEE2D0u
#define FM1_RAM_CODE_GUARD_H4_ADDR        0x01EEE290u
#define FM1_RAM_CODE_GUARD_BIT            (1u << 4)
#define FM1_RAM_CODE_GUARD_C1_RESET_BIT   (1u << 1)
#define FM1_RAM_CODE_GUARD_C1_START_BIT   (1u << 3)

/* Fixed half-open RAMTEXT reservation. Hardware H4 is inclusive. Keeping
 * padding in the reservation makes the persistent comparator stable when the
 * linked code image grows, shrinks, or rolls back to another release. */
#define FM1_RAM_CODE_GUARD_RANGE_START    0x01C00000u
#define FM1_RAM_CODE_GUARD_RANGE_END      0x01C07F00u

/* Filled by platform.c from one barrier-bracketed mailbox snapshot after the
 * early worker bootstrap returns. */
typedef struct {
    uint32_t state;
    uint32_t cpu_id;
    uint32_t request_seq;
    uint32_t done_seq;
    uint32_t park_request;
    uint32_t worker_fault;
} fm1_ram_code_guard_worker_snapshot_t;

#ifndef FM1_RAM_CODE_GUARD_READ
#define FM1_RAM_CODE_GUARD_READ(address) \
    (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef FM1_RAM_CODE_GUARD_WRITE
#define FM1_RAM_CODE_GUARD_WRITE(address, value) \
    (*(volatile uint32_t *)(uintptr_t)(address) = (uint32_t)(value))
#endif
#ifndef FM1_RAM_CODE_GUARD_CSYNC
#define FM1_RAM_CODE_GUARD_CSYNC() __asm__ volatile("csync" ::: "memory")
#endif

/* Reject a different RAMTEXT origin, discontinuous sections, misalignment,
 * empty/reversed ranges, and any image reaching the legacy STATUS page. This
 * validation is performed before a persistent comparator can be disabled. */
static inline uint32_t fm1_ram_code_guard_layout_valid(uint32_t rt_start,
                                                        uint32_t rt_end,
                                                        uint32_t ra_start,
                                                        uint32_t ra_end)
{
    return rt_start == FM1_RAM_CODE_GUARD_RANGE_START &&
           rt_end == ra_start && rt_end >= rt_start &&
           ra_end >= ra_start && ra_end > rt_start &&
           ra_end <= FM1_RAM_CODE_GUARD_RANGE_END &&
           ((rt_start | rt_end | ra_start | ra_end) & 3u) == 0u;
}

static inline uint32_t fm1_ram_code_guard_c1_held(void)
{
    uint32_t control = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C1_CON_ADDR);
    return (control & FM1_RAM_CODE_GUARD_C1_RESET_BIT) != 0u &&
           (control & FM1_RAM_CODE_GUARD_C1_START_BIT) == 0u;
}

static inline uint32_t fm1_ram_code_guard_worker_quiescent(
    const fm1_ram_code_guard_worker_snapshot_t *snapshot)
{
    return snapshot && snapshot->state == FM1_CPU1_WORKER_READY &&
           snapshot->cpu_id == 1u &&
           snapshot->request_seq == snapshot->done_seq &&
           snapshot->park_request == 0u && snapshot->worker_fault == 0u;
}

/* Toggle the debug-register write latch only when needed, then restore its
 * incoming state. The same 0xE7 key is used by fm1_irq_init/fm1_guard.h. */
static inline uint32_t fm1_ram_code_guard_debug_begin(uint32_t *was_open)
{
    uint32_t value;
    if (!was_open) return 0u;
    value = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR);
    *was_open = value & 1u;
    if (!*was_open) {
        FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR, 0xE7u);
        FM1_RAM_CODE_GUARD_CSYNC();
    }
    return (FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR) & 1u) != 0u;
}

static inline uint32_t fm1_ram_code_guard_debug_end(uint32_t was_open)
{
    uint32_t value = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR);
    if ((value & 1u) != (was_open & 1u)) {
        FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR, 0xE7u);
        FM1_RAM_CODE_GUARD_CSYNC();
    }
    return (FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_DBG_WR_EN_ADDR) & 1u) ==
           (was_open & 1u);
}

/* Run after fm1_irq_init has unlocked the debug registers and before BSS/data/
 * RAM-code copies. Only the exact fixed reservation may be disabled, even if
 * this build's actual _ra_end differs from the previous build's. */
static inline uint32_t fm1_ram_code_guard_prepare_copy(uint32_t rt_start,
                                                        uint32_t rt_end,
                                                        uint32_t ra_start,
                                                        uint32_t ra_end)
{
    uint32_t c0, c1, low, high, old_open, active, ok;
    if (!fm1_ram_code_guard_layout_valid(rt_start, rt_end, ra_start, ra_end) ||
        !fm1_ram_code_guard_c1_held()) return 0u;
    c0 = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C0_EN_ADDR);
    c1 = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C1_EN_ADDR);
    active = (c0 | c1) & FM1_RAM_CODE_GUARD_BIT;
    if (!active) return 1u;
    low = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_L4_ADDR);
    high = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_H4_ADDR);
    if (low != FM1_RAM_CODE_GUARD_RANGE_START ||
        high != FM1_RAM_CODE_GUARD_RANGE_END - 1u) return 0u;
    if (!fm1_ram_code_guard_debug_begin(&old_open)) return 0u;
    FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_C0_EN_ADDR,
                             c0 & ~FM1_RAM_CODE_GUARD_BIT);
    FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_C1_EN_ADDR,
                             c1 & ~FM1_RAM_CODE_GUARD_BIT);
    FM1_RAM_CODE_GUARD_CSYNC();
    ok = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C0_EN_ADDR) ==
             (c0 & ~FM1_RAM_CODE_GUARD_BIT) &&
         FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C1_EN_ADDR) ==
             (c1 & ~FM1_RAM_CODE_GUARD_BIT);
    if (!fm1_ram_code_guard_debug_end(old_open)) ok = 0u;
    return ok;
}

/* Run after both RAM-code copies and a successful fm1_cpu1_early_bootstrap.
 * C1 must either remain held reset or be proven quiescent by the platform's
 * mailbox snapshot. Slot 4 protects the same fixed inclusive range on both
 * cores. */
static inline uint32_t fm1_ram_code_guard_install(uint32_t rt_start,
                                                   uint32_t rt_end,
                                                   uint32_t ra_start,
                                                   uint32_t ra_end,
                                                   const fm1_ram_code_guard_worker_snapshot_t *worker)
{
    uint32_t c0, c1, low, high, old_open, active, ok, c1_held;
    if (!fm1_ram_code_guard_layout_valid(rt_start, rt_end, ra_start, ra_end))
        return 0u;
    c1_held = fm1_ram_code_guard_c1_held();
    if (!c1_held && !fm1_ram_code_guard_worker_quiescent(worker)) return 0u;
    c0 = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C0_EN_ADDR);
    c1 = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C1_EN_ADDR);
    active = (c0 | c1) & FM1_RAM_CODE_GUARD_BIT;
    low = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_L4_ADDR);
    high = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_H4_ADDR);
    if (active && (low != FM1_RAM_CODE_GUARD_RANGE_START ||
                   high != FM1_RAM_CODE_GUARD_RANGE_END - 1u)) return 0u;
    if (!fm1_ram_code_guard_debug_begin(&old_open)) return 0u;
    if (!active) {
        FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_L4_ADDR,
                                 FM1_RAM_CODE_GUARD_RANGE_START);
        FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_H4_ADDR,
                                 FM1_RAM_CODE_GUARD_RANGE_END - 1u);
        FM1_RAM_CODE_GUARD_CSYNC();
    }
    if (FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_L4_ADDR) !=
            FM1_RAM_CODE_GUARD_RANGE_START ||
        FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_H4_ADDR) !=
            FM1_RAM_CODE_GUARD_RANGE_END - 1u) {
        (void)fm1_ram_code_guard_debug_end(old_open);
        return 0u;
    }
    FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_C0_EN_ADDR,
                             c0 | FM1_RAM_CODE_GUARD_BIT);
    FM1_RAM_CODE_GUARD_WRITE(FM1_RAM_CODE_GUARD_C1_EN_ADDR,
                             c1 | FM1_RAM_CODE_GUARD_BIT);
    FM1_RAM_CODE_GUARD_CSYNC();
    ok = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C0_EN_ADDR) ==
             (c0 | FM1_RAM_CODE_GUARD_BIT) &&
         FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C1_EN_ADDR) ==
             (c1 | FM1_RAM_CODE_GUARD_BIT);
    if (!fm1_ram_code_guard_debug_end(old_open)) ok = 0u;
    return ok;
}

/* Wire order for diagnostic command 0x9A: C0 enable, C1 enable, L4, H4. */
static inline void fm1_ram_code_guard_snapshot(uint32_t out[4])
{
    out[0] = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C0_EN_ADDR);
    out[1] = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_C1_EN_ADDR);
    out[2] = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_L4_ADDR);
    out[3] = FM1_RAM_CODE_GUARD_READ(FM1_RAM_CODE_GUARD_H4_ADDR);
}
