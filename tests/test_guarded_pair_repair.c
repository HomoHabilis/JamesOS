/* Host-only integration of repair contract and real v4 pair implementation. */
#define main original_interrupted_main
#include "test_pattern_pair_interrupted_save.c"
#undef main
#include "../firmware/src/pattern_pair_stream.c"
#include "recovery/guarded_pair_repair.h"
static uint8_t backup[TOTAL],saved_partial[TOTAL];
static int fenced=1,consent=1,faulted=0,proof_count=0,lose_after=-1,broken_backup=0,read_failure=0,backup_count=0,fail_backup_at=-1,copy_count=0,fail_copy_at=-1;
static int fence_cb(void *p){(void)p;return fenced;}
static int proof_cb(void *p,uint32_t slot,unsigned bad){(void)p;proof_count++;if(lose_after>0&&proof_count>=lose_after)fenced=0;return consent&&slot==0&&bad==0;}
static int backup_cb(void *p,uint32_t off,void *dst,uint32_t n){(void)p;backup_count++;if(broken_backup||(fail_backup_at>0&&backup_count>=fail_backup_at)||off>TOTAL||n>TOTAL-off)return -1;memcpy(dst,backup+off,n);return 0;}
static int copy_cb(void *p,uint32_t slot,unsigned copy){uint8_t h[256];int r;(void)p;copy_count++;if(read_failure||(fail_copy_at>0&&copy_count>=fail_copy_at))return -1;r=inspect(&store,slot,copy,scratch,h);return r==PS_OK?1:r==PS_CORRUPT?0:-1;}
static void fault_cb(void *p){(void)p;faulted=1;}
static jos_repair_model model={0,fence_cb,proof_cb,backup_cb,copy_cb,fault_cb,&store};
static void reset_controls(void){fenced=consent=1;faulted=proof_count=broken_backup=read_failure=0;lose_after=-1;erase_prefix=-1;backup_count=copy_count=0;fail_backup_at=fail_copy_at=-1;}
int main(void){
 for(unsigned i=0;i<LENGTH;i++){old[i]=(uint8_t)(i*17u+3u);newer[i]=(uint8_t)(i*29u+7u);}
 memset(flash,255,TOTAL);assert(save(old,"FIRST")==PS_OK);assert(save(old,"OLD")==PS_OK);memcpy(baseline,flash,TOTAL);
 budget=1;assert(save(newer,"TORN")==PS_IO);budget=-1;memcpy(backup,flash,TOTAL);reset_controls();
 /* A missing fence/consent, invalid slot, invalid survivor, or changed
  * archive must never reach erase. */
 consent=0;assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_DENIED&&!memcmp(flash,backup,TOTAL));
 reset_controls();fenced=0;assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_DENIED&&!memcmp(flash,backup,TOTAL));
 reset_controls();assert(jos_repair_damaged_copy(&model,0,1,scratch)==JOS_REPAIR_DENIED&&!memcmp(flash,backup,TOTAL));
 reset_controls();backup[2*PS_SECTOR]^=1;assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_DENIED&&!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));backup[2*PS_SECTOR]^=1;
 reset_controls();read_failure=1;assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_DENIED&&!memcmp(flash,backup,TOTAL));
 reset_controls();broken_backup=1;assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_FAULT&&faulted&&!memcmp(flash,backup,TOTAL));
 /* Loss of the fence at several pre-erase phases latches quarantine. */
 for(int lost=2;lost<420;lost+=73){reset_controls();lose_after=lost;int r=jos_repair_damaged_copy(&model,0,0,scratch);assert((r==JOS_REPAIR_DENIED||r==JOS_REPAIR_FAULT)&&!memcmp(flash,backup,TOTAL));}
 /* A fence or backup/read failure after erase must quarantine the system,
  * retain the valid survivor, and never touch other projects. */
 {const int lost_points[]={420,421,422,425,450,500,700};int after_erase=0;
  for(unsigned i=0;i<sizeof lost_points/sizeof lost_points[0];i++){
   memcpy(flash,backup,TOTAL);reset_controls();lose_after=lost_points[i];
   int r=jos_repair_damaged_copy(&model,0,0,scratch);
   assert(r!=JOS_REPAIR_OK && faulted && !memcmp(flash+PS_SECTOR,backup+PS_SECTOR,TOTAL-PS_SECTOR));
   after_erase|=memcmp(flash,backup,PS_SECTOR)!=0;verify(old,0);
  }assert(after_erase);}
 memcpy(flash,backup,TOTAL);reset_controls();fail_backup_at=430;
 assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_FAULT&&faulted);verify(old,0);
 memcpy(flash,backup,TOTAL);reset_controls();fail_copy_at=5;
 assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_FAULT&&faulted);verify(old,0);
 memcpy(flash,backup,TOTAL);
 /* Exhaust every modeled erase tear. No retry and no other-sector mutation. */
 for(int cut=0;cut<=PS_SECTOR;cut++){
  memcpy(flash,backup,TOTAL);reset_controls();erase_prefix=cut;
  assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_FAULT&&faulted);
  erase_prefix=-1;assert(!memcmp(flash+PS_SECTOR,backup+PS_SECTOR,TOTAL-PS_SECTOR));verify(old,0);
  if(memcmp(flash,backup,TOTAL)){memcpy(saved_partial,flash,TOTAL);reset_controls();assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_DENIED&&!memcmp(saved_partial,flash,TOTAL));}
 }
 memcpy(flash,backup,TOTAL);reset_controls();assert(jos_repair_damaged_copy(&model,0,0,scratch)==JOS_REPAIR_OK&&!faulted);verify(old,0);
 assert(save(newer,"AFTER")==PS_OK);verify(newer,0);
 puts("PASS: guarded pair repair with real pair inspection, complete backup match, fence/consent failures and 4097 partial-erase cuts");return 0;
}
