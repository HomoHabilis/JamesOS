/* SMK-37 Pro "fader link": NOTE REPEAT + SCALE held 0.8 s toggles the fader link; with it on,
 * fader 1 sets the T1 and T2 levels and fader 2 is ignored in the track-level bank.
 *
 * The device panel (smk37_panel.c) and input layer (smk37_input.h) do not build on the host, so
 * tick() below is their button path, line for line: the SCALE hold-back (smk_scale_hold_back), then the
 * panel (chord, NOTE REPEAT release/press), then the real app tick (dinghy.c: SCALE = LFO). With
 * SMK_LINK_BASE defined it is the 15d49c3 path (no chord) for a trace comparison against that tree;
 * `test_smk_fader_link --trace` prints the traces. */
#define DINGHY_HOST 1
#define DINGHY_SMK_PREVIEW 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "smk37_fader_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !BR_PRODUCT_SMK37
int main(void) { puts("SMK fader link: SMK-37 Pro only (compiled out)"); return 0; }
#else
#define REPEAT (1u << 17)                 /* SMK_BTN_REPEAT (smk37_input.h) */
#define SCALE (1u << BTN_LFO)             /* the printed SCALE is the logical LFO button */
static uint32_t held_now;
#ifndef SMK_LINK_BASE
static uint8_t held_back, late;           /* smk37_panel.c smk_scale_held_back, smk_scale_late */
static smk_chord_t chord;
#endif
static int tracing;

static void tick(uint32_t held)
{
    uint32_t pressed = held & ~held_now, released = held_now & ~held, repeat;
    held_now = held; fm1_in.buttons = held;
#ifndef SMK_LINK_BASE
    /* smk37_input.h fm1_input_edges -> smk37_panel.c smk_scale_hold_back */
    if ((pressed & SCALE) && (held & REPEAT)) { pressed &= ~SCALE; held_back = 1u; }
    if (late) { pressed |= SCALE; late = 0u; }
    /* smk37_panel.c br_smk_controls */
    if (!br_retrig_page()) br_smk_retrig.opened = 0u;
    repeat = br_smk_link_chord(&chord, pressed & REPEAT, &held_back, &late, (held & REPEAT) != 0u, (held & SCALE) != 0u);
#else
    repeat = (pressed & REPEAT) != 0u;
#endif
    if (released & REPEAT) {
        if (br_punch_key(&br_punch_ks, BR_PUNCH_SHIFT_KEY, 0u)) br_punch_changed();
    }
#ifndef SMK_LINK_BASE
    if (repeat) {
        uint32_t act = br_smk_repeat_press(&br_smk_retrig, br_step_press_pending() || br_ui.step_edit_active);
        if (act == SMK_REPEAT_PUNCH && br_punch_page() &&
            br_punch_key(&br_punch_ks, BR_PUNCH_SHIFT_KEY, 1u)) br_punch_changed();
    }
#else
    if (repeat && br_punch_page()) {
        if (br_punch_key(&br_punch_ks, BR_PUNCH_SHIFT_KEY, 1u)) br_punch_changed();
    }
#endif
    /* dinghy.c: the rest of the tick (SCALE is SEL there) */
    host_pressed = pressed & 0x3fffu; host_released = released & 0x3fffu;
    br_app_tick();
    fm1_ms += 1u;
}

static uint32_t link_now(void)
{
#ifndef SMK_LINK_BASE
    return br_smk_fader_link;
#else
    return 0u;
#endif
}

typedef struct { uint32_t page, screen, shift, link; } snap_t;
static snap_t snap(void)
{
    snap_t s = {br_ui.page, br_ui.screen[br_ui.page], br_punch_ks.shift, link_now()};
    return s;
}

/* A script: (buttons held, for ms) pairs, ending with everything up. Returns the final state and
 * fills trace[] with the state after every 1 ms tick. */
