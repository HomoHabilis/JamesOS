/* Sequencer behaviour audit (FM-1 and SMK-37 Pro: see SMK below). Built by tests/run_seq_behaviour.py against
 * a copy of firmware/src whose instrument_audio.c carries observation hooks
 * (seqtest_*). The hooks record every sequencer trigger with its time in
 * samples; they change nothing. Input goes through the real key/button/encoder
 * path (fm1_in, host_* edges, br_app_tick) at 1 ms ticks with audio rendered
 * in 256-frame blocks, exactly as tests/host.c does.
 *
 * Output lines:
 *   PASS  test: what held
 *   BUG   id test: a defect reproduced (the check describes the wrong behaviour;
 *         it prints BUG-FIXED? instead once the behaviour changes)
 *   NOTE  test: a measurement
 *   FAIL  test: an invariant that should hold did not (exit status 1)
 * An optional argument limits the run to comma-separated test names. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { E_KIT = 1, E_SYN, E_SUS, E_OFF, E_CUT, E_VOICE, E_RDUE };
typedef struct { uint8_t kind, part, a, vel, retrig, b; uint64_t t, block; } st_ev;
#define ST_MAX 400000u
static st_ev st_log[ST_MAX];
static uint32_t st_n, st_overflow;
static uint64_t st_frame;                 /* first frame of the block being rendered */
static uint32_t st_off[4];                /* sequencer loop offset within the block, per part */
static uint64_t st_rdue[4];
static uint32_t seqtest_retrig_firing;
static void st_push(uint32_t kind, uint32_t part, uint32_t a, uint32_t vel, uint32_t retrig,
                    uint32_t b, uint64_t t)
{
    if (st_n >= ST_MAX) { st_overflow = 1u; return; }
    st_ev *e = &st_log[st_n++];
    e->kind = (uint8_t)kind; e->part = (uint8_t)part; e->a = (uint8_t)a; e->vel = (uint8_t)vel;
    e->retrig = (uint8_t)retrig; e->b = (uint8_t)b; e->t = t; e->block = st_frame;
}
static void seqtest_at(uint32_t p, uint32_t off) { if (p < 4u) st_off[p] = off; }
static void seqtest_retrig_due(uint32_t p, uint32_t lane, uint32_t off)
{ if (lane < 4u) { st_rdue[lane] = st_frame + off; st_push(E_RDUE, p, lane, 0, 0, 0, st_frame + off); } }
static void seqtest_kit(uint32_t p, uint32_t lane, uint32_t voice, uint32_t vel, uint32_t pitch)
{
    uint32_t r = seqtest_retrig_firing; seqtest_retrig_firing = 0u; (void)voice;
    st_push(E_KIT, p, lane, vel, r, pitch, r && lane < 4u ? st_rdue[lane] : st_frame + st_off[p & 3u]);
}
static void seqtest_synth(uint32_t p, uint32_t note, uint32_t vel, uint32_t sustain)
{ st_push(sustain ? E_SUS : E_SYN, p, note, vel, 0, 0, st_frame + st_off[p & 3u]); }
static void seqtest_off(uint32_t p, uint32_t note, uint32_t cut)
{ st_push(cut ? E_CUT : E_OFF, p, note, 0, 0, 0, st_frame + st_off[p & 3u]); }
static void seqtest_voice(uint32_t p, uint32_t note, uint32_t vel)
{ st_push(E_VOICE, p, note, vel, 0, 0, st_frame); }

#define DINGHY_HOST 1
#define MIDI_OUT_TEST_CAPTURE 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
/* Product: the FM-1 piano-key step grid (inert black keys, the key-LED scheme, another key behind a
 * held step sets its pitch, br_step_pitch_*) is FM-1 only. The SMK-37 keeps its own scheme: pads are
 * steps (the panel turns each pad into a white-key event, SMK_WHITE_NOTE), the keybed sets the
 * insertion pitch (tonal) or picks the kit lane (black keys), and keys behind a held step do nothing.
 * Host builds compile the SMK-37 control scheme without the device-only panel scanning
 * (smk37_panel.c: pads, outer keys, faders), so here a white key stands for a pad and a black key for
 * the keybed key the panel forwards; the keybed white keys (pitch only, intercepted by the panel)
 * and the pad lights cannot be exercised on the host. */
#if BR_PRODUCT_SMK37
#define SMK 1
#define STEP_PITCH_KEY 0u
#else
#define SMK 0
#define STEP_PITCH_KEY br_step_pitch_key
#endif
#ifdef SEQTEST_PATTERN_STUBS
/* The same stand-in the SMK-37 tree's render_screens.py --smk-preview uses:
 * slot state only, so the pattern page and its gestures can be driven. */
fm1_pattern_song br_ps_state = {.used = 7, .queued = FM1_PATTERN_NONE};
static uint32_t stub_selected = 255u, stub_copied = 255u, stub_pasted = 255u;
int br_ps_select(uint32_t target) { stub_selected = target; br_ps_state.active = (uint8_t)target; return 1; }
int br_ps_copy_pattern(uint32_t target) { stub_copied = target; return 1; }
int br_ps_paste_pattern(uint32_t target) { stub_pasted = target; return 1; }
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SLOW 1
#endif
#endif
#if !defined(SLOW) && defined(__SANITIZE_ADDRESS__)
#define SLOW 1
#endif
#ifndef SLOW
#define SLOW 0
#endif

/* ---------------------------------------------------------------- harness */
static uint32_t fails, bugs, passes;
static const char *cur;
#define PASS(...) do { ++passes; printf("PASS  %s: ", cur); printf(__VA_ARGS__); putchar('\n'); } while (0)
#define FAILF(...) do { ++fails; printf("FAIL  %s: ", cur); printf(__VA_ARGS__); putchar('\n'); } while (0)
#define CHECK(c, ...) do { if (c) PASS(__VA_ARGS__); else FAILF(__VA_ARGS__); } while (0)
#define NOTE(...) do { printf("NOTE  %s: ", cur); printf(__VA_ARGS__); putchar('\n'); } while (0)
/* A reproduced defect: cond is true while the defect is present. */
#define BUG(id, cond, ...) do { if (cond) { ++bugs; printf("BUG   %s %s: ", id, cur); } \
    else printf("BUG-FIXED? %s %s: ", id, cur); printf(__VA_ARGS__); putchar('\n'); } while (0)

static const uint8_t W[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t B[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static uint64_t total_frames;
static uint32_t owed;

static void render_block(void)
{
    int32_t out[BR_BLOCK * 2u];
    memset(out, 0, sizeof out);
    st_frame = total_frames;
    if (app_audio(out, BR_BLOCK) != FM1_AUDIO_RENDER_OK) { FAILF("render failed"); exit(1); }
    total_frames += BR_BLOCK;
}
static void render_frames(uint64_t frames) { uint64_t end = total_frames + frames; while (total_frames < end) render_block(); }
/* One millisecond as tests/host.c runs it: audio owed, then one UI tick. */
static void run_ms(uint32_t ms)
{
    while (ms--) {
        fm1_ms++;
        owed += FS;
        while (owed >= 1000u * BR_BLOCK) { owed -= 1000u * BR_BLOCK; render_block(); }
        br_app_tick();
    }
}
static void key_down(uint32_t k) { uint32_t b = 1u << k; host_note_pressed |= b & ~fm1_in.notes; fm1_in.notes |= b; }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u << k); }
static void tap_key(uint32_t k) { key_down(k); run_ms(60); key_up(k); run_ms(40); }
static void btn_down(uint32_t b) { fm1_in.buttons |= 1u << b; host_pressed |= 1u << b; }
static void btn_up(uint32_t b) { fm1_in.buttons &= ~(1u << b); host_released |= 1u << b; }
static void tap_btn(uint32_t b) { btn_down(b); run_ms(1); btn_up(b); run_ms(40); }
static void turn(uint32_t enc, int32_t d)
{
    int32_t s = d < 0 ? -1 : 1;
    while (d) { host_encoder[enc] += s; d -= s; run_ms(20); }
}
static void fresh(void)
{
    host_store[0] = 0; host_store_len = 0;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    host_pressed = host_released = host_note_pressed = 0;
    memset(host_encoder, 0, sizeof host_encoder);
    br_app_init(); fm1_ms = 2000u; owed = 0u; total_frames = 0u;
    run_ms(5);
    st_n = 0; st_overflow = 0;
}
static void select_track(uint32_t part) { int32_t d = (int32_t)part - (int32_t)br_ui.part; if (d) turn(ENC_ALGORITHM, d); }
static void open_seq_grid(void)
{
    if (br_ui.page != BR_PAGE_SEQ || br_ui.screen[BR_PAGE_SEQ]) tap_btn(BTN_SEQ);
    if (br_ui.screen[BR_PAGE_SEQ]) tap_btn(BTN_SEQ);
}
/* Kit lanes are picked with the SELECT dial on the grid (black keys are inert there). */
static void select_lane(uint32_t lane) { turn(ENC_SELECT, (int32_t)lane - (int32_t)br_ui.lane); }
static uint32_t on(uint32_t p, uint32_t s, uint32_t lane)
{ return p == BR_DRUM_PART ? (BR_STEP_HIT(br_part[p].step[s]) >> lane) & 1u : !!(br_part[p].step[s] & BR_STEP_ON); }
static uint32_t count_on(uint32_t p, uint32_t lane)
{ uint32_t n = 0; for (uint32_t s = 0; s < BR_STEPS; ++s) n += on(p, s, lane); return n; }

/* All per-step sequencer state of one part, for "nothing else changed" checks. */
typedef struct {
    uint32_t step[BR_STEPS];
    uint16_t slen[BR_STEPS]; uint8_t scond[BR_STEPS], snote[BR_STEPS], svel[BR_STEPS]; int8_t smicro[BR_STEPS];
    uint16_t klen[BR_STEPS][4]; uint8_t kcond[BR_STEPS][4], kvel[BR_STEPS][4], kret[BR_STEPS][4], kpitch[BR_STEPS][4]; int8_t kmicro[BR_STEPS][4];
} snap_t;
static void snap(uint32_t p, snap_t *s)
{
    memset(s, 0, sizeof *s);
    for (uint32_t i = 0; i < BR_STEPS; ++i) {
        s->step[i] = br_part[p].step[i];
        if (p < BR_SYNTH_PARTS) {
            s->slen[i] = br_seq_synth_length[p][i]; s->scond[i] = br_seq_synth_condition[p][i];
            s->snote[i] = br_seq_synth_note[p][i]; s->svel[i] = br_seq_synth_velocity[p][i];
            s->smicro[i] = br_seq_synth_micro[p][i];
        } else for (uint32_t l = 0; l < 4; ++l) {
            s->klen[i][l] = br_seq_kit_length[i][l]; s->kcond[i][l] = br_seq_kit_condition[i][l];
            s->kvel[i][l] = br_seq_kit_velocity[i][l]; s->kret[i][l] = br_seq_kit_retrig[i][l];
            s->kpitch[i][l] = br_seq_kit_pitch[i][l]; s->kmicro[i][l] = br_seq_kit_micro[i][l];
        }
    }
}
/* Number of (step, lane) cells that differ, ignoring one cell. */
static uint32_t snap_diff(const snap_t *a, const snap_t *b, uint32_t p, uint32_t skip_step, uint32_t skip_lane)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < BR_STEPS; ++i) {
        if (p < BR_SYNTH_PARTS) {
            if (i == skip_step) continue;
            n += a->step[i] != b->step[i] || a->slen[i] != b->slen[i] || a->scond[i] != b->scond[i] ||
                 a->snote[i] != b->snote[i] || a->svel[i] != b->svel[i] || a->smicro[i] != b->smicro[i];
        } else for (uint32_t l = 0; l < 4; ++l) {
            uint32_t mask = (1u << l) | (1u << (l + 8u));
            if (i == skip_step && l == skip_lane) continue;
            n += (a->step[i] & mask) != (b->step[i] & mask) || a->klen[i][l] != b->klen[i][l] ||
                 a->kcond[i][l] != b->kcond[i][l] || a->kvel[i][l] != b->kvel[i][l] ||
                 a->kret[i][l] != b->kret[i][l] || a->kpitch[i][l] != b->kpitch[i][l] ||
                 a->kmicro[i][l] != b->kmicro[i][l];
        }
    }
    return n;
}

