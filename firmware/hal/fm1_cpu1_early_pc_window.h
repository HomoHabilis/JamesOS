/* SPDX-License-Identifier: GPL-3.0-only
 * Observed WL82 reset PC windows from ID41 read-only snapshot. Keep this
 * predicate exact: a different captured range must refuse the CPU1 probe. */
#pragma once
#include <stdint.h>

#define FM1_CPU1_EARLY_PC0_L 0x00000000u
#define FM1_CPU1_EARLY_PC0_H 0x07FFFFFFu
#define FM1_CPU1_EARLY_PC1_L 0x00000000u
#define FM1_CPU1_EARLY_PC1_H 0x07FFFFFFu

static inline uint32_t fm1_cpu1_early_pc_windows_match(uint32_t pc0_l,
                                                        uint32_t pc0_h,
                                                        uint32_t pc1_l,
                                                        uint32_t pc1_h)
{
    return pc0_l == FM1_CPU1_EARLY_PC0_L && pc0_h == FM1_CPU1_EARLY_PC0_H &&
           pc1_l == FM1_CPU1_EARLY_PC1_L && pc1_h == FM1_CPU1_EARLY_PC1_H;
}
