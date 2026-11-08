/* Two-copy project records. Storage operations return zero on success. */
#ifndef FM1_PROJECT_STORE_H
#define FM1_PROJECT_STORE_H
#include <stdint.h>

#define PS_SLOTS 12u
/* One additional two-copy record holds the last selected working project. */
#define PS_TOTAL_SLOTS (PS_SLOTS + 1u)
#define PS_SECTOR 4096u
#define PS_PAYLOAD_OFFSET 256u
#define PS_NAME_BYTES 13u
/* The updater scans the last page of every data sector for its recovery
 * records. Leave that page erased to avoid a false record and sector erase. */
#define PS_MAX_PAYLOAD (PS_SECTOR - PS_PAYLOAD_OFFSET - 256u)

enum { PS_OK = 0, PS_EMPTY = 1, PS_CORRUPT = 2, PS_IO = -1, PS_INVALID = -2 };
typedef struct {
    void *context;
    int (*read)(void *, uint32_t, void *, uint32_t);
    int (*erase)(void *, uint32_t);
    int (*program)(void *, uint32_t, const void *, uint32_t);
    int (*valid)(const void *, uint32_t);
    uint32_t base, payload_size;
} ps_store;
typedef struct { uint32_t generation; char name[PS_NAME_BYTES]; int copy; } ps_info;

/* scratch must hold payload_size bytes. Inputs to ps_save must not overlap it. */
int ps_load(const ps_store *, uint32_t slot, void *scratch, ps_info *);
int ps_save(const ps_store *, uint32_t slot, const char *name,
            const void *payload, void *scratch, ps_info *);
/* Explicitly reclaim a slot with no valid project, including foreign data. */
int ps_save_reclaim(const ps_store *, uint32_t slot, const char *name,
                    const void *payload, void *scratch, ps_info *);
#endif
