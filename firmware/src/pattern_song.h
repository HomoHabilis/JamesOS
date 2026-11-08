/* Pattern/song transport decisions. This module does not access flash or audio. */
#ifndef FM1_PATTERN_SONG_H
#define FM1_PATTERN_SONG_H
#include <stdint.h>

#define FM1_PATTERN_COUNT 11u
#define FM1_SONG_ENTRIES 64u
#define FM1_SONG_MAX_BARS 16u
#define FM1_PATTERN_NONE 255u

typedef struct { uint8_t pattern, bars; } fm1_song_entry;
typedef struct {
    fm1_song_entry entry[FM1_SONG_ENTRIES];
    uint16_t used;
    uint8_t active, queued;
    uint8_t song_enabled, song_position, song_length, bars_elapsed;
} fm1_pattern_song;

/* The owner must copy all pattern data before returning success. This is
 * deliberately outside the transport so copying never occurs in the ISR. */
typedef int (*fm1_pattern_copy_fn)(void *context, uint8_t source, uint8_t destination);
void fm1_pattern_song_init(fm1_pattern_song *s);
int fm1_pattern_select(fm1_pattern_song *s, uint8_t target, int playing,
                       fm1_pattern_copy_fn copy, void *context);
int fm1_song_entry_set(fm1_pattern_song *s, uint8_t position, uint8_t pattern, uint8_t bars);
int fm1_song_length_set(fm1_pattern_song *s, uint8_t length);
int fm1_song_start(fm1_pattern_song *s);
/* Call exactly once on each global bar boundary; return 1 on a change. */
int fm1_pattern_song_bar(fm1_pattern_song *s);
#endif
