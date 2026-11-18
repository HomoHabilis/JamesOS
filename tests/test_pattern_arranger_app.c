/* Pattern arranger through the real app loop, owner, publication mailbox and
 * audio render (host only): SONG mode plays the arrangement, patterns change
 * on bar lines through the same queued-switch path as a black-key tap, audio
 * keeps rendering, edits during play, start-from-block, both song ends, and a
 * project save -> clear -> load of the arrangement through the owner. */
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
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

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

static br_pattern_snapshot slots[3];
static br_pattern_irq_publication pub;
static uint8_t owner_arena[20000], owner_codec[9000];
static br_pattern_snapshot owner_live, owner_work, owner_clip;
static int owner_quiet(void *ctx) { (void)ctx; return !br_playing && br_diagnostic_restore_in_progress; }
static void ui_tick(void) { ++fm1_ms; br_app_tick(); }
static void button(unsigned b)
{
    fm1_in.buttons |= 1u << b; host_pressed |= 1u << b; ui_tick();
    fm1_in.buttons &= ~(1u << b); host_released |= 1u << b; ui_tick();
}
static void note_tap(unsigned k)
{
    fm1_in.notes |= 1u << k; host_note_pressed |= 1u << k; ui_tick();
    fm1_in.notes &= ~(1u << k); ui_tick();
}