typedef struct { uint32_t held, ms; } step_t;
static uint32_t run(const char *name, const step_t *steps, uint32_t n, snap_t *trace, uint32_t cap)
{
    uint32_t t = 0u;
    br_app_init(); fm1_ms = 1000u; br_intro_until = 0u;
    br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = 0u; br_ui.part = 0u;
    held_now = 0u; fm1_in.buttons = 0u;
#ifndef SMK_LINK_BASE
    held_back = late = 0u; memset(&chord, 0, sizeof chord); memset(&br_smk_retrig, 0, sizeof br_smk_retrig); br_smk_fader_link = 0u;
#endif
    tick(0u);
    for (uint32_t i = 0u; i < n; ++i)
        for (uint32_t ms = 0u; ms < steps[i].ms; ++ms) {
            tick(steps[i].held);
            if (t < cap) trace[t] = snap();
            if (tracing) printf("%s %u page=%u screen=%u shift=%u link=%u\n", name, t, trace[t].page,
                                trace[t].screen, trace[t].shift, trace[t].link);
            ++t;
        }
    return t;
}

#define SCRIPT(name, ...) static const step_t name[] = {__VA_ARGS__}
SCRIPT(repeat_alone, {REPEAT, 300}, {0, 100});
SCRIPT(scale_alone, {SCALE, 300}, {0, 100});
SCRIPT(repeat_long, {REPEAT, 1500}, {0, 100});
SCRIPT(scale_long, {SCALE, 1500}, {0, 100});
SCRIPT(brief_rs_scale_up_first, {REPEAT, 200}, {REPEAT | SCALE, 200}, {REPEAT, 100}, {0, 100});
SCRIPT(brief_rs_repeat_up_first, {REPEAT, 200}, {REPEAT | SCALE, 200}, {SCALE, 100}, {0, 100});
SCRIPT(brief_sr, {SCALE, 200}, {REPEAT | SCALE, 200}, {0, 100});
SCRIPT(brief_together, {REPEAT | SCALE, 300}, {0, 100});
SCRIPT(hold_079, {REPEAT, 100}, {REPEAT | SCALE, 790}, {0, 100});
SCRIPT(hold_080, {REPEAT, 100}, {REPEAT | SCALE, 801}, {0, 100});
SCRIPT(hold_long_sr, {SCALE, 100}, {REPEAT | SCALE, 2500}, {SCALE, 100}, {0, 100});
SCRIPT(hold_together, {REPEAT | SCALE, 900}, {0, 100});
#define N(s) (uint32_t)(sizeof s / sizeof s[0])
static const struct { const char *name; const step_t *s; uint32_t n; } scripts[] = {
    {"repeat_alone", repeat_alone, N(repeat_alone)}, {"scale_alone", scale_alone, N(scale_alone)},
    {"repeat_long", repeat_long, N(repeat_long)}, {"scale_long", scale_long, N(scale_long)},
    {"brief_rs_scale_up_first", brief_rs_scale_up_first, N(brief_rs_scale_up_first)},
    {"brief_rs_repeat_up_first", brief_rs_repeat_up_first, N(brief_rs_repeat_up_first)},
    {"brief_sr", brief_sr, N(brief_sr)}, {"brief_together", brief_together, N(brief_together)},
    {"hold_079", hold_079, N(hold_079)}, {"hold_080", hold_080, N(hold_080)},
    {"hold_long_sr", hold_long_sr, N(hold_long_sr)}, {"hold_together", hold_together, N(hold_together)},
};

#ifndef SMK_LINK_BASE
static uint32_t toggles_in(const snap_t *t, uint32_t n)
{
    uint32_t c = 0u;
    for (uint32_t i = 1u; i < n; ++i) c += t[i].link != t[i - 1u].link;
    return c;
}

