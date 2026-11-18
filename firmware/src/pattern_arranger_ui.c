/* SPDX-License-Identifier: GPL-3.0-only */
/* Pattern and arrangement performance share this page (OCT- + SEQ). Knobs 1 and 2 edit the
 * arrangement cursor and bars, knob 3 sets whether the arrangement loops at the end, and knob 4
 * selects PATTERN or ARRANGE performance.
 * In ARRANGE, OCT- plus knob 1 selects blocks, and OCT- plus REC removes the cursor block. */
#if BR_ARR_UI_PART == 1
#include "pattern_arranger.h"
#define BR_ARR_VIEW (br_ps_state.mode == FM1_SONG_MODE_SONG)
static uint8_t br_arr_start; /* the transport's pending arrangement start */
/* All of the screen's state, in one object of its own section on the device: the compiler's merging of
 * small globals would otherwise move other variables (the SMK-37 key scanner's, in RAM code) and change
 * that code's size. */
#if defined(FM1_DEVICE) && defined(__clang__)
#define BR_ARR_STATE __attribute__((section(".bss.br_arr_ui")))
#else
#define BR_ARR_STATE
#endif
static struct {
    uint8_t cursor, anchor, sel, bars;   /* cursor: even gap, odd block; bars: last used, 0 = 4 */
    uint16_t ws;                         /* first ribbon bar on screen */
    uint8_t px, next;                    /* ribbon pixels a bar; the ribbon's next band (part 2) */
    uint32_t old[12];                    /* region hashes (part 2) */
    char text[16];                       /* a knob's value text */
} br_arr BR_ARR_STATE;
#define br_arr_text br_arr.text

/* A live pattern chain is performance state, separate from the saved song. */
#define BR_CHAIN_MAX 16u
static struct {
    uint32_t held, bars;
    uint8_t pattern[BR_CHAIN_MAX], repeats[BR_CHAIN_MAX];
    uint8_t draft_pattern[BR_CHAIN_MAX], draft_repeats[BR_CHAIN_MAX];
    uint8_t count, draft_count, position, pass, elapsed, span, armed, gesture, revision, single_pending;
} br_chain BR_ARR_STATE;

static void br_chain_down(uint32_t key, uint32_t pattern)
{
    uint8_t n;
    if (pattern >= FM1_PATTERN_COUNT || key >= 27u) return;
    if (!br_chain.held) { br_chain.draft_count = 0u; br_chain.gesture = 0u; }
    br_chain.held |= 1u << key;
    n = br_chain.draft_count;
    unsigned i;
    for (i = 0; i < n && br_chain.draft_pattern[i] != pattern; ++i) {}
    if (i < n && br_chain.draft_repeats[i] < 99u)
        ++br_chain.draft_repeats[i];
    else if (i == n && n < BR_CHAIN_MAX) {
        br_chain.draft_pattern[n] = (uint8_t)pattern;
        br_chain.draft_repeats[n] = 1u;
        ++br_chain.draft_count;
    } else br_message("CHAIN FULL");
    if (br_chain.draft_count > 1u || (i < n && br_chain.draft_repeats[i] > 1u)) {
        br_chain.gesture = 1u;
        br_ui.ps_held_used = 1u;
    }
    ++br_chain.revision;
}

static uint32_t br_chain_up(uint32_t key)
{
    if (key >= 27u || !(br_chain.held & (1u << key))) return 0u;
    br_chain.held &= ~(1u << key);
    if (br_chain.gesture) {
        if (!br_chain.held) {
            br_chain.count = br_chain.draft_count;
            for (unsigned i = 0; i < br_chain.count; ++i) {
                br_chain.pattern[i] = br_chain.draft_pattern[i];
                br_chain.repeats[i] = br_chain.draft_repeats[i];
            }
            br_chain.armed = 1u; br_chain.position = 0u; br_chain.pass = 0u;
            br_chain.single_pending = 0u;
            if (!br_playing && !br_ps_select(br_chain.pattern[0])) br_message("PATTERN FAILED");
        }
        ++br_chain.revision;
        return 1u;
    }
    if (!br_chain.held && !br_ui.ps_held_used) {
        uint32_t defer = br_chain.count && br_playing && br_ps_state.queued != FM1_PATTERN_NONE;
        br_chain.count = br_chain.armed = 0u;
        ++br_chain.revision;
        if (defer) {
            br_chain.single_pending = (uint8_t)(br_chain.draft_pattern[0] + 1u);
            return 1u;
        }
    }
    return 0u;
}

