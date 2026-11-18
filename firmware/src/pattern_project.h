/* Host-only experimental project interchange, not a device format. */
#ifndef FM1_PATTERN_PROJECT_H
#define FM1_PATTERN_PROJECT_H
#include "pattern_bank.h"
/* Envelope version 6: 24-byte header (v4 + session length u16 + 2 reserved),
 * then the session record (docs/SESSION-V18.md), then the 80-byte song
 * arrangement (pattern_song.h), then the patterns. */
#define FM1_PROJECT_SESSION 3584u
#define FM1_PROJECT_HEADER_BYTES 24u
#define FM1_PROJECT_ENVELOPE_VERSION 6u
#define FM1_PROJECT_ARRANGEMENT FM1_SONG_RECORD_BYTES
/* Version 5 (no arrangement) is read with an empty arrangement and saved back
 * as version 6. Set when a decoder met an older project envelope (FMP1 version
 * < 5, an older session format); callers clear it before a scan or load and
 * read it after a rejection to label it OLD FORMAT. */
extern volatile unsigned fm1_project_old_format_seen;
#define FM1_PROJECT_MAX 84480u
#define FM1_PROJECT_PATTERN_BYTES 4256u
/* Literal runs: 12-byte header plus one token per 64 bytes. */
#define FM1_PROJECT_RECORD_MAX (12u + FM1_PROJECT_PATTERN_BYTES + (FM1_PROJECT_PATTERN_BYTES+63u)/64u)
enum { FM1_PROJECT_OK=0, FM1_PROJECT_FULL=1, FM1_PROJECT_INVALID=-1 };
/* Validator must validate the complete canonical little-endian v17 session.
 * All buffers and structures must be disjoint and exclusively owned. Staging arena and scratch are disposable. Current project and store never
 * change on failure; output is disposable after any error.
 * This format is experimental, no hardware compatibility is promised. */
typedef int (*fm1_project_session_valid)(const uint8_t *,size_t,void *);
typedef struct {
 uint8_t *compressed; size_t compressed_capacity;
 uint8_t *session;
 /* Disposable, separate staging arena; caller chooses bounded capacity. */
 uint8_t *arena; size_t arena_capacity;
 br_pattern_snapshot *pattern;
 br_pattern_bank bank;
 fm1_pattern_song song;
} fm1_project_work;
int fm1_project_encode(const uint8_t session[FM1_PROJECT_SESSION], const br_pattern_bank *, const br_pattern_snapshot *, const fm1_pattern_song *, fm1_project_session_valid, void *, fm1_project_work *, uint8_t *,size_t,size_t *);
int fm1_project_decode(const uint8_t *,size_t,fm1_project_session_valid,void *,fm1_project_work *, uint8_t session[FM1_PROJECT_SESSION],br_pattern_bank *,uint8_t *arena,size_t arena_size,br_pattern_snapshot *,fm1_pattern_song *);
/* A seekable encoded source. Its input objects and workspace stay immutable
 * and exclusively owned until the store finishes consuming it. */
typedef int (*fm1_project_read_fn)(void *,uint32_t,void *,uint32_t);
typedef struct {
 const uint8_t *session; const br_pattern_bank *bank; const br_pattern_snapshot *live;
 fm1_project_work *work; uint8_t header[FM1_PROJECT_HEADER_BYTES], arrangement[FM1_PROJECT_ARRANGEMENT]; uint32_t offset[11], length[11], total;
} fm1_project_source;
int fm1_project_source_init(fm1_project_source *,const uint8_t *,const br_pattern_bank *,const br_pattern_snapshot *,const fm1_pattern_song *,fm1_project_session_valid,void *,fm1_project_work *);
int fm1_project_source_read(void *,uint32_t,void *,uint32_t);
int fm1_project_decode_stream(fm1_project_read_fn,void *,uint32_t,fm1_project_session_valid,void *,fm1_project_work *,uint8_t *,br_pattern_bank *,uint8_t *,size_t,br_pattern_snapshot *,fm1_pattern_song *);

#endif
