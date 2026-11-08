/* Offline authority journal prototype. No target backend or provisioning. */
#ifndef FM1_PATTERN_PAIR_AUTHORITY_H
#define FM1_PATTERN_PAIR_AUTHORITY_H
#include "project_store.h"
#include <stdint.h>
typedef struct {
 void *ctx;
 int (*read)(void *,uint32_t,void *,uint32_t);
 int (*program)(void *,uint32_t,const void *,uint32_t);
 /* Exact 1 only for sectors independently and durably reserved for this use. */
 int (*reserved)(void *,uint32_t);
 /* Exact 1 only after the first project was independently loaded and verified. */
 int (*project_ready)(void *,uint32_t);
 /* authority can be outside the pair range, or the first sector of a named
  * pair within it. The latter reserves the entire pair and disables that
  * project slot. The working-project pair cannot be used for authority. */
 uint32_t authority,pairs_base;
 /* Obtained from an independently pinned identity, never from the journal. */
 uint8_t identity[16];
} ppa_store;
enum { PPA_OK=0,PPA_EMPTY=1,PPA_FULL=2,PPA_IO=-1,PPA_INVALID=-2,PPA_CORRUPT=-3 };
enum { PPA_UNCLAIMED=0,PPA_INITIALIZING=1,PPA_ACTIVE=2 };
/* All calls require one disjoint 4096-byte scratch, persistent independent
 * address/identity pinning, stable exclusive reservations and a fence against
 * all writers. Callbacks must not modify scratch or authority metadata.
 * program must respect page boundaries internally. No operation erases flash.
 * Interrupted authority/entry programming fails closed; repair or rotation
 * requires separately authorized machinery. Never infer reservation from FF. */
int ppa_open(const ppa_store *,void *scratch,uint8_t states[PS_TOTAL_SLOTS]);
/* Returns the reserved named slot index, -1 if outside the pair range, or -2
 * for an invalid in-range location (including the working-project pair). */
int ppa_authority_slot(const ppa_store *);
int ppa_provision(const ppa_store *,void *scratch);
/* Initialize reserves an unclaimed pair only if both entire sectors are blank.
 * Activate requires an independently verified first project and its reservation.
 * A successful entry is durable only after this function returns PPA_OK. */
int ppa_transition(const ppa_store *,uint32_t slot,unsigned state,void *scratch);
/* Complete an exact-prefix interrupted final entry only under independent,
 * durable proof of that specific intended transition. The proof callback
 * returns exactly 1 only if the intended slot/state was recorded before the
 * interrupted append, the transaction remains exclusively fenced, and, for
 * ACTIVE, the first project has independently been verified. Neither proof
 * nor reservations may be inferred from the torn bytes. The remaining journal
 * must be erased and all earlier entries valid. No sector is ever erased.
 * The program callback must accept reprogramming identical NOR bits. A fully
 * committed entry is already recovered by ppa_open and is not rewritten. */
typedef int (*ppa_recovery_proof_fn)(void *,uint32_t slot,unsigned state);
int ppa_recover_tail(const ppa_store *,uint32_t slot,unsigned state,void *scratch,ppa_recovery_proof_fn,void *proof_ctx);
#endif
