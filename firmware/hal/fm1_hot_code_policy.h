/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* Experimental internal SRAM placement. The application does not run the
 * legacy RAM-debug payload. Keep its STATUS page (0x01c07f00..0x01c08000)
 * and the complete data/BSS region reserved even when using its lower space.
 * This selector changes neither processor clocks nor audio deadlines. */
#ifndef FM1_EXTENDED_RAM_AUDIO
#define FM1_EXTENDED_RAM_AUDIO 1
#endif

/* Keep diagnostics and the build's selected RAMTEXT layout on the same
 * policy. The last 0x100 bytes are the legacy boot STATUS page and are never
 * included in the RAM-code image, even when extended RAM audio is enabled. */
#define FM1_HOT_CODE_RAM_START 0x01C00000u
#if FM1_EXTENDED_RAM_AUDIO
#define FM1_HOT_CODE_RAM_LIMIT 0x01C07F00u
#else
#define FM1_HOT_CODE_RAM_LIMIT 0x01C06000u
#endif
#define FM1_HOT_CODE_RAM_BYTES (FM1_HOT_CODE_RAM_LIMIT - FM1_HOT_CODE_RAM_START)