/* out = f with each '%' replaced by the next number of v (fixed formats: the caller's buffer fits). */
static char *br_arr_fmt(char *out, const char *f, const int32_t *v)
{
    char *o = out;
    while (*f) {
        if (*f != '%') *o++ = *f;
        else { br_number(o, *v++); while (*o) ++o; }
        ++f;
    }
    *o = 0;
    return out;
}
static unsigned br_arr_at(void)
{
    unsigned end = 2u * br_ps_state.song_length;
    if (br_arr.cursor > end) br_arr.cursor = (uint8_t)end;
    return br_arr.cursor / 2u;
}
static unsigned br_arr_gap(void) { (void)br_arr_at(); return !(br_arr.cursor & 1u); }
static unsigned br_arr_new_bars(void) { return br_arr.bars ? br_arr.bars : 4u; }
/* The bar count the BARS knob shows: the cursor block's, or the new block's. */
static unsigned br_arr_bars_shown(void)
{
    unsigned at = br_arr_at();
    return !br_arr_gap() && at < br_ps_state.song_length ? br_ps_state.entry[at].bars : br_arr_new_bars();
}
/* The selected blocks a..b, when there is a selection. */
static unsigned br_arr_range(unsigned *a, unsigned *b)
{
    unsigned n = br_ps_state.song_length, c = br_arr_at(), x = br_arr.anchor;
    if (!br_arr.sel || !n) return 0u;
    *a = x < c ? x : c; *b = x < c ? c : x;
    if (*b >= n) *b = n - 1u;
    if (*a > *b) *a = *b;
    return 1u;
}
/* The refusals the knobs and keys can meet (the others cannot happen from this screen). */
static void br_arr_result(int r)
{
    if (r == FM1_ARR_FULL) br_message("SONG FULL: 64");
    else if (r == FM1_ARR_OVERLAP) br_message("LOOPS OVERLAP");
}
/* br_control for the page: label and text of knob k (no text: no knob there). */
static void br_arr_control(uint32_t k, br_control_t *c)
{
    const fm1_pattern_song *s = &br_ps_state;
    unsigned n = s->song_length, at = br_arr_at();
    int32_t v[2];
    v[0] = (int32_t)at + 1; v[1] = (int32_t)n;
    c->text = br_arr_text;
    if (k == 3u) {
        c->label = "PERFORM";
        c->text = BR_ARR_VIEW ? "ARRANGE" : "PATTERN";
    } else if (k == 2u) {
        c->label = "LOOP MODE";
        c->text = BR_ARR_VIEW ? (s->end ? "ON" : "OFF") : 0;
    } else if (!BR_ARR_VIEW) {
        c->label = k ? "BARS" : "CURSOR";
        c->text = 0;
    } else if (!k) {
        c->label = "CURSOR";
        if (at >= n) c->text = n ? "END" : "-";
        else if (br_arr_gap()) { v[0] = (int32_t)at + 1; br_arr_fmt(br_arr_text, "GAP %", v); }
        else br_arr_fmt(br_arr_text, "%/%", v);
    } else {
        c->label = !br_arr_gap() && at < n ? "BARS" : "NEW BARS";
        v[0] = (int32_t)br_arr_bars_shown();
        br_arr_fmt(br_arr_text, v[0] == 1 ? "% BAR" : "% BARS", v);
    }
}
static void br_arr_turn(uint32_t k, int32_t turn)
{
    fm1_pattern_song *s = &br_ps_state;
    unsigned n = s->song_length, at = br_arr_at(), on = !br_arr_gap() && at < n;
    int v;
    if (k == 3u) {
        unsigned mode = turn > 0 ? FM1_SONG_MODE_SONG : FM1_SONG_MODE_PATTERN;
        if (mode != s->mode && (br_playing || br_arr_start)) {
            br_message("STOP TO CHANGE MODE");
            return;
        }
        if (mode != s->mode) {
            fm1_arr_set_mode(s, mode); br_arr.sel = 0u;
            br_chain.count = br_chain.held = br_chain.single_pending = 0u;
            ++br_chain.revision;
            br_message(mode == FM1_SONG_MODE_SONG ? "NOW IN ARRANGER MODE" : "NOW IN PATTERN MODE");
        }
    } else if (k == 2u) {
        if (!BR_ARR_VIEW) return;
        fm1_arr_set_end(s, turn > 0);
    } else if (!BR_ARR_VIEW) return;
    br_ui.edit_key = BR_PAGE_PS * 256u + BR_ARR_VIEW * 16u + br_ui.part * 4u + k;
    br_ui.edit_time = fm1_ms; br_ui.redraw = 1u;
    if (k >= 2u) return;
    if (!k) {
        if (!(fm1_in.buttons & (1u << BTN_OCT_DOWN))) {
            br_arr.sel = 0u;
            br_arr.cursor = (uint8_t)br_clip((int32_t)br_arr.cursor + turn, 0, (int32_t)(2u * n));
        } else if (n) {
            int32_t block = (int32_t)at;
            if (block >= (int32_t)n) block = (int32_t)n - 1;
            if (!br_arr.sel) { br_arr.sel = 1u; br_arr.anchor = (uint8_t)block; }
            block = br_clip(block + turn, 0, (int32_t)n - 1);
            br_arr.cursor = (uint8_t)(2 * block + 1);
        }
        return;
    }
    v = br_clip((int32_t)(on ? s->entry[at].bars : br_arr_new_bars()) + turn, 1, FM1_SONG_MAX_BARS);
    br_arr.bars = (uint8_t)v;
    if (on) fm1_arr_set_bars(s, at, (unsigned)v);
}
/* A black key (pattern place) on the page: 1 when ARRANGE in SONG mode took it. */
static uint32_t br_arr_key(uint32_t place)
{
    fm1_pattern_song *s = &br_ps_state;
    unsigned at;
    int r;
    if (!BR_ARR_VIEW || place >= FM1_PATTERN_COUNT) return 0u;
    if (s->mode != FM1_SONG_MODE_SONG) { br_message("SONG MODE RECORDS"); return 0u; }
    at = br_arr_at();
    if (!br_arr_gap() && at < s->song_length) {
        r = fm1_arr_set_pattern(s, at, place);
    } else {
        r = fm1_arr_insert(s, at, place, br_arr_new_bars());
        if (r == FM1_ARR_OK) br_arr.cursor = (uint8_t)(2u * (at + 1u));
    }
    if (r == FM1_ARR_OK) br_arr.sel = 0u;
    br_arr_result(r);
    return 1u;
}
/* REC pressed: OCT- held on ARRANGE removes the cursor block (1: REC is used). */
static uint32_t br_arr_rec(void)
{
    unsigned at;
    if (br_ui.page != BR_PAGE_PS || !BR_ARR_VIEW || !(fm1_in.buttons & (1u << BTN_OCT_DOWN))) return 0u;
    at = br_arr_at();
    br_arr.sel = 0u;
    if (br_arr_gap() || at >= br_ps_state.song_length) br_message("NO BLOCK HERE");
    else { fm1_arr_remove(&br_ps_state, at); br_arr.cursor = (uint8_t)(2u * at + (at < br_ps_state.song_length)); }
    br_ui.redraw = 1u;
    return 1u;
}
/* The cue line when no message shows, and what the screen reader says for the OCT- hint. */
static const char *br_arr_hint(void)
{
    if (!BR_ARR_VIEW) return (fm1_ms >> 12) & 1u ?
        "HOLD [PATTERN KEYS] TO CHAIN" : "[PATTERN KEYS] QUEUE NEXT";
    if (br_arr_gap() && br_arr_at() < br_ps_state.song_length) return "[PATTERN KEY] INSERT";
    if (!br_ps_state.song_length) return "[PATTERN KEY] INSERT";
    switch ((fm1_ms >> 12) & 3u) {
    case 0u: return "[PATTERN KEY] CHANGE";
    default: return "[OCT-] + [REC] REMOVE";
    }
}

