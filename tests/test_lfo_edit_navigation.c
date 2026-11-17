#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "host_intro.h"
#include <assert.h>
#include <stdio.h>
static void tick(void) { ++fm1_ms; br_app_tick(); }
static void tap(unsigned b) { fm1_in.buttons|=1u<<b;host_pressed|=1u<<b;tick();fm1_in.buttons&=~(1u<<b);host_released|=1u<<b;tick(); }
static void page(unsigned p,unsigned s,const char *title) {
 assert(br_ui.page==p && br_ui.screen[p]==s); assert(!strcmp(br_header_title(),title));
}
int main(void) {
 br_app_init(); host_after_intro(); fm1_ms=2000;
 /* EDIT always targets the kit mixer, without changing the selected track. */
 for(unsigned part=0;part<BR_PARTS;part++) {
   br_ui.part=part; tap(BTN_HOME);
#if BR_PRODUCT_SMK37
   br_smk_fader_bank_select(1u);   /* SMK-37: FADER BANK opens KIT LEVEL; EDIT does not */
#else
   tap(BTN_EDIT);
#endif
   page(BR_PAGE_ROUTE,3,"KIT LEVEL"); assert(br_ui.part==part && !br_page_plays_keys());
   uint32_t before=br_part[BR_DRUM_PART].lane_level[2];
   br_control_turn(2,-1); assert(br_part[BR_DRUM_PART].lane_level[2]==before-1);
   tap(BTN_EDIT); page(BR_PAGE_ROUTE,0,"PATCH 1");
 }
 br_ui.part=0; tap(BTN_HOME);
 /* Changing pages while a synth key is held must release its original note. */
 fm1_in.notes|=1u; host_note_pressed|=1u; tick();
 assert(br_ui.held_note[0] && br_ui.held_part[0]==0u);
 tap(BTN_EDIT); fm1_in.notes&=~1u; tick(); assert(!br_ui.held_note[0]);
 tap(BTN_HOME); tap(BTN_GLO);
#if BR_PRODUCT_SMK37
 page(BR_PAGE_MIX,3,"GLOBAL / PAN");   /* SMK-37: GLO starts on PAN; the mixer is not in its ring */
 for(unsigned i=0;i<5;i++) tap(BTN_GLO);
 page(BR_PAGE_MIX,3,"GLOBAL / PAN");
#else
 page(BR_PAGE_MIX,0,"MIXER");
 for(unsigned i=0;i<6;i++) tap(BTN_GLO);
 page(BR_PAGE_MIX,0,"MIXER");
#endif
#if BR_PRODUCT_SMK37
 tap(BTN_EDIT);
#else
 tap(BTN_EDIT); tap(BTN_EDIT);
#endif
 page(BR_PAGE_ROUTE,0,"PATCH 1");
 br_control_turn(3,1); assert(br_part[0].mod.route[BR_ROUTES].enabled==1);
 br_ui.part=BR_DRUM_PART; assert(br_control(3).value==0);
 tap(BTN_EDIT); page(BR_PAGE_ROUTE,1,"PATCH 2");
 tap(BTN_GLO);
#if BR_PRODUCT_SMK37
 page(BR_PAGE_MIX,3,"GLOBAL / PAN");
#else
 page(BR_PAGE_MIX,0,"MIXER");
#endif
 /* Track patches and existing hidden routes are saved independently. */
 { static br_session_t saved;
   br_part[0].mod.route[BR_ROUTES] = (br_route_config_t){BR_SOURCE_VELOCITY, BR_TARGET_LEVEL, 35, 1};
   br_part[1].mod.route[BR_ROUTES+1] = (br_route_config_t){BR_SOURCE_KEY, BR_TARGET_PAN, -27, 1};
   br_part[0].mod.route[0] = (br_route_config_t){BR_SOURCE_ENV, BR_TARGET_FILTER, 19, 1};
   assert(br_capture(&saved));
   br_part[0].mod.route[BR_ROUTES].enabled=0; br_part[0].mod.route[0].enabled=0;
   assert(br_restore(&saved));
   assert(br_part[0].mod.route[BR_ROUTES].enabled && br_part[0].mod.route[BR_ROUTES].amount==35);
   assert(br_part[1].mod.route[BR_ROUTES+1].enabled && br_part[1].mod.route[BR_ROUTES+1].amount==-27);
   assert(br_part[0].mod.route[0].enabled && br_part[0].mod.route[0].amount==19);
   for (unsigned p=0;p<BR_PARTS;p++) { br_cached_revision[p]=0xffffffffu; br_snapshot(p); }
   assert(br_cached_mod[0].route[BR_ROUTES].target==BR_TARGET_LEVEL);
   assert(br_cached_mod[1].route[BR_ROUTES+1].amount==-27);
   assert(br_cached_mod[0].route[0].amount==19);
 }
 br_ui.part=0; tap(BTN_HOME); tap(BTN_LFO); page(BR_PAGE_LFO,0,"LFO 1");
 tap(BTN_LFO); page(BR_PAGE_LFO,1,"LFO 2");
 tap(BTN_LFO); page(BR_PAGE_LFO,2,"GLOBAL LFO");
 tap(BTN_LFO); page(BR_PAGE_LFO,0,"LFO 1");
 br_ui.part=BR_DRUM_PART; tap(BTN_HOME); tap(BTN_LFO);
 tap(BTN_LFO); tap(BTN_LFO); page(BR_PAGE_LFO,2,"GLOBAL LFO");
 tap(BTN_LFO); page(BR_PAGE_LFO,0,"LFO 1");
#if BR_PRODUCT_SMK37
 /* SMK-37: EDIT opens PATCH 1 (no KIT LEVEL) and cycles PATCH 1, PATCH 2. */
 br_ui.part=0; tap(BTN_HOME); tap(BTN_EDIT); page(BR_PAGE_ROUTE,0,"PATCH 1");
 tap(BTN_EDIT); page(BR_PAGE_ROUTE,1,"PATCH 2");
 tap(BTN_EDIT); page(BR_PAGE_ROUTE,0,"PATCH 1");
 br_ui.part=BR_DRUM_PART; tap(BTN_HOME); tap(BTN_EDIT); page(BR_PAGE_ROUTE,0,"PATCH 1");
 br_smk_fader_bank_select(1u); tap(BTN_EDIT); page(BR_PAGE_ROUTE,0,"PATCH 1");   /* out of KIT LEVEL */
 br_ui.part=0;
#endif
 br_ui.part=0; br_load_preset(0, BR_FIRST_PLAITS);
 br_ui.page=BR_PAGE_ENV; tap(BTN_HOME); assert(br_ui.screen[BR_PAGE_HOME]==0u);
 tap(BTN_HOME); assert(br_ui.screen[BR_PAGE_HOME]==1u);
 br_open_home_settings(); page(BR_PAGE_HOME,2,"MOD DEPTHS");
 assert(!strcmp(br_control(1).label,"HARMONICS"));
 br_open_home_settings(); assert(br_ui.screen[BR_PAGE_HOME]==0u);
 puts("Track patch navigation and kit mixer passed");
}