/* Direct pattern setup for playback tests (bypasses the UI on purpose). */
static void clear_all_steps(void)
{
    for (uint32_t p = 0; p < BR_PARTS; ++p) {
        for (uint32_t s = 0; s < BR_STEPS; ++s) {
            br_step_trigger_clear(p, s, 0u);
            if (p == BR_DRUM_PART) for (uint32_t l = 1; l < 4; ++l) br_step_trigger_clear(p, s, l);
            br_part[p].step[s] = 0u;
        }
        br_part[p].length = 16u; br_part[p].division = 1u;
    }
    br_swing = 0u;
}
static void synth_step(uint32_t p, uint32_t s, uint32_t note, uint32_t len16, uint32_t cond)
{
    br_part[p].step[s] = BR_NOTE_STEP(note, 100u);
    br_seq_synth_velocity[p][s] = 255u; br_seq_synth_note[p][s] = 255u;
    br_seq_synth_length[p][s] = (uint16_t)len16; br_seq_synth_condition[p][s] = (uint8_t)cond;
    br_seq_synth_micro[p][s] = 0;
}
static void kit_step(uint32_t s, uint32_t lane, uint32_t pitch, uint32_t cond, uint32_t retrig)
{
    br_part[BR_DRUM_PART].step[s] |= 1u << lane;
    br_seq_kit_velocity[s][lane] = 100u; br_seq_kit_pitch[s][lane] = (uint8_t)pitch;
    br_seq_kit_condition[s][lane] = (uint8_t)cond; br_seq_kit_retrig[s][lane] = (uint8_t)retrig;
    br_seq_kit_length[s][lane] = 0u; br_seq_kit_micro[s][lane] = 0;
}
static void play_reset(void) { br_playing = 0u; render_block(); render_block(); st_n = 0; }
static void play_start(void) { br_playing = 1u; }
/* Collect events of one kind and part (and lane for kit events; 255 = any). */
static uint32_t collect(uint32_t kind, uint32_t part, uint32_t lane, st_ev *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < st_n && n < cap; ++i)
        if (st_log[i].kind == kind && st_log[i].part == part && (lane == 255u || st_log[i].a == lane))
            out[n++] = st_log[i];
    return n;
}
static st_ev tmp[200000];

/* -------------------------------------------------------------- tests */

/* 1. Toggling every step in every bank, tonal and drum, through the keys. */
static void t_toggle(void)
{
    fresh(); select_track(0); open_seq_grid();
    /* B1: OCT+ wraps through the banks in use. */
    tap_btn(BTN_OCT_UP);
    uint32_t bank = br_ui.step_bank;
    br_part[0].length = 17u; tap_btn(BTN_OCT_UP); tap_btn(BTN_OCT_UP);
    uint32_t bank17 = br_ui.step_bank;
    br_part[0].length = 16u; tap_btn(BTN_OCT_DOWN); run_ms(5);
    CHECK(bank == 0u && bank17 == 0u && br_ui.step_bank == 0u,
          "OCT+ wraps banks: length 16 stays on bank %u, length 17 returns to bank %u",
          bank + 1u, bank17 + 1u);
    /* Settings page, STEPS to 64. */
    tap_btn(BTN_SEQ);
    CHECK(br_ui.screen[BR_PAGE_SEQ] == 1u, "second SEQ tap opens the settings page");
    turn(ENC_KNOB1, 48);
    CHECK(br_part[0].length == 64u, "STEPS knob sets length 64 (got %u)", br_part[0].length);
    tap_btn(BTN_SEQ);
    CHECK(br_ui.screen[BR_PAGE_SEQ] == 0u, "SEQ from settings returns to the grid");
    for (uint32_t b = 0; b < 4; ++b) {
        while (br_ui.step_bank < b) tap_btn(BTN_OCT_UP);
        for (uint32_t i = 0; i < 16; ++i) tap_key(W[i]);
    }
    uint32_t all = count_on(0, 0) == 64u;
    CHECK(all, "tonal: 64 white-key taps over four banks set all 64 steps (%u on)", count_on(0, 0));
    for (uint32_t b = 4; b-- > 0;) {
        while (br_ui.step_bank > b) tap_btn(BTN_OCT_DOWN);
        for (uint32_t i = 0; i < 16; ++i) tap_key(W[i]);
    }
    CHECK(count_on(0, 0) == 0u, "tonal: tapping them again clears all 64 (%u left)", count_on(0, 0));
    /* Black keys on a tonal grid do nothing: no step, no insertion pitch, no sound. */
    uint32_t before = br_ui.seq_default[0][0].note, before_note = br_ui.note[0], sounded = 0;
    uint32_t n0 = st_n;
    for (uint32_t i = 0; i < 11; ++i) tap_key(B[i]);
    for (uint32_t i = n0; i < st_n; ++i) sounded += st_log[i].kind == E_VOICE && st_log[i].part == 0;
#if SMK
    /* SMK-37: a keybed key sets the insertion pitch (53 + key + 12 x octave) and is heard (it plays
     * as on any page: docs/SMK37-PORT.md, tests/test_smk_seq_keys.c); it sets no step. */
    (void)before_note;
    before = 53u + B[10];
    CHECK(count_on(0, 0) == 0u && br_ui.seq_default[0][0].note == before && br_ui.note[0] == before && sounded == 11u,
          "SMK-37 tonal grid, nothing held: keybed keys set no step, each is heard (%u of 11); the last sets the "
          "insertion pitch (%u)", sounded, br_ui.seq_default[0][0].note);
#else
    CHECK(count_on(0, 0) == 0u && br_ui.seq_default[0][0].note == before && br_ui.note[0] == before_note && !sounded,
          "tonal grid, nothing held: black keys set no step, leave the insertion pitch (%u) and sound nothing", before);
#endif
    /* A10: once a held step is an edit, another white key sets its pitch and sounds it; a black key does nothing. */
    tap_key(W[3]);
    key_down(W[3]); run_ms(400);
    uint32_t heard = 0;
    n0 = st_n;
    tap_key(W[5]); uint32_t white = br_step_note_get(0, 3, 0);
    tap_key(B[0]); uint32_t black = br_step_note_get(0, 3, 0);
    for (uint32_t i = n0; i < st_n; ++i)
        if (st_log[i].kind == E_VOICE && st_log[i].part == 0) heard |= st_log[i].a == 53u + W[5] ? 1u : 2u;
    key_up(W[3]); run_ms(20);
#if SMK
    /* SMK-37: keys behind a held step do nothing (the held-step lock swallows them): no pitch, no sound. */
    CHECK(white == before && black == before && heard == 0u && on(0, 3, 0) && count_on(0, 0) == 1u &&
          !br_ui.step_edit_active && br_ui.step_press_key == BR_STEP_NO_PRESS &&
          br_ui.seq_default[0][0].note == before,
          "SMK-37 tonal: hold step 4, tap another pad and a keybed key: the step keeps its pitch (%u, %u), "
          "nothing sounds, no other step changes and release ends the edit", white, black);
#else
    CHECK(white == 53u + W[5] && black == white && heard == 1u && on(0, 3, 0) && count_on(0, 0) == 1u &&
          !br_ui.step_edit_active && br_ui.step_press_key == BR_STEP_NO_PRESS && !STEP_PITCH_KEY &&
          br_ui.seq_default[0][0].note == before,
          "tonal: hold step 4, tap a white then a black key: the white sets and sounds the pitch (%u), the black "
          "changes nothing (%u), no other step changes and release ends the edit", white, black);
#endif
    tap_key(W[3]);

    /* Drum kit: four lanes x 64 steps. */
    select_track(BR_DRUM_PART); open_seq_grid();
    tap_btn(BTN_SEQ); turn(ENC_KNOB1, 48); tap_btn(BTN_SEQ);
    CHECK(br_part[BR_DRUM_PART].length == 64u, "kit length 64");
    uint32_t ok = 1;
    for (uint32_t lane = 0; lane < 4; ++lane) {
        select_lane(lane);
        if (br_ui.lane != lane) ok = 0;
        for (uint32_t b = 0; b < 4; ++b) {
            while (br_ui.step_bank < b) tap_btn(BTN_OCT_UP);
            while (br_ui.step_bank > b) tap_btn(BTN_OCT_DOWN);
            for (uint32_t i = 0; i < 16; ++i) tap_key(W[i]);
        }
        for (uint32_t l = 0; l < 4; ++l)
            if (count_on(BR_DRUM_PART, l) != (l <= lane ? 64u : 0u)) ok = 0;
        while (br_ui.step_bank) tap_btn(BTN_OCT_DOWN);
    }
    CHECK(ok, "kit: SELECT picks lanes 1-4; each lane's 64 steps toggle on without touching other lanes");
    uint32_t lane_before = br_ui.lane, kit_default = br_ui.seq_default[BR_DRUM_PART][lane_before].note;
    snap_t ka, kb; snap(BR_DRUM_PART, &ka);
#if SMK
    /* SMK-37: the first four black keys of the keybed pick the kit lane; the others do nothing. */
    {
        uint32_t picked = 1u;
        for (uint32_t i = 0; i < 11; ++i) {
            uint32_t want = i < 4u ? i : br_ui.lane;
            tap_key(B[i]);
            picked &= br_ui.lane == want;
        }
        snap(BR_DRUM_PART, &kb);
        tap_key(B[lane_before]);
        CHECK(picked && br_ui.lane == lane_before && snap_diff(&ka, &kb, BR_DRUM_PART, BR_STEPS, 4u) == 0u &&
              br_ui.seq_default[BR_DRUM_PART][lane_before].note == kit_default,
              "SMK-37 kit: keybed black keys 1-4 pick lanes 1-4, the other 7 do nothing; no step changes");
    }
#else
    for (uint32_t i = 0; i < 11; ++i) tap_key(B[i]);
    snap(BR_DRUM_PART, &kb);
    CHECK(br_ui.lane == lane_before && snap_diff(&ka, &kb, BR_DRUM_PART, BR_STEPS, 4u) == 0u &&
          br_ui.seq_default[BR_DRUM_PART][lane_before].note == kit_default,
          "kit: all 11 black keys do nothing (no lane change, no step change)");
#endif
    for (uint32_t lane = 0; lane < 4; ++lane) {
        select_lane(lane);
        for (uint32_t b = 0; b < 4; ++b) {
            while (br_ui.step_bank < b) tap_btn(BTN_OCT_UP);
            while (br_ui.step_bank > b) tap_btn(BTN_OCT_DOWN);
            for (uint32_t i = 0; i < 16; ++i) tap_key(W[i]);
        }
        while (br_ui.step_bank) tap_btn(BTN_OCT_DOWN);
    }
    CHECK(count_on(BR_DRUM_PART, 0) + count_on(BR_DRUM_PART, 1) + count_on(BR_DRUM_PART, 2) +
          count_on(BR_DRUM_PART, 3) == 0u, "kit: all 256 lane steps toggle back off");
    /* OCT- acts on release on this page, OCT+ on press. */
    tap_btn(BTN_OCT_UP);
    btn_down(BTN_OCT_DOWN); run_ms(30);
    uint32_t during = br_ui.step_bank;
    btn_up(BTN_OCT_DOWN); run_ms(5);
    NOTE("bank change: OCT+ acts on press; OCT- waits for release (bank while held %u, after %u), "
         "because OCT- is reserved for the OCT-+GLO chord", during + 1u, br_ui.step_bank + 1u);
}