#else /* ---------------------------------------------------------------- part 2: drawing */

/* The ribbon's scale: a short song fills the 228 px from x 6 (at most 38 px a bar), a long one scrolls
 * at 12 px a bar; set once per frame by br_arr_draw. */
#define BR_ARR_PX ((int32_t)br_arr.px)
#define BR_ARR_SHOWN (228u / br_arr.px)
/* Eleven pattern colours: hue steps with alternating lightness, so neighbours differ two ways. */
static uint16_t br_arr_color(unsigned pattern)
{
    static const uint16_t c[FM1_PATTERN_COUNT] = {
        RGB(240, 64, 56), RGB(248, 150, 40), RGB(240, 216, 56), RGB(140, 212, 48), RGB(32, 176, 88),
        RGB(48, 212, 200), RGB(56, 140, 248), RGB(150, 140, 255), RGB(184, 80, 232), RGB(248, 104, 192),
        RGB(200, 184, 160)};
    return c[pattern % FM1_PATTERN_COUNT];
}
static unsigned br_arr_bar_at(unsigned block)        /* ribbon bar where block starts */
{
    unsigned bars = 0u;
    while (block) bars += br_ps_state.entry[--block].bars;
    return bars;
}
static uint32_t br_arr_hash(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}
static int32_t br_arr_x(unsigned bar, unsigned ws) { return 6 + ((int32_t)bar - (int32_t)ws) * BR_ARR_PX; }
static void br_arr_centre(int32_t cx, int32_t y, const char *t, uint16_t c) { br_widget_text(cx - br_widget_text_width(t) / 2, y, t, c); }
/* The part of x .. x + w on the 240 px screen: its centre, and its width as the return value. */
static int32_t br_arr_seen(int32_t x, int32_t w, int32_t *cx)
{
    int32_t a = x > 0 ? x : 0, b = x + w < 240 ? x + w : 240;
    *cx = (a + b) / 2;
    return b - a;
}
/* A card (a pattern slot, a block, the end slot): fill, edge, and its number ("+" for 0) in ink, centred in
 * the part on screen (a block cut by the edge still shows its pattern), at scale 2 when that part is 24 px
 * wide or more. */
