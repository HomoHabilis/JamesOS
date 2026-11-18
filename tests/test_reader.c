/* Screen reader adapter (firmware/src/instrument_reader.h, protocol v1, docs/SCREEN-READER.md):
 * silence until a HELLO, the HELLO answer, what screens say (exact bytes), the rate limit, a full
 * USB queue, WHERE, BYE and the five-second timeout. Every frame sent is checked as SysEx and
 * decoded by the small decoder below, written from the protocol description. Host only. */
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1               /* the SAVE menu (es), for its slot list */
#define FM1_PATTERN_TARGET_EXPERIMENT 1
#define FM1_PATTERN_PROJECT_TARGET_EXPERIMENT 1
#define main host_unused_main
volatile unsigned fm1_project_old_format_seen;   /* pattern_project.c is not linked here */
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !BR_CAP_READER
int main(void) { puts("reader: BR_CAP_READER is 0, nothing to test"); return 0; }
#else
/* the project store the SAVE menu talks to (as tests/test_save_ux_1953.c) */
int br_pp_target_scan(unsigned s, char name[13]) { if (s != 1u) return PFS_EMPTY; strcpy(name, "MY SONG"); return PFS_OK; }
int br_pp_target_save(unsigned s, const char *name) { (void)s; (void)name; return PFS_OK; }
int br_pp_target_load(unsigned s) { (void)s; return PFS_OK; }
int br_pp_target_ready(void) { return 1; }
int br_pp_target_recover(int *slot) { (void)slot; return FM1_PROJECT_SAFE_EMPTY; }
int br_pp_target_init_project(void) { return PFS_OK; }

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

/* ---- a decoder written from the protocol: F0 7D 53 52 cmd args F7 ---- */
typedef struct { uint8_t flags; char text[256]; uint32_t len, off; } said_t;
static said_t said[512];                        /* SAYs */
static uint32_t said_n, read_at, hellos;
static char hello_text[64];

static uint32_t decode(void)                    /* new SAYs since the last call; checks every frame */
{
    uint32_t start = said_n;
    while (read_at < rd_host_len) {
        const uint8_t *f = rd_host_out + read_at;
        uint32_t n = 0, i;
        while (read_at + n < rd_host_len && f[n] != 0xF7) ++n;
        CHECK(read_at + n < rd_host_len);                       /* ends with F7 */
        ++n;
        CHECK(n >= 6 && f[0] == 0xF0 && f[1] == 0x7D && f[2] == 0x53 && f[3] == 0x52);
        for (i = 1; i + 1 < n; ++i) CHECK(f[i] < 0x80);         /* 7-bit between F0 and F7 */
        CHECK(n <= 6u + 240u + 1u);                             /* the protocol's limit */
        CHECK(n <= 6u + RD_TEXT_MAX + 2u);                      /* this adapter's (HELLO: + its 0) */
        CHECK(f[4] == RD_HELLO || f[4] == RD_SAY);
        if (f[4] == RD_HELLO) {
            ++hellos;
            for (i = 6; i + 1 < n && i - 6 < sizeof hello_text - 1; ++i) hello_text[i - 6] = f[i] ? (char)f[i] : '|';
            hello_text[i - 6] = 0;
        } else {
            said_t *s = &said[said_n < 511u ? said_n++ : said_n];
            s->flags = f[5]; s->len = n; s->off = read_at;
            CHECK(s->flags <= 3u);
            for (i = 6; i + 1 < n && i - 6 < sizeof s->text - 1; ++i) { CHECK(f[i] >= 0x20); s->text[i - 6] = (char)f[i]; }
            s->text[i - 6] = 0;
        }
        read_at += n;
    }
    return said_n - start;
}

/* ---- a simulated panel and a reader that keeps listening (HELLO every 2 s) ---- */
static uint32_t listening, last_hello;
static void frame(uint8_t cmd)
{
    const uint8_t p[5] = {0x7D, 0x53, 0x52, cmd, 1};
    CHECK(br_reader_frame(p, cmd == RD_HELLO ? 5u : 4u));
    if (cmd == RD_HELLO) last_hello = fm1_ms;
}
static void tick(void)
{
    ++fm1_ms;
    if (listening && fm1_ms - last_hello >= 2000u) frame(RD_HELLO);
    br_app_tick();
}
static void run(uint32_t ms) { while (ms--) tick(); }
static void tap(unsigned k)
{
    fm1_in.buttons |= 1u << k; host_pressed |= 1u << k; tick();
    fm1_in.buttons &= ~(1u << k); host_released |= 1u << k; tick();
}
static void turn(unsigned enc, int n) { host_encoder[enc] += n; tick(); }