/* 2. Hold-to-edit changes only the held step (tonal) or the held lane (kit). */
static void t_hold_edit(void)
{
    fresh(); select_track(0); open_seq_grid();
    tap_key(W[0]); tap_key(W[4]); tap_key(W[8]);
    snap_t a, b; snap(0, &a);
    struct { uint8_t note, vel, cond; uint16_t len; } d0 = {br_ui.seq_default[0][0].note,
        br_ui.seq_default[0][0].velocity, br_ui.seq_default[0][0].condition, br_ui.seq_default[0][0].length};
    key_down(W[4]); run_ms(400);
    CHECK(br_ui.step_edit_active, "holding a lit step 400 ms enters step edit");
    turn(ENC_KNOB1, 3); int32_t n = br_control(0).value;
    turn(ENC_KNOB2, 8); int32_t l = br_control(1).value;
    turn(ENC_KNOB3, -20); int32_t v = br_control(2).value;
    turn(ENC_KNOB4, 5); int32_t c = br_control(3).value;
    key_up(W[4]); run_ms(20);
    CHECK(br_step_note_get(0, 4, 0) == (uint32_t)n && br_step_length_get(0, 4, 0) == (uint32_t)l &&
          br_step_velocity_get(0, 4, 0) == (uint32_t)v && br_step_condition_get(0, 4, 0) == (uint32_t)c,
          "step 5 now note %d length %d velocity %d condition %d", n, l, v, c);
    CHECK(n == 63 && c == 5, "NOTE and CONDITION move one value per detent (note 60+3, cond 0+5)");
    NOTE("LENGTH +8 detents -> %d (from 16), VELOCITY -20 detents -> %d (from 100): accelerated", l, v);
    /* B3: a held step's VELOCITY knob stops at 1, so a lit step is never silent. */
    key_down(W[4]); run_ms(400); turn(ENC_KNOB3, -60); key_up(W[4]); run_ms(20);
    CHECK(v == 1 && br_step_velocity_get(0, 4, 0) == 1u,
          "held-step VELOCITY clamps at 1 (after -20 detents %d, after -60 more %u)", v, br_step_velocity_get(0, 4, 0));
    snap(0, &b);
    CHECK(snap_diff(&a, &b, 0, 4, 0) == 0u, "no other step changed");
    CHECK(on(0, 4, 0) && br_ui.step_press_key == BR_STEP_NO_PRESS && !br_ui.step_edit_active && !br_fill_active,
          "release leaves the step lit and the edit state clean");
    CHECK(d0.note == br_ui.seq_default[0][0].note && d0.vel == br_ui.seq_default[0][0].velocity &&
          d0.cond == br_ui.seq_default[0][0].condition && d0.len == br_ui.seq_default[0][0].length,
          "insertion defaults unchanged by a held edit");
    /* Short tap on a lit step removes it; long hold without turning keeps it. */
    tap_key(W[8]);
    CHECK(!on(0, 8, 0), "short tap removes a lit step");
    key_down(W[0]); run_ms(500); key_up(W[0]); run_ms(20);
    CHECK(on(0, 0, 0), "a 500 ms hold with no turn keeps the step (edit, not toggle)");
    /* Holding an empty step: nothing until a knob turns or the key is released. */
    key_down(W[10]); run_ms(500);
    uint32_t mid = on(0, 10, 0) || br_ui.step_edit_active;
    turn(ENC_KNOB3, 1);
    uint32_t created = on(0, 10, 0) && br_ui.step_edit_active;
    key_up(W[10]); run_ms(20);
    CHECK(!mid && created && on(0, 10, 0), "empty step: hold shows nothing; a knob turn creates and edits it");

    /* Kit: edit lane 2 of step 1 while lanes 1 and 3 also have that step. */
    select_track(BR_DRUM_PART); open_seq_grid();
    for (uint32_t lane = 0; lane < 3; ++lane) { select_lane(lane); tap_key(W[1]); }
    select_lane(1);
    snap(BR_DRUM_PART, &a);
    key_down(W[1]); run_ms(400);
    turn(ENC_KNOB1, 4); turn(ENC_KNOB2, 6); turn(ENC_KNOB3, -10); turn(ENC_KNOB4, 2);
    key_up(W[1]); run_ms(20);
    snap(BR_DRUM_PART, &b);
    CHECK(snap_diff(&a, &b, BR_DRUM_PART, 1, 1) == 0u, "kit: only lane 2 of step 2 changed");
    CHECK(br_step_note_get(3, 1, 1) == 64u && br_step_condition_get(3, 1, 1) == 2u &&
          br_step_length_get(3, 1, 1) > 0u && br_step_velocity_get(3, 1, 1) < 100u,
          "kit lane 2 step 2: pitch %u length %u velocity %u condition %u", br_step_note_get(3, 1, 1),
          br_step_length_get(3, 1, 1), br_step_velocity_get(3, 1, 1), br_step_condition_get(3, 1, 1));
    /* Condition knob is one value per detent across 145 values. */
    NOTE("CONDITION list: 0 ALWAYS, 1-100 %%, 101-108 FIRST/REPEATS/AFTER HIT/AFTER MISS/NEIGHBOR/ALONE/FILL/NO FILL, "
         "109-144 A:B; reaching 1:2 from ALWAYS takes 110 detents, 50%% takes 50");
}

/* Fire the pattern and return the sequence of steps heard on part p (lane for kit). */
static uint32_t heard_steps(uint32_t p, uint32_t lane, uint32_t *steps, uint64_t *times, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < st_n && n < cap; ++i) {
        const st_ev *e = &st_log[i];
        if (p < BR_SYNTH_PARTS && e->part == p && (e->kind == E_SYN || e->kind == E_SUS)) {
            steps[n] = e->a - 36u; times[n++] = e->t;
        } else if (p == BR_DRUM_PART && e->kind == E_KIT && e->part == p && e->a == lane && !e->retrig) {
            steps[n] = e->b - 36u; times[n++] = e->t;
        }
    }
    return n;
}

/* 3. Lengths 1..64, wrap order and step timing; change of length while playing. */
static void t_lengths(void)
{
    static uint32_t steps[400]; static uint64_t times[400];
    fresh(); br_bpm = 240u; clear_all_steps();
    uint32_t bad_order = 0, bad_time = 0, kit_bad = 0;
    for (uint32_t L = 1; L <= BR_STEPS; L += (SLOW ? 7u : 1u)) {
        play_reset(); clear_all_steps();
        br_part[0].length = L; br_part[BR_DRUM_PART].length = L;
        for (uint32_t s = 0; s < L; ++s) { synth_step(0, s, 36u + s, 8u, 0u); kit_step(s, 0, 36u + s % 49u, 0u, 0u); }
        uint32_t dur = br_step_duration(0, 0);
        play_start(); render_frames((uint64_t)dur * (2u * L + 1u) + BR_BLOCK);
        uint32_t n = heard_steps(0, 0, steps, times, 400);
        if (n < 2u * L + 1u) bad_order++;
        for (uint32_t i = 0; i < n; ++i) {
            if (steps[i] != i % L) { bad_order++; break; }
            if (times[i] != times[0] + (uint64_t)i * dur) { bad_time++; break; }
        }
        n = heard_steps(BR_DRUM_PART, 0, steps, times, 400);
        for (uint32_t i = 0; i < n; ++i) if (steps[i] != (i % L) % 49u || times[i] != times[0] + (uint64_t)i * dur) { kit_bad++; break; }
    }
    CHECK(!bad_order && !bad_time && !kit_bad, "lengths 1..64: each loop plays steps 0..L-1 in order, one step "
          "duration apart, tonal and kit (%u order, %u timing, %u kit errors)", bad_order, bad_time, kit_bad);

    /* Shorten 16 -> 8 while the playhead is on step 11, through SEQ + KNOB1. */
    fresh(); clear_all_steps(); select_track(0); open_seq_grid();
    for (uint32_t s = 0; s < 16; ++s) synth_step(0, s, 36u + s, 8u, 0u);
    br_seq_synth_condition[0][0] = BR_STEP_COND_FIRST;
    st_n = 0; tap_btn(BTN_PLAY);
    while (br_step_pos[0] != 11u) run_ms(1);
    btn_down(BTN_SEQ); run_ms(5); turn(ENC_KNOB1, -8); btn_up(BTN_SEQ);
    uint32_t len = br_part[0].length;
    run_ms(2500); tap_btn(BTN_PLAY);
    uint32_t n = heard_steps(0, 0, steps, times, 400), at11 = 0, after = 99, first_count = 0;
    for (uint32_t i = 0; i < n; ++i) { if (steps[i] == 11u && !at11) { at11 = 1; after = i + 1 < n ? steps[i + 1] : 99; } if (steps[i] == 0u) ++first_count; }
    CHECK(len == 8u && br_ui.screen[BR_PAGE_SEQ] == 0u && after <= 1u,
          "SEQ+KNOB1 shortens to %u while playing; settings page stays shut; after step 12 the playhead wraps to the "
          "start (next heard step %u)", len, after + 1u);
    CHECK(first_count == 1u,
          "a FIRST-only step plays once (%u times): the wrap after shortening past the playhead counts a new "
          "cycle, so FIRST does not repeat and A:B counters stay in step", first_count);
    /* Lengthen 8 -> 16 at step 5: continues 6, 7, 8 ... */
    play_reset(); clear_all_steps(); br_part[0].length = 8u;
    for (uint32_t s = 0; s < 16; ++s) synth_step(0, s, 36u + s, 8u, 0u);
    play_start(); while (br_step_pos[0] != 5u) render_block();
    br_part[0].length = 16u; render_frames((uint64_t)br_step_duration(0, 0) * 12u);
    n = heard_steps(0, 0, steps, times, 400);
    uint32_t ok = 0; for (uint32_t i = 0; i + 3 < n; ++i) if (steps[i] == 5u) { ok = steps[i + 1] == 6u && steps[i + 2] == 7u && steps[i + 3] == 8u; break; }
    CHECK(ok, "lengthening while playing continues past the old end (5,6,7,8)");
}

/* 4. Stop/start: note-offs, playhead, restart point. */
static void t_stop_start(void)
{
    static uint32_t steps[400]; static uint64_t times[400];
    fresh(); clear_all_steps(); select_track(0); open_seq_grid();
    for (uint32_t s = 0; s < 16; ++s) synth_step(0, s, 36u + s, 64u, 0u);  /* 4-step notes */
    st_n = 0; tap_btn(BTN_PLAY);
    while (br_step_pos[0] != 5u) run_ms(1);
    run_ms(30);
    uint64_t stop_at = total_frames; uint32_t n0 = st_n;
    tap_btn(BTN_PLAY);
    uint32_t offs = 0; uint64_t off_t = 0;
    for (uint32_t i = n0; i < st_n; ++i) if (st_log[i].kind == E_OFF && st_log[i].part == 0) { ++offs; off_t = st_log[i].block; }
    CHECK(!br_playing && offs == 0u, "stop: no sequencer note-off is logged (%u); the stop path releases voices "
          "directly (br_stop_part)", offs);
    render_block();
    CHECK(br_env[0].stage != BR_ENV_ATTACK && br_env[0].stage != BR_ENV_DECAY && br_env[0].stage != BR_ENV_SUSTAIN,
          "stop releases the sounding note within a block (envelope stage %u)", br_env[0].stage);
    NOTE("after stop the playhead variable still shows step %u until the next start", br_step_pos[0] + 1u);
    (void)off_t; (void)stop_at;
    run_ms(200);
    uint32_t n1 = st_n; uint64_t start_block = total_frames;
    tap_btn(BTN_PLAY); run_ms(400);
    uint32_t first = 99; uint64_t first_t = 0;
    for (uint32_t i = n1; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part == 0) { first = st_log[i].a - 36u; first_t = st_log[i].t; break; }
    CHECK(first == 0u, "PLAY always restarts at step 1 (heard step %u, %llu frames after the press block)",
          first + 1u, (unsigned long long)(first_t - start_block));
    (void)steps; (void)heard_steps;
}

