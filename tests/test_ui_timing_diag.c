/* SPDX-License-Identifier: GPL-3.0-only
 * UI timing diagnostic (BR_UI_TIMING_DIAG, firmware/src/ui_timing_diag.c) through the actual app command
 * handler and main-loop tick, with the device SAVE menu (DINGHY_DEVICE_SAVE_TEST): remote buttons and
 * encoders change pages as the panel does, the SAVE-page guard refuses or strips everything but HOME and
 * encoder turns, and the counters move plausibly. Built with BR_UI_TIMING_DIAG=0, it checks that 0xB5..0xB8
 * are refused. Host only; never touches hardware. */
#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls;
uint32_t br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
#define FM1_AUDIO_METRICS_WORDS 10u
static void fm1_audio_metrics_snapshot(uint32_t out[10]) { memset(out, 0, 40u); }
static void fm1_audio_metrics_reset(void) { }
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out, 0, 76u); out[0] = 1u; }
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define BR_SPLIT_AUDIO 1
#define BR_DIAGNOSTIC_TEST 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/instrument_audio_unit.c"
#include <assert.h>
#include <stdio.h>

static uint32_t fails;
#define CHECK(c, ...) do { if (!(c)) { ++fails; fprintf(stderr, "FAIL line %d: ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void tick_extra(uint32_t extra_ms)       /* one host millisecond (plus extra_ms of a slow pass) */
{
    fm1_ms += 1u + extra_ms;
    host_ticks = fm1_ms * 24000u;
    br_app_tick();
}
static void run_ms(uint32_t n) { while (n--) tick_extra(0u); }

#if BR_UI_TIMING_DIAG
static uint32_t c[BR_UIT_WORDS];
static void read_counters(void)
{
    uint8_t reply[128];
    uint32_t n = 0u, i;
    assert(fm1_app_command(0xB5u, 0, 0u, reply, &n));
    assert(n == 4u + 4u * BR_UIT_WORDS && reply[0] == 'U' && reply[1] == 'T' && reply[2] == 1u && reply[3] == BR_UIT_WORDS);
    for (i = 0u; i < BR_UIT_WORDS; ++i) c[i] = word(reply + 4u + 4u * i);
}
static void reset_counters(void)
{
    const uint8_t body[3] = {'U', 'T', 1u};
    uint8_t reply[128];
    uint32_t n = 0u;
    assert(fm1_app_command(0xB6u, body, 3u, reply, &n) && n == 1u && reply[0] == 1u);
}
static uint8_t where[36];
static void read_where(void)
{
    uint32_t n = 0u;
    assert(fm1_app_command(0xB8u, 0, 0u, where, &n) && n == 36u && where[0] == 'U' && where[1] == 'W');
}
static uint32_t inject(uint32_t kind, uint32_t index, int32_t value)
{
    uint8_t body[7] = {'U', 'T', 1u, (uint8_t)kind, (uint8_t)index, (uint8_t)value, (uint8_t)((uint32_t)value >> 8)};
    uint8_t reply[128];
    uint32_t n = 0u;
    if (kind == 2u) body[6] = 0u;
    assert(fm1_app_command(0xB7u, body, 7u, reply, &n) && n == 3u);
    return reply[0];
}
static uint32_t button(uint32_t b, uint32_t hold) { return inject(1u, b, (int32_t)hold); }
static uint32_t turn(uint32_t e, int32_t d) { return inject(2u, e, d); }
static void settle(void) { run_ms(400u); read_where(); }   /* the tap, its release and a frame or more */
#endif

