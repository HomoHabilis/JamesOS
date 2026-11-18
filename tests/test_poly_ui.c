/* SPDX-License-Identifier: GPL-3.0-only */
/* Polyphony through the real UI (keys, MIDI, HOLD, PLAY, pages, ARP, SAVE, punch) with the real
 * polyphonic engine, for either product: build with -DBR_PRODUCT=BR_PRODUCT_ID_FM1 -DBR_CAP_POLY=1 or
 * -DBR_PRODUCT=BR_PRODUCT_ID_SMK37 (see tests/poly_ui_host.h). The host keyboard is the FM-1 one on both
 * products (the SMK-37 pads, wheels and velocity exist only on the device); everything else is the
 * shared app. Polyphony is entered and left by dinghy.c's host model of the foreground transition. */
#define POLY_UI_HOST_TEST 1
#include "poly_ui_host.h"
#include <assert.h>
#include <stdio.h>

static int32_t blk[2u * HALF_FRAMES];
static uint64_t energy;
static void tick(void)
{
    ++fm1_ms; br_app_tick();
    memset(blk, 0, sizeof blk); app_audio(blk, HALF_FRAMES);
    for (uint32_t i = 0; i < 2u * HALF_FRAMES; ++i) energy += (uint64_t)(blk[i] < 0 ? -(int64_t)blk[i] : blk[i]);
}
static void ticks(uint32_t n) { while (n--) tick(); }
static void keys(uint32_t mask) { host_note_pressed |= mask & ~fm1_in.notes; fm1_in.notes = mask; ticks(3); }
static void tap(uint32_t button)
{
    fm1_in.buttons |= 1u << button; host_pressed |= 1u << button; tick();
    fm1_in.buttons &= ~(1u << button); host_released |= 1u << button; ticks(3);
}
static void long_press(uint32_t button)
{
    fm1_in.buttons |= 1u << button; host_pressed |= 1u << button; ticks(820);
    fm1_in.buttons &= ~(1u << button); host_released |= 1u << button; ticks(3);
}
static void poly_shortcut(void)
{
    fm1_in.buttons |= 1u << BTN_OCT_DOWN; host_pressed |= 1u << BTN_OCT_DOWN; tick();
    tap(BTN_PLAY);
    fm1_in.buttons &= ~(1u << BTN_OCT_DOWN); host_released |= 1u << BTN_OCT_DOWN; ticks(3);
}
static void hold_shortcut(void)
{ long_press(BR_HOLD_BUTTON); }   /* HOME on the FM-1, the printed PATCH (logical SEL) on the SMK-37 */
static void home_deep_shortcut(void)
{
    fm1_in.buttons |= 1u << BTN_OCT_DOWN; host_pressed |= 1u << BTN_OCT_DOWN; tick();
    tap(BTN_HOME);
    fm1_in.buttons &= ~(1u << BTN_OCT_DOWN); host_released |= 1u << BTN_OCT_DOWN; ticks(3);
}
static void midi(uint8_t status, uint8_t d1, uint8_t d2) { br_midi(status, d1, d2); ticks(2); }
#define K(k) (1u << (k))
static const uint32_t CHORD = K(0) | K(4) | K(7) | K(11) | K(14) | K(16);   /* notes 53 57 60 64 67 69 */
static uint32_t held(void) { return br_poly_scheduler_events.count; }
static uint32_t sounding(void)                    /* voices held down, not releasing */
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < BR_POLY_VOICES; ++i)
        n += br_poly_scheduler.envelope[i].stage != BR_ENV_OFF && br_poly_scheduler.envelope[i].stage != BR_ENV_RELEASE;
    return n;
}
static int voice_with_note(uint32_t note)
{
    for (uint32_t i = 0; i < BR_POLY_VOICES; ++i)
        if (br_poly_scheduler.envelope[i].stage != BR_ENV_OFF && br_poly_scheduler.envelope[i].stage != BR_ENV_RELEASE &&
            br_poly_scheduler.envelope[i].note == note) return (int)i;
    return -1;
}
static uint32_t wait_poly(uint32_t want, uint32_t limit)
{
    while (limit-- && br_poly_mode_active() != want) tick();
    return br_poly_mode_active() == want;
}
static void load_model(uint32_t model) { br_load_preset(br_ui.part, BR_FIRST_PLAITS + model); ticks(3); }
static void home(void) { if (br_ui.page != BR_PAGE_HOME) tap(BTN_HOME); }
static int hint_is(const char *text) { return !strcmp(br_oct_hint(), text); }
static void check_home_voice_badge(uint32_t voices)
{
    static uint16_t actual[240u * 28u];
    char text[24], number[8];
    br_ui_accent = br_track_color(br_ui.part);
    br_draw_page_route_badge(1u, br_ui.part);
    memcpy(actual, fb + 38u * 240u, sizeof actual);
    cv_begin(240u, 36u, BR_BACKGROUND); cv_oy = 8;   /* as br_draw_page_route_badge: the badge 8 px down */
    br_text_copy(text, voices ? "POLY: " : "MONO", sizeof text);
    if (voices) {
        br_number(number, (int32_t)voices); br_text_append(text, number, sizeof text);
        br_text_append(text, " VOICES", sizeof text);
    }
    br_draw_page_badge(text, br_track_color(br_ui.part)); cv_blit(0u, 38u); cv_oy = 0;
    assert(!memcmp(actual, fb + 38u * 240u, sizeof actual));
}

