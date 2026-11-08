/* Sequencer LEDs follow the visible bank, track and selected drum lane. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
static uint32_t white(uint32_t step) {
    for(uint32_t k=0;k<27;k++) if(!br_black_key(k) && br_key_place(k)==step) return k;
    assert(0); return 0;
}
int main(void) {
    br_app_init(); br_load_preset(0,BR_FIRST_PLAITS);
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0; br_ui.part=0;
    br_part[0].length=64u;
    for(uint32_t i=0;i<BR_STEPS;i++) br_part[0].step[i]=0;
    br_part[0].step[1]=BR_NOTE_STEP(60,100);
    br_part[0].step[18]=BR_NOTE_STEP(62,100);
    br_ui.step_bank=0; br_leds();
    assert(host_led_level_state[KEY_LED(white(1))]==FM1_LED_BRIGHT);
    assert(host_led_level_state[KEY_LED(white(2))]==FM1_LED_OFF);
    br_ui.step_bank=1; br_leds();
    assert(host_led_level_state[KEY_LED(white(1))]==FM1_LED_OFF);
    assert(host_led_level_state[KEY_LED(white(2))]==FM1_LED_BRIGHT);
    br_part[0].length=18; br_leds();
    assert(host_led_level_state[KEY_LED(white(2))]==FM1_LED_OFF);
    br_ui.part=BR_DRUM_PART; br_load_preset(BR_DRUM_PART,BR_KIT_PLAITS);
    br_part[BR_DRUM_PART].length=64; br_ui.step_bank=0; br_ui.lane=0;
    for(uint32_t i=0;i<BR_STEPS;i++) br_part[BR_DRUM_PART].step[i]=0;
    br_part[BR_DRUM_PART].step[0]=1u;
    br_part[BR_DRUM_PART].step[1]=2u;
    br_leds(); assert(host_led_level_state[KEY_LED(white(0))]==FM1_LED_BRIGHT);
    assert(host_led_level_state[KEY_LED(white(1))]==FM1_LED_OFF);
    {
        uint32_t filter=br_master.fx.filter;
        host_encoder[ENC_SELECT]=1; br_app_tick();
        assert(br_ui.lane==1u && br_master.fx.filter==filter);
        br_leds();
        assert(host_led_level_state[KEY_LED(white(0))]==FM1_LED_OFF);
        assert(host_led_level_state[KEY_LED(white(1))]==FM1_LED_BRIGHT);
        host_encoder[ENC_SELECT]=20; br_app_tick(); assert(br_ui.lane==BR_DRUM_LANES-1u);
        host_encoder[ENC_SELECT]=-20; br_app_tick(); assert(br_ui.lane==0u);
        br_ui.screen[BR_PAGE_SEQ]=1u;
        br_master.fx.filter=64u; host_encoder[ENC_SELECT]=1; br_app_tick();
        assert(br_master.fx.filter==65u && br_ui.lane==0u);
        br_ui.screen[BR_PAGE_SEQ]=0u;
    }
    br_ui.lane=1; br_leds();
    assert(host_led_level_state[KEY_LED(white(0))]==FM1_LED_OFF);
    assert(host_led_level_state[KEY_LED(white(1))]==FM1_LED_BRIGHT);
    br_leds_mode(1); for(uint32_t k=0;k<27;k++) assert(host_led_level_state[KEY_LED(k)]==FM1_LED_OFF);
    br_ui.screen[BR_PAGE_SEQ]=1; br_leds();
    for(uint32_t k=0;k<27;k++) assert(host_led_level_state[KEY_LED(k)]==FM1_LED_OFF);
    puts("PASS: sequencer note LEDs, bank, track, drum lane and SAVE precedence");
}