static void br_arr_card(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill, uint16_t edge, int32_t number, uint16_t ink)
{
    char u[12];
    int32_t cx, big = br_arr_seen(x, w, &cx) >= 24 ? 2 : 1;
    hd_rrect(x, y, w, h, 5, fill);
    hd_rrect_frame(x, y, w, h, 5, edge);
    if (number) br_arr_fmt(u, "%", &number); else br_text_copy(u, "+", sizeof u);
    br_widget_text_scaled(cx - br_widget_text_width_scaled(u, big) / 2, y + h / 2 - (big == 2 ? 11 : 4), u, ink, big);
}
/* This frame's facts, shared by the region painters below. */
typedef struct {
    uint32_t sum, force;                 /* sum: the song's content, scale and scroll */
    uint16_t ws, total, pbar, pmark;     /* pmark: playhead bar + 1 while the song plays, else 0 */
    uint8_t n, at, play, sel, sa, sb, gap;
} br_arf_t;
/* Region r (br_region) changed or forced: start its canvas (w x h) and say yes. */
static uint32_t br_arr_open(const br_arf_t *f, unsigned r, uint32_t h, uint32_t w, uint32_t rows)
{
    if (!br_region(&br_arr.old[r], h, f->force)) return 0u;
    cv_begin(w, rows, BR_BACKGROUND);
    return 1u;
}
/* The cue line on ARRANGE (rows 216..239): the message while it shows, otherwise the hint, where "[KEY]"
 * is drawn as a key cap (hdc_key, as the cue bars draw panel buttons); centred. */
