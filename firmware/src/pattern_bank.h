/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_BANK_H
#define FM1_PATTERN_BANK_H
/* Host-only experiment. This is neither device integration nor a disk format. */
#if defined(FM1_DEVICE) && (!defined(FM1_PATTERN_TARGET_EXPERIMENT) || !FM1_PATTERN_TARGET_EXPERIMENT)
#error "Experimental pattern bank cannot be used on device"
#endif
#include "pattern_snapshot.h"
#include "pattern_song.h"
#include <stddef.h>
#include <stdint.h>

enum { BR_BANK_OK=0, BR_BANK_FULL=1, BR_BANK_INVALID=-1, BR_BANK_CORRUPT=-2 };
typedef struct {
    uint8_t *arena;
    size_t capacity, size;
    size_t offset[FM1_PATTERN_COUNT], length[FM1_PATTERN_COUNT];
    size_t clip_offset, clip_length; /* ephemeral; not a song pattern */
    uint16_t used;
    uint8_t active, reserved, reserved_target;
} br_pattern_bank;
/* All functions are foreground-only and require exclusive access to bank,
 * live and work; there is no audio-boundary handoff or synchronization.
 * Caller owns all buffers. The active snapshot exists in live and can differ
 * from its last stored image. No flash, allocation or device I/O occurs. */
int br_pattern_bank_init(br_pattern_bank *, uint8_t *arena, size_t capacity);
/* Validate metadata and every stored record without changing any buffer. */
int br_pattern_bank_verify(const br_pattern_bank *);
/* Preflight for a future handoff: reserves no bytes itself, but confirms that
 * even the worst codec output for outgoing live can fit. It reserves capacity
 * and blocks other bank mutations until commit or cancel. The caller retains
 * the prepared work snapshot unchanged until commit; unused targets copy live. */
int br_pattern_bank_prepare(br_pattern_bank *, unsigned target,
    const br_pattern_snapshot *live, br_pattern_snapshot *work,
    size_t encoded_capacity);
/* Commits the reserved target with the latest outgoing live edits. work must
 * be the unchanged snapshot returned by prepare. These are foreground only;
 * caller is responsible for audio handoff and exclusive buffer ownership. */
int br_pattern_bank_commit(br_pattern_bank *, br_pattern_snapshot *live,
    const br_pattern_snapshot *work, uint8_t *encoded, size_t encoded_capacity);
int br_pattern_bank_cancel(br_pattern_bank *);
/* Encodes outgoing live before loading target. Unused target starts as a copy
 * of outgoing live. FULL/corrupt/invalid leaves live, bank and arena unchanged.
 * work, encoded, arena and live must be distinct, non-overlapping buffers. */
int br_pattern_bank_select(br_pattern_bank *, unsigned target,
    br_pattern_snapshot *live, br_pattern_snapshot *work,
    uint8_t *encoded, size_t encoded_capacity);
/* Copy reads active live or a previously stored pattern into clipboard.
 * Paste updates active live or a stored non-active pattern. Clipboard must not
 * overlap any other argument. Failed paste never modifies bank or live. */
int br_pattern_bank_copy(const br_pattern_bank *, unsigned source,
    const br_pattern_snapshot *live, br_pattern_snapshot *clipboard);
int br_pattern_bank_paste(br_pattern_bank *, unsigned destination,
    br_pattern_snapshot *live, const br_pattern_snapshot *clipboard,
    uint8_t *encoded, size_t encoded_capacity);
/* Optional compressed clipboard shares arena capacity. Failed copy/paste
 * leaves both patterns and previous clipboard unchanged. */
int br_pattern_bank_clip_copy(br_pattern_bank *, unsigned source,
    const br_pattern_snapshot *live, br_pattern_snapshot *work,
    uint8_t *encoded, size_t encoded_capacity);
int br_pattern_bank_clip_paste(br_pattern_bank *, unsigned destination,
    br_pattern_snapshot *live, br_pattern_snapshot *work,
    uint8_t *encoded, size_t encoded_capacity);
#endif
