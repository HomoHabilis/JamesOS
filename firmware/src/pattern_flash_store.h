/* Experimental host-only append-only flash store. Not wired to the device. */
#ifndef PATTERN_FLASH_STORE_H
#define PATTERN_FLASH_STORE_H
#include <stdint.h>
#define PFS_SECTOR 4096u
#define PFS_COUNT 26u
#define PFS_CHUNK 3520u
#define PFS_MAX_CHUNKS 24u
#define PFS_MAX_DATA (PFS_CHUNK * PFS_MAX_CHUNKS)
enum { PFS_OK=0, PFS_EMPTY=1, PFS_FULL=2, PFS_IO=-1, PFS_INVALID=-2, PFS_SMALL=-3 };
typedef struct {
    void *ctx;
    int (*read)(void *, uint32_t, void *, uint32_t);
    int (*program)(void *, uint32_t, const void *, uint32_t);
    uint32_t base;
} pfs_store;
/* Caller supplies 4096 bytes of scratch, disjoint from input/output. Calls
 * must be serialized. The program callback must split writes at NOR page
 * boundaries. Never erases or reuses sectors. A manifest consumes one sector
 * per save, in addition to its chunk sectors: this is not a practical final
 * allocator. Exactly 13 logical IDs (12 projects and working) are supported.
 * On any load error, discard the output: it may contain partial data. */
int pfs_save(const pfs_store *, uint32_t id, const void *, uint32_t, void *scratch);
int pfs_load(const pfs_store *, uint32_t id, void *, uint32_t capacity, uint32_t *length, void *scratch);
/* Streaming requires one 4096-byte scratch buffer plus this bounded reader.
 * Callers serialize all access and keep save sources immutable for the entire
 * transaction. Read errors invalidate all output from that call. CRC does not
 * authenticate data. Descriptor is private state; do not edit it. */
typedef struct {
    const pfs_store *store;
    uint32_t id, generation, manifest_sector, length, count;
    uint32_t sectors[PFS_MAX_CHUNKS], chunk_crc[PFS_MAX_CHUNKS];
    uint8_t manifest[32u+8u+4u*PFS_MAX_CHUNKS];
} pfs_reader;
typedef int (*pfs_read_at_fn)(void *ctx, uint32_t offset, void *dst, uint32_t length);
typedef int (*pfs_eligible_fn)(void *,uint32_t);
int pfs_reader_open(const pfs_store *,uint32_t id,pfs_reader *,void *scratch);
/* Like open, but the selected manifest and every referenced chunk must each
 * be independently certified as owned (exactly 1). Verification callbacks
 * may use scratch but must preserve the reader and store. All writers and
 * ownership changes must stay fenced through the reader's lifetime. A
 * failure rejects the newest candidate; it does not fall back silently. */
int pfs_reader_open_owned(const pfs_store *,uint32_t id,pfs_reader *,void *scratch,pfs_eligible_fn,void *owner_ctx);
int pfs_reader_read_at(const pfs_reader *,uint32_t offset,void *dst,uint32_t length,void *scratch);
int pfs_save_stream(const pfs_store *,uint32_t id,uint32_t length,pfs_read_at_fn,void *ctx,void *scratch);
/* The caller must independently reserve each eligible sector exclusively for
 * the whole transaction. Only exactly 1 authorizes allocation; the predicate
 * receives an absolute sector address and must not modify flash or scratch.
 * This limits allocation but does not establish provenance or authorize GC. */
int pfs_save_stream_eligible(const pfs_store *,uint32_t id,uint32_t length,pfs_read_at_fn,void *ctx,void *scratch,pfs_eligible_fn,void *eligible_ctx);
/* GC is experimental and requires caller-provided provenance attesting that
 * each listed sector is exclusively owned by this store and has no external
 * references. It erases at most one obsolete generation. Interrupted GC may
 * strand sectors, but must preserve the newest two complete generations. */
/* Only exactly 1 certifies ownership; every other return denies it. */
typedef int (*pfs_owned_fn)(void *, uint32_t);
typedef int (*pfs_erase_fn)(void *, uint32_t);
int pfs_gc_one(const pfs_store *, uint32_t id, pfs_owned_fn, pfs_erase_fn, void *scratch);
#endif
