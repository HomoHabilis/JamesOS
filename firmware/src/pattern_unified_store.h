/* Experimental feature-gated unified legacy/PFS routing. Host-only. */
#ifndef PATTERN_UNIFIED_STORE_H
#define PATTERN_UNIFIED_STORE_H
#include "project_store.h"
#include "pattern_flash_store.h"
typedef struct { const ps_store *legacy; uint8_t *sector; uint32_t armed; } pus_guard;
/* Guard storage is a separate aligned 4096-byte buffer. All calls must be
 * serialized across BOTH stores. Only this wrapper may issue legacy writes. */
int pus_legacy_save(const ps_store *,uint32_t slot,const char *name,const void *payload,void *ps_scratch,void *guard_sector,ps_info *);
#endif
