/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen map: which screens each page button reaches, in which order, and what the page dots
 * count, driven through the real input loop (press and release edges, held time). Both products. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "host_intro.h"
#include <assert.h>
#include <stdio.h>

static void tick(void) { ++fm1_ms; br_app_tick(); }
static void down(uint32_t b) { fm1_in.buttons |= 1u << b; host_pressed |= 1u << b; tick(); }
static void up(uint32_t b) { fm1_in.buttons &= ~(1u << b); host_released |= 1u << b; tick(); }
static void tap(uint32_t b) { down(b); up(b); }
static void hold(uint32_t b, uint32_t ms) { down(b); for (uint32_t i = 0u; i < ms; ++i) tick(); up(b); }
/* The page dots: how many, and which one is lit (br_draw_page_dots). */
static uint32_t dots(uint32_t *lit)
{
    const uint8_t *ring = 0;
    uint32_t n = br_nav_ring(&ring);
    *lit = n ? br_nav_at(ring, n) : br_ui.screen[br_ui.page];
    return n ? n : br_screen_count();
}
static void expect(uint32_t page, uint32_t screen, const char *title, uint32_t count, uint32_t lit)
{
    uint32_t at, n = dots(&at);
    if (br_ui.page != page || br_ui.screen[page] != screen || strcmp(br_header_title(), title) || n != count || at != lit) {
        fprintf(stderr, "expected %u/%u '%s' dots %u@%u, got %u/%u '%s' dots %u@%u (part %u)\n", page, screen, title, count, lit,
                br_ui.page, br_ui.screen[br_ui.page], br_header_title(), n, at, br_ui.part);
        assert(0);
    }
}

#if !BR_CAP_DIN_MIDI
#define MIDI1 "USB MIDI 1"
#define MIDI2 "USB MIDI 2"
#else
#define MIDI1 "MIDI 1"
#define MIDI2 "MIDI 2"
#endif

