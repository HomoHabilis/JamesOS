/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_POLY_SOURCE_H
#define BR_INSTRUMENT_POLY_SOURCE_H
#include <string.h>
#include "instrument_poly_storage.h"
#include "instrument_model_sweep.h"
#include "../plaits/plaits_c_api.h"
#ifndef BR_MODEL_MOD_EXPERIMENT
#define BR_MODEL_MOD_EXPERIMENT 0
#endif

/* Independent sound sources only: the audio engine must apply a separate
 * envelope per voice, combine voices, and apply the selected track's FX. */
typedef struct {
    br_poly_allocator_t allocator;
    br_poly_storage_t storage;
    uint32_t model;
#if BR_MODEL_MOD_EXPERIMENT
    uint32_t voice_bytes;
    br_model_sweep_plan sweep;
    uint8_t voice_model[BR_POLY_VOICES];
#endif
} br_poly_source_t;

/* capacity_model selects the arena size independently of the starting sound.
 * The standard path keeps the compact allocation; the experimental sweep
 * path reserves enough space for every model before voices become active. */
BR_COLD_SIZE static inline int br_poly_source_init_capacity_count(br_poly_source_t *s, uint32_t model,
        uint32_t capacity_model, void *primary, size_t primary_bytes, void *extra, size_t extra_bytes, unsigned count)
{
    br_poly_source_t next;
    uint32_t bytes = plaits_compact_state_bytes(capacity_model);
    memset(&next, 0, sizeof next);
    if (!s || !bytes || !plaits_compact_state_bytes(model) ||
        bytes < plaits_compact_state_bytes(model) || !br_poly_storage_plan_count(&next.storage, primary, primary_bytes,
                                              extra, extra_bytes, bytes, count)) return 0;
    memset(&next.allocator, 0, sizeof next.allocator);
    next.allocator.limit = (uint8_t)count;
    next.model = model;
#if BR_MODEL_MOD_EXPERIMENT
    next.voice_bytes = bytes;
    br_model_sweep_init(&next.sweep, (uint8_t)model);
    next.sweep.voice_count = (uint8_t)count;
#endif
    for (unsigned i = 0; i < count; ++i) {
#if BR_MODEL_MOD_EXPERIMENT
        next.voice_model[i] = (uint8_t)model;
#endif
        if (!plaits_compact_state_init(next.storage.voice[i], bytes, capacity_model,
                                       0x42520021u + i)) return 0;
        /* The caller constructs these while audio is muted and workers are
         * parked, avoiding six simultaneous cold model starts in a render. */
        plaits_prepare_model(next.storage.voice[i], model);
    }
    *s = next;
    return 1;
}

BR_COLD_SIZE static inline int br_poly_source_init_capacity(br_poly_source_t *s, uint32_t model,
        uint32_t capacity_model, void *primary, size_t primary_bytes, void *extra, size_t extra_bytes)
{
    return br_poly_source_init_capacity_count(s, model, capacity_model, primary, primary_bytes,
                                              extra, extra_bytes, BR_POLY_VOICES);
}

#if BR_MODEL_MOD_EXPERIMENT
/* Request/select/commit are called by the audio owner outside worker jobs.
 * Preparation belongs to the selected voice's sole worker, after its prior
 * job completed and before rendering it. Commit only after that job joins.
 * This bounds initialization count, not its execution time. */
static inline int br_poly_source_request_model(br_poly_source_t *s, uint32_t model)
{
    uint32_t bytes = plaits_compact_state_bytes(model);
    return bytes && bytes <= s->voice_bytes && br_model_sweep_request(&s->sweep, model);
}

static inline uint8_t br_poly_source_next_model_voice(const br_poly_source_t *s, uint32_t interval)
{
    return br_model_sweep_next(&s->sweep, interval);
}

static inline int br_poly_source_prepare_voice(br_poly_source_t *s, uint8_t voice, uint8_t model)
{
#if !FM1_PLAITS_PREWARM_MODEL
    (void)s; (void)voice; (void)model;
    return 0;
#else
    if (voice >= s->allocator.limit || !plaits_compact_state_bytes(model) ||
        plaits_compact_state_bytes(model) > s->voice_bytes) return 0;
    plaits_prepare_model(s->storage.voice[voice], model);
    if (plaits_active_engine(s->storage.voice[voice]) != (int32_t)model) return 0;
    s->voice_model[voice] = model;
    return 1;
#endif
}

static inline int br_poly_source_commit_model(br_poly_source_t *s, uint8_t voice, uint32_t interval)
{
    if (voice >= s->allocator.limit) return 0;
    return br_model_sweep_commit(&s->sweep, voice, s->voice_model[voice], interval);
}
#endif

static inline uint32_t br_poly_source_voice_model(const br_poly_source_t *s, uint8_t voice)
{
#if BR_MODEL_MOD_EXPERIMENT
    return s->voice_model[voice];
#else
    (void)voice;
    return s->model;
#endif
}

BR_COLD_SIZE static inline int br_poly_source_init(br_poly_source_t *s, uint32_t model,
        void *primary, size_t primary_bytes, void *extra, size_t extra_bytes)
{
    return br_poly_source_init_capacity(s, model, model, primary, primary_bytes, extra, extra_bytes);
}

BR_COLD_SIZE static inline uint8_t br_poly_source_on(br_poly_source_t *s, uint8_t note,
        int32_t pitch, uint16_t timbre, uint16_t morph, uint16_t harmonics,
        uint8_t velocity, uint8_t *stolen, uint32_t *generation)
{
    uint8_t voice = br_poly_note_on(&s->allocator, note, stolen, generation);
    if (*stolen != BR_POLY_NO_VOICE) plaits_all_off(s->storage.voice[voice]);
    plaits_note_on(s->storage.voice[voice], br_poly_source_voice_model(s, voice), pitch, timbre, morph, harmonics, velocity);
    return voice;
}

BR_COLD_SIZE static inline uint8_t br_poly_source_off(br_poly_source_t *s, uint8_t note, uint32_t generation)
{
    uint8_t voice = br_poly_note_off(&s->allocator, note, generation);
    if (voice != BR_POLY_NO_VOICE) plaits_note_off(s->storage.voice[voice], 128u);
    return voice;
}

static inline void br_poly_source_render(br_poly_source_t *s, uint8_t voice,
        int32_t pitch, uint16_t timbre, uint16_t morph, uint16_t harmonics,
        uint16_t aux, int16_t *out, uint32_t n)
{
    if (voice >= s->allocator.limit || !s->storage.voice[voice] || plaits_quiet(s->storage.voice[voice])) {
        memset(out, 0, n * sizeof(*out));
        return;
    }
    plaits_render(s->storage.voice[voice], br_poly_source_voice_model(s, voice), pitch, timbre, morph, harmonics, aux, out, n);
}
#endif
