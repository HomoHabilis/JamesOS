/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_BOOT_RECOVERY_H
#define FM1_PATTERN_BOOT_RECOVERY_H
#include <stdint.h>
#include "pattern_project_adapter.h"
/* Pure scheduling policy; no I/O. Only an unacquired transaction is
 * retryable. Empty is accepted only after independent policy certification. */
typedef struct { uint32_t last_attempt; unsigned attempted, done; } br_pattern_boot_recovery;
static inline int br_pattern_boot_should_try(const br_pattern_boot_recovery *s,int ready,uint32_t now)
{ return s && ready && !s->done && (!s->attempted || (uint32_t)(now-s->last_attempt)>=250u); }
/* 1 ready, 0 retry, -1 fatal, -2 permanent I/O/corruption/policy failure. */
static inline int br_pattern_boot_result(br_pattern_boot_recovery *s,uint32_t now,int result,int fatal)
{
    if(!s || s->done) return -2;
    s->attempted=1; s->last_attempt=now;
    if(fatal || result==FM1_PROJECT_ROLLBACK_FAILED) { s->done=1; return -1; }
    if(result==PFS_OK || result==FM1_PROJECT_SAFE_EMPTY) { s->done=1; return 1; }
    if(result==FM1_PROJECT_BUSY) return 0;
    s->done=1; return -2;
}
#endif