static void chord_checks(void)
{
    static snap_t a[4096];
    uint32_t n;
    {   /* the bare state machine: both down at 1000, still down at 1799 / 1800 / 5000 */
        smk_chord_t c = {0};
        assert(smk_chord_step(&c, 0u, 1u, 1u, 1u, 1000u) == 0u && c.state == SMK_CHORD_PENDING && c.undo);
        assert(smk_chord_step(&c, 0u, 0u, 1u, 1u, 1799u) == 0u);
        assert(smk_chord_step(&c, 0u, 0u, 1u, 1u, 1800u) == (SMK_CHORD_TOGGLE | SMK_CHORD_REPEAT_UNDO));
        assert(smk_chord_step(&c, 0u, 1u, 1u, 1u, 5000u) == 0u && smk_chord_step(&c, 1u, 0u, 1u, 1u, 5001u) == 0u);
        assert(smk_chord_step(&c, 0u, 0u, 0u, 0u, 5002u) == 0u && c.state == SMK_CHORD_IDLE);
        assert(smk_chord_step(&c, 1u, 0u, 1u, 0u, 6000u) == SMK_CHORD_REPEAT_PRESS);   /* alone: at once */
    }
    /* Alone, each button acts at once as before: NOTE REPEAT opens the punch page while held, SCALE the
     * LFO page on press. Both acts are at the same tick as 15d49c3 (the --trace comparison). */
    n = run("repeat_alone", repeat_alone, N(repeat_alone), a, 4096);
    assert(a[0].page == BR_PAGE_PUNCH && a[0].shift && a[n - 1].page == BR_PAGE_HOME && !a[n - 1].shift);
    n = run("scale_alone", scale_alone, N(scale_alone), a, 4096);
    assert(a[0].page == BR_PAGE_LFO && a[n - 1].page == BR_PAGE_LFO && !a[n - 1].link);
    n = run("repeat_long", repeat_long, N(repeat_long), a, 4096); assert(!toggles_in(a, n));
    n = run("scale_long", scale_long, N(scale_long), a, 4096); assert(!toggles_in(a, n));
    /* Both briefly (NOTE REPEAT first): punch page at once; SCALE's press waits for the first release,
     * then opens the LFO page as it would have; same end state as before, no toggle. */
    n = run("rs1", brief_rs_scale_up_first, N(brief_rs_scale_up_first), a, 4096);
    assert(a[0].page == BR_PAGE_PUNCH && a[399].page == BR_PAGE_PUNCH && a[400].page == BR_PAGE_PUNCH && a[401].page == BR_PAGE_LFO);
    assert(!toggles_in(a, n) && a[n - 1].page == BR_PAGE_LFO && !a[n - 1].shift);
    n = run("rs2", brief_rs_repeat_up_first, N(brief_rs_repeat_up_first), a, 4096);
    assert(a[399].page == BR_PAGE_PUNCH && a[401].page == BR_PAGE_LFO && !toggles_in(a, n) && a[n - 1].page == BR_PAGE_LFO);
    /* SCALE first: LFO page at once (NOTE REPEAT does nothing on the LFO page, then or now). */
    n = run("sr", brief_sr, N(brief_sr), a, 4096);
    assert(a[0].page == BR_PAGE_LFO && !toggles_in(a, n) && a[n - 1].page == BR_PAGE_LFO);
    /* Pressed in the same tick and let go early: NOTE REPEAT goes first, as before. */
    n = run("together", brief_together, N(brief_together), a, 4096);
    assert(a[0].page == BR_PAGE_HOME && a[n - 1].page == BR_PAGE_LFO && !toggles_in(a, n));
    /* 0.79 s together: nothing; 0.8 s: one toggle, at exactly 0.8 s after the second press (1 ms ticks). */
    n = run("hold_079", hold_079, N(hold_079), a, 4096);
    assert(!toggles_in(a, n) && a[n - 1].page == BR_PAGE_LFO);
    n = run("hold_080", hold_080, N(hold_080), a, 4096);
    assert(toggles_in(a, n) == 1u && a[n - 1].link == 1u && !a[899].link && a[900].link);
    assert(!strcmp(br_ui.message, "FADER LINK ON"));
    /* Fired: the punch page NOTE REPEAT opened is closed; SCALE never acted; release does nothing. */
    assert(a[899].page == BR_PAGE_PUNCH && a[900].page == BR_PAGE_HOME && !a[900].shift && a[n - 1].page == BR_PAGE_HOME);
    /* Held 2.5 s: still one toggle; SCALE first stays on its page (it acted at its press). */
    n = run("hold_long_sr", hold_long_sr, N(hold_long_sr), a, 4096);
    assert(toggles_in(a, n) == 1u && a[n - 1].page == BR_PAGE_LFO);
    n = run("hold_together", hold_together, N(hold_together), a, 4096);
    assert(toggles_in(a, n) == 1u && a[n - 1].page == BR_PAGE_HOME && !a[n - 1].shift);
    /* Toggle back off. */
    br_smk_fader_link = 1u; br_smk_fader_link_toggle();
    assert(!br_smk_fader_link && !strcmp(br_ui.message, "FADER LINK OFF"));
    assert(br_widget_text_width("FADER LINK OFF") <= 200);
}

