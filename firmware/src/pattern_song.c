/* Pure transport model; integration and project persistence are separate. */
#include "pattern_song.h"
#include <string.h>

void fm1_pattern_song_init(fm1_pattern_song *s)
{
    memset(s, 0, sizeof *s);
    s->used = 1u;
    s->queued = FM1_PATTERN_NONE;
    s->song_length = 1u;
    for (unsigned i = 0; i < FM1_SONG_ENTRIES; ++i) {
        s->entry[i].pattern = FM1_PATTERN_NONE;
        s->entry[i].bars = 1u;
    }
}

int fm1_pattern_select(fm1_pattern_song *s, uint8_t target, int playing,
                       fm1_pattern_copy_fn copy, void *context)
{
    if (!s || target >= FM1_PATTERN_COUNT || s->active >= FM1_PATTERN_COUNT) return 0;
    if (!(s->used & (1u << target))) {
        if (!copy || !copy(context, s->active, target)) return 0;
        s->used |= (uint16_t)(1u << target);
    }
    if (playing) s->queued = target;
    else { s->active = target; s->queued = FM1_PATTERN_NONE; s->song_enabled = 0u; }
    return 1;
}

int fm1_song_entry_set(fm1_pattern_song *s, uint8_t position, uint8_t pattern, uint8_t bars)
{
    if (!s || position >= FM1_SONG_ENTRIES || pattern >= FM1_PATTERN_COUNT ||
        !(s->used & (1u << pattern)) || !bars || bars > FM1_SONG_MAX_BARS) return 0;
    s->entry[position].pattern = pattern;
    s->entry[position].bars = bars;
    return 1;
}

int fm1_song_length_set(fm1_pattern_song *s, uint8_t length)
{
    if (!s || !length || length > FM1_SONG_ENTRIES) return 0;
    s->song_length = length;
    return 1;
}

int fm1_song_start(fm1_pattern_song *s)
{
    if (!s || !s->song_length || s->song_length > FM1_SONG_ENTRIES) return 0;
    /* Do not enter a song with holes or references to missing patterns. */
    for (unsigned i = 0; i < s->song_length; ++i) {
        uint8_t p = s->entry[i].pattern, bars = s->entry[i].bars;
        if (p >= FM1_PATTERN_COUNT || !(s->used & (1u << p)) ||
            !bars || bars > FM1_SONG_MAX_BARS) return 0;
    }
    s->song_enabled = 1u;
    s->song_position = s->bars_elapsed = 0u;
    s->active = s->entry[0].pattern;
    s->queued = FM1_PATTERN_NONE;
    return 1;
}

int fm1_pattern_song_bar(fm1_pattern_song *s)
{
    uint8_t position, pattern;
    if (!s) return 0;
    if (s->queued < FM1_PATTERN_COUNT) {
        s->active = s->queued;
        s->queued = FM1_PATTERN_NONE;
        s->song_enabled = 0u;
        s->bars_elapsed = 0u;
        return 1;
    }
    if (!s->song_enabled || !s->song_length || s->song_length > FM1_SONG_ENTRIES ||
        s->song_position >= FM1_SONG_ENTRIES) return 0;
    if (++s->bars_elapsed < s->entry[s->song_position].bars) return 0;
    position = s->song_position + 1u >= s->song_length ? 0u :
        (uint8_t)(s->song_position + 1u);
    pattern = s->entry[position].pattern;
    if (pattern >= FM1_PATTERN_COUNT || !(s->used & (1u << pattern)) ||
        !s->entry[position].bars || s->entry[position].bars > FM1_SONG_MAX_BARS) {
        s->song_enabled = 0u;
        return 0;
    }
    s->bars_elapsed = 0u;
    s->song_position = position;
    s->active = pattern;
    return 1;
}
