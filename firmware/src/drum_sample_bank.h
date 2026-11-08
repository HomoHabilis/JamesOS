/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_DRUM_SAMPLE_BANK_H
#define BR_DRUM_SAMPLE_BANK_H

#include <stdint.h>
#include "instrument.h"

/* Resident sample-bank descriptors. `bytes` is the exact encoded size and
 * `frames` is the original mono frame count. */
typedef enum {
    BR_DRUM_SAMPLE_CODEC_IMA4 = 0,
    BR_DRUM_SAMPLE_CODEC_PCM8 = 1,
    BR_DRUM_SAMPLE_CODEC_PCM8_PACKED = 2,
} br_drum_sample_codec_t;

typedef struct {
    const uint8_t *data;
    uint32_t frames;
    uint32_t bytes;
    uint32_t rate;
    uint32_t codec;
} br_drum_sample_desc_t;

/* Bank 0 is 808, bank 1 is 909. Voice columns use BR_DRUM_* enum order:
 * KCK1, KCK2, SNAR, CLAP, OPEN, CLOS, CYMB, PERC, KCK3, RIM, PEDL, TOM. */
extern const br_drum_sample_desc_t br_drum_sample_bank[2][BR_DRUM_VOICE_COUNT];

#endif /* BR_DRUM_SAMPLE_BANK_H */
