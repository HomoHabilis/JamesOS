/* Pattern-only target seam. Unbound until independently qualified. */
#ifndef FM1_PATTERN_FIXED_TARGET_H
#define FM1_PATTERN_FIXED_TARGET_H
#include "pattern_fixed_bridge.h"
typedef struct {
    void *context;
    fm1_fixed_bridge *bridge;
    /* A valid working record alone does not authorize replacing a live take. */
    int (*recovery_authorized)(void *);
    int (*current_slot)(void *,int *);
    /* Scan returns PFS_OK, PFS_EMPTY, PFS_INVALID, or PFS_IO, and may not
     * classify unknown or failed reads as empty. It owns its fence. */
    int (*scan)(void *,unsigned,char [PS_NAME_BYTES]);
} fm1_fixed_target_config;
typedef struct { const fm1_fixed_target_config *config; unsigned recovered,fault; } fm1_fixed_target;
/* Host-only binding for exercising orchestration; real targets always reject.
 * Config, callbacks and the initialized bridge must remain immutable and live
 * throughout every call; calls are foreground-only and serialized. */
int fm1_fixed_target_bind(fm1_fixed_target *,const fm1_fixed_target_config *);
int fm1_fixed_target_ready(const fm1_fixed_target *);
int fm1_fixed_target_scan(fm1_fixed_target *,unsigned,char [PS_NAME_BYTES]);
int fm1_fixed_target_save(fm1_fixed_target *,unsigned,const char *);
int fm1_fixed_target_load(fm1_fixed_target *,unsigned);
/* Explicit, separately authorized startup recovery. No recovery on bind.
 * Slot output is caller-owned private storage, disjoint from all live state,
 * callbacks and transaction workspaces; publication happens only in the
 * bridge commit callback while its fence is held. */
int fm1_fixed_target_recover(fm1_fixed_target *,int *);
#endif
