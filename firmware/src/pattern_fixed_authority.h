/* Offline fixed-role, non-adversarial authority policy prototype. */
#ifndef FM1_PATTERN_FIXED_AUTHORITY_H
#define FM1_PATTERN_FIXED_AUTHORITY_H
#include "pattern_pair_authority.h"
/* Slot 12 (index 11) is the stable on-flash authority role. It is not a
 * project slot while this mode is enabled. The working pair remains distinct.
 * The marker is an accidental-collision discriminator, not authentication. */
#define PPA_FIXED_SLOT 11u
/* Configure an existing store's location and stable marker in RAM; callbacks,
 * pair base and exclusive writer fence are supplied by the caller. The caller
 * must preserve this role across builds and never silently choose another. */
int ppa_fixed_configure(ppa_store *);
typedef struct { const ppa_store *authority; void *scratch; uint32_t slot; unsigned state; } ppa_fixed_certificate_context;
/* Compatible with pops_certify_fn. scratch is a dedicated authority sector
 * buffer, separate from all project buffers and the authority configuration.
 * Return exactly 1 only for the requested journal state and pair. */
int ppa_fixed_certificate(void *,uint32_t sector_address);
/* A transaction may validate once and then reuse scratch if an independently
 * verified exclusive fence prevents every flash/journal writer until end.
 * Both the live config and snapshot are transaction-owned and immutable;
 * fence must return exactly 1 only while this same nonzero token is held.
 * The fence and reservation callbacks must not mutate the snapshot/config. */
typedef struct { const ppa_store *live; ppa_store pinned; uint32_t token,slot; void *fence_ctx; int (*fence)(void *,uint32_t); uint8_t states[PS_TOTAL_SLOTS]; unsigned state,active; } ppa_fixed_snapshot;
typedef struct { ppa_fixed_snapshot *snapshot; uint32_t token; } ppa_fixed_snapshot_handle;
int ppa_fixed_snapshot_begin(ppa_fixed_snapshot *,const ppa_store *,void *scratch,uint32_t slot,unsigned state,uint32_t token,void *fence_ctx,int (*fence)(void *,uint32_t));
/* The handle retains its original token and must not be mutated or reused by
 * stale callers. Reusing snapshot storage with a fresh token cannot authorize
 * an old handle. Both objects remain live for the entire operation. */
int ppa_fixed_snapshot_certificate(void *,uint32_t sector_address);
void ppa_fixed_snapshot_end(ppa_fixed_snapshot *);
/* All old-format writes, including implicit working-copy writes, must pass
 * this gate once this model is selected. Reads and preservation are separate. */
int ppa_fixed_legacy_write_allowed(void);
#endif
