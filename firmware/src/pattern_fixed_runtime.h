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
/* Read six named slots in one exclusive park, without decompression. PRESENT
 * checks record framing and CRCs only; it does not establish loadability. */
#define FM1_PROJECT_LIST_PAGE 6u
enum { FM1_LIST_EMPTY=0, FM1_LIST_PRESENT=1, FM1_LIST_UNAVAILABLE=2 };
typedef struct { uint8_t state; char name[PS_NAME_BYTES]; } fm1_project_list_entry;
int fm1_fixed_runtime_list(fm1_fixed_runtime *,uint32_t first,fm1_project_list_entry out[FM1_PROJECT_LIST_PAGE]);
/* Explicit NEW: commit W00 first, then adopt Pattern 1; caller owns immutable
 * validated factory input disjoint from all runtime/loan/live buffers,
 * configurations and opaque callback contexts for the whole call. */
int fm1_fixed_runtime_new(fm1_fixed_runtime *,const uint8_t *factory,size_t length);
/* Import one immutable PSC3 payload into a named slot, without adopting it.
 * The caller must keep it disjoint from all live and loaned buffers. */
int fm1_fixed_runtime_import(fm1_fixed_runtime *,uint32_t,const char *,const uint8_t *,size_t);
/* Read one validated named pair into caller-owned private snapshot storage.
 * Outputs and buffer must be disjoint from runtime, loans and live state. */
int fm1_fixed_runtime_export(fm1_fixed_runtime *,uint32_t,uint8_t *,size_t,uint32_t *,char [PS_NAME_BYTES]);
#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_RAW_EXPORT_EXPERIMENT) && FM1_PATTERN_RAW_EXPORT_EXPERIMENT && defined(FM1_DEVICE))
/* Storage integrity only; caller must validate project format and semantics. */
int fm1_fixed_runtime_export_raw(fm1_fixed_runtime *,uint32_t,uint8_t *,size_t,uint32_t *,char [PS_NAME_BYTES]);
#endif
#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_DELETE_EXPERIMENT) && FM1_PATTERN_DELETE_EXPERIMENT && defined(FM1_DEVICE))
int fm1_fixed_runtime_delete_experiment(fm1_fixed_runtime *,uint32_t,const char *,const uint8_t digest[32],int explicit_retry);
int fm1_fixed_runtime_inspect_unreadable_experiment(fm1_fixed_runtime *,uint32_t,uint8_t digest[32]);
int fm1_fixed_runtime_clear_unreadable_experiment(fm1_fixed_runtime *,uint32_t,const uint8_t digest[32]);
#endif
#endif
