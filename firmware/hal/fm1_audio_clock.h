/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_AUDIO_CLOCK_H
#define FM1_AUDIO_CLOCK_H

#include <stdint.h>
#include "fm1_audio_rate.h"

/* Apply the AC79 SDK IIS sample-rate clock selection for ALNK0. Native 32 kHz
 * and 48 kHz select the audio PLL and IIS0 clock source; 44.1 kHz uses the
 * standard source. The physical output cadence still requires measurement. */
static inline void fm1_audio_select_clock(volatile uint32_t *clk_con2,
                                          volatile uint32_t *ana_pll_con1,
                                          volatile uint8_t *alnk_con3) {
  *clk_con2 &= 0xFFFFF0FFu;
  *alnk_con3 &= (uint8_t)~0x1Cu;
#if FM1_NATIVE_OUTPUT_32K
  /* SDK iis_set_sample_rate(32000, index 0), case label 158: select audio
   * PLL, CLK_CON2[8], and ALNK_CON3 clock field 0xA0. */
  *ana_pll_con1 |= 1u << 25;
  *clk_con2 |= 1u << 8;
  *alnk_con3 = (uint8_t)((*alnk_con3 & 0x1Fu) | 0xA0u);
#elif FM1_NATIVE_OUTPUT_48K
  /* SDK iis_set_sample_rate(48000, index 0): PLL source, IIS0 clock select,
   * and ALNK_CON3 divider field 0x80. */
  *ana_pll_con1 |= 1u << 25;
  *clk_con2 |= 1u << 8;
  *alnk_con3 = (uint8_t)((*alnk_con3 & 0x1Fu) | 0x80u);
#else
  /* Existing standard clock selection. */
  (void)ana_pll_con1;
  *alnk_con3 = (uint8_t)((*alnk_con3 & 0x1Fu) | 0x80u);
#endif
  /* SDK common path, index 0: clear CLK_CON2[9]. */
  *clk_con2 &= ~(1u << 9);
}

#endif
