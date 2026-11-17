/* SPDX-License-Identifier: GPL-3.0-only */
/* Arranger transport glue, included by dinghy.c when the pattern owner runtime
 * is built (BR_ARRANGER). Foreground only; see pattern_arranger.h for the
 * contract. The audio interrupt never reads the arrangement: it counts bar
 * lines and swaps to a pattern this code queued through br_ps_runtime_queue. */
#include "pattern_arranger.h"
#if defined(__clang__)
#define BR_ARR_SIZE __attribute__((minsize))
#else
#define BR_ARR_SIZE
#endif

static uint32_t br_arr_seen_bars;            /* bar lines already applied to the playhead */
static uint8_t br_arr_was_playing, br_arr_start, br_arr_selecting;  /* start: block + 1 */

#if BR_ARR_UI
static uint8_t br_chain_pattern_bars(void)
{
    uint32_t ticks = 1u;
    for (unsigned p = 0; p < BR_PARTS; ++p) {
        uint32_t div = br_part[p].division > 2u ? 2u : br_part[p].division;
        uint32_t track = br_part[p].length * (16u >> div);
        if (track > ticks) ticks = track;
    }
    return (uint8_t)((ticks + 127u) / 128u);
}
/* Chain changes use the same preloaded bar-boundary handoff as the arranger. */
static void br_chain_tick(void)
{
    uint32_t now, length;
    unsigned next;
    if (br_ps_state.mode != FM1_SONG_MODE_PATTERN) return;
    if (br_chain.single_pending) {
        if (!br_playing) { br_chain.single_pending = 0u; return; }
        if (br_ps_state.queued != FM1_PATTERN_NONE) return;
        next = br_chain.single_pending - 1u;
        if (next == br_ps_state.active || br_ps_runtime_queue(next)) br_chain.single_pending = 0u;
        return;
    }
    if (!br_chain.count) return;
    if (!br_playing) { br_chain.armed = 1u; return; }
    if (br_chain.armed == 1u) {
        if (br_ps_state.queued != FM1_PATTERN_NONE) return;
        if (br_ps_state.active != br_chain.pattern[0]) {
            (void)br_ps_runtime_queue(br_chain.pattern[0]);
            return;
        }
        br_chain.position = br_chain.pass = br_chain.elapsed = 0u;
        br_chain.span = 0u;
        br_chain.bars = br_pattern_irq_bar_count();
        br_chain.armed = 2u;
    }
    if (br_chain.armed != 2u) return;
    if (!br_chain.span && br_ps_state.active == br_chain.pattern[br_chain.position]) {
        br_chain.span = br_chain_pattern_bars();
    }
    length = br_chain.span ? br_chain.span : 1u;
    now = br_pattern_irq_bar_count();
    if (now != br_chain.bars) {
        ++br_chain.bars;
        if (++br_chain.elapsed >= length) {
            br_chain.elapsed = 0u;
            if (++br_chain.pass >= br_chain.repeats[br_chain.position]) {
                br_chain.pass = 0u;
                br_chain.position = (uint8_t)((br_chain.position + 1u) % br_chain.count);
                br_chain.span = 0u;
                ++br_chain.revision;
            }
        }
    }
    if (br_ps_state.active != br_chain.pattern[br_chain.position]) return;
    if (!br_chain.span) {
        br_chain.span = br_chain_pattern_bars();
    }
    length = br_chain.span;
    next = (br_chain.position + 1u) % br_chain.count;
    if (br_chain.elapsed + 1u >= length && br_chain.pass + 1u >= br_chain.repeats[br_chain.position] &&
        br_chain.pattern[next] != br_ps_state.active && br_ps_state.queued == FM1_PATTERN_NONE)
        (void)br_ps_runtime_queue(br_chain.pattern[next]);
}
#endif

BR_ARR_SIZE int br_arranger_play(unsigned block)
{
    fm1_pattern_song *s = &br_ps_state;
    if (!fm1_arr_song_mode(s)) return s->mode == FM1_SONG_MODE_SONG ? FM1_ARR_EMPTY : FM1_ARR_MODE;
    if (block >= s->song_length) return FM1_ARR_RANGE;
    if (br_playing || br_arr_start) return FM1_ARR_BUSY;
    br_arr_start = (uint8_t)(block + 1u); br_arr_selecting = 0u;
    return FM1_ARR_OK;
}

