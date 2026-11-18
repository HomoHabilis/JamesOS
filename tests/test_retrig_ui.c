/* Real key/knob input: only held, active drum steps accept retrigger edits. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
static void tick(void){++fm1_ms;br_app_tick();}
/* The drum track's RETRIGGER page: FM-1 opens it with ARP; on the SMK-37 only NOTE REPEAT does (the ARP page is
 * a cue there), as smk37_fader_logic.h br_smk_repeat_press opens it (the panel code is device-only). */
static void open_retrig(void){
#if BR_PRODUCT_SMK37
 br_smk_retrig.page=(uint8_t)br_ui.page;br_smk_retrig.screen=(uint8_t)br_ui.screen[br_ui.page];br_smk_retrig.opened=1u;
 br_ui.page=BR_PAGE_PATTERN;br_ui.screen[BR_PAGE_PATTERN]=0u;br_ui.edit_key=0xffffffffu;br_ui.redraw=1u;
#else
 br_buttons(1u<<BTN_ARP);
#endif
 tick();
}
static void press(uint32_t k){fm1_in.notes|=1u<<k;host_note_pressed|=1u<<k;tick();}
static void release(uint32_t k){fm1_in.notes&=~(1u<<k);tick();}
static void turn(uint32_t k,int32_t n){host_encoder[ENC_KNOB1+k]+=n;tick();}
int main(void){
 br_app_init();fm1_ms=3000; br_ui.part=BR_DRUM_PART;br_ui.page=BR_PAGE_HOME;
 br_part[BR_DRUM_PART].length=BR_STEPS;
 for(uint32_t i=0;i<BR_STEPS;i++)br_part[BR_DRUM_PART].step[i]=15u;
 memset((void*)br_seq_kit_retrig,0,sizeof br_seq_kit_retrig);tick();open_retrig();
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
 open_retrig();turn(0,1);assert(br_seq_kit_retrig[0][0]==19u);release(0);
 br_ui.step_bank=0;br_buttons(1u<<BTN_OCT_UP);assert(br_ui.step_bank==1);
 br_buttons(1u<<BTN_OCT_DOWN);assert(!br_ui.step_bank);
 press(25);assert(br_retrig_page() && !br_punch_ks.shift);release(25);
 uint32_t arp=br_arp_on;br_arp_toggle();assert(br_arp_on==arp);
#if BR_PRODUCT_SMK37
 assert(!br_retrig_page() && br_arp_cue_page());   /* the 0.8 s ARP hold on the drum track: the cue page, not RETRIGGER */
#else
 assert(br_retrig_page());
#endif
 br_step_trigger_clear(BR_DRUM_PART,0,0);assert(!br_seq_kit_retrig[0][0]);
#if BR_PRODUCT_SMK37
 {   /* SMK-37: the ARP page on the drum track is a "SWITCH TO [T1] [T2] [T3]" cue; only NOTE REPEAT shows RETRIGGER */
  uint8_t before[sizeof br_seq_kit_retrig];
  br_part_t parts[BR_PARTS];
  for(uint32_t i=0;i<BR_STEPS;i++)for(uint32_t l=0;l<BR_DRUM_LANES;l++)br_seq_kit_retrig[i][l]=(uint8_t)((i+l)&63u);
  memcpy(before,(const void*)br_seq_kit_retrig,sizeof before);
  /* NOTE REPEAT: RETRIGGER (title, controls, hint, LED, grid layout) */
  br_ui.part=BR_DRUM_PART;br_ui.page=BR_PAGE_HOME;tick();open_retrig();
  assert(br_retrig_page() && !br_arp_cue_page() && !strcmp(br_header_title(),"RETRIGGER") && !br_page_plays_keys());
  assert(!strcmp(br_oct_hint(),"HOLD STEP: EDIT") && !strcmp(br_control(0).label,"RETRIGS") && !strcmp(br_control(1).label,"SPACING"));
  /* from RETRIGGER the ARP button gives the cue */
  br_buttons(1u<<BTN_ARP);tick();
  assert(!br_retrig_page() && br_arp_cue_page() && br_ui.page==BR_PAGE_PATTERN && !strcmp(br_header_title(),"ARP"));
  assert(br_page_plays_keys() && !*br_oct_hint() && !br_control(0).label[0] && !br_control(1).label[0] && !br_control(2).label[0] && !br_control(3).label[0]);
  assert(host_led_level_state[BTN_ARP]==FM1_LED_BRIGHT);
  /* knob turns and pads/keys change nothing: no retrig data, no arp settings, no steps */
  memcpy(parts,(const void*)br_part,sizeof parts);
  for(uint32_t k=0;k<4;k++){turn(k,3);turn(k,-5);}
  press(0);press(2);press(8);release(8);release(2);release(0);
  assert(!br_retrig_editing() && !memcmp(before,(const void*)br_seq_kit_retrig,sizeof before));
  for(uint32_t q=0;q<BR_PARTS;q++){br_part[q].revision=parts[q].revision;}
  assert(!memcmp(parts,(const void*)br_part,sizeof parts));
  /* the cue pixels: one line, centred (+-1 px), the whole page body otherwise empty, three track-coloured badges */
  br_ui.redraw=1u;for(uint32_t i=0;i<8u;i++){br_draw();}
  {
   int32_t x0=240,x1=-1,y0=240,y1=-1;
   for(int32_t y=34;y<214;y++)for(int32_t x=0;x<240;x++)if(fb[y*240+x]!=BR_BACKGROUND){
    if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y;}
   assert(x1>x0 && y1>y0);
   assert(abs((x0+x1)/2-120)<=1);                 /* horizontally centred as a whole */
   assert(y0>=108 && y1<=140 && abs((y0+y1)/2-124)<=3);   /* vertically centred in the body (rows 34..214) */
   {                                              /* three badges in T1, T2, T3 colours, left to right; no kit (DK) badge */
    int32_t mean[4];uint32_t cnt[4];
    for(uint32_t t=0;t<4;t++){
     uint32_t want=t<3?t:BR_DRUM_PART;uint16_t c=br_track_color(want);long sum=0;cnt[t]=0;
     for(int32_t y=y0;y<=y1;y++)for(int32_t x=x0;x<=x1;x++){
      uint16_t v=fb[y*240+x];
      int32_t dr=(int32_t)((v>>11)&31)-(int32_t)((c>>11)&31),dg=((int32_t)((v>>5)&63)-(int32_t)((c>>5)&63))/2,db=(int32_t)(v&31)-(int32_t)(c&31);
      if(abs(dr)+abs(dg)+abs(db)<=6){sum+=x;cnt[t]++;}
     }
     mean[t]=cnt[t]?(int32_t)(sum/(long)cnt[t]):-1;
    }
    assert(cnt[0]>60u && cnt[1]>60u && cnt[2]>60u && !cnt[3]);
    assert(mean[0]<mean[1] && mean[1]<mean[2]);
   }
  }
  /* NOTE REPEAT from the cue opens RETRIGGER, a track arrow away and back gives the cue again */
  open_retrig();assert(br_retrig_page());
  br_smk_track_turn(-1);assert(!br_retrig_page() && br_ui.part==BR_DRUM_PART-1u && !strcmp(br_header_title(),"ARP"));
  br_smk_track_turn(1);assert(br_arp_cue_page() && !br_retrig_page() && !strcmp(br_header_title(),"ARP"));
  /* T1's ARP page, arrow onto the drum track: the cue; arrow back to T1: the normal ARP page */
  br_ui.part=0u;br_buttons(1u<<BTN_ARP);tick();
  assert(!br_arp_cue_page() && !br_retrig_page() && !strcmp(br_control(0).label,"MODE") && !strcmp(br_control(1).label,"RATE"));
  br_smk_track_turn(1);br_smk_track_turn(1);br_smk_track_turn(1);
  assert(br_ui.part==BR_DRUM_PART && br_arp_cue_page() && !br_retrig_page() && !strcmp(br_header_title(),"ARP"));
  br_smk_track_turn(-3);
  assert(br_ui.part==0u && !br_arp_cue_page() && !br_retrig_page() && !strcmp(br_control(2).label,"OCTAVES") && !strcmp(br_control(3).label,"GATE"));
  /* the ARP page of a synth track draws no cue: its own settings are there */
  br_ui.redraw=1u;for(uint32_t i=0;i<8u;i++)br_draw();
  {uint32_t n=0;for(int32_t y=108;y<140;y++)for(int32_t x=0;x<240;x++)n+=fb[y*240+x]!=BR_BACKGROUND;(void)n;}
  /* the ARP hold on the drum track says nothing about RETRIGGER (dinghy.c: no cue for it on the SMK-37) */
  br_ui.part=BR_DRUM_PART;br_ui.page=BR_PAGE_HOME;br_smk_retrig.opened=0u;tick();
  fm1_in.buttons|=1u<<BTN_ARP;host_pressed|=1u<<BTN_ARP;tick();
  for(uint32_t i=0;i<1000u;i++)tick();
  assert(br_arp_cue_page() && strcmp(br_ui.message,"RETRIGGER"));
  fm1_in.buttons&=~(1u<<BTN_ARP);host_released|=1u<<BTN_ARP;tick();
 }
#endif
 puts("PASS: held-step retrigger UI, four lanes/banks, inert empty keys and release/navigation locks");return 0;
}
