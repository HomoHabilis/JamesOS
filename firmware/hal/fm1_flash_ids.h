/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Only the identified 1 MiB flash parts are allowed to use the storage and
 * update paths. Keep this check shared by the app, recovery and loader. */
static inline __attribute__((always_inline)) int fl_jedec_known(uint32_t id)
{
    return id == 0x856014u || id == 0xB36014u;
}
