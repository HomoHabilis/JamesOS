/* Host-only composition experiment. No device binding or ownership source. */
#ifndef FM1_FIXED_PROJECT_H
#define FM1_FIXED_PROJECT_H
#include "pattern_compressed_project.h"
#include "pattern_owned_ps.h"
enum { FM1_FIXED_FULL=3 };
#define FM1_FIXED_PAYLOAD PS_MAX_PAYLOAD
#define FM1_FIXED_COMPRESSED_MAX (FM1_FIXED_PAYLOAD-4u)
/* Both 3584-byte buffers must be disjoint from each other, the immutable
 * source, callback state and all live data. A certificate must independently
 * attest ownership as required by pops_save. No output is adopted on error. */
/* Status translation for a future bridge; this does not bind a UI. */
int fm1_fixed_project_bridge_result(int);
int fm1_fixed_project_valid(const void *,uint32_t);
/* Preflight a writable region against every retained source buffer. */
int fm1_fixed_project_source_separate(const fm1_project_source *,const void *,size_t);
/* preflight validates the entire inner project in separate staging without
 * changing the payload or live state; exactly 1 is required. */
typedef int (*fm1_fixed_preflight_fn)(void *,const uint8_t *,size_t);
int fm1_fixed_project_save(const ps_store *,uint32_t,const char *,fm1_project_source *,uint8_t *,uint8_t *,fm1_fixed_preflight_fn,void *,pops_certify_fn,void *);
/* First write/retry only, with independently durable INITIALIZING authority
 * and the exact same name/payload on every retry, as in pops_initialize. */
int fm1_fixed_project_initialize(const ps_store *,uint32_t,const char *,fm1_project_source *,uint8_t *,uint8_t *,fm1_fixed_preflight_fn,void *,pops_certify_fn,void *);
/* `payload` is disposable. All project outputs/work are disposable staging,
 * separate from payload and live state. Adopt only after success. */
int fm1_fixed_project_load(const ps_store *,uint32_t,uint8_t *,pops_certify_fn,void *,fm1_project_session_valid,void *,fm1_project_work *,uint8_t *,br_pattern_bank *,uint8_t *,size_t,br_pattern_snapshot *,fm1_pattern_song *);
/* Returns verified record metadata only after the full decoded project passes.
 * info is disjoint from all other arguments, callback state and live data. */
int fm1_fixed_project_load_info(const ps_store *,uint32_t,uint8_t *,pops_certify_fn,void *,fm1_project_session_valid,void *,fm1_project_work *,uint8_t *,br_pattern_bank *,uint8_t *,size_t,br_pattern_snapshot *,fm1_pattern_song *,ps_info *);
#endif
