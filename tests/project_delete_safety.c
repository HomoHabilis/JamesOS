/* Evidence for the deletion format requirement: two raw erases are not an
 * interruption-safe delete protocol for the installed pair store. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pattern_pair_stream.h"
#define TOTAL (PS_TOTAL_SLOTS * 2u * PS_SECTOR)
#define LEN 64u
static uint8_t flash[TOTAL], initial[TOTAL], scratch[256], out[LEN];
static int rd(void *ctx,uint32_t off,void *dst,uint32_t n){(void)ctx;if(off>TOTAL||n>TOTAL-off)return -1;memcpy(dst,flash+off,n);return 0;}
static int er(void *ctx,uint32_t off){(void)ctx;if(off%PS_SECTOR||off>TOTAL-PS_SECTOR)return -1;memset(flash+off,255,PS_SECTOR);return 0;}
static int pr(void *ctx,uint32_t off,const void *src,uint32_t n){const uint8_t *p=src;(void)ctx;if(off>TOTAL||n>TOTAL-off)return -1;for(uint32_t i=0;i<n;i++){if((flash[off+i]&p[i])!=p[i])return -1;flash[off+i]&=p[i];}return 0;}
static int source(void *ctx,uint32_t off,void *dst,uint32_t n){if(off>LEN||n>LEN-off)return -1;memcpy(dst,(uint8_t*)ctx+off,n);return 0;}
static int valid(void *ctx,pps_read_at read,void *rctx,uint32_t n){uint8_t b[LEN];(void)ctx;return n==LEN&&!read(rctx,0,b,n);}
static pps_store store={.read=rd,.program=pr,.erase=er,.role={1,2,3,4},.validate=valid};
int main(void){uint8_t older[LEN],newer[LEN];uint32_t n;ps_info info;memset(older,0x31,LEN);memset(newer,0x72,LEN);memset(flash,255,TOTAL);
 assert(pps_save(&store,0,"OLD",LEN,source,older,scratch,0)==PS_OK);
 assert(pps_save(&store,0,"NEW",LEN,source,newer,scratch,0)==PS_OK);
 memcpy(initial,flash,TOTAL);
 /* Removing the selected copy resurrects the older project. */
 assert(er(0,PS_SECTOR)==0);assert(pps_load(&store,0,out,LEN,&n,scratch,&info)==PS_OK);assert(!memcmp(out,older,LEN));
 /* Removing the old copy first can be interrupted and leaves a nonblank,
  * damaged sibling that ordinary save is forbidden from erasing. */
 memcpy(flash,initial,TOTAL);memset(flash,255,16);assert(pps_load(&store,0,out,LEN,&n,scratch,&info)==PS_OK);assert(!memcmp(out,newer,LEN));assert(!pps_erase_allowed(&store,0,0,scratch));assert(pps_save(&store,0,"RETRY",LEN,source,older,scratch,0)==PS_CORRUPT);
 assert(!memcmp(flash+2*PS_SECTOR,initial+2*PS_SECTOR,TOTAL-2*PS_SECTOR));puts("PASS: raw deletion can resurrect old data or leave an unsafe-to-reclaim sibling");return 0;}
