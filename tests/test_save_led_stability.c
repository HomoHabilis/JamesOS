/* Catch transient LED writes that a device scan can observe between updates. */
#define DINGHY_HOST 1
#define DINGHY_EMULATOR_SAVE 1
#define LED_WRITE_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

int main(void)
{
    br_app_init();fm1_ms=2000;
    for(uint32_t page=0;page<BR_PAGE_COUNT;page++) {
        br_ui.page=page;br_ui.screen[page]=0;br_leds();
        for(int mode=1;mode<=6;mode++) {
            uint32_t buttons=0,released=0;es.mode=mode;
            assert(es_input(&buttons,&released,0));
            assert(host_led_level_state[BTN_HOME]==FM1_LED_OFF);
            assert(host_led_level_state[BTN_SAVE]==FM1_LED_BRIGHT);
            assert(host_led_level_state[BTN_OCT_DOWN]==(mode==1?FM1_LED_OFF:FM1_LED_BRIGHT));
            assert(host_led_level_state[BTN_OCT_UP]==FM1_LED_BRIGHT);
            assert(host_led_level_state[BTN_PLAY]==FM1_LED_OFF);
            assert(host_led_level_state[FM1_LED_PLAY_GREEN]==FM1_LED_MID);
            for(uint32_t k=0;k<27;k++)assert(host_led_level_state[KEY_LED(k)]==FM1_LED_OFF);
            memset(host_led_changes,0,sizeof host_led_changes);
            for(unsigned i=0;i<10;i++){++fm1_ms;assert(es_input(&buttons,&released,0));}
            for(uint32_t id=0;id<FM1_LED_COUNT;id++)assert(host_led_changes[id]==0);
        }
    }
    puts("PASS: SAVE LEDs have no transient changes during idle updates");
}
