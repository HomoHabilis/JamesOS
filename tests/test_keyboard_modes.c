/* Exercise physical-key gating, releases and firmware LED output on the host. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void tick(void) { ++fm1_ms; br_app_tick(); }
static void press_key(uint32_t key) {
    fm1_in.notes |= 1u << key; host_note_pressed |= 1u << key; tick();
}
static void release_key(uint32_t key) { fm1_in.notes &= ~(1u << key); tick(); }
static void tap(uint32_t button) {
    fm1_in.buttons |= 1u << button; host_pressed |= 1u << button; tick();
    fm1_in.buttons &= ~(1u << button); host_released |= 1u << button; tick();
}
int main(void) {
    br_app_init(); fm1_ms = 2000; tick();
    assert(br_ui.page == BR_PAGE_HOME);
    press_key(0); assert(br_ui.held_note[0]);
    /* The keyboard and octave buttons work on ENV, LFO and GLO as well, lights included. (EDIT on
     * the drum track opens KIT LEVEL, the drum mixer, where the keys never play: checked below.) */
    {
        static const struct { uint32_t button, page; } pages[] = {
            {BTN_ENV, BR_PAGE_ENV}, {BTN_LFO, BR_PAGE_LFO}, {BTN_GLO, BR_PAGE_MIX}
#if BR_PRODUCT_SMK37
            , {BTN_EDIT, BR_PAGE_ROUTE}      /* SMK-37: EDIT opens PATCH 1 (no KIT LEVEL), where keys play */
#endif
        };
        for (uint32_t i = 0; i < sizeof pages / sizeof pages[0]; ++i) {
            tap(pages[i].button); assert(br_ui.page == pages[i].page);
            assert(br_ui.held_note[0]);                           /* a key held across the change keeps sounding */
            release_key(0); assert(!br_ui.held_note[0]);
            press_key(2); assert(br_ui.held_note[2]); release_key(2);
            for (uint32_t k = 0; k < 27; ++k) assert(host_led_level_state[KEY_LED(k)] != FM1_LED_OFF);
            assert(host_led_level_state[BTN_OCT_DOWN] != FM1_LED_OFF && host_led_level_state[BTN_OCT_UP] != FM1_LED_OFF);
            press_key(0);
        }
        /* KIT LEVEL is the drum mixer, where the keys never play (FM-1: EDIT; SMK-37: FADER BANK). */
        release_key(0); br_ui.part = BR_DRUM_PART;
#if BR_PRODUCT_SMK37
        br_smk_fader_bank_select(1u);
        assert(br_ui.page == BR_PAGE_ROUTE && br_gate_page() && !br_page_plays_keys());
#else
        tap(BTN_EDIT); assert(br_ui.page == BR_PAGE_ROUTE && br_gate_page() && !br_page_plays_keys());
#endif
        press_key(2); assert(!br_ui.held_note[2]); release_key(2);
        br_ui.part = 0u; tap(BTN_HOME); press_key(0);
        tap(BTN_LFO); br_ui.octave = 0; tap(BTN_OCT_UP); assert(br_ui.octave == 1);
        tap(BTN_OCT_DOWN); tap(BTN_OCT_DOWN); assert(br_ui.octave == -1);
        br_ui.octave = 0; release_key(0);
    }
    tap(BTN_ARP); assert(br_ui.page == BR_PAGE_PATTERN);
    press_key(2); assert(br_ui.held_note[2]); release_key(2);
    tap(BTN_SEQ); assert(br_ui.page == BR_PAGE_SEQ);
    press_key(1);
#if BR_PRODUCT_SMK37
    assert(br_ui.held_note[1]); /* the SMK keyboard can audition on the sequencer */
#else
    assert(!br_ui.held_note[1]);
