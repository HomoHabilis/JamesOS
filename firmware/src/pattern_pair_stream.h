/* Host-only CRC-only page-streamed pair storage experiment. Its distinct
 * format intentionally rejects SHA-format records. CRC detects common random
 * corruption, but is not collision-resistant or an ownership proof. */
#ifndef FM1_PATTERN_PAIR_STREAM_H
#define FM1_PATTERN_PAIR_STREAM_H
#if (!defined(FM1_PAIR_STREAM_HOST_TEST) || !FM1_PAIR_STREAM_HOST_TEST || defined(FM1_DEVICE)) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
#error "Streaming pair prototype is host-only"
#endif
#include "project_store.h"
#define PPS_MAX_PAYLOAD 3584u
/* Every callback must be serialized under a fence across all writers including
 * updater and legacy paths. program takes exactly one aligned 256-byte page.
 * A validator returns exactly 1 only for a valid fully read payload. Sources
 * and validation inputs remain immutable for a transaction. The 16-byte role
 * is a collision discriminator, not authentication. A copied/forged marker
 * cannot prove historical reservation. Ordinary saves never erase unknown
 * data; an explicitly selected replacement uses a separate guarded path. */
typedef int (*pps_read_at)(void *,uint32_t,void *,uint32_t);
typedef int (*pps_validate)(void *,pps_read_at,void *,uint32_t);
typedef struct { void *ctx; int (*read)(void *,uint32_t,void *,uint32_t); int (*program)(void *,uint32_t,const void *,uint32_t); int (*erase)(void *,uint32_t); uint32_t base; uint8_t role[16]; pps_validate validate; void *validate_ctx; } pps_store;
/* scratch is one 256-byte buffer, disjoint from store, callback state, source,
 * descriptor, info, length and outputs. The validator supplies its own bounded
 * temporary storage. The internal stack and validator workspace are additional
 * and have not been qualified on the target. */
typedef struct { const pps_store *store;uint32_t slot,copy,length;uint8_t header[256]; } pps_reader;
int pps_reader_open(const pps_store *,uint32_t slot,pps_reader *,void *scratch);
/* Returns the canonical name, generation and copy cached by a successful
 * open; it does not reread flash. The descriptor and writer fence must remain
 * valid and immutable. info must not overlap the descriptor or store. */
int pps_reader_info(const pps_reader *,ps_info *);
/* Under the reader's exclusive fence, validate once and compare the record
 * against an immutable, disjoint buffer with bounded page reads. Returns
 * PS_INVALID for a length/content mismatch, PS_IO for an I/O failure, and
 * PS_CORRUPT for an invalid record. expected and all reader/store/callback
 * state must remain disjoint from the reusable 256-byte scratch. */
int pps_reader_equal(const pps_reader *,const uint8_t *expected,uint32_t length,void *scratch);
/* Concrete non-adversarial erase preflight: exactly 1 permits target only if
 * both sectors are wholly blank or a verified survivor is newer than the
 * target, and the target is wholly blank or a verified older record. Any
 * invalid nonblank sector, ambiguity, or I/O error denies. The caller must
 * fence all writers continuously until its subsequent erase completes. */
int pps_erase_allowed(const pps_store *,uint32_t slot,unsigned target,void *scratch);
/* Classify nonblank invalid copies under the same exclusive fence. Never
 * return an erase mask if any read fails. Valid copies are never selected. */
int pps_incompatible_mask(const pps_store *,uint32_t slot,void *scratch,unsigned *mask);
/* Revalidates the entire selected record; fencing guarantees it remains
 * immutable throughout the reader lifetime. Out must be disjoint from the
 * reader, store, scratch and callback state. Discard it on any error. */
int pps_reader_read_at(const pps_reader *,uint32_t offset,void *out,uint32_t length,void *scratch);
int pps_save(const pps_store *,uint32_t slot,const char *name,uint32_t length,pps_read_at,void *source_ctx,void *scratch,ps_info *);
int pps_load(const pps_store *,uint32_t slot,void *out,uint32_t capacity,uint32_t *length,void *scratch,ps_info *);
#endif
