/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BRAIDS_INSTRUMENT_WORKER_DIAG87_H
#define BRAIDS_INSTRUMENT_WORKER_DIAG87_H
#include <stdint.h>

#define BR_WORKER_DIAG87_VERSION 1u
#define BR_WORKER_DIAG87_WORDS 18u
#define BR_WORKER_DIAG87_BYTES (BR_WORKER_DIAG87_WORDS * 4u)

typedef struct {
    uint32_t words[BR_WORKER_DIAG87_WORDS];
} br_worker_diag87_t;

static inline uint32_t br_worker_diag87_request_valid(const uint8_t *body,
                                                       uint32_t body_size)
{
    (void)body;
    return body_size == 0u;
}

static inline uint32_t br_worker_diag87_decode(const uint8_t *bytes,
                                                uint32_t size,
                                                br_worker_diag87_t *out)
{
    uint32_t i;
    if (!bytes || !out || size != BR_WORKER_DIAG87_BYTES) return 0u;
    for (i = 0u; i < BR_WORKER_DIAG87_WORDS; ++i) {
        uint32_t at = i * 4u;
        out->words[i] = (uint32_t)bytes[at] |
                        ((uint32_t)bytes[at + 1u] << 8) |
                        ((uint32_t)bytes[at + 2u] << 16) |
                        ((uint32_t)bytes[at + 3u] << 24);
    }
    return out->words[0] == BR_WORKER_DIAG87_VERSION;
}
#endif
