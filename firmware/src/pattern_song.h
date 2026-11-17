/* Pattern state and the song arrangement engine. Pure data model: this module
 * never touches flash, audio, globals or the pattern bank. The screen-facing
 * contract (contexts, playback commands) is documented in pattern_arranger.h. */
#ifndef FM1_PATTERN_SONG_H
#define FM1_PATTERN_SONG_H
#include <stdint.h>

#define FM1_PATTERN_COUNT 11u
#define FM1_SONG_ENTRIES 64u     /* blocks in an arrangement */
#define FM1_SONG_MAX_BARS 16u    /* bars per block, 1..16 */
#define FM1_SONG_LOOPS 4u        /* loop regions, not nested, not overlapping */
#define FM1_SONG_MIN_REPEATS 2u
#define FM1_SONG_MAX_REPEATS 16u
#define FM1_PATTERN_NONE 255u
/* Serialized arrangement (project envelope v6, after the session record):
 *   0 block count 0..64 | 1 flags: bit0 SONG mode, bit1 LOOP at end | 2 loop count 0..4 | 3 zero
 *   4..15  four loops {first, last, repeats}, sorted, unused zero
 *   16..79 64 blocks, pattern | (bars-1) << 4, unused zero
 * A record that breaks any rule is rejected whole (strict canonical form). */
#define FM1_SONG_RECORD_BYTES 80u

enum { FM1_SONG_MODE_PATTERN = 0, FM1_SONG_MODE_SONG = 1 };
enum { FM1_SONG_END_STOP = 0, FM1_SONG_END_LOOP = 1 };
/* Result of every arrangement command; the screen turns it into a message. */
enum {
    FM1_ARR_OK = 0,
    FM1_ARR_FULL,     /* 64 blocks or 4 loops already */
    FM1_ARR_RANGE,    /* index, pattern, bars or repeats out of range */
    FM1_ARR_OVERLAP,  /* loop would overlap another loop */
    FM1_ARR_EMPTY,    /* no blocks (or no such loop) */
    FM1_ARR_MODE,     /* playback command while in PATTERN mode */
    FM1_ARR_BUSY      /* playback command refused: already playing / start pending */
};

typedef struct { uint8_t pattern, bars; } fm1_song_entry;
typedef struct { uint8_t first, last, repeats; } fm1_song_loop;
typedef struct {
    fm1_song_entry entry[FM1_SONG_ENTRIES];   /* entry[0..song_length) are the blocks */
    uint16_t used;                            /* pattern slots holding data */
    uint8_t active, queued;                   /* owned by the pattern owner/runtime */
    /* Playhead (foreground only, never saved): song_enabled = the song is
     * driving the transport; song_position = block; bars_elapsed = bars of
     * that block already finished; pass = finished passes of the loop holding
     * the block; pre = the remainder of the current bar still belongs to a
     * removed block, so the next bar boundary enters song_position at bar 0. */
    uint8_t song_enabled, song_position, song_length, bars_elapsed;
    uint8_t pass, pre;
    uint8_t mode, end, loop_count, changes;   /* changes: wraps; bumped on every visible change */
    fm1_song_loop loop[FM1_SONG_LOOPS];       /* loop[0..loop_count), sorted by first */
} fm1_pattern_song;

/* The owner must copy all pattern data before returning success. This is
 * deliberately outside the transport so copying never occurs in the ISR. */
typedef int (*fm1_pattern_copy_fn)(void *context, uint8_t source, uint8_t destination);
void fm1_pattern_song_init(fm1_pattern_song *s);
void fm1_pattern_song_new(fm1_pattern_song *s, unsigned bars);
/* Panel-only model of a pattern tap (host panel test). */
int fm1_pattern_select(fm1_pattern_song *s, uint8_t target, int playing,
                       fm1_pattern_copy_fn copy, void *context);

/* ---- Arrangement edits: O(64), no heap. Return FM1_ARR_*. ---- */
int fm1_arr_insert(fm1_pattern_song *s, unsigned at, unsigned pattern, unsigned bars); /* at 0..count */
int fm1_arr_remove(fm1_pattern_song *s, unsigned at);
int fm1_arr_set_pattern(fm1_pattern_song *s, unsigned at, unsigned pattern);
int fm1_arr_set_bars(fm1_pattern_song *s, unsigned at, unsigned bars);
int fm1_arr_clear(fm1_pattern_song *s);
/* Same range: changes its repeats. Repeats below 2 remove the loop there. */
int fm1_arr_loop_set(fm1_pattern_song *s, unsigned first, unsigned last, unsigned repeats);
int fm1_arr_loop_repeats(fm1_pattern_song *s, unsigned index, unsigned repeats);
int fm1_arr_loop_remove(fm1_pattern_song *s, unsigned index);
int fm1_arr_set_mode(fm1_pattern_song *s, unsigned mode);
int fm1_arr_set_end(fm1_pattern_song *s, unsigned end);

/* ---- Queries ---- */
/* Loop index (0..loop_count-1) holding block, or -1. */
int fm1_arr_loop_of(const fm1_pattern_song *s, unsigned block);
/* Bars the arrangement plays once through, loops counted with their repeats. */
uint32_t fm1_arr_total_bars(const fm1_pattern_song *s);
/* SONG mode with at least one block: the song owns the pattern while playing. */
int fm1_arr_song_mode(const fm1_pattern_song *s);

/* ---- Playhead (driven by the transport glue, foreground only) ---- */
int fm1_arr_begin(fm1_pattern_song *s, unsigned block);   /* arm at block, bar 0, pass 0 */
void fm1_arr_halt(fm1_pattern_song *s);                  /* transport stopped */
/* One bar boundary. Returns 0 when the song has ended (STOP end); the playhead
 * then stays on the last block. */
int fm1_arr_bar(fm1_pattern_song *s);
/* Pattern the song wants playing after the next bar boundary (FM1_PATTERN_NONE
 * when nothing follows). */
uint8_t fm1_arr_next_pattern(const fm1_pattern_song *s);

/* ---- Persistence ---- */
void fm1_arr_pack(const fm1_pattern_song *s, uint8_t out[FM1_SONG_RECORD_BYTES]);
/* Returns 1 and replaces blocks, loops, mode and end (playhead cleared) only if
 * the record is canonical; otherwise returns 0 and leaves *s unchanged. */
int fm1_arr_unpack(fm1_pattern_song *s, const uint8_t in[FM1_SONG_RECORD_BYTES]);
#endif
