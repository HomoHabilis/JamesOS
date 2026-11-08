/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_BRAIDS_WT_WAVES_SYMBOL_H_
#define FM1_BRAIDS_WT_WAVES_SYMBOL_H_

/* Keep the compiler-specific linker-label syntax inside the HAL boundary. */
#if defined(__APPLE__)
#define FM1_BRAIDS_WT_WAVES_LINKER_SYMBOL "__ZN6braids8wt_wavesE"
#else
#define FM1_BRAIDS_WT_WAVES_LINKER_SYMBOL "_ZN6braids8wt_wavesE"
#endif
#define FM1_BRAIDS_WT_WAVES_LINKER_LABEL(symbol) __asm__(symbol)

#endif  // FM1_BRAIDS_WT_WAVES_SYMBOL_H_
