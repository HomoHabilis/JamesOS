/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_DRUMS_H
#define BR_INSTRUMENT_DRUMS_H

#include <stdint.h>
#include "instrument.h"

/* Opaque, four-byte-aligned storage for four monophonic logical lanes. */
#define BR_DRUM_STATE_BYTES 12288u
/* Sufficient storage for one masked worker state on the target ABI. */
#define BR_DRUM_MASKED_STATE_BYTES 9216u

void br_drum_init(void *state, uint32_t seed, uint32_t kit);
void br_drum_note_on(void *state, uint32_t note, uint32_t velocity);
/* Opt-in split-worker API. A masked instance owns only lanes in lane_mask;
 * renders add an unclipped partial mix to out for summation across workers. */
void br_drum_note_on_mask(void *state, uint32_t note, uint32_t velocity,
                          uint32_t lane_mask);
void br_drum_note_on_voice_mask(void *state, uint32_t note, uint32_t velocity,
                                uint32_t lane_mask, uint32_t voice_id);
/* Per-hit semitone offset is latched with this trigger and never changes the
 * lane selected by note/lane_mask. Legacy note-on APIs use a zero offset. */
void br_drum_note_on_voice_pitch_mask(void *state, uint32_t note,
                                      uint32_t velocity, uint32_t lane_mask,
                                      uint32_t voice_id, int32_t pitch_delta);
void br_drum_render(void *state, const br_sound_t *sound, const uint32_t lane_level[BR_DRUM_LANES],
                    int32_t *out, uint32_t n);
void br_drum_render_gated(void *state, const br_sound_t *sound,
                          const uint32_t lane_level[BR_DRUM_LANES],
                          const uint32_t lane_gate_frames[BR_DRUM_LANES],
                          int32_t *out, uint32_t n);
/* Same render and PCM as the gated API, with the configured drum_key_lane's
 * gained audio replacing key[0..n). out remains additive; key may be NULL. */
void br_drum_render_keyed(void *state, const br_sound_t *sound,
                          const uint32_t lane_level[BR_DRUM_LANES],
                          const uint32_t lane_gate_frames[BR_DRUM_LANES],
                          int32_t *out, int32_t *key, uint32_t n);
void br_drum_render_keyed_mask(void *state, const br_sound_t *sound,
                               const uint32_t lane_level[BR_DRUM_LANES],
                               const uint32_t lane_gate_frames[BR_DRUM_LANES],
                               uint32_t lane_mask, int32_t *out, int32_t *key,
                               uint32_t n);
void br_drum_choke(void *state, uint32_t lane);
void br_drum_choke_mask(void *state, uint32_t lane, uint32_t lane_mask);
void br_drum_all_off(void *state);
void br_drum_all_off_mask(void *state, uint32_t lane_mask);
int br_drum_quiet(const void *state);
int br_drum_quiet_mask(const void *state, uint32_t lane_mask);
/* Read-only BRAIDS scheduler lookahead. `engine_work` excludes tail-only
 * output and active voices being discarded after a selection change. */
uint32_t br_drum_lane_scheduler_state(const void *state,
                                     const br_sound_t *sound,
                                     uint32_t lane,
                                     uint32_t *engine_work,
                                     uint32_t *render_voice);
uint32_t br_drum_lane_scheduler_state_ex(const void *state,
                                        const br_sound_t *sound,
                                        uint32_t lane,
                                        uint32_t *engine_work,
                                        uint32_t *render_voice,
                                        uint32_t *render_cold);

/* Standard GM aliases map to the four logical roles. The selected A/B voice
 * is controlled by br_sound_t::drum_voice_mask, never by the MIDI alias. */
static inline uint32_t br_drum_lane(uint32_t note)
{
    uint32_t i, best = 0u, distance = 0xFFFFFFFFu;
    static const uint8_t canonical[BR_DRUM_LANES] = {36u, 38u, 46u, 49u};
    if (note == 35u || note == 36u) return 0u;
    if (note == 38u || note == 40u || note == 39u) return 1u;
    if (note == 42u || note == 44u || note == 46u) return 2u;
    if (note == 41u || note == 43u || note == 45u || note == 47u ||
        note == 48u || note == 50u || note == 37u ||
        (note >= 49u && note <= 59u)) return 3u;
    for (i = 0u; i < BR_DRUM_LANES; ++i) {
        uint32_t d = note > canonical[i] ? note - canonical[i] : canonical[i] - note;
        if (d < distance) { distance = d; best = i; }
    }
    return best;
}

static inline uint32_t br_drum_lane_note(uint32_t lane)
{
    static const uint8_t canonical[BR_DRUM_LANES] = {36u, 38u, 46u, 49u};
    return canonical[lane < BR_DRUM_LANES ? lane : 0u];
}

#endif
