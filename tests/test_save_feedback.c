/* Exercise the real foreground UI with a delayed physical-display model. */
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define FM1_PATTERN_TARGET_EXPERIMENT 1
#define FM1_PATTERN_PROJECT_TARGET_EXPERIMENT 1
static void feedback_sync_test(void);
#define ES_STORAGE_FEEDBACK_SYNC() feedback_sync_test()
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
volatile unsigned fm1_project_old_format_seen;
static unsigned flushes, saves, loads, inits;
static int save_result=PFS_OK;
static int init_result=PFS_OK;
static uint16_t physical[240*24];
static void feedback_sync_test(void) {
    /* Host blits represent queued panel pixels; only this wait commits them. */
    lcd_sync();
    memcpy(physical,fb+216*240,sizeof physical);
    ++flushes;
    if (flushes==1) {
        FILE *f=fopen("build/save-feedback-test/saving.ppm","wb");assert(f);
        fprintf(f,"P6\n240 240\n255\n");
        for (unsigned i=0;i<240*240;i++) {
            uint16_t c=fb[i];unsigned char rgb[3]={(c>>11)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
            fwrite(rgb,1,3,f);
        }
        fclose(f);
    }
}
static void check_banner(const char *text) {
    uint16_t expected[240*24];
    unsigned n=flushes;
    assert(n && !br_diagnostic_restore_in_progress);
    memcpy(expected,physical,sizeof expected);
    cv_begin(240,24,BR_BACKGROUND);cv_ox=cv_oy=0;
    br_widget_text_2x((240-br_widget_text_width_scaled(text,2))/2,4,text,C_WHITE);
    /* br_canvas_blit shades the background before sending it. */
    br_canvas_shade(0,216);
    for(unsigned i=0;i<240*24;i++) assert(expected[i]==swap16(cv_px[i]));
}
int br_pp_target_ready(void){return 1;}
int br_pp_target_scan(unsigned s,char name[13]){(void)s;(void)name;assert(!br_recording);return PFS_EMPTY;}
int br_pp_target_recover(int *slot){(void)slot;return FM1_PROJECT_SAFE_EMPTY;}
int br_pp_target_save(unsigned s,const char *name){(void)s;(void)name;assert(!br_recording);assert(flushes==saves+loads+inits+1);check_banner("SAVING...");saves++;return save_result;}
int br_pp_target_load(unsigned s){(void)s;assert(flushes==saves+loads+inits+1);check_banner("LOADING...");loads++;return PFS_OK;}
int br_pp_target_init_project(void){assert(!br_recording);assert(br_engine_quiet() && br_fx_quiet());assert(flushes==saves+loads+inits+1);check_banner("INITIALIZING...");inits++;return init_result;}
static void tick(void){++fm1_ms;br_app_tick();}
static void tap(unsigned k){fm1_in.buttons|=1u<<k;host_pressed|=1u<<k;tick();fm1_in.buttons&=~(1u<<k);host_released|=1u<<k;tick();}
int main(void){
 br_app_init();fm1_ms=2000;es_project_boot_state=1;es.current=0;es_directory[0].state=1;strcpy(es_directory[0].name,"TEST");
 /* A held canvas is drained before the feedback, even with an incomplete pass. */
 tap(BTN_SAVE);br_draw_more=br_draw_pass_with(es_draw);tap(BTN_SAVE);assert(saves==1&&!es.mode);
 /* REC+SAVE quicksave works and its release does not toggle recording. */
 fm1_in.buttons|=1u<<BTN_REC;host_pressed|=1u<<BTN_REC;tick();tap(BTN_SAVE);fm1_in.buttons&=~(1u<<BTN_REC);host_released|=1u<<BTN_REC;tick();assert(saves==2&&!br_recording);
 /* Save current before load, then load: each storage operation gets its own flush. */
 es.mode=2;es.pending=1;es.target=1;es.choice=0;tap(BTN_SAVE);assert(saves==3&&loads==1&&!es.mode);
 es.mode=2;es.pending=2;es.choice=1;tap(BTN_SAVE);assert(inits==1&&!es.mode);
 /* Cancellation and failed preconditions never touch storage. */
 es.mode=2;es.pending=1;es.choice=2;tap(BTN_SAVE);assert(flushes==5);
 br_playing=1;assert(!es_write(0,"TEST"));assert(flushes==5);br_playing=0;
 save_result=PFS_FULL;es.current=0;es.mode=1;es.action=0;tap(BTN_SAVE);assert(saves==4 && !strcmp(es.error,"STORE FULL"));assert(!strcmp(br_ui.message,"STORE FULL"));
 /* With REC armed, directory scans and both existing/new-slot saves temporarily
  * disarm recording, then restore it on success and failure. */
 br_app_init(); fm1_ms=4000; es_project_boot_state=1; es.mode=1;
 es_directory[0].state=1; strcpy(es_directory[0].name,"TEST"); es.current=0;
 es_directory[1].state=0; br_recording=1; br_playing=0;
 es_scan_tick(); assert(es_directory[1].state==2 && br_recording==1);
 save_result=PFS_OK; assert(es_write(0,"TEST") && br_recording==1);
 assert(es_write(1,"NEW PROJECT") && br_recording==1);
 save_result=PFS_FULL; assert(!es_write(0,"TEST") && br_recording==1);
 save_result=FM1_PROJECT_BUSY; assert(!es_write(0,"TEST") && br_recording==1 && !strcmp(es.error,"SAVE FAILED"));
 save_result=PFS_OK; br_recording=0; assert(es_write(0,"TEST") && br_recording==0);
 /* INIT cuts a lingering effect tail while stopped and temporarily disarms REC. */
 es.mode=2; es.pending=2; br_recording=1; br_playing=0;
 br_fx_track[0].tail_samples=100000u; assert(!br_fx_quiet());
 { unsigned previous=inits; es_finish_pending(); assert(inits==previous+1 && !es.mode && br_recording && br_fx_quiet()); }
 /* Failed INIT still restores REC; playback remains a hard stop. */
 es.mode=2; es.pending=2; init_result=PFS_FULL;
 { unsigned previous=inits; es_finish_pending(); assert(inits==previous+1 && br_recording && !strcmp(es.error,"STORE FULL")); }
 init_result=PFS_OK; br_playing=1;
 { unsigned previous=inits; es_finish_pending(); assert(inits==previous && br_recording && !strcmp(es.error,"STOP AND WAIT")); }
 puts("SAVE and INIT preserve REC, and stopped INIT clears effect tails");return 0;
}
