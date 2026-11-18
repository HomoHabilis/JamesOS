/* SPDX-License-Identifier: GPL-3.0-only */
/* UI timing diagnostic, early part: the counters and the hooks that lcd.c, tests/host.c, instrument_ui.c
 * and dinghy.c call. Diagnostic builds only (tools/build.py --define BR_UI_TIMING_DIAG=1); with the
 * default 0 every hook below expands to nothing and the image is unchanged. The counters, the commands
 * (0xB5..0xB8) and the remote panel input are in ui_timing_diag.c. */
#ifndef BR_UI_TIMING_DIAG_H
#define BR_UI_TIMING_DIAG_H
#ifndef BR_UI_TIMING_DIAG
#define BR_UI_TIMING_DIAG 0
#endif
#if BR_UI_TIMING_DIAG
#if BR_PRODUCT_SMK37 && !defined(DINGHY_HOST)
#error "BR_UI_TIMING_DIAG: FM-1 device and host builds only (the SMK-37 panel has its own input scan)"
#endif
/* Counter words (diagnostic 0xB5 reply words 1..BR_UIT_WORDS; all 32-bit, wrapping). Ticks: the 24 MHz timer. */
enum {
    BR_UIT_PASSES,          /* main-loop passes (br_app_tick calls; top-to-top) */
    BR_UIT_PASS_WORST,      /* longest pass, ticks */
    BR_UIT_PASS_OVER5,      /* passes over 5 ms */
    BR_UIT_PASS_OVER10,     /* passes over 10 ms */
    BR_UIT_DRAW_PASSES,     /* br_draw_pass_with calls */
    BR_UIT_DRAW_COMPUTE,    /* ticks inside br_draw_pass_with less the LCD ticks inside it */
    BR_UIT_LCD_TICKS,       /* ticks waiting on / starting LCD transfers (lcd.c), everywhere */
    BR_UIT_BYTES,           /* pixel bytes sent (lcd_blit, lcd_fill) */
    BR_UIT_FRAMES,          /* completed frames */
    BR_UIT_FRAME_WORST,     /* longest frame span, ticks */
    BR_UIT_FRAME_TOTAL,     /* sum of frame spans, ticks */
    BR_UIT_NOTES,           /* played notes seen by br_note_played (stamp 0..200 ms old) */
    BR_UIT_NOTE_TOTAL,      /* sum of their lags (now - arrival), ticks */
    BR_UIT_NOTE_WORST,      /* longest lag, ticks */
    BR_UIT_NOTE_OVER6,      /* lags over 6 ms */
    BR_UIT_INPUTS,          /* input-to-screen samples */
    BR_UIT_INPUT_TOTAL,     /* sum of input-to-screen times, ticks */
    BR_UIT_INPUT_WORST,     /* longest input-to-screen time, ticks */
    BR_UIT_ELAPSED_MS,      /* ms since the reset (filled in by the read) */
    BR_UIT_DRAW_TICKS,      /* ticks inside br_draw_pass_with, LCD included */
    BR_UIT_LCD_IN_DRAW,     /* the LCD ticks that fell inside br_draw_pass_with */
    BR_UIT_DRAW_SENDING,    /* draw passes that sent pixels */
    BR_UIT_FRAMES_SENDING,  /* completed frames that sent pixels */
    BR_UIT_NOTE_STALE,      /* played notes with a stamp older than 200 ms or in the future (not timed) */
    BR_UIT_INPUT_PASSES,    /* passes that took a button edge or found an encoder detent */
    BR_UIT_REGION_WORST,    /* longest single drawing region (wall ticks, from its go-ahead to the next region's
                             * question or the pass's end; LCD waits and interrupts included) */
    BR_UIT_REGION_WHERE,    /* that region: page << 24 | screen << 16 | SAVE mode << 12 | its place in the
                             * drawing order (the region questions asked in that pass before it, 0..4095) */
    BR_UIT_PASS_TIMED,      /* draw passes ended by the time bound (BR_PASS_TICKS) with bytes to spare */
    BR_UIT_DRAW_WORST,      /* longest single br_draw_pass_with call, ticks */
    BR_UIT_WORDS
};
static struct {
    uint32_t c[BR_UIT_WORDS];
    uint32_t reset_ms, last_pass, frame_start, input_pending, input_armed;
    uint32_t draw_t0, draw_lcd0, draw_bytes0, frame_bytes0, inj_t[2], guard_until;
    uint32_t region_t0;
    uint16_t inj_hold[2], region_ord, region_open_ord;
    uint8_t region_open, pass_timed, inj_state[2], inj_btn[2], inj_note[2];
    uint8_t have_last, in_frame, frame_valid, has_pending, has_armed, last_status;
} br_uit;
static void br_uit_loop_pass(void);
static void br_uit_draw_begin(void);
static void br_uit_draw_end(uint32_t more);
static void br_uit_note(uint32_t arrival, uint32_t now);
static void br_uit_input(uint32_t *buttons, uint32_t *released);
#define BR_UIT_LCD_BEGIN() uint32_t br_uit_lcd_t0 = fm1_ticks()
#define BR_UIT_LCD_END() (br_uit.c[BR_UIT_LCD_TICKS] += fm1_ticks() - br_uit_lcd_t0)
#define BR_UIT_LCD_BYTES(n) (br_uit.c[BR_UIT_BYTES] += (uint32_t)(n))
#define BR_UIT_LOOP_PASS() br_uit_loop_pass()
#define BR_UIT_DRAW_BEGIN() br_uit_draw_begin()
#define BR_UIT_DRAW_END(more) br_uit_draw_end(more)
#define BR_UIT_NOTE(arrival, now) br_uit_note((arrival), (now))
#define BR_UIT_INPUT(buttons, released) br_uit_input((buttons), (released))
static void br_uit_region_open(void);
static void br_uit_region_close(void);
#define BR_UIT_REGION_ASK() (br_uit_region_close(), ++br_uit.region_ord)
#define BR_UIT_REGION_OPEN() br_uit_region_open()
#define BR_UIT_REGION_CLOSE() br_uit_region_close()
#define BR_UIT_PASS_TIMED() (br_uit.pass_timed = 1u)
#else
#define BR_UIT_REGION_ASK() do { } while (0)
#define BR_UIT_REGION_OPEN() do { } while (0)
#define BR_UIT_REGION_CLOSE() do { } while (0)
#define BR_UIT_PASS_TIMED() do { } while (0)
#define BR_UIT_LCD_BEGIN() do { } while (0)
#define BR_UIT_LCD_END() do { } while (0)
#define BR_UIT_LCD_BYTES(n) do { } while (0)
#define BR_UIT_LOOP_PASS() do { } while (0)
#define BR_UIT_DRAW_BEGIN() do { } while (0)
#define BR_UIT_DRAW_END(more) do { } while (0)
#define BR_UIT_NOTE(arrival, now) do { } while (0)
#define BR_UIT_INPUT(buttons, released) do { } while (0)
#endif
#endif /* BR_UI_TIMING_DIAG_H */
