/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: SLOOP (isod89), built on Felucca (Leo Kuroshita, Hügelton Instruments), by way of Dinghy (zvenson);
 * modified for JamesOS. GPL-3.0-only. See docs/legal/THIRD-PARTY-NOTICES.md. */
/* Warm-reset guard, retained in RAM only. Never writes the bootloader/flash.
 * This cannot rescue an image that fails before reaching the application. */
#ifndef FELUCCA_BOOTGUARD_H
#define FELUCCA_BOOTGUARD_H
#include <stdint.h>
#define BOOTGUARD_MAGIC 0x42475232u
typedef struct { uint32_t magic, failed, pending; } bootguard_t;
enum { BOOT_NORMAL, BOOT_RECOVERY, BOOT_ROM };
static void bootguard_clear(bootguard_t *b)
{
    b->magic = BOOTGUARD_MAGIC;
    b->failed = b->pending = 0;
}
static uint32_t bootguard_begin(bootguard_t *b)
{
    if (b->magic != BOOTGUARD_MAGIC || b->failed > 2u || b->pending > 2u)
        bootguard_clear(b);
    if (b->pending == 2u) {           /* recovery itself reset: keep ROM fallback */
        bootguard_clear(b);
        return BOOT_ROM;
    }
    if (b->pending && b->failed < 2u) b->failed++;
    b->pending = b->failed >= 2u ? 2u : 1u;
    return b->pending == 2u ? BOOT_RECOVERY : BOOT_NORMAL;
}
static int bootguard_manual(uint32_t buttons)
{
    return (buttons & 3u) == 1u;     /* physical OCT- alone; both keep calibration */
}
#endif
