/* SPDX-License-Identifier: GPL-3.0-only */
/* Byte-range overlap test shared by the pattern store units: nonzero when the
 * nonempty ranges [a, a+an) and [b, b+bn) share a byte.
 * The device-candidate image links one copy (defined in pattern_storage_codec.c,
 * which every such build links); host builds and tests, which compile any subset
 * of these files, keep a private copy per file. Same expression either way. */
#ifndef FM1_PATTERN_OVERLAP_H
#define FM1_PATTERN_OVERLAP_H
#include <stddef.h>
#include <stdint.h>
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_DEVICE_CANDIDATE) && FM1_PATTERN_DEVICE_CANDIDATE
#define FM1_PATTERN_OVERLAP_SHARED 1
int fm1_mem_overlap(const void *a, size_t an, const void *b, size_t bn);
#else
#define FM1_PATTERN_OVERLAP_SHARED 0
static int fm1_mem_overlap(const void *a, size_t an, const void *b, size_t bn)
{ uintptr_t x=(uintptr_t)a, y=(uintptr_t)b; return an && bn && (x<=y ? y-x<an : x-y<bn); }
#endif
#endif
