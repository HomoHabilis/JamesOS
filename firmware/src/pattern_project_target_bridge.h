/* Gated project/UI bridge. No default flash policy or device enablement. */
#ifndef FM1_PATTERN_PROJECT_TARGET_BRIDGE_H
#define FM1_PATTERN_PROJECT_TARGET_BRIDGE_H
#if !defined(FM1_PATTERN_PROJECT_TARGET_EXPERIMENT) || !FM1_PATTERN_PROJECT_TARGET_EXPERIMENT
#error "Target project bridge requires the explicit experiment gate"
#endif
#include "pattern_target_storage.h"
/* The integrator must prove every legacy/foreign-sector interaction and
 * provide a policy returning exactly 1 only for authorized access.
 * empty_is_safe returns 1 only when a missing valid PFS generation is
 * independently certified safe to treat as empty, not merely a failed scan. Binding a
 * policy does not enable physical I/O: begin/store retain their own interlock.
 * The factory callback must only copy the immutable validated v16 factory
 * image; it must not restore or modify live state. Bind only while no project
 * operation is active, and keep configuration/callback storage alive. All
 * calls are foreground-only, synchronous and own their worker/IRQ fence. */
typedef struct { void *context; int (*can_access)(void *,const pfs_store *,void *sector,uint32_t,int write); int (*factory)(void *,uint8_t *,size_t); int (*empty_is_safe)(void *,const pfs_store *,void *sector,uint32_t); } br_pp_target_config;
int br_pp_target_bind(const br_pp_target_config *);
int br_pp_target_ready(void);
int br_pp_target_scan(unsigned,char name[13]);
int br_pp_target_save(unsigned,const char *);
int br_pp_target_load(unsigned);
int br_pp_target_init_project(void);
int br_pp_target_recover(int *);
#endif
