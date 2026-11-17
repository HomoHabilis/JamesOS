/* Pattern arranger screen through the real app (host only): performance modes driven by
 * buttons, knobs and black keys (SMK-37 Pro: the pads reach the same keys), recording a song, cursor,
 * insert between blocks, bars and pattern, remove (OCT- + REC), selection (OCT- + knob 1) and loops
 * (engine fixtures), the engine's refusals as messages, PATTERN-mode keys, PLAY from the start with the audio
 * render, edits while the song plays, the drawing cost of a playing song, and save -> clear -> load.
 * With ARR_SHOTS=DIR set, it writes DIR/<name>.ppm screenshots of every state (tools/render_screens.py). */
#define DINGHY_HOST 1
#define BR_PATTERN_TEST_IRQ_TRACK 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define BR_PATTERN_SONG_EXPERIMENT 1
#define BR_PATTERN_OWNER_HOST 1
#define BR_PATTERN_BANK_EXPERIMENT 1
#define BR_PATTERN_SNAPSHOT_MONOLITHIC_TEST 1
#define FM1_PATTERN_TICK_CLAIM_PROTOTYPE 1
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define FM1_PATTERN_AUDIO_IRQ_PROTOTYPE 1
#define UI_REFRESH_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#undef br_ps_select
#undef br_ps_copy_pattern
#undef br_ps_paste_pattern
#define BR_SPLIT_AUDIO 1
#include "../firmware/src/pattern_snapshot.c"
#include "../firmware/src/pattern_storage_codec.c"
#include "../firmware/src/pattern_bank.c"
#include "../firmware/src/pattern_owner.c"
#include "../firmware/src/pattern_song.c"
#define put32 project_put32
#define get32 project_get32
#include "../firmware/src/pattern_project.c"
#undef put32
#undef get32

#if !BR_ARR_UI
#error "the arranger screen is not built in this configuration"
#endif

