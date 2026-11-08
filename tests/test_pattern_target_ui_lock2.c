#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define FM1_PATTERN_TARGET_EXPERIMENT 1
#define FM1_PATTERN_PROJECT_TARGET_EXPERIMENT 1
#define FM1_PATTERN_DEVICE_CANDIDATE 1
#define FM1_PATTERN_V16_ONLY 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static int scans;
int br_pp_target_scan(unsigned slot,char name[13]){(void)name;assert(slot<12);scans++;return PFS_EMPTY;}
int br_pp_target_save(unsigned slot,const char*name){(void)slot;(void)name;return PFS_INVALID;}
int br_pp_target_load(unsigned slot){(void)slot;return PFS_INVALID;}
int br_pp_target_ready(void){return 1;}
int br_pp_target_recover(int*slot){(void)slot;return FM1_PROJECT_SAFE_EMPTY;}
int br_pp_target_init_project(void){return PFS_INVALID;}
int main(void){br_session_t session;uint8_t saved[sizeof host_store];
 br_app_init();fm1_ms=2000;assert(br_capture(&session));assert(!settings_save(&session,sizeof session));memcpy(saved,host_store,sizeof saved);
 es.mode=1;es_project_boot_state=1;br_invalid_session_pending=1;assert(br_session_save_blocked()&&!br_pattern_session_scratch_blocked());
 es_scan_tick();assert(scans==1&&es_directory[0].state==2&&br_session_save_blocked());
 br_save_pending=1;br_save_poll();assert(!br_save_pending&&!memcmp(host_store,saved,sizeof saved));
 br_legacy_migration_pending=1;es_scan_tick();assert(scans==1&&es_directory[1].state==0);
 br_legacy_migration_pending=0;es_project_boot_state=0;es_scan_tick();assert(scans==1&&es_directory[1].state==0);
 es_project_boot_state=1;es_scan_tick();assert(scans==2&&es_directory[1].state==2&&br_session_save_blocked());
 puts("target project UI scans under settings lock while ordinary SAVE remains blocked");return 0;}
