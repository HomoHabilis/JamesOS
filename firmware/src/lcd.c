/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ST7789-class 240x240 panel on SPI1: PC9 CLK, PC10 DO, PC7 CS, PC8 D/C,
 * backlight PA2 active low. Polled DMA transfers; every source buffer must be
 * in RAM. A pixel transfer is left running (lcd_busy): the next LCD access, or
 * a write to its source buffer (lcd_sync), waits for it, so the main loop works
 * while the last strip of a frame goes out. */
/* pins and SPI1: hal/fm1_lcd_hw.h (SMK-37 Pro: other pins, and the RGB pad LEDs below, BR_CAP_PADS) */
#include "../product.h"
#include "ui_timing_diag.h"            /* diagnostic builds only (BR_UI_TIMING_DIAG); else nothing */
/* The SPI write clock is lsb / (LCD_BAUD + 1), lsb = 60 MHz (4 = 12 MHz; FM1_PADLED_BAUD 8 = 6.67 MHz):
 *   LCD_BAUD 4: 12 MHz, 1.5 MB/s, a 240 x 240 screen in 77 ms  (the default)
 *   LCD_BAUD 3: 15 MHz, 1.9 MB/s, 61 ms   (the ST7789V's rated serial write cycle, 66 ns: about 15 MHz)
 *   LCD_BAUD 2: 20 MHz, 2.5 MB/s, 46 ms   (above the rating)
 *   LCD_BAUD 1: 30 MHz, 3.75 MB/s, 31 ms  (above the rating)
 * A build option only (tools/build.py --define LCD_BAUD=2) for trials on hardware. */
#ifndef LCD_BAUD
#define LCD_BAUD 4u
#endif

static uint8_t lcd_small[64];
static uint8_t lcd_busy;           /* a lcd_data DMA may still run; CS is low */

static void lcd_sync(void)          /* finish the running transfer (before touching its buffer) */
{
    if (!lcd_busy)
        return;
    BR_UIT_LCD_BEGIN();
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_busy = 0;
    BR_UIT_LCD_END();
}

static void lcd_cmd(uint8_t c)
{
    lcd_sync();
    BR_UIT_LCD_BEGIN();
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    BR_UIT_LCD_END();
}

static void lcd_data(const void *p, uint32_t n)
{
    if (!n)
        return;                    /* SPI_CNT = 0 never completes */
    lcd_sync();
    BR_UIT_LCD_BEGIN();
    fm1_lcd_send_data(p, n);
    lcd_busy = 1;                  /* completed by lcd_sync */
    BR_UIT_LCD_END();
}

static void lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    lcd_sync();
    lcd_small[0] = (uint8_t)(x0 >> 8);
    lcd_small[1] = (uint8_t)x0;
    lcd_small[2] = (uint8_t)(x1 >> 8);
    lcd_small[3] = (uint8_t)x1;
    lcd_cmd(0x2A);
    lcd_data(lcd_small, 4);
    lcd_small[4] = (uint8_t)(y0 >> 8);       /* own bytes: the x transfer may still read [0..3] */
    lcd_small[5] = (uint8_t)y0;
    lcd_small[6] = (uint8_t)(y1 >> 8);
    lcd_small[7] = (uint8_t)y1;
    lcd_cmd(0x2B);
    lcd_data(lcd_small + 4, 4);
    lcd_cmd(0x2C);
}

static uint16_t lcd_fillbuf[240];

static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n, k, sw = (uint16_t)((c >> 8) | (c << 8));
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    lcd_sync();
    BR_UIT_LCD_BYTES(w * h * 2u);
    k = 240u / w * w;                       /* whole rows per transfer (narrow fills: one DMA) */
    for (i = 0; i < k; i++)
        lcd_fillbuf[i] = (uint16_t)sw;
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    for (n = w * h; n; n -= k) {
        if (k > n)
            k = n;
        lcd_data(lcd_fillbuf, k * 2u);
    }
}

