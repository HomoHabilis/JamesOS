/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_EXAMPLE_SONG_IMPORT_H
#define FM1_EXAMPLE_SONG_IMPORT_H
/* Host-only importer for the experimental pattern bank. It never writes flash,
 * installs a project, starts playback or edits active firmware state. */
#include "pattern_bank.h"
#include <stddef.h>
#include <stdint.h>

#define FM1_EXAMPLE_SONG_COUNT 2u
/* Returns the descriptive name for a populated slot, or NULL. Pattern names
 * are not yet stored by the experimental bank. */
const char *fm1_example_pattern_name(unsigned example, unsigned slot);
typedef struct {
    const char *title, *description;
    uint32_t tempo, swing, preset[BR_PARTS];
} fm1_example_settings;

/* Build into a NEW isolated staging bank and caller-owned disjoint buffers.
 * On any error discard all staging outputs; the input/current project must not
 * share any buffer. On success the bank has slot 0 live, six defined patterns,
 * and a valid stopped song. Settings must be applied by a future owner after
 * project validation; this function does not edit live globals or persist.
 * Memory/capacity failure is BR_BANK_FULL, invalid arguments BR_BANK_INVALID. */
int fm1_example_song_import(unsigned example, br_pattern_bank *staging,
    uint8_t *arena, size_t arena_capacity, br_pattern_snapshot *live,
    br_pattern_snapshot *scratch, uint8_t *encoded, size_t encoded_capacity,
    fm1_pattern_song *song, fm1_example_settings *settings);
#endif
