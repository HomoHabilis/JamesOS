/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

#define FM1_CPU1_EARLY_COOKIE_START_MAGIC  0x43503141u
#define FM1_CPU1_EARLY_COOKIE_BYPASS_MAGIC 0x43503142u

enum {
    FM1_CPU1_EARLY_COOKIE_NONE = 0u,
    FM1_CPU1_EARLY_COOKIE_START = 1u,
    FM1_CPU1_EARLY_COOKIE_BYPASS = 2u
};

static inline void fm1_cpu1_early_cookie_store(volatile uint32_t *magic,
                                                volatile uint32_t *inverse,
                                                uint32_t kind)
{
    uint32_t value = kind == FM1_CPU1_EARLY_COOKIE_BYPASS
                         ? FM1_CPU1_EARLY_COOKIE_BYPASS_MAGIC
                         : FM1_CPU1_EARLY_COOKIE_START_MAGIC;
    *magic = 0u;
    *inverse = 0u;
    *inverse = ~value;
#if defined(FM1_DEVICE)
    __asm__ volatile("csync" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    *magic = value;
#if defined(FM1_DEVICE)
    __asm__ volatile("csync" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static inline uint32_t fm1_cpu1_early_cookie_consume(volatile uint32_t *magic,
                                                      volatile uint32_t *inverse)
{
    uint32_t value = *magic;
    uint32_t inverse_value = *inverse;
    uint32_t kind = FM1_CPU1_EARLY_COOKIE_NONE;
    if (value == FM1_CPU1_EARLY_COOKIE_START_MAGIC &&
        inverse_value == ~FM1_CPU1_EARLY_COOKIE_START_MAGIC)
        kind = FM1_CPU1_EARLY_COOKIE_START;
    else if (value == FM1_CPU1_EARLY_COOKIE_BYPASS_MAGIC &&
             inverse_value == ~FM1_CPU1_EARLY_COOKIE_BYPASS_MAGIC)
        kind = FM1_CPU1_EARLY_COOKIE_BYPASS;
    *magic = 0u;
    *inverse = 0u;
#if defined(FM1_DEVICE)
    __asm__ volatile("csync" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    return kind;
}
