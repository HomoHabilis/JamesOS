#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static void tick(void) { ++fm1_ms; br_app_tick(); }
static void tap(unsigned b) { fm1_in.buttons|=1u<<b;host_pressed|=1u<<b;tick();fm1_in.buttons&=~(1u<<b);host_released|=1u<<b;tick(); }
static void page(unsigned p,unsigned s,const char *title) {
 if(br_ui.page!=p || br_ui.screen[p]!=s) fprintf(stderr,"expected %u/%u, got %u/%u, part %u\n",p,s,br_ui.page,br_ui.screen[br_ui.page],br_ui.part);
 assert(br_ui.page==p && br_ui.screen[p]==s);assert(!strcmp(br_header_title(),title));
 br_leds();assert(host_led_level_state[BTN_LFO]==FM1_LED_BRIGHT);assert(host_led_level_state[BTN_EDIT]!=FM1_LED_BRIGHT);
}
static void badge_raster(unsigned page_id, unsigned screen, unsigned bit) {
 static uint16_t actual[240*20];
 br_ui.page=page_id; br_ui.screen[page_id]=screen;
 br_draw_page_route_badge(1,0); memcpy(actual,fb+46*240,sizeof actual);
 cv_begin(240,20,BR_BACKGROUND);
 br_draw_named_badge_scaled(120,1,br_route_badge_name[bit],br_route_badge_tint(bit,0,BR_BACKGROUND),2);
 cv_blit(0,46); assert(!memcmp(actual,fb+46*240,sizeof actual));
}
int main(void) {
 br_app_init();fm1_ms=2000; br_ui.part=0; br_ui.page=BR_PAGE_HOME;
 /* Compare the complete actual badge strip with the punch renderer geometry. */
 badge_raster(BR_PAGE_LFO,0,0); badge_raster(BR_PAGE_LFO,1,1);
 badge_raster(BR_PAGE_ROUTE,0,2); badge_raster(BR_PAGE_ROUTE,1,3);
 br_ui.page=BR_PAGE_HOME;

 tap(BTN_LFO);page(BR_PAGE_LFO,0,"LFO 1");
 tap(BTN_LFO);page(BR_PAGE_LFO,1,"LFO 2");
 tap(BTN_LFO);page(BR_PAGE_ROUTE,0,"PATCH 1");
 tap(BTN_LFO);page(BR_PAGE_ROUTE,1,"PATCH 2");
 tap(BTN_LFO);page(BR_PAGE_ROUTE,2,"LFO DEPTHS");
 tap(BTN_LFO);page(BR_PAGE_LFO,0,"LFO 1");
 /* The internal page cycle handles reverse and multi-step navigation. */
 br_select_screen(-1);page(BR_PAGE_ROUTE,2,"LFO DEPTHS");
 br_select_screen(1);page(BR_PAGE_LFO,0,"LFO 1");
 br_select_screen(3);page(BR_PAGE_ROUTE,1,"PATCH 2");
 /* SELECT retains its global filter behavior. */
 { uint32_t old=br_master.fx.filter;host_encoder[ENC_SELECT]++;tick();
   assert(br_ui.page==BR_PAGE_ROUTE && br_master.fx.filter>old); }
 /* PATCH controls still edit the selected synth and remain isolated. */
 {uint32_t other=br_part[1].mod.route[1].enabled;
  br_part[0].mod.route[1].enabled=0;
  host_encoder[ENC_KNOB1+3u]++;tick();
  assert(br_part[0].mod.route[1].enabled==1u && br_part[1].mod.route[1].enabled==other);}
 /* EDIT always opens kit level, including from synth and on repeat. */
 tap(BTN_EDIT);assert(br_ui.part==0 && br_gate_page());assert(!strcmp(br_header_title(),"KIT LEVEL"));
 br_leds();assert(host_led_level_state[BTN_EDIT]==FM1_LED_BRIGHT && host_led_level_state[BTN_LFO]!=FM1_LED_BRIGHT);
 tap(BTN_EDIT);assert(br_ui.part==0 && br_gate_page());
 { uint32_t synth=br_part[0].lane_level[2], drum=br_part[BR_DRUM_PART].lane_level[2];
 host_encoder[ENC_KNOB1+2u]++;tick();assert(br_ui.lane==2u);
 assert(br_part[0].lane_level[2]==synth && br_part[BR_DRUM_PART].lane_level[2]>drum); }
 tap(BTN_OCT_DOWN);assert(br_kit_mute_mask & (1u<<2));tap(BTN_OCT_DOWN);assert(!(br_kit_mute_mask & (1u<<2)));
 /* LFO returns to the active synth after EDIT. */
 tap(BTN_LFO);page(BR_PAGE_LFO,0,"LFO 1");
 tap(BTN_EDIT);assert(br_ui.part==0 && br_gate_page());
 /* Each active synth stays selected; preset changes address the drum lane. */
 for (unsigned part=0; part<BR_SYNTH_PARTS; ++part) {
  tap(BTN_HOME);br_ui.part=part;uint32_t synth=br_part[part].sound.preset;
  uint32_t mask=br_part[BR_DRUM_PART].sound.drum_voice_mask;
  tap(BTN_EDIT);assert(br_gate_page() && br_ui.part==part && !br_page_plays_keys());
  host_encoder[ENC_PRESETS]++;tick();
  assert(br_ui.part==part && br_part[part].sound.preset==synth);
  assert(br_part[BR_DRUM_PART].sound.drum_voice_mask!=mask);
  host_encoder[ENC_ALGORITHM]++;tick();assert(br_ui.part==part);
 }
 /* Kit LFO has its original two pages; EDIT returns directly to mixer. */
 tap(BTN_HOME);host_encoder[ENC_ALGORITHM]+=3;tick();assert(br_ui.part==BR_DRUM_PART);
 tap(BTN_LFO);assert(br_ui.page==BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO]==0);
 tap(BTN_LFO);assert(br_ui.page==BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO]==1);
 tap(BTN_LFO);assert(br_ui.page==BR_PAGE_LFO && br_ui.screen[BR_PAGE_LFO]==0);
 tap(BTN_EDIT);assert(br_gate_page());
 /* A held synth key belongs to its original part after EDIT changes focus. */
 for(unsigned arp=0;arp<2;arp++) {
  tap(BTN_HOME);if(br_ui.part==BR_DRUM_PART){host_encoder[ENC_ALGORITHM]-=3;tick();}assert(br_ui.part==0);
  br_arp_on=arp;fm1_in.notes|=1u;host_note_pressed|=1u;tick();
  assert(br_ui.held_note[0] && br_ui.held_part[0]==0u);
  assert(!!br_ui.held_arp[0]==!!arp);
  tap(BTN_EDIT);assert(br_gate_page() && br_ui.part==0);
  fm1_in.notes&=~1u;tick();assert(!br_ui.held_note[0] && !br_ui.held_arp[0]);
 }
 br_arp_on=0;tap(BTN_HOME);if(br_ui.part==BR_DRUM_PART){host_encoder[ENC_ALGORITHM]-=3;tick();}assert(br_ui.part==0);
 tap(BTN_LFO);page(BR_PAGE_LFO,0,"LFO 1");
 puts("LFO/PATCH navigation and dedicated kit mixer passed");
}
