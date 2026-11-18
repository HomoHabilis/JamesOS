/* Target callback bodies are extracted by test_delete_target_callbacks.py. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "project_delete_protocol.h"
#include "pattern_sha256.h"
#define FM1_PROJECT_BUSY (-13)
#define PFS_OK 0
#define PFS_INVALID (-2)
#define PPS_MAX_PAYLOAD 3584u
static uint32_t br_import_state,br_export_nonce,br_diagnostic_upload_length,br_diagnostic_snapshot_valid,br_diagnostic_chunks[4];
static int br_playing,quiet=1,erases,feedback;
static struct {uint8_t b[PPS_MAX_PAYLOAD];} br_diagnostic_session;
static struct {unsigned state;} es_directory[12];
static struct {unsigned redraw;} br_ui;
static int br_ps_target_fixed_runtime;
static int br_engine_quiet(void){return quiet;}static int br_fx_quiet(void){return quiet;}
static void es_storage_feedback(const char *text){assert(!strcmp(text,"DELETING..."));feedback++;}
static int fm1_fixed_runtime_export(void *r,unsigned slot,uint8_t *bytes,unsigned cap,uint32_t *length,char *name){(void)r;assert(slot<12&&cap>=8);memcpy(bytes,"PSC3DATA",8);memcpy(name,"TEST",5);*length=8;return PFS_OK;}
static int fm1_fixed_runtime_inspect_unreadable_experiment(void *r,unsigned slot,uint8_t *digest){(void)r;(void)slot;memset(digest,7,32);return PFS_OK;}
static int fm1_fixed_runtime_clear_unreadable_experiment(void *r,unsigned slot,const uint8_t *digest){(void)r;(void)slot;(void)digest;assert(feedback>erases);erases++;return PFS_OK;}
static int fm1_fixed_runtime_delete_experiment(void *r,unsigned slot,const char *name,const uint8_t *digest,int retry){(void)r;(void)slot;(void)digest;(void)retry;assert(!strcmp(name,"TEST"));assert(feedback>erases);erases++;return PFS_OK;}
#include "callbacks.inc"
int main(void){pdg_gate g={0};pdp_ops ops={0,br_delete_review,br_delete_perform};uint8_t b[63]={0},reply[65];unsigned n;b[0]=1;b[16]=2;es_directory[2].state=1;
 br_playing=1;assert(pdp_dispatch(&g,&ops,PDP_REVIEW,b,18,1,reply,&n)&&n==20&&reply[16]==243&&erases==0);br_playing=0;
 assert(pdp_dispatch(&g,&ops,PDP_REVIEW,b,18,2,reply,&n)&&n==65);memcpy(b+18,reply+20,13);memcpy(b+31,reply+33,32);
 assert(pdp_dispatch(&g,&ops,PDP_COMMIT,b,63,3,reply,&n)&&n==20&&erases==1&&feedback==1&&es_directory[2].state==0&&br_ui.redraw);
 assert(pdp_dispatch(&g,&ops,PDP_COMMIT,b,63,4,reply,&n)&&erases==1&&feedback==1);
 memset(&g,0,sizeof g);b[0]=2;assert(pdp_dispatch(&g,&ops,PDP_REVIEW,b,18,5,reply,&n)&&n==65);memcpy(b+18,reply+20,13);memcpy(b+31,reply+33,32);
 br_playing=1;assert(pdp_dispatch(&g,&ops,PDP_COMMIT,b,63,6,reply,&n)&&reply[16]==243&&erases==1&&feedback==1);br_playing=0;
 assert(pdp_dispatch(&g,&ops,PDP_COMMIT,b,63,7,reply,&n)&&reply[16]==243&&erases==1);
 puts("PASS: target delete callbacks check busy state, show feedback, and invalidate the slot");return 0;}
