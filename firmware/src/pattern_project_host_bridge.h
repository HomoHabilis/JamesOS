/* Host-only UI bridge; not linked into device firmware. */
#ifndef FM1_PROJECT_HOST_BRIDGE_H
#define FM1_PROJECT_HOST_BRIDGE_H
#if defined(FM1_DEVICE)
#error "Host project bridge cannot be used on device"
#endif
#include "pattern_project_adapter.h"
/* Caller keeps the configuration, operations and arenas alive. Both arenas
 * must be distinct and not overlap the current owner; after a load, one is
 * transferred to the owner and the next load rotates to the other. can_access
 * must reject reserved/foreign slots, returning exactly 1 only when safe. The
 * new_project callback must be an atomic quiescent session+owner reset. */
typedef struct {
 const pfs_store *store; const fm1_project_adapter_ops *ops;
 fm1_project_adapter *adapter;
 uint8_t *arena[2]; size_t capacity[2];
 void *context;
 int (*can_access)(void *,uint32_t,int write);
 int (*new_project)(void *);
} br_pp_host_config;
int br_pp_host_configure(const br_pp_host_config *);
int br_pp_host_scan(unsigned,char name[13]);
int br_pp_host_save(unsigned,const char *);
int br_pp_host_load(unsigned);
int br_pp_host_init_project(void);
int br_pp_host_working(void);
int br_pp_host_recover(int *slot);
/* Explicit recovery calls the configured atomic engine/owner reset; only a
 * successful reset can clear an indeterminate-state latch. */
int br_pp_host_reset_fault(void);
int br_pp_host_faulted(void);
#endif
