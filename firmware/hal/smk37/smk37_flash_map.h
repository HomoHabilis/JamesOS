/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/* Physical flash offsets, with exclusive ends. The SMK-37 Pro's VM ends
 * immediately before the Bluetooth and factory calibration sectors. */
#define SMK37_FLASH_SIZE 0x100000u
#define SMK37_FLASH_SECTOR 0x1000u
#define SMK37_APP_LO 0x04000u
#define SMK37_APP_HI 0x9D000u
#define SMK37_PROJECT_LO SMK37_APP_HI
#define SMK37_PROJECT_HI 0xB7000u
#define SMK37_SETTINGS_LO 0xB9000u
#define SMK37_STORE_OWNER 0xBA000u
#define SMK37_STORE_HI 0xBB000u
#define SMK37_OTA_LO 0xBB000u
#define SMK37_OTA_HI 0xC0000u

_Static_assert(SMK37_PROJECT_HI <= SMK37_SETTINGS_LO &&
               SMK37_SETTINGS_LO + SMK37_FLASH_SECTOR == SMK37_STORE_OWNER &&
               SMK37_STORE_OWNER + SMK37_FLASH_SECTOR == SMK37_STORE_HI &&
               SMK37_STORE_HI <= SMK37_OTA_LO, "JamesOS-K store must fit in VM");
_Static_assert(SMK37_APP_LO == 0x4000u && SMK37_APP_HI <= SMK37_OTA_LO,
               "app writes must exclude the boot head and update staging");
_Static_assert(SMK37_OTA_HI <= 0xC0000u && SMK37_OTA_HI - SMK37_OTA_LO == 0x5000u,
               "update staging must fit in VM and exclude BTIF and USRTRIM");
_Static_assert(((SMK37_APP_LO | SMK37_APP_HI | SMK37_OTA_LO | SMK37_OTA_HI) &
                (SMK37_FLASH_SECTOR - 1u)) == 0u, "flash windows must be sector aligned");

/* A write must fit entirely within one window. Subtraction avoids unsigned
 * wraparound on malformed requests to the 24-bit NOR address register. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define SMK37_LOADER_WRITE_OK(off, n) ((n) != 0u && \
    (FL_IN(off, n, SMK37_APP_LO, SMK37_APP_HI) || FL_IN(off, n, SMK37_OTA_LO, SMK37_OTA_HI)))

/* Preserve the two occupied sectors on this unit. Project offsets form a
 * contiguous logical space; the flash driver receives translated addresses. */
static inline uint32_t smk37_project_physical(uint32_t off)
{
    if (off >= SMK37_PROJECT_LO && off < SMK37_PROJECT_HI) {
        if (off >= 0xAB000u) off += SMK37_FLASH_SECTOR;
        if (off >= 0xAF000u) off += SMK37_FLASH_SECTOR;
    }
    return off;
}
#define SMK37_APP_WRITE_OK(off, n) ((n) != 0u && \
    (FL_IN(off, n, 0x9D000u, 0xAB000u) || \
     FL_IN(off, n, 0xAC000u, 0xAF000u) || \
     FL_IN(off, n, 0xB0000u, SMK37_OTA_HI)))
