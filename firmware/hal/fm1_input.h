/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 input HAL: key/button/encoder matrix and LEDs.
 *
 * One 11-column x 6-row diode matrix behind a 2x74HC595 chain (PA4 SER, PA3
 * SRCLK, PA1 RCLK), bit-banged and polled; rows PA0, PA5..PA8, PB7 with
 * pull-ups (low = closed). LED lines PH6/PH9/PA9/PA10 light the LED on the
 * same column, row PA7/PA8/PA5/PA6 respectively.
 *
 *   fm1_input_init();
 *   polled:  for (;;) { fm1_input_scan(); ... }
 *   IRQ:     call fm1_input_tick() from a ~10 kHz timer ISR; it advances one
 *            column per call (rows are sampled one tick after the column was
 *            latched, LEDs stay lit in between) and processes a frame every
 *            FM1_NCOL ticks. The main loop reads fm1_in.notes / buttons and
 *            takes edges/steps with fm1_input_edges() / fm1_enc_take().
 *
 * fm1_input_scan() runs one full frame (11 columns, ~0.6 ms) and calls
 * FM1_INPUT_IDLE() while it waits.
 * Keys/buttons (as Felucca 1.0): debounced as soon as their column is read. A press counts after
 * FM1_DEB_PRESS frames closed in a row (1.1-2.2 ms: the matrix has diodes and no ghosting, so a closed
 * sample is a closed key; two in a row keep one stray sample from playing a note), a release after
 * FM1_DEB_RELEASE frames open in a row (~9 ms): a contact bouncing open on the way down, or chattering
 * on the way up, never ends a note early or plays it twice.
 * Encoders: quadrature decoder (2-sample filter, + = clockwise) with detent counting, as
 * Felucca 1.0 reworked it (its #23, "knobs skipping or jumping"): an FM-1 detent is one full
 * quadrature cycle (4 transitions) and the knob rests in one state, the one seen at power-on
 * (relearned only after FM1_REST_FRAMES, ~1 s, parked elsewhere). Steps are emitted on arriving
 * back at it, the net transitions rounded to whole cycles (>= 2 counts one: a lost transition or
 * two is forgiven; a two-state jump counts on in the direction of travel). One click = one step
 * at any speed; bounce and back-and-forth cancel out. Never a second rest state: a knob held
 * mid-click taught the complement as a rest and every click counted twice, and short mid-click
 * pauses of a slow turn taught the mid states and the knob went dead (the 0.9 learner).
 * fm1_enc_take() returns the steps.
 * LEDs: set fm1_led[col] (packed row bits, bit1 PA5..bit4 PA8); they are lit while that column is
 * selected (one tick, ~95 us a frame). Three dim layers (fm1_input_tick only), after Felucca 1.0.1 (its
 * #35: the eye is logarithmic, an LED lit 1/4 or 1/6 of the time reads as nearly lit): a short pulse on
 * every frame (~910 Hz, no flicker) at the start of the column's next tick, riding on the 595 shift of
 * the next column (its outputs change only at the latch, so the pulse stays on column p and the key
 * read before it is unchanged). fm1_led_mid[col]: mid brightness, FM1_MID_NS per column (~15% duty);
 * fm1_led_dim[col]: the glow (landmarks, notes under tiles), FM1_GLOW_NS (~1/24 of a lit LED);
 * fm1_led_bg[col]: the backlight (menu LIGHTS), fm1_led_bg_ns, shorter. TIMER4
 * times the pulses bit by bit; only a pulse longer than the whole shift waits for the rest. An LED in
 * several layers takes the brightest. fm1_led_key/btn helpers address them by id.
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "fm1_cc.h"
#include "fm1_led_levels.h"

#if defined(FM1_DEVICE) && defined(FM1_INPUT_SCANNER_RAM) && FM1_INPUT_SCANNER_RAM
#if defined(__APPLE__)
#define FM1_INPUT_SCANNER_RAM_ATTR __attribute__((section("__TEXT,__ram_audio")))
#else
#define FM1_INPUT_SCANNER_RAM_ATTR __attribute__((section(".ram_audio.scanner")))
#endif
#else
#define FM1_INPUT_SCANNER_RAM_ATTR
#endif

#if defined(FM1_DEVICE) && defined(FM1_INPUT_SCANNER_RAM) && FM1_INPUT_SCANNER_RAM
#define FM1_INPUT_SCANNER_INLINE static inline __attribute__((always_inline))
#else
#define FM1_INPUT_SCANNER_INLINE static inline
#endif

#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_LED_US
#define FM1_LED_US 40u           /* LED on-time per column (brightness vs scan rate) */
#endif
#ifndef FM1_MID_NS
#define FM1_MID_NS 15000u        /* 15 us in each 100 us scan slot: ~15% duty */
#endif
#ifndef FM1_INPUT_FAST_SHIFT
#define FM1_INPUT_FAST_SHIFT 0   /* enable the output-shadow fast path with =1 */
#endif
#ifndef FM1_INPUT_FAST_PWM
#define FM1_INPUT_FAST_PWM 1     /* use the pre-masked PWM resolver with =1 */
#endif
#ifndef FM1_INPUT_COMPACT_SHIFT
#define FM1_INPUT_COMPACT_SHIFT 0 /* merge clock-low with the next SER write */
#endif
#ifndef FM1_INPUT_FAST_SR_SHADOW
#define FM1_INPUT_FAST_SR_SHADOW 0 /* keep serial shadow in a local across pin edges */
#endif
#if FM1_INPUT_COMPACT_SHIFT && !(FM1_INPUT_FAST_SHIFT && FM1_INPUT_FAST_SR_SHADOW)
#error "FM1_INPUT_COMPACT_SHIFT requires FM1_INPUT_FAST_SHIFT and FM1_INPUT_FAST_SR_SHADOW"
#endif
#ifndef FM1_INPUT_TICKS
#define FM1_INPUT_TICKS() fm1_ticks()
#endif
#define FM1_DEB_PRESS 2u          /* frames closed in a row: a press (a frame = 11 ticks, ~1.1 ms) */
#define FM1_DEB_RELEASE 8u        /* frames open in a row: a release (~9 ms) */
#define FM1_SETTLE_US 10u
#define FM1_REST_FRAMES 900u      /* ~1 s still off the detent state: that is the detent (power-on) */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u


#define FM1_KEYMAP FM1_LED_KEYMAP
/* encoder i: A at (col, row bit), B at (col, row bit) */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };

static const int8_t FM1_LED_PORT[6] = {-1, FM1_PA, FM1_PA, FM1_PH, FM1_PH, -1};
static const uint8_t FM1_LED_BIT[6] = {0, 9, 10, 6, 9, 0};

static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last note snapshot/edge read */
    uint8_t raw[FM1_NCOL];       /* last frame, packed rows, 1 = closed */
    uint8_t cnt[FM1_NKEY];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];  /* the detent state (0..3) */
    uint16_t enc_still[FM1_NENC]; /* frames since the last state change */
    int8_t enc_sub[FM1_NENC];    /* net transitions since the last rest state */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    uint32_t frames;
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    /* Saturation fails closed; equality cannot silently wrap during a watch. */
    uint32_t pattern_activity;