/* Transport edges. masked = called at the end of a foreground mutation with
 * the audio interrupt still masked, so a PLAY that needs a different pattern
 * can be withdrawn before audio renders a single block of the wrong one.
 * Unmasked callers (MIDI start, a stop from a fault) only arm and disarm. */
BR_ARR_SIZE static void br_arranger_edge(int masked)
{
    fm1_pattern_song *s = &br_ps_state;
    if (br_playing && !br_arr_was_playing && s->mode == FM1_SONG_MODE_SONG && !s->song_length) {
        br_playing = 0u;
        br_arr_start = br_arr_selecting = 0u;
        br_message("ARRANGEMENT EMPTY");
    }
    if (br_playing && !br_arr_was_playing && fm1_arr_song_mode(s)) {
        unsigned block = br_arr_start ? br_arr_start - 1u : 0u;
        if (block >= s->song_length) block = 0u;
        if (masked && !br_arr_selecting && s->entry[block].pattern != s->active) {
            br_playing = 0u; br_arr_start = (uint8_t)(block + 1u);
            return;
        }
        (void)fm1_arr_begin(s, block);
        br_arr_seen_bars = br_pattern_irq_bar_count(); br_arr_start = 0u;
    }
    if (!br_playing && br_arr_was_playing && s->song_enabled) fm1_arr_halt(s);
    br_arr_was_playing = (uint8_t)br_playing;
}

#if defined(BR_ARRANGER_LINK_ALL) && BR_ARRANGER_LINK_ALL
/* Size measurement only (--define BR_ARRANGER_LINK_ALL=1): link every screen
 * command, as a screen that uses them all will. */
static const void *volatile br_arranger_link_all[] = {
    (const void *)fm1_arr_insert, (const void *)fm1_arr_remove, (const void *)fm1_arr_set_pattern,
    (const void *)fm1_arr_set_bars, (const void *)fm1_arr_clear, (const void *)fm1_arr_loop_set,
    (const void *)fm1_arr_loop_repeats, (const void *)fm1_arr_loop_remove, (const void *)fm1_arr_set_mode,
    (const void *)fm1_arr_set_end, (const void *)fm1_arr_total_bars, (const void *)br_arranger_play
};
#endif
/* Each input pass, with the audio foreground claim held: bar lines cannot be
 * consumed between reading the count and queueing the next pattern. */
BR_ARR_SIZE static void br_arranger_tick(void)
{
    fm1_pattern_song *s = &br_ps_state;
    uint32_t bars;
    uint8_t want;
#if defined(BR_ARRANGER_LINK_ALL) && BR_ARRANGER_LINK_ALL
    (void)br_arranger_link_all[fm1_ms & 7u];
#endif
    br_arranger_edge(0);
#if BR_ARR_UI
    br_chain_tick();
#endif
    if (!br_playing) {
        unsigned block = br_arr_start - 1u;
        if (!br_arr_start) return;
        if (!fm1_arr_song_mode(s) || block >= s->song_length) { br_arr_start = 0u; return; }
        if (s->entry[block].pattern != s->active && !br_arr_selecting) {
            /* The ordinary stopped selection (br_ps_service, later this pass). */
            if (br_ps_enqueue(BR_PS_SELECT, s->entry[block].pattern, 0u, 0u)) br_arr_selecting = 1u;
            return;
        }
        if (br_ps_request.op) return;
        /* Selected (or the selection failed: start anyway, the song queues
         * the block's pattern for the next bar line). */
        BR_FOREGROUND_MUTATE(br_playing = 1u);  /* its end runs br_arranger_edge(1) */
        br_arr_selecting = 0u; br_ui.redraw = 1u;
        return;
    }
    br_arr_start = 0u;
    if (!s->song_enabled) return;
    if (!fm1_arr_song_mode(s)) { fm1_arr_halt(s); return; }
    bars = br_pattern_irq_bar_count();
    while (br_arr_seen_bars != bars) {
        ++br_arr_seen_bars;
        if (!fm1_arr_bar(s)) {
            BR_FOREGROUND_MUTATE(br_playing = 0u);
            br_message("SONG END");
            return;
        }
    }
    want = fm1_arr_next_pattern(s);
    if (want < FM1_PATTERN_COUNT && want != s->active && s->queued == FM1_PATTERN_NONE)
        (void)br_ps_runtime_queue(want);
}
