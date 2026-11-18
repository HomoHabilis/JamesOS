/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "../product.h"

#if BR_PRODUCT_SMK37
/* SMK-37 Pro (from jamesos-k-pre-unify:firmware/hal/fm1_flash_ids.h) */
/* Identified 1 MiB parts on SMK-37 Pro hardware. */
static inline __attribute__((always_inline)) int fl_jedec_known(uint32_t id)
{
    return id == 0xEB6014u || id == 0x856014u;
}
#else
/* Only the identified 1 MiB flash parts are allowed to use the storage and
 * update paths. Keep this check shared by the app, recovery and loader. */
static inline __attribute__((always_inline)) int fl_jedec_known(uint32_t id)
{
    return id == 0x856014u || id == 0xB36014u;
}
#endif
