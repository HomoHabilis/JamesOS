/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded streaming SHA-256 for experimental project namespace metadata. */
#ifndef FM1_PATTERN_SHA256_H
#define FM1_PATTERN_SHA256_H
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t buffer[64];
    uint8_t used, finalized;
} fm1_pattern_sha256;

/* init, update and final return 0 on success, -1 for invalid arguments,
 * length overflow or use after final. update accepts NULL only for length 0.
 * Context and input/output must not overlap. No allocation is performed. */
int fm1_pattern_sha256_init(fm1_pattern_sha256 *ctx);
int fm1_pattern_sha256_update(fm1_pattern_sha256 *ctx, const void *data, uint32_t length);
int fm1_pattern_sha256_final(fm1_pattern_sha256 *ctx, uint8_t digest[32]);
#endif
