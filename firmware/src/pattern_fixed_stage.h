/* Host-only concrete staging callbacks for the fixed bridge. */
#ifndef FM1_PATTERN_FIXED_STAGE_H
#define FM1_PATTERN_FIXED_STAGE_H
#include "pattern_fixed_bridge.h"
typedef struct {
 const ps_store *store; uint8_t *payload;
 fm1_project_session_valid valid; void *validate_ctx;
 fm1_project_work *work; uint8_t *session; br_pattern_bank *bank;
 uint8_t *arena; size_t capacity; br_pattern_snapshot *live;
 fm1_pattern_song *state;
 /* Exactly 1 only while the transaction fence is held and all disposable
  * outputs are disjoint from retained live state, immutable save source,
  * and opaque validation/certificate/guard contexts and configurations. */
 /* faulted returns exactly 0 only when containment is verified. */
 int (*safe)(void *); int (*faulted)(void *); void *guard_ctx;
} fm1_fixed_stage;
/* Validate staging layout and caller-supplied fence/disjointness guard. */
int fm1_fixed_stage_ready(fm1_fixed_stage *);
/* Region must be disjoint from every staging/configuration buffer. */
int fm1_fixed_stage_source_separate(fm1_fixed_stage *,const void *,size_t);
/* Compatible with fm1_fixed_preflight_fn and bridge stage callbacks. */
int fm1_fixed_stage_preflight(void *,const uint8_t *,size_t);
int fm1_fixed_stage_load(void *,uint32_t,pops_certify_fn,void *);
int fm1_fixed_stage_working(void *,pops_certify_fn,void *,char name[PS_NAME_BYTES]);
#endif
