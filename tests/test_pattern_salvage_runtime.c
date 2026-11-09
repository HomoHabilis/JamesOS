/* Host-only real-v4 runtime salvage: damaged sibling must not block reading
 * the valid survivor, an explicit save may replace the damaged copy. */
#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
int main(void){
 fm1_fixed_runtime_port port={0};port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
 /* A valid named project with an unknown damaged sibling is readable. */
 init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
 br_session_t expected_session;assert(br_capture(&expected_session));
 unsigned expected=br_bpm;pair_flash[PS_SECTOR+100]=0x42;memcpy(pair_before,pair_flash,sizeof pair_flash);
 {char name[PS_NAME_BYTES];assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_OK&&!strcmp(name,"PATTERN"));}
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));br_bpm=45;
 assert(fm1_fixed_bridge_load(&runtime.bridge,0)==PFS_OK && br_bpm==expected);
 assert(br_capture(&target_factory)&&!memcmp(&expected_session,&target_factory,sizeof expected_session));
 assert(br_pattern_snapshot_capture(&cmp_a)==0&&!memcmp(&cmp_a,&src_live,sizeof cmp_a));
 /* Read-only load refreshes the valid working pair; damaged named copy remains untouched. */
 assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));memcpy(pair_before,pair_flash,sizeof pair_flash);
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK&&!memcmp(pair_before,pair_flash,PS_SECTOR));
 /* A damaged working sibling still allows recovery and explicit save. */
 init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
 expected=br_bpm;assert(br_capture(&expected_session));pair_flash[25u*PS_SECTOR+100]=0x42;memcpy(pair_before,pair_flash,sizeof pair_flash);br_bpm=45;
 assert(fm1_fixed_bridge_recover(&runtime.bridge)==PFS_OK && br_bpm==expected);
 assert(br_capture(&target_factory)&&!memcmp(&expected_session,&target_factory,sizeof expected_session));
 assert(br_pattern_snapshot_capture(&cmp_a)==0&&!memcmp(&cmp_a,&src_live,sizeof cmp_a));
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);memcpy(pair_before,pair_flash,sizeof pair_flash);
 {br_session_t old_session;assert(br_capture(&old_session));
  assert(fm1_fixed_bridge_load(&runtime.bridge,0)==PFS_OK);
  assert(br_capture(&target_factory)&&!memcmp(&old_session,&target_factory,sizeof old_session));
  assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));}
 /* Simulate a restart after interrupted repair of the working sibling.
  * With residual bytes, recovery reads the survivor and writes stay denied;
  * a completely erased sibling is safe for normal saving again. */
 { const unsigned cuts[]={0,1,99,100,101,1024,2048,3499,3500,3501,4095,4096};
   for(unsigned i=0;i<sizeof cuts/sizeof cuts[0];++i){
    init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
    assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
    expected=br_bpm;assert(br_capture(&expected_session));
    pair_flash[25u*PS_SECTOR+100]=0x42;pair_flash[25u*PS_SECTOR+3500]=0x33;
    memset(pair_flash+25u*PS_SECTOR,255,cuts[i]);memcpy(pair_before,pair_flash,sizeof pair_flash);
    memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
    br_bpm=45;assert(fm1_fixed_bridge_recover(&runtime.bridge)==PFS_OK&&br_bpm==expected);
    assert(br_capture(&target_factory)&&!memcmp(&expected_session,&target_factory,sizeof expected_session));
    assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
    assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
   }}
 /* Two corrupt copies do not permit scan, load or writes. */
 init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
 pair_flash[PS_SECTOR+100]=0x42;pair_flash[PS_PAYLOAD_OFFSET+7]^=1;memcpy(pair_before,pair_flash,sizeof pair_flash);
 {char name[PS_NAME_BYTES];assert(fm1_fixed_runtime_scan(&runtime,0,name)!=PFS_OK);}
 assert(fm1_fixed_bridge_load(&runtime.bridge,0)!=PFS_OK);
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
 assert(!memcmp(pair_before+2u*PS_SECTOR,pair_flash+2u*PS_SECTOR,22u*PS_SECTOR));
 puts("PASS: survivors readable and explicit replacement recovers damaged records");return 0;
}
