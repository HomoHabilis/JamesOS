/* SPDX-License-Identifier: GPL-3.0-only
 * Opt-in read-only startup check for the fixed RAM-code load images.
 */
#ifndef FM1_RAM_IMAGE_CRC_DIAG_H_
#define FM1_RAM_IMAGE_CRC_DIAG_H_

#include <stdint.h>
#include "fm1_hot_code_policy.h"

#ifndef FM1_RAM_IMAGE_CRC_DIAG
#if defined(FM1_DEVELOPMENT_DIAGNOSTICS) && !FM1_DEVELOPMENT_DIAGNOSTICS
#define FM1_RAM_IMAGE_CRC_DIAG 0
#else
#define FM1_RAM_IMAGE_CRC_DIAG 1
#endif
#endif
#if defined(FM1_DEVELOPMENT_DIAGNOSTICS) && !FM1_DEVELOPMENT_DIAGNOSTICS && FM1_RAM_IMAGE_CRC_DIAG
#error "Distribution firmware cannot enable the diagnostic RAM image CRC capture"
#endif

/* Address-only validation is shared with the host boundary tests so the
 * selected RAM-code policy can be checked without dereferencing addresses. */
static inline uint32_t fm1_ram_image_crc_range_valid(uintptr_t start,
                                                      uintptr_t end,
                                                      uintptr_t load,
                                                      uint32_t *length)
{
    uintptr_t bytes;
    if (!length || end <= start || (start & 3u) || (end & 3u) || (load & 3u) ||
        start < FM1_HOT_CODE_RAM_START || end > FM1_HOT_CODE_RAM_LIMIT ||
        load < 0x02000120u || load > 0x0208E0DCu)
        return 0u;
    bytes = end - start;
    if (bytes > FM1_HOT_CODE_RAM_BYTES || bytes > 0x0208E0DCu - load)
        return 0u;
    *length = (uint32_t)bytes;
    return 1u;
}

enum {
    FM1_RAM_IMAGE_CRC_DIAG_WORDS = 21u,
    FM1_RAM_IMAGE_CRC_DIAG_BYTES = FM1_RAM_IMAGE_CRC_DIAG_WORDS * 4u,
    FM1_RAM_IMAGE_CRC_STATUS_NOT_RUN = 0u,
    FM1_RAM_IMAGE_CRC_STATUS_MATCH = 1u,
    FM1_RAM_IMAGE_CRC_STATUS_MISMATCH = 2u,
    FM1_RAM_IMAGE_CRC_STATUS_SKIPPED_C1_LIVE = 3u,
    FM1_RAM_IMAGE_CRC_STATUS_BAD_RANGE = 4u,
    FM1_RAM_IMAGE_CRC_SCOPE_RAM_BYTES_ONLY = 1u << 0,
    FM1_RAM_IMAGE_CRC_BRANCH_EARLY_IN_PROGRESS = 1u << 0,
    FM1_RAM_IMAGE_CRC_BRANCH_RECOVERY_JOURNAL_VALID = 1u << 1,
    FM1_RAM_IMAGE_CRC_BRANCH_C1_RESET_HELD = 1u << 2
};

/* Reply words:
 * 0 schema, 1 status, 2 scope flags, 3 boot mode, 4 startup-branch flags,
 * 5 early-journal state, 6 recovery-journal validity, 7 sampled C1 control,
 * 8..13 .ram_text start/end/load/length/RAM CRC/load CRC,
 * 14..19 .ram_audio start/end/load/length/RAM CRC/load CRC,
 * 20 match mask (bit 0 .ram_text, bit 1 .ram_audio).
 *
 * A matching CRC proves only that RAM bytes match the linker load image. It
 * makes no claim about CPU1 instruction-cache contents or coherency.
 */
#if defined(FM1_DEVICE) && FM1_RAM_IMAGE_CRC_DIAG
void fm1_ram_image_crc_diag_snapshot(uint32_t out[FM1_RAM_IMAGE_CRC_DIAG_WORDS]);
#endif

#endif