/* NOTE REPEAT on the drum track: a tap toggles RETRIGGER (smk37_panel_logic.h smk_repeat_decide). */
static void retrig_checks(void)
{
    uint32_t n;
    /* the pure function */
    assert(smk_repeat_decide(0u, 0u, 0u, 0u) == SMK_REPEAT_PUNCH && smk_repeat_decide(0u, 1u, 1u, 1u) == SMK_REPEAT_PUNCH);
    assert(smk_repeat_decide(1u, 0u, 0u, 0u) == SMK_REPEAT_OPEN_RETRIG);
    assert(smk_repeat_decide(1u, 1u, 1u, 0u) == SMK_REPEAT_RETURN);
    assert(smk_repeat_decide(1u, 1u, 0u, 0u) == SMK_REPEAT_NONE);        /* opened from ARP: no return page */
    assert(smk_repeat_decide(1u, 0u, 0u, 1u) == SMK_REPEAT_NONE && smk_repeat_decide(1u, 1u, 1u, 1u) == SMK_REPEAT_NONE);
    /* in the app: HOME, drum track; the first tap opens it, the second returns, no punch page either time */
    br_app_init(); fm1_ms = 1000u; br_intro_until = 0u; br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = 2u;
    br_ui.part = BR_DRUM_PART; held_now = 0u; fm1_in.buttons = 0u; memset(&chord, 0, sizeof chord);
    memset(&br_smk_retrig, 0, sizeof br_smk_retrig); held_back = late = 0u;
    tick(0u);
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(br_retrig_page() && br_ui.page == BR_PAGE_PATTERN && !br_punch_ks.shift);
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    assert(br_retrig_page());                                           /* stays after release */
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(!br_retrig_page() && br_ui.page == BR_PAGE_FX && br_ui.screen[BR_PAGE_FX] == 2u);
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    assert(br_ui.page == BR_PAGE_FX && !br_punch_ks.shift);
    /* a page button leaves it */
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(br_retrig_page());
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    for (uint32_t i = 0u; i < 50u; ++i) tick(SCALE);
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    assert(br_ui.page == BR_PAGE_LFO);
    /* the ARP button on the drum track opens the cue page, not RETRIGGER; NOTE REPEAT there opens RETRIGGER
     * (returning to the ARP page on the next tap), and the ARP button leaves RETRIGGER for the cue again */
    br_buttons(1u << BTN_ARP); assert(!br_retrig_page() && br_arp_cue_page());
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(br_retrig_page() && !br_arp_cue_page());
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    assert(br_retrig_page());
    br_buttons(1u << BTN_ARP); assert(!br_retrig_page() && br_arp_cue_page());
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(br_retrig_page());
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(!br_retrig_page() && br_arp_cue_page() && br_ui.page == BR_PAGE_PATTERN);   /* back to the ARP cue */
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    /* ignored while a step press is pending */
    br_ui.page = BR_PAGE_FX; br_ui.step_press_key = 0u;
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(br_ui.page == BR_PAGE_FX);
    br_ui.step_press_key = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    /* the fader-link chord with NOTE REPEAT first: the retrig page it opened closes at 0.8 s */
    br_ui.page = BR_PAGE_ENV; br_smk_fader_link = 0u;
    n = 0u;
    for (uint32_t i = 0u; i < 100u; ++i) tick(REPEAT);
    assert(br_retrig_page());
    for (uint32_t i = 0u; i < 850u; ++i) { tick(REPEAT | SCALE); if (br_smk_fader_link) break; ++n; }
    assert(br_smk_fader_link && br_ui.page == BR_PAGE_ENV && n >= 799u && n <= 801u);
    for (uint32_t i = 0u; i < 100u; ++i) tick(0u);
    assert(br_ui.page == BR_PAGE_ENV);
    /* synth track: unchanged, the punch page while held */
    br_ui.part = 0u; br_ui.page = BR_PAGE_HOME; br_smk_fader_link = 0u;
    for (uint32_t i = 0u; i < 50u; ++i) tick(REPEAT);
    assert(br_ui.page == BR_PAGE_PUNCH && br_punch_ks.shift);
    for (uint32_t i = 0u; i < 50u; ++i) tick(0u);
    assert(br_ui.page == BR_PAGE_HOME);
}