int main(void)
{
#if BR_PRODUCT_SMK37
    /* SMK-37: GLO's ring is five screens starting on PAN; the mixer is not in it (FADER BANK opens it). */
    static const struct { uint32_t page, screen; const char *title; } sound[5] = {
        {BR_PAGE_MIX, 3u, "GLOBAL / PAN"},
        {BR_PAGE_MIX, 1u, "GLOBAL / FILTER"}, {BR_PAGE_MIX, 6u, "GLOBAL / HEAT"},
        {BR_PAGE_MIX, 4u, "COMPRESSOR"}, {BR_PAGE_MIX, 5u, "COMP TIME"}};
#define NSOUND 5u
#define GLO_N 5u          /* dots on a GLOBAL sound screen */
#define GLO_PAN_AT 0u     /* the lit dot on PAN, FILTER */
#define GLO_FILTER_AT 1u
#else
    static const struct { uint32_t page, screen; const char *title; } sound[6] = {
        {BR_PAGE_MIX, 0u, "MIXER"}, {BR_PAGE_MIX, 3u, "GLOBAL / PAN"},
        {BR_PAGE_MIX, 1u, "GLOBAL / FILTER"}, {BR_PAGE_MIX, 6u, "GLOBAL / HEAT"},
        {BR_PAGE_MIX, 4u, "COMPRESSOR"}, {BR_PAGE_MIX, 5u, "COMP TIME"}};
#define NSOUND 6u
#define GLO_N 6u
#define GLO_PAN_AT 1u
#define GLO_FILTER_AT 2u
#endif
    static const struct { uint32_t screen; const char *title; } setup[3] = {{2u, "TEMPO"}, {7u, MIDI1}, {8u, MIDI2}};
    br_app_init(); host_after_intro(); fm1_ms = 3000u;

    /* GLO taps: the sound screens in order (six on the FM-1, five from PAN on the SMK-37), then around again. */
    for (uint32_t i = 0u; i < 17u; ++i) {
        tap(BTN_GLO); expect(sound[i % NSOUND].page, sound[i % NSOUND].screen, sound[i % NSOUND].title, GLO_N, i % NSOUND);
    }
    /* A long GLO press is an ordinary tap on release; OCT- + GLO opens TEMPO. */
    br_ui.screen[BR_PAGE_MIX] = 3u;
    down(BTN_GLO); for (uint32_t i = 0u; i < 600u; ++i) tick();
    expect(BR_PAGE_MIX, 3u, "GLOBAL / PAN", GLO_N, GLO_PAN_AT);
    up(BTN_GLO); expect(BR_PAGE_MIX, 1u, "GLOBAL / FILTER", GLO_N, GLO_FILTER_AT);
    down(BTN_OCT_DOWN); down(BTN_GLO); up(BTN_GLO); up(BTN_OCT_DOWN);
    expect(BR_PAGE_MIX, 2u, "TEMPO", 3u, 0u);
    /* TEMPO has BPM, SWING, CLOCK SOURCE and MONITOR. */
    assert(!strcmp(br_control(0u).label, "BPM") && !strcmp(br_control(1u).label, "SWING") &&
           !strcmp(br_control(2u).label, "CLOCK SOURCE") && !strcmp(br_control(3u).label, "MONITOR"));
    /* Taps cycle inside the second group. */
    for (uint32_t i = 1u; i < 7u; ++i) {
        tap(BTN_GLO); expect(BR_PAGE_MIX, setup[i % 3u].screen, setup[i % 3u].title, 3u, i % 3u);
    }
    /* Long GLO continues the active group rather than switching groups. */
    tap(BTN_GLO); expect(BR_PAGE_MIX, 7u, MIDI1, 3u, 1u);
    hold(BTN_GLO, 520u); expect(BR_PAGE_MIX, 8u, MIDI2, 3u, 2u);
    /* Coming from another page (a long press, or a tap after HOME), GLO opens its first screen. */
    tap(BTN_ENV); hold(BTN_GLO, 700u);
#if BR_PRODUCT_SMK37
    expect(BR_PAGE_MIX, 3u, "GLOBAL / PAN", GLO_N, GLO_PAN_AT);
#else
    expect(BR_PAGE_MIX, 0u, "MIXER", 6u, 0u);
#endif
    tap(BTN_HOME); assert(br_ui.page == BR_PAGE_HOME);
    tap(BTN_GLO);
#if BR_PRODUCT_SMK37
    expect(BR_PAGE_MIX, 3u, "GLOBAL / PAN", GLO_N, GLO_PAN_AT);
#else
    expect(BR_PAGE_MIX, 0u, "MIXER", 6u, 0u);
#endif
    /* Holding GLO and turning SELECT still jumps to the global filter, with no group change on release. */
    tap(BTN_HOME); down(BTN_GLO); for (uint32_t i = 0u; i < 700u; ++i) tick();
    host_encoder[ENC_SELECT] += 1; tick(); up(BTN_GLO);
    expect(BR_PAGE_MIX, 1u, "GLOBAL / FILTER", GLO_N, GLO_FILTER_AT);
    /* OCT- + GLO opens TEMPO; GLO then reaches MIDI 1. */
    tap(BTN_HOME); down(BTN_OCT_DOWN); down(BTN_GLO); up(BTN_GLO); up(BTN_OCT_DOWN);
    expect(BR_PAGE_MIX, 2u, "TEMPO", 3u, 0u);
    tap(BTN_GLO);
    expect(BR_PAGE_MIX, 7u, MIDI1, 3u, 1u);
    /* Turning ALGORITHM on a second-group screen keeps the screen. */
    host_encoder[ENC_ALGORITHM] += 1; tick(); expect(BR_PAGE_MIX, 7u, MIDI1, 3u, 1u);
    host_encoder[ENC_ALGORITHM] -= 1; tick();
    /* ENV: one name per control. HOME filter knob 3 (ENV DEPTH) is the filter's own
     * depth from the mod envelope: it edits filter_env_depth whatever the envelope's
     * TARGET, and leaves that TARGET and its DEPTH alone. */
    tap(BTN_HOME); tap(BTN_ENV); expect(BR_PAGE_ENV, 0u, "ENV / AMP", 3u, 0u);
    tap(BTN_ENV); expect(BR_PAGE_ENV, 1u, "ENV / MOD", 3u, 1u);
    tap(BTN_ENV); expect(BR_PAGE_ENV, 2u, "ENV / MOD SET", 3u, 2u);
    assert(!strcmp(br_control(0u).label, "DEPTH"));
    for (uint32_t part = 0u; part < BR_PARTS; ++part) {
        br_ui.part = part; br_ui.page = BR_PAGE_ENV; tap(BTN_HOME); tap(BTN_HOME); assert(br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] == 1u);
        br_part[part].mod.target = BR_TARGET_PITCH; br_part[part].mod.amount = 33;
        br_part[part].sound.filter_env_depth = 0;
        {
            br_control_t c = br_control(2u);
            assert(!strcmp(c.label, "ENV DEPTH") && !c.text && c.low == -127 && c.value == 0);
            host_encoder[ENC_KNOB3] += 5; tick(); host_encoder[ENC_KNOB3] -= 9; tick();
            assert(br_part[part].mod.target == BR_TARGET_PITCH && br_part[part].mod.amount == 33);
            assert(br_part[part].sound.filter_env_depth < 0 && br_control(2u).value == br_part[part].sound.filter_env_depth);
        }
        br_part[part].mod.target = BR_TARGET_FILTER;
        { int32_t d = br_part[part].sound.filter_env_depth;
          host_encoder[ENC_KNOB3] += 1; tick();
          assert(br_part[part].sound.filter_env_depth == d + 1 && br_part[part].mod.amount == 33 && !br_control(2u).text); }
    }
    br_ui.part = 0u;

    /* LFO has three pages on every track; EDIT cycles the kit, depths and patches. */
    for (uint32_t part = 0u; part < BR_PARTS; ++part) {
        br_ui.part = part; tap(BTN_HOME);
        for (uint32_t i = 0u; i < 4u; ++i) {
            static const char *const title[3] = {"LFO 1", "LFO 2", "GLOBAL LFO"};
            tap(BTN_LFO); expect(BR_PAGE_LFO, i % 3u, title[i % 3u], 3u, i % 3u);
        }
        br_ui.page = BR_PAGE_ENV; tap(BTN_HOME);
        down(BTN_OCT_DOWN); down(BTN_HOME); up(BTN_HOME); up(BTN_OCT_DOWN);
        expect(BR_PAGE_HOME, 2u, "MOD DEPTHS", 2u, 0u);
        assert(!strcmp(br_control(0u).label, "TIMBRE") && !strcmp(br_control(3u).label, "PITCH"));
        uint32_t prior = br_part[part].sound.timbre_mod;
        host_encoder[ENC_KNOB1] += 1; tick();
        assert(br_part[part].sound.timbre_mod != prior || prior == 127u);
        tap(BTN_HOME); expect(BR_PAGE_HOME, 3u, "VOICE", 2u, 1u);
        down(BTN_OCT_DOWN); down(BTN_HOME); up(BTN_HOME); up(BTN_OCT_DOWN); assert(br_ui.screen[BR_PAGE_HOME] == 0u);
#if BR_PRODUCT_SMK37
        /* SMK-37: EDIT has no KIT LEVEL (FADER BANK opens it): PATCH 1, PATCH 2. */
        for (uint32_t i = 0u; i < 4u; ++i) {
            static const uint32_t screen[2] = {0u, 1u};
            static const char *const title[2] = {"PATCH 1", "PATCH 2"};
            tap(BTN_EDIT); expect(BR_PAGE_ROUTE, screen[i % 2u], title[i % 2u], 2u, i % 2u);
            assert(br_ui.part == part);
        }
#else
        for (uint32_t i = 0u; i < 4u; ++i) {
            static const uint32_t screen[3] = {3u, 0u, 1u};
            static const char *const title[3] = {"KIT LEVEL", "PATCH 1", "PATCH 2"};
            tap(BTN_EDIT); expect(BR_PAGE_ROUTE, screen[i % 3u], title[i % 3u], 3u, i % 3u);
            assert(br_ui.part == part);
        }
#endif
    }
    /* Switching tracks from any screen of any page never leaves an invalid screen: ALGORITHM (and the
     * SMK-37 arrows) from every screen ID of every page on every track. */
    for (uint32_t part = 0u; part < BR_PARTS; ++part)
        for (uint32_t page = 0u; page < BR_PAGE_COUNT; ++page)
            for (uint32_t screen = 0u; screen < 9u; ++screen)
                for (int32_t turn = -3; turn <= 3; ++turn) {
                    if (page == BR_PAGE_PUNCH || page == BR_PAGE_SCALE) continue;   /* overlays / the sound list */
                    br_ui.part = part; br_ui.page = page; br_ui.screen[page] = screen;
                    if (screen >= br_screen_count()) continue;
                    if (page == BR_PAGE_ROUTE && screen == 2u) continue; /* retired screen ID */
#if BR_PRODUCT_SMK37
                    if (turn & 1) br_smk_track_turn(turn); else
#endif
                    { host_encoder[ENC_ALGORITHM] += turn; tick(); }
                    {
                        const uint8_t *ring = 0;
                        uint32_t n = br_nav_ring(&ring), at = n ? br_nav_at(ring, n) : 0u;
                        uint32_t s2 = br_ui.screen[br_ui.page];
                        /* KIT LEVEL is valid with any selected track, including SMK track changes. */
                        /* SMK-37: KIT LEVEL is not in EDIT's ring, but FADER BANK opens it. */
                        /* ... and so is the MIXER (MIX screen 0) for GLO's ring. */
                        uint32_t outside = BR_PRODUCT_SMK37 && ((br_ui.page == BR_PAGE_ROUTE && s2 == 3u) ||
                                                                (br_ui.page == BR_PAGE_MIX && s2 == 0u));
                        if (s2 >= br_screen_count() ||
                            (n && !outside && ring[at] != BR_NAV(br_ui.page, s2))) {
                            fprintf(stderr, "track change from part %u page %u screen %u (turn %d): part %u page %u screen %u\n",
                                    part, page, screen, turn, br_ui.part, br_ui.page, s2);
                            assert(0);
                        }
                    }
                }
    br_ui.part = 0u; tap(BTN_HOME);
