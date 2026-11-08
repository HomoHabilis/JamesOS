#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls;
uint32_t br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static void fm1_audio_metrics_snapshot(uint32_t out[8]) { memset(out,0,32u); }
static void fm1_audio_metrics_reset(void) { }
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out,0,76u); out[0]=1u; }
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define BR_SPLIT_AUDIO 1
#define BR_DIAGNOSTIC_TEST 1
#define BR_PATTERN_SONG_EXPERIMENT 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/instrument_audio_unit.c"
#include <assert.h>
#include <stdio.h>
fm1_pattern_song br_ps_state;
int br_ps_select(uint32_t p){(void)p;return 0;}
int br_ps_copy_pattern(uint32_t p){(void)p;return 0;}
int br_ps_paste_pattern(uint32_t p){(void)p;return 0;}
static void body(uint8_t b[4],unsigned sector,unsigned off,unsigned n){b[0]=(uint8_t)sector;b[1]=(uint8_t)off;b[2]=(uint8_t)(off>>8);b[3]=(uint8_t)n;}
int main(void){uint8_t req[4],reply[128],before[sizeof host_project_flash];uint32_t n,i;
 br_app_init();for(i=0;i<sizeof host_project_flash;i++)host_project_flash[i]=(uint8_t)(i*17u+3u);memcpy(before,host_project_flash,sizeof before);
 body(req,0,0,32);
#if defined(FM1_PATTERN_SECTOR_TEST) && FM1_PATTERN_SECTOR_TEST
 assert(fm1_app_command(0xd2,req,4,reply,&n)&&n==32&&!memcmp(reply,before,32));
 body(req,25,4095,1);assert(fm1_app_command(0xd2,req,4,reply,&n)&&n==1&&reply[0]==before[sizeof before-1]);
 body(req,24,4064,32);assert(fm1_app_command(0xd2,req,4,reply,&n)&&n==32&&!memcmp(reply,before+24*4096+4064,32));
 body(req,26,0,1);assert(!fm1_app_command(0xd2,req,4,reply,&n));
 body(req,25,4096,1);assert(!fm1_app_command(0xd2,req,4,reply,&n));
 body(req,25,4095,2);assert(!fm1_app_command(0xd2,req,4,reply,&n));
 body(req,0,0,0);assert(!fm1_app_command(0xd2,req,4,reply,&n));
 body(req,0,0,33);assert(!fm1_app_command(0xd2,req,4,reply,&n));
 body(req,0,0,1);assert(!fm1_app_command(0xd2,req,3,reply,&n));assert(!fm1_app_command(0xd2,0,4,reply,&n));
 br_playing=1;assert(!fm1_app_command(0xd2,req,4,reply,&n));br_playing=0;
 br_recording=1;assert(!fm1_app_command(0xd2,req,4,reply,&n));br_recording=0;
 br_diagnostic_restore_in_progress=1;assert(!fm1_app_command(0xd2,req,4,reply,&n));br_diagnostic_restore_in_progress=0;
 flash_ok=0;assert(!fm1_app_command(0xd2,req,4,reply,&n));flash_ok=1;
 host_project_fail=3;assert(!fm1_app_command(0xd2,req,4,reply,&n));host_project_fail=0;
 assert(!br_diagnostic_restore_in_progress&&!memcmp(before,host_project_flash,sizeof before));
 br_ready=0;assert(!fm1_app_command(0xd2,req,4,reply,&n));br_ready=1;
 br_engine_pipeline_test_uncontained_fault_latch();assert(!fm1_app_command(0xd2,req,4,reply,&n));
#else
 assert(!fm1_app_command(0xd2,req,4,reply,&n));
#endif
 assert(!memcmp(before,host_project_flash,sizeof before));puts("read-only project-sector diagnostic gates and bounds passed");return 0;}
