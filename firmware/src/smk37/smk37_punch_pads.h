/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Four effects repeat across each row. Only pads struck in this mode count;
 * holding a pad across a screen change cannot start an unintended effect. */
typedef struct { uint16_t held; uint8_t active; } smk_punch_pads_t;
static inline uint8_t smk_punch_pads_update(smk_punch_pads_t *s, uint16_t pressed,
                                             uint16_t down, int enabled)
{
    if (!enabled) { s->held = 0; s->active = 0; return 0; }
    s->held &= down;
    if (s->active && !(s->held & (0x1111u << (s->active - 1u)))) s->active = 0;
    for (unsigned pad = 0; pad < 16; ++pad) {
        uint16_t bit = (uint16_t)(1u << pad);
        unsigned effect = pad % 4u + 1u;
        if (!(pressed & bit)) continue;
        if (!s->active) s->active = (uint8_t)effect;
        if (s->active == effect) s->held |= bit & down;
    }
    if (!s->held) s->active = 0;
    return s->active;
}
