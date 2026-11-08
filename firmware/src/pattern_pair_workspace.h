/* Host-only model of the target's proposed pattern-storage scratch loan. */
#ifndef FM1_PATTERN_PAIR_WORKSPACE_H
#define FM1_PATTERN_PAIR_WORKSPACE_H
#if (!defined(FM1_PAIR_STREAM_HOST_TEST) || !FM1_PAIR_STREAM_HOST_TEST || defined(FM1_DEVICE)) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
#error "Pattern pair workspace is host-only until target loans are qualified"
#endif
#include <stddef.h>
#include <stdint.h>
#define PPS_WORKSPACE_ENCODED 3584u
#define PPS_WORKSPACE_PAGE 256u
#define PPS_WORKSPACE_TOTAL (PPS_WORKSPACE_ENCODED+2u*PPS_WORKSPACE_PAGE)
typedef struct { const void *address; size_t length; } pps_workspace_range;
typedef int (*pps_workspace_fence)(void *,uint32_t);
typedef struct { uint8_t *encoded,*writer_page,*owner_page; void *context; pps_workspace_fence fence; uint32_t token; unsigned active,retired; } pps_workspace;
/* Zero initialize once; a released object is permanently retired. Exclusions
 * must enumerate every live/callback/config/rollback/other-loan
 * region and remain immutable while the loan is active. Exact 1 from fence
 * attests the same held token and exclusive worker/editor/flash ownership.
 * This model cannot itself prove the list complete or a target is quiet. */
int pps_workspace_acquire(pps_workspace *,void *,size_t,const pps_workspace_range *,size_t,pps_workspace_fence,void *,uint32_t);
/* Caller must recheck before every use; returned pointers do not outlive loan. */
int pps_workspace_valid(const pps_workspace *,uint32_t);
/* On lost fence, keep the loan intact until independent containment restores
 * a predicate capable of certifying safe release. */
int pps_workspace_release(pps_workspace *,uint32_t);
#endif
