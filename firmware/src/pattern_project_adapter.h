/* Host-only project storage bridge. No device flash or runtime integration. */
#ifndef FM1_PATTERN_PROJECT_ADAPTER_H
#define FM1_PATTERN_PROJECT_ADAPTER_H
#include "pattern_project.h"
#include "pattern_flash_store.h"
#define FM1_PROJECT_NAME_BYTES 13u
#define FM1_PROJECT_WORKING_ID 12u
/* Restore and rollback both failed; state is indeterminate and callers must
 * stop further operations until the engine is explicitly reinitialized. */
#define FM1_PROJECT_ROLLBACK_FAILED (-10)
#define FM1_PROJECT_WORKING_FAILED (-11)
/* NEW working record committed, but live reset failed and was rolled back. */
#define FM1_PROJECT_NEW_WORKING_ONLY (-12)
/* No transaction was acquired; caller may retry after the engine is quiet. */
#define FM1_PROJECT_BUSY (-13)
/* The access policy positively certified an absent working record. */
#define FM1_PROJECT_SAFE_EMPTY 3
/* A fenced scan found an incompatible working record; no data was changed. */
#define FM1_PROJECT_FOREIGN_WORKING 4
/* All operations require exclusive audio/editor quiescence. The callbacks
 * must keep that condition and the exported bank immutable until completion.
 * Adoption transfers destination arena ownership; caller must keep it alive.
 * The adapter validates that simultaneously live buffers are disjoint:
 * source/rollback snapshots, record workspace, session images, sector scratch,
 * and staging bank all have overlapping lifetimes during load/save. Only the
 * decoded staging arena and adoption arena may be exactly identical. The
 * decoded-session scratch may also equal the incoming-session buffer, or
 * the prior-session buffer: decode copies it out before old-session capture.
 * During load only, the record snapshot may equal the destination live
 * snapshot; save requires them separate because it rereads the live source.
 * During save only, live and previous-live may be the same snapshot because
 * rollback state is unused. The
 * separately bound real-session scratch must also be disjoint from all these
 * and from in-flight engine/project buffers. A target may use other existing
 * storage only after independently proving its lifetime under quiescence. */
typedef struct {
 void *context;
 int (*quiescent)(void *);
 int (*capture)(uint8_t *,size_t);
 int (*validate)(const uint8_t *,size_t,void *);
 int (*restore)(const uint8_t *,size_t);
 int (*export_bank)(void *,br_pattern_bank *,br_pattern_snapshot *,fm1_pattern_song *);
 int (*can_adopt)(void *,const br_pattern_bank *,const br_pattern_snapshot *,const fm1_pattern_song *);
 int (*adopt)(void *,const br_pattern_bank *,const br_pattern_snapshot *,const fm1_pattern_song *);
 int (*restore_pattern)(void *,const br_pattern_snapshot *);
} fm1_project_adapter_ops;
typedef struct {
 fm1_project_work *work;
 int current_slot; /* -1 means no linked named project. */
 uint8_t *sector,*arena; size_t arena_capacity;
 uint8_t *session, *previous;
 br_pattern_snapshot *live, *previous_live;
 br_pattern_bank bank;
 fm1_pattern_song song;
 fm1_project_source source;
 pfs_reader reader;
} fm1_project_adapter;
/* IDs 0..11 are named projects, 12 is the working record. A valid name is
 * 1..12 printable bytes. FULL is reported by the underlying PFS store. */
int fm1_project_adapter_save(const pfs_store *,uint32_t,const char *,const fm1_project_adapter_ops *,fm1_project_adapter *);
/* Write a fully staged single-pattern image without changing current owner.
 * Used by guarded NEW after the caller validates and captures the factory
 * snapshot. Session, bank, live and workspace remain immutable to callers. */
int fm1_project_adapter_save_initial(const pfs_store *,const fm1_project_adapter_ops *,fm1_project_adapter *);
int fm1_project_adapter_load(const pfs_store *,uint32_t,char name[FM1_PROJECT_NAME_BYTES],const fm1_project_adapter_ops *,fm1_project_adapter *);
/* Scratch must be aligned for the v16 session, exclusive and at least 3484
 * bytes; binding refuses overlap with firmware save/undo/load buffers. */
int br_pattern_session_bind(void *,size_t);
int br_pattern_session_snapshot(const uint8_t *,size_t,br_pattern_snapshot *);
int br_pattern_session_capture(uint8_t *,size_t);
int br_pattern_session_validate(const uint8_t *,size_t,void *);
int br_pattern_session_restore(const uint8_t *,size_t);
#endif
