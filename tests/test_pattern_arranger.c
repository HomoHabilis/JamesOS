/* Song arrangement engine as a pure module (firmware/src/pattern_song.c): edits,
 * loops under insert/remove, play order with loops and both song ends,
 * start-from-block, edits during play, limits, record pack/unpack. */
#include "pattern_song.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static fm1_pattern_song s;

/* Play from block until the song ends or max bars; record the block of every bar. */
static unsigned play(uint8_t from, unsigned max, uint8_t *blocks, uint8_t *pass)
{
    unsigned n = 0;
    assert(fm1_arr_begin(&s, from) == FM1_ARR_OK);
    for (;;) {
        if (n == max) break;
        blocks[n] = s.song_position; if (pass) pass[n] = s.pass; n++;
        if (!fm1_arr_bar(&s)) break;
    }
    return n;
}
static void expect(const uint8_t *got, unsigned n, const char *want)
{
    char text[512]; unsigned i, at = 0;
    for (i = 0; i < n; ++i) at += (unsigned)snprintf(text + at, sizeof text - at, "%s%u", i ? " " : "", got[i]);
    text[at] = 0;
    if (strcmp(text, want)) { fprintf(stderr, "play order\n got  %s\n want %s\n", text, want); assert(0); }
}
static void build(const uint8_t *pattern, const uint8_t *bars, unsigned n)
{
    fm1_pattern_song_init(&s);
    for (unsigned i = 0; i < n; ++i) assert(fm1_arr_insert(&s, (uint8_t)i, pattern[i], bars[i]) == FM1_ARR_OK);
}

static void test_edits(void)
{
    uint8_t before;
    fm1_pattern_song_new(&s, 4u);
    assert(s.song_length == 1u && s.entry[0].pattern == 0u && s.entry[0].bars == 4u);
    fm1_pattern_song_init(&s);
    assert(!s.song_length && !s.loop_count && s.mode == FM1_SONG_MODE_PATTERN && s.end == FM1_SONG_END_STOP);
    assert(!fm1_arr_song_mode(&s));
    assert(fm1_arr_remove(&s, 0) == FM1_ARR_EMPTY);
    assert(fm1_arr_set_pattern(&s, 0, 1) == FM1_ARR_EMPTY);
    assert(fm1_arr_loop_set(&s, 0, 0, 2) == FM1_ARR_EMPTY);
    assert(fm1_arr_insert(&s, 1, 0, 1) == FM1_ARR_RANGE);
    assert(fm1_arr_insert(&s, 0, 11, 1) == FM1_ARR_RANGE);
    assert(fm1_arr_insert(&s, 0, 0, 0) == FM1_ARR_RANGE);
    assert(fm1_arr_insert(&s, 0, 0, 17) == FM1_ARR_RANGE);
    before = s.changes;
    assert(fm1_arr_insert(&s, 0, 3, 4) == FM1_ARR_OK && s.changes != before);
    assert(fm1_arr_insert(&s, 0, 1, 2) == FM1_ARR_OK);     /* front */
    assert(fm1_arr_insert(&s, 2, 10, 16) == FM1_ARR_OK);   /* end */
    assert(fm1_arr_insert(&s, 1, 2, 1) == FM1_ARR_OK);     /* middle */
    assert(s.song_length == 4);
    assert(s.entry[0].pattern == 1 && s.entry[1].pattern == 2 && s.entry[2].pattern == 3 && s.entry[3].pattern == 10);
    assert(s.entry[3].bars == 16 && fm1_arr_total_bars(&s) == 2 + 1 + 4 + 16);
    assert(fm1_arr_set_pattern(&s, 4, 1) == FM1_ARR_RANGE && fm1_arr_set_pattern(&s, 0, 11) == FM1_ARR_RANGE);
    assert(fm1_arr_set_pattern(&s, 0, 7) == FM1_ARR_OK && s.entry[0].pattern == 7);
    assert(fm1_arr_set_bars(&s, 0, 0) == FM1_ARR_RANGE && fm1_arr_set_bars(&s, 0, 17) == FM1_ARR_RANGE);
    assert(fm1_arr_set_bars(&s, 0, 9) == FM1_ARR_OK && s.entry[0].bars == 9);
    assert(fm1_arr_remove(&s, 4) == FM1_ARR_RANGE);
    assert(fm1_arr_remove(&s, 1) == FM1_ARR_OK && s.song_length == 3 && s.entry[1].pattern == 3);
    assert(!s.entry[3].pattern && !s.entry[3].bars);        /* unused tail stays canonical */
    assert(fm1_arr_set_mode(&s, 2) == FM1_ARR_RANGE && fm1_arr_set_end(&s, 2) == FM1_ARR_RANGE);
    assert(fm1_arr_set_mode(&s, FM1_SONG_MODE_SONG) == FM1_ARR_OK && fm1_arr_song_mode(&s));
    assert(fm1_arr_set_end(&s, FM1_SONG_END_LOOP) == FM1_ARR_OK && s.end == FM1_SONG_END_LOOP);
    assert(fm1_arr_clear(&s) == FM1_ARR_OK && !s.song_length && !fm1_arr_song_mode(&s) && s.mode == FM1_SONG_MODE_SONG);
    /* Full */
    for (unsigned i = 0; i < FM1_SONG_ENTRIES; ++i) assert(fm1_arr_insert(&s, (uint8_t)i, (uint8_t)(i % 11), (uint8_t)(i % 16 + 1)) == FM1_ARR_OK);
    assert(fm1_arr_insert(&s, 0, 0, 1) == FM1_ARR_FULL && fm1_arr_insert(&s, 64, 0, 1) == FM1_ARR_FULL);
    assert(s.song_length == 64 && s.entry[63].pattern == 63 % 11 && s.entry[63].bars == 16);
    assert(fm1_arr_remove(&s, 0) == FM1_ARR_OK && s.song_length == 63 && s.entry[62].pattern == 63 % 11);
    puts("edits ok");
}

