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
int main(void) {
 br_app_init(); fm1_ms=2000; es.mode=1;
 uint8_t factory_copy[sizeof es.factory];
 assert(!br_project_target_factory(0,factory_copy,sizeof factory_copy));
 assert(br_quiescent_transaction_begin()); assert(br_project_target_factory(0,factory_copy,sizeof factory_copy)); br_quiescent_transaction_end();
 assert(!memcmp(factory_copy,&es.factory,sizeof factory_copy));
 ready_state=0; es.action=0; es_confirm(); assert(!strcmp(es.error,"PROJECT STORE LOCKED") && !saves && !loads); ready_state=1;
 es_confirm(); assert(!strcmp(es.error,"PROJECT STORE LOCKED") && !saves && !loads);
 recover_result=FM1_PROJECT_BUSY; assert(br_project_target_recover()==FM1_PROJECT_BUSY && !es_project_boot_state);
 es_confirm(); assert(!strcmp(es.error,"PROJECT STORE LOCKED") && !saves && !loads);
 recover_result=PFS_EMPTY; assert(br_project_target_recover()==PFS_EMPTY && !es_project_boot_state);
 recover_result=FM1_PROJECT_SAFE_EMPTY; assert(br_project_target_recover()==FM1_PROJECT_SAFE_EMPTY && es_project_boot_state==1u);
 es_scan_slot(0); assert(es_directory[0].state==2);
 es.slot=0; es.mode=4; strcpy(es.name,"TEST"); es_confirm(); assert(saves==1 && es.current==0 && es.mode==0);
 es_directory[1].state=3; es.slot=1; es.mode=4; es_confirm(); assert(es.mode==5); es_confirm(); assert(saves==1 && !strcmp(es.error,"USED DATA"));
 es_directory[1].state=2; save_result=PFS_FULL; es.mode=4; es_confirm(); assert(saves==2 && es.current==0 && !strcmp(es.error,"PROJECT STORE FULL"));
 load_result=PFS_INVALID; es.pending=1; es.target=0; es.mode=2; es_finish_pending(); assert(loads==1 && es.current==0 && es.mode==2);
 load_result=FM1_PROJECT_WORKING_FAILED; es.target=1; es_finish_pending(); assert(es.current==1 && !strcmp(es.error,"LOADED; WORK SAVE FAILED"));
 es.target=0; load_result=PFS_OK; es_finish_pending(); assert(loads==3 && es.current==0 && es.mode==0);
 es.pending=2; es.mode=2; init_result=PFS_INVALID; es_finish_pending(); assert(inits==1 && es.mode==2 && !strcmp(es.error,"INIT FAILED"));
 init_result=FM1_PROJECT_NEW_WORKING_ONLY; es_finish_pending(); assert(inits==2 && es.current==0 && es.mode==2 && !strcmp(es.error,"WORK SAVED; INIT FAILED"));
 init_result=PFS_OK; es_finish_pending(); assert(inits==3 && es.current==-1 && es.mode==0);
 scan_result=FM1_PROJECT_ROLLBACK_FAILED; es_scan_slot(1);
 assert(es_project_fault && !es_slot_retry(1) && !strcmp(es.error,"PROJECT RECOVERY NEEDED"));
 es.mode=1; es.action=0; es_confirm(); assert(es.mode==1 && !strcmp(es.error,"PROJECT RECOVERY NEEDED"));
 puts("target project UI guarded hooks passed"); return 0;
}
