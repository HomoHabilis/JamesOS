/* Hardware-specific backend for the shared SAVE interface. */
#if defined(BR_PATTERN_PROJECT_STREAM_HOST) && BR_PATTERN_PROJECT_STREAM_HOST
#if defined(FM1_DEVICE) && FM1_DEVICE
#error "Pattern stream project UI cannot bind on target before qualification"
#endif
#define BR_PATTERN_PROJECT_UI 1
#define BR_PATTERN_PROJECT_UI_TARGET 0
#include "pattern_pair_ui.h"
#define br_pp_ui_scan br_pair_ui_scan
#define br_pp_ui_save br_pair_ui_save
#define br_pp_ui_load br_pair_ui_load
#define br_pp_ui_init_project br_pair_ui_init_project
#define br_pp_ui_recover br_pair_ui_recover
#elif defined(FM1_PATTERN_PROJECT_TARGET_EXPERIMENT) && FM1_PATTERN_PROJECT_TARGET_EXPERIMENT
#if !defined(FM1_PATTERN_TARGET_EXPERIMENT) || !FM1_PATTERN_TARGET_EXPERIMENT
#error "Target project UI requires the target pattern experiment"
#endif
#define BR_PATTERN_PROJECT_UI 1
#define BR_PATTERN_PROJECT_UI_TARGET 1
#include "pattern_project_adapter.h"
extern int br_pp_target_scan(unsigned, char [13]);
extern int br_pp_target_save(unsigned, const char *);
extern int br_pp_target_load(unsigned);
extern int br_pp_target_init_project(void);
extern int br_pp_target_recover(int *);
extern int br_pp_target_ready(void);
#define br_pp_ui_scan br_pp_target_scan
#define br_pp_ui_save br_pp_target_save
#define br_pp_ui_load br_pp_target_load
#define br_pp_ui_init_project br_pp_target_init_project
#define br_pp_ui_recover br_pp_target_recover
#elif defined(BR_PATTERN_PROJECT_HOST) && BR_PATTERN_PROJECT_HOST
#if defined(FM1_DEVICE)
#error "Host project backend cannot be enabled on device"
#endif
#define BR_PATTERN_PROJECT_UI 1
#define BR_PATTERN_PROJECT_UI_TARGET 0
#include "pattern_project_adapter.h"
extern int br_pp_host_scan(unsigned, char [13]);
extern int br_pp_host_save(unsigned, const char *);
extern int br_pp_host_load(unsigned);
extern int br_pp_host_init_project(void);
extern int br_pp_host_recover(int *);
#define br_pp_ui_scan br_pp_host_scan
#define br_pp_ui_save br_pp_host_save
#define br_pp_ui_load br_pp_host_load
#define br_pp_ui_init_project br_pp_host_init_project
#define br_pp_ui_recover br_pp_host_recover
#else
#define BR_PATTERN_PROJECT_UI 0
#define BR_PATTERN_PROJECT_UI_TARGET 0
#endif
#define ES_SLOTS 12
typedef struct { char name[PS_NAME_BYTES]; } es_record;
static struct {
    int mode, action, slot, slot_purpose, current, pending, target, choice, cursor, name_from, rec_pending, rec_used;
    char name[13], error[32], notice[32];
    br_session_t factory;
} es
#if defined(__ELF__) && !defined(DINGHY_HOST)
__attribute__((section(".pool"), aligned(4)))
#endif
;
/* 0 unknown, 1 valid, 2 empty, 3 foreign/corrupt, 4 read error. */
static struct { uint8_t state; char name[PS_NAME_BYTES]; } es_directory[ES_SLOTS];
#if BR_PATTERN_PROJECT_UI_TARGET
static uint32_t es_project_fault;
/* 0: waiting for startup recovery, 1: ready, 2: recovery failed. */
static uint32_t es_project_boot_state;
#endif
static int es_slot_ready(int slot) { return slot>=0 && slot<ES_SLOTS && es_directory[slot].state>=1 && es_directory[slot].state<=3; }
static int es_slot_loadable(int slot) { return es_slot_ready(slot) && es_directory[slot].state==1; }
static int es_slot_retry(int slot) {
#if BR_PATTERN_PROJECT_UI_TARGET
    if(es_project_fault)return 0;
#endif
    if(slot<0 || slot>=ES_SLOTS || es_directory[slot].state!=4)return 0;
    es_directory[slot].state=0;return 1;
}
static int es_latest(int slot, es_record *out, int *copy) {
    const char *name; unsigned state;
    if(slot<0 || slot>=ES_SLOTS) return 0;
    state=es_directory[slot].state;
    if(state==2) return 0;
#if BR_PATTERN_PROJECT_UI_TARGET
    name=state==1?es_directory[slot].name:state==3?"USED DATA":state==4?"READ ERROR":!br_pp_target_ready() || es_project_boot_state!=1u?"LOCKED":"SCANNING";
#else
    name=state==1?es_directory[slot].name:state==3?"USED DATA":state==4?"READ ERROR":"SCANNING";
#endif
    if(out)br_text_copy(out->name,name,sizeof out->name);
    if(copy)*copy=0;
    return 1;
}
static void es_error(const char *s) { es.notice[0]=0;br_text_copy(es.error,s,sizeof es.error);br_message(s);br_ui.message_until+=2000u; }
/* Caller holds the quiescent transaction. */
#if BR_PATTERN_PROJECT_UI
static void es_scan_slot(int slot) {
    char name[13]; int rc=br_pp_ui_scan((unsigned)slot,name);
#if BR_PATTERN_PROJECT_UI_TARGET
    if(rc==FM1_PROJECT_ROLLBACK_FAILED){es_project_fault=1u;es_directory[slot].state=4;es_error("RECOVERY NEEDED");return;}
#endif
    es_directory[slot].state=(uint8_t)(rc==PFS_OK?1:rc==PFS_EMPTY?2:rc==PFS_INVALID?3:4);
    if(rc==PFS_OK)br_text_copy(es_directory[slot].name,name,sizeof es_directory[slot].name);
    br_ui.redraw=1;
}
#else
static void es_scan_slot(int slot) {
    int rc; ps_info info;
    rc=ps_load(&br_project_store,(uint32_t)slot,&br_loaded,&info);
    es_directory[slot].state=(uint8_t)(rc==PS_OK?1:rc==PS_EMPTY?2:rc==PS_CORRUPT?3:4);
    if(rc==PS_OK)br_text_copy(es_directory[slot].name,info.name,sizeof es_directory[slot].name);
    br_ui.redraw=1;
}
#endif
#if BR_PATTERN_PROJECT_UI
/* The host configures the owner and store after br_app_init, then calls this
 * once to restore the last committed working project. */
