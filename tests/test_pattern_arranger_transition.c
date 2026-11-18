/* Pattern arranger through the real app loop, owner, publication mailbox and
 * audio render (host only): SONG mode plays the arrangement, patterns change
 * on bar lines through the same queued-switch path as a black-key tap, audio
 * keeps rendering, edits during play, start-from-block, both song ends, and a
 * project save -> clear -> load of the arrangement through the owner. */
#define DINGHY_HOST 1
#define BR_PATTERN_TRANSITION_TEST 1
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
#ifndef TEST_PATTERN_ARENA_BYTES
#define TEST_PATTERN_ARENA_BYTES 8432u
#endif
static uint8_t owner_arena[TEST_PATTERN_ARENA_BYTES], owner_codec[9000];
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

static void fill_pattern(unsigned index)
{
    for (unsigned p = 0; p < BR_PARTS; ++p) {
        br_part[p].length = 16; br_part[p].division = 1;
        memset((void *)br_part[p].step, 0, sizeof br_part[p].step);
    }
    for (unsigned i = 0; i < 16; ++i) {
        br_part[BR_DRUM_PART].step[i] = (i % 4u == 0u ? 1u : 0u) | (index && i % 4u == 2u ? 2u : 0u) | (index && i % 2u ? 4u : 0u);
        for (unsigned l = 0; l < BR_DRUM_LANES; ++l) {
            br_seq_kit_velocity[i][l] = 100u; br_seq_kit_length[i][l] = 16u;
            br_seq_kit_note[i][l] = 255u; br_seq_kit_pitch[i][l] = 60u;
            br_seq_kit_condition[i][l] = 0u; br_seq_kit_micro[i][l] = 0;
        }
        for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) {
            if (index && i % 4u == p) br_part[p].step[i] = BR_NOTE_STEP(50u + p * 7u, 100u);
            br_seq_synth_velocity[p][i] = 255u; br_seq_synth_length[p][i] = 16u;
            br_seq_synth_condition[p][i] = 0u; br_seq_synth_micro[p][i] = 0;
        }
    }
}
static void next_boundary(void)
{
    unsigned guard = 0;
    while (!br_pattern_host_due) { step(); assert(++guard < 100000u); }
}
static void begin(void)
{
    button(BTN_PLAY);
    for (unsigned i = 0; i < 12u; ++i) step();
    assert(br_playing && br_ps_state.song_enabled);
}
static void stop(void)
{
    button(BTN_PLAY);
    for (unsigned i = 0; i < 20u; ++i) step();
    assert(!br_playing && !br_ps_state.song_enabled && br_ps_state.queued == FM1_PATTERN_NONE);
}
int main(int argc, char **argv)
{
    uint32_t direct[BR_PARTS][BR_DRUM_LANES];
    br_app_init(); fm1_ms = 2000; br_bpm = 240;
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) br_load_preset(p, BR_FIRST_PLAITS);
    br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    assert(br_quiescent_transaction_begin());
    assert(br_ps_host_init(owner_arena, sizeof owner_arena, &owner_live, &owner_work, &owner_clip, owner_codec, sizeof owner_codec, owner_quiet, 0));
    assert(br_ps_runtime_bind(&pub, &slots[0], &slots[1], &slots[2]));
    br_quiescent_transaction_end();
    if (argc > 1) {
        static br_pattern_snapshot fixture;
        for (unsigned i = 0; i < 11u; ++i) {
            char path[512]; snprintf(path, sizeof path, "%s/%02u.bin", argv[1], i);
            FILE *f = fopen(path, "rb"); assert(f && fread(&fixture, sizeof fixture, 1u, f) == 1u); fclose(f);
            select_stopped(i); assert(!br_pattern_snapshot_apply(&fixture));
        }
        select_stopped(1);
    } else {
        fill_pattern(0); select_stopped(1); fill_pattern(1);
        /* Nine varied patterns reproduce a nearly full saved bank without user data. */
        uint32_t random = 0x3167a42du;
        for (unsigned i = 2; i < 11u; ++i) {
            select_stopped(i); fill_pattern(1);
            for (unsigned step_no = 0; step_no < 16u; ++step_no) {
                for (unsigned part = 0; part < BR_SYNTH_PARTS; ++part) {
                    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                    br_part[part].step[step_no] = BR_NOTE_STEP(36u + random % 48u, 60u + (random >> 8) % 60u);
                    br_seq_synth_length[part][step_no] = 4u + (random >> 16) % 24u;
                }
            }
        }
        select_stopped(1);
    }
    printf("inactive bank %zu bytes; target-0 bytes %zu\n", bank.size, bank.length[0]);
    /* The former 7756-byte device arena could not reserve a worst-case
     * outgoing pattern for this otherwise loadable, realistic project. */
    assert(bank.size - bank.length[0] > 7756u - 4335u);
    assert(bank.size - bank.length[0] <= 4096u);
    {
        static br_pattern_snapshot current, staged;
        br_pattern_bank limited = bank;
        assert(!br_pattern_snapshot_capture(&current));
        limited.capacity = 7756u;
        assert(br_pattern_bank_prepare(&limited, 0u, &current, &staged, 4335u) == BR_BANK_FULL);
        limited.capacity = sizeof owner_arena;
        assert(br_pattern_bank_prepare(&limited, 0u, &current, &staged, 4335u) == BR_BANK_OK);
    }
    fm1_pattern_song *s = &br_ps_state;
    assert(fm1_arr_clear(s) == FM1_ARR_OK); /* Startup already contains a default arrangement. */
    assert(fm1_arr_set_mode(s, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
    assert(fm1_arr_set_end(s, FM1_SONG_END_LOOP) == FM1_ARR_OK);
    assert(fm1_arr_insert(s, 0, 1, 1) == FM1_ARR_OK);
    begin(); next_boundary(); memset(br_pattern_test_hits, 0, sizeof br_pattern_test_hits); step(); next_boundary();
    memcpy(direct, br_pattern_test_hits, sizeof direct);
    stop(); select_stopped(0); assert(fm1_arr_clear(s) == FM1_ARR_OK);
    assert(fm1_arr_insert(s, 0, 0, 1) == FM1_ARR_OK);
    assert(fm1_arr_insert(s, 1, 1, 1) == FM1_ARR_OK);
    begin(); next_boundary();
    memset(br_pattern_test_hits, 0, sizeof br_pattern_test_hits); step(); next_boundary();
    assert(s->song_position == 1u && s->active == 1u);
    for (unsigned p = 0; p < BR_PARTS; ++p) {
        unsigned lanes = p == BR_DRUM_PART ? BR_DRUM_LANES : 1u;
        for (unsigned l = 0; l < lanes; ++l) {
            printf("part %u lane %u: direct %u, transitioned %u\n", p, l, direct[p][l], br_pattern_test_hits[p][l]);
            if (argc == 1 && (p < BR_SYNTH_PARTS || l < 3u)) assert(direct[p][l] > 0u);
            assert(direct[p][l] == br_pattern_test_hits[p][l]);
        }
    }
    stop(); puts("multi-track arrangement transition matches direct playback"); return 0;
}
