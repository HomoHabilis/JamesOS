/* Shared host-only fixture for experimental pattern storage regressions. */
#include "pattern_storage_codec.h"
#include "pattern_codec_source.h"
#include "pattern_compressed_project.h"
#include "pattern_fixed_project.h"
#include "pattern_fixed_stage.h"
#include "pattern_fixed_target.h"
#include "pattern_pair_project.h"
#include "pattern_pair_bridge.h"
#include "pattern_fixed_authority.h"
#include "project_store.h"
/* Host-only real-session, example-bank and simulated restart integration. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#define BR_PATTERN_SNAPSHOT_MONOLITHIC_TEST 1
#include "../firmware/src/pattern_snapshot.c"
#include "../firmware/src/pattern_owner.c"
#include "pattern_project_adapter.h"
#include "pattern_project_host_bridge.h"
#include "pattern_project_target_bridge.h"
#include "pattern_project.h"
#include "pattern_flash_store.h"
#include "example_song_import.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int fixed_stage_safe=1,fixed_stage_fault;
static uint8_t pair_flash[26u*PS_SECTOR],pair_before[sizeof pair_flash],pair_encoded[PPS_MAX_PAYLOAD],pair_scratch[256],pair_written[26u*16u];
static int pair_budget=-1,pair_program_calls,pair_work_initializing,pair_read_calls;
static int pair_rd(void *c,uint32_t a,void *p,uint32_t n){(void)c;pair_read_calls++;if(a>sizeof pair_flash||n>sizeof pair_flash-a)return -1;memcpy(p,pair_flash+a,n);return 0;}
static int pair_erase(void *c,uint32_t a){(void)c;if(a%PS_SECTOR||a>sizeof pair_flash-PS_SECTOR)return -1;memset(pair_flash+a,255,PS_SECTOR);memset(pair_written+a/256u,0,16);return 0;}
/* Host journal model only: partial page writes here are NOT target-qualified. */
static uint8_t authority_scratch[PS_SECTOR];
static int authority_reserved(void *v,uint32_t a){(void)v;return !(a%PS_SECTOR)&&a<26u*PS_SECTOR;}
static int authority_program(void *v,uint32_t a,const void *p,uint32_t n){const uint8_t *b=p;(void)v;if(a>sizeof pair_flash||n>sizeof pair_flash-a)return -1;for(uint32_t i=0;i<n;i++){if((pair_flash[a+i]&b[i])!=b[i])return -1;pair_flash[a+i]&=b[i];}return 0;}
static int authority_ready(void *v,uint32_t slot){return fm1_pair_bridge_first_valid(v,slot);}
static int authority_fence(void *v,uint32_t token){(void)v;return token==37u&&fixed_stage_safe;}
static int pair_pg(void *c,uint32_t a,const void *p,uint32_t n){const uint8_t *b=p;(void)c;if(n!=256||a%256||a>sizeof pair_flash-256||pair_written[a/256])return -1;pair_written[a/256]=1;pair_program_calls++;for(uint32_t i=0;i<n;i++){if(pair_budget==0)return -1;if((pair_flash[a+i]&b[i])!=b[i])return -1;pair_flash[a+i]&=b[i];if(pair_budget>0)pair_budget--;}return 0;}
static uint8_t fixed_flash[26u*PS_SECTOR], fixed_before[sizeof fixed_flash], fixed_payload[FM1_FIXED_PAYLOAD], fixed_verify[FM1_FIXED_PAYLOAD];
static int fixed_allow=1, fixed_budget=-1;
static int fixed_rd(void *c,uint32_t a,void *p,uint32_t n){(void)c;if(a>sizeof fixed_flash||n>sizeof fixed_flash-a)return -1;memcpy(p,fixed_flash+a,n);return 0;}
static int fixed_erase(void *c,uint32_t a){(void)c;if(a%PS_SECTOR||a>sizeof fixed_flash-PS_SECTOR)return -1;memset(fixed_flash+a,255,PS_SECTOR);return 0;}
static int fixed_pg(void *c,uint32_t a,const void *p,uint32_t n){const uint8_t *b=p;(void)c;if(a>sizeof fixed_flash||n>sizeof fixed_flash-a)return -1;for(uint32_t i=0;i<n;i++){if(fixed_budget==0)return -1;if((fixed_flash[a+i]&b[i])!=b[i])return -1;fixed_flash[a+i]&=b[i];if(fixed_budget>0)fixed_budget--;}return 0;}
static int fixed_cert(void *c,uint32_t a){(void)c;return fixed_allow&&(a==0||a==PS_SECTOR||a==24u*PS_SECTOR||a==25u*PS_SECTOR);}
static uint8_t flash[PFS_SECTOR*PFS_COUNT], saved[sizeof flash], sector[PFS_SECTOR];
static uint8_t source_arena[16384],dest_arena[16384],wire[FM1_PROJECT_MAX],loaded[FM1_PROJECT_MAX],first_wire[FM1_PROJECT_MAX],enc[9000];
static size_t first_count;
static br_pattern_bank source,dest;
static br_pattern_snapshot src_live,src_work,dst_live,cmp_a,cmp_b;
static fm1_pattern_song src_song,dst_song;
static fm1_example_settings settings;
static fm1_project_work work;
static br_pattern_snapshot work_pattern,adapter_pattern;
static uint8_t work_session[FM1_PROJECT_SESSION],adapter_work_session[FM1_PROJECT_SESSION];
static uint8_t staging_arena[16384],record_scratch[FM1_PROJECT_RECORD_MAX];
static br_session_t session,restored,session_scratch;
static fm1_project_source stream;
static pfs_reader reader;
static int fail_after=-1,corrupt_staged_record;
static int reader_cb(void *ctx,uint32_t off,void *dst,uint32_t n){int rc;if(fail_after==0)return -1;if(fail_after>0)fail_after--;rc=pfs_reader_read_at(ctx,off,dst,n,sector);if(!rc&&corrupt_staged_record&&(uintptr_t)dst>=(uintptr_t)staging_arena&&(uintptr_t)dst-(uintptr_t)staging_arena<sizeof staging_arena&&n){((uint8_t*)dst)[n-1]^=1;corrupt_staged_record=0;}return rc;}
static int budget=-1,read_budget=-1;
static int rd(void *c,uint32_t p,void *v,uint32_t n){(void)c;if(read_budget==0)return -1;if(read_budget>0)read_budget--;if(p>sizeof flash||n>sizeof flash-p)return -1;memcpy(v,flash+p,n);return 0;}
static int pg(void *c,uint32_t p,const void *v,uint32_t n){const uint8_t *b=v;uint32_t i;(void)c;if(p>sizeof flash||n>sizeof flash-p)return -1;for(i=0;i<n;i++){if(budget==0)return -1;if((flash[p+i]&b[i])!=b[i])return -1;flash[p+i]&=b[i];if(budget>0)budget--;}return 0;}
static int valid(const uint8_t *v,size_t n,void *ctx){br_session_t tmp;(void)ctx;if(n!=sizeof tmp)return 0;memcpy(&tmp,v,n);return br_session_v16_valid(&tmp);}

