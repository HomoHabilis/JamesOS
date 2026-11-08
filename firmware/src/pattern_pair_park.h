/* Host-only integration experiment: no target flash authority is granted. */
#ifndef FM1_PATTERN_PAIR_PARK_H
#define FM1_PATTERN_PAIR_PARK_H
#if !defined(FM1_PAIR_STREAM_HOST_TEST) || !FM1_PAIR_STREAM_HOST_TEST || defined(FM1_DEVICE)
#error "Pair/park adapter is restricted to host-only experiments"
#endif
#include "pattern_pair_stream.h"
#include "pattern_park_io.h"
/* Fence and durable ownership attestations must return exactly 1 for the same
 * nonzero token. Both sectors must be independently reserved to the selected
 * role, across resets and all legacy/updater writers. Immutable inputs and the
 * parked transaction must outlive this adapter. No such target authority is
 * currently implemented; the role marker alone is never sufficient. */
typedef int (*pps_park_proof)(void *,uint32_t,uint32_t);
typedef int (*pps_park_reserved_proof)(void *,uint32_t,uint32_t,const uint8_t role[16]);
typedef struct {
    br_pattern_park_io *park; pps_store *store; void *proof_ctx; pps_park_proof fenced; pps_park_reserved_proof reserved;
    uint8_t role[16];
    uint32_t token,slot,first,erased; uint16_t programmed; unsigned has_erased;
} pps_park_adapter;
int pps_park_bind(pps_park_adapter *,pps_store *,br_pattern_park_io *,uint32_t token,uint32_t slot,
                  const uint8_t role[16],pps_validate,void *,pps_park_proof,pps_park_reserved_proof,void *);
#endif
