/* Real key/knob input: only held, active drum steps accept retrigger edits. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
static void tick(void){++fm1_ms;br_app_tick();}
static void press(uint32_t k){fm1_in.notes|=1u<<k;host_note_pressed|=1u<<k;tick();}
static void release(uint32_t k){fm1_in.notes&=~(1u<<k);tick();}
static void turn(uint32_t k,int32_t n){host_encoder[ENC_KNOB1+k]+=n;tick();}
int main(void){
 br_app_init();fm1_ms=3000; br_ui.part=BR_DRUM_PART;br_ui.page=BR_PAGE_PATTERN;
 br_part[BR_DRUM_PART].length=BR_STEPS;
 for(uint32_t i=0;i<BR_STEPS;i++)br_part[BR_DRUM_PART].step[i]=15u;
 memset((void*)br_seq_kit_retrig,0,sizeof br_seq_kit_retrig);tick();
 assert(!strcmp(br_header_title(),"RETRIGGER") && !br_page_plays_keys());
 assert(host_led_level_state[KEY_LED(0)]==FM1_LED_MID);
 static const uint32_t lane_keys[4]={1,3,5,8};
 for(uint32_t bank=0;bank<BR_BANKS;bank++)for(uint32_t lane=0;lane<BR_DRUM_LANES;lane++){
  br_ui.step_bank=bank; press(lane_keys[lane]);release(lane_keys[lane]);assert(br_ui.lane==lane);
  uint32_t step=bank*BR_BANK_STEPS;
  turn(0,1);assert(br_seq_kit_retrig[step][lane]==0u);
  press(0);assert(br_retrig_editing() && br_ui.retrig_step==step && !br_ui.held_note[0]);
  assert(host_led_level_state[KEY_LED(0)]==FM1_LED_BRIGHT);
  turn(0,3);turn(1,2);assert(br_seq_kit_retrig[step][lane]==19u);
  { char text[32];br_control_t c=br_control(0);br_control_text(0,&c,text);assert(!strcmp(text,"3")); }
  turn(2,7);turn(3,7);assert(br_seq_kit_retrig[step][lane]==19u);
  br_buttons(1u<<BTN_OCT_UP);assert(br_ui.step_bank==bank);
  release(0);assert(!br_retrig_editing());turn(0,-1);assert(br_seq_kit_retrig[step][lane]==19u);
 }
 br_ui.step_bank=0;br_ui.lane=0;br_part[BR_DRUM_PART].step[1]=0;
 press(2);turn(0,2);release(2);assert(!br_seq_kit_retrig[1][0] && !br_ui.held_note[2]);
 press(0);br_buttons(1u<<BTN_HOME);tick();assert(!br_retrig_editing());
 br_buttons(1u<<BTN_ARP);tick();turn(0,1);assert(br_seq_kit_retrig[0][0]==19u);release(0);
 br_ui.step_bank=0;br_buttons(1u<<BTN_OCT_UP);assert(br_ui.step_bank==1);
 br_buttons(1u<<BTN_OCT_DOWN);assert(!br_ui.step_bank);
 press(25);assert(br_retrig_page() && !br_punch_ks.shift);release(25);
 uint32_t arp=br_arp_on;br_arp_toggle();assert(br_arp_on==arp && br_retrig_page());
 br_step_trigger_clear(BR_DRUM_PART,0,0);assert(!br_seq_kit_retrig[0][0]);
 puts("PASS: held-step retrigger UI, four lanes/banks, inert empty keys and release/navigation locks");return 0;
}