static void br_arr_cue(const br_arf_t *f)
{
    uint32_t msg = (int32_t)(br_ui.message_until - fm1_ms) > 0 && br_ui.message[0];
    const char *s = msg ? br_ui.message : br_arr_hint(), *t;
    char part[24];
    int32_t x = 0, w = 0, pass, k, cap;
    if (!br_arr_open(f, 11u, br_hash_text(s) ^ msg, 240, 24)) return;
    for (pass = 0; pass < 2; ++pass) {
        if (pass) x = (240 - w) / 2;
        for (t = s; *t; ) {
            cap = *t == '[';
            t += cap;
            for (k = 0; *t && *t != (cap ? ']' : '[') && k < 23; ) part[k++] = *t++;
            part[k] = 0;
            t += cap && *t;
            uint32_t pattern = cap && (br_header_title_equal(part, "PATTERN KEY") || br_header_title_equal(part, "PATTERN KEYS")), rec = cap && br_header_title_equal(part, "REC");
            uint32_t oct = cap && br_header_title_equal(part, "OCT-");
            int32_t ink = br_widget_text_width(part);
            k = rec ? 22 : oct ? BR_OCT_KEY_W : ink + (pattern ? 12 : 6 * cap);
            if (pass && pattern) {
                hd_rrect(x, 4, k, 16, 8, BR_LINE);
                hd_rrect(x + 1, 5, k - 2, 14, 7, RGB(40, 47, 50));
            } else if (pass && oct) br_oct_key(x, 4, part);
            else if (pass && cap) hdc_key(x, rec ? 1 : 4, k, rec ? 22 : 16);
            if (pass && cap && !oct) hdc_key_text(x + (k - ink) / 2, 8, part, rec ? RGB(240, 72, 64) : C_WHITE);
            else if (pass && !cap) br_widget_text(x + (k - ink) / 2, 8, part, msg ? BR_GLOBAL_ACC : BR_DIM);
            x += k + 2 * cap; w = x;
        }
    }
    cv_blit(0, 216);
}
/* Knob tile k: show a compact empty mark when the control is unavailable. */
static void br_arr_knob(const br_arf_t *f, uint32_t k)
{
    br_control_t c = br_control(k);
    uint32_t active = k == br_active_knob();
    const char *v = c.text ? c.text : "";
    if (!br_arr_open(f, 7u + k, br_hash_text(v) ^ br_hash_text(c.label) * 31u ^ active ^ br_ui_accent << 8, 54, 52)) return;
    if (!c.text) c.label = "";
    br_draw_seq_control(k, &c, v);
    cv_blit(4u + k * 60u, 146u);
}
/* Positions follow the FM-1's black keys: groups of three, two, three, two and one. */
static int32_t br_arr_pattern_x(uint32_t pattern)
{
    static const uint8_t key[FM1_PATTERN_COUNT] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
    return ((int32_t)key[pattern] - 1) * 222 / 24 - (pattern == 5u ? 1 : 0);
}
/* Pattern slots, with no arrangement summary. */
static void br_arr_patterns(const br_arf_t *f)
{
#if !BR_PRODUCT_SMK37
    const fm1_pattern_song *s = &br_ps_state;
    unsigned i;
    /* Clear the former position line after switching from the arrangement view. */
    if (br_arr_open(f, 1u, 0u, 240, 18)) cv_blit(0, 116);
#endif
#if BR_PRODUCT_SMK37
    br_draw_smk_patterns(f->force);
#else
    if (br_arr_open(f, 3u, br_chain.revision, 240, 14)) {
        const uint8_t *patterns = br_chain.held && br_chain.gesture ? br_chain.draft_pattern : br_chain.pattern;
        const uint8_t *repeats = br_chain.held && br_chain.gesture ? br_chain.draft_repeats : br_chain.repeats;
        unsigned count = br_chain.held && br_chain.gesture ? br_chain.draft_count : br_chain.count;
        for (i = 0; i < count; ++i) {
            char label[16];
            int32_t numbers[2] = {(int32_t)i + 1, repeats[i]};
            br_arr_fmt(label, repeats[i] > 1u ? "%x%" : "%", numbers);
            br_arr_centre(br_arr_pattern_x(patterns[i]) + 9, 3, label, BR_DIM);
        }
        cv_blit(0, 44);
    }
    if (!br_arr_open(f, 2u, s->used | s->active << 16 | s->queued << 24 | br_ui_accent * 31u, 240, 36)) return;
    for (i = 0; i < FM1_PATTERN_COUNT; ++i) {
        int32_t x = br_arr_pattern_x(i);
        uint32_t used = (s->used >> i) & 1u, on = i == s->active;
        uint16_t fill = on ? hd_mix(BR_CARD, BR_ACC, 90u) : used ? RGB(40, 46, 50) : RGB(14, 17, 19);
        char label[12];
        int32_t number = (int32_t)i + 1;
        br_arr_fmt(label, "%", &number);
        hd_rrect(x, 2, 18, 32, 8, fill);
        hd_rrect_frame(x, 2, 18, 32, 8, on ? hd_lighten(BR_ACC, 40u) : i == s->queued ? BR_GLOBAL_ACC : fill);
        br_arr_centre(x + 9, 14, label, used || on ? C_WHITE : BR_DIM);
    }
    cv_blit(0, 60);
#endif
}
/* The ribbon (rows 34..87), three bands: over each loop a gold bar tagged "X4" (the pass "2/4" while the
 * playhead is in it); the blocks with pattern and bars, the cursor (white frame), the selection (light frame),
 * the playing block (white top line), the end slot ("+": where a key appends). */
