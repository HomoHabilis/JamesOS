/* Offline legacy-v16 migration primitive. Not connected to device writes. */
#ifndef PATTERN_LEGACY_MIGRATE_H
#define PATTERN_LEGACY_MIGRATE_H
#include "project_store.h"
#include "pattern_flash_store.h"
#define PLM_LEGACY_BYTES 3484u
#define PLM_PREFIX 24u
#define PLM_SIZE (PLM_PREFIX + PLM_LEGACY_BYTES)
/* work and scratch each hold one 4096-byte sector. Disjoint
 * buffers, serialized calls. The legacy validity callback enforces v16/v12–15
 * validation. No source sector is erased; only all-FF PFS sectors are used.
 * Existing target must be byte-identical. An error never authorizes reclaim. */
int plm_migrate(const ps_store *, const pfs_store *, uint32_t slot, void *work, void *scratch);
/* A successful load returns the exact legacy payload and its 13-byte name. */
int plm_load(const ps_store *, const pfs_store *, uint32_t slot, char name[PS_NAME_BYTES], void *legacy, void *work, void *scratch);
#endif
