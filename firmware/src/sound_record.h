/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_SOUND_RECORD_H
#define FM1_SOUND_RECORD_H
#include <stdint.h>
#include "instrument.h"

/* Version 1: little-endian 32-bit fields, independent of C struct layout.
 * Synth and kit records include oscillator, envelopes, filters, gate, sends,
 * per-track effects, full modulation and kit voice choices. Track level, pan,
 * mute, patterns, lane levels and derived drum_key_lane are not changed.
 * This record defines no flash addresses, UI or concurrent publication policy.
 * Callers must publish decoded state using the normal track revision protocol. */
#define SOUND_RECORD_VERSION 1u
#define SOUND_RECORD_BYTES 336u

typedef struct { br_sound_t sound; br_mod_config_t mod; br_fx_config_t fx; } sound_record_state;
/* Return 0 on success, -1 for malformed/out-of-range data or null inputs.
 * Decode never changes output on failure. Encode never changes output on failure. */
int sound_record_encode(uint8_t out[SOUND_RECORD_BYTES], const sound_record_state *state);
int sound_record_decode(sound_record_state *out, const uint8_t *record, uint32_t size);
/* Capture and apply only the fields owned by the sound record. */
void sound_record_capture(sound_record_state *out, const volatile br_part_t *part);
/* Rejects a kit on a synth track or a synth on the kit track. */
int sound_record_apply(volatile br_part_t *part, uint32_t part_index, const sound_record_state *state);
#endif
