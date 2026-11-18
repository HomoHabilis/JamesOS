/* Played-note timing through the real loop (br_app_tick) and the real engine (app_audio). No hardware.
 *
 * A simulated 24 MHz timer runs the audio halves (256 frames at 44.1 kHz) at their exact times and the
 * foreground loop every 0.25 ms, except while it is "held" (standing in for a screen redraw). MIDI notes
 * and note keys arrive at known ticks; the onset of each note is the first output sample above a
 * threshold, as a time on the same timer (the half's render tick + its frame offset; the DMA/DAC output
 * delay is a constant on top and is not included). Prints a table per hold and checks the design:
 *   - holds <= BR_NOTE_DELAY_MS: each note's onset is the same, sample for sample, as with no hold;
 *   - longer holds: the onset follows the release of the hold by at most one block (+ attack);
 *   - a six-note burst with its offs under a 20 ms hold, and a 200-note run with repeated holds:
 *     nothing dropped, order kept, every note sounds and ends.
 * Compiled against the code before played-note timing (no BR_NOTE_DELAY_MS) it prints the same tables
 * without the checks: the "before" numbers.
 *
 *   test_note_timing            the tables and checks
 *   test_note_timing rec HOLD   record one MIDI note while playing with the loop held HOLD ms; prints
 *                               the recorded step and micro offset (the runner compares holds) */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define T_MS 24000u
#define SIM_STEP 3000u                    /* 0.125 ms */
#define FG_PERIOD 6000u                   /* the loop's own pass: 0.25 ms */
#define THRESHOLD 300
#define MAX_FRAMES (100u * 44100u)
#ifdef BR_NOTE_DELAY_MS
#define NEW_CODE 1
#else
#define NEW_CODE 0
#endif

static int failed;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); ++failed; } } while (0)

typedef struct { uint32_t at, hold; uint8_t kind, st, d1, d2; } sim_ev_t;   /* kind 0 MIDI, 1 key down, 2 key up */
static sim_ev_t evs[4096];
static uint32_t nev, next_ev;
static uint32_t now_t, half_k, fg_free_at, next_fg;
static int32_t *outbuf;                   /* mono (left) per frame */
static uint32_t nframes;
static uint32_t ring_max;
static uint32_t env_peak[BR_PARTS];

static uint32_t starts[4096], nstarts, prev_stage;   /* track 1's note starts: their block's start tick */
static uint32_t half_tick(uint32_t k) { return (uint32_t)(((uint64_t)k * 256u * 24000000u + 22050u) / 44100u); }
static double frame_tick(uint32_t f)
{
    return (double)half_tick(f / 256u) + (double)(f % 256u) * 24000000.0 / 44100.0;
}

static void add_ev(uint32_t at, uint32_t hold, uint8_t kind, uint8_t st, uint8_t d1, uint8_t d2)
{
    sim_ev_t e = {at, hold, kind, st, d1, d2};
    uint32_t i = nev++;
    while (i && evs[i - 1].at > at) { evs[i] = evs[i - 1]; --i; }
    evs[i] = e;
}

static void inject(const sim_ev_t *e)
{
    if (e->kind == 0) {
        mq[mq_w % 64u][0] = e->st; mq[mq_w % 64u][1] = e->d1; mq[mq_w % 64u][2] = e->d2;
#if NEW_CODE
        mq_time[mq_w % 64u] = e->at; mq_stamped[mq_w % 64u] = 1u;
#endif
        mq_w++;
    } else {
        uint32_t bit = 1u << e->d1;
#if NEW_CODE
        host_note_tick[e->d1] = e->at;
#endif
        if (e->kind == 1) { fm1_in.notes |= bit; host_note_pressed |= bit; }
        else fm1_in.notes &= ~bit;
#if NEW_CODE
        host_note_stamped = fm1_in.notes;
#endif
    }
    if (e->hold && e->at + e->hold > fg_free_at) fg_free_at = e->at + e->hold;
}

static void render_half(void)
{
    static int32_t blk[2u * 256u];
    uint32_t i, p;
#if NEW_CODE
    br_engine_note_clock(half_tick(half_k));
#endif
    memset(blk, 0, sizeof blk);
    app_audio(blk, 256u);
    for (i = 0; i < 256u && nframes < MAX_FRAMES; ++i) outbuf[nframes++] = blk[2u * i];
    for (p = 0; p < BR_SYNTH_PARTS; ++p) if ((uint32_t)br_env[p].amp > env_peak[p]) env_peak[p] = (uint32_t)br_env[p].amp;
    if (br_env[0].stage != BR_ENV_OFF && prev_stage == BR_ENV_OFF && nstarts < 4096u)
        starts[nstarts++] = half_tick(half_k);
    prev_stage = br_env[0].stage;
    ++half_k;
}