static void br_project_host_ui_reset(void) {
#if defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE
#endif
}
#if BR_PATTERN_PROJECT_UI_TARGET
/* The target bridge calls this only inside its owned project transaction. */
static int br_project_target_factory(void *context,uint8_t *out,size_t length) {
    (void)context;
    if(!out || length!=sizeof es.factory || !br_diagnostic_restore_in_progress || br_playing) return 0;
    memcpy(out,&es.factory,sizeof es.factory); return 1;
}
static int br_project_target_recover(void) {
    int slot=-1, rc;
    if(br_playing) return PFS_INVALID;
    rc=br_pp_ui_recover(&slot);
    if(rc==FM1_PROJECT_ROLLBACK_FAILED){es_project_fault=1u;es_error("RECOVERY NEEDED");}
    if(rc==PFS_OK || rc==FM1_PROJECT_SAFE_EMPTY || rc==FM1_PROJECT_FOREIGN_WORKING) es_project_boot_state=1u;
    if(rc==PFS_OK) { br_project_host_ui_reset(); es.current=slot; if(slot>=0 && slot<ES_SLOTS) es_scan_slot(slot); }
    return rc;
}
#else
static int br_project_host_recover(void) {
    int slot=-1, rc;
    if(br_playing || !br_quiescent_transaction_begin()) return PFS_INVALID;
    rc=br_pp_ui_recover(&slot);
    if(rc==PFS_OK) { br_project_host_ui_reset(); es.current=slot; if(slot>=0 && slot<ES_SLOTS) es_scan_slot(slot); }
    br_quiescent_transaction_end();
    return rc;
}
#endif
#endif
static void es_scan_tick(void) {
    int slot;
#if BR_PATTERN_PROJECT_UI_TARGET
    if(es_project_fault || !br_pp_target_ready() || es_project_boot_state!=1u)return;
#endif
    if(!es.mode || br_playing || !br_engine_quiet() || !br_fx_quiet())return;
#if BR_PATTERN_PROJECT_UI_TARGET && defined(FM1_PATTERN_DEVICE_CANDIDATE) && FM1_PATTERN_DEVICE_CANDIDATE && defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    /* The protected settings record is independent of the pattern partition.
     * Only a legacy migration still owns the project scratch buffers. */
    if(br_pattern_session_scratch_blocked())return;
#else
    if(br_session_save_blocked())return;
#endif
    for(slot=0;slot<ES_SLOTS;slot++) if(!es_directory[slot].state)break;
#if BR_PATTERN_PROJECT_UI_TARGET
    if(slot==ES_SLOTS)return;
    es_scan_slot(slot);
#else
    if(slot==ES_SLOTS || !br_quiescent_transaction_begin())return;
    es_scan_slot(slot);br_quiescent_transaction_end();
#endif
}
static int es_silence_for_save(void);
#if BR_PATTERN_PROJECT_UI
static int es_write(int slot,const char *name) {
    int rc;
#if BR_PATTERN_PROJECT_UI_TARGET
    if(es_project_fault){es_error("RECOVERY NEEDED");return 0;}
    if(!br_pp_target_ready() || es_project_boot_state!=1u){es_error(es_project_boot_state==2u?"RECOVERY FAILED":"STORE LOCKED");return 0;}
#endif
    if(!es_slot_ready(slot)){es_error("SCANNING SLOTS");return 0;}
    if(br_playing){es_error("STOP PLAYBACK");return 0;}
    if(!es_silence_for_save()){es_error("BUSY");return 0;}
#if !BR_PATTERN_PROJECT_UI_TARGET
    if(!br_quiescent_transaction_begin()){es_error("BUSY");return 0;}
#endif
    rc=br_pp_ui_save((unsigned)slot,name);
#if !BR_PATTERN_PROJECT_UI_TARGET
    br_quiescent_transaction_end();
#endif
    if(rc==FM1_PROJECT_WORKING_FAILED){es_directory[slot].state=0;es_error("SAVED; WORK SAVE FAILED");return 0;}
    if(rc==FM1_PROJECT_ROLLBACK_FAILED){
#if BR_PATTERN_PROJECT_UI_TARGET
        es_project_fault=1u;
#endif
        es_error("RECOVERY NEEDED");return 0;
    }
    if(rc!=PFS_OK){es_error(rc==PFS_FULL?"STORE FULL":"SAVE FAILED");return 0;}
    es_directory[slot].state=1;br_text_copy(es_directory[slot].name,name,sizeof es_directory[slot].name);
    es.current=slot;es.error[0]=0;return 1;
}
#else
static int es_write(int slot,const char *name) {
    int rc; ps_info info;
    if(!es_slot_ready(slot)){es_error("SCANNING SLOTS");return 0;}
    if(br_playing){es_error("STOP PLAYBACK");return 0;}
    if(!es_silence_for_save()){es_error("BUSY");return 0;}
    rc=br_project_save((uint32_t)slot,name,es_directory[slot].state==3,&info);
    if(rc!=PS_OK){es_error("SAVE FAILED");es_directory[slot].state=0;return 0;}
    es_directory[slot].state=1;br_text_copy(es_directory[slot].name,info.name,sizeof es_directory[slot].name);
    es.current=slot;es.error[0]=0;return 1;
}
#endif
static void es_close(void) { es.mode=0;es.pending=0;es.error[0]=0;es.notice[0]=0;br_draw_layout_old=0xffffffffu;br_ui.redraw=1; }
#if BR_PATTERN_PROJECT_UI
static void es_finish_pending(void) {
    int rc;
#if BR_PATTERN_PROJECT_UI_TARGET
    if(es_project_fault){es_error("RECOVERY NEEDED");return;}
    if(!br_pp_target_ready() || es_project_boot_state!=1u){es_error(es_project_boot_state==2u?"RECOVERY FAILED":"STORE LOCKED");return;}
#endif
    if(br_playing || !br_engine_quiet() || !br_fx_quiet()){es_error("STOP AND WAIT");return;}
#if !BR_PATTERN_PROJECT_UI_TARGET
    if(!br_quiescent_transaction_begin()){es_error("BUSY");return;}
#endif
    rc=es.pending==1?br_pp_ui_load((unsigned)es.target):
       es.pending==2?br_pp_ui_init_project():PFS_INVALID;
#if !BR_PATTERN_PROJECT_UI_TARGET
    br_quiescent_transaction_end();
#endif
#if BR_PATTERN_PROJECT_UI_TARGET
    if(rc==FM1_PROJECT_NEW_WORKING_ONLY){es_error("WORK SAVED; INIT FAILED");return;}
#endif
    if(rc==FM1_PROJECT_WORKING_FAILED){
        br_project_host_ui_reset(); es.current=es.pending==1?es.target:-1;
        es_error(es.pending==1?"LOADED; WORK SAVE FAILED":"INIT; WORK SAVE FAILED"); return;
    }
    if(rc==FM1_PROJECT_ROLLBACK_FAILED){
#if BR_PATTERN_PROJECT_UI_TARGET
        es_project_fault=1u;
#endif
        es_error("RECOVERY NEEDED");return;
    }
    if(rc!=PFS_OK){
#if BR_PATTERN_PROJECT_UI_TARGET
        es_error(rc==PFS_FULL?"STORE FULL":es.pending==1?"LOAD FAILED":"INIT FAILED");
#else
        es_error(rc==PFS_FULL?"STORE FULL":es.pending==1?"LOAD FAILED":"INIT FAILED");
#endif
        return;
    }
    br_project_host_ui_reset(); es.current=es.pending==1?es.target:-1;es_close();
}
#else
static void es_finish_pending(void) {
    int rc;
    if(br_playing || !br_engine_quiet() || !br_fx_quiet()){es_error("STOP AND WAIT");return;}
    rc=es.pending==1?br_project_load((uint32_t)es.target,0):
       es.pending==2?br_project_init(&es.factory):PS_INVALID;
    if(rc!=PS_OK){es_error(es.pending==1?"LOAD FAILED":"INIT FAILED");return;}
    es.current=es.pending==1?es.target:-1;es_close();
}
#endif
static void es_name_begin(void) {
    int i;memset(es.name,0,sizeof es.name);es.name_from=es.mode;
    if(es_slot_loadable(es.slot))br_text_copy(es.name,es_directory[es.slot].name,sizeof es.name);
    else {br_text_copy(es.name,"SLOT 00",sizeof es.name);es.name[5]=(char)('0'+(es.slot+1)/10);es.name[6]=(char)('0'+(es.slot+1)%10);}
    for(i=0;i<12 && es.name[i];i++){}
    for(;i<12;i++)es.name[i]=' ';
    es.name[12]=0;es.cursor=0;es.mode=4;es.error[0]=0;
}
/* A stopped SAVE should silence active notes and effect tails immediately.
 * Reinitialize only under the existing audio-quiescent transaction, as in
 * the validated session restore path. Playback is never stopped implicitly. */