#endif
    release_key(1);
    tap(BTN_HOME); press_key(0); assert(br_ui.held_note[0]); release_key(0);
    assert(host_led_level_state[KEY_LED(0)] != FM1_LED_OFF);
    /* A tap navigates; a hold toggles once at 800ms without navigating. */
    br_ui.page = BR_PAGE_ENV;
    fm1_in.buttons |= 1u << BTN_ARP; host_pressed |= 1u << BTN_ARP; tick();
    uint32_t started = br_ui.mode_start[1], old_arp = br_arp_on;
    fm1_ms = started + 799u; br_app_tick(); assert(br_arp_on == old_arp);
    fm1_ms = started + 800u; br_app_tick(); assert(br_arp_on != old_arp && br_ui.page == BR_PAGE_ENV);
    fm1_ms = started + 1600u; br_app_tick(); assert(br_arp_on != old_arp);
    fm1_in.buttons &= ~(1u << BTN_ARP); host_released |= 1u << BTN_ARP; tick();
    assert(br_ui.page == BR_PAGE_ENV);
    tap(BTN_ARP); assert(br_ui.page == BR_PAGE_PATTERN && br_ui.screen[BR_PAGE_PATTERN] == 0u);
    tap(BTN_ARP); assert(br_ui.screen[BR_PAGE_PATTERN] == 0u);
    for (uint32_t i = 0u; i < 5u; ++i) {
        br_part[0].arp_mode = i; br_part[0].arp_rate = i;
        br_control_t mode = br_control(0u), rate = br_control(1u);
        assert(br_control_widget(0u, &mode) == BR_WIDGET_ARP_MODE);
        assert(br_control_widget(1u, &rate) == BR_WIDGET_ARP_RATE);
        assert(br_control_widget_variant(0u, &mode, BR_WIDGET_ARP_MODE) == i);
        assert(br_control_widget_variant(1u, &rate, BR_WIDGET_ARP_RATE) == i);
    }
    br_ui.page = BR_PAGE_ENV;
    /* HOLD: the HOME button on the FM-1, the printed PATCH (logical SEL) on the SMK-37. */
    fm1_in.buttons |= 1u << BR_HOLD_BUTTON; host_pressed |= 1u << BR_HOLD_BUTTON; tick();
    started = br_ui.mode_start[0]; uint32_t old_hold = br_hold_on;
    fm1_ms = started + 799u; br_app_tick(); assert(br_hold_on == old_hold);
    fm1_ms = started + 800u; br_app_tick(); assert(br_hold_on != old_hold && br_ui.page == BR_PAGE_ENV);
    fm1_in.buttons &= ~(1u << BR_HOLD_BUTTON); host_released |= 1u << BR_HOLD_BUTTON; tick();
    assert(br_ui.page == BR_PAGE_ENV);
    assert(!strcmp(br_ui.message, "HOLD ON") || !strcmp(br_ui.message, "HOLD OFF"));
