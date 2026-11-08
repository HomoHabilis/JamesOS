/* Host-side experimental bounded codec. No device storage operations. */
#ifndef FM1_PATTERN_STORAGE_CODEC_H
#define FM1_PATTERN_STORAGE_CODEC_H
#include <stddef.h>
#include <stdint.h>

enum { FM1_CODEC_OK = 0, FM1_CODEC_FULL = 1, FM1_CODEC_INVALID = -1 };
/* Both buffers must be disjoint. A sizing pass guarantees that FULL or an
 * invalid argument leaves output unchanged. The encoded length is returned
 * even on FULL. Input is an opaque byte image, not a defined project format. */
int fm1_pattern_codec_encode(const uint8_t *input, size_t length,
                             uint8_t *output, size_t capacity, size_t *encoded);
/* Reconstruct a bounded portion of the canonical V3 stream without a full
 * compressed buffer. Input and output must not overlap. */
int fm1_pattern_codec_encode_slice(const uint8_t *input,size_t length,
    size_t offset,uint8_t *output,size_t count,size_t *encoded);
/* V3 seekable decode validates the full record before touching output.
 * Callback returns zero only after supplying the entire requested span.
 * Output is disposable on every error, including a failed second read pass.
 * The source must remain immutable throughout the operation. */
typedef int (*fm1_codec_read_fn)(void *,uint32_t,void *,uint32_t);
int fm1_pattern_codec_decode_stream(fm1_codec_read_fn,void *,uint32_t,size_t,uint8_t *,size_t);
/* The caller supplies the exact expected decoded length. A validation pass
 * checks the entire stream and CRC before changing output. */
/* Validate a complete encoded snapshot without an output buffer. */
int fm1_pattern_codec_validate(const uint8_t *input, size_t length, size_t expected);
int fm1_pattern_codec_decode(const uint8_t *input, size_t length,
                             uint8_t *output, size_t expected);
#endif