static int es_silence_for_save(void) {
    uint32_t i;
    if (br_playing) return 0;
    if (br_engine_quiet() && br_fx_quiet()) return 1;
    if (!br_quiescent_transaction_begin()) return 0;
    for (i=0u;i<7u;++i) br_midi_output_release_destination(i);
    br_midi_route_invalidate(&br_midi_routes,15u);
    br_record_note_cancel_all();
    memset(br_ui.held_note,0,sizeof br_ui.held_note);
    memset(br_ui.held_arp,0,sizeof br_ui.held_arp);
    memset(br_ui.sustain_note,0,sizeof br_ui.sustain_note);
    memset(br_ui.sustain_gen,0,sizeof br_ui.sustain_gen);
    memset(br_arp_keys,0,sizeof br_arp_keys);
    br_engine_init();br_fx_init();
    if (br_engine_quiet() && br_fx_quiet()) br_fx_warm_controls_quiescent();
    i = br_engine_quiet() && br_fx_quiet();
    br_quiescent_transaction_end();
    return (int)i;
}
static void es_open(void) {
    uint32_t k;
    for(k=0;k<27;k++)if(br_ui.held_note[k])BR_FOREGROUND_MUTATE(br_key_up(k));
    if(br_step_press_pending() || br_ui.step_edit_active)BR_FOREGROUND_MUTATE(br_step_edit_end());
    if (!br_playing) (void)es_silence_for_save();
    es.mode=1;es.action=0;es.slot=es.current>=0?es.current:0;es.slot_purpose=0;es.pending=0;es.error[0]=0;es.notice[0]=0;br_ui.redraw=1;
}
static void es_quick_save(void) {
    es_record r;
    if (!br_playing && !es_silence_for_save()){es_error("BUSY");return;}
    if(es.current<0){es_open();es.mode=3;return;}
    if(!es_slot_ready(es.current) || !es_slot_loadable(es.current)){es_open();es.mode=3;es_error("SELECT SLOT");return;}
    (void)es_latest(es.current,&r,0);
    if(es_write(es.current,r.name)){
        if(es.mode==1)es_close();
        br_message("SAVED");
    }
}
#include "instrument_project_ui.c"
