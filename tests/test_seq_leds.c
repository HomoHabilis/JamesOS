/* Sequencer LEDs follow the visible bank, track and selected drum lane.
 * FM-1 step grid: black keys off; white keys are steps: on BRIGHT, beat starts
 * (1 5 9 13 of the bank) MID, other steps DIM, steps past the length OFF.
 * SMK-37 Pro (its own scheme, unchanged by the FM-1 grid work): white keys (the steps the pads
 * stand for) on BRIGHT, otherwise OFF; black keys light the selected kit lane (kit) or the held
 * keybed keys (tonal). Black keys are lane keys (kit) and set the insertion pitch (tonal).
 * The SMK-37 pad RGB lights (smk37_panel.c br_smk_pad_lights) are device-only: the host
 * checks the key-LED scheme br_leds() computes. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
static uint32_t white(uint32_t step) {
    for(uint32_t k=0;k<27;k++) if(!br_black_key(k) && br_key_place(k)==step) return k;
    assert(0); return 0;
}
static uint32_t led(uint32_t k) { return host_led_level_state[KEY_LED(k)]; }
#if BR_PRODUCT_SMK37
#define SMK 1
#else
#define SMK 0
#endif
/* The level an off step shows (FM-1: beat starts MID, others DIM; SMK-37: OFF). */
static uint32_t off_level(uint32_t place) {
    if (SMK) return FM1_LED_OFF;
    return (place%4u)==0u ? FM1_LED_MID : FM1_LED_DIM;
}
/* Expected step-grid level for every key of the visible bank. */
static void expect_grid(uint32_t part, uint32_t lane) {
    uint32_t kit=part==BR_DRUM_PART;
    br_leds();
    for(uint32_t k=0;k<27;k++) {
        if(br_black_key(k)) {
            uint32_t want_black=FM1_LED_OFF;
            if(SMK && kit && br_key_place(k)<BR_DRUM_LANES && br_key_place(k)==lane) want_black=FM1_LED_BRIGHT;
            if(SMK && !kit && ((fm1_in.notes>>k)&1u)) want_black=FM1_LED_BRIGHT;
            assert(led(k)==want_black); continue;
        }
        uint32_t s=br_ui.step_bank*BR_BANK_STEPS+br_key_place(k), want;
        uint32_t on=s<br_part[part].length && (kit ? (BR_STEP_HIT(br_part[part].step[s])>>lane)&1u :
                                               (br_part[part].step[s]&BR_STEP_ON)!=0u);
        if(on) want=FM1_LED_BRIGHT;
        else if(s>=br_part[part].length) want=FM1_LED_OFF;
        else want=off_level(br_key_place(k));
        assert(led(k)==want);
    }
}
static uint32_t count_steps(uint32_t part) {
    uint32_t n=0; for(uint32_t i=0;i<BR_STEPS;i++) n+=br_part[part].step[i]!=0u; return n;
}
int main(void) {
    br_app_init(); br_load_preset(0,BR_FIRST_PLAITS);
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0; br_ui.part=0;
    br_part[0].length=64u;
    for(uint32_t i=0;i<BR_STEPS;i++) br_part[0].step[i]=0;
    /* Empty bank: 1 5 9 13 MID, the rest DIM, black keys off. */
    br_ui.step_bank=0; expect_grid(0,0);
    assert(led(white(0))==off_level(0) && led(white(4))==off_level(4) && led(white(8))==off_level(8) &&
           led(white(12))==off_level(12) && led(white(1))==off_level(1) && led(white(15))==off_level(15));
    assert(SMK || (off_level(0)==FM1_LED_MID && off_level(1)==FM1_LED_DIM));
    br_part[0].step[1]=BR_NOTE_STEP(60,100);
    br_part[0].step[4]=BR_NOTE_STEP(61,100);
    br_part[0].step[18]=BR_NOTE_STEP(62,100);
    br_part[0].step[40]=BR_NOTE_STEP(63,100);
    br_part[0].step[63]=BR_NOTE_STEP(64,100);
    br_ui.step_bank=0; expect_grid(0,0);
    assert(led(white(1))==FM1_LED_BRIGHT && led(white(4))==FM1_LED_BRIGHT && led(white(2))==off_level(2));
    br_ui.step_bank=1; expect_grid(0,0);
    assert(led(white(1))==off_level(1) && led(white(2))==FM1_LED_BRIGHT && led(white(0))==off_level(0));
    br_ui.step_bank=2; expect_grid(0,0); assert(led(white(8))==FM1_LED_BRIGHT && led(white(12))==off_level(12));
    br_ui.step_bank=3; expect_grid(0,0); assert(led(white(15))==FM1_LED_BRIGHT && led(white(4))==off_level(4));
    /* Past the pattern length: off, as before. */
    br_ui.step_bank=1; br_part[0].length=18; expect_grid(0,0);
    assert(led(white(2))==FM1_LED_OFF && led(white(1))==off_level(1) && led(white(0))==off_level(0));
    br_part[0].length=64;
    /* No key playhead on the step grid (as before): playing leaves the levels alone. */
    br_ui.step_bank=0; br_playing=1u;
    for(uint32_t s=0;s<16;s++) { br_step_pos[0]=s; expect_grid(0,0); }
    br_playing=0u; br_step_pos[0]=0;
    /* A held step keeps its level (the screen shows the edit). */
    br_ui.step_press_key=white(4); br_ui.step_press_part=0; br_ui.step_press_step=4;
    br_ui.step_edit_active=1u; expect_grid(0,0);
    br_ui.step_press_key=BR_STEP_NO_PRESS; br_ui.step_edit_active=0u;
#if SMK
    /* SMK-37 tonal: a keybed key sets the insertion pitch (53 + key + 12 x octave), no step; its LED
     * shows while it is held. */
    {
        uint32_t steps=count_steps(0);
        for(uint32_t k=0;k<27;k++) if(br_black_key(k)) {
            br_key_down(k,0); br_key_up(k);
            assert(br_ui.seq_default[0][0].note==53u+k+12u*(uint32_t)br_ui.octave && br_ui.note[0]==53u+k+12u*(uint32_t)br_ui.octave);
        }
        assert(steps==count_steps(0));
        fm1_in.notes=1u<<5; expect_grid(0,0); assert(led(5)==FM1_LED_BRIGHT && led(3)==FM1_LED_OFF); fm1_in.notes=0;
    }
#else
    /* Tonal black keys are inert: no step, insertion pitch or note change. */
    {
        uint32_t def=br_ui.seq_default[0][0].note, note=br_ui.note[0], steps=count_steps(0);
        for(uint32_t k=0;k<27;k++) if(br_black_key(k)) { br_key_down(k,0); br_key_up(k); }
        assert(def==br_ui.seq_default[0][0].note && note==br_ui.note[0] && steps==count_steps(0));
    }
#endif
    br_ui.part=BR_DRUM_PART; br_load_preset(BR_DRUM_PART,BR_KIT_PLAITS);
    br_part[BR_DRUM_PART].length=64; br_ui.step_bank=0; br_ui.lane=0;
    for(uint32_t i=0;i<BR_STEPS;i++) br_part[BR_DRUM_PART].step[i]=0;
    expect_grid(BR_DRUM_PART,0);
    br_part[BR_DRUM_PART].step[0]=1u;
    br_part[BR_DRUM_PART].step[1]=2u;
    br_part[BR_DRUM_PART].step[37]=2u;
    expect_grid(BR_DRUM_PART,0);
    assert(led(white(0))==FM1_LED_BRIGHT && led(white(1))==off_level(1));
    {
        uint32_t filter=br_master.fx.filter;
        host_encoder[ENC_SELECT]=1; br_app_tick();
        assert(br_ui.lane==1u && br_master.fx.filter==filter);
        expect_grid(BR_DRUM_PART,1);
        assert(led(white(0))==off_level(0) && led(white(1))==FM1_LED_BRIGHT);
        br_ui.step_bank=2; expect_grid(BR_DRUM_PART,1); assert(led(white(5))==FM1_LED_BRIGHT);
        br_ui.step_bank=0;
        host_encoder[ENC_SELECT]=20; br_app_tick(); assert(br_ui.lane==BR_DRUM_LANES-1u);
        host_encoder[ENC_SELECT]=-20; br_app_tick(); assert(br_ui.lane==0u);
        br_ui.screen[BR_PAGE_SEQ]=1u;
        br_master.fx.filter=64u; host_encoder[ENC_SELECT]=1; br_app_tick();
        assert(br_master.fx.filter==65u && br_ui.lane==0u);
        br_ui.screen[BR_PAGE_SEQ]=0u;
    }
    br_ui.lane=1; expect_grid(BR_DRUM_PART,1);
#if SMK
    /* SMK-37 kit: black keys 1-4 are the lane keys (their LED marks the lane), the others do nothing. */
    {
        uint32_t def=br_ui.seq_default[BR_DRUM_PART][1].note, steps=count_steps(BR_DRUM_PART);
        for(uint32_t k=0;k<27;k++) if(br_black_key(k)) {
            uint32_t want=br_key_place(k)<BR_DRUM_LANES ? br_key_place(k) : br_ui.lane;
            br_key_down(k,0); br_key_up(k); assert(br_ui.lane==want);
            expect_grid(BR_DRUM_PART,br_ui.lane);
        }
        br_ui.lane=1; expect_grid(BR_DRUM_PART,1);
        assert(def==br_ui.seq_default[BR_DRUM_PART][1].note && steps==count_steps(BR_DRUM_PART));
        br_playing=1u; br_step_pos[BR_DRUM_PART]=1u; expect_grid(BR_DRUM_PART,1); br_playing=0u;
    }
#else
    /* Kit black keys are inert: no lane, step or insertion change. */
    {
        uint32_t def=br_ui.seq_default[BR_DRUM_PART][1].note, steps=count_steps(BR_DRUM_PART);
        for(uint32_t k=0;k<27;k++) if(br_black_key(k)) { br_key_down(k,0); br_key_up(k); }
        assert(br_ui.lane==1u && def==br_ui.seq_default[BR_DRUM_PART][1].note && steps==count_steps(BR_DRUM_PART));
        br_playing=1u; br_step_pos[BR_DRUM_PART]=1u; expect_grid(BR_DRUM_PART,1); br_playing=0u;
    }
#endif
    /* RETRIG (ARP on the drum track) keeps its own scheme: the lane key lit, hits MID, others off. */
    br_ui.page=BR_PAGE_PATTERN; br_leds();
    for(uint32_t k=0;k<27;k++) {
        uint32_t place=br_key_place(k);
        if(br_black_key(k)) assert(led(k)==(place==1u ? FM1_LED_BRIGHT : FM1_LED_OFF));
        else assert(led(k)==(place==1u ? FM1_LED_MID : FM1_LED_OFF));
    }
    br_ui.page=BR_PAGE_SEQ;
    br_leds_mode(1); for(uint32_t k=0;k<27;k++) assert(host_led_level_state[KEY_LED(k)]==FM1_LED_OFF);
    br_ui.screen[BR_PAGE_SEQ]=1; br_leds();
    for(uint32_t k=0;k<27;k++) assert(host_led_level_state[KEY_LED(k)]==FM1_LED_OFF);
#if SMK
    puts("PASS: SMK-37 sequencer key LEDs (steps on BRIGHT, others OFF; lane key / held keybed key lit), "
         "keybed lane keys and insertion pitch, bank, track, drum lane, RETRIG and SAVE precedence");
#else
    puts("PASS: sequencer key LEDs (black off; on BRIGHT, beats MID, rest DIM, past length OFF), "
         "inert black keys, bank, track, drum lane, RETRIG and SAVE precedence");
#endif
}