/* 5 and 6. Ties across the loop point and lengths longer than a step. */
static void t_ties_lengths(void)
{
    fresh(); clear_all_steps(); br_bpm = 120u;
    uint32_t dur = br_step_duration(0, 0);
    /* Legacy tie bit, same pitch, last step -> first step. */
    play_reset(); clear_all_steps();
    synth_step(0, 15, 60, 16, 0); br_part[0].step[15] |= BR_STEP_TIE; synth_step(0, 0, 60, 16, 0);
    play_start(); render_frames((uint64_t)dur * 33u);
    st_ev e[64]; uint32_t sus = collect(E_SUS, 0, 255, e, 64), offs = collect(E_OFF, 0, 255, e, 64) + collect(E_CUT, 0, 255, e, 64);
    CHECK(sus >= 1u, "tie bit: step 16 -> step 1 with the same note sustains across the loop point (%u sustains, %u offs)", sus, offs);
    NOTE("no UI gesture sets BR_STEP_TIE (only legacy sessions and copy/paste carry it); the drawn tie mark can "
         "appear on steps the player cannot make");
    /* Long length over empty steps, across the loop point. */
    play_reset(); clear_all_steps();
    synth_step(0, 15, 62, 48, 0);   /* three steps */
    play_start(); render_frames((uint64_t)dur * 20u);
    st_ev on_ev[8], off_ev[8];
    uint32_t no = collect(E_SYN, 0, 255, on_ev, 8), nf = collect(E_OFF, 0, 255, off_ev, 8);
    int64_t held = no && nf ? (int64_t)(off_ev[0].t - on_ev[0].t) : -1;
    CHECK(no == 1u && nf >= 1u && held >= (int64_t)(3u * dur) - BR_BLOCK && held <= (int64_t)(3u * dur) + BR_BLOCK,
          "length 3 steps on step 16 holds across the loop over empty steps: %lld frames (3 steps = %u)",
          (long long)held, 3u * dur);
    /* Long length cut by the next note (mono). */
    play_reset(); clear_all_steps();
    synth_step(0, 0, 60, 40, 0); synth_step(0, 1, 64, 16, 0);
    play_start(); render_frames((uint64_t)dur * 3u);
    st_ev cut[4]; uint32_t nc = collect(E_CUT, 0, 255, cut, 4);
    collect(E_SYN, 0, 255, on_ev, 8);
    CHECK(nc >= 1u && cut[0].a == 60u && cut[0].t == on_ev[0].t + dur,
          "length 2.5 steps is cut at the next note one step later (mono track)");
    /* Same pitch, full length, no tie: retriggers. */
    play_reset(); clear_all_steps();
    synth_step(0, 0, 60, 16, 0); synth_step(0, 1, 60, 16, 0);
    play_start(); render_frames((uint64_t)dur * 3u);
    uint32_t ons = collect(E_SYN, 0, 255, on_ev, 8);
    CHECK(ons >= 2u, "same pitch on adjacent full-length steps retriggers (no tie gesture exists to join them)");
    /* Length 64 steps on a 16-step loop: the note is restarted by its own step each loop. */
    play_reset(); clear_all_steps();
    synth_step(0, 0, 60, BR_STEP_LENGTH_MAX, 0);
    play_start(); render_frames((uint64_t)dur * 33u);
    ons = collect(E_SYN, 0, 255, on_ev, 8); nc = collect(E_CUT, 0, 255, cut, 4);
    CHECK(ons == 3u && nc == 2u, "maximum length (64 steps) on a 16-step loop: restarted each loop (%u starts, %u cuts)", ons, nc);
    /* Velocity 0 keeps a step lit but silent (tonal and kit). */
    play_reset(); clear_all_steps();
    synth_step(0, 0, 60, 16, 0); br_step_velocity_set(0, 0, 0, 0u); kit_step(0, 1, 60, 0, 0); br_seq_kit_velocity[0][1] = 0u;
    play_start(); render_frames((uint64_t)dur * 2u);
    ons = collect(E_SYN, 0, 255, on_ev, 8);
    uint32_t kits = collect(E_KIT, 3, 255, on_ev, 8);
    CHECK(on(0, 0, 0) && on(3, 0, 1) && ons == 0u && kits == 0u,
          "velocity 0 (only from older sessions now) still loads and plays as before: lit on the grid but silent (tonal and kit)");
}

/* 7. Conditions over many cycles. Uses length-1 patterns at 240 BPM, 1/32. */
static uint32_t run_cycles_count(uint32_t p, uint32_t lane, uint32_t cycles)
{
    uint32_t dur;
    play_start();
    dur = br_step_duration(p, 0);
    render_frames((uint64_t)dur * cycles - BR_BLOCK);
    br_playing = 0u; render_block();
    uint32_t n = 0;
    for (uint32_t i = 0; i < st_n; ++i) {
        const st_ev *e = &st_log[i];
        if (p < 3u ? (e->part == p && e->kind == E_SYN) : (e->part == p && e->kind == E_KIT && e->a == lane && !e->retrig)) ++n;
    }
    return n;
}
static void t_conditions(void)
{
    uint32_t cycles = SLOW ? 300u : 2000u;
    fresh(); br_bpm = 240u;
    struct { uint32_t cond; const char *name; } probs[] = {{1, "1%"}, {25, "25%"}, {50, "50%"}, {99, "99%"}, {100, "100%"}, {0, "ALWAYS"}};
    for (uint32_t k = 0; k < 6; ++k) for (uint32_t kit = 0; kit < 2; ++kit) {
        play_reset(); clear_all_steps();
        br_part[0].length = br_part[BR_DRUM_PART].length = 1u; br_part[0].division = br_part[BR_DRUM_PART].division = 2u;
        if (kit) kit_step(0, 2, 60, probs[k].cond, 0); else synth_step(0, 0, 60, 4, probs[k].cond);
        uint32_t n = run_cycles_count(kit ? 3u : 0u, 2u, cycles);
        double expect = probs[k].cond ? cycles * probs[k].cond / 100.0 : cycles;
        double sd = probs[k].cond && probs[k].cond < 100 ? __builtin_sqrt(cycles * probs[k].cond / 100.0 * (1 - probs[k].cond / 100.0)) : 0.5;
        CHECK(n >= expect - 4 * sd - 1 && n <= expect + 4 * sd + 1, "%s %s over %u cycles: %u hits (expect %.0f +- %.0f)",
              kit ? "kit" : "tonal", probs[k].name, cycles, n, expect, 4 * sd);
    }
    /* FIRST / REPEATS / A:B by cycle index, tonal: one step per cycle. */
    uint32_t ab_bad = 0, total_ab = 0, cyc = SLOW ? 56u : 840u;
    for (uint32_t cond = BR_STEP_COND_FIRST; cond < BR_STEP_COND_COUNT; ++cond) {
        if (cond >= BR_STEP_COND_PRE && cond <= BR_STEP_COND_NOT_FILL) continue;
        play_reset(); clear_all_steps();
        br_part[0].length = 1u; br_part[0].division = 2u;
        synth_step(0, 0, 60, 4, cond);
        uint32_t n = run_cycles_count(0, 0, cyc), expect;
        if (cond == BR_STEP_COND_FIRST) expect = 1u;
        else if (cond == BR_STEP_COND_NOT_FIRST) expect = cyc - 1u;
        else {
            uint32_t code = cond - BR_STEP_COND_AB_BASE, d = 1;
            while (code >= d) { code -= d; ++d; }
            expect = cyc / d + (cyc % d > code ? 1u : 0u);
            ++total_ab;
        }
        if (n != expect) { ++ab_bad; NOTE("condition %u: %u hits, expected %u", cond, n, expect); }
    }
    CHECK(!ab_bad, "FIRST, REPEATS and all 36 A:B conditions fire on exactly the expected cycles over %u cycles", cyc);
    /* AFTER HIT / AFTER MISS follow the previous conditional step on the same track. */
    play_reset(); clear_all_steps(); br_part[0].length = 3u; br_part[0].division = 2u;
    synth_step(0, 0, 60, 4, 50); synth_step(0, 1, 61, 4, BR_STEP_COND_PRE); synth_step(0, 2, 62, 4, BR_STEP_COND_NOT_PRE);
    play_start(); render_frames((uint64_t)br_step_duration(0, 0) * 3u * 300u);
    br_playing = 0u; render_block();
    uint32_t viol = 0, a = 0, h1 = 0, h2 = 0, prev_hit = 0, cyc_i = 0;
    uint64_t pre_t0 = 0; for (uint32_t i = 0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part == 0) { pre_t0 = st_log[i].t - (st_log[i].a - 60u) * (uint64_t)br_step_duration(0, 0); break; }
    for (uint32_t i = 0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part == 0) {
        uint32_t s = st_log[i].a - 60u;
        uint32_t cycle_of = (uint32_t)((st_log[i].t - pre_t0) / ((uint64_t)br_step_duration(0, 0) * 3u));
        if (cycle_of >= 299u) continue;
        if (s == 0) { prev_hit = cycle_of + 1u; ++a; }
        if (s == 1) { ++h1; if (prev_hit != cycle_of + 1u) ++viol; }
        if (s == 2) { ++h2; if (prev_hit == cycle_of + 1u) ++viol; }
        cyc_i = cycle_of;
    }
    (void)cyc_i;
    CHECK(!viol && h1 == a && h1 + h2 == 299u, "AFTER HIT plays exactly when the 50%% step hit (%u/%u), AFTER MISS exactly when it "
          "missed (%u)", h1, a, h2);
    /* NEIGHBOR: track 2 follows track 1; drum lane 1 follows track 3. */
    play_reset(); clear_all_steps();
    for (uint32_t p = 0; p < 4; ++p) { br_part[p].length = 1u; br_part[p].division = 2u; }
    synth_step(0, 0, 60, 4, 50); synth_step(1, 0, 61, 4, BR_STEP_COND_NEI);
    synth_step(2, 0, 62, 4, 50); kit_step(0, 0, 60, BR_STEP_COND_NEI, 0);
    play_start(); render_frames((uint64_t)br_step_duration(0, 0) * 400u);
    br_playing = 0u; render_block();
    uint32_t t1 = 0, t2 = 0, t3 = 0, k1 = 0, nei_bad = 0;
    uint64_t last1 = ~0ull, last3 = ~0ull;
    for (uint32_t i = 0; i < st_n; ++i) {
        const st_ev *e = &st_log[i];
        if (e->kind == E_SYN && e->part == 0) { ++t1; last1 = e->t; }
        if (e->kind == E_SYN && e->part == 1) { ++t2; if (last1 != e->t) ++nei_bad; }
        if (e->kind == E_SYN && e->part == 2) { ++t3; last3 = e->t; }
        if (e->kind == E_KIT && e->part == 3 && !e->retrig) { ++k1; if (last3 != e->t) ++nei_bad; }
    }
    CHECK(!nei_bad && t2 == t1 && k1 == t3, "NEIGHBOR: track 2 plays with track 1 (%u/%u); drum lane 1's neighbour is "
          "track 3 (%u/%u)", t2, t1, k1, t3);
    /* FILL needs SEQ held together with a held step key. */
    fresh(); clear_all_steps(); select_track(0); open_seq_grid();
    synth_step(0, 3, 70, 8, BR_STEP_COND_FILL); synth_step(0, 0, 60, 8, 0);
    st_n = 0; tap_btn(BTN_PLAY); run_ms(2200);
    st_ev ev[64]; uint32_t before = 0, n = collect(E_SYN, 0, 255, ev, 64);
    for (uint32_t i = 0; i < n; ++i) before += ev[i].a == 70u;
    btn_down(BTN_SEQ); run_ms(2200);
    uint32_t seq_only = 0; n = collect(E_SYN, 0, 255, ev, 64);
    for (uint32_t i = 0; i < n; ++i) seq_only += ev[i].a == 70u;
    key_down(W[0]); run_ms(2200);
    uint32_t fill = 0, editing = br_ui.step_edit_active; n = collect(E_SYN, 0, 255, ev, 64);
    for (uint32_t i = 0; i < n; ++i) fill += ev[i].a == 70u;
    key_up(W[0]); btn_up(BTN_SEQ); run_ms(50); tap_btn(BTN_PLAY);
    BUG("SEQ-FILL-GESTURE", before == 0u && seq_only == before && fill > seq_only && editing,
        "FILL steps play only while SEQ and a lit step key are both held (no fill: %u, SEQ alone: %u, SEQ+step: %u); "
        "the held step is put into edit mode at the same time, and nothing on screen names the gesture",
        before, seq_only - before, fill - seq_only);
}