static uint32_t rnd(void) { static uint32_t x = 12345u; x = x * 1103515245u + 12345u; return x >> 16; }

static void fader_checks(void)
{
    smk_fader_state_t s;
    uint8_t v[4] = {10, 20, 30, 40};
    br_app_init(); fm1_ms = 1000u; br_intro_until = 0u;
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u; br_smk_fader_link = 0u;
    memset(&s, 0, sizeof s); smk_fader_reset_pickup(&s, v); s.ready = 1u;
    br_part[0].sound.level = 60; br_part[1].sound.level = 100; br_part[2].sound.level = 70;
    /* On: T2 takes T1's level value (not the fader position) at once. */
    br_smk_fader_link_toggle();
    assert(br_smk_fader_link && br_part[1].sound.level == 60 && !strcmp(br_ui.message, "FADER LINK ON"));
    br_smk_faders_step(&s, v, 0u);
    /* Fader 2 noise: no value, no peek, no pickup change. */
    br_smk_fader_peek_cancel();
    uint8_t prev1 = s.previous[1], picked1 = s.picked[1];
    for (uint32_t i = 0u; i < 500u; ++i) {
        v[1] = (uint8_t)(rnd() % 128u); br_smk_faders_step(&s, v, 0u); fm1_ms += 2u;
        assert(br_part[0].sound.level == 60 && br_part[1].sound.level == 60 && br_part[2].sound.level == 70);
        assert(!br_smk_fader_peek.active && s.previous[1] == prev1 && s.picked[1] == picked1);
    }
    /* Fader 1: its own pickup (crossing 60), then both levels follow; the peek shows. */
    v[0] = 50; br_smk_faders_step(&s, v, 0u);
    assert(br_part[0].sound.level == 60 && br_part[1].sound.level == 60 && br_smk_fader_peek.active);
    v[0] = 61; br_smk_faders_step(&s, v, 0u);
    assert(br_part[0].sound.level == 61 && br_part[1].sound.level == 61);
    v[0] = 90; br_smk_faders_step(&s, v, 0u);
    assert(br_part[0].sound.level == 90 && br_part[1].sound.level == 90 && br_part[2].sound.level == 70);
    v[2] = 31; br_smk_faders_step(&s, v, 0u); assert(br_part[2].sound.level == 70);   /* fader 3: pickup as before */
    /* Bank 1 (kit lanes): the link holds there too: fader 2 is ignored, fader 1 sets lanes 1 and 2. */
    br_smk_faders_step(&s, v, 1u); assert(s.bank == 1u && br_gate_page());   /* FADER BANK opens KIT LEVEL */
    uint32_t lane0 = br_part[BR_DRUM_PART].lane_level[0], lane1 = br_part[BR_DRUM_PART].lane_level[1];
    assert(lane0 == lane1);                                                  /* (the toggle made them equal) */
    for (uint32_t i = 0u; i < 200u; ++i) {
        v[1] = (uint8_t)(rnd() % 128u); br_smk_faders_step(&s, v, 0u);
        assert(br_part[BR_DRUM_PART].lane_level[1] == lane1 && br_part[1].sound.level == 90);
    }
    v[0] = (uint8_t)(lane0 > 5 ? lane0 - 5 : lane0 + 5); br_smk_faders_step(&s, v, 0u);   /* pickup: cross the value */
    v[0] = (uint8_t)(lane0 > 5 ? lane0 + 1 : lane0 - 1); br_smk_faders_step(&s, v, 0u);
    assert(br_part[BR_DRUM_PART].lane_level[0] == v[0] && br_part[BR_DRUM_PART].lane_level[1] == v[0]);
    v[0] = 30; br_smk_faders_step(&s, v, 0u);
    br_smk_faders_step(&s, v, 1u); assert(s.bank == 0u && br_smk_fader_link && br_ui.page == BR_PAGE_MIX);
    v[0] = 40; br_smk_faders_step(&s, v, 0u); assert(br_part[0].sound.level == 90);     /* re-pickup after a bank switch */
    v[0] = 95; br_smk_faders_step(&s, v, 0u); assert(br_part[0].sound.level == 95 && br_part[1].sound.level == 95);
    /* Off: both stay; fader 2 needs a fresh pickup (no jump); the faders are independent again. */
    v[1] = 120; br_smk_faders_step(&s, v, 0u);
    br_smk_fader_link_toggle();
    assert(!br_smk_fader_link && br_part[0].sound.level == 95 && br_part[1].sound.level == 95);
    br_smk_faders_step(&s, v, 0u);
    v[1] = 119; br_smk_faders_step(&s, v, 0u); assert(br_part[1].sound.level == 95);
    v[1] = 94; br_smk_faders_step(&s, v, 0u); assert(br_part[1].sound.level == 94);
    v[0] = 70; br_smk_faders_step(&s, v, 0u); assert(br_part[0].sound.level == 70 && br_part[1].sound.level == 94);
    /* SAVE -> clear -> LOAD keeps the mode; a LOAD that turns it off resets fader 2's pickup. */
    static br_session_t saved;
    br_smk_fader_link = 1u; assert(br_capture(&saved));
    br_app_init(); br_smk_fader_link = 0u;
    assert(br_restore(&saved) && br_smk_fader_link == 1u);
    { static br_session_t off; br_smk_fader_link = 0u; assert(br_capture(&off)); br_smk_fader_link = 1u;
      s.link = 1u; s.picked[1] = 1u; s.bank = 0u; v[1] = 5; assert(br_restore(&off) && !br_smk_fader_link);
      br_smk_faders_step(&s, v, 0u); assert(!s.picked[1] && s.previous[1] == 5u); }
}
#endif

int main(int argc, char **argv)
{
    static snap_t a[4096];
    tracing = argc > 1 && !strcmp(argv[1], "--trace");
    if (tracing) {
        for (uint32_t i = 0u; i < sizeof scripts / sizeof scripts[0]; ++i)
            run(scripts[i].name, scripts[i].s, scripts[i].n, a, 4096);
        return 0;
    }
#ifndef SMK_LINK_BASE
    chord_checks();
    retrig_checks();
    fader_checks();
    puts("SMK fader link checks passed");
#endif
    return 0;
}
#endif