static void test_loops(void)
{
    static const uint8_t p[8] = {0,1,2,3,4,5,6,7}, b[8] = {1,1,1,1,1,1,1,1};
    build(p, b, 8);
    assert(fm1_arr_loop_set(&s, 2, 1, 2) == FM1_ARR_RANGE);
    assert(fm1_arr_loop_set(&s, 2, 8, 2) == FM1_ARR_RANGE);
    assert(fm1_arr_loop_set(&s, 2, 3, 1) == FM1_ARR_RANGE);
    assert(fm1_arr_loop_set(&s, 2, 3, 17) == FM1_ARR_RANGE);
    assert(fm1_arr_loop_set(&s, 4, 5, 3) == FM1_ARR_OK);
    assert(fm1_arr_loop_set(&s, 1, 2, 2) == FM1_ARR_OK);          /* sorted in front */
    assert(s.loop_count == 2 && s.loop[0].first == 1 && s.loop[1].first == 4);
    assert(fm1_arr_loop_set(&s, 2, 4, 2) == FM1_ARR_OVERLAP);
    assert(fm1_arr_loop_set(&s, 5, 6, 2) == FM1_ARR_OVERLAP);
    assert(fm1_arr_loop_set(&s, 0, 7, 2) == FM1_ARR_OVERLAP);     /* no nesting either */
    assert(fm1_arr_loop_set(&s, 4, 5, 5) == FM1_ARR_OK && s.loop_count == 2 && s.loop[1].repeats == 5); /* same range */
    assert(fm1_arr_loop_set(&s, 0, 0, 2) == FM1_ARR_OK && fm1_arr_loop_set(&s, 7, 7, 16) == FM1_ARR_OK);
    assert(fm1_arr_loop_set(&s, 3, 3, 2) == FM1_ARR_FULL);
    assert(fm1_arr_loop_of(&s, 0) == 0 && fm1_arr_loop_of(&s, 2) == 1 && fm1_arr_loop_of(&s, 3) == -1 && fm1_arr_loop_of(&s, 7) == 3);
    assert(fm1_arr_total_bars(&s) == 2 + 2*2 + 1 + 2*5 + 1 + 16);
    assert(fm1_arr_loop_repeats(&s, 4, 3) == FM1_ARR_EMPTY && fm1_arr_loop_repeats(&s, 0, 17) == FM1_ARR_RANGE);
    assert(fm1_arr_loop_repeats(&s, 3, 1) == FM1_ARR_OK && s.loop_count == 3);   /* below 2 removes */
    assert(fm1_arr_loop_set(&s, 0, 0, 0) == FM1_ARR_OK && s.loop_count == 2);    /* same range, 0 removes */
    assert(fm1_arr_loop_remove(&s, 0) == FM1_ARR_OK && s.loop_count == 1 && s.loop[0].first == 4);
    assert(!s.loop[1].first && !s.loop[1].repeats);
    /* Loops follow their blocks: loop [4,5]. */
    assert(fm1_arr_insert(&s, 4, 9, 1) == FM1_ARR_OK && s.loop[0].first == 5 && s.loop[0].last == 6);  /* at first: before */
    assert(fm1_arr_insert(&s, 6, 9, 1) == FM1_ARR_OK && s.loop[0].first == 5 && s.loop[0].last == 7);  /* strictly inside: joins */
    assert(fm1_arr_insert(&s, 8, 9, 1) == FM1_ARR_OK && s.loop[0].first == 5 && s.loop[0].last == 7);  /* after last: outside */
    assert(fm1_arr_insert(&s, 0, 9, 1) == FM1_ARR_OK && s.loop[0].first == 6 && s.loop[0].last == 8);
    assert(fm1_arr_remove(&s, 0) == FM1_ARR_OK && s.loop[0].first == 5 && s.loop[0].last == 7);
    assert(fm1_arr_remove(&s, 7) == FM1_ARR_OK && s.loop[0].first == 5 && s.loop[0].last == 6);        /* last block */
    assert(fm1_arr_remove(&s, 5) == FM1_ARR_OK && s.loop[0].first == 5 && s.loop[0].last == 5);        /* first block */
    assert(fm1_arr_remove(&s, 5) == FM1_ARR_OK && !s.loop_count);                                      /* loses all its blocks */
    /* Inserting into a full arrangement leaves loops alone. */
    fm1_pattern_song_init(&s);
    for (unsigned i = 0; i < 64; ++i) assert(fm1_arr_insert(&s, (uint8_t)i, 0, 1) == FM1_ARR_OK);
    assert(fm1_arr_loop_set(&s, 60, 63, 2) == FM1_ARR_OK && fm1_arr_insert(&s, 0, 0, 1) == FM1_ARR_FULL && s.loop[0].first == 60);
    assert(fm1_arr_clear(&s) == FM1_ARR_OK && !s.loop_count);
    puts("loops ok");
}