/* 8. Retriggers at the fastest spacing and highest tempo. */
static void t_retrig(void)
{
    for (uint32_t bpm_i = 0; bpm_i < 2; ++bpm_i) {
        uint32_t bpm = bpm_i ? 240u : 120u;
        fresh(); clear_all_steps(); br_bpm = bpm;
        play_reset(); clear_all_steps();
        kit_step(0, 0, 60, 0, 7u);           /* 7 repeats, spacing 0 = 1/128 */
        play_start(); render_frames(br_tempo_quarter_frames() / 2u);
        st_ev h[16]; uint32_t n = collect(E_KIT, 3, 0, h, 16);
        uint32_t interval = br_tempo_quarter_frames() / 32u, even = 1, max_dev = 0;
        uint32_t min_gap = ~0u, max_gap = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (h[i].t != h[0].t + (uint64_t)i * interval) even = 0;
            uint32_t dev = (uint32_t)((h[0].t + (uint64_t)i * interval) - h[i].block);
            if (dev > max_dev) max_dev = dev;
            if (i) { uint32_t g = (uint32_t)(h[i].block - h[i - 1].block); if (g < min_gap) min_gap = g; if (g > max_gap) max_gap = g; }
        }
        CHECK(n == 8u && even, "%u BPM: 1 hit + 7 repeats at 1/128 (%u frames apart) scheduled exactly", bpm, interval);
        BUG("SEQ-RATCHET-JITTER", max_gap - min_gap >= BR_BLOCK / 2u,
            "%u BPM: repeats sound at block starts, so the audible gaps are %u..%u frames for a nominal %u "
            "(up to %u frames = %.1f ms early)", bpm, min_gap, max_gap, interval, max_dev, max_dev * 1000.0 / FS);
    }
    /* Repeats are cancelled by the next real hit of the lane, kept across other lanes. */
    fresh(); clear_all_steps(); br_bpm = 120u; play_reset(); clear_all_steps();
    kit_step(0, 0, 60, 0, 7u | (3u << 3));   /* 7 repeats at 1/16 = one per step */
    kit_step(2, 0, 61, 0, 0); kit_step(1, 1, 62, 0, 0);
    play_start(); render_frames((uint64_t)br_step_duration(3, 0) * 8u);
    st_ev h[32]; uint32_t n = collect(E_KIT, 3, 0, h, 32), reps = 0;
    for (uint32_t i = 0; i < n && h[i].b != 61u; ++i) reps += h[i].retrig;
    CHECK(reps == 1u, "a real hit on the same lane two steps later cuts 7 repeats short after %u (other lanes don't)", reps);
    /* Spacing ignores SPEED and swing (it is in note values). */
    fresh(); clear_all_steps(); br_bpm = 120u; play_reset(); clear_all_steps();
    br_part[3].division = 0u; br_swing = 77u;
    kit_step(0, 0, 60, 0, 1u | (3u << 3));
    play_start(); render_frames(br_tempo_quarter_frames());
    n = collect(E_KIT, 3, 0, h, 32);
    CHECK(n >= 2u && h[1].t - h[0].t == br_tempo_quarter_frames() / 4u, "retrig 1/16 spacing is a 1/16 note at SPEED 1/8 and swing 80%%");
}

/* 9. Swing extremes. */
static void t_swing(void)
{
    static uint32_t steps[400]; static uint64_t times[400];
    for (uint32_t sw = 0; sw <= BR_SWING_MAX; sw += BR_SWING_MAX) {
        fresh(); clear_all_steps(); br_bpm = 120u; play_reset(); clear_all_steps();
        br_swing = sw;
        for (uint32_t s = 0; s < 16; ++s) synth_step(0, s, 36 + s, 4, 0);
        play_start(); render_frames(br_tempo_quarter_frames() * 8u + 2u * BR_BLOCK);
        uint32_t n = heard_steps(0, 0, steps, times, 400);
        uint64_t bar = n > 16 ? times[16] - times[0] : 0;
        uint32_t even = n > 1 ? (uint32_t)(times[1] - times[0]) : 0, odd = n > 2 ? (uint32_t)(times[2] - times[1]) : 0;
        NOTE("swing %u (%u%%): even step %u frames, odd %u, bar %llu frames vs 4 quarters %u", sw,
             (128u + sw) * 100u / 256u, even, odd, (unsigned long long)bar, 4u * br_tempo_quarter_frames());
    }
    /* Odd length with swing: two long steps meet at the loop point. */
    fresh(); clear_all_steps(); br_bpm = 120u; play_reset(); clear_all_steps();
    br_swing = BR_SWING_MAX; br_part[0].length = 3u;
    for (uint32_t s = 0; s < 3; ++s) synth_step(0, s, 36 + s, 4, 0);
    play_start(); render_frames(br_tempo_quarter_frames() * 3u);
    uint32_t n = heard_steps(0, 0, steps, times, 400);
    if (n >= 5) NOTE("length 3, swing 80%%: step gaps %llu %llu %llu %llu (long-short-long-long: swing follows the step "
                     "number, so odd lengths put two long steps together at the loop point)",
                     (unsigned long long)(times[1] - times[0]), (unsigned long long)(times[2] - times[1]),
                     (unsigned long long)(times[3] - times[2]), (unsigned long long)(times[4] - times[3]));
}

/* 10. Tempo extremes, truncation drift between speeds and against the beat clock. */
static void t_tempo(void)
{
    uint32_t bpms[] = {60, 61, 97, 120, 133, 240};
    for (uint32_t i = 0; i < 6; ++i) {
        fresh(); br_bpm = bpms[i];
        uint32_t q = br_tempo_quarter_frames();
        double ideal = FS * 60.0 / bpms[i];
        uint32_t d8, d16, d32;
        br_part[0].division = 0; d8 = br_step_duration(0, 0);
        br_part[0].division = 1; d16 = br_step_duration(0, 0);
        br_part[0].division = 2; d32 = br_step_duration(0, 0);
        NOTE("%3u BPM: quarter %u frames (ideal %.2f); per bar, 1/8 steps fall %d frames short of 4 quarters, "
             "1/16 %d, 1/32 %d", bpms[i], q, ideal, (int)(4u * q - 8u * d8), (int)(4u * q - 16u * d16), (int)(4u * q - 32u * d32));
    }
    /* Measure it: three tracks at 1/8, 1/16, 1/32 each hit on every quarter. */
    uint32_t bars = SLOW ? 8u : 64u;
    fresh(); clear_all_steps(); br_bpm = 133u; play_reset(); clear_all_steps();
    br_part[0].division = 0; br_part[0].length = 2; synth_step(0, 0, 60, 4, 0);
    br_part[1].division = 1; br_part[1].length = 4; synth_step(1, 0, 62, 4, 0);
    br_part[2].division = 2; br_part[2].length = 8; synth_step(2, 0, 64, 4, 0);
    play_start(); render_frames((uint64_t)br_tempo_quarter_frames() * 4u * bars + BR_BLOCK);
    uint64_t last[3] = {0, 0, 0}, first = ~0ull; uint32_t cnt[3] = {0, 0, 0};
    for (uint32_t i = 0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part < 3u && st_log[i].t < first) first = st_log[i].t;
    for (uint32_t i = 0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part < 3u) {
        uint32_t p = st_log[i].part;
        if (cnt[p] == 4u * bars) last[p] = st_log[i].t - first;
        ++cnt[p];
    }
    uint64_t beat = (uint64_t)br_tempo_quarter_frames() * 4u * bars;
    int64_t lag8 = (int64_t)beat - (int64_t)last[0], lag16 = (int64_t)beat - (int64_t)last[1], lag32 = (int64_t)beat - (int64_t)last[2];
    BUG("SEQ-TEMPO-DRIFT", lag32 > 0 && lag32 > lag8,
        "133 BPM after %u bars: the downbeat is early by %lld frames on a 1/8 track, %lld on 1/16, %lld (%.1f ms) on "
        "1/32, against the beat clock that drives the arp, punch loop and pattern bar switch: tracks at different "
        "SPEED drift apart", bars, (long long)lag8, (long long)lag16, (long long)lag32, lag32 * 1000.0 / FS);
    /* Tempo change while playing takes effect on the next step. */
    fresh(); clear_all_steps(); br_bpm = 60u; play_reset(); clear_all_steps();
    for (uint32_t s = 0; s < 16; ++s) synth_step(0, s, 36 + s, 4, 0);
    play_start(); render_frames(br_step_duration(0, 0) + BR_BLOCK);
    uint32_t d60 = br_step_duration(0, 0); br_bpm = 240u;
    render_frames((uint64_t)d60 * 2u);
    st_ev e[16]; uint32_t n = collect(E_SYN, 0, 255, e, 16);
    CHECK(n >= 4 && e[1].t - e[0].t == d60 && e[3].t - e[2].t == br_step_duration(0, 0),
          "a tempo change applies from the next step boundary (60 -> 240 BPM)");
}

/* 11. Track and page changes during a held edit; PLAY/REC while holding. */
static void t_mid_edit(void)
{
    fresh(); clear_all_steps(); select_track(0); open_seq_grid();
    tap_key(W[2]);
    key_down(W[2]); run_ms(400);
    turn(ENC_ALGORITHM, 2); tap_btn(BTN_HOME); tap_btn(BTN_FX); turn(ENC_SELECT, 1); turn(ENC_PRESETS, 1);
    uint32_t still = br_ui.part == 0u && br_ui.page == BR_PAGE_SEQ && br_ui.step_edit_active;
    turn(ENC_KNOB1, 2);
    key_up(W[2]); run_ms(20);
    CHECK(still && br_step_note_get(0, 2, 0) == 62u && br_ui.step_press_key == BR_STEP_NO_PRESS && !br_ui.step_edit_active,
          "while a step is held, track (ALGORITHM), page buttons, SELECT and PRESETS are ignored; the edit "
          "lands on the held step and ends cleanly on release");
    /* A9: with nothing copied, PLAY stays transport while a step is held (REC still copies). */
    tap_btn(BTN_PLAY); run_ms(300);
    uint32_t playing = br_playing;
    key_down(W[5]); run_ms(100); tap_btn(BTN_PLAY);
    uint32_t still_playing = br_playing; char msg[25]; memcpy(msg, br_ui.message, sizeof msg);
    key_up(W[5]); run_ms(20);
    CHECK(playing && !still_playing && !on(0, 5, 0),
          "with nothing copied, PLAY while a step is held stops the sequencer and leaves the held empty step "
          "empty (message \"%s\")", msg);
    /* With a valid clipboard PLAY still pastes and does not change transport. */
    key_down(W[2]); run_ms(100); tap_btn(BTN_REC); key_up(W[2]); run_ms(20);
    key_down(W[6]); run_ms(100); tap_btn(BTN_PLAY); key_up(W[6]); run_ms(20);
    CHECK(!br_playing && on(0, 6, 0) && br_step_note_get(0, 6, 0) == 62u,
          "with a step copied, held step + PLAY pastes it (note %u) and transport stays stopped", br_step_note_get(0, 6, 0));
    if (br_playing) tap_btn(BTN_PLAY);
    /* Retrigger page: a held step is not locked; switching track drops the edit. */
    select_track(BR_DRUM_PART); open_seq_grid(); tap_key(W[0]);
#if BR_PRODUCT_SMK37
    /* SMK-37: ARP on the drum track is the "SWITCH TO" cue; only NOTE REPEAT opens RETRIGGER (smk37_fader_logic.h
     * br_smk_repeat_press: its state; the panel code is device-only) */
    tap_btn(BTN_ARP); run_ms(900);
    CHECK(!br_retrig_page() && br_arp_cue_page(), "ARP on the drum track opens the cue page, not retrigger");
    br_smk_retrig.page = (uint8_t)br_ui.page; br_smk_retrig.screen = 0u; br_smk_retrig.opened = 1u;
    br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u; run_ms(20);
    CHECK(br_retrig_page(), "NOTE REPEAT on the drum track opens the retrigger page");
#else
    tap_btn(BTN_ARP); run_ms(900);
    CHECK(br_retrig_page(), "ARP on the drum track opens the retrigger page");
#endif
    key_down(W[0]); run_ms(100);
    uint32_t editing = br_retrig_editing();
    turn(ENC_KNOB1, 2);
    turn(ENC_ALGORITHM, -1);
    uint32_t dropped = !br_retrig_editing() && br_ui.retrig_key == 0u;
    turn(ENC_KNOB1, 1);
    key_up(W[0]); run_ms(20);
    CHECK(editing && dropped && (br_seq_kit_retrig[0][0] & 7u) == 2u,
          "retrigger page: switching track mid-hold drops the edit cleanly (count stays %u)", br_seq_kit_retrig[0][0] & 7u);
}

