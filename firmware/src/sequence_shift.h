/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_SEQUENCE_SHIFT_H
#define BR_SEQUENCE_SHIFT_H
#include "instrument.h"

/* Rotate only the active steps; hidden steps beyond the track length stay put.
 * Move the entire step, including inactive step metadata and every drum lane. */
#define BR_ROTATE_ROW(row, count, direction, type) do { \
    type br_saved = (row)[(direction) > 0 ? (count) - 1u : 0u]; \
    if ((direction) > 0) { \
        for (uint32_t br_i = (count) - 1u; br_i; --br_i) (row)[br_i] = (row)[br_i - 1u]; \
        (row)[0] = br_saved; \
    } else { \
        for (uint32_t br_i = 0; br_i + 1u < (count); ++br_i) (row)[br_i] = (row)[br_i + 1u]; \
        (row)[(count) - 1u] = br_saved; \
    } \
} while (0)

static void br_sequence_shift_track(uint32_t part, int direction)
{
    if (part >= BR_PARTS || !direction) return;
    uint32_t n = br_part[part].length;
    if (n > BR_STEPS) n = BR_STEPS;
    if (n < 2u) return;
    BR_ROTATE_ROW(br_part[part].step, n, direction, uint32_t);
    if (part != BR_DRUM_PART) {
        BR_ROTATE_ROW(br_seq_synth_length[part], n, direction, uint16_t);
        BR_ROTATE_ROW(br_seq_synth_condition[part], n, direction, uint8_t);
        BR_ROTATE_ROW(br_seq_synth_note[part], n, direction, uint8_t);
        BR_ROTATE_ROW(br_seq_synth_velocity[part], n, direction, uint8_t);
        BR_ROTATE_ROW(br_seq_synth_micro[part], n, direction, int8_t);
    } else {
        /* The kit arrays are step-major. Rotate each lane independently. */
#define BR_ROTATE_KIT(field, type) do { \
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) { \
            type saved = field[direction > 0 ? n - 1u : 0u][lane]; \
            if (direction > 0) { \
                for (uint32_t i = n - 1u; i; --i) field[i][lane] = field[i - 1u][lane]; \
                field[0][lane] = saved; \
            } else { \
                for (uint32_t i = 0; i + 1u < n; ++i) field[i][lane] = field[i + 1u][lane]; \
                field[n - 1u][lane] = saved; \
            } \
        } \
    } while (0)
        BR_ROTATE_KIT(br_seq_kit_length, uint16_t);
        BR_ROTATE_KIT(br_seq_kit_condition, uint8_t);
        BR_ROTATE_KIT(br_seq_kit_velocity, uint8_t);
        BR_ROTATE_KIT(br_seq_kit_retrig, uint8_t);
        BR_ROTATE_KIT(br_seq_kit_note, uint8_t);
        BR_ROTATE_KIT(br_seq_kit_pitch, uint8_t);
        BR_ROTATE_KIT(br_seq_kit_micro, int8_t);
#undef BR_ROTATE_KIT
    }
}
#undef BR_ROTATE_ROW
#endif