static int fixed_safe(void *v){(void)v;return fixed_stage_safe;}
static int fixed_faulted(void *v){(void)v;return fixed_stage_fault;}
static unsigned recovery_adoptions;static int recovered_slot;
static int recovery_prepare_dummy(void *v,uint32_t slot,fm1_fixed_bridge_transaction *t){(void)v;(void)slot;(void)t;return 0;}
static int recovery_prepare(void *v,fm1_fixed_bridge_transaction *t){(void)v;(void)t;return fixed_stage_safe;}
static int recovery_open(void *v,uint32_t slot,fm1_fixed_bridge_access *a){(void)v;if(slot!=12)return 0;a->certify=fixed_cert;a->context=0;a->initializing=0;return 1;}
static int recovery_close(void *v,fm1_fixed_bridge_access *a){(void)v;(void)a;return 1;}
static int recovery_activate(void *v,uint32_t slot){(void)v;(void)slot;return 1;}
static int recovery_finish(void *v){(void)v;return fixed_stage_safe;}
static int recovery_adopt(void *v){(void)v;recovery_adoptions++;return 1;}
static int recovery_slot(void *v,int slot){(void)v;recovered_slot=slot;return 1;}
static int pair_prepare(void *v,uint32_t slot,fm1_fixed_bridge_transaction *t){(void)slot;t->source=&stream;t->write_pair=fm1_pair_bridge_write;t->write_ctx=v;return fixed_stage_safe;}
static int pair_prepare_recovery(void *v,fm1_fixed_bridge_transaction *t){(void)v;(void)t;return fixed_stage_safe;}
static int pair_open(void *v,uint32_t slot,fm1_fixed_bridge_access *a){(void)v;if(slot!=0&&slot!=12)return 0;a->certify=fixed_cert;a->context=0;a->initializing=(slot==12&&pair_work_initializing);return 1;}
static int pair_activate(void *v,uint32_t slot){(void)v;if(slot==12)pair_work_initializing=0;return 1;}
static int pair_recapture(void *v,fm1_fixed_bridge_transaction *t){return pair_prepare(v,0,t);}