/* 12. Two steps pressed together. */
static void t_two_steps(void)
{
    for (uint32_t kit = 0; kit < 2; ++kit) {
        uint32_t p = kit ? BR_DRUM_PART : 0u;
        fresh(); clear_all_steps(); select_track(p); open_seq_grid();
        key_down(W[1]); key_down(W[2]); run_ms(60); key_up(W[1]); key_up(W[2]); run_ms(40);
        uint32_t same_tick = on(p, 1, 0) + on(p, 2, 0);
        key_down(W[4]); run_ms(80); key_down(W[5]); run_ms(80); key_up(W[4]); run_ms(40); key_up(W[5]); run_ms(40);
        uint32_t rolled = on(p, 4, 0) + on(p, 5, 0);
#if SMK
        /* SMK-37: a pad pressed while another is held is swallowed by the held-step lock (dinghy.c). */
        CHECK(same_tick == 2u && rolled == 1u && on(p, 4, 0),
              "SMK-37 %s: two step pads pressed together set %u of 2; a rolled second pad while the first is "
              "held is ignored (%u of 2: the first only)", kit ? "kit" : "tonal", same_tick, rolled);
#else
        CHECK(same_tick == 2u && rolled == 2u,
              "%s: two step keys pressed together set %u of 2; a rolled second press while the first is held sets %u of 2",
              kit ? "kit" : "tonal", same_tick, rolled);
#endif
        /* Once the first key is an edit, a second white key is not a tap (tonal: pitch; kit: ignored). */
        key_down(W[1]); run_ms(400); key_down(W[7]); run_ms(60); key_up(W[7]); run_ms(20); key_up(W[1]); run_ms(40);
        CHECK(on(p, 1, 0) && !on(p, 7, 0) && br_ui.step_press_key == BR_STEP_NO_PRESS,
              "%s: a second key after the 350 ms hold does not toggle its step", kit ? "kit" : "tonal");
        /* Black keys behind a held step, before and after it becomes an edit, change nothing. */
        snap_t a, b; snap(p, &a);
        uint32_t lane = br_ui.lane, def = br_ui.seq_default[p][kit ? lane : 0u].note, note = br_ui.note[p];
        key_down(W[1]); run_ms(60); tap_key(B[0]); tap_key(B[2]);
        uint32_t early = br_ui.step_edit_active;
        run_ms(300); tap_key(B[1]); tap_key(B[3]); key_up(W[1]); run_ms(40);
        snap(p, &b);
        CHECK(!early && snap_diff(&a, &b, p, BR_STEPS, 4u) == 0u && br_ui.lane == lane &&
              br_ui.seq_default[p][kit ? lane : 0u].note == def && br_ui.note[p] == note && !STEP_PITCH_KEY &&
              br_ui.step_press_key == BR_STEP_NO_PRESS && !br_ui.step_edit_active,
              "%s: black keys behind a held step (before and after the hold becomes an edit) change no step, "
              "pitch, lane or insertion default", kit ? "kit" : "tonal");
    }
}

/* 13. Edits while playing land on the pressed step; adds happen on key release. */
static void t_edit_playing(void)
{
    fresh(); clear_all_steps(); select_track(0); open_seq_grid(); br_bpm = 120u;
    synth_step(0, 0, 60, 4, 0);
    st_n = 0; tap_btn(BTN_PLAY);
    while (br_step_pos[0] != 3u) run_ms(1);
    tap_key(W[7]);
    uint32_t dur = br_step_duration(0, 0);
    run_ms(5 * dur * 1000u / FS);
    st_ev e[64]; uint32_t n = collect(E_SYN, 0, 255, e, 64), heard7 = 0;
    for (uint32_t i = 0; i < n; ++i) heard7 += e[i].a == 60u && i > 0;
    CHECK(on(0, 7, 0) && br_step_note_get(0, 7, 0) == 60u, "tap during playback toggles the pressed step (8), not the playhead's");
    /* Press just before the playhead reaches the step, release after: it misses this pass. */
    while (br_step_pos[0] != 10u) run_ms(1);
    uint32_t n0 = st_n;
    key_down(W[11]); run_ms(dur * 1000u / FS + 60u); key_up(W[11]); run_ms(10);
    uint32_t heard11 = 0;
    for (uint32_t i = n0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part == 0) ++heard11;
    NOTE("a step added while the playhead passes it sounds only from the next loop: steps are added on key "
         "release (%u notes heard between press and release+10 ms)", heard11);
    /* Knob edit on a held step while playing lands on that step. */
    key_down(W[7]); run_ms(400); turn(ENC_KNOB1, 5); key_up(W[7]); run_ms(10);
    CHECK(br_step_note_get(0, 7, 0) == 65u && br_step_note_get(0, 0, 0) == 60u, "held edit while playing changes step 8 only");
    tap_btn(BTN_PLAY);
    (void)heard7;
}

/* 14. Recording. */
static void press_at(uint32_t step, uint32_t percent)
{
    uint32_t want = br_step_duration(0, step) * percent / 100u;
    for (;;) {
        uint32_t s, el, du, un; br_engine_record_position(0, &s, &el, &du, &un);
        if (s == step && el >= want) break;
        run_ms(1);
    }
}
static void t_record(void)
{
    fresh(); clear_all_steps(); select_track(0); br_bpm = 120u;
    tap_btn(BTN_HOME); tap_btn(BTN_REC);
    CHECK(br_recording, "REC arms recording (message \"%s\")", br_ui.message);
    st_n = 0; tap_btn(BTN_PLAY);
    uint32_t dur = br_step_duration(0, 0);
    press_at(4, 30); key_down(W[4]); run_ms(dur * 1000u * 3u / 2u / FS); key_up(W[4]); run_ms(5);
    uint32_t note4 = BR_STEP_NOTE(br_part[0].step[4]), len4 = br_step_length_get(0, 4, 0);
    CHECK(on(0, 4, 0) && note4 == 53u + W[4] && len4 >= 20u && len4 <= 28u,
          "a key held 1.5 steps from 30%% into step 5 records step 5, note %u, length %u/16", note4, len4);
    /* Late press quantized forward: the new step then plays again a moment later. */
    press_at(8, 80);
    uint32_t n0 = st_n; uint64_t t_press = total_frames;
    key_down(W[6]); run_ms(80); key_up(W[6]); run_ms(5);
    run_ms(dur * 1000u / FS);
    uint32_t starts = 0; uint64_t t_first = 0, t_last = 0;
    for (uint32_t i = n0; i < st_n; ++i) if (st_log[i].kind == E_VOICE && st_log[i].part == 0 && st_log[i].a == 53u + W[6]) {
        if (!starts) t_first = st_log[i].block; t_last = st_log[i].block; ++starts; }
    CHECK(on(0, 9, 0) && starts == 1u,
          "a note played at 80%% of step 9 is written to step 10 and attacks once, live (%u starts, last %llu "
          "frames after the first): the sequencer skips that step's firing in this pass", starts,
          (unsigned long long)(t_last - t_first));
    n0 = st_n; run_ms(16u * dur * 1000u / FS);
    starts = 0;
    for (uint32_t i = n0; i < st_n; ++i)
        starts += st_log[i].kind == E_VOICE && st_log[i].part == 0 && st_log[i].a == 53u + W[6];
    CHECK(starts == 1u, "the recorded step 10 plays on the next loop (%u starts in one loop)", starts);
    (void)t_press;
    /* Quantize OFF keeps the offset. */
    tap_btn(BTN_PLAY); tap_btn(BTN_SEQ); tap_btn(BTN_SEQ); turn(ENC_KNOB3, 1);
    CHECK(br_seq_record_quantize[0] == BR_TIME_QUANT_OFF, "settings QUANTIZE knob: STEP -> OFF");
    tap_btn(BTN_HOME); tap_btn(BTN_PLAY);
    press_at(12, 25); key_down(W[9]); run_ms(30); key_up(W[9]); run_ms(5);
    CHECK(on(0, 12, 0) && br_step_micro_get(0, 12, 0) >= 2 && br_step_micro_get(0, 12, 0) <= 3,
          "QUANTIZE OFF records step 13 with +%d/128 (pressed 25%% into a 1/16 step = 2/128, plus up to a block)", br_step_micro_get(0, 12, 0));
    tap_btn(BTN_PLAY);
    /* On the SEQ grid the keys never record. */
    open_seq_grid(); uint32_t before = count_on(0, 0);
    tap_btn(BTN_PLAY); run_ms(300); tap_key(B[2]); run_ms(100); tap_btn(BTN_PLAY);
#if SMK
    CHECK(count_on(0, 0) == before + 1u, "SMK-37: with REC armed, a keybed key on the SEQ grid records as on any page (%u -> %u)", before, count_on(0, 0));
#else
    CHECK(count_on(0, 0) == before, "with REC armed, black keys on the SEQ grid record nothing: recording needs another page");
#endif
    /* A7: stopped recording is step entry: each note takes the cursor step, then the cursor advances
     * and wraps at the pattern length (tonal and kit). */
    fresh(); clear_all_steps(); select_track(0); tap_btn(BTN_HOME); tap_btn(BTN_REC);
    br_part[0].length = 3u;
    tap_key(W[0]); tap_key(W[2]); tap_key(W[4]);
    uint32_t steps3 = count_on(0, 0) == 3u && BR_STEP_NOTE(br_part[0].step[0]) == 53u + W[0] &&
        BR_STEP_NOTE(br_part[0].step[1]) == 53u + W[2] && BR_STEP_NOTE(br_part[0].step[2]) == 53u + W[4];
    uint32_t wrapped = br_ui.cursor == 0u;
    tap_key(W[1]);
    CHECK(steps3 && wrapped && BR_STEP_NOTE(br_part[0].step[0]) == 53u + W[1] && br_ui.cursor == 1u,
          "REC stopped, tonal, length 3: three notes fill steps 1-3 in order, the cursor wraps and a fourth replaces step 1");
    fresh(); clear_all_steps(); select_track(BR_DRUM_PART); tap_btn(BTN_HOME); tap_btn(BTN_REC);
    tap_key(W[0]); tap_key(W[1]); tap_key(W[2]);
    CHECK(on(3, 0, W[0] % 4u) && on(3, 1, W[1] % 4u) && on(3, 2, W[2] % 4u) && br_ui.cursor == 3u,
          "REC stopped, kit: three hits land on steps 1, 2, 3 and the cursor moves to step 4");
    /* Kit recording: keys map to lanes key%%4, fixed velocity 100 on FM-1 (no accents from keys). */
    fresh(); clear_all_steps(); select_track(BR_DRUM_PART); tap_btn(BTN_HOME); tap_btn(BTN_REC); tap_btn(BTN_PLAY);
    press_at(2, 10); key_down(W[1]); run_ms(30); key_up(W[1]); run_ms(5);
    uint32_t lane = W[1] % 4u;
    CHECK(on(3, 2, lane) && br_seq_kit_velocity[2][lane] == 100u && !(BR_STEP_ACC(br_part[3].step[2]) & (1u << lane)),
          "kit: white key %u records lane %u at step 3, velocity 100, no accent", W[1], lane + 1u);
    tap_btn(BTN_PLAY);
}