int main(void)
{
    br_app_init(); fm1_ms = 2000;
    br_part[0].sound.voice_mode = 2u; /* exercise poly even when the default bass is mono */
    tick();
    assert(br_ui.page == BR_PAGE_HOME && br_ui.part == 0u);
    load_model(8u);
    assert(wait_poly(1u, 4000u));                         /* stopped, HOME, a Plaits model: polyphony */
#if defined(BR_ADAPTIVE_POLY) && BR_ADAPTIVE_POLY
    { uint32_t capacity[4]; br_poly_capacity_snapshot(capacity); check_home_voice_badge(capacity[1]); }
#else
    check_home_voice_badge(6u);
#endif
    assert(hint_is(BR_PRODUCT_SMK37 ? "" : "OCT- + PLAY: POLYPHONY OFF"));
    assert(strcmp(br_ui.message, "POLY: 6 NOTES"));
    assert(br_fx_poly_loan_active() == !!BR_MODEL_MOD_EXPERIMENT);

    /* 1. Six keys: six voices; every release reaches its own voice (key tags on press and release). */
    keys(CHORD);
    assert(held() == 6u && sounding() == 6u);
    static const uint8_t chord_notes[6] = {53, 57, 60, 64, 67, 69};
    for (uint32_t i = 0; i < 6u; ++i) assert(voice_with_note(chord_notes[i]) >= 0);
    keys(CHORD & ~K(7));
    assert(held() == 5u && sounding() == 5u && voice_with_note(60) < 0);
    keys(0u);
    assert(held() == 0u && sounding() == 0u);
    printf("keys: six voices, tagged releases ok\n");

    /* 2. The seventh note steals the oldest voice; the stolen key's release leaves the new voice alone. */
    ticks(400);
    keys(K(0)); keys(K(0) | K(4)); keys(K(0) | K(4) | K(7)); keys(K(0) | K(4) | K(7) | K(11));
    keys(K(0) | K(4) | K(7) | K(11) | K(14)); keys(CHORD);
    int oldest = voice_with_note(53);
    assert(oldest >= 0);
    keys(CHORD | K(19));                                  /* note 72 */
    assert(held() == 7u && sounding() == 6u && voice_with_note(53) < 0 && voice_with_note(72) == oldest);
    keys((CHORD | K(19)) & ~K(0));                        /* release the stolen key */
    assert(held() == 6u && sounding() == 6u && voice_with_note(72) == oldest);
    keys(K(19));
    assert(held() == 1u && sounding() == 1u);
    keys(0u);
    assert(held() == 0u && sounding() == 0u);
    printf("seventh note steals the oldest voice ok\n");

    /* 3. MIDI (USB and DIN share br_midi): six voices, stealing, and a key and MIDI on one pitch. */
    for (uint8_t n = 60; n < 66; ++n) midi(0x90, n, 100);
    assert(held() == 6u && sounding() == 6u);
    midi(0x90, 66, 100);
    assert(held() == 7u && sounding() == 6u && voice_with_note(60) < 0 && voice_with_note(66) >= 0);
    for (uint8_t n = 60; n < 67; ++n) midi(0x80, n, 0);
    assert(held() == 0u && sounding() == 0u);
    keys(K(7)); midi(0x90, 60, 90);                      /* key 7 is note 60 too: one voice, two holders */
    assert(held() == 2u && sounding() == 1u);
    midi(0x80, 60, 0);
    assert(held() == 1u && sounding() == 1u);
    keys(0u);
    assert(held() == 0u && sounding() == 0u);
    printf("MIDI: six voices, stealing, shared pitch ok\n");

    /* 4. HOLD: released keys keep sounding; a new key with none held releases them (tagged). */
    ticks(400);
    hold_shortcut(); assert(br_hold_on);
    keys(CHORD); keys(0u);
    assert(br_poly_mode_active() && held() == 6u && sounding() == 6u);
    keys(K(2));
    assert(held() == 1u && sounding() == 1u && voice_with_note(55) >= 0);
    keys(0u);
    assert(held() == 1u);                                /* sustained by HOLD */
    hold_shortcut(); assert(!br_hold_on);
    assert(held() == 0u && sounding() == 0u);
    printf("HOLD ok\n");

    /* 5. The PUNCH overlay (shift key 25) keeps polyphony; the punch effects reach the chord. */
    assert(wait_poly(1u, 4000u));
    keys(CHORD);
    keys(CHORD | K(25));
    assert(br_ui.page == BR_PAGE_PUNCH);
    ticks(20);
    assert(br_poly_mode_active() && held() == 6u && sounding() == 6u);
    assert(hint_is("OCT-/+: PAGES"));                    /* the page's own cue keeps its place */
    static const struct { uint32_t key, effect; } fx[] = {
        {1u, BR_PUNCH_LOOP}, {13u, BR_PUNCH_FILTER}, {20u, BR_PUNCH_TREM}, {8u, BR_PUNCH_ECHO}};
    for (uint32_t i = 0; i < 4u; ++i) {
        uint64_t dry, wet;
        energy = 0; ticks(60); dry = energy;
        keys(CHORD | K(25) | K(fx[i].key));
        uint32_t refused = BR_MODEL_MOD_EXPERIMENT && fx[i].effect == BR_PUNCH_ECHO;
        assert(br_punch_effect == (refused ? BR_PUNCH_NONE : fx[i].effect) && br_poly_mode_active() && sounding() == 6u);
        energy = 0; ticks(60); wet = energy;
        if (!refused && fx[i].effect != BR_PUNCH_LOOP) assert(wet != dry);      /* it changes the sound */
        keys(CHORD | K(25));
        assert(br_punch_effect == BR_PUNCH_NONE && br_poly_mode_active());
    }
    keys(CHORD);
    assert(br_ui.page == BR_PAGE_HOME && br_poly_mode_active() && sounding() == 6u);
    keys(0u);
    printf("PUNCH overlay keeps six voices; effects follow the active delay-loan policy\n");

    /* 6. Echo with the loan models: refused (FM-1 keys here; the SMK-37 refuses its echo pad on the
     * device), every other effect allowed; the loan never exists for the other models. */
    static const uint8_t models[] = {0u, 8u, 13u, 15u, 18u, 19u, 23u};
    for (uint32_t m = 0; m < sizeof models; ++m) {
        uint32_t loan = BR_MODEL_MOD_EXPERIMENT || models[m] == 15u || models[m] == 18u || models[m] == 19u;
        load_model(models[m]);
        assert(wait_poly(1u, 8000u));
        assert(br_fx_poly_loan_active() == loan);
        keys(K(25));
#if !BR_PRODUCT_SMK37
        /* With SHIFT held the group keys light up; the echo keys stay dark while echo is unavailable. */
        assert((host_led_level_state[KEY_LED(8)] == FM1_LED_OFF) == loan);
        assert((host_led_level_state[KEY_LED(10)] == FM1_LED_OFF) == loan);
        assert(host_led_level_state[KEY_LED(13)] != FM1_LED_OFF);
#endif
        keys(K(25) | K(10));
#if !BR_PRODUCT_SMK37
        assert(br_punch_ks.active == BR_PUNCH_ECHO);
        assert(br_punch_effect == (loan ? BR_PUNCH_NONE : BR_PUNCH_ECHO));
        if (loan) assert(!strcmp(br_ui.message, "ECHO UNAVAILABLE IN POLY") && (int32_t)(br_ui.message_until - fm1_ms) > 0);
#endif
        keys(K(25)); keys(K(25) | K(15));
        assert(br_punch_effect == BR_PUNCH_FILTER && br_poly_mode_active());
        keys(K(25)); keys(0u);
        assert(br_ui.page == BR_PAGE_HOME && br_poly_mode_active());
    }
    printf("echo refused while the delay loan is active ok\n");
    load_model(8u); assert(wait_poly(1u, 8000u));

    /* 7. PLAY with notes held: polyphony ends in the same tick and the sequencer starts; it returns only
     * when stopped with the keys released (and the release finished). */
    keys(CHORD);
    tap(BTN_PLAY);
    assert(!br_poly_mode_active() && br_playing && !br_fx_poly_loan_active());
    check_home_voice_badge(0u);
    assert(hint_is("POLYPHONY OFF DURING PLAYBACK"));
    ticks(200); assert(!br_poly_mode_active());
    tap(BTN_PLAY); ticks(20); assert(!br_playing);
    ticks(200); assert(!br_poly_mode_active());          /* keys still held */
    keys(0u);
    assert(wait_poly(1u, 8000u) && hint_is(BR_PRODUCT_SMK37 ? "" : "OCT- + PLAY: POLYPHONY OFF"));
    printf("PLAY with notes held ok\n");

    /* 8. Page and track changes end it; coming back restores it. */
    tap(BTN_SEQ); assert((BR_PRODUCT_SMK37 ? br_poly_mode_active() : !br_poly_mode_active()) && hint_is("OCT-/+ + REC SHIFT NOTES"));
    home(); assert(wait_poly(1u, 8000u));
    tap(BTN_ENV); assert(br_poly_mode_active());          /* ENV plays keys */
    home();
    host_encoder[ENC_ALGORITHM] += 1; ticks(3);
    assert(br_ui.part == 1u);
    assert(wait_poly(1u, 8000u) && br_poly_scheduler_part == 1u);
    host_encoder[ENC_ALGORITHM] -= 1; ticks(3);
    assert(wait_poly(1u, 8000u) && br_poly_scheduler_part == 0u);
    printf("page and track changes ok\n");

    /* 9. ARP on ends polyphony and keeps it off; ARP off brings it back. */
    long_press(BTN_ARP); assert(br_arp_on);
    ticks(50); assert(!br_poly_mode_active());
    long_press(BTN_ARP); assert(!br_arp_on);
    assert(wait_poly(1u, 8000u));
    printf("ARP ok\n");

    /* 10. SAVE ends it before the SAVE screen opens. */
    tap(BTN_SAVE);
    assert(es.mode && !br_poly_mode_active());
    es_close(); ticks(3);
    assert(wait_poly(1u, 8000u));
    printf("SAVE ok\n");

    /* 11. A held step on the grid never auditions into the polyphonic engine. */
#if !BR_PRODUCT_SMK37
    tap(BTN_SEQ); assert(!br_poly_mode_active());
    keys(K(0)); ticks(700);                               /* hold step 1 until it becomes an edit */
    keys(K(0) | K(4));                                    /* another white key sets and sounds its pitch */
    assert(!br_poly_mode_active() && held() == 0u);
    keys(K(0)); keys(0u);
    home(); assert(wait_poly(1u, 8000u));
    printf("step audition stays out of the polyphonic engine ok\n");
#endif
    /* Voice mode is set independently on the selected track. */
    home();
    home_deep_shortcut(); assert(br_ui.page == BR_PAGE_HOME && br_ui.screen[BR_PAGE_HOME] == 2u);
    tap(BTN_HOME); assert(br_ui.screen[BR_PAGE_HOME] == 3u);
    home_deep_shortcut(); assert(br_ui.screen[BR_PAGE_HOME] == 0u);
    {
        uint32_t initial_hold = br_hold_on; int32_t initial_octave = br_ui.octave;
        fm1_in.buttons |= 1u << BTN_OCT_DOWN; host_pressed |= 1u << BTN_OCT_DOWN; tick();
        tap(BTN_HOME);
        fm1_in.buttons &= ~(1u << BTN_OCT_DOWN); host_released |= 1u << BTN_OCT_DOWN; ticks(3);
        assert(br_hold_on == initial_hold && br_ui.octave == initial_octave && br_ui.screen[BR_PAGE_HOME] == 2u);
        fm1_in.buttons |= 1u << BTN_HOME; host_pressed |= 1u << BTN_HOME; tick();
        tap(BTN_OCT_DOWN);
        fm1_in.buttons &= ~(1u << BTN_HOME); host_released |= 1u << BTN_HOME; ticks(3);
        assert(br_hold_on == initial_hold && br_ui.octave == initial_octave && br_ui.screen[BR_PAGE_HOME] == 0u);
    }
    home(); assert(wait_poly(1u, 8000u));
    fm1_in.buttons |= 1u << BTN_OCT_DOWN; host_pressed |= 1u << BTN_OCT_DOWN; tick();
    tap(BTN_GLO); fm1_in.buttons &= ~(1u << BTN_OCT_DOWN); host_released |= 1u << BTN_OCT_DOWN; ticks(3);
    assert(br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 2u && !strcmp(br_control(2u).label, "CLOCK SOURCE"));
    home(); poly_shortcut();
    assert(!strcmp(br_ui.message, "POLYPHONY OFF") && (int32_t)(br_ui.message_until - fm1_ms) > 0);
    ticks(3000);
    assert(br_part[0].sound.voice_mode == 1u && !br_playing);
    assert(!br_poly_mode_active() && hint_is("OCT- + PLAY: POLYPHONY ON"));
    keys(CHORD); assert(!br_poly_mode_active() && held() == 0u); keys(0u); ticks(400);
    br_ui.part = 1u; br_part[1].sound.voice_mode = 2u; ticks(3);
    assert(wait_poly(1u, 8000u));
    br_ui.part = 0u; ticks(3); assert(!br_poly_mode_active());
    poly_shortcut();
    assert(!strcmp(br_ui.message, "POLYPHONY ON") && (int32_t)(br_ui.message_until - fm1_ms) > 0);
    assert(br_part[0].sound.voice_mode == 2u && br_part[1].sound.voice_mode == 2u);
    home(); assert(wait_poly(1u, 8000u) && hint_is(BR_PRODUCT_SMK37 ? "" : "OCT- + PLAY: POLYPHONY OFF"));
    tap(BTN_PLAY); assert(!br_poly_mode_active() && br_playing);
    tap(BTN_PLAY); ticks(20); assert(!br_playing); assert(wait_poly(1u, 8000u));
    /* Poly glide starts at the previously heard voice only when LEGATO overlaps. */
    br_part[0].sound.portamento = 100u; br_part[0].sound.glide_mode = BR_GLIDE_LEGATO;
    br_part[0].sound.amp_mode = 1u; br_part[0].sound.release = 75u; br_part[0].revision += 2u; ticks(5);
    keys(K(0)); ticks(20); keys(K(0) | K(12)); ticks(20);
    { int voice = voice_with_note(65); /* key 12 maps to MIDI 65 */
      assert(voice >= 0 && br_poly_scheduler.glide[voice].duration > 0u);
      assert(br_poly_scheduler.glide[voice].elapsed < br_poly_scheduler.glide[voice].duration);
      assert(br_poly_scheduler.lpg[voice] != 0);
      keys(0u); ticks(2);
      /* (the 2 ms close may already have finished) */
      assert(br_poly_scheduler.envelope[voice].stage == BR_ENV_RELEASE || br_poly_scheduler.envelope[voice].stage == BR_ENV_OFF);
      assert(br_poly_scheduler.envelope[voice].release == br_lpg_release_samples()); }
    ticks(20);
    br_part[0].sound.portamento = br_part[0].sound.glide_mode = br_part[0].sound.amp_mode = 0u;
    br_part[0].revision += 2u; ticks(5);
    printf("per-track voice mode ok\n");
    printf("poly UI host test passed (%s)\n", BR_PRODUCT_NAME);
    return 0;
}
