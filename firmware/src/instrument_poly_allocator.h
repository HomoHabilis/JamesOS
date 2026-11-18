/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_POLY_ALLOCATOR_H
#define BR_INSTRUMENT_POLY_ALLOCATOR_H
#include <stdint.h>
/* BR_COLD_SIZE: see instrument_audio.c; empty when this header is used on its own. */
#ifndef BR_COLD_SIZE
#define BR_COLD_SIZE
#endif

/* Voice selection only. The audio engine owns rendering and release tails. */
#ifndef BR_ADAPTIVE_POLY
#define BR_ADAPTIVE_POLY 0
#endif
#if BR_ADAPTIVE_POLY
#define BR_POLY_VOICES 8u
#else
#define BR_POLY_VOICES 6u
#endif
#define BR_POLY_NO_VOICE 255u
typedef struct {
    uint8_t note, count;
    uint32_t age;
} br_poly_voice_t;
typedef struct {
    br_poly_voice_t voice[BR_POLY_VOICES];
    uint32_t clock;
    uint8_t limit; /* zero retains the default maximum for standalone callers */
} br_poly_allocator_t;

/* Returns a voice index, and, when replacing a held note, its note number. */
BR_COLD_SIZE static inline uint8_t br_poly_note_on(br_poly_allocator_t *a, uint8_t note, uint8_t *stolen, uint32_t *generation)
{
    unsigned i, limit = a->limit && a->limit <= BR_POLY_VOICES ? a->limit : BR_POLY_VOICES;
    unsigned chosen = limit;
    *stolen = BR_POLY_NO_VOICE;
    for (i = 0; i < limit; ++i) {
        if (a->voice[i].count && a->voice[i].note == note) {
            if (a->voice[i].count < 255u) ++a->voice[i].count;
            *generation = a->voice[i].age;
            return (uint8_t)i;
        }
        if (!a->voice[i].count && chosen == limit) chosen = i;
    }
    if (chosen == limit) {
        chosen = 0;
        for (i = 1; i < limit; ++i)
            if ((int32_t)(a->voice[i].age - a->voice[chosen].age) < 0) chosen = i;
        *stolen = a->voice[chosen].note;
    }
    a->voice[chosen].note = note;
    a->voice[chosen].count = 1u;
    a->voice[chosen].age = ++a->clock;
    *generation = a->voice[chosen].age;
    return (uint8_t)chosen;
}

/* Returns the released voice only after the last matching input releases. */
BR_COLD_SIZE static inline uint8_t br_poly_note_off(br_poly_allocator_t *a, uint8_t note, uint32_t generation)
{
    for (unsigned i = 0; i < BR_POLY_VOICES; ++i) {
        if (!a->voice[i].count || a->voice[i].note != note || a->voice[i].age != generation) continue;
        if (--a->voice[i].count == 0u) return (uint8_t)i;
        return BR_POLY_NO_VOICE;
    }
    return BR_POLY_NO_VOICE;
}
#endif
