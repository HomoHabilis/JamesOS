/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_MODEL_SWEEP_H
#define BR_INSTRUMENT_MODEL_SWEEP_H
#include <stdint.h>

/* Experimental foreground/audio handoff planner. The caller may prepare at
 * most one voice per audio interval, only while that voice is exclusively
 * owned. Requests coalesce to the latest model. Planning alone does not fence
 * workers, migrate held notes, or establish a device deadline. */
#if defined(BR_ADAPTIVE_POLY) && BR_ADAPTIVE_POLY
#define BR_SWEEP_VOICES 8u
#else
#define BR_SWEEP_VOICES 6u
#endif
enum { BR_SWEEP_MODELS = 24u, BR_SWEEP_NONE = 255u };
typedef struct {
    uint32_t last_interval;
    uint8_t voice_model[BR_SWEEP_VOICES];
    uint8_t target, cursor, interval_used, voice_count;
} br_model_sweep_plan;

static inline void br_model_sweep_init(br_model_sweep_plan *s, uint8_t model)
{
    s->target = model; s->cursor = s->interval_used = 0u; s->last_interval = 0u; s->voice_count = BR_SWEEP_VOICES;
    for (uint32_t i = 0; i < BR_SWEEP_VOICES; ++i) s->voice_model[i] = model;
}
static inline int br_model_sweep_request(br_model_sweep_plan *s, uint32_t model)
{
    if (model >= BR_SWEEP_MODELS) return 0;
    s->target = (uint8_t)model;
    return 1;
}
/* Return the next voice; the caller commits only after successful preparation. */
static inline uint8_t br_model_sweep_next(const br_model_sweep_plan *s, uint32_t interval)
{
    if (s->interval_used && s->last_interval == interval) return BR_SWEEP_NONE;
    for (uint32_t i = 0; i < s->voice_count; ++i) {
        uint32_t v = (s->cursor + i) % s->voice_count;
        if (s->voice_model[v] != s->target) return (uint8_t)v;
    }
    return BR_SWEEP_NONE;
}
static inline int br_model_sweep_commit(br_model_sweep_plan *s, uint8_t voice, uint8_t prepared_model, uint32_t interval)
{
    if (voice >= s->voice_count || prepared_model >= BR_SWEEP_MODELS ||
        (s->interval_used && s->last_interval == interval)) return 0;
    s->voice_model[voice] = prepared_model;
    s->cursor = (uint8_t)((voice + 1u) % s->voice_count);
    s->last_interval = interval; s->interval_used = 1u;
    return 1;
}
#endif
