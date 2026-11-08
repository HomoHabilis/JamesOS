/* SPDX-License-Identifier: MIT
 * Compact original Plaits drum-engine kit for the Dinghy instrument API.
 */
#ifndef DINGHY_PLAITS_KIT_C_API_H_
#define DINGHY_PLAITS_KIT_C_API_H_
#include <stdint.h>
#include "../src/instrument.h"
#include "../src/instrument_drums.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PLAITS_KIT_STATE_BYTES 5760u
#define PLAITS_KIT_STATE_ALIGN 8u
void plaits_kit_init(void *state, uint32_t seed);
/* Initialize two independently backed masked instances while consuming the
 * same one-init global RNG stream as a single full-kit instance. */
void plaits_kit_init_pair(void *state_a, void *state_b, uint32_t seed);
/* Initialize four independently backed masked instances while consuming the
 * same one-init global RNG stream as a single full-kit instance. */
void plaits_kit_init_quad(void *states[4], uint32_t seed);
void plaits_kit_note_on(void *state, uint32_t raw_note, uint32_t velocity);
/* Split-worker APIs are opt-in. Legacy entry points below keep all-lane
 * behavior; masked renders return an unclipped partial sum for the caller to
 * combine across workers before final int16 saturation. */
void plaits_kit_note_on_mask(void *state, uint32_t raw_note,
                             uint32_t velocity, uint32_t lane_mask);
void plaits_kit_note_on_voice_mask(void *state, uint32_t raw_note,
                                   uint32_t velocity, uint32_t lane_mask,
                                   uint32_t voice_id);
/* Per-hit semitone offset is latched with this trigger and never changes the
 * lane selected by raw_note/lane_mask. Legacy note-on APIs use zero. */
void plaits_kit_note_on_voice_pitch_mask(void *state, uint32_t raw_note,
                                         uint32_t velocity, uint32_t lane_mask,
                                         uint32_t voice_id,
                                         int32_t pitch_delta);
void plaits_kit_note_off(void *state, uint32_t raw_note);
void plaits_kit_note_off_mask(void *state, uint32_t raw_note,
                              uint32_t lane_mask);
/* Render entry points write all n frames and split larger requests into
 * source/FIFO-safe chunks internally. */
void plaits_kit_render(void *state, const br_sound_t *sound,
                       const uint32_t lane_level[BR_DRUM_LANES], int32_t *out, uint32_t n);
void plaits_kit_render_gated(void *state, const br_sound_t *sound,
                             const uint32_t lane_level[BR_DRUM_LANES],
                             const uint32_t lane_gate_frames[BR_DRUM_LANES],
                             int32_t *out, uint32_t n);
/* Same render and PCM as the gated API, with the configured drum_key_lane's
 * gained audio returned separately in key. key is overwritten per call. */
void plaits_kit_render_keyed(void *state, const br_sound_t *sound,
                             const uint32_t lane_level[BR_DRUM_LANES],
                             const uint32_t lane_gate_frames[BR_DRUM_LANES],
                             int32_t *out, int32_t *key, uint32_t n);
void plaits_kit_render_keyed_mask(void *state, const br_sound_t *sound,
                                  const uint32_t lane_level[BR_DRUM_LANES],
                                  const uint32_t lane_gate_frames[BR_DRUM_LANES],
                                  uint32_t lane_mask, int32_t *out,
                                  int32_t *key, uint32_t n);
/* Return the number of successful 24-source-frame FIFO append operations
 * performed by this render. Audio is identical to plaits_kit_render_keyed_mask. */
uint32_t plaits_kit_render_keyed_mask_counted(
    void *state, const br_sound_t *sound,
    const uint32_t lane_level[BR_DRUM_LANES],
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    int32_t *out, int32_t *key, uint32_t n);
/* Same exact render, also reporting successful 24-source-frame appends per
 * logical lane. Counts are zeroed for lanes outside lane_mask. */
uint32_t plaits_kit_render_keyed_mask_counted_by_lane(
    void *state, const br_sound_t *sound,
    const uint32_t lane_level[BR_DRUM_LANES],
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    int32_t *out, int32_t *key, uint32_t n,
    uint8_t append_units[BR_DRUM_LANES]);
/* Read-only lookahead for the next render call. The result is the predicted
 * number of successful 24-source-frame FIFO append operations. */
uint32_t plaits_kit_predict_append_units(
    const void *state, const br_sound_t *sound,
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    uint32_t n);
/* Extended read-only lookahead. `append_units` is per logical lane. A bit in
 * nonproportional_mask marks pending/voice-change/clap work whose cost is not
 * safely scaled from a prior FIFO-append timing. */
uint32_t plaits_kit_predict_append_units_ex(
    const void *state, const br_sound_t *sound,
    const uint32_t lane_gate_frames[BR_DRUM_LANES], uint32_t lane_mask,
    uint32_t n, uint32_t *nonproportional_mask,
    uint32_t *cost_signature, uint8_t append_units[BR_DRUM_LANES]);
void plaits_kit_all_off(void *state);
void plaits_kit_all_off_mask(void *state, uint32_t lane_mask);
uint32_t plaits_kit_quiet(const void *state);
uint32_t plaits_kit_quiet_mask(const void *state, uint32_t lane_mask);
/* Read-only scheduler lookahead for one lane. */
uint32_t plaits_kit_lane_scheduler_state(const void *state,
                                         const br_sound_t *sound,
                                         uint32_t lane,
                                         uint32_t *engine_work,
                                         uint32_t *render_voice);
#ifdef __cplusplus
}
#endif
#endif