#endif
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_mid[FM1_NCOL];
#ifndef FM1_GLOW_NS
#define FM1_GLOW_NS 4000u        /* the glow pulse a frame (ns); a lit LED ~95 us: ~1/24 the brightness */
#endif
#define FM1__NS_T(ns) (((uint32_t)(ns) * FM1_TICKS_PER_US + 500u) / 1000u)   /* ns -> TIMER4 ticks */
static uint8_t fm1_led_dim[FM1_NCOL];   /* same layout as fm1_led: the glow */
static uint8_t fm1_led_bg[FM1_NCOL];    /* same layout: the backlight (labels readable in the dark) */
static volatile uint16_t fm1_led_bg_ns; /* the backlight pulse a frame (ns), 0 = off (menu LIGHTS) */

/* The scanner caller passes mutually exclusive brightness bands: mid already
 * excludes bright, dim excludes bright/mid, and background excludes all three.
 * Keep fm1_led_pwm_mask_at() as the general overlap-safe API; this narrower
 * resolver avoids repeating those precedence masks in the per-shift loop. */
FM1_INPUT_SCANNER_INLINE uint8_t fm1__led_pwm_priority_mask_at(uint8_t bright,
                                                              uint8_t mid,
                                                              uint8_t dim,
                                                              uint8_t bg,
                                                              uint32_t elapsed_ticks,
                                                              uint32_t mid_ticks,
                                                              uint32_t dim_ticks,
                                                              uint32_t bg_ticks,
                                                              uint32_t span_ticks)
{
    if (!span_ticks || elapsed_ticks >= span_ticks)
        return 0u;
    return (uint8_t)(bright |
                     (elapsed_ticks < mid_ticks ? mid : 0u) |
                     (elapsed_ticks < dim_ticks ? dim : 0u) |
                     (elapsed_ticks < bg_ticks ? bg : 0u));
}