/* 15. MIDI clock start / stop / continue (device configuration only). */
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
static uint32_t ck_half, ck_lead;
static uint64_t ck_frame;
static void ck_block(void)
{
    render_block(); ck_frame += BR_BLOCK; ck_half ^= 1u;
    uint32_t observed = (uint32_t)((ck_frame * 24000000u) / FS);
    observed = ck_half ? observed + 100u : observed - 100u;
    br_engine_clock_dma_edge(observed, ck_half, ck_lead);
}
static void clock_session(uint32_t start_block, uint32_t stop_block, uint32_t cont_block, uint32_t blocks,
                          uint32_t clock_from_start,
                          uint32_t *stop_step, uint32_t *n_stop, uint32_t *n_cont, uint64_t *fa_frame)
{
    fresh(); clear_all_steps(); select_track(0); open_seq_grid();
    for (uint32_t s = 0; s < 16; ++s) synth_step(0, s, 36 + s, 8, 0);
    br_midi_clock_enabled = 1u; ck_frame = 0; ck_half = 0; ck_lead = BR_BLOCK;
    memset(&br_clock_experiment, 0, sizeof br_clock_experiment);
    br_engine_clock_dma_edge(0u, 0u, ck_lead);
    st_n = 0; total_frames = 0;
    uint32_t next_tick = 0;
    for (uint32_t b = 0; b < blocks; ++b) {
        uint32_t now = (uint32_t)((ck_frame * 24000000u) / FS);
        if (b == start_block) { br_engine_clock_midi_event(0xfau, now, br_midi_clock_generation); *fa_frame = ck_frame;
                                if (clock_from_start) next_tick = now + clock_from_start; }
        while ((!clock_from_start || b >= start_block) && next_tick <= now) {
            br_engine_clock_midi_event(0xf8u, next_tick, br_midi_clock_generation); next_tick += 500000u; }
        if (b == stop_block) br_engine_clock_midi_event(0xfcu, now, br_midi_clock_generation);
        if (b == stop_block + 1u) { *stop_step = br_step_pos[0]; *n_stop = st_n; }
        if (b == cont_block) { *n_cont = st_n; br_engine_clock_midi_event(0xfbu, now, br_midi_clock_generation); }
        ck_block();
    }
}
static void t_midi_clock(void)
{
    fresh(); br_midi_clock_enabled = 1u;
    tap_btn(BTN_PLAY);
    CHECK(!br_playing, "PLAY with CLOCK SOURCE on does not start (message \"%s\")", br_ui.message);
    br_midi_clock_enabled = 0u;
    static const uint32_t prerolls[3] = {20u, 20u, 400u};
    for (uint32_t k = 0; k < 3; ++k) {
        uint32_t stop_step = 0, n_stop = 0, n_cont = 0, first_after = 99; uint64_t fa = 0;
        uint32_t s0 = prerolls[k];
        if (k == 0) {
            static const uint32_t gaps[4] = {0u, 24000u, 240000u, 500000u};   /* first F8 0, 1, 10, 20.8 ms after Start */
            for (uint32_t g = 0; g < 4; ++g) {
                clock_session(s0, 100000u, 100000u, s0 + 1500u, gaps[g], &stop_step, &n_stop, &n_cont, &fa);
                st_ev e1[2]; uint32_t h = collect(E_SYN, 0, 255, e1, 2);
                double f1 = ((uint32_t)((fa * 24000000u) / FS) + gaps[g]) * (double)FS / 24000000.0;
                BUG("SEQ-CLOCK-START", !h || (double)e1[0].t - f1 > 4.0 * 918.75,
                    "clock only while the master runs (first F8 %u us after Start): %s%.0f ms after the master's first beat; "
                    "clock failure mask %u", gaps[g] / 24u, h ? "step 1 plays " : "the sequencer never starts within 8.7 s; ",
                    h ? ((double)e1[0].t - f1) * 1000.0 / FS : 0.0, br_clock_last_failure);
                br_clock_last_failure = 0u;
            }
            continue;
        }
        clock_session(s0, s0 + 480u, s0 + 880u, s0 + 1280u, 0u, &stop_step, &n_stop, &n_cont, &fa);
        uint32_t during = 0, starts = 0;
        for (uint32_t i = 0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part == 0) {
            ++starts;
            if (i >= n_stop && i < n_cont) ++during;
            if (i >= n_cont && first_after == 99) first_after = st_log[i].a - 36u;
        }
        /* The master's first clock after Start is beat 1; tick k is at k * 918.75 frames. */
        double tick = 500000.0 * FS / 24000000.0, f0 = 0;
        if (k == 0) f0 = ((uint32_t)((fa * 24000000u) / FS) + 1u) * (double)FS / 24000000.0;
        else while (f0 < (double)fa) f0 += tick;
        st_ev e0[6]; uint32_t n0 = collect(E_SYN, 0, 255, e0, 6);
        char line[200] = "";
        for (uint32_t i = 0; i < n0; ++i) {
            char w[40]; double want = f0 + (e0[i].a - 36u) * 6.0 * tick;
            snprintf(w, sizeof w, " s%u %+.0f", e0[i].a - 35u, (double)e0[i].t - want); strcat(line, w);
        }
        double late = n0 ? (double)e0[0].t - (f0 + (e0[0].a - 36u) * 6.0 * tick) : 0;
        uint32_t pre_ms = k == 0 ? 0u : (uint32_t)(fa * 1000u / FS);
        NOTE("clock 120 BPM, %u ms of clock before Start: first steps heard vs the master's grid (frames):%s", pre_ms, line);
        if (k < 2)
            BUG("SEQ-CLOCK-START", n0 && late > 4.0 * tick,
                "with %u ms of clock before MIDI Start, step 1 plays %.0f frames (%.0f ms) after the master's "
                "first beat and the whole pattern stays that far behind", pre_ms, late, late * 1000.0 / FS);
        else
            CHECK(n0 && late > -(double)BR_BLOCK && late < 2.0 * BR_BLOCK,
                  "with clock running before Start, step 1 lands on the master's first beat (%+.0f frames)", late);
        CHECK(starts > 20u && during == 0u, "MIDI Start plays; Stop silences the sequencer (%u steps during stop)", during);
        CHECK(first_after == (stop_step + 1u) % 16u || first_after == stop_step,
              "Continue resumes from the stopped position (stopped on step %u, resumed with step %u)", stop_step + 1u, first_after + 1u);
        st_ev e[400]; uint32_t n = collect(E_SYN, 0, 255, e, 400), worst = 0;
        for (uint32_t i = 2; i < n && e[i].t < (s0 + 470u) * (uint64_t)BR_BLOCK; ++i) {
            int32_t d = (int32_t)(e[i].t - e[i - 1].t) - (int32_t)(FS / 8u);
            if (d < 0) d = -d; if ((uint32_t)d > worst) worst = (uint32_t)d;
        }
        CHECK(worst <= 64u, "under steady clock, 1/16 steps after the first are %u +- %u frames apart", FS / 8u, worst);
        if (n > 2) NOTE("first step-to-step gap after Start: %lld frames", (long long)(e[1].t - e[0].t));
    }
    br_midi_clock_enabled = 0u;
}
#endif

/* 16. Save and reload (session codec and the SAVE button power-cycle path). */
static uint32_t rng_state = 12345u;
static uint32_t rnd(uint32_t n) { rng_state = rng_state * 1103515245u + 12345u; return (rng_state >> 8) % n; }
static void randomize(void)
{
    for (uint32_t p = 0; p < BR_PARTS; ++p) {
        br_part[p].length = 1u + rnd(64); br_part[p].division = rnd(3);
        if (p < BR_SYNTH_PARTS) {
            br_part[p].arp_mode = rnd(BR_ARP_MODES); br_part[p].arp_rate = rnd(BR_ARP_RATES);
        }
        for (uint32_t s = 0; s < BR_STEPS; ++s) {
            if (p < BR_SYNTH_PARTS) {
                br_step_trigger_clear(p, s, 0);
                if (rnd(2)) {
                    br_part[p].step[s] = BR_NOTE_STEP(rnd(128), 1u + rnd(127)) | (rnd(4) ? 0u : BR_STEP_TIE);
                    br_step_length_set(p, s, 0, 1u + rnd(BR_STEP_LENGTH_MAX));
                    br_step_condition_set(p, s, 0, rnd(BR_STEP_COND_COUNT));
                    br_step_velocity_set(p, s, 0, rnd(128));
                }
            } else for (uint32_t l = 0; l < 4; ++l) {
                br_step_trigger_clear(p, s, l);
                if (rnd(2)) {
                    br_part[p].step[s] |= (1u << l) | (rnd(3) ? 0u : 1u << (l + 8u));
                    br_step_length_set(p, s, l, rnd(BR_STEP_LENGTH_MAX + 1u));
                    br_step_condition_set(p, s, l, rnd(BR_STEP_COND_COUNT));
                    br_step_velocity_set(p, s, l, rnd(128));
                    br_step_note_set(p, s, l, 36u + rnd(49));
                    br_seq_kit_retrig[s][l] = (uint8_t)rnd(64);
                }
            }
        }
    }
    br_swing = rnd(BR_SWING_MAX + 1u);
    for (uint32_t p = 0; p < BR_PARTS; ++p) for (uint32_t s = 0; s < BR_STEPS; ++s)
        for (uint32_t l = 0; l < (p == 3 ? 4u : 1u); ++l) if (on(p, s, l)) {
            int32_t mn, mx; br_micro_bounds(p, s, &mn, &mx);
            br_step_micro_set(p, s, l, mn + (int32_t)rnd((uint32_t)(mx - mn + 1)));
        }
    for (uint32_t p = 0; p < BR_PARTS; ++p) br_seq_record_quantize[p] = (uint8_t)rnd(BR_TIME_QUANT_COUNT);
    br_bpm = BR_BPM_MIN + rnd(BR_BPM_MAX - BR_BPM_MIN + 1u);
}
typedef struct { snap_t s[4]; uint32_t len[4], div[4], swing, bpm, q[4], arp[3][2]; } world_t;
static void world(world_t *w)
{
    memset(w, 0, sizeof *w);
    for (uint32_t p = 0; p < 4; ++p) { snap(p, &w->s[p]); w->len[p] = br_part[p].length; w->div[p] = br_part[p].division; w->q[p] = br_seq_record_quantize[p]; }
    for (uint32_t p = 0; p < 3; ++p) { w->arp[p][0] = br_part[p].arp_mode; w->arp[p][1] = br_part[p].arp_rate; }
    w->swing = br_swing; w->bpm = br_bpm;
}
static void world_report(const world_t *a, const world_t *b, uint32_t *cells, uint32_t *fields)
{
    *cells = 0; *fields = 0;
    for (uint32_t p = 0; p < 4; ++p) *cells += snap_diff(&a->s[p], &b->s[p], p, 999, 999);
    for (uint32_t p = 0; p < 4; ++p) *fields += (a->len[p] != b->len[p]) + (a->div[p] != b->div[p]) + (a->q[p] != b->q[p]);
    for (uint32_t p = 0; p < 3; ++p) *fields += (a->arp[p][0] != b->arp[p][0]) + (a->arp[p][1] != b->arp[p][1]);
    *fields += (a->swing != b->swing) + (a->bpm != b->bpm);
}
static void t_save(void)
{
    static br_session_t s; static world_t a, b;
    uint32_t rounds = SLOW ? 20u : 300u, bad = 0, invalid = 0, cells = 0, fields = 0;
    fresh();
    for (uint32_t r = 0; r < rounds; ++r) {
        randomize(); world(&a);
        if (!br_capture(&s)) { ++invalid; continue; }
        clear_all_steps(); br_bpm = 77u;
        if (!br_session_valid(&s) || !br_restore(&s)) { ++invalid; continue; }
        world(&b); world_report(&a, &b, &cells, &fields);
        if (cells || fields) { if (!bad) NOTE("round %u: %u cells and %u fields differ", r, cells, fields); ++bad; }
    }
    CHECK(!bad && !invalid, "%u random full-range patterns survive capture -> validate -> restore unchanged "
          "(%u differ, %u rejected)", rounds, bad, invalid);
    /* The SAVE button path, then a simulated power cycle. */
    fresh(); randomize(); br_playing = 0u; world(&a);
    tap_btn(BTN_SAVE); run_ms(300);
    uint32_t saved = host_store[0] == 1u;
    for (uint32_t p = 0; p < 4; ++p) for (uint32_t i = 0; i < BR_STEPS; ++i) br_part[p].step[i] = 0xdeadbeefu;
    memset((void *)br_seq_synth_length, 0x5a, sizeof br_seq_synth_length);
    memset((void *)br_seq_kit_retrig, 0x5a, sizeof br_seq_kit_retrig);
    memset((void *)br_seq_kit_micro, 0x5a, sizeof br_seq_kit_micro);
    br_app_init(); run_ms(5);
    world(&b); world_report(&a, &b, &cells, &fields);
    CHECK(saved && !cells && !fields, "SAVE then power cycle restores every step, lane, length, speed, swing, tempo, "
          "quantize and arp field (%u cells, %u fields differ; %s)", cells, fields, br_ui.message);
    /* SAVE while playing is refused. */
    tap_btn(BTN_PLAY); run_ms(50); host_store[0] = 0; tap_btn(BTN_SAVE); run_ms(100);
    CHECK(host_store[0] == 0u && !strcmp(br_ui.message, "STOP BEFORE SAVE"), "SAVE while playing is refused: \"%s\"", br_ui.message);
    tap_btn(BTN_PLAY);
    NOTE("not persisted: insertion defaults (KNOB values on the grid), cursor/bank, REC/ARP/HOLD state, fill");
}