/* Run the timeline to tick end. */
static void run_until(uint32_t end)
{
    while ((int32_t)(now_t - end) < 0) {
        while (next_ev < nev && evs[next_ev].at <= now_t) inject(&evs[next_ev++]);
        while ((int32_t)(half_tick(half_k) - now_t) <= 0) render_half();
        if ((int32_t)(now_t - fg_free_at) >= 0 && (int32_t)(now_t - next_fg) >= 0) {
            uint32_t used;
#if NEW_CODE
            host_ticks = now_t;
#endif
            fm1_ms = now_t / T_MS;
            br_app_tick();
            used = br_event_write - br_event_read;
            if (used > ring_max) ring_max = used;
            next_fg = now_t + FG_PERIOD;
        }
        now_t += SIM_STEP;
    }
}

/* The engine's start of track 1's next note, ms after arrival (its block's start), or -1. */
static double start_ms(uint32_t arrival)
{
    uint32_t i;
    for (i = 0; i < nstarts; ++i)
        if ((int32_t)(starts[i] - arrival) > -24000 && (int32_t)(starts[i] - arrival) < 100 * 24000)
            return (double)(int32_t)(starts[i] - arrival) / T_MS;
    return -1.0;
}

/* Onset in ms after arrival (render time), or -1 when nothing sounded within 100 ms. silent: whether the
 * 20 ms before arrival were quiet. */
static double onset_ms(uint32_t arrival, int *silent)
{
    uint32_t f = (uint32_t)((double)arrival * 44100.0 / 24000000.0), from = f > 900u ? f - 900u : 0u, i;
    *silent = 1;
    for (i = from; i < f && i < nframes; ++i) if (abs(outbuf[i]) > THRESHOLD) *silent = 0;
    for (i = f > 300u ? f - 300u : 0u; i < nframes && i < f + 4410u; ++i)
        if (abs(outbuf[i]) > THRESHOLD) return (frame_tick(i) - (double)arrival) / T_MS;
    return -1.0;
}

static void setup(void)
{
    br_app_init();
    memset(&br_midi_routes, 0, sizeof br_midi_routes);
    for (unsigned i = 0; i < 7; ++i) br_midi_channels[i] = (uint8_t)(i + 1);
    br_ui.page = BR_PAGE_HOME;
}

static void print_table(const char *code, const char *what, double v[6][8])
{
    static const uint32_t holds[] = {0, 2, 5, 10, 20, 50};
    uint32_t h, j;
    printf("%s code: MIDI note, %s, ms after arrival (render time; + constant output delay)\n", code, what);
    printf("hold ms | arrival phase 0, 0.75 .. 5.25 ms                         |   min    max spread\n");
    for (h = 0; h < 6; ++h) {
        double lo = 1e9, hi = -1e9;
        printf("%7u |", holds[h]);
        for (j = 0; j < 8; ++j) { printf(" %6.2f", v[h][j]); if (v[h][j] < lo) lo = v[h][j]; if (v[h][j] > hi) hi = v[h][j]; }
        printf(" | %6.2f %6.2f %6.2f\n", lo, hi, hi - lo);
    }
}

/* Cases one second apart, a whole number of halves, so each phase sits at the same place in a block
 * for every hold. */
#define CASE_HALVES 172u
#define MIDI_AT(h, j) (half_tick(CASE_HALVES * (1u + (h) * 8u + (j))) + (j) * 18000u)
#define KEY_AT(h, j) (half_tick(CASE_HALVES * (1u + 6u * 8u + (h) * 4u + (j))) + (j) * 33000u)

static int record_mode(uint32_t hold_ms)
{
    uint32_t a, s, found = 0, step = 0;
    int32_t micro = 0;
    setup();
    br_recording = 1; br_playing = 1;
    for (uint32_t q = 0; q < BR_PARTS; ++q) br_seq_record_quantize[q] = BR_TIME_QUANT_OFF;
    run_until(400u * T_MS);
    a = 1000u * T_MS + 17u * SIM_STEP;
    add_ev(a, hold_ms * T_MS, 0, 0x90, 60, 100);
    add_ev(a + 80u * T_MS, 0, 0, 0x80, 60, 0);
    run_until(a + 200u * T_MS);
    for (s = 0; s < BR_STEPS; ++s) if (br_part[0].step[s] & BR_STEP_ON) {
        ++found; step = s; micro = br_step_micro_get(0, s, 0);
    }
    printf("rec hold %u ms: %u steps, step %u micro %d\n", hold_ms, found, step, (int)micro);
    return found == 1 ? 0 : 1;
}