/* One audio block, then one pass of the app loop (as the device interleaves them). */
static uint64_t energy;
static unsigned blocks;
static void step(void)
{
    int32_t out[BR_BLOCK * 2u] = {0};
    assert(app_audio(out, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    assert(!br_pattern_host_error);
    for (unsigned i = 0; i < BR_BLOCK * 2u; ++i) energy += (uint64_t)(out[i] < 0 ? -(int64_t)out[i] : out[i]);
    ++blocks;
    ui_tick();
}
/* Pattern k plays note 50 + k on track 1, step 1 of 16. */
static unsigned playing_note(void)
{
    return br_seq_note[0] >= 50u && br_seq_note[0] < 50u + FM1_PATTERN_COUNT ? br_seq_note[0] - 50u : 99u;
}
/* Run until `bars` more bar lines have passed (or the transport stops); for each bar
 * record the block, the active pattern and the pattern heard (track 1's note). */
typedef struct { unsigned block, active, heard, switched; uint64_t energy; } bar_log;
static unsigned run_bars(unsigned bars, bar_log *log)
{
    unsigned n = 0, start = br_pattern_irq_bar_count(), at = start, guard = 0;
    uint64_t e0 = energy;
    log[0].block = br_ps_state.song_position; log[0].active = br_ps_state.active; log[0].heard = 99u;
    while (br_playing && at - start < bars) {
        step();
        assert(++guard < 200000u);
        if (log[n].heard == 99u) log[n].heard = playing_note();
        if (br_pattern_irq_bar_count() != at) {
            log[n].energy = energy - e0; e0 = energy;
            at = br_pattern_irq_bar_count();
            ++n;
            /* A switch is consumed by audio exactly at the bar line: the gate is
             * up in the block that crossed it. */
            log[n].switched = br_pattern_irq_edits_blocked();
            /* Let the switch finish (and the playhead follow), then sample. */
            for (unsigned i = 0; i < 8u && br_playing; ++i) step();
            if (at - start >= bars) break;
            if (!br_playing) return n;      /* stopped at this bar line (song end) */
            log[n].block = br_ps_state.song_position; log[n].active = br_ps_state.active; log[n].heard = 99u;
        }
    }
    if (!br_playing && at - start < bars) { log[n].energy = energy - e0; ++n; }
    return n;
}
static void print_log(const char *name, const bar_log *log, unsigned n)
{
    printf("%s:", name);
    for (unsigned i = 0; i < n; ++i) printf(" [b%u p%u heard %u]", log[i].block, log[i].active, log[i].heard);
    printf("\n");
}
static void expect_patterns(const bar_log *log, unsigned n, const unsigned *want, unsigned count)
{
    if (n != count) { fprintf(stderr, "bars: got %u want %u\n", n, count); assert(0); }
    for (unsigned i = 0; i < n; ++i) {
        if (log[i].active != want[i] || log[i].heard != want[i]) {
            fprintf(stderr, "bar %u: active %u heard %u want %u\n", i, log[i].active, log[i].heard, want[i]); assert(0);
        }
        assert(log[i].energy > 0u);
        if (i && log[i].switched != (want[i] != want[i - 1])) {
            fprintf(stderr, "bar %u: switch at bar line %u, want %u\n", i, log[i].switched, want[i] != want[i - 1]); assert(0);
        }
    }
}
static void select_stopped(unsigned k)
{
    assert(!br_playing && br_quiescent_transaction_begin());
    assert(br_ps_select(k));
    br_quiescent_transaction_end();
}
static int session_valid(const uint8_t *p, size_t n, void *ctx) { (void)ctx; return n == FM1_PROJECT_SESSION && p[0] == 0x5a; }

int main(void)
{
    static bar_log log[64];
    unsigned n;
    br_app_init(); fm1_ms = 2000; br_bpm = 240;
    br_load_preset(0, BR_FIRST_PLAITS);
    br_ui.part = 0; br_ui.page = BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ] = 0;
    assert(br_quiescent_transaction_begin());
    assert(br_ps_host_init(owner_arena, sizeof owner_arena, &owner_live, &owner_work, &owner_clip, owner_codec, sizeof owner_codec, owner_quiet, 0));
    assert(br_ps_state.song_length == 1u && br_ps_state.entry[0].pattern == 0u && br_ps_state.entry[0].bars == br_pattern_snapshot_bars(&owner_live));
    assert(fm1_arr_clear(&br_ps_state) == FM1_ARR_OK);
    assert(br_ps_runtime_bind(&pub, &slots[0], &slots[1], &slots[2]));
    br_quiescent_transaction_end();
    /* Patterns 0..4: track 1 plays note 50 + k on its first step. */
    for (unsigned k = 0; k < 5u; ++k) {
        select_stopped(k);
        memset((void *)br_part[0].step, 0, sizeof br_part[0].step);
        br_part[0].length = 16; br_part[0].division = 1;
        br_part[0].step[0] = BR_NOTE_STEP(50u + k, 100u); br_seq_synth_velocity[0][0] = 255;
        br_seq_synth_length[0][0] = 64;
    }
    assert(br_ps_state.active == 4u && br_ps_state.used == 0x1fu);

    /* PATTERN mode (default) with blocks: PLAY behaves as today. */
    fm1_pattern_song *s = &br_ps_state;
    assert(fm1_arr_insert(s, 0, 0, 1) == FM1_ARR_OK && fm1_arr_insert(s, 1, 1, 2) == FM1_ARR_OK);
    assert(fm1_arr_insert(s, 2, 2, 1) == FM1_ARR_OK && fm1_arr_insert(s, 3, 3, 1) == FM1_ARR_OK);
    assert(fm1_arr_loop_set(s, 1, 2, 2) == FM1_ARR_OK);
    assert(br_arranger_play(0) == FM1_ARR_MODE);
    button(BTN_PLAY); assert(br_playing && !s->song_enabled && s->active == 4u);
    n = run_bars(2, log); print_log("pattern mode", log, n);
    { static const unsigned want[] = {4, 4}; expect_patterns(log, n, want, 2); }
    button(BTN_PLAY); step(); step(); assert(!br_playing);

    /* SONG mode, STOP end, plain PLAY: block 0's pattern is selected while stopped
     * first (no block of pattern 4 is played), then the song runs and stops. */
    assert(fm1_arr_set_mode(s, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
    {
        unsigned before = blocks;
        fm1_in.buttons |= 1u << BTN_PLAY; host_pressed |= 1u << BTN_PLAY; ui_tick();
        assert(!br_playing && s->active == 0u && !s->song_enabled);   /* withdrawn under the mask, selected */
        fm1_in.buttons &= ~(1u << BTN_PLAY); host_released |= 1u << BTN_PLAY; ui_tick();
        assert(br_playing && s->song_enabled && s->song_position == 0u && s->active == 0u);
        assert(blocks == before);
    }
    n = run_bars(20, log); print_log("song stop", log, n);
    { static const unsigned want[] = {0, 1, 1, 2, 1, 1, 2, 3}; expect_patterns(log, n, want, 8); }
    for (unsigned i = 0; i < 8u; ++i) step();
    assert(!br_playing && !s->song_enabled && s->song_position == 3u && s->active == 3u);
    assert(!strcmp(br_ui.message, "SONG END"));

    /* Start from a block (the cursor), LOOP end: inside the loop, then around. */
    assert(fm1_arr_set_end(s, FM1_SONG_END_LOOP) == FM1_ARR_OK);
    assert(br_arranger_play(4) == FM1_ARR_RANGE);
    assert(br_arranger_play(2) == FM1_ARR_OK);
    ui_tick(); ui_tick(); ui_tick();
    assert(br_playing && s->song_position == 2u && s->active == 2u);
    assert(br_arranger_play(0) == FM1_ARR_BUSY);
    n = run_bars(9, log); print_log("song loop from 2", log, n);
    { static const unsigned want[] = {2, 1, 1, 2, 3, 0, 1, 1, 2}; expect_patterns(log, n, want, 9); }

    /* Pattern selection is refused while the arrangement drives the patterns. */
    for (unsigned i = 0; i < 4u; ++i) step();          /* past the switch gate */
    assert(!br_pattern_irq_edits_blocked());
    assert(br_ps_ui_select(5u)); assert(s->queued != 5u);
    assert(!strcmp(br_ui.message, "SONG PLAYING"));

    /* Edits during play. The run ended on the bar line back into the loop:
     * block 1 (pattern 1, 2 bars), loop pass 1. Insert a block in front: the
     * playhead follows its block, and the song plays on, wrapping into the new
     * block 0 (LOOP end). */
    assert(s->song_position == 1u && s->pass == 1u && s->bars_elapsed == 0u);
    assert(fm1_arr_insert(s, 0, 4, 1) == FM1_ARR_OK && s->song_position == 2u && s->entry[2].pattern == 1u && s->pass == 1u);
    n = run_bars(4, log); print_log("after insert", log, n);
    { static const unsigned want[] = {1, 1, 2, 3}; expect_patterns(log, n, want, 4); }
    /* Now entering block 0 (pattern 4, the inserted block). Remove the playing
     * block: its bar finishes, then the next surviving block (pattern 0) starts. */
    assert(s->song_position == 0u && s->entry[0].pattern == 4u);
    assert(fm1_arr_remove(s, 0) == FM1_ARR_OK && s->pre && s->song_position == 0u);
    n = run_bars(3, log); print_log("after remove", log, n);
    { static const unsigned want[] = {4, 0, 1}; expect_patterns(log, n, want, 3); }
    /* Changing the very next block's pattern during the bar before it is too
     * late for that bar line: its pattern is already decoded and queued. */
    assert(s->song_position == 1u && s->bars_elapsed == 1u && s->queued == 2u);
    assert(fm1_arr_set_pattern(s, 2, 4) == FM1_ARR_OK);
    n = run_bars(3, log); print_log("set pattern, late", log, n);
    { static const unsigned want[] = {1, 2, 1}; expect_patterns(log, n, want, 3); }
    /* On its next play block 2 has the new pattern. A change to a block that is
     * not next yet (block 3) is honoured at its bar line. */
    assert(s->song_position == 1u && s->bars_elapsed == 1u && s->pass == 1u && s->queued == 4u);
    assert(fm1_arr_set_pattern(s, 3, 0) == FM1_ARR_OK);
    n = run_bars(3, log); print_log("set pattern", log, n);
    { static const unsigned want[] = {1, 4, 0}; expect_patterns(log, n, want, 3); }
    /* PATTERN mode while playing hands the transport back. The switch already
     * queued for this bar line (block 1, pattern 1) still lands; then the
     * pattern stays. */
    assert(s->song_position == 0u && s->queued == 1u);
    assert(fm1_arr_set_mode(s, FM1_SONG_MODE_PATTERN) == FM1_ARR_OK);
    n = run_bars(3, log); print_log("back to pattern mode", log, n);
    { static const unsigned want[] = {0, 1, 1}; expect_patterns(log, n, want, 3); }
    assert(!s->song_enabled && br_playing);
    br_ui.page = BR_PAGE_PS; note_tap(1); br_ui.page = BR_PAGE_SEQ;
    assert(s->queued == 0u);                                          /* taps work again */
    n = run_bars(2, log); print_log("tap", log, n);
    { static const unsigned want[] = {1, 0}; expect_patterns(log, n, want, 2); }
    button(BTN_PLAY); for (unsigned i = 0; i < 8u; ++i) step();
    assert(!br_playing && !br_pattern_irq_edits_blocked() && s->queued == FM1_PATTERN_NONE);
    printf("rendered %u blocks, late blocks %u\n", blocks, br_pattern_irq_late_count());

    /* Save -> clear -> load through the owner's export/adopt (the project path). */
    {
        static uint8_t session[FM1_PROJECT_SESSION], restored[FM1_PROJECT_SESSION], work_session[FM1_PROJECT_SESSION];
        static uint8_t wire[FM1_PROJECT_MAX], stage_arena[20000], work_arena[20000], comp[FM1_PROJECT_RECORD_MAX];
        static br_pattern_snapshot work_pattern, live, staged_live;
        static br_pattern_bank view, staged;
        static fm1_project_work work;
        fm1_pattern_song exported, loaded, saved;
        size_t len = 0;
        assert(fm1_arr_set_mode(s, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
        saved = *s;
        session[0] = 0x5a;
        work.compressed = comp; work.compressed_capacity = sizeof comp; work.session = work_session;
        work.arena = work_arena; work.arena_capacity = sizeof work_arena; work.pattern = &work_pattern;
        assert(br_quiescent_transaction_begin());
        assert(br_ps_host_export(&view, &live, &exported));
        assert(fm1_project_encode(session, &view, &live, &exported, session_valid, 0, &work, wire, sizeof wire, &len) == FM1_PROJECT_OK);
        br_quiescent_transaction_end();
        assert(fm1_arr_clear(s) == FM1_ARR_OK && fm1_arr_set_mode(s, 0) == FM1_ARR_OK && fm1_arr_set_end(s, 0) == FM1_ARR_OK);
        assert(!s->song_length);
        assert(fm1_project_decode(wire, len, session_valid, 0, &work, restored, &staged, stage_arena, sizeof stage_arena, &staged_live, &loaded) == FM1_PROJECT_OK);
        assert(br_quiescent_transaction_begin());
        assert(br_ps_host_adopt_copy(&staged, &staged_live, &loaded));
        br_quiescent_transaction_end();
        assert(s->song_length == saved.song_length && !memcmp(s->entry, saved.entry, sizeof saved.entry));
        assert(s->loop_count == saved.loop_count && !memcmp(s->loop, saved.loop, sizeof saved.loop));
        assert(s->mode == FM1_SONG_MODE_SONG && s->end == FM1_SONG_END_LOOP && !s->song_enabled);
        /* The loaded song plays. */
        assert(br_arranger_play(0) == FM1_ARR_OK);
        ui_tick(); ui_tick(); ui_tick();
        assert(br_playing && s->song_enabled && s->active == s->entry[0].pattern);
        n = run_bars(2, log); print_log("loaded song", log, n);
        { unsigned want[2] = {s->entry[0].pattern, s->entry[1].pattern}; expect_patterns(log, n, want, 2); }
    }
    puts("pattern arranger app: song playback, edits, ends and save/load passed");
    return 0;
}
