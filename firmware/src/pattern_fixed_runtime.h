/* Host-only concrete composition of project loans, pair ownership and bridge. */
#ifndef FM1_PATTERN_FIXED_RUNTIME_H
#define FM1_PATTERN_FIXED_RUNTIME_H
#if (!defined(FM1_PAIR_STREAM_HOST_TEST) || !FM1_PAIR_STREAM_HOST_TEST || defined(FM1_DEVICE)) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
#error "Fixed runtime composition is host-only and target-unqualified"
#endif
#include "pattern_pair_owner.h"
#include "pattern_pair_bridge.h"
/* No default target access. The caller must independently guarantee the park
 * excludes every writer, the loans stay live, and the exact-pair policy is
 * durable across updates/rollback. The port guarantees all borrowed stage
 * buffers are disjoint from live engine state and opaque callback contexts;
 * adopt must be failure-atomic after can_adopt. Returning 1 alone is not
 * physical proof. */
typedef struct {
 void *ctx;
 int (*begin)(void *,int mode,fm1_project_adapter **,const fm1_project_adapter_ops **,uint32_t *,br_pattern_park_io **,size_t *sector_capacity);
 int (*set_mode)(void *,uint32_t,int);
 int (*end)(void *,uint32_t);
 int (*fence)(void *,uint32_t);
 /* Only exactly 0 means no uncontained fault; all other values fail closed. */
 int (*faulted)(void *);
 int (*pair_state)(void *,uint32_t,uint32_t,int write,unsigned *initializing);
 int (*certificate)(void *,uint32_t,uint32_t,unsigned,uint32_t);
 int (*activate)(void *,uint32_t,uint32_t);
 int (*commit_slot)(void *,uint32_t,int);
 uint8_t role[16];
 unsigned named_slots;
 /* Opt-in non-adversarial fixed partition: fully scan both candidate pairs
  * under one exclusive fence and derive the exact address capability. */
 unsigned fixed_partition_auto;
} fm1_fixed_runtime_port;
struct fm1_fixed_runtime;
typedef struct { struct fm1_fixed_runtime *runtime; uint32_t token,slot; unsigned initializing,active; } fm1_fixed_runtime_cert;
typedef struct fm1_fixed_runtime {
 fm1_fixed_bridge bridge; fm1_fixed_bridge_ops bridge_ops; const fm1_fixed_runtime_port *port; fm1_fixed_runtime_port pinned;
 fm1_project_adapter *adapter; const fm1_project_adapter_ops *ops; br_pattern_park_io *park; uint32_t token,next_lease; unsigned active,mode,opened,states[2];
 pps_owner owner[2]; pps_owner_lease lease[2]; fm1_fixed_runtime_cert cert[2];
 uint8_t factory_arena[1]; fm1_fixed_stage stage; fm1_pair_bridge pair; br_pattern_bank staged_bank,old_bank; fm1_pattern_song staged_state,old_state;
} fm1_fixed_runtime;
/* Zero initialize once; no binding performs I/O. */
int fm1_fixed_runtime_bind(fm1_fixed_runtime *,const fm1_fixed_runtime_port *);
/* Full read-only scan; empty only when both whole sectors are erased. The
 * caller supplies private output disjoint from live engine state, all loaned
 * buffers, callback contexts and configurations. */
int fm1_fixed_runtime_scan(fm1_fixed_runtime *,uint32_t,char name[PS_NAME_BYTES]);
/* Explicit NEW: commit W00 first, then adopt Pattern 1; caller owns immutable
 * validated factory input disjoint from all runtime/loan/live buffers,
 * configurations and opaque callback contexts for the whole call. */
int fm1_fixed_runtime_new(fm1_fixed_runtime *,const uint8_t *factory,size_t length);
#endif
