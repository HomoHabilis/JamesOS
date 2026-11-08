/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "../hal/fm1_fault_diag.h"

/* FX progress is a diagnostic cursor, not a synchronization primitive.
 * Layout: stage[14:7], sample[6:2], part[1:0]; zero means unknown. */
enum {
    BR_FX_PROGRESS_ENTRY = 1u,
    BR_FX_PROGRESS_BUS_READ = 2u,
    BR_FX_PROGRESS_DRIVE = 3u,
    BR_FX_PROGRESS_MOD = 4u,
    BR_FX_PROGRESS_DELAY = 5u,
    BR_FX_PROGRESS_REVERB = 6u,
    BR_FX_PROGRESS_WET_MIX = 7u,
    BR_FX_PROGRESS_OUTPUT = 8u,
    BR_FX_PROGRESS_DONE = 9u
};

#define BR_FX_PROGRESS_RETAINED_MAGIC 0x46585031u /* "FXP1" */
#define BR_FX_PROGRESS_RETAINED_VERSION 1u

static inline __attribute__((always_inline)) uint32_t
br_fx_progress_pack(uint32_t part, uint32_t sample, uint32_t stage)
{
    return ((stage & 0xffu) << 7) | ((sample & 0x1fu) << 2) | (part & 3u);
}

static inline __attribute__((always_inline)) uint32_t
br_fx_progress_part(uint32_t tag)
{
    return tag & 3u;
}

static inline __attribute__((always_inline)) uint32_t
br_fx_progress_sample(uint32_t tag)
{
    return (tag >> 2) & 0x1fu;
}

static inline __attribute__((always_inline)) uint32_t
br_fx_progress_stage(uint32_t tag)
{
    return (tag >> 7) & 0xffu;
}

/* One aligned volatile store followed by a compiler-only ordering barrier. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static inline __attribute__((always_inline)) void
br_fx_progress_store_force(volatile uint32_t tags[2], uint32_t core,
                           uint32_t part, uint32_t sample, uint32_t stage)
{
    tags[core] = br_fx_progress_pack(part, sample, stage);
    __asm__ volatile("" ::: "memory");
}
static inline __attribute__((always_inline)) void
br_fx_progress_store(volatile uint32_t tags[2], uint32_t core,
                     uint32_t part, uint32_t sample, uint32_t stage)
{
#if defined(BR_FX_JOB_PROGRESS_ONLY) && BR_FX_JOB_PROGRESS_ONLY
    if (stage == BR_FX_PROGRESS_ENTRY)
        br_fx_progress_store_force(tags, core, part, sample, stage);
#else
    br_fx_progress_store_force(tags, core, part, sample, stage);
#endif
}
#else
#define br_fx_progress_store_force(tags, core, part, sample, stage) ((void)0)
#define br_fx_progress_store(tags, core, part, sample, stage) ((void)0)
#endif

uint32_t br_fx_progress_get(uint32_t core);
/* Called once after NOINIT is available and BSS has been cleared, before audio. */
void br_fx_progress_bootstrap(void);
/* out: previous-valid, previous[0..1], current[0..1]. No address arguments. */
void br_fx_progress_read(uint32_t out[5]);