static void br_arr_ribbon(const br_arf_t *f)
{
    const fm1_pattern_song *s = &br_ps_state;
    unsigned i, k, bar, n = f->n, at = f->at;
    char u[16];
    int32_t v[2], l;
    for (i = 0; i < 3u; ++i) {
        if (!br_band(&br_arr.old[4], &br_arr.next, (f->sum ^ at << 20 ^ f->gap << 29) * 31u + f->play + s->pass * 4096u +
                     (f->sel ? f->sa << 8 | f->sb << 16 | 1u << 24 : 0u), i, f->force)) continue;
        cv_begin(240, 18, BR_BACKGROUND);
        cv_oy = -(int32_t)(i * 18u);
        for (l = 0; l < (int32_t)s->loop_count; ++l) {
            const fm1_song_loop *lp = &s->loop[l];
            int32_t x0 = br_arr_x(br_arr_bar_at(lp->first), f->ws);
            v[0] = f->pmark && fm1_arr_loop_of(s, f->play) == l ? s->pass + 1 : 0; v[1] = lp->repeats;
            hd_rrect(x0, 10, br_arr_x(br_arr_bar_at(lp->last + 1u), f->ws) - 2 - x0, 3, 1, BR_GLOBAL_ACC);
            br_widget_text(x0 < 2 ? 2 : x0, 0, br_arr_fmt(u, v[0] ? "%/%" : "X%", v[0] ? v : v + 1), v[0] ? C_WHITE : BR_GLOBAL_ACC);
        }
        cv_oy += 14;
        /* An empty rail remains visible beyond the placed blocks. */
        hd_rrect(0, 1, 240, 38, 3, RGB(17, 20, 21));
        for (k = 0, bar = 0u; k <= n; bar += k < n ? s->entry[k].bars : 0u, ++k) {
            int32_t x = br_arr_x(bar, f->ws), w = k < n ? (int32_t)s->entry[k].bars * BR_ARR_PX - 2 : 24, cx;
            uint16_t pc = k < n ? br_arr_color(s->entry[k].pattern) : RGB(70, 80, 84), ink = pc;
            if (x >= 240 || x + w <= 0) continue;
            if (f->sel && k >= f->sa && k <= f->sb) ink = RGB(200, 208, 212);
            if (k == at && (!f->gap || k == n)) ink = C_WHITE;
            br_arr_card(x, 1, w, 38, k < n ? hd_darken(pc, 165u) : RGB(12, 14, 16), ink, k < n ? s->entry[k].pattern + 1 : 0,
                        k < n ? hd_lighten(pc, 70u) : ink);
            if (k == at && (!f->gap || k == n)) hd_rrect_frame(x + 1, 2, w - 2, 36, 4, C_WHITE);
            if (k < n) {
                hd_rrect(x + 2, 3, w - 4, 3, 1, k == f->play ? C_WHITE : pc);
                v[0] = s->entry[k].bars;
                cx = br_arr_seen(x, w, &cx) >= 50 ? 1 : 0;
                br_arr_fmt(u, cx ? "% BARS" : "%", v);
                if (br_arr_seen(x, w, &cx) >= br_widget_text_width(u) + 4) br_arr_centre(cx, 26, u, RGB(190, 198, 202));
            }
        }
        if (f->gap && at < n) {
            int32_t x = br_arr_x(br_arr_bar_at(at), f->ws) - 1;
            hd_rrect(x - 1, 0, 3, 40, 1, C_WHITE);
            hd_rrect(x - 5, 14, 11, 11, 2, C_BLACK);
            hd_rrect_frame(x - 5, 14, 11, 11, 2, C_WHITE);
            hd_rrect(x - 3, 19, 7, 1, 0, C_WHITE);
            hd_rrect(x, 16, 1, 7, 0, C_WHITE);
        }
        cv_oy = 0;
        cv_blit(0, 34u + i * 18u);
    }
}
/* Ruler (rows 92..107): a tick a bar, the bar number where each block starts (when it clears the one before);
 * the playhead's bar, white. */
