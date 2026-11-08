/* Host-only provenance prototype. No target backend or installer wiring. */
#ifndef FM1_PATTERN_NAMESPACE_H
#define FM1_PATTERN_NAMESPACE_H
#if defined(FM1_DEVICE) && (!defined(FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT) || !FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT)
#error "Namespace remains host-only unless explicitly included in a link-only target experiment"
#endif
#include "pattern_flash_store.h"
#include <stddef.h>
#include <stdint.h>
#define PNS_DOMAIN_BYTES 16u
#define PNS_HASH_BYTES 32u
/* Callbacks must be serialized with all flash writers. `digest` must be a
 * collision-resistant 256-bit hash (e.g. SHA-256). `reserved` must certify
 * exactly 1 only for a sector independently verified erased and exclusively
 * reserved by this transaction before any byte was programmed. */
/* root and sector arguments are sector indices relative to store->base;
 * reservation/reclaim/erase callbacks receive absolute flash offsets. */
typedef struct {
 const pfs_store *store; void *ctx;
 int (*digest)(void *,const void *,size_t,uint8_t out[PNS_HASH_BYTES]);
 /* Narrow token-scoped root-journal writer, not the general sector writer.
  * It must permit only the verified current cursor/body/commit sequence. */
 int (*journal_program)(void *,uint32_t,const void *,uint32_t);
 int (*reserved)(void *,uint32_t);
 /* Separate, narrow authority for a pinned peer after committed-intent and
  * exact-prefix validation. It must only grant a lease: it may not mutate
  * flash or scratch, and must not authorize general nonblank writes. */
 int (*recover_peer)(void *,uint32_t root,uint32_t peer,const uint8_t expected_hash[32]);
 /* Exactly 1 only after every manifest/reference has been checked and this
  * sector is an obsolete, exclusively owned GC candidate. */
 int (*reclaimable)(void *,uint32_t);
 int (*erase)(void *,uint32_t);
 uint32_t root, peer; uint8_t expected[PNS_DOMAIN_BYTES];
} pns_namespace;
/* Shared journal CRC; used for validation by the cursor gate as well. */
uint32_t pns_crc32(const void *,size_t);
/* Compare a live namespace/store to immutable transaction snapshots. */
int pns_namespace_unchanged(const pns_namespace *,const pns_namespace *,const pfs_store *);
/* Adapter for the separately tested bounded SHA-256 implementation. */
int pns_sha256_digest(void *,const void *,size_t,uint8_t out[PNS_HASH_BYTES]);
enum { PNS_OK=0, PNS_FULL=1, PNS_PENDING=2, PNS_IO=-1, PNS_INVALID=-2, PNS_CORRUPT=-3 };
/* Domain must come from a trusted CSPRNG and be pinned independently of this
 * mixed-use partition. Caller provides one 4096-byte disposable scratch. */
int pns_provision(pns_namespace *,void *scratch);
int pns_open(const pns_namespace *,void *scratch);
/* Both root indices and domain must be independently pinned. Choose the
 * newest fully valid committed root; torn peer bytes are never erased. */
int pns_open_pair(pns_namespace *,void *scratch);
/* Rotate live claims to the wholly erased, exclusively reserved peer; commit
 * its header last. Retiring the old root needs separate authorization. */
int pns_rotate(pns_namespace *,void *scratch);
/* Calculate the only legal next-rotation image hash from the current root. */
int pns_rotation_expected(pns_namespace *,void *scratch,uint8_t out[32]);
/* Explicit write operation: only after pinned-root ownership is certified,
 * continue an exact-prefix interrupted rotation with a committed intent. */
int pns_recover_rotation(pns_namespace *,void *scratch);
int pns_retire_peer(pns_namespace *,void *scratch);
/* Claim only after a newly reserved sector is fully written and verified. */
int pns_claim(const pns_namespace *,uint32_t sector,void *scratch);
/* Only exact currently claimed bytes are owned; returns 1, 0, or <0 error. */
int pns_owned(const pns_namespace *,uint32_t sector,void *scratch);
/* Commit revocation BEFORE erase; interrupted erases may strand a sector but
 * never resurrect a claim. The erase callback must independently enforce
 * exclusive flash ownership and a verified parked worker on target. */
int pns_revoke_erase(const pns_namespace *,uint32_t sector,void *scratch);
#endif