/* PA also carries the LCD backlight on PA2. Capture the complete output word
 * after all pin initialization, then keep this shadow coherent for every
 * scanner write. Normal startup initializes the LCD before the scanner; the
 * rescue key scan happens before LCD init and never resumes scanning after it. */
#if FM1_INPUT_FAST_SHIFT
static uint32_t fm1__pa_out_shadow;
static uint32_t fm1__ph_out_shadow;
#endif

#ifndef FM1_INPUT_GPIO_OUT_WRITE
#define FM1_INPUT_GPIO_OUT_WRITE(port, value) (FM1_PR((port), FM1_OUT) = (value))
#endif

FM1_INPUT_SCANNER_INLINE void fm1__pa_out_update(uint32_t mask, uint32_t value)
{
#if FM1_INPUT_FAST_SHIFT
    fm1__pa_out_shadow = (fm1__pa_out_shadow & ~mask) | (value & mask);
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA, fm1__pa_out_shadow);
#else
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA,
        (FM1_PR(FM1_PA, FM1_OUT) & ~mask) | (value & mask));
#endif
}

FM1_INPUT_SCANNER_INLINE void fm1__ph_out_update(uint32_t mask, uint32_t value)
{
#if FM1_INPUT_FAST_SHIFT
    fm1__ph_out_shadow = (fm1__ph_out_shadow & ~mask) | (value & mask);
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PH, fm1__ph_out_shadow);
#else
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PH,
        (FM1_PR(FM1_PH, FM1_OUT) & ~mask) | (value & mask));
#endif
}

static FM1_INPUT_SCANNER_RAM_ATTR void fm1__led_lines(uint32_t rowmask)
{
    const uint32_t pa_led_mask = (1u << 9) | (1u << 10);
    const uint32_t ph_led_mask = (1u << 6) | (1u << 9);
    uint32_t pa_on = (rowmask & 0x06u) << 8;
    uint32_t ph_on = ((rowmask & 0x08u) << 3) | ((rowmask & 0x10u) << 5);
    fm1__pa_out_update(pa_led_mask, pa_on);
    fm1__ph_out_update(ph_led_mask, ph_on);
}

