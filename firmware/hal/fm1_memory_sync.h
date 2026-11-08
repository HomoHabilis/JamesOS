/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Publish ordered shared-memory state to the other core on FM-1. */
static inline __attribute__((always_inline)) void fm1_memory_publish_sync(void)
{
#if defined(FM1_DEVICE)
    __asm__ volatile("csync" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}
