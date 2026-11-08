/* Extracted production raw callbacks with simulated flash; never hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BR_PROJECT_BASE 0x97000u
#define BR_PROJECT_END 0xb1000u
#define PS_SECTOR 4096u
#define PS_TOTAL_SLOTS 13u
#define FL_IN(off,n,lo,hi) ((uint32_t)(off)>=(lo) && (uint32_t)(off)<=(hi) && (uint32_t)(n)<=(hi)-(uint32_t)(off))
#define FL_FAR(name) mock_##name
#define BR_PATTERN_RAM_RANGE(p,n) ((p) != NULL && (n) != 0u)
static int flash_ok=1, fail_read, fail_write, fail_erase, br_playing, br_diagnostic_restore_in_progress=1;
static uint32_t br_ps_target_reserved_sectors;
static int parked=1, inject_loss;
static int br_ps_target_parked(void *v){(void)v;return parked && !br_playing && br_diagnostic_restore_in_progress;}
static int br_ps_project_watch_ok_locked(void){return 1;}
static unsigned irq_depth, mask_calls, unmask_calls, reads, writes, erases;
static uint8_t flash_sim[BR_PROJECT_END-BR_PROJECT_BASE];
static uint32_t irq_save(void){assert(!irq_depth);irq_depth=1;mask_calls++;if(inject_loss) parked=0;return 0x1234;}
static void irq_restore(uint32_t v){assert(irq_depth && v==0x1234);irq_depth=0;unmask_calls++;}
static int mock_fl_read_ram(uint32_t off,uint8_t *dst,uint32_t n){assert(irq_depth);reads++;if(fail_read)return -9;memcpy(dst,flash_sim+(off-BR_PROJECT_BASE),n);return 0;}
static int fl_write(uint32_t off,const uint8_t *src,uint32_t n){writes++;if(fail_write)return -8;memcpy(flash_sim+(off-BR_PROJECT_BASE),src,n);return 0;}
static int fl_erase4k(uint32_t off,uint32_t *took){erases++;*took=1;if(fail_erase)return -7;memset(flash_sim+(off-BR_PROJECT_BASE),255,PS_SECTOR);return 0;}
#include "target_raw_extracted.inc"
int main(void){uint8_t in[257],out[257];memset(in,0x5a,sizeof in);memset(flash_sim,255,sizeof flash_sim);
 assert(!br_ps_target_program_raw(0,BR_PROJECT_BASE+1,in,255));
 assert(!br_ps_target_read_raw(0,BR_PROJECT_BASE+1,out,255));assert(!memcmp(in,out,255));
 assert(mask_calls==unmask_calls && !irq_depth);
 {unsigned r=reads,w=writes,e=erases;inject_loss=1;
  assert(br_ps_target_read_raw(0,BR_PROJECT_BASE,out,1)<0 && reads==r);parked=1;
  assert(br_ps_target_program_raw(0,BR_PROJECT_BASE,in,1)<0 && writes==w);parked=1;
  assert(br_ps_target_erase_raw(0,BR_PROJECT_BASE)<0 && erases==e);parked=1;inject_loss=0;}
 flash_sim[PS_SECTOR+300]=0; assert(br_ps_target_program_raw(0,BR_PROJECT_BASE+PS_SECTOR,in,1)<0);
 br_playing=1; assert(br_ps_target_program_raw(0,BR_PROJECT_BASE,in,1)<0); assert(br_ps_target_read_raw(0,BR_PROJECT_BASE,out,1)<0); assert(br_ps_target_erase_raw(0,BR_PROJECT_BASE)<0); br_playing=0;
 br_diagnostic_restore_in_progress=0; assert(br_ps_target_program_raw(0,BR_PROJECT_BASE,in,1)<0); br_diagnostic_restore_in_progress=1;
 assert(br_ps_target_read_raw(0,BR_PROJECT_BASE,out,257)<0);
 assert(br_ps_target_program_raw(0,BR_PROJECT_END-1,in,2)<0);
 assert(br_ps_target_erase_raw(0,BR_PROJECT_END-PS_SECTOR+1)<0);
 assert(br_ps_target_may_erase(0,BR_PROJECT_BASE)==0);
 fail_read=1;assert(br_ps_target_read_raw(0,BR_PROJECT_BASE,out,1)==-9);assert(mask_calls==unmask_calls && !irq_depth);
 fail_write=1;assert(br_ps_target_program_raw(0,BR_PROJECT_BASE,in,1)==-8);
 fail_erase=1;assert(br_ps_target_erase_raw(0,BR_PROJECT_BASE)==-7);
 flash_ok=0;assert(br_ps_target_read_raw(0,BR_PROJECT_BASE,out,1)<0);
 assert(br_ps_target_program_raw(0,BR_PROJECT_BASE,in,1)<0 && br_ps_target_erase_raw(0,BR_PROJECT_BASE)<0);
 assert(reads>2 && writes==2 && erases==1 && mask_calls==unmask_calls && !irq_depth);
 puts("target raw callback bounds, IRQ restore and error propagation passed (mock)");return 0;}
