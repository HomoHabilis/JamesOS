/* Pattern arranger screen through the real app (host only): the PATTERNS page's two views driven by
 * buttons, knobs and black keys (SMK-37 Pro: the pads reach the same keys), recording a song, cursor,
 * insert between blocks, bars and pattern, remove (OCT- + REC), selection (OCT- + knob 1) and loops
 * (knob 4), the engine's refusals as messages, PATTERN-mode keys, PLAY from the cursor with the audio
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
static void knob(unsigned k, int d) { host_encoder[ENC_KNOB1 + k] += d; tick(); }
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
static unsigned view(void) { return br_ui.screen[BR_PAGE_PS]; }
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

int main(void)
{
    br_app_init(); fm1_ms = 2000; br_bpm = 120;
    CHECK(br_quiescent_transaction_begin());
    CHECK(br_ps_host_init(owner_arena, sizeof owner_arena, &owner_live, &owner_work, &owner_clip, owner_codec, sizeof owner_codec, owner_quiet, 0));
    CHECK(br_ps_runtime_bind(&pub, &slots[0], &slots[1], &slots[2]));
    br_quiescent_transaction_end();
    CHECK(fm1_arr_clear(s) == FM1_ARR_OK);
    CHECK(fm1_arr_set_mode(s, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
    br_ui.page = BR_PAGE_HOME; br_ui.redraw = 1; shot("strip-home-empty-song");
    CHECK(br_song_strip_visible());
    CHECK(fb[24 * 240] == C_WHITE);
    CHECK(fm1_arr_set_mode(s, FM1_SONG_MODE_PATTERN) == FM1_ARR_OK);
    settle(100); CHECK(!br_song_strip_visible());
    CHECK(fm1_arr_insert(s, 0, 0, 2) == FM1_ARR_OK);
    CHECK(fm1_arr_insert(s, 1, 1, 2) == FM1_ARR_OK);
    CHECK(fm1_arr_insert(s, 2, 2, 2) == FM1_ARR_OK);
    CHECK(fm1_arr_loop_set(s, 0, 1, 3) == FM1_ARR_OK);
    CHECK(fm1_arr_set_mode(s, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
    br_ui.page = BR_PAGE_HOME; settle(200);
    CHECK(br_song_strip_visible());
    shot("strip-home-song-stopped");
    CHECK(fb[25 * 240 + 20] == br_arr_color(0));
    play_start(); CHECK(br_playing && s->song_enabled && br_song_strip_visible());
    const unsigned pages[] = { BR_PAGE_HOME, BR_PAGE_ENV, BR_PAGE_FX, BR_PAGE_MIX, BR_PAGE_PS };
    const char *names[] = { "strip-home", "strip-env", "strip-fx", "strip-mixer", "strip-arranger" };
    for (unsigned i = 0; i < sizeof pages / sizeof *pages; ++i) {
        br_ui.page = pages[i]; br_ui.screen[pages[i]] = pages[i] == BR_PAGE_PS ? 1 : 0; br_ui.redraw = 1;
        shot(names[i]);
        /* Each page has both the coloured song and the note/beat row. */
        CHECK(fb[25 * 240 + 20] == br_arr_color(0));
        CHECK(fb[25 * 240 + 220] == br_arr_color(2));
        for (unsigned x = 0; x < 240; ++x) CHECK((fb[27 * 240 + x] & ~0x40u) == 0u || fb[27 * 240 + x] == 0x20u);
        unsigned ink = 0; for (unsigned x = 6; x < 234; ++x) ink += fb[29 * 240 + x] != BR_BACKGROUND;
        CHECK(ink > 20);
    }
    /* The step row moves flush to the header when the song row is hidden. */
    {
        uint16_t lower[4 * 240];
        const unsigned compare_pages[] = { BR_PAGE_HOME, BR_PAGE_MIX };
        for (unsigned page = 0; page < 2; ++page) for (unsigned save = 0; save < 2; ++save) {
            br_ui.page = compare_pages[page]; br_top.save = save;
            cv_ox = cv_oy = 0; cv_begin(240, 8, BR_BACKGROUND); br_strip_art();
            memcpy(lower, cv_px + 4 * 240, sizeof lower);
            s->mode = FM1_SONG_MODE_PATTERN;
            cv_begin(240, 8, BR_BACKGROUND); br_strip_art();
            CHECK(!memcmp(lower, cv_px, sizeof lower));
            s->mode = FM1_SONG_MODE_SONG;
        }
        br_top.save = 0;
    }
    br_ui.page = BR_PAGE_HOME;
    for (unsigned banks = 1; banks <= 4; ++banks) {
        br_part[0].length = banks * 16u; br_ui.redraw = 1; settle(100);
        CHECK(fb[29 * 240] != BR_BACKGROUND);
        CHECK(fb[29 * 240 + 239] != BR_BACKGROUND);
    }
    es.mode = 1; shot("strip-save");
    CHECK(fb[25 * 240 + 220] == br_arr_color(2));
    es.mode = 0; br_ui.page = BR_PAGE_PS; br_ui.redraw = 1; settle(100);
    button(BTN_PLAY); settle(100); CHECK(!br_playing && br_song_strip_visible());
    br_arr.cursor = 3; br_ui.redraw = 1; shot("strip-arranger-stopped");
    CHECK(fb[25 * 240 + 20] == br_arr_color(0));
    CHECK(fb[25 * 240 + 80] == C_WHITE);
    br_arr.cursor = 5; br_ui.redraw = 1; settle(100);
    CHECK(fb[25 * 240 + 160] == C_WHITE);
    CHECK(fb[25 * 240 + 80] != C_WHITE);
    for (unsigned i = 0; i < sizeof pages / sizeof *pages; ++i) {
        br_ui.page = pages[i]; br_ui.redraw = 1; settle(100);
        CHECK(fb[25 * 240 + 20] == br_arr_color(0));
    }
    es.mode = 1; shot("strip-save-song-stopped");
    CHECK(fb[25 * 240 + 20] == br_arr_color(0));
    es.mode = 0; br_ui.page = BR_PAGE_HOME; br_ui.redraw = 1; settle(100);
    CHECK(fm1_arr_set_mode(s, FM1_SONG_MODE_PATTERN) == FM1_ARR_OK);
    br_ui.page = BR_PAGE_PS; br_ui.redraw = 1; settle(120);
    { unsigned ink = 0; for (unsigned x = 6; x < 234; ++x) ink += fb[24 * 240 + x] != C_BLACK; CHECK(ink > 20); }
    for (unsigned y = 28; y < 32; ++y)
        for (unsigned x = 0; x < 240; ++x) CHECK(fb[y * 240 + x] == C_BLACK);
    shot("strip-pattern-mode");
    br_ui.page = BR_PAGE_HOME; br_part[0].length = 16; br_ui.redraw = 1;
    shot("strip-home-stopped");
    /* A stopped transport does not show a playhead, even if position is step zero. */
    br_ui.cursor = BR_STEPS; br_step_pos[0] = 0; br_ui.redraw = 1; settle(100);
    for (unsigned x = 6; x < 19; ++x) CHECK(fb[24 * 240 + x] != C_WHITE);
    play_start(); CHECK(br_playing && !br_song_strip_visible());
    shot("strip-home-pattern-playing");
    br_ui.page = BR_PAGE_MIX; br_ui.redraw = 1; shot("strip-mixer-pattern-playing");
    es.mode = 1; shot("strip-save-pattern-playing"); es.mode = 0;
    printf("arrangement strip: %u checks passed\n", checks);
    return 0;
}
