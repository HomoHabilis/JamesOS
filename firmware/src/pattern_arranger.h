/* SPDX-License-Identifier: GPL-3.0-only */
/* Pattern arranger: the interface a screen uses (FM-1 and SMK-37 Pro alike).
 *
 * STATE. The arrangement lives in br_ps_state (fm1_pattern_song, pattern_song.h),
 * the same object that holds the 11-pattern state; pass &br_ps_state to every
 * fm1_arr_* call. It is saved and loaded with the project (envelope v6, see
 * pattern_project.c) - blocks, loops, mode and song end; the playhead is not.
 *
 * QUERIES (read the struct directly; never write it):
 *   blocks       s->song_length (0..64); block i = s->entry[i].pattern (0..10), .bars (1..16)
 *   loops        s->loop_count (0..4); s->loop[i] = {first, last, repeats 2..16}, sorted by first;
 *                fm1_arr_loop_of(s, block) -> loop index or -1
 *   mode / end   s->mode (FM1_SONG_MODE_PATTERN / _SONG), s->end (FM1_SONG_END_STOP / _LOOP)
 *   playhead     s->song_enabled (song driving the transport), s->song_position (block),
 *                s->bars_elapsed (bar within block, 0-based), s->pass (finished passes of
 *                the playhead's loop, 0-based), s->pre (finishing a removed block's bar)
 *   total bars   fm1_arr_total_bars(s)
 *   redraw       s->changes - a wrapping counter bumped on every edit and every playhead
 *                move; redraw when it differs from the value seen at the last draw
 *                (fm1_arranger_changes() below is the same value).
 *   The cursor is the screen's own state; the engine never reads or moves it.
 *
 * COMMANDS (return FM1_ARR_OK or FM1_ARR_FULL / _RANGE / _OVERLAP / _EMPTY / _MODE / _BUSY):
 *   fm1_arr_insert(s, at, pattern, bars)   at = 0..count; FULL at 64 blocks
 *   fm1_arr_remove(s, at), fm1_arr_set_pattern(s, at, p), fm1_arr_set_bars(s, at, bars),
 *   fm1_arr_clear(s)
 *   fm1_arr_loop_set(s, first, last, repeats)  new loop, or new repeats for the same range;
 *                                               OVERLAP / FULL (4) / RANGE
 *   fm1_arr_loop_repeats(s, index, repeats)    repeats < 2 removes the loop
 *   fm1_arr_loop_remove(s, index)
 *   fm1_arr_set_mode(s, mode), fm1_arr_set_end(s, end)
 *   br_arranger_play(block)                    internal start at a given block; MODE in PATTERN
 *                                               mode, BUSY while playing
 *   The panel PLAY toggle starts an arrangement at block 0; an empty arrangement cannot play.
 *
 * LOOPS UNDER EDITS: insert at a loop's first block goes before the loop; strictly inside
 * (first < at <= last) joins it; removing a loop's only block removes the loop.
 *
 * EDITS DURING PLAY: the playhead stays on the same block (index shifts with inserts and
 * removes before it). Removing the playing block: the current bar finishes (the old pattern
 * keeps sounding to the bar line), then the next surviving block starts at bar 0 (block 0
 * with a LOOP end; the song ends with a STOP end). Shortening the playing block below the
 * bars already played makes the current bar its last. A changed pattern or loop takes effect
 * at the next bar line where it applies, except that the next block's pattern is decoded and
 * queued at the start of the bar before it: a pattern change to that block during that bar
 * applies from the block's next play. Clearing all, or switching to PATTERN mode, hands the
 * running transport back to PATTERN behaviour (a switch already queued still lands). Mode
 * SONG chosen while playing in PATTERN mode applies at the next start.
 * Two blocks in a row with the same pattern do not restart it at the block line (no switch
 * happens); a pattern longer than its block keeps running across that line.
 *
 * CONTEXTS. Everything here is foreground (main loop, CPU0): the screen's commands and the
 * playhead updates run in the same loop, so they never interleave. The audio interrupt never
 * reads the arrangement. It only counts bar lines (br_pattern_irq_bar_count) and swaps to a
 * pattern the foreground already decoded into the publication slot - the same mailbox a
 * black-key pattern tap uses (br_ps_runtime_queue, queued under the foreground claim). In
 * SONG mode the foreground queues the next block's pattern during the last bar of a block,
 * so the swap lands on that block's first bar line; while the song plays, black-key taps are
 * refused ("SONG PLAYING"). Starting on a block whose pattern is not active first selects it
 * while stopped (the ordinary stopped selection), then starts the transport.
 * Never call these from an interrupt or with interrupts masked (except where the glue does).
 */
#ifndef FM1_PATTERN_ARRANGER_H
#define FM1_PATTERN_ARRANGER_H
#include "pattern_song.h"
int br_arranger_play(unsigned block);
static inline uint8_t fm1_arranger_changes(const fm1_pattern_song *s) { return s->changes; }
#endif
