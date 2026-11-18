/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_POLY_RENDER_H
#define BR_INSTRUMENT_POLY_RENDER_H
#include "instrument_poly_source.h"

/* Included after the normal audio engine's envelope helpers. Effects, track
 * modulation, mixing and real-time scheduling remain the caller's job. */
typedef struct {
    br_poly_source_t source;
    br_env_t envelope[BR_POLY_VOICES];
    br_glide_t glide[BR_POLY_VOICES];
    int32_t lpg[BR_POLY_VOICES];
    uint8_t last_voice, last_valid;
} br_poly_render_t;

BR_COLD_SIZE static int br_poly_render_init_capacity_count(br_poly_render_t *r, uint32_t model, uint32_t capacity_model,
        void *primary, size_t primary_bytes, void *extra, size_t extra_bytes, unsigned count)
{
    if (!r || !br_poly_source_init_capacity_count(&r->source, model, capacity_model, primary, primary_bytes, extra, extra_bytes, count)) return 0;
    memset(r->envelope, 0, sizeof r->envelope);
    memset(r->glide, 0, sizeof r->glide); memset(r->lpg, 0, sizeof r->lpg);
    r->last_valid = 0u;
    return 1;
}

BR_COLD_SIZE static int br_poly_render_init_capacity(br_poly_render_t *r, uint32_t model, uint32_t capacity_model,
        void *primary, size_t primary_bytes, void *extra, size_t extra_bytes)
{
    return br_poly_render_init_capacity_count(r, model, capacity_model, primary, primary_bytes,
                                              extra, extra_bytes, BR_POLY_VOICES);
}

BR_COLD_SIZE static int br_poly_render_init(br_poly_render_t *r, uint32_t model,
        void *primary, size_t primary_bytes, void *extra, size_t extra_bytes)
{
    return br_poly_render_init_capacity(r, model, model, primary, primary_bytes, extra, extra_bytes);
}

BR_COLD_SIZE static uint8_t br_poly_render_on_overlap(br_poly_render_t *r, const br_sound_t *sound,
        uint8_t note, int32_t pitch, uint8_t velocity, uint8_t *stolen, uint32_t *generation, uint32_t overlap)
{
    br_glide_t next = {0};
    if (r->last_valid) { next.valid = 1u; next.target_q16 = br_glide_pitch(&r->glide[r->last_voice]); }
    br_glide_note(&next, (uint32_t)pitch, sound->portamento, sound->glide_mode, overlap, FM1_AUDIO_RATE_HZ);
    uint8_t voice = br_poly_source_on(&r->source, note, (br_glide_pitch(&next) + 32768) >> 16,
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->timbre, 127u)),
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->color, 127u)),
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->plaits_harmonics, 127u)),
        velocity, stolen, generation);
    if (*stolen != BR_POLY_NO_VOICE) memset(&r->envelope[voice], 0, sizeof r->envelope[voice]);
    r->glide[voice] = next; r->last_voice = voice; r->last_valid = 1u;
    if (*stolen != BR_POLY_NO_VOICE || r->envelope[voice].stage == BR_ENV_OFF) r->lpg[voice] = 0;
    br_env_start_state(&r->envelope[voice], note, velocity, sound, 1u);
    return voice;
}
BR_COLD_SIZE static uint8_t br_poly_render_on(br_poly_render_t *r, const br_sound_t *sound,
        uint8_t note, int32_t pitch, uint8_t velocity, uint8_t *stolen, uint32_t *generation)
{ return br_poly_render_on_overlap(r, sound, note, pitch, velocity, stolen, generation, 0u); }

BR_COLD_SIZE static uint8_t br_poly_render_off(br_poly_render_t *r, uint8_t note, uint32_t generation)
{
    uint8_t voice = br_poly_source_off(&r->source, note, generation);
    if (voice != BR_POLY_NO_VOICE && r->envelope[voice].stage != BR_ENV_OFF && r->envelope[voice].stage != BR_ENV_RELEASE) {
        r->envelope[voice].stage = BR_ENV_RELEASE;
        r->envelope[voice].release_start = r->envelope[voice].amp;
    }
    return voice;
}

/* Render one independently enveloped voice. All output samples are written. */
static void br_poly_render_voice(br_poly_render_t *r, uint8_t voice,
        const br_sound_t *sound, int32_t pitch, int32_t *out, uint32_t n)
{
    int16_t samples[BR_BLOCK];
    if (voice >= BR_POLY_VOICES || n > BR_BLOCK) return;
    if (voice >= r->source.allocator.limit) { memset(out, 0, n * sizeof(*out)); return; }
    br_env_t *e = &r->envelope[voice];
    if (r->glide[voice].valid) {
        int32_t target = r->glide[voice].target_q16 >> 16;
        pitch = (int32_t)br_glide_advance(&r->glide[voice], n) + pitch - target;
        if (pitch < 0) pitch = 0; else if (pitch > 16383) pitch = 16383;
    }
    br_poly_source_render(&r->source, voice, pitch,
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->timbre, 127u)),
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->color, 127u)),
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->plaits_harmonics, 127u)),
        (uint16_t)br_scale_7bit_q15_exact(br_bound(sound->plaits_aux, 127u)), samples, n);
    if (e->stage == BR_ENV_OFF) { memset(out, 0, n * sizeof(*out)); return; }
    br_env_refresh(e, sound, 1u);
#if BR_MODEL_MOD_EXPERIMENT
    uint32_t calibration = br_audio_preset_gain(BR_FIRST_PLAITS + br_poly_source_voice_model(&r->source, voice));
#else
    uint32_t calibration = br_audio_preset_gain(sound->preset);
#endif
    uint32_t level = sound->mute ? 0u : br_bound(sound->level, 127u);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t gain;
        if (e->velocity_gain_ramp_left) {
            uint32_t level_gain = level * 127u;
            if (calibration != 64u) level_gain = (level_gain * calibration + 32u) >> 6;
            gain = (br_env_advance_velocity_gain(e) * level_gain + 16384u) >> 15;
        } else {
            gain = e->velocity * level;
            if (calibration != 64u) gain = (gain * calibration + 32u) >> 6;
        }
        br_env_advance(e);
        int32_t raw = sound->amp_mode ? br_lpg_sample(samples[i], e->amp, &r->lpg[voice]) : samples[i];
        int32_t sample = (raw * (int32_t)e->amp) >> 15;
        out[i] = br_div16129_exact(sample * (int32_t)gain);
    }
}
#endif