static void br_arr_ruler(const br_arf_t *f)
{
    unsigned i, ws = f->ws;
    int32_t free = 0, x, v;
    char u[8];
    if (!br_arr_open(f, 5u, f->sum * 31u + f->pmark, 240, 16)) return;
    for (i = ws; i <= ws + BR_ARR_SHOWN; ++i) hd_rrect(br_arr_x(i, ws) - 1, 3, 1, 3, 0, RGB(100, 110, 114));
    for (i = 0; i < f->n; ++i) {
        v = (int32_t)br_arr_bar_at(i) + 1;
        x = br_arr_x((unsigned)v - 1u, ws);
        br_arr_fmt(u, "%", &v);
        if (x < free || x + br_widget_text_width(u) > 238) continue;
        hd_rrect(x - 1, 3, 1, 5, 0, RGB(150, 160, 164));
        br_widget_text(x + 1, 7, u, BR_DIM);
        free = x + br_widget_text_width(u) + 6;
    }
    if (f->pmark) hd_rrect(br_arr_x(f->pbar, ws), 0, BR_ARR_PX - 2, 3, 1, C_WHITE);
    cv_blit(0, 92);
}
/* Position (rows 116..133): the cursor's block and bar on the ribbon; while the song plays, the playhead's. */
static void br_arr_position_text(const br_arf_t *f, char t[40])
{
    unsigned b = f->pmark ? f->play : f->at;
    int32_t v[4];
    v[0] = (int32_t)b + 1; v[1] = f->n; v[2] = (int32_t)br_arr_bar_at(b) + 1;
    v[3] = b < f->n ? v[2] + (int32_t)br_ps_state.entry[b].bars - 1 : f->total;
    if (!f->pmark && f->gap && b < f->n) br_arr_fmt(t, "INSERT BEFORE BLOCK %", v);
    else br_arr_fmt(t, !f->n ? "NO BLOCKS YET" : b >= f->n ? "END: % BARS" :
                    v[2] == v[3] ? "BLOCK %/%   BAR %" : "BLOCK %/%   BARS %-%",
                    b < f->n ? v : v + 3);
}
static void br_arr_position(const br_arf_t *f)
{
    char t[40];
    br_arr_position_text(f, t);
    if (!br_arr_open(f, 6u, br_hash_text(t) + f->pmark, 240, 18)) return;
    br_arr_centre(120, 5, t, f->pmark ? C_WHITE : BR_DIM);
    cv_blit(0, 116);
}
static void br_arr_draw(uint32_t force)
{
    br_arf_t F, *f = &F;
    const fm1_pattern_song *s = &br_ps_state;
    unsigned n = s->song_length, at = br_arr_at(), ws = 0u, sa = 0u, sb = 0u, k;
    unsigned total = br_arr_bar_at(n), play = s->song_enabled && s->song_position < n ? s->song_position : 255u;
    f->sel = (uint8_t)br_arr_range(&sa, &sb); f->sa = (uint8_t)sa; f->sb = (uint8_t)sb;
    f->n = (uint8_t)n; f->at = (uint8_t)at; f->gap = (uint8_t)br_arr_gap(); f->play = (uint8_t)play; f->total = (uint16_t)total; f->force = force;
    f->pbar = (uint16_t)(play < n ? br_arr_bar_at(play) + (s->pre ? 0u : s->bars_elapsed) : 0u);
    f->pmark = (uint16_t)(play < n ? f->pbar + 1u : 0u);
    br_arr.px = (uint8_t)br_clip(228 / (int32_t)(total + 2u), 12, 38);
    if (BR_ARR_VIEW) {     /* keep the cursor block (or the end slot) on screen with two bars of its neighbours */
        unsigned a = br_arr_bar_at(at), b = a + (!f->gap && at < n ? s->entry[at].bars : 2u), end = total + 2u;
        ws = br_arr.ws;
        if (a < ws + 2u) ws = a > 2u ? a - 2u : 0u;
        if (b + 2u > ws + BR_ARR_SHOWN) ws = b + 2u - BR_ARR_SHOWN;
        if (ws + BR_ARR_SHOWN > end) ws = end > BR_ARR_SHOWN ? end - BR_ARR_SHOWN : 0u;
        if (ws > a) ws = a;
        br_arr.ws = (uint16_t)ws;
    }
    f->ws = (uint16_t)ws;
    f->sum = br_arr_hash(s, sizeof s->entry, br_arr_hash(s->loop, sizeof s->loop, n * 64u + s->mode * 2u + s->end)) * 31u +
                 br_arr.px + (ws << 8);
    if (!BR_ARR_VIEW) br_arr_patterns(f);
    else { br_arr_ribbon(f); br_arr_ruler(f); br_arr_position(f); }
    for (k = 0; k < 4u; ++k) br_arr_knob(f, k);
    br_arr_cue(f);
}
#endif
