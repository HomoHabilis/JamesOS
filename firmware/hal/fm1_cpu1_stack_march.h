/* SPDX-License-Identifier: GPL-3.0-only
 * Optional early-boot CPU1 stack RAM March-style test. */
#pragma once

#include <stdint.h>

#ifndef FM1_CPU1_STACK_MARCH_DIAG
#define FM1_CPU1_STACK_MARCH_DIAG 0
#endif
#if defined(FM1_DEVELOPMENT_DIAGNOSTICS) && !FM1_DEVELOPMENT_DIAGNOSTICS && FM1_CPU1_STACK_MARCH_DIAG
#error "Distribution firmware cannot enable the diagnostic CPU1 stack test"
#endif
#if FM1_CPU1_STACK_MARCH_DIAG != 0 && FM1_CPU1_STACK_MARCH_DIAG != 1
#error "FM1_CPU1_STACK_MARCH_DIAG must be 0 or 1"
#endif

#define FM1_CPU1_STACK_MARCH_MAGIC 0x43534D31u /* "CSM1" */
#define FM1_CPU1_STACK_MARCH_WORDS 10u

enum {
    FM1_CPU1_STACK_MARCH_NOT_RUN = 0u,
    FM1_CPU1_STACK_MARCH_PASS = 1u,
    FM1_CPU1_STACK_MARCH_MISMATCH = 2u,
    FM1_CPU1_STACK_MARCH_RESET_NOT_HELD = 3u,
    FM1_CPU1_STACK_MARCH_BOUNDS_INVALID = 4u,
    FM1_CPU1_STACK_MARCH_RESET_LOST = 5u
};

enum {
    FM1_CPU1_STACK_MARCH_HELD_BEFORE = 1u << 0,
    FM1_CPU1_STACK_MARCH_BOUNDS_PROVED = 1u << 1,
    FM1_CPU1_STACK_MARCH_HELD_AFTER = 1u << 2,
    FM1_CPU1_STACK_MARCH_STACKS_REINITIALIZED = 1u << 3
};

typedef struct {
    uint32_t magic;
    uint32_t status;
    uint32_t flags;
    uint32_t first_bad_address;
    uint32_t expected;
    uint32_t actual;
    uint32_t phase;
    uint32_t stack_id;
    uint32_t bytes_checked;
    uint32_t completed_phase_mask;
} fm1_cpu1_stack_march_result_t;

static inline uint32_t fm1_cpu1_stack_march_range_disjoint(
    uintptr_t lo0, uintptr_t hi0, uintptr_t lo1, uintptr_t hi1)
{
    return lo0 < hi0 && lo1 < hi1 && (hi0 <= lo1 || hi1 <= lo0);
}

static inline uint32_t fm1_cpu1_stack_march_range_in_bss(
    uintptr_t lo, uintptr_t bytes, uintptr_t bss_lo, uintptr_t bss_hi)
{
    uintptr_t hi = lo + bytes;
    return lo && bytes && hi >= lo && !(lo & 3u) && !(bytes & 3u) &&
           bss_lo < bss_hi && lo >= bss_lo && hi <= bss_hi;
}

static inline uint32_t fm1_cpu1_stack_march_ranges_safe(
    uintptr_t user_lo, uintptr_t user_bytes,
    uintptr_t system_lo, uintptr_t system_bytes,
    uintptr_t bss_lo, uintptr_t bss_hi,
    uintptr_t c0_user_lo, uintptr_t c0_user_hi,
    uintptr_t c0_system_lo, uintptr_t c0_system_hi)
{
    uintptr_t user_hi = user_lo + user_bytes;
    uintptr_t system_hi = system_lo + system_bytes;
    return fm1_cpu1_stack_march_range_in_bss(
               user_lo, user_bytes, bss_lo, bss_hi) &&
           fm1_cpu1_stack_march_range_in_bss(
               system_lo, system_bytes, bss_lo, bss_hi) &&
           user_hi >= user_lo && system_hi >= system_lo &&
           fm1_cpu1_stack_march_range_disjoint(
               user_lo, user_hi, system_lo, system_hi) &&
           fm1_cpu1_stack_march_range_disjoint(
               c0_user_lo, c0_user_hi, c0_system_lo, c0_system_hi) &&
           fm1_cpu1_stack_march_range_disjoint(
               user_lo, user_hi, c0_user_lo, c0_user_hi) &&
           fm1_cpu1_stack_march_range_disjoint(
               user_lo, user_hi, c0_system_lo, c0_system_hi) &&
           fm1_cpu1_stack_march_range_disjoint(
               system_lo, system_hi, c0_user_lo, c0_user_hi) &&
           fm1_cpu1_stack_march_range_disjoint(
               system_lo, system_hi, c0_system_lo, c0_system_hi);
}

