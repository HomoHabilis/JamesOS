/* Host experiment: bounded-input encoder for the existing PSC3 codec. */
#ifndef FM1_PATTERN_CODEC_SOURCE_H
#define FM1_PATTERN_CODEC_SOURCE_H
#include <stddef.h>
#include <stdint.h>
typedef int (*fm1_codec_source_read)(void *,uint32_t,void *,uint32_t);
/* Reads an immutable source twice, with bounded scratch. Output is disposable
 * on every error; FULL does not write it. Caller serializes source access.
 * This is not yet a target-qualified real-time or flash operation. */
int fm1_codec_source_encode(fm1_codec_source_read,void *,uint32_t,uint8_t *,size_t,size_t *);
#endif
