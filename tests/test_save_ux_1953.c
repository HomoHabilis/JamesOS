#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define FM1_PATTERN_TARGET_EXPERIMENT 1
#define FM1_PATTERN_PROJECT_TARGET_EXPERIMENT 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
static int scan_result=PFS_EMPTY, save_result=PFS_OK, load_result=PFS_OK, init_result=PFS_OK;
static int saves, loads, inits, ready_state=1, recover_result=FM1_PROJECT_SAFE_EMPTY;
int br_pp_target_scan(unsigned s, char name[13]) { assert(!br_diagnostic_restore_in_progress && s<12); if(scan_result==PFS_OK) strcpy(name,"TEST"); return scan_result; }
int br_pp_target_save(unsigned s,const char *name) { assert(!br_diagnostic_restore_in_progress && s<12 && name[0]); ++saves; return save_result; }
int br_pp_target_load(unsigned s) { assert(!br_diagnostic_restore_in_progress && s<12); ++loads; return load_result; }
int br_pp_target_ready(void) { return ready_state; }
int br_pp_target_recover(int *slot) { (void)slot; return recover_result; }
int br_pp_target_init_project(void) { assert(!br_diagnostic_restore_in_progress); ++inits; return init_result; }

static void tick(void){++fm1_ms;br_app_tick();}
static void tap(unsigned k){fm1_in.buttons|=1u<<k;host_pressed|=1u<<k;tick();fm1_in.buttons&=~(1u<<k);host_released|=1u<<k;tick();}
static void knob(int n){host_encoder[ENC_KNOB1]+=n;tick();}
static void knob2(int n){host_encoder[ENC_KNOB2]+=n;tick();}
int main(void){
 br_app_init();fm1_ms=2000;es_project_boot_state=1;es.current=0;es_directory[0].state=1;strcpy(es_directory[0].name,"PROJECT OLD");
 tap(BTN_SAVE);assert(es.mode==1 && es.action==0);tap(BTN_OCT_UP);assert(es.mode==1&&!saves);
 tap(BTN_SAVE);assert(es.mode==0&&saves==1&&es.current==0&&!strcmp(es_directory[0].name,"PROJECT OLD"));
 tap(BTN_SAVE);knob(1);assert(es.action==1);tap(BTN_SAVE);assert(es.mode==3&&es.slot_purpose==0);
 es_directory[1].state=1;strcpy(es_directory[1].name,"OTHER");knob(1);assert(es.slot==1);tap(BTN_SAVE);assert(es.mode==4&&!strcmp(es.name,"OTHER       "));
 knob(1);assert(es.cursor==0 && es.name[0]=='P' && es_knob_active==0u);knob(-1);assert(es.name[0]=='O');
 knob2(1);assert(es.cursor==1 && es.name[1]=='T' && es_knob_active==1u);knob2(-1);assert(es.cursor==0);
 tap(BTN_OCT_UP);assert(es.mode==4 && saves==1);tap(BTN_SAVE);assert(es.mode==6&&saves==1);
 tap(BTN_SAVE);assert(es.mode==5&&saves==1);tap(BTN_SAVE);assert(es.mode==0&&saves==2&&es.current==1);
 tap(BTN_SAVE);knob(1);tap(BTN_SAVE);es_directory[2].state=2;knob(1);tap(BTN_SAVE);assert(es.mode==4&&!memcmp(es.name,"SLOT 03",7));
 tap(BTN_SAVE);assert(es.mode==6);save_result=FM1_PROJECT_WORKING_FAILED;tap(BTN_SAVE);
 assert(es.mode==4&&es.current==1&&!strcmp(es.error,"SAVED; WORK SAVE FAILED")&&saves==3);save_result=PFS_OK;
 tap(BTN_PLAY);assert(es.mode==4&&br_playing);tap(BTN_PLAY);assert(es.mode==4&&!br_playing);
 tap(BTN_HOME);tap(BTN_SAVE);knob(2);assert(es.action==2);tap(BTN_SAVE);assert(es.mode==3&&es.slot_purpose==1);
 tap(BTN_OCT_UP);assert(es.mode==3&&!loads);es_directory[0].state=1;es.slot=0;tap(BTN_SAVE);assert(es.mode==2&&es.pending==1);
 knob(1);tap(BTN_SAVE);assert(loads==1&&es.mode==0&&es.current==0);
 tap(BTN_SAVE);knob(3);assert(es.action==3);tap(BTN_SAVE);assert(es.mode==2&&es.pending==2);knob(2);tap(BTN_SAVE);assert(es.mode==0&&!inits);
 /* Simultaneous mode+SAVE cancels, and REC+SAVE still quick-saves. */
 tap(BTN_SAVE);{uint32_t buttons=(1u<<BTN_HOME)|(1u<<BTN_SAVE),up=0;es_input(&buttons,&up,0);assert(es.mode==0&&saves==3);}
 fm1_in.buttons|=1u<<BTN_REC;host_pressed|=1u<<BTN_REC;tick();tap(BTN_SAVE);fm1_in.buttons&=~(1u<<BTN_REC);host_released|=1u<<BTN_REC;tick();assert(saves==4&&!br_recording);
 /* Holding SAVE cannot generate another edge. */
 fm1_in.buttons|=1u<<BTN_SAVE;host_pressed|=1u<<BTN_SAVE;tick();assert(es.mode==1);for(int i=0;i<5;i++)tick();assert(es.mode==1&&saves==4);fm1_in.buttons&=~(1u<<BTN_SAVE);host_released|=1u<<BTN_SAVE;tick();
 /* A sounding stopped instrument is silenced by SAVE without altering the session. */
 { br_session_t before,after; int32_t audio[BR_BLOCK*2u]={0};
   es_close(); br_ui.part=0; br_ui.page=BR_PAGE_HOME; br_hold_on=0; br_recording=0; br_playing=0;
   br_part[0].sound.reverb=127u; br_part[0].revision++;
   br_key_down(0u,1); for(int i=0;i<12;i++){memset(audio,0,sizeof audio);assert(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK);}
   assert(!br_engine_quiet() || !br_fx_quiet()); assert(br_capture(&before));
   es_open(); assert(es.mode==1); assert(br_engine_quiet() && br_fx_quiet()); assert(br_capture(&after));
   assert(!memcmp(&before,&after,sizeof before)); assert(!br_diagnostic_restore_in_progress);
   es_close(); br_key_down(1u,1); memset(audio,0,sizeof audio);assert(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
   assert(!br_engine_quiet()); br_playing=1; es_open(); assert(br_playing && !br_engine_quiet()); es_close(); br_playing=0; br_key_up(1u);
   assert(br_quiescent_transaction_begin()); assert(!es_silence_for_save()); br_quiescent_transaction_end();
   es.current=0;es_directory[0].state=1; br_playing=1;
   assert(!es_write(0,"TEST") && !strcmp(es.error,"STOP PLAYBACK"));
   br_playing=0; br_key_down(2u,1); memset(audio,0,sizeof audio);assert(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
   assert(es_write(0,"TEST") && br_engine_quiet() && br_fx_quiet());
 }
 /* Error notices last exactly two seconds longer, including quick-save
  * failures where no SAVE menu is open; ordinary messages are unchanged. */
 { br_app_init();fm1_ms=10000u;br_bpm=120u;es_project_boot_state=1u;
   es.current=0;es_directory[0].state=1;strcpy(es_directory[0].name,"PROJECT OLD");
   br_message("NORMAL");uint32_t normal=br_ui.message_until-fm1_ms;
   es_error("STORE FULL");assert(br_ui.message_until-fm1_ms==normal+2000u);
   save_result=PFS_FULL;es_quick_save();assert(!strcmp(br_ui.message,"STORE FULL"));
   assert(br_ui.message_until-fm1_ms==normal+2000u);save_result=PFS_OK;
 }
 /* Incompatible slots remain visible and require explicit overwrite. */
 { int prior=saves;br_app_init();fm1_ms=12000u;es_project_boot_state=0u;
   recover_result=FM1_PROJECT_FOREIGN_WORKING;assert(br_project_target_recover()==FM1_PROJECT_FOREIGN_WORKING&&es_project_boot_state==1u);
   es_directory[3].state=3;es.slot=3;es.current=-1;es.mode=4;strcpy(es.name,"REPLACED");
   es_confirm();assert(es.mode==5&&saves==prior);es_confirm();assert(saves==prior+1&&es_directory[3].state==1);
 }
 puts("SAVE menu navigation, silence and failure notice timing passed");return 0;
}
