/* Experimental fixed-pair storage; does not create ownership certificates. */
#ifndef FM1_PATTERN_OWNED_PS_H
#define FM1_PATTERN_OWNED_PS_H
#include "project_store.h"
/* Certification is supplied by an independently pinned, durable authority.
 * It must return exactly 1 only for this slot's exclusively owned sector and
 * remain stable throughout each transaction. Both copies must be certified,
 * including blank and interrupted sectors. Calls and all other flash writers
 * must be serialized, including certification changes. The callback must not
 * modify flash, the store, payload, scratch, or callback state. These functions
 * cannot establish ownership from magic, CRC, erased bytes, or record contents.
 * A corrupt pair without a valid survivor remains inaccessible: provisioning
 * or destructive repair is outside this interface. */
typedef int (*pops_certify_fn)(void *ctx,uint32_t sector_address);
int pops_load(const ps_store *,uint32_t slot,void *scratch,ps_info *,pops_certify_fn,void *);
int pops_save(const ps_store *,uint32_t slot,const char *name,const void *payload,void *scratch,ps_info *,pops_certify_fn,void *);
/* First-save recovery only. The separate callback must certify an externally
 * durable INITIALIZING state for both sectors, established after exclusively
 * reserving and verifying both wholly blank. No other writes to the pair are
 * allowed until the caller has verified this first record and durably moved
 * authority to ACTIVE. Each retry must supply the same name and payload; if
 * a complete record already exists it must match exactly. On success caller
 * still has to persist ACTIVE; this function does not update the authority. */
int pops_initialize(const ps_store *,uint32_t slot,const char *name,const void *payload,void *scratch,ps_info *,pops_certify_fn,void *);
#endif
