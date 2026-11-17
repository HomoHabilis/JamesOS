/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_POLY_EVENTS_H
#define BR_INSTRUMENT_POLY_EVENTS_H
#include "instrument_poly_render.h"

/* Each input press retains its own generation even after its voice is stolen.
 * Tags identify physical keys or a MIDI channel/note pair. */
#define BR_POLY_INPUTS 256u
typedef struct { uint32_t generation; uint16_t tag; uint8_t note; } br_poly_input_t;
typedef struct { br_poly_input_t held[BR_POLY_INPUTS]; uint16_t count; } br_poly_events_t;

BR_COLD_SIZE static int br_poly_events_on(br_poly_events_t *events, br_poly_render_t *render,
        const br_sound_t *sound, uint16_t tag, uint8_t note, int32_t pitch, uint8_t velocity)
{
    uint8_t stolen;
    uint32_t generation;
    if (!events || !render || !sound || !tag || !velocity || events->count >= BR_POLY_INPUTS) return 0;
    br_poly_render_on_overlap(render, sound, note, pitch, velocity, &stolen, &generation, events->count != 0u);
    br_poly_input_t *slot = &events->held[events->count++];
    slot->tag = tag; slot->note = note; slot->generation = generation;
    return 1;
}

/* Releases the oldest press with this source and pitch; a stolen press is
 * retired without touching a newer voice using the same note. */
BR_COLD_SIZE static int br_poly_events_take(br_poly_events_t *events, uint16_t tag, uint8_t note,
                               uint32_t *generation)
{
    if (!events || !tag || !generation) return 0;
    for (uint32_t i = 0; i < events->count; ++i) {
        if (events->held[i].tag != tag || events->held[i].note != note) continue;
        *generation = events->held[i].generation;
        memmove(&events->held[i], &events->held[i + 1u],
                (events->count - i - 1u) * sizeof events->held[0]);
        --events->count;
        return 1;
    }
    return 0;
}
BR_COLD_SIZE static int br_poly_events_off(br_poly_events_t *events, br_poly_render_t *render,
                              uint16_t tag, uint8_t note)
{
    uint32_t generation;
    if (!render || !br_poly_events_take(events, tag, note, &generation)) return 0;
    br_poly_render_off(render, note, generation);
    return 1;
}
#endif
