/* Host-only: real pattern v4 + bridge + serialized owner handoff on fake NOR. */
#include "pattern_project_fixture.h"
#include "pattern_pair_owner.h"
static uint8_t owner_io[256];
static br_pattern_park_io realpark;
static pps_owner named_owner,working_owner;
static pps_owner_lease named_lease,working_lease;
static pps_store stale_store;
static fm1_pair_bridge pair_backend;
static fm1_fixed_stage pair_stage;
static unsigned park_releases,raw_erases,raw_writes,stale_rejected;
static int independent_authority,deny_handoff,force_unpark,lose_working_fence;
static int host_ok(void *v){(void)v;return 0;}
static int host_release(void *v){(void)v;park_releases++;return 0;}
static int host_parked(void *v){(void)v;return !force_unpark;}
/* This is a separately simulated reservation policy, not owner->admitted and
 * not a proof of physical-sector ownership. */
static int host_may_erase(void *v,uint32_t a){(void)v;return independent_authority&&(a<2u*PS_SECTOR||(a>=24u*PS_SECTOR&&a<26u*PS_SECTOR));}
static void host_hold(void *v){(void)v;}
static int host_contained(void *v){(void)v;return 1;}
static int raw_read(void *v,uint32_t a,void *p,uint32_t n){(void)v;return pair_rd(0,a,p,n);}
static int raw_program(void *v,uint32_t a,const void *p,uint32_t n){int rc;(void)v;raw_writes++;rc=pair_pg(0,a,p,n);if(lose_working_fence&&a>=24u*PS_SECTOR)force_unpark=1;return rc;}
static int raw_erase(void *v,uint32_t a){(void)v;raw_erases++;return pair_erase(0,a);}
static int live_fence(void *v){(void)v;return realpark.fenced==1&&!force_unpark;}
static int owner_fault(void *v){(void)v;return br_pattern_park_faulted(&realpark)||force_unpark;}
static int reservation_cert(void *v,uint32_t a){(void)v;return independent_authority&&live_fence(0)&&(a==0||a==PS_SECTOR||a==24u*PS_SECTOR||a==25u*PS_SECTOR);}
static br_pattern_park_ops parkops={0,host_ok,host_release,host_ok,host_parked,raw_read,raw_program,raw_erase,host_may_erase,host_ok,host_ok,host_hold,host_contained};
static int owner_prepare(void *v,uint32_t slot,fm1_fixed_bridge_transaction *t){uint8_t role[16]={1,2,3,4};(void)v;if(slot!=0||!independent_authority)return 0;memset(&realpark,0,sizeof realpark);memset(&named_owner,0,sizeof named_owner);memset(&working_owner,0,sizeof working_owner);named_lease=(pps_owner_lease){.token=101,.active=1};working_lease=(pps_owner_lease){.token=102,.active=1};if(br_pattern_park_begin(&realpark,&parkops,0,26u*PS_SECTOR)!=BR_PARK_OK)return -1;if(!pps_owner_bind(&named_owner,&realpark,&named_lease,101,slot,role,fm1_pair_project_validate,&pair_stage,owner_io))return -1;pair_backend.store=pps_owner_store(&named_owner);t->source=&stream;t->write_pair=fm1_pair_bridge_write;t->write_ctx=&pair_backend;return 1;}
static int owner_open(void *v,uint32_t slot,fm1_fixed_bridge_access *a){(void)v;if(!live_fence(0))return -1;if(slot==12){if(deny_handoff||!pps_owner_handoff_working(&named_owner,&working_owner,&working_lease,102,owner_io))return 0;pair_backend.store=pps_owner_store(&working_owner);{uint8_t probe[1];if(stale_store.read(stale_store.ctx,0,probe,1)!=0)stale_rejected++;}}else if(slot==0)stale_store=*pps_owner_store(&named_owner);else return 0;a->certify=reservation_cert;a->context=0;a->initializing=1;return 1;}
static int owner_close(void *v,fm1_fixed_bridge_access *a){(void)v;(void)a;return live_fence(0)?1:0;}
static int owner_activate(void *v,uint32_t slot){(void)v;(void)slot;return live_fence(0)?1:0;}
static int owner_finish(void *v){(void)v;pps_owner_end(&named_owner);pps_owner_end(&working_owner);return br_pattern_park_end(&realpark)==BR_PARK_OK;}
static void build_source(unsigned k){unsigned p;br_app_init();assert(br_pattern_session_bind(&session_scratch,sizeof session_scratch));work.pattern=&work_pattern;work.session=work_session;work.compressed=record_scratch;work.compressed_capacity=sizeof record_scratch;work.arena=staging_arena;work.arena_capacity=sizeof staging_arena;assert(fm1_example_song_import(k,&source,source_arena,sizeof source_arena,&src_live,&src_work,enc,sizeof enc,&src_song,&settings)==0);br_bpm=settings.tempo;for(p=0;p<BR_PARTS;p++){br_part[p].sound.preset=settings.preset[p];br_part[p].swing=settings.swing;}assert(!br_pattern_snapshot_apply(&src_live));assert(br_capture(&session));assert(fm1_project_source_init(&stream,(uint8_t*)&session,&source,&src_live,&src_song,valid,0,&work)==FM1_PROJECT_OK);pair_stage=(fm1_fixed_stage){0,0,valid,0,&work,(uint8_t*)&restored,&dest,dest_arena,sizeof dest_arena,&dst_live,&dst_song,live_fence,owner_fault,0};pair_backend=(fm1_pair_bridge){0,&pair_stage,pair_encoded,pair_scratch,live_fence,owner_fault,0};}
static void verify_saved(unsigned slot,uint32_t token){pps_owner owner={0};pps_owner_lease lease={.token=token,.active=1};uint8_t role[16]={1,2,3,4};char name[PS_NAME_BYTES];assert(br_pattern_park_begin(&realpark,&parkops,0,26u*PS_SECTOR)==BR_PARK_OK);assert(pps_owner_bind(&owner,&realpark,&lease,token,slot,role,fm1_pair_project_validate,&pair_stage,owner_io));pair_backend.store=pps_owner_store(&owner);if(slot==12){assert(fm1_pair_bridge_working(&pair_backend,reservation_cert,0,name)==1&&!strcmp(name,"W01"));}else assert(fm1_pair_bridge_stage(&pair_backend,slot,reservation_cert,0)==1);assert(!memcmp(&session,&restored,sizeof session)&&!memcmp(&src_live,&dst_live,sizeof src_live));for(unsigned p=0;p<11;p++)if(source.used&(1u<<p)){assert(br_pattern_bank_copy(&source,p,&src_live,&cmp_a)==BR_BANK_OK);assert(br_pattern_bank_copy(&dest,p,&dst_live,&cmp_b)==BR_BANK_OK);assert(!memcmp(&cmp_a,&cmp_b,sizeof cmp_a));}pps_owner_end(&owner);assert(br_pattern_park_end(&realpark)==BR_PARK_OK);}
static void reset_flash(void){memset(pair_flash,255,sizeof pair_flash);memset(pair_written,0,sizeof pair_written);pair_budget=-1;independent_authority=1;deny_handoff=force_unpark=lose_working_fence=0;park_releases=raw_erases=raw_writes=stale_rejected=0;}
#if !defined(FM1_OWNER_V4_FIXTURE_ONLY)
int main(void){unsigned k;fm1_fixed_bridge_ops ops={0};ops.prepare=owner_prepare;ops.open_pair=owner_open;ops.close_pair=owner_close;ops.activate=owner_activate;ops.finish=owner_finish;ops.named_slots=12;for(k=0;k<2;k++){fm1_fixed_bridge bridge={0};build_source(k);reset_flash();assert(fm1_fixed_bridge_bind(&bridge,&ops)==PFS_OK);assert(fm1_fixed_bridge_save(&bridge,0,"PATTERN")==PFS_OK);assert(park_releases==1&&stale_rejected==1&&raw_erases==2&&raw_writes>0&&!realpark.fenced);assert(!named_lease.active&&!working_lease.active);verify_saved(0,201u);verify_saved(12,202u);printf("v4 example %u: named + working saved under one park\n",k);}
 {fm1_fixed_bridge bridge={0};build_source(0);reset_flash();pair_flash[9u*PS_SECTOR+31]=0x3c;memcpy(pair_before,pair_flash,sizeof pair_flash);independent_authority=0;assert(fm1_fixed_bridge_bind(&bridge,&ops)==PFS_OK);assert(fm1_fixed_bridge_save(&bridge,0,"PATTERN")==FM1_PROJECT_BUSY&&!raw_erases&&!raw_writes&&!park_releases&&!memcmp(pair_before,pair_flash,sizeof pair_flash));assert(br_capture(&target_factory)&&!memcmp(&session,&target_factory,sizeof session));}
 {fm1_fixed_bridge bridge={0};build_source(0);reset_flash();deny_handoff=1;assert(fm1_fixed_bridge_bind(&bridge,&ops)==PFS_OK);assert(fm1_fixed_bridge_save(&bridge,0,"PATTERN")==FM1_PROJECT_WORKING_FAILED&&park_releases==1&&raw_erases==1);for(unsigned i=24u*PS_SECTOR;i<26u*PS_SECTOR;i++)assert(pair_flash[i]==255);assert(br_capture(&target_factory)&&!memcmp(&session,&target_factory,sizeof session));}
 {fm1_fixed_bridge bridge={0};build_source(0);reset_flash();pair_flash[24u*PS_SECTOR+77]=0x42;memcpy(pair_before,pair_flash,sizeof pair_flash);assert(fm1_fixed_bridge_bind(&bridge,&ops)==PFS_OK);assert(fm1_fixed_bridge_save(&bridge,0,"PATTERN")==FM1_PROJECT_ROLLBACK_FAILED&&bridge.fault&&park_releases==1&&!memcmp(pair_flash+24u*PS_SECTOR,pair_before+24u*PS_SECTOR,2u*PS_SECTOR));assert(br_capture(&target_factory)&&!memcmp(&session,&target_factory,sizeof session));}
 {fm1_fixed_bridge bridge={0};build_source(0);reset_flash();lose_working_fence=1;assert(fm1_fixed_bridge_bind(&bridge,&ops)==PFS_OK);assert(fm1_fixed_bridge_save(&bridge,0,"PATTERN")==FM1_PROJECT_ROLLBACK_FAILED&&bridge.fault&&realpark.fenced&&park_releases==0);assert(br_capture(&target_factory)&&!memcmp(&session,&target_factory,sizeof session));assert(pair_flash[24u*PS_SECTOR+PS_PAYLOAD_OFFSET]!=255);}
 puts("real v4 owner handoff, denied authority, failed handoff, fence loss and foreign working preservation passed");return 0;}

#endif /* !FM1_OWNER_V4_FIXTURE_ONLY */
