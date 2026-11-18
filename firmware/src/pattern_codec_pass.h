/* SPDX-License-Identifier: GPL-3.0-only */
/* Internal: the single PSC3 encoder pass (pattern_storage_codec.c), shared by
 * the in-memory encoders and the bounded-source encoder (pattern_codec_source.c).
 * Bytes come from `mem` when it is set, otherwise from `get`, which sets
 * `error` on a failed read. Returns the encoded length; output bytes outside
 * [start, start+cap) are counted but not written. */
#ifndef FM1_PATTERN_CODEC_PASS_H
#define FM1_PATTERN_CODEC_PASS_H
#include <stddef.h>
#include <stdint.h>
typedef struct fm1_codec_bytes fm1_codec_bytes;
struct fm1_codec_bytes {
    const uint8_t *mem;
    uint8_t (*get)(fm1_codec_bytes *, uint32_t);
    int error;
};
size_t fm1_codec_encode_pass(fm1_codec_bytes *in, size_t n, uint8_t *out, size_t start, size_t cap);
#endif
