/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_POLY_STORAGE_H
#define BR_INSTRUMENT_POLY_STORAGE_H
#include <stdint.h>
#include <stddef.h>
#include "instrument_poly_allocator.h"

/* Plan six equally-sized, aligned voice slots in up to two owned buffers.
 * No buffer is touched, and no partial plan is published, on failure. */
typedef struct { void *voice[BR_POLY_VOICES]; } br_poly_storage_t;
BR_COLD_SIZE static inline int br_poly_storage_plan_count(br_poly_storage_t *out, void *primary,
        size_t primary_bytes, void *extra, size_t extra_bytes, size_t voice_bytes, unsigned count)
{
    br_poly_storage_t next = {{0}};
    uintptr_t a = (uintptr_t)primary, b = (uintptr_t)extra;
    size_t a_used = 0, b_used = 0;
    if (!out || !primary || !count || count > BR_POLY_VOICES || !voice_bytes || (voice_bytes & 7u) || (a & 7u)) return 0;
    if (primary_bytes > UINTPTR_MAX - a) return 0;
    if (extra_bytes) {
        if (!extra || (b & 7u) || extra_bytes > UINTPTR_MAX - b) return 0;
        if (a < b + extra_bytes && b < a + primary_bytes) return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (voice_bytes <= primary_bytes - a_used) {
            next.voice[i] = (void *)(a + a_used); a_used += voice_bytes;
        } else if (extra_bytes && voice_bytes <= extra_bytes - b_used) {
            next.voice[i] = (void *)(b + b_used); b_used += voice_bytes;
        } else return 0;
    }
    *out = next;
    return 1;
}
BR_COLD_SIZE static inline int br_poly_storage_plan(br_poly_storage_t *out, void *primary,
        size_t primary_bytes, void *extra, size_t extra_bytes, size_t voice_bytes)
{
    return br_poly_storage_plan_count(out, primary, primary_bytes, extra, extra_bytes,
                                      voice_bytes, BR_POLY_VOICES);
}
#endif
