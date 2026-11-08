/* SPDX-License-Identifier: GPL-3.0-only
 * Private CPU1 smoke test. Fixed-address HAL wiring is separate from this
 * host-testable bounded launch/rollback state machine.
 */
#pragma once
#include <stdint.h>

enum {
    FM1_CPU1_SMOKE_OK = 0u,
    FM1_CPU1_SMOKE_TIMEOUT = 1u,
    FM1_CPU1_SMOKE_BUSY = 2u,
    FM1_CPU1_SMOKE_RESTORE_FAILED = 3u,
    FM1_CPU1_SMOKE_BAD_LIMIT = 4u,
    FM1_CPU1_SMOKE_BAD_CPU = 5u,
    FM1_CPU1_SMOKE_GUARD_REFUSED = 6u,
    FM1_CPU1_SMOKE_UNSUPPORTED = 7u,
    FM1_CPU1_SMOKE_RECOVERED = 8u,
    FM1_CPU1_SMOKE_NOT_RUN = 9u
};

#define FM1_CPU1_RESET_BIT       (1u << 1)
#define FM1_CPU1_START_BIT       (1u << 3)
#define FM1_CPU1_SMOKE_LIMIT     (50u * 1000u * 24u)

typedef struct {
    void *context;
    uint32_t (*read_control)(void *context);
    uint32_t (*read_entry)(void *context);
    uint32_t (*read_debug_enable)(void *context);
    uint32_t (*read_write_enable)(void *context);
    void (*write_control)(void *context, uint32_t value);
    int (*write_entry)(void *context, uint32_t value);
    void (*barrier)(void *context);
    void (*clear_mailbox)(void *context);
    uint32_t (*read_ready)(void *context);
    uint32_t (*read_heartbeat)(void *context);
    uint32_t (*read_cpu_id)(void *context);
    uint32_t (*ticks)(void *context);
    uint32_t (*irq_save)(void *context);
    void (*irq_restore)(void *context, uint32_t state);
} fm1_cpu1_smoke_ops_t;

typedef struct {
    uint32_t status;
    uint32_t heartbeat;
    uint32_t cpu_id;
    uint32_t restored;
    uint32_t saved_control;
    uint32_t saved_entry;
    uint32_t saved_debug_enable;
    uint32_t saved_write_enable;
} fm1_cpu1_smoke_result_t;

uint32_t fm1_cpu1_smoke_run(const fm1_cpu1_smoke_ops_t *ops,
                            uint32_t start_entry,
                            uint32_t timeout_ticks,
                            fm1_cpu1_smoke_result_t *result);

/* Device-only fixed-register wrapper; never called at startup. */
uint32_t fm1_cpu1_smoke_run_target(fm1_cpu1_smoke_result_t *result);
