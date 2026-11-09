/* Concrete, non-adversarial host-only pair ownership provider. */
#ifndef FM1_PATTERN_PAIR_OWNER_H
#define FM1_PATTERN_PAIR_OWNER_H
#if (!defined(FM1_PAIR_STREAM_HOST_TEST) || !FM1_PAIR_STREAM_HOST_TEST || defined(FM1_DEVICE)) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
#error "Concrete pair owner is host-only"
#endif
#include "pattern_pair_stream.h"
#include "pattern_park_io.h"
/* A caller-owned, unique token is active for exactly one parked transaction.
 * Bind, end and lease changes are serialized by the caller; tokens must not
 * be reused while any stale handle may exist. The park submission fence must
 * cover every legacy, updater and project
 * writer throughout and across transitions. Revocation is permanent for all
 * handles with that token. Ownership is a non-adversarial convention: copied
 * or forged valid records do not authenticate the pair's history. All fence,
 * validation and lower-layer policy callbacks must be read-only with respect
 * to flash, owner and lease state. The fence must remain effective through
 * each raw operation. End is ignored while an erase callback is active. */
typedef struct { uint32_t token; unsigned active,consumed; void *holder; } pps_owner_lease;
typedef struct {
 pps_store store; br_pattern_park_io *park; br_pattern_park_ops pinned_ops;
 pps_owner_lease *lease; void *scratch; uint32_t token,slot,first,erased,armed;
 uint16_t programmed; unsigned active,retired,has_erased,erase_armed; uint8_t role[16];
 pps_validate validate; void *validate_ctx;
} pps_owner;
/* Zero initialize once. scratch is a dedicated 256-byte provider buffer,
 * disjoint from writer/validator scratch, source, owner, lease and park. The
 * park must already be acquired and all configuration remains immutable. The
 * one-owner claim is spent until the park transaction ends successfully.
 * It supplies no target permission and cannot bypass park->may_erase. */
int pps_owner_bind(pps_owner *,br_pattern_park_io *,pps_owner_lease *,uint32_t token,uint32_t slot,const uint8_t role[16],pps_validate,void *validate_ctx,void *scratch);
pps_store *pps_owner_store(pps_owner *);
/* Lower park policy can call this synchronously from may_erase. Exactly 1 is
 * returned only during the owner-authorized erase, for its preflighted sector
 * under the same live lease and fence; ordinary calls grant no authority. */
int pps_owner_may_erase(pps_owner *,uint32_t sector_address);
/* Explicit project replacement: erase only fully scanned incompatible copies,
 * retaining valid survivors. Requires the same live parked owner and fence. */
int pps_owner_reclaim_incompatible(pps_owner *);
/* Serialized one-way transfer from a named slot to the working pair while the
 * SAME park/freeze remains held. next, fresh_lease and scratch are live for
 * the rest of the transaction; next_token is distinct from all stale tokens.
 * The old owner and all copied handles are permanently retired. A failed
 * preflight never releases the outer park fence. No further pair handoff is
 * supported. All operations and handle lifetimes must remain serialized. */
int pps_owner_handoff_working(pps_owner *old,pps_owner *next,pps_owner_lease *fresh_lease,uint32_t next_token,void *scratch);
void pps_owner_end(pps_owner *);
#endif
