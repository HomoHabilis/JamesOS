/* Pattern state and song arrangement engine: pure data, no flash/audio/globals.
 * Contexts and the transport hand-off are described in pattern_arranger.h. */
#include "pattern_song.h"
#include <string.h>

void fm1_pattern_song_init(fm1_pattern_song *s)
{
    memset(s, 0, sizeof *s);
    s->used = 1u;
    s->queued = FM1_PATTERN_NONE;
}

/* A new project starts with one bar of its first pattern. Decoders use the
 * empty initializer so older projects retain their original arrangements. */
void fm1_pattern_song_new(fm1_pattern_song *s, unsigned bars)
{
    fm1_pattern_song_init(s);
    s->song_length = 1u;
    s->entry[0].bars = (uint8_t)(bars >= 1u && bars <= 4u ? bars : 1u);
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

static int changed(fm1_pattern_song *s) { ++s->changes; return FM1_ARR_OK; }

int fm1_arr_loop_of(const fm1_pattern_song *s, unsigned block)
{
    for (unsigned i = 0; i < s->loop_count; ++i)
        if (block >= s->loop[i].first && block <= s->loop[i].last) return (int)i;
    return -1;
}

static void loop_drop(fm1_pattern_song *s, unsigned i)
{
    int here = fm1_arr_loop_of(s, s->song_position) == (int)i;
    memmove(&s->loop[i], &s->loop[i + 1u], (s->loop_count - 1u - i) * sizeof s->loop[0]);
    memset(&s->loop[--s->loop_count], 0, sizeof s->loop[0]);
    if (here) s->pass = 0u;
}

int fm1_arr_insert(fm1_pattern_song *s, unsigned at, unsigned pattern, unsigned bars)
{
    uint8_t n = s->song_length;
    if (n >= FM1_SONG_ENTRIES) return FM1_ARR_FULL;
    if (at > n || pattern >= FM1_PATTERN_COUNT || !bars || bars > FM1_SONG_MAX_BARS) return FM1_ARR_RANGE;
    memmove(&s->entry[at + 1u], &s->entry[at], (n - at) * sizeof s->entry[0]);
    s->entry[at].pattern = pattern; s->entry[at].bars = bars;
    s->song_length = (uint8_t)(n + 1u);
    /* At a loop's first position the block goes before the loop; strictly
     * inside (up to before its last block) it joins the loop. */
    for (unsigned i = 0; i < s->loop_count; ++i) {
        if (at <= s->loop[i].first) ++s->loop[i].first;
        if (at <= s->loop[i].last) ++s->loop[i].last;
    }
    /* Playhead stays on its block. A playhead past the end (pre, STOP end)
     * enters a block appended there. */
    if (at < s->song_position || (at == s->song_position && at < n)) ++s->song_position;
    return changed(s);
}

int fm1_arr_remove(fm1_pattern_song *s, unsigned at)
{
    uint8_t n = s->song_length;
    int l, keep_pass;
    if (!n) return FM1_ARR_EMPTY;
    if (at >= n) return FM1_ARR_RANGE;
    l = fm1_arr_loop_of(s, at);
    keep_pass = l >= 0 && s->loop[l].last != at;
    memmove(&s->entry[at], &s->entry[at + 1u], (n - 1u - at) * sizeof s->entry[0]);
    s->entry[n - 1u].pattern = 0u; s->entry[n - 1u].bars = 0u;
    s->song_length = --n;
    if (at == s->song_position) {
        /* The playing block is gone: finish this bar, then enter the next
         * surviving block (block 0 with a LOOP end, or end of song). */
        s->bars_elapsed = 0u;
        if (!keep_pass) s->pass = 0u;
        if (!s->song_enabled) { if (at >= n) s->song_position = n ? (uint8_t)(n - 1u) : 0u; }
        else {
            s->pre = 1u;
            if (at >= n && s->end == FM1_SONG_END_LOOP) s->song_position = 0u;
        }
    } else if (at < s->song_position) --s->song_position;
    for (unsigned i = s->loop_count; i-- > 0;) {
        fm1_song_loop *p = &s->loop[i];
        if (at < p->first) { --p->first; --p->last; }
        else if (at <= p->last) {
            if (p->first == p->last) loop_drop(s, i);
            else --p->last;
        }
    }
    return changed(s);
}

int fm1_arr_set_pattern(fm1_pattern_song *s, unsigned at, unsigned pattern)
{
    if (at >= s->song_length || pattern >= FM1_PATTERN_COUNT) return s->song_length ? FM1_ARR_RANGE : FM1_ARR_EMPTY;
    s->entry[at].pattern = pattern;
    return changed(s);
}

int fm1_arr_set_bars(fm1_pattern_song *s, unsigned at, unsigned bars)
{
    if (at >= s->song_length || !bars || bars > FM1_SONG_MAX_BARS) return s->song_length ? FM1_ARR_RANGE : FM1_ARR_EMPTY;
    s->entry[at].bars = bars;
    /* Shortened under the playhead: it is now in the block's last bar. */
    if (at == s->song_position && !s->pre && s->bars_elapsed >= bars) s->bars_elapsed = (uint8_t)(bars - 1u);
    return changed(s);
}

int fm1_arr_clear(fm1_pattern_song *s)
{
    memset(s->entry, 0, sizeof s->entry);
    memset(s->loop, 0, sizeof s->loop);
    s->song_length = s->loop_count = 0u;
    s->song_enabled = s->song_position = s->bars_elapsed = s->pass = s->pre = 0u;
    return changed(s);
}

int fm1_arr_loop_set(fm1_pattern_song *s, unsigned first, unsigned last, unsigned repeats)
{
    unsigned i, at;
    if (!s->song_length) return FM1_ARR_EMPTY;
    if (first > last || last >= s->song_length || repeats > FM1_SONG_MAX_REPEATS) return FM1_ARR_RANGE;
    for (i = 0; i < s->loop_count; ++i)
        if (s->loop[i].first == first && s->loop[i].last == last) return fm1_arr_loop_repeats(s, (uint8_t)i, repeats);
    if (repeats < FM1_SONG_MIN_REPEATS) return FM1_ARR_RANGE;
    for (i = 0, at = 0; i < s->loop_count; ++i) {
        if (first <= s->loop[i].last && last >= s->loop[i].first) return FM1_ARR_OVERLAP;
        if (s->loop[i].last < first) at = i + 1u;
    }
    if (s->loop_count >= FM1_SONG_LOOPS) return FM1_ARR_FULL;
    memmove(&s->loop[at + 1u], &s->loop[at], (s->loop_count - at) * sizeof s->loop[0]);
    s->loop[at].first = first; s->loop[at].last = last; s->loop[at].repeats = repeats;
    ++s->loop_count;
    if (s->song_position >= first && s->song_position <= last) s->pass = 0u;
    return changed(s);
}

int fm1_arr_loop_repeats(fm1_pattern_song *s, unsigned index, unsigned repeats)
{
    if (index >= s->loop_count) return FM1_ARR_EMPTY;
    if (repeats > FM1_SONG_MAX_REPEATS) return FM1_ARR_RANGE;
    if (repeats < FM1_SONG_MIN_REPEATS) loop_drop(s, index);
    else s->loop[index].repeats = repeats;  /* a pass already past it leaves at the loop's end */
    return changed(s);
}

int fm1_arr_loop_remove(fm1_pattern_song *s, unsigned index)
{
    return fm1_arr_loop_repeats(s, index, 0u);
}

int fm1_arr_set_mode(fm1_pattern_song *s, unsigned mode)
{
    if (mode > FM1_SONG_MODE_SONG) return FM1_ARR_RANGE;
    s->mode = mode;
    return changed(s);
}

int fm1_arr_set_end(fm1_pattern_song *s, unsigned end)
{
    if (end > FM1_SONG_END_LOOP) return FM1_ARR_RANGE;
    s->end = end;
    return changed(s);
}

uint32_t fm1_arr_total_bars(const fm1_pattern_song *s)
{
    uint32_t total = 0u;
    for (unsigned i = 0; i < s->song_length; ++i) {
        int l = fm1_arr_loop_of(s, (uint8_t)i);
        total += (uint32_t)s->entry[i].bars * (l < 0 ? 1u : s->loop[l].repeats);
    }
    return total;
}

int fm1_arr_song_mode(const fm1_pattern_song *s)
{
    return s->mode == FM1_SONG_MODE_SONG && s->song_length;
}

int fm1_arr_begin(fm1_pattern_song *s, unsigned block)
{
    if (block >= s->song_length) return s->song_length ? FM1_ARR_RANGE : FM1_ARR_EMPTY;
    s->song_enabled = 1u; s->song_position = block;
    s->bars_elapsed = s->pass = s->pre = 0u;
    return changed(s);
}

void fm1_arr_halt(fm1_pattern_song *s)
{
    s->song_enabled = s->pre = 0u;
    ++s->changes;
}

/* Block entered when the playing one (or the pre-roll) finishes, and its loop
 * pass; 255 at the end of a STOP song. */
static uint8_t follow(const fm1_pattern_song *s, uint8_t *pass)
{
    uint8_t pos = s->song_position;
    int l;
    *pass = 0u;
    if (!s->pre) {
        l = fm1_arr_loop_of(s, pos);
        if (l >= 0 && s->loop[l].last == pos && s->pass + 1u < s->loop[l].repeats) {
            *pass = (uint8_t)(s->pass + 1u);
            return s->loop[l].first;
        }
        if (l >= 0 && s->loop[l].last != pos) *pass = s->pass;
        ++pos;
    } else *pass = s->pass;
    if (pos < s->song_length) return pos;
    *pass = 0u;
    return s->end == FM1_SONG_END_LOOP && s->song_length ? 0u : 255u;
}

int fm1_arr_bar(fm1_pattern_song *s)
{
    uint8_t next, pass;
    if (!s->song_enabled) return 0;
    if (!s->pre && s->song_position < s->song_length &&
        ++s->bars_elapsed < s->entry[s->song_position].bars) { ++s->changes; return 1; }
    next = follow(s, &pass);
    s->pre = 0u;
    if (next == 255u) {
        if (s->song_length) {
            if (s->song_position >= s->song_length) s->song_position = (uint8_t)(s->song_length - 1u);
            s->bars_elapsed = (uint8_t)(s->entry[s->song_position].bars - 1u);
        }
        s->song_enabled = 0u; ++s->changes;
        return 0;
    }
    s->song_position = next; s->pass = pass; s->bars_elapsed = 0u; ++s->changes;
    return 1;
}

uint8_t fm1_arr_next_pattern(const fm1_pattern_song *s)
{
    uint8_t pos = s->song_position, pass;
    if (!s->song_length) return FM1_PATTERN_NONE;
    if (!s->pre && pos < s->song_length && s->bars_elapsed + 1u < s->entry[pos].bars)
        return s->entry[pos].pattern;
    pos = follow(s, &pass);
    return pos == 255u ? FM1_PATTERN_NONE : s->entry[pos].pattern;
}

void fm1_arr_pack(const fm1_pattern_song *s, uint8_t out[FM1_SONG_RECORD_BYTES])
{
    memset(out, 0, FM1_SONG_RECORD_BYTES);
    out[0] = s->song_length;
    out[1] = (uint8_t)(s->mode | s->end << 1);
    out[2] = s->loop_count;
    memcpy(out + 4, s->loop, s->loop_count * sizeof s->loop[0]);
    for (unsigned i = 0; i < s->song_length; ++i)
        out[16u + i] = (uint8_t)(s->entry[i].pattern | (s->entry[i].bars - 1u) << 4);
}

int fm1_arr_unpack(fm1_pattern_song *s, const uint8_t in[FM1_SONG_RECORD_BYTES])
{
    /* Rebuild through the editing rules (range, overlap, limits), then demand
     * the canonical bytes back: unused bytes zero, loops sorted, flags known. */
    fm1_pattern_song t = *s;
    uint8_t again[FM1_SONG_RECORD_BYTES];
    unsigned i, n = in[0];
    fm1_arr_clear(&t);
    t.mode = in[1] & 1u; t.end = (in[1] >> 1) & 1u;
    for (i = 0; i < n && i < FM1_SONG_ENTRIES; ++i)
        if (fm1_arr_insert(&t, i, in[16u + i] & 15u, (in[16u + i] >> 4) + 1u) != FM1_ARR_OK) return 0;
    for (i = 0; i < in[2] && i < FM1_SONG_LOOPS; ++i) {
        const uint8_t *p = in + 4u + i * 3u;
        if (p[2] < FM1_SONG_MIN_REPEATS || fm1_arr_loop_set(&t, p[0], p[1], p[2]) != FM1_ARR_OK) return 0;
    }
    fm1_arr_pack(&t, again);
    if (memcmp(again, in, FM1_SONG_RECORD_BYTES)) return 0;
    t.song_position = 0u;
    *s = t;
    return 1;
}