static int recovery_authorized_flag;
static int target_authorized(void *v){(void)v;return recovery_authorized_flag;}
static int target_current(void *v,int *slot){(void)v;*slot=recovered_slot;return 1;}
static int target_scan(void *v,unsigned slot,char name[PS_NAME_BYTES]){(void)v;(void)slot;(void)name;return PFS_INVALID;}


static int fixed_preflight_ok=1;
static int fixed_preflight(void *c,const uint8_t *p,size_t n){(void)c;return fixed_preflight_ok&&fm1_compressed_project_decode(p,n,valid,0,&work,(uint8_t*)&restored,&dest,dest_arena,sizeof dest_arena,&dst_live,&dst_song)==FM1_PROJECT_OK;}
static uint8_t owner_arena[16384],owner_encoded[9000],adapter_arena[16384],adapter_stage[16384],adapter_sector[PFS_SECTOR];
static br_pattern_snapshot owner_live,owner_work,owner_clip;
static fm1_project_work adapter_work;
static fm1_project_adapter adapter;
static br_pattern_snapshot adapter_live,adapter_previous_live;
static uint8_t adapter_session[FM1_PROJECT_SESSION],adapter_previous[FM1_PROJECT_SESSION];
#include "pattern_session_loan.h"
static int quiet=1;
static int isquiet(void *ctx){(void)ctx;return quiet;}
static int export_owner(void *ctx,br_pattern_bank *b,br_pattern_snapshot *p,fm1_pattern_song *s){(void)ctx;return br_ps_host_export(b,p,s);}
static int can_owner(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *p,const fm1_pattern_song *s){(void)ctx;return br_ps_host_can_adopt(b,p,s);}
static int adopt_owner(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *p,const fm1_pattern_song *s){(void)ctx;return br_ps_host_adopt(b,p,s);}
static int can_copy(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *p,const fm1_pattern_song *s){(void)ctx;return br_ps_host_can_adopt_copy(b,p,s);}
static int adopt_copy(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *p,const fm1_pattern_song *s){(void)ctx;return br_ps_host_adopt_copy(b,p,s);}
static int reject_adopt(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *p,const fm1_pattern_song *s){(void)ctx;(void)b;(void)p;(void)s;return 0;}
static int restore_pattern(void *ctx,const br_pattern_snapshot *p){(void)ctx;return br_pattern_snapshot_apply(p)==0;}
static int restore_failure;
static int restore_calls,fail_on_call;
static int scripted_restore(const uint8_t *p,size_t n){if(++restore_calls==fail_on_call){br_bpm=33;br_seq_kit_note[0][0]=7;return 0;}return br_pattern_session_restore(p,n);}
static int injecting_restore(const uint8_t *p,size_t n){if(restore_failure){int mode=restore_failure;br_bpm=33;br_seq_kit_note[0][0]=7;if(mode==1)restore_failure=0;return 0;}return br_pattern_session_restore(p,n);}
static int deny_working;
static int empty_safe(void *ctx,const pfs_store *s,void *b,uint32_t id){(void)ctx;(void)s;(void)b;(void)id;return 1;}
static int slot_permission(void *ctx,uint32_t id,int write){(void)ctx;return id<=12&&!(id==12&&write&&deny_working);}
static int target_permission(void *ctx,const pfs_store *s,void *b,uint32_t id,int write){(void)s;(void)b;return slot_permission(ctx,id,write);}
static int init_new(void *ctx){(void)ctx;br_app_init();return br_ps_host_reset();}
static fm1_project_adapter_ops ops={0,isquiet,br_pattern_session_capture,br_pattern_session_validate,br_pattern_session_restore,export_owner,can_owner,adopt_owner,restore_pattern};
static const pfs_store *mock_target_store;
static uint32_t mock_target_token;
static br_session_t target_factory;
static int factory_copy(void *ctx,uint8_t *out,size_t n){(void)ctx;if(n!=sizeof target_factory)return 0;memcpy(out,&target_factory,n);return 1;}
int br_ps_target_project_begin(int mode,fm1_project_adapter **a,const fm1_project_adapter_ops **o,uint32_t *token){if(mock_target_token||!quiet)return 0;mock_target_token=123;adapter.live=mode==1?adapter_work.pattern:&adapter_live;*a=&adapter;*o=&ops;*token=mock_target_token;return 1;}
int br_ps_target_project_mode(uint32_t token,int loading){if(token!=mock_target_token)return 0;adapter.live=loading?adapter_work.pattern:&adapter_live;return 1;}
int br_ps_target_project_store(uint32_t token,br_ps_target_store_handle *h){if(token!=mock_target_token||!mock_target_store)return 0;h->store=*mock_target_store;h->token=token;return 1;}
int br_ps_target_project_end(uint32_t token){if(token!=mock_target_token)return 0;mock_target_token=0;return 1;}
int br_ps_target_project_faulted(void){return 0;}