static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (!w || !h)
        return;
    BR_UIT_LCD_BYTES(w * h * 2u);
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    lcd_data(px, w * h * 2u);
}

/* ST7789V init: reset, then 16-bit colour, scan direction and IPS inversion;
 * voltage and gamma stay at the defaults. Format: cmd, n, n data bytes;
 * cmd 0x00 = wait (data byte: ms on the TIMER4 crystal timebase). */
static const uint8_t LCD_SEQ[] = {
    0x01, 0,                     /* SWRESET */
    0x00, 1, 150,
    0x11, 0,                     /* SLPOUT */
    0x00, 1, 120,
    0x3A, 1, 0x55,               /* COLMOD: RGB565 */
    0x36, 1, 0x00,               /* MADCTL: top-left origin, RGB order */
    0x21, 0,                     /* INVON: IPS panel */
    0x13, 0,                     /* NORON */
};

static void lcd_init(void)
{
    uint32_t r, x;
    fm1_lcd_hw_init();
    fm1_delay_ms(80u);
    for (r = 0; r < sizeof LCD_SEQ; r += 2u + LCD_SEQ[r + 1u]) {
        if (LCD_SEQ[r] == 0x00u) {                       /* pseudo command: wait */
            fm1_delay_ms(LCD_SEQ[r + 2u]);
            continue;
        }
        lcd_cmd(LCD_SEQ[r]);
        for (x = 0; x < LCD_SEQ[r + 1u]; x++)
            lcd_small[x] = LCD_SEQ[r + 2u + x];
        if (LCD_SEQ[r + 1u])
            lcd_data(lcd_small, LCD_SEQ[r + 1u]);
    }
    fm1_lcd_baud(LCD_BAUD);
    lcd_fill(0, 0, 240, 240, 0);
    lcd_cmd(0x29);
}

#if BR_CAP_PADS
/* Pad LED transport derived from M. Geisler’s Felucca SMK-37 port. */
/* The 16 RGB pads (SMK-37 Pro): WS2812-class LEDs on SPI1 group B (PC10), one SPI byte per
 * LED bit (0xF0 = 1, 0xC0 = 0, MSB first, GRB), 250 zero bytes of reset first (~300 us; the
 * stock firmware's 36 bytes = 43 us are too short for newer WS2812 parts: a glitch on PC10 at the
 * pin switch then counts as a first bit and shifts the chain by one, so a lit pad turns its chain
 * neighbour green). The frame goes out between LCD transfers (LCD CS high), blocking for ~0.8 ms;
 * padled_flush() only sends when a colour changed. */
#define PADLED_N 16u
#define PADLED_LATCH 250u
static uint8_t padled_buf[PADLED_LATCH + PADLED_N * 24u];
static uint32_t padled_rgb[PADLED_N];
static uint8_t padled_dirty = 1;
/* hardware pad (0..7 top row, 8..15 bottom row, left to right; smk_input.h SMK_PAD_OF_WHITE) -> position in
 * the chain (stock table) */
static const uint8_t PADLED_SLOT[PADLED_N] = {0, 15, 14, 13, 12, 11, 10, 9, 1, 2, 3, 4, 5, 6, 7, 8};

static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t c = (uint32_t)g << 16 | (uint32_t)r << 8 | b;
    if (pad < PADLED_N && padled_rgb[pad] != c) {
        padled_rgb[pad] = c;
        padled_dirty = 1;
    }
}

static void padled_flush(void)
{
    uint32_t p, i;
    if (!padled_dirty)
        return;
    padled_dirty = 0;
    for (p = 0; p < PADLED_N; p++) {
        uint8_t *o = padled_buf + PADLED_LATCH + PADLED_SLOT[p] * 24u;
        for (i = 0; i < 24u; i++)
            o[i] = (padled_rgb[p] >> (23u - i)) & 1u ? 0xF0u : 0xC0u;
    }
    lcd_sync();
    fm1_padled_send(padled_buf, sizeof padled_buf);
    fm1_lcd_wait();
    fm1_padled_done();
}
#endif