static FM1_INPUT_SCANNER_RAM_ATTR void fm1__sr_bit(uint32_t w, uint32_t i)  /* bit i of w (msb first) into the 595 */
{
#if FM1_INPUT_FAST_SHIFT && FM1_INPUT_FAST_SR_SHADOW
    /* Timer5 is non-nested and no other live PA OUT writer runs during this
     * helper. Keep the identical three GPIO stores, but publish the final
     * shadow once after the bit's complete SER/SRCLK sequence. */
    uint32_t word = fm1__pa_out_shadow;
#if FM1_INPUT_COMPACT_SHIFT
    /* SER still settles for one complete peripheral write before SRCLK rises.
     * Its write also ends the previous bit's high clock pulse. */
    word = (word & ~((1u << 4) | (1u << 3))) |
        ((w & (0x8000u >> i)) ? (1u << 4) : 0u);
#else
    word = (word & ~(1u << 4)) | ((w & (0x8000u >> i)) ? (1u << 4) : 0u);
#endif
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA, word);
    word |= 1u << 3;
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA, word);
#if !FM1_INPUT_COMPACT_SHIFT
    word &= ~(1u << 3);
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA, word);
#endif
    fm1__pa_out_shadow = word;
#else
    fm1__pa_out_update(1u << 4, (w & (0x8000u >> i)) ? (1u << 4) : 0u);
    fm1__pa_out_update(1u << 3, 1u << 3);
    fm1__pa_out_update(1u << 3, 0u);
#endif
}
static FM1_INPUT_SCANNER_RAM_ATTR void fm1__sr_latch(void)                 /* the outputs change here only */
{
#if FM1_INPUT_FAST_SHIFT && FM1_INPUT_FAST_SR_SHADOW
    uint32_t word = fm1__pa_out_shadow | (1u << 1);
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA, word);
    word &= ~(1u << 1);
    FM1_INPUT_GPIO_OUT_WRITE(FM1_PA, word);
    fm1__pa_out_shadow = word;
#else
    fm1__pa_out_update(1u << 1, 1u << 1);
    fm1__pa_out_update(1u << 1, 0u);
#endif
}
static FM1_INPUT_SCANNER_RAM_ATTR void fm1__sr_word(uint32_t w)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        fm1__sr_bit(w, i);
    fm1__sr_latch();
}

static FM1_INPUT_SCANNER_RAM_ATTR uint32_t fm1__rows(void)
{
    uint32_t a = FM1_PR(FM1_PA, FM1_IN), b = FM1_PR(FM1_PB, FM1_IN);
    return (~((a & 1u) | ((a >> 4) & 0x1Eu) | ((b >> 2) & 0x20u))) & 0x3Fu;
}

static void fm1__wait(uint32_t us)
{
    uint32_t t0 = FM1_INPUT_TICKS(), span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(FM1_INPUT_TICKS() - t0) < span)
        FM1_INPUT_IDLE();
}

static void fm1_input_init(void)
{
    static const uint8_t LEDP[4][2] = {{FM1_PH, 6}, {FM1_PH, 9}, {FM1_PA, 9}, {FM1_PA, 10}};
    const uint32_t rows_a = (1u << 0) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    const uint32_t sr = (1u << 1) | (1u << 3) | (1u << 4), row_b = 1u << 7;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t p = LEDP[i][0], m = 1u << LEDP[i][1];
        FM1_PR(p, FM1_DIE) |= m;
        FM1_PR(p, FM1_OUT) &= ~m;
        FM1_PR(p, FM1_DIR) &= ~m;
        FM1_PR(p, FM1_HD0) |= m;
        FM1_PR(p, FM1_HD) |= m;
    }
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a;
    FM1_PR(FM1_PA, FM1_DIR) |= rows_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~rows_a;
    FM1_PR(FM1_PA, FM1_PU) |= rows_a;
    FM1_PR(FM1_PB, FM1_DIE) |= row_b;
    FM1_PR(FM1_PB, FM1_DIR) |= row_b;
    FM1_PR(FM1_PB, FM1_PD) &= ~row_b;
    FM1_PR(FM1_PB, FM1_PU) |= row_b;
    FM1_PR(FM1_PA, FM1_DIE) |= sr;
    FM1_PR(FM1_PA, FM1_PU) &= ~sr;
    FM1_PR(FM1_PA, FM1_PD) &= ~sr;
    FM1_PR(FM1_PA, FM1_OUT) &= ~sr;
    FM1_PR(FM1_PA, FM1_DIR) &= ~sr;
