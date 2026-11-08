/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared LED intensity contract. Keep this header free of platform dependencies. */
#pragma once
#include <stdint.h>

typedef enum {
    FM1_LED_OFF = 0,
    FM1_LED_DIM = 1,
    FM1_LED_MID = 2,
    FM1_LED_BRIGHT = 3
} fm1_led_level_t;

/* Physical key matrix: rows are indexed by the packed GPIO row bit, columns
 * by the shift-register column. The lookup below is the inverse mapping used
 * by LED setters, so callers do not scan this 6x11 matrix for every update. */
static const int8_t FM1_LED_KEYMAP[6][11] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
};

/* Green PLAY has no key: hardware-tested mapping from Felucca ui_input.c
 * commit a4f007c9211ea5b37298c01ff6d73430fe2cb96a, column 8 / PA9 (row bit 1). */
enum { FM1_LED_PLAY_GREEN = 41u, FM1_LED_COUNT = 42u };

/* Indexed by public LED ID, with {column, row-bit}. */
static const uint8_t FM1_LED_POSITION[FM1_LED_COUNT][2] = {
    {0,4}, {1,4}, {6,1}, {4,1}, {2,1}, {0,1}, {9,2}, {8,2}, {7,1}, {5,1},
    {3,1}, {1,1}, {10,2}, {7,2}, {3,4}, {2,4}, {5,4}, {4,4}, {7,4}, {6,4},
    {8,4}, {9,4}, {10,4}, {0,3}, {1,3}, {2,3}, {3,3}, {4,3}, {5,3}, {6,3},
    {7,3}, {8,3}, {9,3}, {10,3}, {0,2}, {1,2}, {2,2}, {3,2}, {4,2}, {6,2},
    {5,2}, {8,1}
};

static inline int fm1_led_position(uint32_t id, uint8_t *column, uint8_t *row)
{
    if (id >= FM1_LED_COUNT || !column || !row)
        return 0;
    *column = FM1_LED_POSITION[id][0];
    *row = FM1_LED_POSITION[id][1];
    return 1;
}

/* Resolve the active pulse for one column while TIMER4 shifts the next column.
 * Higher levels mask lower ones; bright remains on for this bounded slot. */
static inline uint8_t fm1_led_pwm_mask_at(uint8_t bright, uint8_t mid, uint8_t dim,
                                          uint8_t bg, uint32_t elapsed_ticks,
                                          uint32_t mid_ticks, uint32_t dim_ticks,
                                          uint32_t bg_ticks)
{
    uint8_t m = (uint8_t)(mid & (uint8_t)~bright);
    uint8_t d = (uint8_t)(dim & (uint8_t)~(bright | mid));
    uint8_t b = (uint8_t)(bg & (uint8_t)~(bright | mid | dim));
    if (!m) mid_ticks = 0;
    if (!d) dim_ticks = 0;
    if (!b) bg_ticks = 0;
    uint32_t span = mid_ticks > dim_ticks ? mid_ticks : dim_ticks;
    if (bg_ticks > span)
        span = bg_ticks;
    if (!span || elapsed_ticks >= span)
        return 0;
    return (uint8_t)(bright |
                     (elapsed_ticks < mid_ticks ? m : 0u) |
                     (elapsed_ticks < dim_ticks ? d : 0u) |
                     (elapsed_ticks < bg_ticks ? b : 0u));
}