static br_pattern_snapshot slots[3];
static br_pattern_irq_publication pub;
static uint8_t owner_arena[20000], owner_codec[9000];
static br_pattern_snapshot owner_live, owner_work, owner_clip;
static fm1_pattern_song *const s = &br_ps_state;
static unsigned checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s (message \"%s\")\n", __FILE__, __LINE__, #c, br_ui.message); abort(); } ++checks; } while (0)

static int owner_quiet(void *ctx) { (void)ctx; return !br_playing && br_diagnostic_restore_in_progress; }
static uint64_t energy;
static unsigned blocks;
static void ui_tick(void) { ++fm1_ms; br_app_tick(); }
/* One audio block, then one pass of the app loop (as the device interleaves them). */
static void step(void)
{
    int32_t out[BR_BLOCK * 2u] = {0};
    CHECK(app_audio(out, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    CHECK(!br_pattern_host_error);
    for (unsigned i = 0; i < BR_BLOCK * 2u; ++i) energy += (uint64_t)(out[i] < 0 ? -(int64_t)out[i] : out[i]);
    ++blocks;
    ui_tick();
    /* The song's own block changes never show the switch-gate message. */
    if (s->song_enabled && br_ui.message_start == fm1_ms) CHECK(strcmp(br_ui.message, "PATTERN SWITCH BUSY"));
}
static void tick(void) { if (br_playing) step(); else ui_tick(); }
static void press(unsigned b) { fm1_in.buttons |= 1u << b; host_pressed |= 1u << b; tick(); }
static void release(unsigned b) { fm1_in.buttons &= ~(1u << b); host_released |= 1u << b; tick(); }
static void button(unsigned b) { press(b); release(b); }
/* PLAY as the app test starts it: the start (selection while stopped) completes before audio runs. */
static void play_start(void)
{
    fm1_in.buttons |= 1u << BTN_PLAY; host_pressed |= 1u << BTN_PLAY; ui_tick();
    fm1_in.buttons &= ~(1u << BTN_PLAY); host_released |= 1u << BTN_PLAY; ui_tick();
    ui_tick(); ui_tick(); ui_tick();
}
static void raw_knob(unsigned k, int d) { host_encoder[ENC_KNOB1 + k] += d; tick(); }
/* The older block-edit tests step directly between blocks; raw_knob exercises every gap. */
static void knob(unsigned k, int d) {
    if (k == 0u && BR_ARR_VIEW && !(fm1_in.buttons & ((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP)))) {
        int target = br_clip((int)br_arr_at() + d, 0, (int)s->song_length);
        int pos = target == (int)s->song_length ? 2 * target : 2 * target + 1;
        d = pos - br_arr.cursor;
    }
    raw_knob(k, d);
}
static unsigned key_of(unsigned pattern)
{
    for (unsigned k = 0; k < 27u; ++k) if (br_black_key(k) && br_key_place(k) == pattern) return k;
    abort();
}
static void key(unsigned pattern)    /* a black key tap (SMK-37 Pro: the pad of that pattern) */
{
    unsigned k = key_of(pattern);
    fm1_in.notes |= 1u << k; host_note_pressed |= 1u << k; tick();
    fm1_in.notes &= ~(1u << k); tick();
}
static void settle(unsigned n) { while (n--) tick(); }
static void quiet(void) { br_ui.message_until = fm1_ms; br_ui.redraw = 1u; settle(120); }
static unsigned view(void) { return BR_ARR_VIEW; }
static const char *knob_text(unsigned k) { br_control_t c = br_control(k); return c.text ? c.text : ""; }
static unsigned song(const char *want)   /* blocks as "pattern:bars ..." (1-based patterns) */
{
    char got[512] = "", one[16];
    for (unsigned i = 0; i < s->song_length; ++i) {
        snprintf(one, sizeof one, "%s%u:%u", i ? " " : "", s->entry[i].pattern + 1u, s->entry[i].bars);
        strcat(got, one);
    }
    if (strcmp(got, want)) { fprintf(stderr, "song: got \"%s\" want \"%s\"\n", got, want); return 0; }
    return 1;
}
static void shot(const char *name)
{
    const char *dir = getenv("ARR_SHOTS");
    char path[512];
    FILE *f;
    settle(150);
    if (!dir) return;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    CHECK(f != 0);
    fprintf(f, "P6\n240 240\n255\n");
    for (unsigned i = 0; i < 240u * 240u; ++i) {
        uint16_t c = fb[i];
        fputc((c >> 11) << 3 | (c >> 13), f); fputc(((c >> 5) & 63u) << 2 | ((c >> 9) & 3u), f); fputc((c & 31u) << 3 | ((c >> 2) & 7u), f);
    }
    fclose(f);
}
/* The four knob-card icons: LOOP MODE ON/OFF, PERFORM PATTERN/ARRANGE and BARS 1/2/3/4/8 each draw a different
 * picture on the live screen (redrawn incrementally, no forced repaint), and no icon leaves its tile. */
#define ICON_X(k) (14 + 60 * (int)(k))
#define ICON_Y 146
static void icon_grab(unsigned k, uint16_t *out)       /* the 34 x 32 tile */
{
    for (unsigned y = 0; y < 32u; ++y) memcpy(out + y * 34u, fb + (ICON_Y + y) * 240u + ICON_X(k), 34u * 2u);
}
static unsigned icon_leak(unsigned k)                  /* canvas pixels beside the tile (rows 0..31) that are not background */
{
    unsigned n = 0;
    for (unsigned y = 0; y < 32u; ++y)       /* (rows 32 and below hold the value and label text) */
        for (unsigned x = 0; x < 54u; ++x) {
            if (x >= 10u && x < 44u) continue;
            if (fb[(ICON_Y + y) * 240u + 4u + 60u * k + x] != BR_BACKGROUND) ++n;
        }
    return n;
}
static void icon_test(void)
{
    static uint16_t pic[5][34u * 32u];
    static const unsigned bars[5] = {1, 2, 3, 4, 8};
    br_ui.page = BR_PAGE_PS; s->mode = FM1_SONG_MODE_SONG;
    CHECK(fm1_arr_clear(s) == FM1_ARR_OK);
    br_arr.cursor = 0u;
    for (unsigned i = 0; i < 5u; ++i) {
        br_arr.bars = (uint8_t)bars[i]; quiet(); settle(150);
        icon_grab(1u, pic[i]);
        CHECK(icon_leak(1u) == 0u);
        for (unsigned j = 0; j < i; ++j) CHECK(memcmp(pic[i], pic[j], sizeof pic[i]));
        { char name[16]; snprintf(name, sizeof name, "icon-bars-%u", bars[i]); shot(name); }
    }
    br_arr.bars = 0u;
    for (unsigned k = 2u; k < 4u; ++k) {
        for (unsigned st = 0; st < 2u; ++st) {
            if (k == 2u) s->end = (uint8_t)st; else s->mode = st ? FM1_SONG_MODE_SONG : FM1_SONG_MODE_PATTERN;
            quiet(); settle(150);
            icon_grab(k, pic[st]);
            shot(k == 2u ? (st ? "icon-loop-on" : "icon-loop-off") : (st ? "icon-perform-arrange" : "icon-perform-pattern"));
            CHECK(icon_leak(k) == 0u);
        }
        CHECK(memcmp(pic[0], pic[1], sizeof pic[0]));
    }
    s->mode = FM1_SONG_MODE_SONG; s->end = 0u; quiet();
}
/* PATTERNS alternates the two actionable key hints. The actual cue pixels
 * must change with the text and return to the same image on the next cycle. */
static void pattern_cue_test(void)
{
    static uint16_t first[240u * 24u], second[240u * 24u];
    br_ui.page = BR_PAGE_PS; s->mode = FM1_SONG_MODE_PATTERN;
    fm1_ms = 0u; br_ui.message_until = fm1_ms; br_ui.redraw = 1u; settle(150);
    CHECK(!strcmp(br_arr_hint(), "[PATTERN KEYS] QUEUE NEXT"));
    memcpy(first, fb + 216u * 240u, sizeof first); shot("pattern-cue-queue");
    fm1_ms = 4096u; br_ui.message_until = fm1_ms; br_ui.redraw = 1u; settle(150);
    CHECK(!strcmp(br_arr_hint(), "HOLD [PATTERN KEYS] TO CHAIN"));
    memcpy(second, fb + 216u * 240u, sizeof second); shot("pattern-cue-chain");
    CHECK(memcmp(first, second, sizeof first));
    fm1_ms = 8192u; br_ui.message_until = fm1_ms; br_ui.redraw = 1u; settle(150);
    CHECK(!memcmp(first, fb + 216u * 240u, sizeof first));
    br_message("PATTERN QUEUED"); settle(150);
    CHECK(memcmp(first, fb + 216u * 240u, sizeof first));
    br_ui.message_until = fm1_ms; br_ui.redraw = 1u; settle(150);
    CHECK(!memcmp(first, fb + 216u * 240u, sizeof first));
}
static unsigned playing_note(void)
{
    return br_seq_note[0] >= 50u && br_seq_note[0] < 50u + FM1_PATTERN_COUNT ? br_seq_note[0] - 50u : 99u;
}
/* Run until `bars` more bar lines have passed (or the transport stops); per bar: the playhead's block,
 * the active pattern once the bar's switch is done, and the pattern heard (track 1's note on its step). */
static unsigned active_log[64];
static unsigned run_bars(unsigned bars, unsigned *heard, unsigned *block)
{
    unsigned n = 0, start = br_pattern_irq_bar_count(), at = start, guard = 0;
    heard[0] = 99u; block[0] = s->song_position; active_log[0] = s->active;
    while (br_playing && at - start < bars) {
        step();
        CHECK(++guard < 400000u);
        if (heard[n] == 99u) heard[n] = playing_note();
        if (br_pattern_irq_bar_count() != at) {
            at = br_pattern_irq_bar_count();
            for (unsigned i = 0; i < 8u && br_playing; ++i) step();
            if (++n >= bars || !br_playing) break;
            heard[n] = 99u; block[n] = s->song_position; active_log[n] = s->active;
        }
    }
    return n;
}
/* Patterns per bar (0-based). Every bar's active pattern must match; the pattern heard is checked from the
 * second bar on (the first bar's step-1 note may be read before the sequencer reports it). */
static void expect_patterns(const unsigned *heard, unsigned n, const unsigned *want, unsigned count)
{
    printf("patterns:");
    for (unsigned i = 0; i < n; ++i) printf(" %u", active_log[i] + 1u);
    printf("  heard:");
    for (unsigned i = 0; i < n; ++i) printf(" %u", heard[i] + 1u);
    printf("\n");
    CHECK(n == count);
    for (unsigned i = 0; i < n; ++i) CHECK(active_log[i] == want[i] && (!i || heard[i] == want[i]));
}
static int session_valid(const uint8_t *p, size_t n, void *ctx) { (void)ctx; return n == FM1_PROJECT_SESSION && p[0] == 0x5a; }
static void select_stopped(unsigned k)
{
    CHECK(!br_playing && br_quiescent_transaction_begin());
    CHECK(br_ps_select(k));
    br_quiescent_transaction_end();
}

/* Compare both mode transitions against a clean draw and report display traffic. */
static void mode_transition_test(void)
{
    static uint16_t expected[240u * 240u];
    fm1_ms = (fm1_ms + 4095u) & ~4095u; /* keep rotating hints stable across both draws */
    for (unsigned target = 0; target < 2u; ++target) {
        CHECK(fm1_arr_set_mode(s, target) == FM1_ARR_OK);
        br_ui.edit_key = 0xffffffffu; br_ui.message_until = fm1_ms;
        lcd_fill(0, 0, 240, 240, BR_BACKGROUND);
        br_draw_layout_old = 0xffffffffu; br_ui.redraw = 1u; settle(120);
        memcpy(expected, fb, sizeof expected);
        CHECK(fm1_arr_set_mode(s, !target) == FM1_ARR_OK);
        br_ui.redraw = 1u; settle(120);
        ui_test_lcd_pixel_bytes = 0;
        CHECK(fm1_arr_set_mode(s, target) == FM1_ARR_OK);
        br_ui.redraw = 1u; settle(120);
        printf("mode %u transition: %llu pixel bytes\n", target, (unsigned long long)ui_test_lcd_pixel_bytes);
        if (memcmp(expected, fb, sizeof expected)) {
            unsigned minx=240, miny=240, maxx=0, maxy=0, count=0;
            for (unsigned y=0; y<240; ++y) for (unsigned x=0; x<240; ++x)
                if (expected[y*240+x] != fb[y*240+x]) {
                    if (x<minx) minx=x; if (x>maxx) maxx=x;
                    if (y<miny) miny=y; if (y>maxy) maxy=y; ++count;
                }
            fprintf(stderr,"transition difference: %u pixels, (%u,%u)-(%u,%u)\n",count,minx,miny,maxx,maxy);
        }
        CHECK(!memcmp(expected, fb, sizeof expected));
        /* Full-height sequencer-style tiles increase the mode-transition
         * traffic; retain a bounded redraw budget. */
        CHECK(ui_test_lcd_pixel_bytes < 105000u);
    }
}

/* The grid and arranger use controls at different heights. Interrupt each
 * transition at several points, then compare the settled sliced frame with a
 * clean rendering of the same populated destination. */
static void transition_case_select(unsigned i, unsigned passes)
{
    br_ui.part = i == 0u ? 0u : BR_DRUM_PART;
    br_ui.page = i <= 1u ? BR_PAGE_SEQ : i == 2u ? BR_PAGE_PATTERN : BR_PAGE_PS;
    br_ui.screen[br_ui.page] = 0u;
    s->mode = i == 4u ? FM1_SONG_MODE_SONG : FM1_SONG_MODE_PATTERN;
    br_ui.message_until = fm1_ms; br_ui.redraw = 1u;
    while (passes--) br_app_tick();
}
static void transition_case_tests(void)
{
    static uint16_t expected[240u * 240u];
    static const unsigned pauses[] = {1u, 2u, 5u, 10u, 150u};
    fm1_ms = 4096u; fm1_in.buttons = 0u; fm1_in.notes = 0u;
    host_pressed = host_released = host_note_pressed = 0u;
    br_ui.edit_key = 0xffffffffu; br_ui.step_bank = 0u; br_ui.lane = 3u;
    for (unsigned j = 0; j < 64u; ++j) {
        br_part[BR_DRUM_PART].step[j] = 15u | ((j & 1u) ? 0xf00u : 0u);
        br_part[0].step[j] = BR_NOTE_STEP(60u + j % 12u, 127u);
    }
    br_part[BR_DRUM_PART].length = br_part[0].length = 64u;
    fm1_arr_clear(s); CHECK(fm1_arr_insert(s, 0u, 2u, 8u) == FM1_ARR_OK);
    CHECK(fm1_arr_insert(s, 1u, 5u, 3u) == FM1_ARR_OK); br_arr.cursor = 1u;
    br_playing = 1u; br_step_pos[BR_DRUM_PART] = br_step_pos[0] = 9u;
    for (unsigned target = 0; target < 5u; ++target)
        for (unsigned source = 0; source < 5u; ++source)
            for (unsigned k = 0; k < sizeof pauses / sizeof pauses[0]; ++k) {
                lcd_fill(0, 0, 240, 240, BR_BACKGROUND);
                br_draw_layout_old = 0xffffffffu;
                transition_case_select(target, 150u); memcpy(expected, fb, sizeof expected);
                transition_case_select(source, pauses[k]); transition_case_select(target, 150u);
                if (memcmp(expected, fb, sizeof expected)) {
                    unsigned x0 = 240u, y0 = 240u, x1 = 0u, y1 = 0u, count = 0u;
                    for (unsigned y = 0; y < 240u; ++y) for (unsigned x = 0; x < 240u; ++x)
                        if (expected[y * 240u + x] != fb[y * 240u + x]) {
                            if (x < x0) x0 = x; if (x > x1) x1 = x;
                            if (y < y0) y0 = y; if (y > y1) y1 = y; ++count;
                        }
                    fprintf(stderr, "transition %u -> %u after %u passes: %u pixels (%u,%u)-(%u,%u)\n",
                            source, target, pauses[k], count, x0, y0, x1, y1);
                }
                CHECK(!memcmp(expected, fb, sizeof expected));
            }
}

int main(void)
{
    static unsigned heard[64], block[64];
    static br_pattern_snapshot lengths;
    unsigned n;
    CHECK(br_pattern_snapshot_bars(&lengths) == 1u);
    lengths.length[0] = 16u; lengths.length[1] = 17u;
    CHECK(br_pattern_snapshot_bars(&lengths) == 2u);
    lengths.length[2] = 33u; CHECK(br_pattern_snapshot_bars(&lengths) == 3u);
    lengths.length[3] = 64u; CHECK(br_pattern_snapshot_bars(&lengths) == 4u);
    br_app_init(); fm1_ms = 2000; br_bpm = 240;
    br_load_preset(0, BR_FIRST_PLAITS);
    br_ui.part = 0; br_ui.page = BR_PAGE_HOME;
    CHECK(br_quiescent_transaction_begin());
    CHECK(br_ps_host_init(owner_arena, sizeof owner_arena, &owner_live, &owner_work, &owner_clip, owner_codec, sizeof owner_codec, owner_quiet, 0));
    CHECK(s->song_length == 1u && s->entry[0].pattern == 0u && s->entry[0].bars == br_pattern_snapshot_bars(&owner_live));
    {   /* Group labels count absolute bars, including singular and playback. */
        fm1_pattern_song saved = *s; br_arf_t f = {0}; char text[40];
        s->song_length = 3u; s->entry[0].bars = 5u; s->entry[1].bars = 1u; s->entry[2].bars = 4u;
        f.n = 3u; f.total = 10u;
        f.at = 0u; br_arr_position_text(&f, text); CHECK(!strcmp(text, "BLOCK 1/3   BARS 1-5"));
        f.at = 1u; br_arr_position_text(&f, text); CHECK(!strcmp(text, "BLOCK 2/3   BAR 6"));
        f.at = 2u; br_arr_position_text(&f, text); CHECK(!strcmp(text, "BLOCK 3/3   BARS 7-10"));
        f.pmark = 1u; f.play = 0u; f.pbar = 3u; br_arr_position_text(&f, text); CHECK(!strcmp(text, "BLOCK 1/3   BARS 1-5"));
        f.pmark = 0u; f.at = 3u; br_arr_position_text(&f, text); CHECK(!strcmp(text, "END: 10 BARS"));
        f.n = 0u; f.total = 0u; f.at = 0u; br_arr_position_text(&f, text); CHECK(!strcmp(text, "NO BLOCKS YET"));
        *s = saved;
    }
    CHECK(fm1_arr_clear(s) == FM1_ARR_OK);
    CHECK(br_ps_runtime_bind(&pub, &slots[0], &slots[1], &slots[2]));
    br_quiescent_transaction_end();
    /* Patterns 1..5: track 1 plays note 50 + k on its first step. */
    for (unsigned k = 0; k < 5u; ++k) {
        select_stopped(k);
        memset((void *)br_part[0].step, 0, sizeof br_part[0].step);
        br_part[0].length = 16; br_part[0].division = 1;
        br_part[0].step[0] = BR_NOTE_STEP(50u + k, 100u); br_seq_synth_velocity[0][0] = 255;
        br_seq_synth_length[0][0] = 64;
    }
    select_stopped(0);
    settle(400);

    /* PATTERNS slot 6 shifts left one pixel, along with its chain label. */
    CHECK(br_arr_pattern_x(5u) == 110 && br_arr_pattern_x(4u) == 83 && br_arr_pattern_x(6u) == 129);
    /* The performance mode selects both the source and the view. */
    press(BTN_OCT_DOWN); press(BTN_SEQ); release(BTN_SEQ); release(BTN_OCT_DOWN);
    CHECK(br_ui.page == BR_PAGE_PS && view() == 0u);
    /* Return from each performance mode in either order, with no chord message. */
    for (unsigned mode = 0; mode < 2u; ++mode) {
        if (mode) knob(3, 1);
        for (unsigned order = 0; order < 2u; ++order) {
            CHECK(br_ui.page == BR_PAGE_PS && view() == mode);
            if (order) { press(BTN_SEQ); press(BTN_OCT_DOWN); release(BTN_OCT_DOWN); release(BTN_SEQ); }
            else { press(BTN_OCT_DOWN); press(BTN_SEQ); release(BTN_SEQ); release(BTN_OCT_DOWN); }
            CHECK(br_ui.page == BR_PAGE_SEQ && br_ui.screen[BR_PAGE_SEQ] == 0u);
            CHECK(!br_ui.message[0]);
            press(BTN_OCT_DOWN); press(BTN_SEQ); release(BTN_SEQ); release(BTN_OCT_DOWN);
            CHECK(br_ui.page == BR_PAGE_PS && view() == mode);
        }
        if (mode) {
            knob(3, -1);
            press(BTN_OCT_DOWN); press(BTN_SEQ); release(BTN_SEQ); release(BTN_OCT_DOWN);
            press(BTN_OCT_DOWN); press(BTN_SEQ); release(BTN_SEQ); release(BTN_OCT_DOWN);
        }
    }
    CHECK(!br_ui.message[0] && *br_arr_hint());
    CHECK(!strcmp(br_header_title(), "PATTERNS"));
    CHECK(!br_control(0).text && !br_control(1).text && !strcmp(knob_text(3), "PATTERN") && !br_control(2).text);
    knob(2, 1); CHECK(s->end == FM1_SONG_END_STOP); /* loop disabled for patterns */
    key(2); settle(20); CHECK(s->active == 2u && !s->song_length);
    key(0); settle(20); CHECK(s->active == 0u);
    /* Hold one pattern while tapping another twice: the chain loops 1, 2, 2. */
    {
        unsigned k = key_of(0);
        br_part[0].length = 32u; /* Pattern 1 takes two bars; Pattern 2 takes one. */
        CHECK(br_chain_pattern_bars() == 2u);
        br_part[0].division = 0u; CHECK(br_chain_pattern_bars() == 4u);
        br_part[0].division = 1u;
        fm1_in.notes |= 1u << k; host_note_pressed |= 1u << k; tick();
        key(1); key(1);
        CHECK(br_chain.gesture && br_chain.draft_count == 2u && br_chain.draft_repeats[1] == 2u);
        fm1_in.notes &= ~(1u << k); tick(); settle(20);
        CHECK(br_chain.count == 2u && br_chain.pattern[0] == 0u && br_chain.pattern[1] == 1u);
        quiet(); shot("pattern-chain");
        play_start();
        n = run_bars(8u, heard, block);
        {
            static const unsigned want[] = {0, 0, 1, 1, 0, 0, 1, 1};
            CHECK(n == 8u);
            for (unsigned i = 0; i < n; ++i) CHECK(active_log[i] == want[i]);
            CHECK(heard[2] == 1u && heard[4] == 0u && heard[6] == 1u);
        }
        /* A fresh held gesture can replace a chain while a switch is queued. */
        {
            unsigned h = key_of(2), anchor = key_of(3);
            fm1_in.notes |= 1u << h; host_note_pressed |= 1u << h; tick();
            fm1_in.notes |= 1u << anchor; host_note_pressed |= 1u << anchor; tick();
            fm1_in.notes &= ~(1u << h); tick();
            key(2);
            CHECK(br_chain.draft_count == 2u && br_chain.draft_repeats[0] == 2u);
            fm1_in.notes &= ~(1u << anchor); tick();
            n = run_bars(8u, heard, block);
            CHECK(n == 8u && br_chain.count == 2u && br_chain.pattern[0] == 2u && br_chain.repeats[0] == 2u);
            CHECK(active_log[4] == 2u && active_log[5] == 2u && active_log[6] == 3u);
        }
        /* A single press ends the chain even with an outstanding handoff. */
        key(4); n = run_bars(4u, heard, block);
        CHECK(n == 4u && !br_chain.count && s->active == 4u);
        button(BTN_PLAY); settle(20);
        key(0); settle(20); CHECK(!br_chain.count && s->active == 0u);
        br_part[0].length = 16u;
        /* A very short tap can arrive as an edge while the anchor stays down. */
        fm1_in.notes |= 1u << k; host_note_pressed |= 1u << k; tick();
        host_note_pressed |= 1u << key_of(1); tick();
        fm1_in.notes &= ~(1u << k); tick(); settle(20);
        CHECK(br_chain.count == 2u && br_chain.pattern[0] == 0u && br_chain.pattern[1] == 1u);
        key(0); settle(20); CHECK(!br_chain.count);
    }
    button(BTN_SEQ); CHECK(view() == 0u);           /* SEQ does not change modes */
    quiet(); shot("perform-pattern");
    knob(3, 1); CHECK(view() == 1u && s->mode == FM1_SONG_MODE_SONG);
    CHECK(!strcmp(br_ui.message, "NOW IN ARRANGER MODE"));
    CHECK(!strcmp(br_header_title(), "ARRANGER"));
    CHECK(!strcmp(knob_text(0), "-") && !strcmp(knob_text(1), "4 BARS"));
    CHECK(!strcmp(knob_text(3), "ARRANGE") && !strcmp(knob_text(2), "OFF"));
    button(BTN_PLAY); CHECK(!br_playing && !s->song_enabled && !strcmp(br_ui.message, "ARRANGEMENT EMPTY"));
    knob(2, 1); CHECK(s->end == FM1_SONG_END_LOOP && !strcmp(knob_text(2), "ON"));
    knob(2, -1); CHECK(s->end == FM1_SONG_END_STOP);
    quiet(); shot("arr-empty");
    press(BTN_OCT_UP);
    CHECK(!strcmp(knob_text(0), "-") && !strcmp(knob_text(1), "4 BARS"));
    CHECK(!strcmp(br_arr_hint(), "[PATTERN KEY] INSERT"));
    CHECK(!strstr(br_arr_hint(), "OCT+"));
    raw_knob(1, 1); CHECK(!strcmp(knob_text(1), "5 BARS"));
    raw_knob(1, -1); release(BTN_OCT_UP);

    /* Record a sequence with black keys: each goes in at the cursor, the cursor steps past it. */
    key(0); key(1); key(2);
    CHECK(song("1:4 2:4 3:4") && br_arr_at() == 3u);
    CHECK(!strcmp(knob_text(0), "END") && !strcmp(knob_text(1), "4 BARS"));
    /* A block tap replaces its pattern; its length and neighboring blocks are unchanged. */
    knob(0, -2); CHECK(br_arr_at() == 1u && !br_arr_gap() && !strcmp(knob_text(0), "2/3"));
    { uint32_t when = fm1_ms;
      fm1_ms = 0u; CHECK(!strcmp(br_arr_hint(), "[PATTERN KEY] CHANGE"));
      fm1_ms = 4096u; CHECK(!strcmp(br_arr_hint(), "[OCT-] + [REC] REMOVE"));
      fm1_ms = when; }
    key(4); CHECK(song("1:4 5:4 3:4") && br_arr_at() == 1u && !br_arr_gap());
    key(1); CHECK(song("1:4 2:4 3:4"));
    /* One detent selects the gap before block 2; a key inserts there. */
    raw_knob(0, -1); CHECK(br_arr_gap() && br_arr_at() == 1u && !strcmp(knob_text(0), "GAP 2"));
    CHECK(!strcmp(br_arr_hint(), "[PATTERN KEY] INSERT"));
    quiet(); shot("arr-gap-between");
    key(3); CHECK(song("1:4 4:4 2:4 3:4") && br_arr_at() == 2u && br_arr_gap());
    knob(0, 0); CHECK(!br_arr_gap() && br_arr_at() == 2u);
    /* Bars and pattern of the cursor block (pattern 2): 8 bars, pattern 3. */
    knob(1, 4); CHECK(song("1:4 4:4 2:8 3:4") && !strcmp(knob_text(1), "8 BARS"));
    key(2); CHECK(song("1:4 4:4 3:8 3:4"));
    knob(1, 40); CHECK(s->entry[2].bars == 16u);
    knob(1, -8); CHECK(s->entry[2].bars == 8u);
    key(0); CHECK(s->entry[2].pattern == 0u);
    key(1);
    /* A new block gets the last bar count used (8). */
    knob(0, 10); CHECK(br_arr_at() == 4u);
    key(4); CHECK(song("1:4 4:4 2:8 3:4 5:8"));
    /* At the end slot knob 2 sets the bar count of the next block. */
    knob(1, -6); CHECK(!strcmp(knob_text(1), "2 BARS") && song("1:4 4:4 2:8 3:4 5:8"));
    knob(1, 2);
    quiet(); shot("arr-short-song");
    mode_transition_test();
    knob(3, -1); quiet(); shot("perform-with-song");
    CHECK(view() == 0u && *br_arr_hint());
    knob(3, 1); CHECK(view() == 1u);

    /* OCT- + REC removes the cursor block; the cursor stays on the block that slides in. REC alone
     * keeps its meaning (record arm), never a removal. */
    knob(0, -4); CHECK(br_arr_at() == 1u);
    press(BTN_OCT_DOWN); press(BTN_REC); CHECK(s->song_length == 4u);
    shot("arr-removed");
    release(BTN_REC); release(BTN_OCT_DOWN);
    CHECK(song("1:4 2:8 3:4 5:8") && br_arr_at() == 1u && !br_recording);
    button(BTN_REC); CHECK(br_recording && song("1:4 2:8 3:4 5:8"));
    button(BTN_REC); CHECK(!br_recording);
    knob(0, 10); press(BTN_OCT_DOWN); press(BTN_REC); release(BTN_REC); release(BTN_OCT_DOWN);
    CHECK(!strcmp(br_ui.message, "NO BLOCK HERE") && s->song_length == 4u);

    /* Existing region loops remain visible and playable; the engine still validates their edits. */
    knob(0, -3); CHECK(br_arr_at() == 1u);
    press(BTN_OCT_DOWN); knob(0, 1); release(BTN_OCT_DOWN);
    CHECK(br_arr.sel && br_arr.anchor == 1u && br_arr_at() == 2u);
    quiet(); shot("arr-selection");
    CHECK(fm1_arr_loop_set(s, 1u, 2u, 4u) == FM1_ARR_OK);
    quiet(); shot("arr-loop");
    CHECK(fm1_arr_loop_remove(s, 0u) == FM1_ARR_OK);
    knob(0, 10); key(0); CHECK(s->song_length == 5u);
    /* Full at 64 blocks. */
    knob(0, 10);
    while (s->song_length < FM1_SONG_ENTRIES) key(s->song_length % FM1_PATTERN_COUNT);
    key(1); CHECK(!strcmp(br_ui.message, "SONG FULL: 64") && s->song_length == 64u);
    shot("arr-message-full");
    /* A long song, scrolled to keep the cursor in view; a loop in sight. */
    knob(0, -30); CHECK(br_arr_at() == 34u);
    press(BTN_OCT_DOWN); knob(0, 2); release(BTN_OCT_DOWN);
    CHECK(fm1_arr_loop_set(s, 34u, 36u, 4u) == FM1_ARR_OK);
    CHECK(s->loop_count == 1u && s->loop[0].first == 34u && s->loop[0].last == 36u && s->loop[0].repeats == 4u);
    quiet(); shot("arr-long-scrolled");
    CHECK(br_arr.ws > 0u);
    /* Back to a short song for the play tests. */
    knob(0, -64);
    while (s->song_length) { press(BTN_OCT_DOWN); press(BTN_REC); release(BTN_REC); release(BTN_OCT_DOWN); }
    CHECK(!s->loop_count && br_arr_at() == 0u);
    key(0); key(1); key(2); key(3);
    for (unsigned i = 0; i < 4u; ++i) { knob(0, -10); knob(0, (int)i); knob(1, -10); }   /* 1 bar each */
    CHECK(song("1:1 2:1 3:1 4:1"));
    knob(0, -10); knob(0, 1); press(BTN_OCT_DOWN); knob(0, 1); release(BTN_OCT_DOWN);
    CHECK(fm1_arr_loop_set(s, 1u, 2u, 2u) == FM1_ARR_OK);
    CHECK(s->loop_count == 1u && s->loop[0].first == 1u && s->loop[0].last == 2u);

    /* PATTERN mode: black keys select (stopped) as on PATTERNS, the song untouched, a hint. */
    CHECK(fm1_arr_set_mode(s, FM1_SONG_MODE_PATTERN) == FM1_ARR_OK);
    key(4); settle(20);
    CHECK(s->active == 4u && song("1:1 2:1 3:1 4:1"));
    CHECK(view() == 0u);
    /* PLAY in PATTERN mode is the plain transport: pattern 5 plays. */
    play_start(); CHECK(br_playing && !s->song_enabled);
    n = run_bars(2, heard, block);
    { static const unsigned want[] = {4, 4}; expect_patterns(heard, n, want, 2); }
    /* Playing in PATTERN mode a key queues the pattern; the song is untouched. */
    key(1); settle(20); CHECK(s->queued == 1u && song("1:1 2:1 3:1 4:1"));
    n = run_bars(2, heard, block);
    button(BTN_PLAY); settle(40); CHECK(!br_playing);

    /* Arrangement playback starts at the beginning, even when the cursor is elsewhere. */
    CHECK(fm1_arr_set_mode(s, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
    knob(0, -10); knob(0, 2);
    play_start();
    CHECK(br_playing && s->song_enabled && s->song_position == 0u);
    n = run_bars(8, heard, block);
    { static const unsigned want[] = {0, 1, 2, 1, 2, 3}; expect_patterns(heard, n, want, 6); }
    settle(20); CHECK(!br_playing && br_arr_at() == 2u);
    knob(2, 1); CHECK(s->end == FM1_SONG_END_LOOP);
    knob(0, -1);
    play_start(); CHECK(br_playing && s->song_position == 0u);
    n = run_bars(7, heard, block);
    { static const unsigned want[] = {0, 1, 2, 1, 2, 3, 0}; expect_patterns(heard, n, want, 7); }
    CHECK(br_arr_at() == 1u);
    quiet(); shot("arr-playing-loop-pass");
    /* Drawing cost while the song plays and nothing is edited: what one bar sends to the screen. */
    {
        uint64_t bytes = ui_test_lcd_pixel_bytes;
        uint32_t blits = ui_test_lcd_blits;
        n = run_bars(1, heard, block);
        printf("one bar while playing: %llu bytes in %u blits\n", (unsigned long long)(ui_test_lcd_pixel_bytes - bytes),
               ui_test_lcd_blits - blits);
        CHECK(ui_test_lcd_pixel_bytes - bytes < 24000u);
    }
    /* Edit while it plays: insert pattern 5 at the cursor (block 2, in the loop's first position:
     * before the loop), the playhead keeps its block, the cursor steps past. */
    {
        unsigned pos = s->song_position;
        raw_knob(0, -1); CHECK(br_arr_gap() && br_arr_at() == 1u);
        key(4);
        CHECK(song("1:1 5:1 2:1 3:1 4:1") && br_arr_at() == 2u && s->song_position == pos + (pos >= 1u));
    }
    n = run_bars(6, heard, block);
    for (unsigned i = 0; i < n; ++i) CHECK(heard[i] < 5u);
    CHECK(br_arr_at() == 2u);
    /* A source change is refused during playback, keeping the song and queued pattern safe. */
    knob(3, -1); CHECK(view() == 1u && !strcmp(br_ui.message, "STOP TO CHANGE MODE"));
    button(BTN_SEQ); CHECK(view() == 1u);
    button(BTN_PLAY); settle(40); CHECK(!br_playing && !s->song_enabled);
    knob(3, -1); CHECK(view() == 0u && !strcmp(br_ui.message, "NOW IN PATTERN MODE"));
    knob(3, 1); CHECK(view() == 1u && !strcmp(br_ui.message, "NOW IN ARRANGER MODE"));

    /* Save -> clear -> load through the owner's export/adopt (the project path); the screen shows it. */
    {
        static uint8_t session[FM1_PROJECT_SESSION], restored[FM1_PROJECT_SESSION], work_session[FM1_PROJECT_SESSION];
        static uint8_t wire[FM1_PROJECT_MAX], stage_arena[20000], work_arena[20000], comp[FM1_PROJECT_RECORD_MAX];
        static br_pattern_snapshot work_pattern, live, staged_live;
        static br_pattern_bank viewb, staged;
        static fm1_project_work work;
        fm1_pattern_song exported, loaded, saved = *s;
        size_t len = 0;
        session[0] = 0x5a;
        work.compressed = comp; work.compressed_capacity = sizeof comp; work.session = work_session;
        work.arena = work_arena; work.arena_capacity = sizeof work_arena; work.pattern = &work_pattern;
        CHECK(br_quiescent_transaction_begin());
        CHECK(br_ps_host_export(&viewb, &live, &exported));
        CHECK(fm1_project_encode(session, &viewb, &live, &exported, session_valid, 0, &work, wire, sizeof wire, &len) == FM1_PROJECT_OK);
        br_quiescent_transaction_end();
        CHECK(fm1_arr_clear(s) == FM1_ARR_OK);
        quiet(); CHECK(!strcmp(knob_text(0), "-"));
        CHECK(fm1_project_decode(wire, len, session_valid, 0, &work, restored, &staged, stage_arena, sizeof stage_arena, &staged_live, &loaded) == FM1_PROJECT_OK);
        CHECK(br_quiescent_transaction_begin());
        CHECK(br_ps_host_adopt_copy(&staged, &staged_live, &loaded));
        br_quiescent_transaction_end();
        CHECK(s->song_length == saved.song_length && !memcmp(s->entry, saved.entry, sizeof saved.entry));
        CHECK(s->loop_count == saved.loop_count && !memcmp(s->loop, saved.loop, sizeof saved.loop));
        knob(0, -10);
        CHECK(!strcmp(knob_text(0), "1/5") && s->entry[0].pattern == 0u && !strcmp(knob_text(1), "1 BAR"));
        quiet(); shot("arr-loaded");
    }
    /* After a load (as at power-on) in SONG mode with another pattern active: PLAY from HOME withdraws the
     * start, selects block 0's pattern while stopped, then starts the song at block 0. */
    {
        unsigned stop_block, i;
        CHECK(s->mode == FM1_SONG_MODE_SONG && s->entry[0].pattern != 4u);
        select_stopped(4); CHECK(s->active == 4u);
        button(BTN_HOME); CHECK(br_ui.page == BR_PAGE_HOME);
        {
            unsigned before = blocks;
            fm1_in.buttons |= 1u << BTN_PLAY; host_pressed |= 1u << BTN_PLAY; ui_tick();
            CHECK(!br_playing && s->active == s->entry[0].pattern && !s->song_enabled);   /* withdrawn, selected */
            fm1_in.buttons &= ~(1u << BTN_PLAY); host_released |= 1u << BTN_PLAY; ui_tick(); ui_tick();
            CHECK(br_playing && s->song_enabled && s->song_position == 0u && s->active == s->entry[0].pattern);
            CHECK(blocks == before);                     /* no audio of pattern 5 before the song */
        }
        n = run_bars(4, heard, block);
        for (i = 0; i < n; ++i) CHECK(active_log[i] == s->entry[block[i]].pattern);
        CHECK(block[0] == 0u);
        /* Stop in the middle of a block whose successor is already queued: the queued switch is withdrawn
         * and the unit stays on the pattern it was playing, ready for a stopped selection or a new start. */
        for (i = 0; i < 400u && s->queued == FM1_PATTERN_NONE; ++i) step();
        CHECK(s->queued != FM1_PATTERN_NONE && s->queued != s->active);
        stop_block = s->song_position;
        fm1_in.buttons |= 1u << BTN_PLAY; host_pressed |= 1u << BTN_PLAY; ui_tick();
        fm1_in.buttons &= ~(1u << BTN_PLAY); host_released |= 1u << BTN_PLAY;
        for (i = 0; i < 40u; ++i) ui_tick();
        printf("stopped in block %u: active pattern %u, queued %u\n", stop_block + 1u, s->active + 1u, s->queued);
        CHECK(!br_playing && !s->song_enabled && s->queued == FM1_PATTERN_NONE && !br_pattern_irq_edits_blocked());
        CHECK(s->active == s->entry[stop_block].pattern);
        /* PLAY again from HOME: the song starts over at block 0. */
        play_start();
        CHECK(br_playing && s->song_position == 0u && s->active == s->entry[0].pattern);
        n = run_bars(2, heard, block);
        CHECK(active_log[0] == s->entry[0].pattern);
        button(BTN_PLAY); for (i = 0; i < 40u; ++i) ui_tick();
        CHECK(!br_playing && s->queued == FM1_PATTERN_NONE);
    }
    /* Leaving and coming back keeps the view (not saved). */
    button(BTN_HOME); CHECK(br_ui.page == BR_PAGE_HOME);
    press(BTN_OCT_DOWN); press(BTN_SEQ); release(BTN_SEQ); release(BTN_OCT_DOWN);
    CHECK(br_ui.page == BR_PAGE_PS && view() == 1u);
    pattern_cue_test();
    icon_test();
    transition_case_tests();
    printf("rendered %u audio blocks, late blocks %u\n", blocks, br_pattern_irq_late_count());
    printf("pattern arranger screen: %u checks passed (record, insert, bars, pattern, remove, selection, loops, refusals, play, edit, save/load)\n", checks);
    return 0;
}
