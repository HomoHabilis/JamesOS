/* Draw load: how many pixel bytes the main loop sends, per pass and per frame, and that a frame sent in
 * slices (br_draw_pass_with) leaves the same screen as one full redraw. The real app loop runs once per host
 * millisecond with audio as the ISR would render it; input goes in as tests/host.c injects it. */
#define DINGHY_HOST 1
#define UI_REFRESH_TEST_COUNTERS 1
#define UI_HOME_DOT_WATCH 1
/* The pass time bound (BR_PASS_TICKS) in play: every question a region asks costs 0.7 ms of a made-up clock,
 * so a pass ends after a few regions and frames are cut at many more places than bytes alone would cut them. */
static unsigned draw_load_clock;
#define BR_DRAW_NOW() ((uint32_t)(draw_load_clock += 16800u))
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static uint32_t owed, tap, fails;
static uint64_t pass_worst, tot, frame_worst, frame_now;
static uint16_t shot[240 * 240];

#define CHECK(c, ...) do { if (!(c)) { ++fails; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void pass(void)                          /* one loop pass, without time passing */
{
    ui_test_lcd_pixel_bytes = 0;
    br_app_tick();
    uint64_t b = ui_test_lcd_pixel_bytes;
    tot += b;
    if (b > pass_worst) pass_worst = b;
    frame_now = b ? frame_now + b : 0;
    if (frame_now > frame_worst) frame_worst = frame_now;
}
static void tick(void)                          /* one host millisecond: audio, then one pass */
{
    static int32_t blk[2 * 256];
    fm1_ms++;
    if (tap) { fm1_in.buttons &= ~tap; host_released |= tap; tap = 0; }
    owed += 48000u;
    while (owed >= 1000u * 256u) { owed -= 1000u * 256u; memset(blk, 0, sizeof blk); app_audio(blk, 256); }
    pass();
}
static void run_ms(uint32_t n) { while (n--) tick(); }
static void btn(uint32_t b) { fm1_in.buttons |= 1u << b; host_pressed |= 1u << b; tick(); tap = 1u << b; tick(); }

