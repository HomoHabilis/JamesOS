/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_POLY_LIMITS_H
#define BR_INSTRUMENT_POLY_LIMITS_H
#include <stdint.h>
/* Per-model admission limits; validate against the measured deadline profile. */
static inline uint32_t br_poly_model_limit(uint32_t model)
{
    static const uint8_t limits[24] = {
        6,4,8,8,8,4,5,8,5,8,4,5,5,7,5,4,5,4,4,6,3,4,6,6
    };
    return model < 24u ? limits[model] : 3u;
}
#endif
