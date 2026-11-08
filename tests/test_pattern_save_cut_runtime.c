/* Host simulation: real v4 runtime across interrupted named/working saves. */
#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
int main(void){
 static const int cuts[]={0,1,127,255,256,511,512,767,768,1023,1024,1279,1280,1535,1536,1791,1792,2047,2048,2303,2304,2559,2560,2815,2816,3071,3072,3327,3328,3583,3584,3839,3840,4095,4096,4608,5120,6144,7168,8192};
 br_session_t old_session,new_session,recovered_session;
 fm1_fixed_runtime_port port={0};port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;mutable_port=&port;
 for(unsigned i=0;i<sizeof cuts/sizeof cuts[0];i++){
  init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
  assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
  unsigned oldtempo=br_bpm;assert(br_capture(&old_session));memcpy(pair_before,pair_flash,sizeof pair_flash);br_bpm=oldtempo==111?112:111;assert(br_capture(&new_session));
  pair_budget=cuts[i];int rc=fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN");pair_budget=-1;
  assert(rc==PFS_OK || rc==FM1_PROJECT_WORKING_FAILED || rc==PFS_IO || rc==FM1_PROJECT_ROLLBACK_FAILED);
  assert(!memcmp(pair_flash+2u*PS_SECTOR,pair_before+2u*PS_SECTOR,22u*PS_SECTOR));
  /* Model volatile runtime reset at power-on; preserve all flash bytes. */
  memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);
  assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);br_bpm=45;
  assert(fm1_fixed_bridge_recover(&runtime.bridge)==PFS_OK);
  assert(br_capture(&recovered_session));
  assert(!memcmp(&recovered_session,&old_session,sizeof old_session)||!memcmp(&recovered_session,&new_session,sizeof new_session));
  assert(br_pattern_snapshot_capture(&cmp_a)==0&&!memcmp(&cmp_a,&src_live,sizeof cmp_a));
  assert(br_bpm==oldtempo || br_bpm==111);if(rc==PFS_OK)assert(br_bpm==111);
 }
 /* Full-partition restore on simulated NOR only, then real runtime recovery. */
 init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
 { unsigned savedtempo=br_bpm;memcpy(pair_before,pair_flash,sizeof pair_flash);
   memset(pair_flash,255,sizeof pair_flash);memcpy(pair_flash,pair_before,sizeof pair_flash);
   memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);
   assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);br_bpm=45;
   assert(fm1_fixed_bridge_recover(&runtime.bridge)==PFS_OK && br_bpm==savedtempo);
   assert(!memcmp(pair_flash,pair_before,sizeof pair_flash));
   verify_saved(0,777);verify_saved(12,778); }
 printf("PASS: full simulated partition restore and %zu real-v4 runtime interrupted-save cut points; other 11 projects unchanged; working recovery old or complete new\n",sizeof cuts/sizeof cuts[0]);
}