static void test_play(void)
{
    static const uint8_t p[5] = {0,1,2,3,4}, b[5] = {2,1,1,1,3};
    uint8_t got[200], pass[200];
    unsigned n;
    build(p, b, 5);
    s.mode = FM1_SONG_MODE_SONG;
    n = play(0, 200, got, 0); expect(got, n, "0 0 1 2 3 4 4 4");
    assert(!s.song_enabled && s.song_position == 4 && s.bars_elapsed == 2);  /* ended on the last bar */
    assert(fm1_arr_bar(&s) == 0);
    n = play(3, 200, got, 0); expect(got, n, "3 4 4 4");                     /* start from block */
    assert(fm1_arr_begin(&s, 5) == FM1_ARR_RANGE);
    assert(fm1_arr_loop_set(&s, 1, 2, 3) == FM1_ARR_OK && fm1_arr_loop_set(&s, 4, 4, 2) == FM1_ARR_OK);
    n = play(0, 200, got, pass); expect(got, n, "0 0 1 2 1 2 1 2 3 4 4 4 4 4 4");
    expect(pass, n, "0 0 0 0 1 1 2 2 0 0 0 0 1 1 1");
    assert(fm1_arr_total_bars(&s) == n);
    n = play(2, 200, got, pass); expect(got, n, "2 1 2 1 2 3 4 4 4 4 4 4"); /* starting inside a loop: pass 0 */
    /* LOOP end: back to block 0 (loops start over). */
    s.end = FM1_SONG_END_LOOP;
    n = play(3, 24, got, 0); expect(got, n, "3 4 4 4 4 4 4 0 0 1 2 1 2 1 2 3 4 4 4 4 4 4 0 0");
    /* Next pattern: the one wanted after the coming bar line. */
    assert(fm1_arr_begin(&s, 0) == FM1_ARR_OK && fm1_arr_next_pattern(&s) == 0);
    assert(fm1_arr_bar(&s) && fm1_arr_next_pattern(&s) == 1);
    assert(fm1_arr_bar(&s) && s.song_position == 1 && fm1_arr_next_pattern(&s) == 2);
    assert(fm1_arr_bar(&s) && fm1_arr_next_pattern(&s) == 1);    /* loop back */
    s.end = FM1_SONG_END_STOP;
    assert(fm1_arr_begin(&s, 4) == FM1_ARR_OK);
    for (int i = 0; i < 5; ++i) assert(fm1_arr_bar(&s));
    assert(fm1_arr_next_pattern(&s) == FM1_PATTERN_NONE);         /* nothing after a STOP end */
    /* One block of one bar, LOOP end, plays forever on the same block. */
    fm1_pattern_song_init(&s);
    assert(fm1_arr_insert(&s, 0, 5, 1) == FM1_ARR_OK); s.end = FM1_SONG_END_LOOP;
    n = play(0, 4, got, 0); expect(got, n, "0 0 0 0");
    assert(fm1_arr_next_pattern(&s) == 5);
    /* Max-size song with the repeats limit: total bars and a full run agree. */
    fm1_pattern_song_init(&s);
    for (unsigned i = 0; i < 64; ++i) assert(fm1_arr_insert(&s, (uint8_t)i, (uint8_t)(i % 11), 16) == FM1_ARR_OK);
    for (unsigned i = 0; i < 4; ++i) assert(fm1_arr_loop_set(&s, (uint8_t)(i * 16), (uint8_t)(i * 16 + 15), 16) == FM1_ARR_OK);
    assert(fm1_arr_total_bars(&s) == 64u * 16u * 16u);
    {
        unsigned bars = 1; assert(fm1_arr_begin(&s, 0) == FM1_ARR_OK);
        while (fm1_arr_bar(&s)) ++bars;
        assert(bars == 64u * 16u * 16u);
    }
    puts("play order ok");
}

