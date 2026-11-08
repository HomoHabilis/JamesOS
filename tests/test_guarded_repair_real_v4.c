/* Host-only: repair experiment against real v4 project encoder/validator. */
#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#define reader pps_internal_reader
#include "../firmware/src/pattern_pair_stream.c"
#undef reader
#include "recovery/guarded_pair_repair.h"
static uint8_t archived[sizeof pair_flash],repair_scratch[256];
static int repair_cut=-1,repair_fault;
static int repair_fenced(void *p){(void)p;return live_fence(0);}
static int repair_proof(void *p,uint32_t slot,unsigned bad){(void)p;return slot==0&&bad==1&&live_fence(0);}
static int repair_archive(void *p,uint32_t off,void *dst,uint32_t n){(void)p;if(off>sizeof archived||n>sizeof archived-off)return -1;memcpy(dst,archived+off,n);return 0;}
static int repair_copy(void *p,uint32_t slot,unsigned copy){uint8_t head[256];pps_store *s=p;int r=inspect(s,slot,copy,repair_scratch,head);return r==PS_OK?1:r==PS_CORRUPT?0:-1;}
static void repair_faulted(void *p){(void)p;repair_fault=1;}
static int repair_erase(void *p,uint32_t a){(void)p;if(repair_cut>=0){memset(pair_flash+a,255,(unsigned)repair_cut);return -1;}return pair_erase(0,a);}
int main(void){
 const int cuts[]={0,1,255,256,511,1024,2048,3072,4095,4096,-1};
 fm1_fixed_runtime_port port={0};port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;mutable_port=&port;
 for(unsigned i=0;i<sizeof cuts/sizeof cuts[0];i++){
  init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
  assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);
  br_session_t original;assert(br_capture(&original));br_bpm=111;pair_budget=1;
  int r=fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN");pair_budget=-1;
  assert(r==FM1_PROJECT_ROLLBACK_FAILED||r==PFS_IO);
  memcpy(archived,pair_flash,sizeof archived);
  /* Model reboot into a recovery mode, retaining the NOR partition. */
  memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);
  assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
  assert(br_pattern_park_begin(&realpark,&parkops,0,26u*PS_SECTOR)==BR_PARK_OK);
  pps_store raw={0,pair_rd,pair_pg,repair_erase,0,{1,2,3,4},fm1_pair_project_validate,&pair_stage};
  jos_repair_model m={&raw,repair_fenced,repair_proof,repair_archive,repair_copy,repair_faulted,&raw};
  repair_cut=cuts[i];repair_fault=0;r=jos_repair_damaged_copy(&m,0,1,repair_scratch);
  if(repair_cut>=0)assert(r==JOS_REPAIR_FAULT&&repair_fault);else assert(r==JOS_REPAIR_OK&&!repair_fault);
  assert(!memcmp(pair_flash,archived,PS_SECTOR));
  assert(!memcmp(pair_flash+2u*PS_SECTOR,archived+2u*PS_SECTOR,sizeof archived-2u*PS_SECTOR));
  /* Model power-on after an interrupted recovery; preserve flash exactly. */
  memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);
  assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);br_bpm=45;
  assert(fm1_fixed_bridge_recover(&runtime.bridge)==PFS_OK);
  br_session_t recovered;assert(br_capture(&recovered)&&!memcmp(&original,&recovered,sizeof original));
  assert(br_pattern_snapshot_capture(&cmp_a)==0&&!memcmp(&cmp_a,&src_live,sizeof cmp_a));
  if(repair_cut<0){br_bpm=111;assert(fm1_fixed_bridge_save(&runtime.bridge,0,"PATTERN")==PFS_OK);}
 }
 puts("PASS: real-v4 named project repair and working recovery with interrupted erases; survivor session and pattern exact");return 0;
}