#ifndef FM1_CPU1_STACK_MARCH_STORE
#define FM1_CPU1_STACK_MARCH_STORE(address, value) (*(address) = (uint8_t)(value))
#endif
#ifndef FM1_CPU1_STACK_MARCH_LOAD
#define FM1_CPU1_STACK_MARCH_LOAD(address) (*(address))
#endif
#ifndef FM1_CPU1_STACK_MARCH_BARRIER
#if defined(FM1_DEVICE)
#define FM1_CPU1_STACK_MARCH_BARRIER() __asm__ volatile("csync" ::: "memory")
#else
#define FM1_CPU1_STACK_MARCH_BARRIER() __sync_synchronize()
#endif
#endif

static inline uint32_t fm1_cpu1_stack_march_pattern(uintptr_t word_address,
                                                     uint32_t phase)
{
    switch (phase) {
    case 0u: return 0x00000000u;
    case 1u: return 0xFFFFFFFFu;
    case 2u: return 0xA5A5A5A5u;
    case 3u: return 0x5A5A5A5Au;
    case 4u: return (uint32_t)word_address ^ 0xA55A5AA5u;
    default: return (uint32_t)word_address ^ 0x5AA5A55Au;
    }
}

/* Volatile byte accesses avoid violating the declared uint8_t-array object
 * type of the stacks. Address-derived phases write both stacks before reading
 * either one, which also detects aliases between the two physical ranges. */
static inline uint32_t fm1_cpu1_stack_march_run(
    volatile uint8_t *stack0, uint32_t bytes0,
    volatile uint8_t *stack1, uint32_t bytes1,
    fm1_cpu1_stack_march_result_t *result)
{
    volatile uint8_t *stack[2];
    uint32_t bytes[2], phase, which, offset, byte_index;

    if (!result) return 0u;
    result->magic = FM1_CPU1_STACK_MARCH_MAGIC;
    result->status = FM1_CPU1_STACK_MARCH_BOUNDS_INVALID;
    result->flags = 0u;
    result->first_bad_address = 0u;
    result->expected = 0u;
    result->actual = 0u;
    result->phase = 0u;
    result->stack_id = 0xFFFFFFFFu;
    result->bytes_checked = 0u;
    result->completed_phase_mask = 0u;
    if (!stack0 || !stack1 || !bytes0 || !bytes1 ||
        (bytes0 & 3u) || (bytes1 & 3u))
        return 0u;

    stack[0] = stack0;
    stack[1] = stack1;
    bytes[0] = bytes0;
    bytes[1] = bytes1;

    for (phase = 0u; phase < 6u; ++phase) {
        for (which = 0u; which < 2u; ++which) {
            for (offset = 0u; offset < bytes[which]; offset += 4u) {
                uintptr_t address = (uintptr_t)(stack[which] + offset);
                uint32_t pattern = fm1_cpu1_stack_march_pattern(address, phase);
                for (byte_index = 0u; byte_index < 4u; ++byte_index)
                    FM1_CPU1_STACK_MARCH_STORE(stack[which] + offset + byte_index,
                                               pattern >> (8u * byte_index));
            }
        }
        FM1_CPU1_STACK_MARCH_BARRIER();
        for (which = 0u; which < 2u; ++which) {
            for (offset = 0u; offset < bytes[which]; offset += 4u) {
                uintptr_t address = (uintptr_t)(stack[which] + offset);
                uint32_t pattern = fm1_cpu1_stack_march_pattern(address, phase);
                for (byte_index = 0u; byte_index < 4u; ++byte_index) {
                    uint32_t expected = (pattern >> (8u * byte_index)) & 0xFFu;
                    uint32_t actual = FM1_CPU1_STACK_MARCH_LOAD(
                        stack[which] + offset + byte_index);
                    ++result->bytes_checked;
                    if (actual != expected) {
                        result->status = FM1_CPU1_STACK_MARCH_MISMATCH;
                        result->first_bad_address =
                            (uint32_t)(uintptr_t)(stack[which] + offset + byte_index);
                        result->expected = expected;
                        result->actual = actual;
                        result->phase = phase;
                        result->stack_id = which;
                        return 0u;
                    }
                }
            }
        }
        result->completed_phase_mask |= 1u << phase;
    }
    result->status = FM1_CPU1_STACK_MARCH_PASS;
    return 1u;
}

#if FM1_CPU1_STACK_MARCH_DIAG
void fm1_cpu1_worker_stack_march_publish(
    const fm1_cpu1_stack_march_result_t *result);
void fm1_cpu1_worker_stack_march_snapshot(uint32_t out[FM1_CPU1_STACK_MARCH_WORDS]);
uint32_t fm1_cpu1_worker_stack_march(fm1_cpu1_stack_march_result_t *result);
#endif
