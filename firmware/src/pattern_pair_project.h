/* Host-only streaming-pair/project composition; no device binding. */
#ifndef FM1_PATTERN_PAIR_PROJECT_H
#define FM1_PATTERN_PAIR_PROJECT_H
#include "pattern_pair_stream.h"
#include "pattern_fixed_stage.h"
/* Configure pps_store.validate as fm1_pair_project_validate and validate_ctx
 * as this same staging object. Source and callback state remain immutable and
 * all writers are fenced; opaque contexts are disjoint from writable buffers. */
int fm1_pair_project_validate(void *,pps_read_at,void *,uint32_t);
int fm1_pair_project_buffers_ready(const pps_store *,fm1_fixed_stage *,uint8_t *,uint8_t *);
/* encoded[3584] and scratch[256] are caller-owned, disjoint, and disposable.
 * FULL is returned before any flash operation; all stage data is disposable. */
int fm1_pair_project_save(const pps_store *,uint32_t,const char *,fm1_project_source *,fm1_fixed_stage *,uint8_t *encoded,uint8_t *scratch);
/* On failure discard stage/output buffers; metadata publishes only on success. */
int fm1_pair_project_load(const pps_store *,uint32_t,fm1_fixed_stage *,uint8_t *encoded,uint8_t *scratch,ps_info *);
#endif