#if FM1_INPUT_FAST_SHIFT
    fm1__pa_out_shadow = FM1_PR(FM1_PA, FM1_OUT);
    fm1__ph_out_shadow = FM1_PR(FM1_PH, FM1_OUT);
#endif
#if defined(FM1_DEVICE) && defined(FM1_INPUT_SCANNER_RAM) && FM1_INPUT_SCANNER_RAM
    /* This init path remains in XIP while the scanner helpers are copied to
     * RAM. Use the same fixed, typed far-call form as the Timer5 entry. */
    void (* volatile sr_word_fn)(uint32_t) = fm1__sr_word;
    sr_word_fn(0xFFFFu);
#else
    fm1__sr_word(0xFFFFu);
#endif
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;   /* seeded by the first frame */
}

static FM1_INPUT_SCANNER_RAM_ATTR void fm1__key(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[id];
    uint32_t note = id >= 14u, bit = note ? 1u << (id - 14u) : 1u << id;
    uint32_t on = ((note ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
    if (closed == on) {                            /* agrees with the state: start over */
        *c = 0;
        return;
    }
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    if (fm1_in.pattern_activity != 0xffffffffu) ++fm1_in.pattern_activity;
#endif
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    if (note) {
        if (!on)
            fm1_in.notes_pressed |= bit;
        fm1_in.notes ^= bit;
    } else {
        fm1_in.buttons ^= bit;
        if (!on)
            fm1_in.pressed |= bit;
        else
            fm1_in.released |= bit;
    }
}

static FM1_INPUT_SCANNER_RAM_ATTR void fm1__keys(uint32_t p)                  /* the keys of column p, just read */
{
    uint32_t r, raw = fm1_in.raw[p];
    for (r = 1; r < 5u; r++)
        if (FM1_KEYMAP[r][p] >= 0)
            fm1__key((uint32_t)FM1_KEYMAP[r][p], (raw >> r) & 1u);
}

static FM1_INPUT_SCANNER_RAM_ATTR void fm1__frame(void);

static void fm1_input_scan(void)
{
    uint32_t p;
#if defined(FM1_DEVICE) && defined(FM1_INPUT_SCANNER_RAM) && FM1_INPUT_SCANNER_RAM
    void (* volatile scan_led_lines)(uint32_t) = fm1__led_lines;
    void (* volatile scan_sr_word)(uint32_t) = fm1__sr_word;
    uint32_t (* volatile scan_rows)(void) = fm1__rows;
    void (* volatile scan_keys)(uint32_t) = fm1__keys;
    void (* volatile scan_frame)(void) = fm1__frame;
#define FM1_INPUT_SCAN_LED_LINES scan_led_lines
#define FM1_INPUT_SCAN_SR_WORD scan_sr_word
#define FM1_INPUT_SCAN_ROWS scan_rows
#define FM1_INPUT_SCAN_KEYS scan_keys
#define FM1_INPUT_SCAN_FRAME scan_frame
#else
#define FM1_INPUT_SCAN_LED_LINES fm1__led_lines
#define FM1_INPUT_SCAN_SR_WORD fm1__sr_word
#define FM1_INPUT_SCAN_ROWS fm1__rows
#define FM1_INPUT_SCAN_KEYS fm1__keys
#define FM1_INPUT_SCAN_FRAME fm1__frame
#endif
    for (p = 0; p < FM1_NCOL; p++) {
        FM1_INPUT_SCAN_LED_LINES(0);
        FM1_INPUT_SCAN_SR_WORD(0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u));
        fm1__wait(FM1_SETTLE_US);
        fm1_in.raw[p] = (uint8_t)FM1_INPUT_SCAN_ROWS();
        FM1_INPUT_SCAN_KEYS(p);
        FM1_INPUT_SCAN_LED_LINES(fm1_led[p]);
        fm1__wait(FM1_LED_US);
    }
    FM1_INPUT_SCAN_LED_LINES(0);
    FM1_INPUT_SCAN_FRAME();
#undef FM1_INPUT_SCAN_LED_LINES
#undef FM1_INPUT_SCAN_SR_WORD
#undef FM1_INPUT_SCAN_ROWS
#undef FM1_INPUT_SCAN_KEYS
#undef FM1_INPUT_SCAN_FRAME
}

static FM1_INPUT_SCANNER_RAM_ATTR void fm1__frame(void)
{
    uint32_t e;                                    /* (the keys: fm1__keys, as each column is read) */
    for (e = 0; e < FM1_NENC; e++) {               /* quadrature decoder + detents */
        const uint8_t *m = FM1_ENC[e];
        uint32_t cur = ((fm1_in.raw[m[0]] >> m[1]) & 1u) << 1 | ((fm1_in.raw[m[2]] >> m[3]) & 1u);
        uint32_t idx;
        volatile int8_t *sub = &fm1_in.enc_sub[e];
        if (cur != fm1_in.enc_last[e]) {
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
            continue;
        }
        if (fm1_in.enc_prev[e] == 0xFF) {          /* first frame: the knob rests here */
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)cur;
        }
        if (fm1_in.enc_still[e] < 0xFFFFu && ++fm1_in.enc_still[e] == FM1_REST_FRAMES &&
            cur != fm1_in.enc_rest[e]) {
            fm1_in.enc_rest[e] = (uint8_t)cur;     /* parked ~1 s off the detent state (held at power-on): */
            *sub = 0;                              /* that is the detent. Never a second state (see top) */
        }
        if (cur == fm1_in.enc_prev[e])
            continue;
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
        if (fm1_in.pattern_activity != 0xffffffffu) ++fm1_in.pattern_activity;
#endif
        idx = (uint32_t)fm1_in.enc_prev[e] << 2 | cur;
        if ((0x4182u >> idx) & 1u)
            (*sub)++;
        else if ((0x2814u >> idx) & 1u)
            (*sub)--;
        else if (*sub > 0)                         /* two states in one sample: a fast turn, */
            *sub = (int8_t)(*sub + 2);             /* the way it was going */
        else if (*sub < 0)
            *sub = (int8_t)(*sub - 2);
        fm1_in.enc_prev[e] = (uint8_t)cur;
        if (*sub > 100 || *sub < -100)
            *sub = 0;                              /* (never off the detent that long) */
        if (cur == fm1_in.enc_rest[e]) {           /* back on the detent: whole cycles, a lost transition */
            int32_t n = *sub < 0 ? -*sub : *sub;   /* or two forgiven (one click = 4 transitions) */
            n = n >= 2 ? (n + 2) / 4 : 0;
            fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + (*sub < 0 ? -n : n));
            *sub = 0;
        }
    }
    fm1_in.frames++;
}