/* Finish the frame now (time stands still), then compare with a full, unsliced redraw at the same moment. */
static uint32_t settle_and_compare(const char *what)
{
    uint32_t n = 0;
    while ((br_draw_more || br_ui.redraw) && n < 400u) { pass(); ++n; }
    br_ui.redraw = 1u;                          /* one more frame begun now: moving pictures at this moment */
    while ((br_draw_more || br_ui.redraw) && n < 800u) { pass(); ++n; }
    CHECK(n < 800u, "%s: the frame never finished", what);
    memcpy(shot, fb, sizeof shot);
    br_draw_layout_old = 0xffffffffu; br_ui.redraw = 1u; br_draw();
    {
        uint32_t x0 = 240, y0 = 240, x1 = 0, y1 = 0;
        for (uint32_t i = 0; i < 240u * 240u; ++i)
            if (shot[i] != fb[i]) {
                uint32_t x = i % 240u, y = i / 240u;
                if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
        if (x0 < 240u) {
            CHECK(0, "%s (page %u screen %u track %u): the sliced screen differs from a full redraw in x %u..%u, y %u..%u",
                  what, br_ui.page, br_ui.screen[br_ui.page], br_ui.part, x0, x1, y0, y1);
            if (getenv("DRAW_LOAD_DUMP")) {
                FILE *f = fopen(getenv("DRAW_LOAD_DUMP"), "wb");
                fprintf(f, "P6\n480 240\n255\n");
                for (uint32_t y = 0; y < 240u; ++y) for (uint32_t x = 0; x < 480u; ++x) {
                    uint16_t c = x < 240u ? shot[y * 240u + x] : fb[y * 240u + x - 240u];
                    uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
            return 0;
        }
    }
    return 1;
}

static uint32_t rng = 12345u;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

static void fuzz(uint32_t steps)
{
    static const uint8_t buttons[] = {0, 1, 2, 2, 3, 4, 4, 5, 5, 6, 7, 8, 8, 10, 11, 11, 12};   /* not SAVE */
    char what[64];
    for (uint32_t i = 0; i < steps; ++i) {
        uint32_t a = rnd(10);
        if (a < 4) { host_encoder[2u + rnd(4)] += (int32_t)rnd(5) - 2; snprintf(what, sizeof what, "step %u knob", i); }
        else if (a < 6) { btn(buttons[rnd(sizeof buttons)]); snprintf(what, sizeof what, "step %u button", i); }
        else if (a < 7) { uint32_t k = rnd(27); if (fm1_in.notes >> k & 1u) fm1_in.notes &= ~(1u << k);
                          else { host_note_pressed |= 1u << k; fm1_in.notes |= 1u << k; }
                          snprintf(what, sizeof what, "step %u key", i); }
        else if (a < 8) { host_encoder[1] += rnd(2) ? 1 : -1; snprintf(what, sizeof what, "step %u track", i); }
        else { run_ms(1u + rnd(300)); snprintf(what, sizeof what, "step %u time", i); }
        tick();
        if (!settle_and_compare(what)) return;
    }
}

/* Bytes per frame over `ms` of the current scene: average (per BR_FRAME_MS) and the worst frame. */
static void measure(uint32_t ms, uint64_t *avg, uint64_t *worst)
{
    tot = frame_worst = frame_now = 0;
    run_ms(ms);
    *avg = tot * BR_FRAME_MS / ms; *worst = frame_worst;
}
static void scene(uint32_t page, uint32_t screen, uint32_t part)
{
    fm1_in.notes = 0; if (br_playing) btn(12);
    br_ui.part = part; br_ui.page = page; br_ui.screen[page] = screen; br_ui.redraw = 1;
    run_ms(800);
}

int main(void)
{
    uint64_t avg, worst;
    host_init_resources();
    br_app_init(); fm1_ms = 5000; br_intro_until = 0; br_ui.redraw = 1;
    run_ms(500);
    pass_worst = 0;

    /* A HOME press changes the selected dot, but neither dot may disappear
     * during any LCD write, including when the layouts are different. */
    scene(BR_PAGE_HOME, 0u, 0u);
    for (uint32_t k=0; k<8u; ++k) {
        host_dot_missing=0u; host_dot_presence_watch=1u;
        btn(BTN_HOME); run_ms(350u);
        host_dot_presence_watch=0u;
        CHECK(br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] == ((k + 1u) & 1u),
              "HOME press %u changes page", k);
        CHECK(!host_dot_missing, "HOME transition %u dots disappeared on %u LCD writes", k, host_dot_missing);
    }

    host_dot_missing=0u; host_dot_presence_watch=1u;
    for (uint32_t k=0; k<8u; ++k) btn(BTN_HOME);
    run_ms(350u); host_dot_presence_watch=0u;
    CHECK(!host_dot_missing, "rapid HOME reversals lose dots on %u LCD writes", host_dot_missing);

    /* Watch the actual sliced drawing path, not just the final settled frame.
     * Control changes and playback must never erase a HOME carousel dot between passes. */
    for (uint32_t home = 0; home < 3u; ++home) {
        scene(BR_PAGE_HOME, home, home);
        memcpy(host_dot_expected, fb + 200u * 240u, sizeof host_dot_expected);
        host_dot_changes = 0u; host_dot_watch = 1u;
        btn(12);
        /* Force the same full-content refresh used after changes such as monitor toggles. */
        br_pass.epoch += 0x9e3779b9u; br_ui.redraw = 1u;
        for (uint32_t ms = 0; ms < 600u; ++ms) {
            if (ms % 100u == 0u) host_encoder[2u + (ms / 100u) % 4u]++;
            tick();
        }
        host_dot_watch = 0u;
        CHECK(!host_dot_changes, "HOME page %u dot strip changed after %u LCD writes during redraw", home, host_dot_changes);
    }
    for (uint32_t lfo = 0; lfo < 3u; ++lfo) {
        scene(BR_PAGE_LFO, lfo, 0u);
        memcpy(host_dot_expected, fb + 200u * 240u, sizeof host_dot_expected);
        host_dot_changes = 0u; host_dot_watch = 1u;
        for (uint32_t ms = 0; ms < 600u; ++ms) {
            if (ms % 60u == 0u) host_encoder[ENC_KNOB2] += ms < 300u ? 1 : -1;
            tick();
        }
        br_pass.epoch += 0x9e3779b9u; br_ui.redraw = 1u;
        for (uint32_t ms = 0; ms < 250u; ++ms) tick();
        host_dot_watch = 0u;
        CHECK(!host_dot_changes, "LFO page %u dot strip changed after %u LCD writes while changing rate", lfo, host_dot_changes);
    }

    /* Budgets (bytes; BR_PASS_BYTES = 7,200 per pass = 4.8 ms at 12 MHz). */
    scene(BR_PAGE_HOME, 0, 0); btn(12); run_ms(300); measure(3000, &avg, &worst);
    printf("HOME sound, playing: %llu B/frame, worst frame %llu\n", (unsigned long long)avg, (unsigned long long)worst);
    CHECK(avg <= 14000u && worst <= 20000u, "HOME sound, playing: %llu B/frame (budget 14,000), worst %llu (20,000)",
          (unsigned long long)avg, (unsigned long long)worst);
    scene(BR_PAGE_HOME, 0, 0); host_note_pressed |= 1u << 12; fm1_in.notes |= 1u << 12; run_ms(300); measure(2000, &avg, &worst);
    printf("HOME sound, note held: %llu B/frame, worst frame %llu\n", (unsigned long long)avg, (unsigned long long)worst);
    CHECK(avg <= 8000u && worst <= 20000u, "HOME sound, note held: %llu B/frame (budget 8,000), worst %llu (20,000)",
          (unsigned long long)avg, (unsigned long long)worst);
    scene(BR_PAGE_ENV, 0, 0); btn(12); run_ms(300); measure(3000, &avg, &worst);
    printf("ENV amp, playing: %llu B/frame, worst frame %llu\n", (unsigned long long)avg, (unsigned long long)worst);
    CHECK(avg <= 2000u, "ENV amp, playing (header and step strip only): %llu B/frame (budget 2,000)", (unsigned long long)avg);
    scene(BR_PAGE_LFO, 0, 0); measure(2000, &avg, &worst);
    printf("LFO 1, idle: %llu B/frame, worst frame %llu\n", (unsigned long long)avg, (unsigned long long)worst);
    CHECK(avg <= 16080u && worst <= 16080u, "LFO 1, idle: %llu B/frame, worst %llu (budget 16,080, the 1.0a cost)",
          (unsigned long long)avg, (unsigned long long)worst);
    scene(BR_PAGE_LFO, 0, 0); btn(12); run_ms(300); measure(3000, &avg, &worst);
    printf("LFO 1, playing: %llu B/frame, worst frame %llu\n", (unsigned long long)avg, (unsigned long long)worst);
    CHECK(avg <= 16080u, "LFO 1, playing: %llu B/frame (budget 16,080)", (unsigned long long)avg);
    scene(BR_PAGE_HOME, 0, 0); btn(3); run_ms(800);              /* SEL: the SOUNDS list */
    measure(2000, &avg, &worst);
    CHECK(tot == 0u, "SOUNDS list, idle: %llu B sent (budget 0)", (unsigned long long)tot);
    tot = 0; for (uint32_t i = 0; i < 6u; ++i) { host_encoder[2] += i < 3u ? 1 : -1; run_ms(300); }
    printf("SOUNDS list: %llu B per detent\n", (unsigned long long)(tot / 6u));
    CHECK(tot / 6u <= 45000u, "SOUNDS list: %llu B per detent (budget 45,000)", (unsigned long long)(tot / 6u));
    btn(8); run_ms(300);
    scene(BR_PAGE_HOME, 0, 0);
    printf("worst pass so far: %llu B\n", (unsigned long long)pass_worst);
    CHECK(pass_worst <= BR_PASS_BYTES, "a pass sent %llu B (budget %u)", (unsigned long long)pass_worst, BR_PASS_BYTES);

#ifdef DINGHY_DEVICE_SAVE_TEST
    {   /* The SAVE screens (device build): nothing sent while idle, a detent costs its rows, sliced = full. */
        static const char *const what[4] = {"SAVE menu", "SAVE slot list", "SAVE name character", "SAVE name cursor"};
        static const uint8_t enc[4] = {2, 2, 2, 3};
        static const uint32_t budget[4] = {30000u, 60000u, 12000u, 24000u};
        btn(9); run_ms(1500);
        tot = 0; run_ms(2000);
        printf("SAVE menu, idle: %llu B in 2 s\n", (unsigned long long)tot);
        CHECK(tot == 0u, "SAVE menu, idle: %llu B sent (budget 0)", (unsigned long long)tot);
        btn(12); run_ms(300); tot = 0; run_ms(2000);   /* playing: only the metronome's arm and the playhead move */
        printf("SAVE menu, playing: %llu B in 2 s\n", (unsigned long long)tot);
        CHECK(tot <= 40000u, "SAVE menu, playing: %llu B in 2 s (budget 40,000)", (unsigned long long)tot);
        {   /* (the SAVE screens look for changes every 250 ms: one frame now, then compare) */
            uint32_t n = 0;
            br_ui.redraw = 1u;
            while ((br_draw_more || br_ui.redraw) && n++ < 400u) pass();
            memcpy(shot, fb, sizeof shot);
            br_pass.epoch += 0x9e3779b9u; es_draw();
            for (uint32_t i = 0; i < 240u * 240u; ++i)
                if (shot[i] != fb[i]) { CHECK(0, "SAVE menu, playing: the sliced screen differs from a full redraw at (%u, %u)", i % 240u, i / 240u); break; }
        }
        btn(12); run_ms(300);
        for (uint32_t m = 0; m < 4u; ++m) {
            if (m == 1u) { host_encoder[2] += 2; run_ms(300); btn(9); run_ms(600); }            /* LOAD: the slot list */
            if (m == 2u) { btn(0); run_ms(300); host_encoder[2] -= 1; run_ms(300); btn(9); run_ms(300); btn(9); run_ms(600); }
            tot = 0;
            for (uint32_t i = 0; i < 6u; ++i) { host_encoder[enc[m]] += m == 0u ? (i & 1u ? -1 : 1) : m == 1u ? (i < 3u ? 1 : -1) : 1; run_ms(300); }
            printf("%s: %llu B per detent (mode %d)\n", what[m], (unsigned long long)(tot / 6u), es.mode);
            CHECK(tot / 6u <= budget[m], "%s: %llu B per detent (budget %u)", what[m], (unsigned long long)(tot / 6u), budget[m]);
            {   /* the screen equals a full, unsliced redraw */
                uint32_t n = 0;
                while ((br_draw_more || br_ui.redraw) && n++ < 400u) pass();
                memcpy(shot, fb, sizeof shot);
                br_pass.epoch += 0x9e3779b9u; es_draw();
                CHECK(!memcmp(shot, fb, sizeof shot), "%s: the sliced screen differs from a full redraw", what[m]);
            }
        }
        btn(8);                                 /* HOME closes the SAVE screens */
        run_ms(500);
    }
#endif
    /* Moving pictures (the HOME cards' marks, the metronome) drawn a phase step at a time equal full redraws,
     * through hundreds of phases, at every HOME screen and on several tracks and knob settings; the step
     * strip's playhead on HOME, MIXER, SEQ and LFO; knob steps between, and the mixer's and drum mixer's
     * fader caps. */
    for (uint32_t round = 0; round < 13u; ++round) {
        char what[64];
        if (round < 8u) scene(BR_PAGE_HOME, round & 1u, round < 6u ? round % 3u : BR_DRUM_PART);
        else if (round < 11u) scene(round == 8u ? BR_PAGE_MIX : round == 9u ? BR_PAGE_SEQ : BR_PAGE_LFO, 0, round - 8u);
        else if (round == 11u) scene(BR_PAGE_MIX, 0, 1u);                  /* the mixer, its faders turned */
        else scene(BR_PAGE_ROUTE, 3u, BR_DRUM_PART);                        /* the drum mixer */
        if (round >= 2u) for (uint32_t knob = 0; knob < 4u; ++knob) host_encoder[2u + knob] += (int32_t)rnd(41) - 20;
        if (round < 11u) btn(12);                 /* play (the mixers' rounds stay silent: their meters read the
                                                     * audio's live peak, which a later full redraw reads afresh) */
        for (uint32_t i = 0; i < 90u; ++i) {
            run_ms(1u + rnd(140));
            if (round < 11u && rnd(4) == 0u) { uint32_t k = rnd(27); host_note_pressed |= 1u << k; fm1_in.notes ^= 1u << k; }
            if (round >= 11u || rnd(5) == 0u) host_encoder[2u + rnd(4)] += (int32_t)rnd(9) - 4;   /* knob steps mid-motion */
            snprintf(what, sizeof what, "animation round %u step %u", round, i);
            if (!settle_and_compare(what)) break;
        }
        fm1_in.notes = 0u;
    }
    /* Sliced frames equal full redraws, after every kind of input. */
    fuzz(400);
    rng = 777u; br_ui.part = BR_DRUM_PART; br_ui.redraw = 1; run_ms(200); fuzz(300);
    CHECK(pass_worst <= BR_PASS_BYTES, "a pass sent %llu B (budget %u)", (unsigned long long)pass_worst, BR_PASS_BYTES);
    if (fails) { printf("draw load: %u checks failed\n", fails); return 1; }
    printf("draw load: all checks passed\n");
    return 0;
}
