/* Host-only fake NOR exercise of the experimental exact-identity deletion. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pattern_pair_owner.h"
#include "pattern_sha256.h"
#define TOTAL (PS_TOTAL_SLOTS*2u*PS_SECTOR)
#define LEN 64u
static uint8_t flash[TOTAL],baseline[TOTAL],interrupted[TOTAL],unreadable[TOTAL],scratch[256],owner_scratch[256],output[LEN];
static pps_owner owner;static pps_owner_lease lease;static br_pattern_park_io park;
static int cut=-1,which=-1,erasures,guard_live=1,parked=1,authority=1,guard_calls,guard_revoke_at=-1,bit_pattern=-1,revoke_kind=0;
static int ok(void *v){(void)v;return 0;}static int isparked(void *v){(void)v;return parked;}static void hold(void *v){(void)v;}static int contained(void *v){(void)v;return 1;}
static int rd(void *v,uint32_t off,void *dst,uint32_t n){(void)v;if(off>TOTAL||n>TOTAL-off)return -1;memcpy(dst,flash+off,n);return 0;}
static int pr(void *v,uint32_t off,const void *src,uint32_t n){const uint8_t *p=src;(void)v;if(off>TOTAL||n>TOTAL-off)return -1;for(uint32_t i=0;i<n;i++){if((flash[off+i]&p[i])!=p[i])return -1;flash[off+i]&=p[i];}return 0;}
static int er(void *v,uint32_t off){(void)v;assert(!park.owner_claim||(guard_live&&parked&&lease.active));assert(off%PS_SECTOR==0&&off<=TOTAL-PS_SECTOR);if(erasures++==which){if(bit_pattern>=0){uint32_t x=(uint32_t)bit_pattern+1u;for(unsigned i=0;i<PS_SECTOR;i++){x=x*1664525u+1013904223u;flash[off+i]|=(uint8_t)(x>>24);}}else memset(flash+off,255,(unsigned)cut);return -1;}memset(flash+off,255,PS_SECTOR);return 0;}
static int may(void *v,uint32_t off){(void)v;return authority&&pps_owner_may_erase(&owner,off);}
static int guard(void *v){(void)v;if(++guard_calls==guard_revoke_at){if(revoke_kind==1)parked=0;else if(revoke_kind==2)lease.active=0;else guard_live=0;}return guard_live;}
static int src(void *v,uint32_t off,void *dst,uint32_t n){if(off>LEN||n>LEN-off)return -1;memcpy(dst,(uint8_t*)v+off,n);return 0;}
static int valid(void *v,pps_read_at read,void *ctx,uint32_t n){uint8_t b[LEN];(void)v;return n==LEN&&!read(ctx,0,b,n);}
static pps_store raw={.read=rd,.program=pr,.erase=er,.role={1,2,3,4},.validate=valid};
static br_pattern_park_ops ops={0,ok,ok,ok,isparked,rd,pr,er,may,ok,ok,hold,contained};
static void start(void){uint8_t role[16]={1,2,3,4};memset(&owner,0,sizeof owner);memset(&park,0,sizeof park);memset(owner_scratch,0,sizeof owner_scratch);lease=(pps_owner_lease){.token=1,.active=1};assert(br_pattern_park_begin(&park,&ops,0,TOTAL)==BR_PARK_OK);assert(pps_owner_bind(&owner,&park,&lease,1,0,role,valid,0,owner_scratch));}
static void stop(void){pps_owner_end(&owner);if(!br_pattern_park_faulted(&park))assert(br_pattern_park_end(&park)==BR_PARK_OK);}
int main(void){uint8_t old[LEN],newer[LEN],digest[32],wrong[32];char name[PS_NAME_BYTES]={0};fm1_pattern_sha256 sha;uint32_t n;int rc;
 memset(old,0x31,LEN);memset(newer,0x72,LEN);memcpy(name,"NEW",3);memset(flash,255,TOTAL);assert(pps_save(&raw,0,"OLD",LEN,src,old,scratch,0)==PS_OK);assert(pps_save(&raw,0,"NEW",LEN,src,newer,scratch,0)==PS_OK);memcpy(baseline,flash,TOTAL);
 assert(!fm1_pattern_sha256_init(&sha)&&!fm1_pattern_sha256_update(&sha,newer,LEN)&&!fm1_pattern_sha256_final(&sha,digest));memcpy(wrong,digest,32);wrong[0]^=1;
 start();assert(pps_owner_delete_experiment(&owner,name,wrong,guard,0)==PS_INVALID);assert(!memcmp(flash,baseline,TOTAL));stop();
 start();guard_live=0;assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_INVALID);guard_live=1;assert(!memcmp(flash,baseline,TOTAL));stop();
 {char badname[PS_NAME_BYTES]={0};memcpy(badname,"OTHER",5);start();assert(pps_owner_delete_experiment(&owner,badname,digest,guard,0)==PS_INVALID);assert(!memcmp(flash,baseline,TOTAL));stop();}
 memcpy(flash,baseline,TOTAL);flash[5]^=1;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_CORRUPT);assert(!memcmp(flash+PS_SECTOR,baseline+PS_SECTOR,TOTAL-PS_SECTOR));stop();
 memcpy(flash,baseline,TOTAL);authority=0;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_IO);assert(!memcmp(flash,baseline,TOTAL));stop();authority=1;
 memcpy(flash,baseline,TOTAL);memset(flash,255,PS_SECTOR);which=-1;erasures=0;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_OK);assert(erasures==1);stop();assert(pps_load(&raw,0,output,LEN,&n,scratch,0)==PS_EMPTY);
 memcpy(flash,baseline,TOTAL);
 for(int phase=0;phase<2;phase++)for(int i=0;i<=(int)PS_SECTOR;i++){memcpy(flash,baseline,TOTAL);which=phase;cut=i;erasures=0;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_IO);stop();which=-1;rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);if(rc==PS_OK)assert(n==LEN&&!memcmp(output,newer,LEN));assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));}
 /* A torn first erase preserves the latest project but blocks a safe retry:
  * the damaged older sector is indistinguishable from foreign contents. */
 memcpy(flash,baseline,TOTAL);which=0;cut=100;erasures=0;start();
 assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_IO);stop();which=-1;
 assert(pps_load(&raw,0,output,LEN,&n,scratch,0)==PS_OK && n==LEN && !memcmp(output,newer,LEN));
 erasures=0;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_CORRUPT);stop();assert(erasures==0);
 memcpy(interrupted,flash,TOTAL);
 /* An explicit retry still refuses the wrong survivor or a revoked fence. */
 erasures=0;start();assert(pps_owner_delete_retry_experiment(&owner,name,wrong,guard,0)==PS_INVALID);stop();assert(erasures==0);
 erasures=0;start();guard_live=0;assert(pps_owner_delete_retry_experiment(&owner,name,digest,guard,0)==PS_INVALID);guard_live=1;stop();assert(erasures==0);
 /* With fresh consent and the same verified survivor, it reclaims only this
  * slot and finishes the delete, leaving all other slots untouched. */
 erasures=0;start();assert(pps_owner_delete_retry_experiment(&owner,name,digest,guard,0)==PS_OK);stop();
 assert(erasures==2);assert(pps_load(&raw,0,output,LEN,&n,scratch,0)==PS_EMPTY);
 assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
 /* Interrupt the explicitly authorized retry at each byte of either erase. */
 for(int phase=0;phase<2;phase++)for(int i=0;i<=(int)PS_SECTOR;i++){
  memcpy(flash,interrupted,TOTAL);which=phase;cut=i;erasures=0;start();
  assert(pps_owner_delete_retry_experiment(&owner,name,digest,guard,0)!=PS_OK);stop();which=-1;
  rc=pps_load(&raw,0,output,LEN,&n,scratch,0);
  assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);
  if(rc==PS_OK)assert(n==LEN&&!memcmp(output,newer,LEN));
  assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
 }


 /* Also revoke the retry guard at every check on the successful path. */
 {int calls;memcpy(flash,interrupted,TOTAL);which=-1;guard_calls=0;guard_revoke_at=-1;erasures=0;start();
  assert(pps_owner_delete_retry_experiment(&owner,name,digest,guard,0)==PS_OK);calls=guard_calls;stop();
  for(int checkpoint=1;checkpoint<=calls;checkpoint++){
   memcpy(flash,interrupted,TOTAL);guard_calls=0;guard_revoke_at=checkpoint;guard_live=1;erasures=0;start();
   assert(pps_owner_delete_retry_experiment(&owner,name,digest,guard,0)!=PS_OK);stop();guard_live=1;guard_revoke_at=-1;
   rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);
   if(rc==PS_OK)assert(n==LEN&&!memcmp(output,newer,LEN));
   assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
  }
 }
 for(int phase=0;phase<2;phase++)for(int seed=0;seed<128;seed++){
  memcpy(flash,interrupted,TOTAL);which=phase;bit_pattern=seed;erasures=0;start();
  assert(pps_owner_delete_retry_experiment(&owner,name,digest,guard,0)!=PS_OK);stop();which=-1;bit_pattern=-1;
  rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);
  if(rc==PS_OK)assert(n==LEN&&!memcmp(output,newer,LEN));
  assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
 }

 /* An interrupted final erase can leave no readable survivor. Clearing that
  * state is a separate action pinned to a digest of both complete sectors. */
 {uint8_t pair_digest[32],bad_pair[32];fm1_pattern_sha256 pair_sha;
  memcpy(flash,baseline,TOTAL);which=1;cut=100;erasures=0;start();
  assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_IO);stop();which=-1;
  assert(pps_load(&raw,0,output,LEN,&n,scratch,0)==PS_CORRUPT);memcpy(unreadable,flash,TOTAL);
  assert(!fm1_pattern_sha256_init(&pair_sha)&&!fm1_pattern_sha256_update(&pair_sha,flash,2u*PS_SECTOR)&&!fm1_pattern_sha256_final(&pair_sha,pair_digest));
  memcpy(bad_pair,pair_digest,32);bad_pair[0]^=1;erasures=0;start();
  assert(pps_owner_clear_unreadable_experiment(&owner,bad_pair,guard,0)==PS_INVALID);stop();
  assert(erasures==0&&!memcmp(flash,unreadable,TOTAL));
  erasures=0;start();guard_live=0;assert(pps_owner_clear_unreadable_experiment(&owner,pair_digest,guard,0)==PS_INVALID);guard_live=1;stop();assert(erasures==0);
  erasures=0;start();assert(pps_owner_clear_unreadable_experiment(&owner,pair_digest,guard,0)==PS_OK);stop();
  assert(pps_load(&raw,0,output,LEN,&n,scratch,0)==PS_EMPTY);
  assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
  for(int i=0;i<=(int)PS_SECTOR;i++){
   memcpy(flash,unreadable,TOTAL);which=0;cut=i;erasures=0;start();
   assert(pps_owner_clear_unreadable_experiment(&owner,pair_digest,guard,0)!=PS_OK);stop();which=-1;
   rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_EMPTY||rc==PS_CORRUPT);
   assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
  }
  /* The raw fingerprint can never authorize clearing a readable project. */
  memcpy(flash,baseline,TOTAL);erasures=0;start();assert(pps_owner_clear_unreadable_experiment(&owner,pair_digest,guard,0)==PS_INVALID);stop();assert(erasures==0);
 }
 memcpy(flash,baseline,TOTAL);which=-1;erasures=0;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_OK);assert(erasures==2);stop();assert(pps_load(&raw,0,output,LEN,&n,scratch,0)==PS_EMPTY);assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));
 for(int phase=0;phase<2;phase++)for(int seed=0;seed<128;seed++){memcpy(flash,baseline,TOTAL);which=phase;bit_pattern=seed;erasures=0;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_IO);stop();which=-1;bit_pattern=-1;rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);if(rc==PS_OK)assert(!memcmp(output,newer,LEN));assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));}
 {int calls;memcpy(flash,baseline,TOTAL);which=-1;erasures=0;guard_calls=0;guard_revoke_at=-1;start();assert(pps_owner_delete_experiment(&owner,name,digest,guard,0)==PS_OK);calls=guard_calls;stop();
 for(int cutguard=1;cutguard<=calls;cutguard++){memcpy(flash,baseline,TOTAL);guard_calls=0;guard_revoke_at=cutguard;guard_live=1;erasures=0;start();rc=pps_owner_delete_experiment(&owner,name,digest,guard,0);assert(rc!=PS_OK);stop();guard_live=1;guard_revoke_at=-1;rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);if(rc==PS_OK)assert(!memcmp(output,newer,LEN));assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));}}
 for(int kind=1;kind<=2;kind++)for(int checkpoint=1;checkpoint<150;checkpoint++){memcpy(flash,baseline,TOTAL);which=-1;guard_calls=0;guard_revoke_at=checkpoint;revoke_kind=kind;guard_live=1;parked=1;erasures=0;start();rc=pps_owner_delete_experiment(&owner,name,digest,guard,0);if(guard_calls>=checkpoint)assert(rc!=PS_OK);parked=1;stop();revoke_kind=0;guard_revoke_at=-1;rc=pps_load(&raw,0,output,LEN,&n,scratch,0);assert(rc==PS_OK||rc==PS_EMPTY||rc==PS_CORRUPT);if(rc==PS_OK)assert(!memcmp(output,newer,LEN));assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));}
 puts("PASS: fenced exact-identity experimental delete, erase-prefix cuts and guard revocations");return 0;}