/* exactly one SAY next, after the debounce has run out: its bytes F0 7D 53 52 10 flags text F7 */
static void expect_say(const char *what, uint32_t flags, const char *text)
{
    uint32_t n, len = (uint32_t)strlen(text);
    run(600);
    n = decode();
    printf("%-15s %u \"%s\"\n", what, n ? said[said_n - 1].flags : 9u, n ? said[said_n - 1].text : "");
    CHECK(n == 1u);
    if (n == 1u) {
        const uint8_t *f = rd_host_out + said[said_n - 1].off;
        CHECK(said[said_n - 1].len == len + 7u && f[0] == 0xF0 && f[1] == 0x7D && f[2] == 0x53 &&
              f[3] == 0x52 && f[4] == 0x10 && f[5] == flags && f[6 + len] == 0xF7);
        if (said[said_n - 1].len != len + 7u || memcmp(f + 6, text, len)) {
            printf("  expected \"%s\"\n", text); ++failures;
        }
    }
}

int main(void)
{
    char text[64];
    uint32_t i, n;
    br_app_init(); fm1_ms = 5000u; es_project_boot_state = 1u;
    run(3000);                                  /* past the intro */

    /* 1. silent until asked: pages, knobs and messages send nothing; other SysEx is not ours */
    tap(BTN_ENV); turn(ENC_KNOB1, 5); br_message("HOLD ON"); run(1000); tap(BTN_HOME); run(6000);
    CHECK(rd_host_len == 0u);
    { const uint8_t other[4] = {0x00, 0x59, 0x11, 0x00}; CHECK(!br_reader_frame(other, 4)); }
    { const uint8_t other[4] = {0x7D, 0x53, 0x53, 0x01}; CHECK(!br_reader_frame(other, 4)); }
    CHECK(rd_host_len == 0u);

    /* 2. HELLO: the answer (version 1, name, 0, no words link), then the whole screen at once */
    frame(RD_HELLO); listening = 1u;
    decode();
    snprintf(text, sizeof text, "%s HOST|", APP_NAME);
    printf("HELLO answer    \"%s\"\n", hello_text);
    CHECK(hellos == 1u && rd_host_out[4] == RD_HELLO && rd_host_out[5] == 1u && !strcmp(hello_text, text));
    CHECK(rd_host_out[rd_host_len - 2u] == 0u && rd_host_out[rd_host_len - 1u] == 0xF7);   /* empty link */
    expect_say("home", RD_NOW, "ANALOG VCF. TIMBRE 45%, HARMONICS 50%, MORPH 6%, OSC MIX MAIN");

    /* 3. walking the screens (exact bytes) */
    tap(BTN_ENV);
    expect_say("env page", RD_NOW, "ENV / AMP. ATTACK 1.04MS, DECAY 191MS, SUSTAIN 87%, RELEASE 37.5MS");
    turn(ENC_KNOB4, 2);                         /* one knob: label and value only */
    br_control_t c = br_control(3); char value[32]; br_control_text(3, &c, value);
    snprintf(text, sizeof text, "RELEASE %s", value);
    expect_say("knob", RD_NOW, text);
    tap(BTN_SEL);
    expect_say("sounds list", RD_NOW, "SOUNDS. 001 LADDER SEQ, 1 OF 128");
    turn(ENC_KNOB1, 1);
    expect_say("list row", RD_NOW, "002 PURE SUB, 2 OF 128");
#if BR_PRODUCT_SMK37
    br_smk_fader_bank_select(0u);               /* SMK-37: GLO no longer reaches the mixer; FADER BANK does */
#else
    tap(BTN_GLO);
#endif
    expect_say("mixer", RD_NOW, "MIXER. TRACK 1 0.0 dB. TRACK 2 0.0 dB, TRACK 3 0.0 dB, DRUM KIT 0.0 dB");
    turn(ENC_KNOB1, -10);                       /* the selected strip's level: said by the knob, once */
    expect_say("mixer strip", RD_NOW, "TRACK 1 -0.7 dB");
    tap(BTN_SEQ);
    expect_say("sequencer", RD_NOW, "SEQUENCER. STEP 1. NOTE C4, LENGTH 1 STEP, VELOCITY 100, CONDITION ALWAYS. "
               "OCT-/+ + REC SHIFT NOTES");
    fm1_in.notes |= 1u << 4; host_note_pressed |= 1u << 4; tick();   /* hold a step and turn velocity */
    turn(ENC_KNOB3, 3);
    expect_say("step edit", RD_NOW, "SEQUENCER. STEP 3. NOTE C4, LENGTH 1 STEP, VELOCITY 103, CONDITION ALWAYS. "
               "OCT-/+ + REC SHIFT NOTES");
    turn(ENC_KNOB3, 1);
    expect_say("step velocity", RD_NOW, "VELOCITY 104");
    fm1_in.notes &= ~(1u << 4); tick(); run(600); decode();

    /* a track of more than one bank of 16 steps: the paging is spoken too (the drawn cue alternates) */
    br_part[br_ui.part].length = 32u;
    tap(BTN_ENV); run(600); decode(); tap(BTN_SEQ);
    expect_say("sequencer, 2 banks", RD_NOW, "SEQUENCER. STEP 1. NOTE C4, LENGTH 1 STEP, VELOCITY 100, CONDITION ALWAYS. "
               "OCT-/+: STEP BANK. OCT-/+ + REC SHIFT NOTES");
    br_part[br_ui.part].length = 16u;
#if BR_PRODUCT_SMK37
    {   /* the drum track's ARP page is the "SWITCH TO" cue: no knobs, no hint */
        uint32_t was = br_ui.part;
        tap(BTN_ENV); run(600); decode();
        br_ui.part = BR_DRUM_PART; br_ui.redraw = 1u; tap(BTN_ARP);
        expect_say("arp cue (drum)", RD_NOW, "ARP. SWITCH TO T1 T2 T3");
        br_ui.part = was; tap(BTN_ENV); run(600); decode(); tap(BTN_SEQ); run(600); decode();
    }
#endif

    /* 4. a knob kept turning (a detent every 100 ms for 2 s): a SAY every 500 ms, then the final value */
    tap(BTN_ENV); run(600); decode();
    n = said_n;
    for (i = 0; i < 2000u; ++i) { if (!(i % 100u)) host_encoder[ENC_KNOB2] -= 1; tick(); }
    run(600);
    decode();
    printf("sweep           %u SAYs in 2.6 s:", said_n - n);
    for (i = n; i < said_n; ++i) printf(" \"%s\"", said[i].text);
    printf("\n");
    CHECK(said_n - n >= 4u && said_n - n <= 5u);
    c = br_control(1); br_control_text(1, &c, value); snprintf(text, sizeof text, "DECAY %s", value);
    CHECK(said_n > n && !strcmp(said[said_n - 1].text, text));

    /* 5. a message: at once, cutting off; what follows within 1 s queues behind it */
    br_message("HOLD ON"); run(60);
    CHECK(decode() == 1u && !strcmp(said[said_n - 1].text, "HOLD ON") && said[said_n - 1].flags == RD_NOW);
    turn(ENC_KNOB2, 1); run(300);
    CHECK(decode() == 1u && said[said_n - 1].flags == 0u);

    /* 6. WHERE: everything again, at once */
    frame(RD_WHERE); run(60);
    CHECK(decode() == 1u && !strncmp(said[said_n - 1].text, "ENV / AMP. ATTACK", 17) && said[said_n - 1].flags == RD_NOW);

    /* 7. the SAVE menu and its slot list */
    run(1000); decode();
    tap(BTN_SAVE);
    expect_say("save menu", RD_NOW, "SAVE. QUICKSAVE");
    turn(ENC_KNOB1, 1);
    expect_say("save choice", RD_NOW, "SAVE AS");
    tap(BTN_SAVE);
    expect_say("slot list", RD_NOW, "CHOOSE SLOT. 01 EMPTY, 1 OF 12");
    turn(ENC_KNOB1, 1);
    expect_say("slot row", RD_NOW, "02 MY SONG, 2 OF 12");
    tap(BTN_HOME); run(600); decode();

    /* 8. a full USB queue: nothing is lost or waited for; sent when there is room */
    tap(BTN_ENV); run(600); decode();
    rd_host_full = 1u; turn(ENC_KNOB1, 1); run(600); CHECK(decode() == 0u);
    rd_host_full = 0u; run(60); CHECK(decode() == 1u);

    /* 9. BYE stops it; HELLO again restarts with the whole screen; 5 s without HELLO stops it */
    frame(RD_BYE); listening = 0u; tap(BTN_HOME); turn(ENC_KNOB1, 1); br_message("HOLD OFF"); run(1000);
    CHECK(decode() == 0u);
    frame(RD_HELLO); n = hellos; run(60); CHECK(decode() == 1u && hellos == n + 1u);
    run(5100); tap(BTN_ENV); turn(ENC_KNOB1, 1); br_message("HOLD ON"); run(1000);
    CHECK(decode() == 0u && !br_reader.on);

    if (failures) { printf("reader: %d checks failed\n", failures); return 1; }
    printf("reader: all checks passed (%u SAYs, %u HELLO answers)\n", said_n, hellos);
    return 0;
}
#endif
