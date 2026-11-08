/* SPDX-License-Identifier: GPL-3.0-only */
/* Sticky policy shared by the CPU1 HAL and host ownership regressions. */
#pragma once
#include <stdint.h>

static inline uint32_t fm1_cpu1_worker_note_reset_confirmation(
    volatile uint32_t *uncontained_fault, uint32_t reset_confirmed)
{
    if (!reset_confirmed) *uncontained_fault = 1u;
    return reset_confirmed;
}

static inline uint32_t fm1_cpu1_worker_note_reset_held(
    volatile uint32_t *uncontained_fault, uint32_t worker_fault,
    uint32_t reset_held)
{
    if (worker_fault && !reset_held) *uncontained_fault = 1u;
    return reset_held;
}
