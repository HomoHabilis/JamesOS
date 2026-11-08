/* Host-only seekable PSC3 reader. No device binding or timing qualification. */
#ifndef FM1_PATTERN_STREAM_PROJECT_H
#define FM1_PATTERN_STREAM_PROJECT_H
#include "pattern_project.h"
typedef int (*fm1_stream_read_fn)(void *,uint32_t,void *,uint32_t);
/* Source must remain immutable and exclusively fenced through both validation
 * and all replay. All output/work buffers are separate disposable staging;
 * discard them on error and adopt only after success. */
int fm1_stream_project_decode(fm1_stream_read_fn,void *,uint32_t,fm1_project_session_valid,void *,fm1_project_work *,uint8_t *,br_pattern_bank *,uint8_t *,size_t,br_pattern_snapshot *,fm1_pattern_song *);
#endif
