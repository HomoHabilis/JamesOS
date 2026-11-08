/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BRAIDS_INSTRUMENT_TAIL_DIAG88_H
#define BRAIDS_INSTRUMENT_TAIL_DIAG88_H
#include <stdint.h>

#define BR_AUDIO_TAIL_DIAG88_VERSION 1u
#define BR_AUDIO_TAIL_DIAG88_WORDS 8u
#define BR_AUDIO_TAIL_DIAG88_BYTES (BR_AUDIO_TAIL_DIAG88_WORDS * 4u)

/* Empty request; response words are version, last-half validity, final
 * successful CPU1 wait return tick, last/max wait-to-ack tail ticks,
 * last/max complete half duration, and valid full-half count. */
static inline uint32_t br_audio_tail_diag88_request_valid(const uint8_t *body,
                                                           uint32_t size)
{
    (void)body;
    return size == 0u;
}

static inline uint32_t br_audio_tail_diag88_decode(const uint8_t *bytes,
                                                    uint32_t size,
                                                    uint32_t out[BR_AUDIO_TAIL_DIAG88_WORDS])
{
    uint32_t i;
    if (!bytes || !out || size != BR_AUDIO_TAIL_DIAG88_BYTES) return 0u;
    for (i = 0u; i < BR_AUDIO_TAIL_DIAG88_WORDS; ++i) {
        uint32_t at = i * 4u;
        out[i] = (uint32_t)bytes[at] |
                 ((uint32_t)bytes[at + 1u] << 8) |
                 ((uint32_t)bytes[at + 2u] << 16) |
                 ((uint32_t)bytes[at + 3u] << 24);
    }
    return out[0] == BR_AUDIO_TAIL_DIAG88_VERSION;
}
#endif