static void test_play_edits(void)
{
    static const uint8_t p[6] = {0,1,2,3,4,5}, b[6] = {2,2,2,2,2,2};
    uint8_t got[64]; unsigned n;
    build(p, b, 6); s.mode = FM1_SONG_MODE_SONG;
    /* Insert before the playhead: it stays on its block. */
    assert(fm1_arr_begin(&s, 2) == FM1_ARR_OK && fm1_arr_bar(&s) && s.bars_elapsed == 1);
    assert(fm1_arr_insert(&s, 0, 9, 1) == FM1_ARR_OK && s.song_position == 3 && s.bars_elapsed == 1 && s.entry[3].pattern == 2);
    assert(fm1_arr_insert(&s, 3, 8, 1) == FM1_ARR_OK && s.song_position == 4 && s.entry[4].pattern == 2);  /* at the playhead: before it */
    assert(fm1_arr_insert(&s, 5, 7, 1) == FM1_ARR_OK && s.song_position == 4);                            /* after: no move */
    /* Remove before the playhead. */
    assert(fm1_arr_remove(&s, 0) == FM1_ARR_OK && s.song_position == 3 && s.entry[3].pattern == 2 && s.bars_elapsed == 1);
    /* Remove after it. */
    assert(fm1_arr_remove(&s, 4) == FM1_ARR_OK && s.song_position == 3);
    /* Blocks now 0:1 1:8? -> rebuild for clarity. */
    build(p, b, 6); s.mode = FM1_SONG_MODE_SONG;
    assert(fm1_arr_begin(&s, 2) == FM1_ARR_OK);
    /* Remove the playing block: this bar finishes, then block 3 (now index 2) starts at bar 0. */
    assert(fm1_arr_remove(&s, 2) == FM1_ARR_OK && s.pre && s.song_position == 2 && s.entry[2].pattern == 3);
    assert(fm1_arr_next_pattern(&s) == 3);
    assert(fm1_arr_bar(&s) && !s.pre && s.song_position == 2 && s.bars_elapsed == 0);
    assert(fm1_arr_bar(&s) && s.song_position == 2 && s.bars_elapsed == 1);
    assert(fm1_arr_bar(&s) && s.song_position == 3 && s.entry[3].pattern == 4);
    /* Remove the playing last block with a STOP end: the song ends at the bar line. */
    assert(fm1_arr_begin(&s, 4) == FM1_ARR_OK && fm1_arr_remove(&s, 4) == FM1_ARR_OK && s.pre);
    assert(fm1_arr_next_pattern(&s) == FM1_PATTERN_NONE);
    assert(fm1_arr_bar(&s) == 0 && !s.song_enabled && s.song_position == 3);
    /* ...with a LOOP end it wraps to block 0. */
    s.end = FM1_SONG_END_LOOP;
    assert(fm1_arr_begin(&s, 3) == FM1_ARR_OK && fm1_arr_remove(&s, 3) == FM1_ARR_OK && s.pre && s.song_position == 0);
    assert(fm1_arr_next_pattern(&s) == 0 && fm1_arr_bar(&s) && s.song_position == 0 && !s.pre);
    /* Appending while a STOP song finishes a removed last block plays the new block. */
    build(p, b, 3); s.mode = FM1_SONG_MODE_SONG;
    assert(fm1_arr_begin(&s, 2) == FM1_ARR_OK && fm1_arr_remove(&s, 2) == FM1_ARR_OK && s.song_position == 2);
    assert(fm1_arr_insert(&s, 2, 6, 1) == FM1_ARR_OK && s.song_position == 2 && fm1_arr_next_pattern(&s) == 6);
    assert(fm1_arr_bar(&s) && s.song_position == 2);
    /* Shortening the playing block: the current bar becomes its last. */
    build(p, b, 6); s.mode = FM1_SONG_MODE_SONG;
    assert(fm1_arr_set_bars(&s, 1, 8) == FM1_ARR_OK && fm1_arr_begin(&s, 1) == FM1_ARR_OK);
    for (int i = 0; i < 5; ++i) assert(fm1_arr_bar(&s));
    assert(s.bars_elapsed == 5 && fm1_arr_set_bars(&s, 1, 3) == FM1_ARR_OK && s.bars_elapsed == 2);
    assert(fm1_arr_next_pattern(&s) == 2 && fm1_arr_bar(&s) && s.song_position == 2);
    /* Loop edits under the playhead. */
    build(p, b, 6); s.mode = FM1_SONG_MODE_SONG;
    assert(fm1_arr_loop_set(&s, 1, 2, 4) == FM1_ARR_OK && fm1_arr_begin(&s, 1) == FM1_ARR_OK);
    for (int i = 0; i < 4; ++i) assert(fm1_arr_bar(&s));
    assert(s.song_position == 1 && s.pass == 1);
    assert(fm1_arr_loop_repeats(&s, 0, 2) == FM1_ARR_OK);           /* already on the last pass */
    for (int i = 0; i < 4; ++i) assert(fm1_arr_bar(&s));
    assert(s.song_position == 3 && s.pass == 0);
    assert(fm1_arr_begin(&s, 1) == FM1_ARR_OK);
    for (int i = 0; i < 4; ++i) assert(fm1_arr_bar(&s));
    assert(s.pass == 1 && fm1_arr_loop_remove(&s, 0) == FM1_ARR_OK && s.pass == 0);
    /* Removing a loop block other than its last keeps the pass. */
    assert(fm1_arr_loop_set(&s, 1, 3, 3) == FM1_ARR_OK && fm1_arr_begin(&s, 1) == FM1_ARR_OK);
    for (int i = 0; i < 6; ++i) assert(fm1_arr_bar(&s));
    assert(s.song_position == 1 && s.pass == 1 && fm1_arr_remove(&s, 1) == FM1_ARR_OK && s.pass == 1);
    n = 0; while (n < 64 && fm1_arr_bar(&s)) got[n++] = s.song_position;
    expect(got, n, "1 1 2 2 1 1 2 2 3 3 4 4");
    /* Clearing all while playing hands back to PATTERN behaviour. */
    assert(fm1_arr_begin(&s, 0) == FM1_ARR_OK && fm1_arr_clear(&s) == FM1_ARR_OK && !s.song_enabled && !fm1_arr_song_mode(&s));
    assert(fm1_arr_next_pattern(&s) == FM1_PATTERN_NONE);
    /* Edits while stopped never leave the playhead out of range. */
    build(p, b, 3); assert(fm1_arr_begin(&s, 2) == FM1_ARR_OK); fm1_arr_halt(&s);
    assert(fm1_arr_remove(&s, 2) == FM1_ARR_OK && !s.pre && s.song_position == 1);
    puts("edits during play ok");
}