/* one column per call, from a timer ISR (see top). The dim pulses of column p ride on the shift of
 * column n: the lines go lit | glow | backlight of p, each layer ends when its time is up (TIMER4,
 * checked after every bit), and the latch comes with the lines dark. */
static uint8_t fm1__tick_col;
static FM1_INPUT_SCANNER_RAM_ATTR void fm1_input_tick(void)
{
    uint32_t p = fm1__tick_col, n = p + 1u == FM1_NCOL ? 0u : p + 1u, i;
    uint32_t w = 0xFFFFu ^ (1u << n) ^ (n < 2u ? 1u << (11u + n) : 0u);
    uint32_t lit = fm1_led[p], mid = fm1_led_mid[p] & ~lit;
    uint32_t a = fm1_led_dim[p] & ~lit & ~mid, b = fm1_led_bg[p] & ~lit & ~mid & ~a;
    uint32_t tm = mid ? FM1__NS_T(FM1_MID_NS) : 0u;
    uint32_t ta = a ? FM1__NS_T(FM1_GLOW_NS) : 0u, tb = b ? FM1__NS_T(fm1_led_bg_ns) : 0u;
    uint32_t tmax = tm > ta ? tm : ta;
    if (tb > tmax) tmax = tb;
    fm1__led_lines(0);
    fm1_in.raw[p] = (uint8_t)fm1__rows();          /* column p has been latched one tick (the lines dark) */
    if (tmax) {
        uint32_t t0 = FM1_INPUT_TICKS(), cur = lit | mid | a | (tb ? b : 0u), on, d;
        fm1__led_lines(cur);                       /* (the 595 still drives column p) */
        for (i = 0; i < 16u || cur; i++) {         /* the shift; then wait if the pulse is longer */
            if (i < 16u)
                fm1__sr_bit(w, i);
            d = FM1_INPUT_TICKS() - t0;
#if FM1_INPUT_FAST_PWM
            on = fm1__led_pwm_priority_mask_at((uint8_t)lit, (uint8_t)mid,
                                               (uint8_t)a, (uint8_t)b,
                                               d, tm, ta, tb, tmax);
#else
            on = fm1_led_pwm_mask_at((uint8_t)lit, (uint8_t)mid, (uint8_t)a, (uint8_t)b,
                                     d, tm, ta, tb);
#endif
            if (on != cur) {
                fm1__led_lines(on);
                cur = on;
            }
        }
        fm1__sr_latch();                           /* column n, the lines dark */
    } else {
        fm1__sr_word(w);
    }
    fm1__led_lines(fm1_led[n]);
    fm1__tick_col = (uint8_t)n;
    fm1__keys(p);                                  /* its keys now: no wait for the frame's end */
    if (n == 0u)
        fm1__frame();
}

