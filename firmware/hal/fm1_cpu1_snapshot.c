/* SPDX-License-Identifier: GPL-3.0-only
 * Private CPU1 smoke prototype; compiled but never called by app startup. */
#include "fm1_cpu1_snapshot.h"

/* Keep a concrete target object for disassembly review. The private firmware
 * does not call this entry unless a later, separately reviewed opt-in is added. */
__attribute__((section(".text.cpu1_snapshot"), noinline, used))
void fm1_cpu1_snapshot_target(fm1_cpu1_snapshot_t *out)
{
    fm1_cpu1_snapshot_readonly(out);
}
