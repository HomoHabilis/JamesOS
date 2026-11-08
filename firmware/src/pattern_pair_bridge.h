/* Host-only certified adapter for conservative page-streamed pairs. */
#ifndef FM1_PATTERN_PAIR_BRIDGE_H
#define FM1_PATTERN_PAIR_BRIDGE_H
#include "pattern_pair_project.h"
/* Opaque store, source, validator, certificate and guard contexts must be
 * disjoint from every writable buffer and remain valid for the whole fenced
 * transaction; the caller's safe() callback certifies this condition. */
typedef struct { const pps_store *store; fm1_fixed_stage *stage; uint8_t *encoded,*scratch; int (*fence)(void *); int (*faulted)(void *); void *guard_ctx; } fm1_pair_bridge;
/* Suitable as fm1_fixed_bridge_transaction.write_pair. Certifies both exact
 * sector addresses and rechecks the fence on every read/program/erase. */
int fm1_pair_bridge_write(void *,uint32_t,const char *,fm1_project_source *,unsigned initializing,pops_certify_fn,void *);
/* Host first-valid proof for ppa_store.project_ready. Expected name/source
 * must be independently retained and immutable; the role is not identity. */
typedef struct { fm1_pair_bridge *bridge; fm1_project_source *expected_source; const char *expected_name; pops_certify_fn certify; void *certctx; } fm1_pair_first_proof;
int fm1_pair_bridge_first_valid(void *,uint32_t);
/* Suitable as bridge stage_load/stage_working callbacks. */
/* Full verified metadata read; caller output must be private. */
int fm1_pair_bridge_info(void *,uint32_t,pops_certify_fn,void *,ps_info *);
int fm1_pair_bridge_stage(void *,uint32_t,pops_certify_fn,void *);
int fm1_pair_bridge_working(void *,pops_certify_fn,void *,char name[PS_NAME_BYTES]);
#endif