#if BR_PRODUCT_SMK37
    /* SMK-37: the MIXER is reached by FADER BANK, and GLO from it goes to PAN (the ring's first screen);
     * returning from the TEMPO/MIDI group (OCT- + GLO) also lands on PAN. */
    assert(br_smk_fader_bank_select(0u));
    assert(br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 0u && !strcmp(br_header_title(), "MIXER"));
    tap(BTN_GLO); expect(BR_PAGE_MIX, 3u, "GLOBAL / PAN", 5u, 0u);
    tap(BTN_GLO); expect(BR_PAGE_MIX, 1u, "GLOBAL / FILTER", 5u, 1u);
    down(BTN_OCT_DOWN); down(BTN_GLO); up(BTN_GLO); up(BTN_OCT_DOWN); expect(BR_PAGE_MIX, 2u, "TEMPO", 3u, 0u);
    down(BTN_OCT_DOWN); down(BTN_GLO); up(BTN_GLO); up(BTN_OCT_DOWN); expect(BR_PAGE_MIX, 3u, "GLOBAL / PAN", 5u, 0u);
    tap(BTN_HOME);
#endif
    /* GLOBAL / PAN: OCT- held snaps each pan knob 100% L / CENTER / 100% R; no octave change after. */
    {
        char text[24];
        br_ui.part = 0u; tap(BTN_HOME); tap(BTN_GLO);
#if !BR_PRODUCT_SMK37
        tap(BTN_GLO);      /* the FM-1's first tap is the MIXER; the SMK-37's is PAN */
#endif
        expect(BR_PAGE_MIX, 3u, "GLOBAL / PAN", GLO_N, GLO_PAN_AT);
        for (uint32_t k = 0u; k < 4u; ++k) {
            static const int32_t right[3] = {64, 127, 127}, left[3] = {64, 0, 0};
            int8_t octave = br_ui.octave;
            br_part[k].sound.pan = 0u;
            down(BTN_OCT_DOWN);
            for (uint32_t i = 0u; i < 3u; ++i) { host_encoder[ENC_KNOB1 + k] += 1; tick(); assert((int32_t)br_part[k].sound.pan == right[i]); }
            for (uint32_t i = 0u; i < 3u; ++i) { host_encoder[ENC_KNOB1 + k] -= 1; tick(); assert((int32_t)br_part[k].sound.pan == left[i]); }
            br_part[k].sound.pan = 83u;                       /* 30% R */
            host_encoder[ENC_KNOB1 + k] -= 1; tick(); assert(br_part[k].sound.pan == 64u);
            { br_control_t c = br_control(k); br_control_text(k, &c, text); assert(!strcmp(text, "CENTER")); }
            up(BTN_OCT_DOWN); assert(br_ui.octave == octave && br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 3u);
            /* Without OCT-: one detent, one step. */
            memset(br_encoder_speed, 0, sizeof br_encoder_speed);
            host_encoder[ENC_KNOB1 + k] += 1; tick(); assert(br_part[k].sound.pan == 65u);
            { br_control_t c = br_control(k); br_control_text(k, &c, text); assert(strcmp(text, "CENTER")); }
            br_part[k].sound.pan = 64u;
        }
        /* The hint row is the SENDS one. */
        {
            static uint16_t pan_row[240u * 24u];
            br_ui.redraw = 1u; br_draw(); memcpy(pan_row, fb + 216u * 240u, sizeof pan_row);
            br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
            assert(!memcmp(pan_row, fb + 216u * 240u, sizeof pan_row));
        }
        tap(BTN_HOME);
    }
    /* SNAP: knob 3 of the drum track's HOME on the Braids and sample kits (not the Plaits kit, whose
     * four slots are taken); the TEMPO screen no longer has it. Switching kits keeps the value. */
    {
        static const uint32_t kits[4] = {BR_KIT_BRAIDS, BR_KIT_808, BR_KIT_909, BR_KIT_PLAITS};
        br_ui.part = BR_DRUM_PART; tap(BTN_HOME); while (br_ui.screen[BR_PAGE_HOME]) tap(BTN_HOME);
        for (uint32_t i = 0u; i < 4u; ++i) {
            br_load_preset(BR_DRUM_PART, kits[i]); br_part[BR_DRUM_PART].sound.drum_snap = 40u; tick();
            assert(br_ui.page == BR_PAGE_HOME && !br_ui.screen[BR_PAGE_HOME]);
            memset(br_encoder_speed, 0, sizeof br_encoder_speed);
            host_encoder[ENC_KNOB3] += 1; tick();
            if (kits[i] == BR_KIT_PLAITS) assert(!strcmp(br_control(2u).label, "MORPH") && br_part[BR_DRUM_PART].sound.drum_snap == 40u);
            else {
                br_control_t c = br_control(2u);
                assert(!strcmp(c.label, "SNAP") && br_part[BR_DRUM_PART].sound.drum_snap == 41u);
                assert(br_control_widget(2u, &c) == BR_WIDGET_SNAP);
            }
        }
        br_ui.part = 0u; down(BTN_OCT_DOWN); down(BTN_GLO); up(BTN_GLO); up(BTN_OCT_DOWN); expect(BR_PAGE_MIX, 2u, "TEMPO", 3u, 0u);
        host_encoder[ENC_KNOB3] += 1; tick(); assert(br_part[BR_DRUM_PART].sound.drum_snap == 40u);
        tap(BTN_HOME);
    }
    puts("PASS: LFO / EDIT groups; track changes keep a valid screen");
    /* MIXER and KIT LEVEL read-out: 20 log10(v / 127), one decimal above -10 dB. */
    {
        static const struct { uint32_t v; const char *text; } db[] = {
            {127u, "0.0 dB"}, {64u, "-6.0 dB"}, {100u, "-2.1 dB"}, {41u, "-9.8 dB"}, {40u, "-10 dB"},
            {13u, "-20 dB"}, {1u, "-42 dB"}, {0u, "-INF"}};
        char text[16];
        for (uint32_t i = 0u; i < sizeof db / sizeof db[0]; ++i) {
            br_level_db_text(text, db[i].v);
            if (strcmp(text, db[i].text)) { fprintf(stderr, "level %u: '%s', want '%s'\n", db[i].v, text, db[i].text); assert(0); }
        }
    }
    puts("PASS: GLO groups (taps, long press, HOME, chord); ENV names; HOME ENV DEPTH is the filter's own depth");
    return 0;
}