/* 17. Cold-start stagger: simultaneous first notes after silence flam. */
static void t_cold_flam(void)
{
    fresh(); clear_all_steps(); br_bpm = 120u; play_reset(); clear_all_steps();
    for (uint32_t p = 0; p < 3; ++p) {
        br_part[p].length = 32u; synth_step(p, 0, 60 + p * 4, 2, 0);
        br_part[p].revision++; br_part[p].sound.release = 0u; br_part[p].sound.decay = 10u; br_part[p].sound.sustain = 0u; br_part[p].revision++;
    }
    br_part[3].length = 32u; kit_step(0, 0, 60, 0, 0);
    play_start(); render_frames((uint64_t)br_step_duration(0, 0) * 32u * 3u);
    /* For each loop, compare the logical step time with each track's actual voice start block. */
    uint32_t loops = 0, worst = 0, flams = 0;
    for (uint32_t i = 0; i < st_n; ++i) if (st_log[i].kind == E_SYN && st_log[i].part == 0) {
        uint64_t t = st_log[i].t; uint32_t late_max = 0;
        for (uint32_t p = 0; p < 3; ++p) for (uint32_t j = 0; j < st_n; ++j)
            if (st_log[j].kind == E_VOICE && st_log[j].part == p && st_log[j].block + BR_BLOCK > t && st_log[j].block < t + 8u * BR_BLOCK) {
                uint32_t late = (uint32_t)(st_log[j].block - (t - t % BR_BLOCK));
                if (late > late_max) late_max = late; break;
            }
        ++loops; if (late_max > worst) worst = late_max; if (late_max >= BR_BLOCK) ++flams;
    }
    BUG("SEQ-COLD-FLAM", flams > 0u,
        "three synth tracks starting together after silence: in %u of %u loops the starts are spread over %u frames "
        "(%.1f ms); one cold Plaits start is admitted per block (BR_AUDIO_STAGGER_COLD_SYNTH_STARTS)",
        flams, loops, worst, worst * 1000.0 / FS);
}

/* 19. The sequencer sends no MIDI notes or clock. */
static void t_midi_out(void)
{
    fresh(); clear_all_steps(); play_reset(); clear_all_steps();
    for (uint32_t p = 0; p < 3; ++p) for (uint32_t s = 0; s < 16; s += 2) synth_step(p, s, 60 + s, 8, 0);
    for (uint32_t s = 0; s < 16; ++s) kit_step(s, s % 4u, 60, 0, 0);
    uint32_t before = host_midi_out_count;
    play_start(); run_ms(4000); br_playing = 0u; run_ms(10);
    uint32_t heard = 0; for (uint32_t i = 0; i < st_n; ++i) heard += st_log[i].kind == E_SYN || st_log[i].kind == E_KIT;
    CHECK(heard > 50u && host_midi_out_count == before,
          "two bars with %u sequenced notes on all four tracks send %u MIDI messages: no note or clock output",
          heard, host_midi_out_count - before);
}

#ifdef SEQTEST_PATTERN_STUBS
/* 20. Pattern slots on the FM-1 device-candidate build (BR_PATTERN_SONG_EXPERIMENT). */
static void t_patterns(void)
{
    fresh(); select_track(0); open_seq_grid();
    btn_down(BTN_OCT_DOWN); run_ms(10); btn_down(BTN_SEQ); run_ms(10); btn_up(BTN_SEQ); btn_up(BTN_OCT_DOWN); run_ms(30);
    CHECK(br_ui.page == BR_PAGE_PS, "OCT- + SEQ opens the pattern page (message \"%s\", hint \"%s\")", br_ui.message, br_oct_hint());
    tap_key(B[4]);
    CHECK(stub_selected == 4u, "a black key tap selects pattern slot %u", stub_selected + 1u);
    key_down(B[6]); run_ms(30); tap_btn(BTN_REC); key_up(B[6]); run_ms(20);
    key_down(B[7]); run_ms(30); tap_btn(BTN_PLAY); key_up(B[7]); run_ms(20);
    CHECK(stub_copied == 6u && stub_pasted == 7u && stub_selected == 4u,
          "hold slot + REC copies (slot %u), hold slot + PLAY pastes (slot %u), without selecting", stub_copied + 1u, stub_pasted + 1u);
    tap_key(W[3]);
    CHECK(stub_selected == 4u, "white keys do nothing on the pattern page (11 slots, black keys only)");
    tap_btn(BTN_SEQ);
    NOTE("SEQ on the pattern page shows \"%s\" and stays on page %u", br_ui.message, br_ui.page);
}
#endif

/* 18. What the hint bar says in each sequencer state. */
static void t_hints(void)
{
    fresh(); select_track(0); open_seq_grid();
    NOTE("tonal grid: status \"%s\"", br_oct_hint());
    tap_key(W[0]); key_down(W[0]); run_ms(400);
    NOTE("holding a step: status shows micro-timing \"OCT-/+ %d/128\"; knobs are NOTE/LENGTH/VELOCITY/CONDITION", br_step_micro_get(0, 0, 0));
    key_up(W[0]); run_ms(20);
    tap_btn(BTN_SEQ);
    NOTE("settings page (screen %u): status \"%s\"; knobs STEPS/SPEED/QUANTIZE", br_ui.screen[BR_PAGE_SEQ], br_oct_hint());
    select_track(BR_DRUM_PART); open_seq_grid();
    NOTE("kit grid: status bar is the SELECT DRUM cue (cue kind 6); OCT bank hint is not shown on the kit grid");
    tap_btn(BTN_ARP); run_ms(900);
    NOTE("retrigger page: status \"%s\"", br_oct_hint());
    tap_btn(BTN_SEQ); btn_down(BTN_SEQ); run_ms(5); turn(ENC_KNOB1, 1); btn_up(BTN_SEQ); run_ms(5);
    NOTE("SEQ+KNOB1: message \"%s\"", br_ui.message);
}

typedef struct { const char *name; void (*fn)(void); } test_t;
static void t_shift_notes(void)
{
    fresh(); select_track(0); open_seq_grid(); clear_all_steps();
    br_part[0].length = 8u;
    br_part[0].step[0] = BR_NOTE_STEP(60, 91);
    br_part[0].step[7] = BR_NOTE_STEP(72, 84);
    br_part[0].step[8] = BR_NOTE_STEP(80, 90);
    br_seq_synth_micro[0][7] = -3;
    br_seq_synth_condition[0][7] = BR_STEP_COND_FILL;
    br_seq_synth_length[0][7] = 23;
    br_part[1].step[0] = BR_NOTE_STEP(55, 88);
    uint32_t recording = br_recording;
    btn_down(BTN_OCT_UP); run_ms(10); btn_down(BTN_REC); run_ms(10);
    btn_up(BTN_REC); btn_up(BTN_OCT_UP); run_ms(30);
    CHECK(BR_STEP_NOTE(br_part[0].step[0]) == 72 && BR_STEP_NOTE(br_part[0].step[1]) == 60 &&
          BR_STEP_NOTE(br_part[0].step[8]) == 80 && BR_STEP_NOTE(br_part[1].step[0]) == 55 &&
          br_seq_synth_micro[0][0] == -3 && br_seq_synth_condition[0][0] == BR_STEP_COND_FILL &&
          br_seq_synth_length[0][0] == 23 && br_recording == recording,
          "forward wraps selected track and carries metadata without changing recording or hidden steps");
    btn_down(BTN_OCT_DOWN); run_ms(10); btn_down(BTN_REC); run_ms(10);
    btn_up(BTN_REC); btn_up(BTN_OCT_DOWN); run_ms(30);
    CHECK(BR_STEP_NOTE(br_part[0].step[0]) == 60 && BR_STEP_NOTE(br_part[0].step[7]) == 72 &&
          br_seq_synth_micro[0][7] == -3 && br_recording == recording,
          "backward wraps and restores the original sequence");
    select_track(BR_DRUM_PART); open_seq_grid(); br_part[BR_DRUM_PART].length = 4u;
    br_part[BR_DRUM_PART].step[3] = 9u;
    br_seq_kit_retrig[3][3] = 17u; br_seq_kit_micro[3][3] = -4;
    btn_down(BTN_OCT_UP); run_ms(10); btn_down(BTN_REC); run_ms(10);
    btn_up(BTN_REC); btn_up(BTN_OCT_UP); run_ms(30);
    CHECK(br_part[BR_DRUM_PART].step[0] == 9u && br_seq_kit_retrig[0][3] == 17u &&
          br_seq_kit_micro[0][3] == -4 && br_recording == recording,
          "kit lanes and their retrigger and timing metadata wrap together");
}
static const test_t tests[] = {
    {"shift_notes", t_shift_notes},
    {"toggle", t_toggle}, {"hold_edit", t_hold_edit}, {"lengths", t_lengths}, {"stop_start", t_stop_start},
    {"ties", t_ties_lengths}, {"conditions", t_conditions}, {"retrig", t_retrig}, {"swing", t_swing},
    {"tempo", t_tempo}, {"mid_edit", t_mid_edit}, {"two_steps", t_two_steps}, {"edit_playing", t_edit_playing},
    {"record", t_record},
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    {"midi_clock", t_midi_clock},
#endif
    {"save", t_save}, {"cold_flam", t_cold_flam}, {"hints", t_hints}, {"midi_out", t_midi_out},
#ifdef SEQTEST_PATTERN_STUBS
    {"patterns", t_patterns},
#endif
};

int main(int argc, char **argv)
{
    if (!host_init_resources()) { puts("FAIL resources"); return 1; }
    printf("config: BR_BLOCK %u, FS %u, MIDI clock experiment %d, sanitizer %d\n", BR_BLOCK, FS,
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
           1,
#else
           0,
#endif
           SLOW);
    for (uint32_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (argc > 1) {
            const char *list = argv[1], *hit = strstr(list, tests[i].name);
            size_t n = strlen(tests[i].name);
            if (!hit || (hit != list && hit[-1] != ',') || (hit[n] && hit[n] != ',')) continue;
        }
        cur = tests[i].name; tests[i].fn();
        if (st_overflow) FAILF("event log overflow");
    }
    printf("summary: %u passed, %u failed, %u defects reproduced\n", passes, fails, bugs);
    return fails ? 1 : 0;
}
