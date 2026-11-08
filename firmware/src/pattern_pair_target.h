/* Link-only target seam. This does not supply or certify flash ownership. */
#ifndef FM1_PATTERN_PAIR_TARGET_H
#define FM1_PATTERN_PAIR_TARGET_H
#include "pattern_pair_park.h"
typedef struct { pps_park_adapter adapter; pps_store store; unsigned active,retired; } pps_target;
/* Zero-initialize once. The target, store, park transaction, proof callbacks,
 * role and validator remain live and exclusively owned until end. Do not reuse
 * the target object until every reference to its store is permanently gone. */
int pps_target_begin(pps_target *,br_pattern_park_io *,uint32_t token,uint32_t slot,
                     const uint8_t role[16],pps_validate,void *,pps_park_proof,
                     pps_park_reserved_proof,void *);
pps_store *pps_target_store(pps_target *);
void pps_target_end(pps_target *);
#endif