#if BR_PRODUCT_SMK37
    {   /* PATCH: a tap acts on release (SOUNDS list); a hold toggles HOLD and does not open the list. */
        br_hold_on = 0u; br_ui.part = 0u; br_ui.page = BR_PAGE_ENV;
        fm1_in.buttons |= 1u << BTN_SEL; host_pressed |= 1u << BTN_SEL; tick();
        assert(br_ui.page == BR_PAGE_ENV && !br_sel_browse());              /* not on press */
        fm1_in.buttons &= ~(1u << BTN_SEL); host_released |= 1u << BTN_SEL; tick();
        assert(br_sel_browse() && !br_hold_on);                              /* on release */
        br_ui.page = BR_PAGE_ENV;
        fm1_in.buttons |= 1u << BTN_SEL; host_pressed |= 1u << BTN_SEL; tick();
        started = br_ui.mode_start[0];
        fm1_ms = started + 799u; br_app_tick(); assert(!br_hold_on);
        fm1_ms = started + 800u; br_app_tick(); assert(br_hold_on);
        fm1_in.buttons &= ~(1u << BTN_SEL); host_released |= 1u << BTN_SEL; tick();
        assert(br_ui.page == BR_PAGE_ENV && !br_sel_browse() && br_hold_on);
        { uint32_t on_blink = 0u, off_blink = 0u;                            /* PATCH's LED blinks while HOLD is on */
          for (uint32_t t = 0u; t < 600u; ++t) { fm1_ms += 1u; br_leds_mode(0u);
              if (host_led_level_state[BTN_SEL] == FM1_LED_BRIGHT) ++on_blink; else ++off_blink; }
          assert(on_blink && off_blink); }
        br_hold_on = 0u;
        /* KNOB BANK (logical HOME): next category on the synth SOUNDS list; HOME everywhere else. */
        tap(BTN_SEL); assert(br_sel_browse());
        br_sel.cursor[0] = 0u; br_sel_move(0);
        { const char *before = br_bank_category(br_sel.cursor[0]);
          tap(BTN_HOME);
          assert(br_sel_browse() && br_bank_category(br_sel.cursor[0]) != before && !strcmp(br_ui.message, br_bank_category(br_sel.cursor[0]))); }
        tap(BTN_ENV); assert(br_ui.page == BR_PAGE_ENV && !br_sel.active);   /* any page button leaves the list */
        tap(BTN_HOME); assert(br_ui.page == BR_PAGE_HOME);
        tap(BTN_SEL); assert(br_sel_browse());
        tap(BTN_SEL); assert(br_sel_browse() && br_sel.loaded);              /* second tap loads */
        tap(BTN_HOME); assert(br_sel_browse());                              /* loaded: KNOB BANK still steps categories */
        tap(BTN_LFO); assert(br_ui.page == BR_PAGE_LFO);                     /* ...and any page button is the way out */
        tap(BTN_SEL); tap(BTN_SEL); assert(br_sel.loaded);
        tap(BTN_SEL); assert(br_ui.page == BR_PAGE_HOME);                    /* loaded: PATCH goes HOME */
        tap(BTN_FX); assert(br_ui.page == BR_PAGE_FX);
        br_ui.part = BR_DRUM_PART; tap(BTN_SEL); assert(br_sel_browse());
        tap(BTN_HOME); assert(br_ui.page == BR_PAGE_HOME);                   /* drum kits: no categories */
        br_ui.part = 0u; br_ui.page = BR_PAGE_ENV;
    }
#endif
    /* All drum-mixer piano keys are inert, including old lane/level shortcuts. */
    br_ui.part = BR_DRUM_PART; br_load_preset(BR_DRUM_PART, BR_KIT_808);
    br_ui.page = BR_PAGE_ROUTE; br_ui.screen[BR_PAGE_ROUTE] = 0u;
    br_ui.lane = 2u;
    uint32_t levels[BR_DRUM_LANES];
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
        levels[lane] = br_part[BR_DRUM_PART].lane_level[lane];
    for (uint32_t key = 0u; key < 27u; ++key) {
        press_key(key); release_key(key);
        assert(br_ui.lane == 2u && !br_ui.held_note[key]);
        for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
            assert(br_part[BR_DRUM_PART].lane_level[lane] == levels[lane]);
    }
    uint32_t bank = br_ui.gate_bank;
    tap(BTN_OCT_DOWN); tap(BTN_OCT_UP); assert(br_ui.gate_bank == bank);
    for (uint32_t playing = 0u; playing < 2u; ++playing) {
        br_playing = playing;
        for (uint32_t mode = 0u; mode <= 6u; ++mode) {
            br_leds_mode(mode);
            assert(host_led_level_state[BTN_PLAY] == (playing ? FM1_LED_MID : FM1_LED_OFF));
            assert(host_led_level_state[FM1_LED_PLAY_GREEN] == (playing ? FM1_LED_OFF : FM1_LED_MID));
        }
    }
    uint8_t column, row;
    assert(fm1_led_position(FM1_LED_PLAY_GREEN, &column, &row) && column == 8u && row == 1u);
    assert(FM1_LED_KEYMAP[1][8] == -1);
    puts("PASS: keyboard modes, held-note release and LED levels");
    return 0;
}