/* main-loop critical section against fm1_input_tick (main loop only: it
 * re-enables interrupts unconditionally) */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* detent steps turned since the last call, + = clockwise */
static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}

/* Atomically take note press edges and the debounced held-key mask against
 * fm1_input_tick(). A press arriving after this snapshot is visible on the
 * next call in both fields, so callers cannot consume an edge against stale
 * held state. */
static uint32_t fm1_input_note_snapshot(uint32_t *held)
{
    uint32_t k = fm1__lock();
    uint32_t pressed = fm1_in.notes_pressed;
    if (held) *held = fm1_in.notes;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return pressed;
}

/* Set one button/note LED to a uniform intensity, replacing its previous level. */
static void fm1_led_level(uint32_t id, fm1_led_level_t level)
{
    uint8_t p, r, bit, mask;
    if (id >= FM1_LED_COUNT || level < FM1_LED_OFF || level > FM1_LED_BRIGHT)
        return;
    if (!fm1_led_position(id, &p, &r))
        return;
    bit = (uint8_t)(1u << r);
    mask = (uint8_t)~bit;
    if ((!!(fm1_led[p] & bit) == (level == FM1_LED_BRIGHT)) &&
        (!!(fm1_led_mid[p] & bit) == (level == FM1_LED_MID)) &&
        (!!(fm1_led_dim[p] & bit) == (level == FM1_LED_DIM)) &&
        !(fm1_led_bg[p] & bit))
        return;
    fm1_led[p] = (uint8_t)((fm1_led[p] & mask) | (level == FM1_LED_BRIGHT ? bit : 0u));
    fm1_led_mid[p] = (uint8_t)((fm1_led_mid[p] & mask) | (level == FM1_LED_MID ? bit : 0u));
    fm1_led_dim[p] = (uint8_t)((fm1_led_dim[p] & mask) | (level == FM1_LED_DIM ? bit : 0u));
    fm1_led_bg[p] &= mask;
}

/* Backward-compatible on/off control: old callers address the bright layer. */
static void fm1_led_key(uint32_t id, int on)
{
    uint8_t p, r, bit;
    if (id >= FM1_NKEY)
        return;
    if (!fm1_led_position(id, &p, &r))
        return;
    bit = (uint8_t)(1u << r);
    if (!!(fm1_led[p] & bit) == !!on)
        return;
    fm1_led[p] = (uint8_t)((fm1_led[p] & (uint8_t)~bit) | (on ? bit : 0u));
}
