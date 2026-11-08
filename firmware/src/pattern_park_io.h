/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_PARK_IO_H
#define FM1_PATTERN_PARK_IO_H
#include <stdint.h>
/* Experimental portable transaction model: callbacks are supplied by an
 * exclusive owner. Raw operations must never park/resume the worker. No
 * device backend or flash range is granted by this module. */
typedef struct {
    void *ctx;
    /* Block new submissions and prove all previously submitted jobs retired.
     * Return 0 only when fenced+drained, positive for busy without changing
     * worker ownership, negative for an unsafe fault. On success the fence
     * remains held until release_fence or fault recovery. */
    int (*fence_and_drain)(void *);
    int (*release_fence)(void *); /* zero only after verified release */
    int (*park)(void *);
    int (*is_parked)(void *); /* exactly 1 means verified parked */
    int (*read_raw)(void *,uint32_t,void *,uint32_t);
    int (*program_raw)(void *,uint32_t,const void *,uint32_t);
    int (*erase_raw)(void *,uint32_t);
    int (*may_erase)(void *,uint32_t); /* exactly 1 certifies ownership */
    int (*revoke_loans)(void *); /* zero only once every borrower is done */
    int (*resume)(void *);
    void (*hold_reset)(void *);
    int (*is_contained)(void *); /* exactly 1 only for verified held reset */
} br_pattern_park_ops;
/* owner_spent and owner_claim form a serialized, one-owner claim for this
 * parked transaction. Only successful park end resets the transaction. */
typedef struct { br_pattern_park_ops ops; uint32_t base,end; unsigned state, fenced,owner_spent; void *owner_claim; } br_pattern_park_io;
enum { BR_PARK_OK=0, BR_PARK_INVALID=-1, BR_PARK_IO=-2, BR_PARK_BUSY=-3 };
/* The object must be zero-initialized. base/end are a caller-verified
 * partition, 4 KiB aligned. The submission fence remains held after fault. */
int br_pattern_park_begin(br_pattern_park_io *,const br_pattern_park_ops *,uint32_t base,uint32_t end);
int br_pattern_park_read(void *,uint32_t,void *,uint32_t);
int br_pattern_park_program(void *,uint32_t,const void *,uint32_t);
int br_pattern_park_erase(void *,uint32_t);
/* End refuses an active owner claim, then revokes loans before normal resume.
 * On failure the reset callback is invoked; it must independently verify the
 * held-reset state. An owner must explicitly retire before end is retried. */
int br_pattern_park_end(br_pattern_park_io *);
int br_pattern_park_faulted(const br_pattern_park_io *);
/* Fail closed after an owner-authorized raw I/O error. */
int br_pattern_park_abort(br_pattern_park_io *);
#endif