static void test_record(void)
{
    static const uint8_t p[5] = {10,1,0,3,4}, b[5] = {16,1,1,8,3};
    uint8_t rec[FM1_SONG_RECORD_BYTES], bad[FM1_SONG_RECORD_BYTES];
    fm1_pattern_song t;
    build(p, b, 5);
    assert(fm1_arr_loop_set(&s, 1, 2, 16) == FM1_ARR_OK && fm1_arr_loop_set(&s, 4, 4, 2) == FM1_ARR_OK);
    s.mode = FM1_SONG_MODE_SONG; s.end = FM1_SONG_END_LOOP;
    fm1_arr_pack(&s, rec);
    assert(rec[0] == 5 && rec[1] == 3 && rec[2] == 2 && !rec[3] && rec[4] == 1 && rec[5] == 2 && rec[6] == 16);
    assert(rec[16] == (10 | 15 << 4) && rec[19] == (3 | 7 << 4));
    for (unsigned i = 21; i < FM1_SONG_RECORD_BYTES; ++i) assert(!rec[i]);
    fm1_pattern_song_init(&t); t.used = 0x123; t.active = 2;
    assert(fm1_arr_begin(&s, 1) == FM1_ARR_OK);
    assert(fm1_arr_unpack(&t, rec) && t.used == 0x123 && t.active == 2);
    assert(t.song_length == 5 && t.loop_count == 2 && t.mode == FM1_SONG_MODE_SONG && t.end == FM1_SONG_END_LOOP);
    assert(!memcmp(t.entry, s.entry, sizeof t.entry) && !memcmp(t.loop, s.loop, sizeof t.loop));
    assert(!t.song_enabled && !t.song_position);
    /* Empty arrangement is all zero. */
    fm1_pattern_song_init(&t); fm1_arr_pack(&t, bad);
    for (unsigned i = 0; i < FM1_SONG_RECORD_BYTES; ++i) assert(!bad[i]);
    assert(fm1_arr_unpack(&t, bad) && !t.song_length);
    /* Every non-canonical record is refused and changes nothing. */
    {
        struct { unsigned at; uint8_t value; } cases[] = {
            {0, 65}, {1, 4}, {2, 5}, {3, 1}, {16, 11}, {21, 1}, {79, 0x10},
            {4, 3}, {5, 5}, {6, 1}, {6, 17}, {7, 1}, {10, 1}, {15, 2}
        };
        for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            fm1_pattern_song before;
            memcpy(bad, rec, sizeof bad); bad[cases[c].at] = cases[c].value;
            fm1_pattern_song_init(&t); before = t;
            if (fm1_arr_unpack(&t, bad)) { fprintf(stderr, "accepted bad byte %u = %u\n", cases[c].at, cases[c].value); assert(0); }
            assert(!memcmp(&t, &before, sizeof t));
        }
        /* Overlapping or unsorted loops. */
        memcpy(bad, rec, sizeof bad); bad[7] = 2; bad[8] = 4; bad[9] = 2;  /* second loop [2,4] overlaps [1,2] */
        assert(!fm1_arr_unpack(&t, bad));
        memcpy(bad, rec, sizeof bad); bad[4] = 4; bad[5] = 4; bad[7] = 1; bad[8] = 2; /* unsorted */
        assert(!fm1_arr_unpack(&t, bad));
    }
    puts("record ok");
}

int main(void)
{
    test_edits();
    test_loops();
    test_play();
    test_play_edits();
    test_record();
    puts("pattern arranger engine: all checks passed");
    return 0;
}