int main(void)
{
    uint8_t reply[128];
    uint32_t n = 0u;
    br_app_init();
#if BR_UI_TIMING_DIAG
    static uint8_t flash_before[sizeof host_project_flash];
    run_ms(4000u);                               /* past the intro */
    read_where();
    CHECK(where[3] == BR_PAGE_HOME && !where[6], "start: page %u save %u", where[3], where[6]);

    /* Counters: a quiet second, one slow pass, one late MIDI note. */
    reset_counters();
    run_ms(1000u);
    read_counters();
    CHECK(c[BR_UIT_PASSES] == 1000u, "passes %u", c[BR_UIT_PASSES]);
    CHECK(c[BR_UIT_ELAPSED_MS] == 1000u, "elapsed %u ms", c[BR_UIT_ELAPSED_MS]);
    CHECK(c[BR_UIT_PASS_WORST] == 24000u && !c[BR_UIT_PASS_OVER5], "worst %u over5 %u", c[BR_UIT_PASS_WORST], c[BR_UIT_PASS_OVER5]);
    CHECK(c[BR_UIT_DRAW_PASSES] >= 20u && c[BR_UIT_FRAMES] >= 20u && c[BR_UIT_FRAMES] <= c[BR_UIT_DRAW_PASSES],
          "draw passes %u frames %u", c[BR_UIT_DRAW_PASSES], c[BR_UIT_FRAMES]);
    CHECK(c[BR_UIT_FRAME_WORST] <= c[BR_UIT_FRAME_TOTAL] && !c[BR_UIT_INPUTS] && !c[BR_UIT_NOTES],
          "frame worst %u total %u inputs %u notes %u", c[BR_UIT_FRAME_WORST], c[BR_UIT_FRAME_TOTAL], c[BR_UIT_INPUTS], c[BR_UIT_NOTES]);
    tick_extra(11u);
    read_counters();
    CHECK(c[BR_UIT_PASS_OVER5] == 1u && c[BR_UIT_PASS_OVER10] == 1u && c[BR_UIT_PASS_WORST] == 12u * 24000u,
          "slow pass: over5 %u over10 %u worst %u", c[BR_UIT_PASS_OVER5], c[BR_UIT_PASS_OVER10], c[BR_UIT_PASS_WORST]);
    mq[mq_w % 64u][0] = 0x90; mq[mq_w % 64u][1] = 60; mq[mq_w % 64u][2] = 100;
    mq_time[mq_w % 64u] = host_ticks + 24000u - 8u * 24000u; mq_stamped[mq_w % 64u] = 1u; mq_w++;   /* 8 ms old next pass */
    tick_extra(0u);
    mq[mq_w % 64u][0] = 0x80; mq[mq_w % 64u][1] = 60; mq[mq_w % 64u][2] = 0;
    mq_time[mq_w % 64u] = host_ticks + 24000u - 2u * 24000u; mq_stamped[mq_w % 64u] = 1u; mq_w++;   /* 2 ms old */
    tick_extra(0u);
    read_counters();
    CHECK(c[BR_UIT_NOTES] == 2u && c[BR_UIT_NOTE_OVER6] == 1u && c[BR_UIT_NOTE_WORST] == 8u * 24000u &&
          c[BR_UIT_NOTE_TOTAL] == 10u * 24000u, "notes %u over6 %u worst %u total %u", c[BR_UIT_NOTES],
          c[BR_UIT_NOTE_OVER6], c[BR_UIT_NOTE_WORST], c[BR_UIT_NOTE_TOTAL]);

    /* Pages through the real input path; input-to-screen timed. */
    reset_counters();
#if BR_PRODUCT_SMK37
    CHECK(button(BTN_ENV, 0u) == 1u, "ENV refused");          /* (SMK-37: LFO cycles the group's screens) */
    settle();
    CHECK(where[3] == BR_PAGE_ENV && where[9] == 0u && where[10] == 1u, "ENV: page %u state %u status %u", where[3], where[9], where[10]);
#else
    CHECK(button(BTN_LFO, 0u) == 1u, "LFO refused");
    settle();
    CHECK(where[3] == BR_PAGE_LFO && where[9] == 0u && where[10] == 1u, "LFO: page %u state %u status %u", where[3], where[9], where[10]);
    CHECK(!strncmp((const char *)where + 14, "LFO", 3), "LFO title %.21s", (const char *)where + 14);
#endif
    CHECK(button(BTN_HOME, 0u) == 1u, "HOME refused");
    settle();
    CHECK(where[3] == BR_PAGE_HOME, "HOME: page %u", where[3]);
    CHECK(button(BTN_SEL, 0u) == 1u, "SEL refused");
    settle();
    CHECK(where[3] == BR_PAGE_SCALE, "SEL: page %u (SOUNDS is %u)", where[3], (unsigned)BR_PAGE_SCALE);
    read_counters();
    CHECK(c[BR_UIT_INPUTS] >= 3u && c[BR_UIT_INPUT_PASSES] >= 6u && c[BR_UIT_INPUT_WORST] >= 24000u &&
          c[BR_UIT_INPUT_WORST] < 100u * 24000u && c[BR_UIT_INPUT_TOTAL] >= c[BR_UIT_INPUT_WORST],
          "input: samples %u passes %u worst %u total %u", c[BR_UIT_INPUTS], c[BR_UIT_INPUT_PASSES],
          c[BR_UIT_INPUT_WORST], c[BR_UIT_INPUT_TOTAL]);
    CHECK(c[BR_UIT_BYTES] > 3u * 7200u && c[BR_UIT_DRAW_SENDING] > 3u && c[BR_UIT_FRAMES_SENDING] >= 3u,
          "bytes %u sending passes %u frames %u", c[BR_UIT_BYTES], c[BR_UIT_DRAW_SENDING], c[BR_UIT_FRAMES_SENDING]);
#if BR_PRODUCT_SMK37
    /* SMK-37: HOME (printed KNOB BANK) on the SOUNDS list is "next category", not "go HOME": leave SOUNDS
     * by another page button first (the test sits on page 6, SOUNDS, after the SEL check above), then HOME. */
    CHECK(button(BTN_ENV, 0u) == 1u, "ENV refused");
    settle();
    CHECK(where[3] == BR_PAGE_ENV, "ENV (leaving SOUNDS): page %u", where[3]);
#endif
    CHECK(button(BTN_HOME, 0u) == 1u, "HOME refused");
    settle();
    CHECK(where[3] == BR_PAGE_HOME, "HOME (back from SOUNDS): page %u", where[3]);

    /* A busy injector; a long press of the HOLD button (HOME on the FM-1, the printed PATCH = SEL on the
     * SMK-37) toggles HOLD without navigating. */
    {
        uint32_t hold = br_hold_on;
        CHECK(button(BR_HOLD_BUTTON, 1000u) == 1u, "HOLD button hold refused");
        CHECK(button(BTN_LFO, 0u) == 3u, "busy not reported");
        run_ms(1400u); read_where();
        CHECK(br_hold_on != hold && where[3] == BR_PAGE_HOME && where[9] == 0u,
              "long HOME: hold %u -> %u page %u", hold, br_hold_on, where[3]);
        CHECK(button(BR_HOLD_BUTTON, 1000u) == 1u, "HOLD button hold refused");
        run_ms(1400u);
        CHECK(br_hold_on == hold, "HOLD not restored");
    }

    /* The SAVE page: open it remotely; only HOME and encoder turns get through. */
    memcpy(flash_before, host_project_flash, sizeof flash_before);
    CHECK(button(BTN_SAVE, 300u) == 5u, "SAVE with a hold not refused");
    es.rec_pending = 1;
    CHECK(button(BTN_SAVE, 0u) == 4u, "SAVE with REC pending not refused");
    es.rec_pending = 0;
    CHECK(button(BTN_SAVE, 0u) == 1u, "SAVE (open) refused");
    settle();
    CHECK(where[6] == 1u && es.mode == 1, "SAVE page not open: %u", where[6]);
    for (uint32_t b = 0u; b < 14u; ++b)
        if (b != BTN_HOME) CHECK(button(b, 0u) == 2u, "button %u allowed on the SAVE page", b);
    CHECK(es.action == 0, "action %d", es.action);
    CHECK(turn(ENC_KNOB1, 2) == 1u, "knob turn refused on the SAVE page");
    settle();
    CHECK(es.mode == 1 && es.action == 2, "knob1 on SAVE: mode %d action %d", es.mode, es.action);
    CHECK(turn(ENC_KNOB1, -2) == 1u, "knob turn refused");
    settle();
    CHECK(es.mode == 1 && es.action == 0, "knob1 back: action %d", es.action);
    CHECK(button(BTN_HOME, 0u) == 1u, "HOME refused on the SAVE page");
    settle();
    CHECK(where[6] == 0u && where[3] == BR_PAGE_HOME, "HOME did not close SAVE: save %u page %u", where[6], where[3]);

    /* Race: a SAVE of ours accepted while the page was closed, the page opening before the pass takes it.
     * Unguarded, mode 1 action 0 would quick-save (es_confirm). Stripped: nothing happens. */
    CHECK(button(BTN_SAVE, 0u) == 1u, "SAVE refused");
    es_open();
    settle();
    CHECK(es.mode == 1 && where[10] == 8u, "race on SAVE page: mode %d status %u", es.mode, where[10]);
    es_close();
    settle();
    /* Race: REC pending appears after a SAVE of ours was accepted (REC + SAVE is a quick save). */
    CHECK(button(BTN_SAVE, 0u) == 1u, "SAVE refused");
    es.rec_pending = 1;
    settle();
    CHECK(es.mode == 0, "quick save path reached: mode %d", es.mode);
    es.rec_pending = 0;
    CHECK(!memcmp(flash_before, host_project_flash, sizeof flash_before), "the project store changed");

    /* Chords: OCT- held, SEQ tapped while it is held (OCT- + SEQ); a knob turned while OCT- is held; a third
     * press, the same button again, and REC + SAVE either way round refused; a note key held and released. */
    {
        uint32_t t0;
        CHECK(button(BTN_OCT_DOWN, 600u) == 1u, "OCT- hold refused");
        CHECK(button(BTN_SEQ, 0u) == 3u, "a second button before the first registers not refused");
        run_ms(60u);
        CHECK(button(BTN_OCT_DOWN, 0u) == 3u, "the same button twice not refused");
        CHECK(button(BTN_SEQ, 0u) == 1u, "SEQ while OCT- is held refused");
        CHECK((fm1_in.buttons & (1u << BTN_OCT_DOWN)) && (fm1_in.buttons & (1u << BTN_SEQ)), "chord not held: %x", fm1_in.buttons);
        CHECK(button(BTN_LFO, 0u) == 3u, "a third button not refused");
        CHECK(turn(ENC_KNOB1, 1) == 1u, "a turn while OCT- is held refused");
        run_ms(60u);
        CHECK((fm1_in.buttons & (1u << BTN_OCT_DOWN)) && !(fm1_in.buttons & (1u << BTN_SEQ)), "OCT- not still held: %x", fm1_in.buttons);
        settle(); settle();
        read_where();
        CHECK(where[9] == 0u, "injector not idle after the chord: %x", where[9]);
        CHECK(button(BTN_REC, 400u) == 1u, "REC hold refused");
        run_ms(60u);
        CHECK(button(BTN_SAVE, 0u) == 4u, "SAVE while our REC is held not refused");
        settle(); settle();
        CHECK(button(BTN_SAVE, 0u) == 1u, "SAVE refused");
        CHECK(button(BTN_REC, 0u) == 3u || button(BTN_REC, 0u) == 4u, "REC while our SAVE is in flight not refused");
        settle();
        CHECK(es.mode == 1, "SAVE page not open: %d", es.mode);
        CHECK(button(BTN_REC, 0u) == 2u && inject(3u, 5u, 0) == 2u, "REC or a key on the SAVE page not refused");
        CHECK(button(BTN_HOME, 0u) == 1u, "HOME refused"); settle(); settle();
        CHECK(es.mode == 0, "SAVE page still open");
        t0 = fm1_in.notes;
        CHECK(inject(3u, 5u, 200u) == 1u, "note key refused");
        run_ms(60u);
        CHECK(fm1_in.notes & (1u << 5u), "note key not down: %x (was %x)", fm1_in.notes, t0);
        run_ms(400u);
        CHECK(!(fm1_in.notes & (1u << 5u)), "note key not released");
        CHECK(inject(3u, 27u, 0) == 6u, "key 27 not refused");
    }

    /* Bad requests. */
    CHECK(button(14u, 0u) == 6u && button(BTN_LFO, 5001u) == 6u && turn(7u, 1) == 6u && turn(ENC_KNOB1, 0) == 6u &&
          turn(ENC_KNOB1, 33) == 6u && inject(4u, 0u, 0) == 6u, "bad requests not refused");
    {
        const uint8_t bad[3] = {'U', 'T', 2u};
        assert(!fm1_app_command(0xB6u, bad, 3u, reply, &n));
        assert(!fm1_app_command(0xB5u, bad, 3u, reply, &n));
        assert(!fm1_app_command(0xB7u, bad, 3u, reply, &n));
        assert(!fm1_app_command(0xB8u, bad, 1u, reply, &n));
        assert(!fm1_app_command(0xB9u, 0, 0u, reply, &n));
    }
    if (fails) { fprintf(stderr, "%u checks failed\n", fails); return 1; }
    puts("PASS: UI timing diagnostic: counters, remote pages, SAVE-page guard (BR_UI_TIMING_DIAG=1)");
#else
    {
        const uint8_t reset[3] = {'U', 'T', 1u}, tap[7] = {'U', 'T', 1u, 1u, BTN_HOME, 0u, 0u};
        assert(!fm1_app_command(0xB5u, 0, 0u, reply, &n));
        assert(!fm1_app_command(0xB6u, reset, 3u, reply, &n));
        assert(!fm1_app_command(0xB7u, tap, 7u, reply, &n));
        assert(!fm1_app_command(0xB8u, 0, 0u, reply, &n));
    }
    (void)run_ms; (void)fails;
    puts("PASS: UI timing diagnostic absent (BR_UI_TIMING_DIAG=0)");
#endif
    return 0;
}
