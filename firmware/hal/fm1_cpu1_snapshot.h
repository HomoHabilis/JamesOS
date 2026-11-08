/* SPDX-License-Identifier: GPL-3.0-only
 * Private CPU1 smoke prototype: fixed read-only register snapshot only.
 * No core-control writes, no launch, and no change to the installed app.
 */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t c1_con;
    uint32_t cpu1_boot_entry;
} fm1_cpu1_snapshot_t;

void fm1_cpu1_snapshot_target(fm1_cpu1_snapshot_t *out);

/* Primary SDK map: csfr_base 0x01ee0000; corex2(0) starts there;
 * C1_CON is the second 32-bit member. SDK EnableOtherCpu stores its startup
 * address in the fixed CPU1 boot-entry word at 0x01c7fff8. */
#define FM1_CPU1_C1_CON_ADDR       0x01eee004u
#define FM1_CPU1_BOOT_ENTRY_ADDR   0x01c7fff8u

/* Deliberately just two volatile reads, with no writes or implicit launch. */
__attribute__((noinline, used))
static void fm1_cpu1_snapshot_readonly(fm1_cpu1_snapshot_t *out)
{
    out->c1_con = *(volatile const uint32_t *)FM1_CPU1_C1_CON_ADDR;
    out->cpu1_boot_entry = *(volatile const uint32_t *)FM1_CPU1_BOOT_ENTRY_ADDR;
}

/* Bit 1 is asserted by the vendor exception-reset path and cleared by
 * EnableOtherCpu before release. This predicate only qualifies a snapshot;
 * it does not itself start or reset a core. */
static inline int fm1_cpu1_snapshot_is_reset(const fm1_cpu1_snapshot_t *s)
{
    return (s->c1_con & (1u << 1)) != 0u;
}

static inline void fm1_cpu1_snapshot_encode(const fm1_cpu1_snapshot_t *s,
                                             uint8_t out[8])
{
    uint32_t i;
    for (i = 0; i < 4u; ++i) {
        out[i] = (uint8_t)(s->c1_con >> (8u * i));
        out[4u + i] = (uint8_t)(s->cpu1_boot_entry >> (8u * i));
    }
}