int main(int argc, char **argv)
{
    static const uint32_t holds[] = {0, 2, 5, 10, 20, 50};
    enum { NH = 6, NP = 8 };
    double on[NH][NP], key_on[3][4], st[NH][NP], key_st[3][4];
    uint32_t h, j, t;
    outbuf = calloc(MAX_FRAMES, sizeof *outbuf);
    if (argc > 2 && !strcmp(argv[1], "rec")) return record_mode((uint32_t)atoi(argv[2]));
    setup();
    run_until(300u * T_MS);
    /* MIDI: each hold, eight arrival phases across a block; a note every second */
    for (h = 0; h < NH; ++h) for (j = 0; j < NP; ++j) {
        uint32_t a = MIDI_AT(h, j);
        add_ev(a, holds[h] * T_MS, 0, 0x90, 60, 110);
        add_ev(a + 40u * T_MS, 0, 0, 0x80, 60, 0);
    }
    /* Local note keys: holds 0, 5, 20; four phases */
    for (h = 0; h < 3; ++h) for (j = 0; j < 4; ++j) {
        uint32_t a = KEY_AT(h, j);
        add_ev(a, (h == 0 ? 0u : h == 1 ? 5u : 20u) * T_MS, 1, 0, 7, 0);
        add_ev(a + 40u * T_MS, 0, 2, 0, 7, 0);
    }
    t = half_tick(CASE_HALVES * (1u + NH * NP + 12u));
    run_until(t + 500u * T_MS);
    for (h = 0; h < NH; ++h) for (j = 0; j < NP; ++j) {
        int silent; uint32_t a = MIDI_AT(h, j);
        on[h][j] = onset_ms(a, &silent);
        st[h][j] = start_ms(a);
        CHECK(silent, "MIDI hold %u phase %u: not silent before the note", holds[h], j);
    }
    for (h = 0; h < 3; ++h) for (j = 0; j < 4; ++j) {
        int silent; uint32_t a = KEY_AT(h, j);
        key_on[h][j] = onset_ms(a, &silent);
        key_st[h][j] = start_ms(a);
        CHECK(silent, "key hold %u phase %u: not silent before the note", h, j);
    }
    print_table(NEW_CODE ? "new" : "old", "engine start (its block's start)", st);
    print_table(NEW_CODE ? "new" : "old", "audible onset (first sample > threshold)", on);
    printf("local keys, engine start then audible onset (hold 0, 5, 20 ms; four phases):\n");
    for (h = 0; h < 3; ++h) {
        printf("%7u |", h == 0 ? 0u : h == 1 ? 5u : 20u);
        for (j = 0; j < 4; ++j) printf(" %6.2f", key_st[h][j]);
        printf(" |");
        for (j = 0; j < 4; ++j) printf(" %6.2f", key_on[h][j]);
        printf("\n");
    }
    for (h = 0; h < NH; ++h) for (j = 0; j < NP; ++j) CHECK(on[h][j] >= 0.0, "MIDI hold %u phase %u: no sound", holds[h], j);
#if NEW_CODE
    {
        double block = 256.0 * 1000.0 / 44100.0;
        for (h = 0; h < NH; ++h) for (j = 0; j < NP; ++j) {
            if (holds[h] <= BR_NOTE_DELAY_MS)
                CHECK(fabs(st[h][j] - st[0][j]) < 0.01, "hold %u ms phase %u: start %.3f ms, without hold %.3f ms",
                      holds[h], j, st[h][j], st[0][j]);
            else
                CHECK(st[h][j] >= holds[h] && st[h][j] < holds[h] + block + 0.3,
                      "hold %u ms phase %u: start %.3f ms", holds[h], j, st[h][j]);
        }
        for (j = 0; j < NP; ++j)
            CHECK(st[0][j] >= BR_NOTE_DELAY_MS && st[0][j] < BR_NOTE_DELAY_MS + block,
                  "phase %u: start %.3f ms outside [L, L + block)", j, st[0][j]);
        for (j = 0; j < 4; ++j) {
            CHECK(fabs(key_st[1][j] - key_st[0][j]) < 0.01, "key hold 5 phase %u: %.3f vs %.3f", j, key_st[1][j], key_st[0][j]);
            CHECK(key_st[2][j] >= 20.0 && key_st[2][j] < 20.0 + block + 0.3, "key hold 20 phase %u: %.3f", j, key_st[2][j]);
        }
    }
#endif
    /* Burst: six notes within 1 ms on six destinations, their offs 3 ms later, the loop held 20 ms */
    {
        static const uint8_t ch_note[6][2] = {{0, 60}, {1, 64}, {2, 67}, {3, 36}, {4, 38}, {5, 42}};
        uint32_t a = t + 1000u * T_MS, i, before_drop = br_dropped_events;
        for (i = 0; i < BR_SYNTH_PARTS; ++i) env_peak[i] = 0;
        for (i = 0; i < 6; ++i) {
            add_ev(a + i * 1000u, i ? 0u : 20u * T_MS, 0, (uint8_t)(0x90 | ch_note[i][0]), ch_note[i][1], 100);
            add_ev(a + 3u * T_MS + i * 1000u, 0, 0, (uint8_t)(0x80 | ch_note[i][0]), ch_note[i][1], 0);
        }
        run_until(a + 20u * T_MS);                 /* the loop is still held: nothing processed yet */
        while ((int32_t)(now_t - next_fg) < 0 || (int32_t)(now_t - fg_free_at) < 0) {
            while (next_ev < nev && evs[next_ev].at <= now_t) inject(&evs[next_ev++]);
            while ((int32_t)(half_tick(half_k) - now_t) <= 0) render_half();
            now_t += SIM_STEP;
        }
        fm1_ms = now_t / T_MS;
#if NEW_CODE
        host_ticks = now_t;
#endif
        br_app_tick();                              /* one pass: all twelve messages, before any audio */
        {
            uint32_t r = br_event_read, n = br_event_write - r, ons = 0, offs = 0, order = 1;
            for (i = 0; i < n; ++i) {
                uint32_t e = br_events[(r + i) & BR_EVENT_MASK], op = e >> 30;
                if (op == BR_EVENT_ON) { if (offs) order = 0; ++ons; }
                else if (op == BR_EVENT_OFF) ++offs;
            }
            printf("burst: %u events queued after the held pass (%u on, %u off), ons before offs: %s\n",
                   n, ons, offs, order ? "yes" : "no");
            CHECK(ons == 6 && offs == 6 && order, "burst queue: %u on %u off order %u", ons, offs, order);
        }
        next_fg = now_t + FG_PERIOD;
        run_until(a + 300u * T_MS);
        {
            int silent; double o = onset_ms(a, &silent);
            printf("burst: onset %.2f ms after the first arrival; synth envelope peaks %u %u %u; stages %u %u %u\n",
                   o, env_peak[0], env_peak[1], env_peak[2], br_env[0].stage, br_env[1].stage, br_env[2].stage);
            CHECK(o >= 0.0, "burst: no sound");
            for (i = 0; i < BR_SYNTH_PARTS; ++i) {
                CHECK(env_peak[i] > 0u, "burst: track %u never sounded", i + 1);
                CHECK(br_env[i].stage == BR_ENV_OFF || br_env[i].stage == BR_ENV_RELEASE,
                      "burst: track %u still gated (stage %u)", i + 1, br_env[i].stage);
            }
        }
        CHECK(br_dropped_events == before_drop, "burst: events dropped");
        CHECK(br_event_read == br_event_write, "burst: ring not drained");
        t = a + 300u * T_MS;
    }
    /* 200 fast notes (one every 2 ms, 1 ms long), the loop held 20 ms in every 40 */
    {
        uint32_t a = t + 500u * T_MS, i, before_drop = br_dropped_events;
        ring_max = 0;
        for (i = 0; i < 200; ++i) {
            uint32_t at = a + i * 2u * T_MS;
            add_ev(at, (i % 20u) == 0 ? 20u * T_MS : 0u, 0, 0x90, (uint8_t)(48 + i % 24), 100);
            add_ev(at + T_MS, 0, 0, 0x80, (uint8_t)(48 + i % 24), 0);
        }
        run_until(a + 900u * T_MS);
        printf("fast run: 400 events, ring peak %u of %u, dropped %u, final stage %u\n",
               ring_max, BR_EVENT_CAP, br_dropped_events - before_drop, br_env[0].stage);
        CHECK(br_dropped_events == before_drop, "fast run: events dropped");
        CHECK(br_event_read == br_event_write, "fast run: ring not drained");
        CHECK(br_env[0].stage == BR_ENV_OFF || br_env[0].stage == BR_ENV_RELEASE, "fast run: note stuck");
    }
    if (failed) { fprintf(stderr, "note timing: %d check(s) failed\n", failed); return 1; }
    printf("note timing: all checks passed%s\n", NEW_CODE ? "" : " (old code: tables only)");
    return 0;
}
