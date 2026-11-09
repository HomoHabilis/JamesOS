/* Exercise the installed v4 page-stream pair store with torn NOR operations. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pattern_pair_stream.h"
#define TOTAL (PS_TOTAL_SLOTS * 2u * PS_SECTOR)
#define LENGTH 1000u
static uint8_t flash[TOTAL],baseline[TOTAL],old[LENGTH],newer[LENGTH],out[LENGTH],scratch[256];
static int budget=-1,erase_prefix=-1,read_count,fail_read_at=-1;
static int rd(void *ctx,uint32_t off,void *outp,uint32_t n) { (void)ctx; if(++read_count==fail_read_at||off>TOTAL||n>TOTAL-off)return -1;memcpy(outp,flash+off,n);return 0; }
static int erase(void *ctx,uint32_t off) { (void)ctx;assert(off%PS_SECTOR==0 && off<=TOTAL-PS_SECTOR);if(erase_prefix>=0){memset(flash+off,255,(unsigned)erase_prefix);return -1;}memset(flash+off,255,PS_SECTOR);return 0; }
static int program(void *ctx,uint32_t off,const void *inp,uint32_t n) { const uint8_t *p=inp;(void)ctx;assert(!(off%256u)&&n==256u&&off<=TOTAL-n);for(uint32_t i=0;i<n;i++){if(budget==0)return -1;if(budget>0)--budget;assert((flash[off+i]&p[i])==p[i]);flash[off+i]&=p[i];}return 0; }
static int source(void *ctx,uint32_t off,void *dst,uint32_t n){const uint8_t *p=ctx;if(off>LENGTH||n>LENGTH-off)return -1;memcpy(dst,p+off,n);return 0;}
static int valid(void *ctx,pps_read_at read,void *rctx,uint32_t n){uint8_t buf[64];(void)ctx;if(n!=LENGTH)return 0;for(uint32_t off=0;off<n;){uint32_t k=n-off<sizeof buf?n-off:sizeof buf;if(read(rctx,off,buf,k))return 0;off+=k;}return 1;}
static pps_store store={.read=rd,.program=program,.erase=erase,.role={1,2,3,4},.validate=valid};
static int save(const uint8_t *p,const char *name){return pps_save(&store,0,name,LENGTH,source,(void*)p,scratch,0);}
static void verify(const uint8_t *a,const uint8_t *b){uint32_t n=0;ps_info info;assert(pps_load(&store,0,out,sizeof out,&n,scratch,&info)==PS_OK);assert(n==LENGTH);assert(!memcmp(out,a,n)||(b&&!memcmp(out,b,n)));assert(!memcmp(flash+2*PS_SECTOR,baseline+2*PS_SECTOR,TOTAL-2*PS_SECTOR));}
int main(void){
 for(unsigned i=0;i<LENGTH;i++){old[i]=(uint8_t)(i*17u+3u);newer[i]=(uint8_t)(i*29u+7u);}
 memset(flash,255,sizeof flash);assert(save(old,"FIRST")==PS_OK);assert(save(old,"OLD")==PS_OK);memcpy(baseline,flash,TOTAL);
 {pps_reader reader;read_count=0;fail_read_at=17;assert(pps_reader_open(&store,0,&reader,scratch)==PS_IO);fail_read_at=-1;assert(!memcmp(flash,baseline,TOTAL));}
 for(int i=0;i<=PS_SECTOR;i++){memcpy(flash,baseline,TOTAL);erase_prefix=i;assert(save(newer,"NEW")==PS_IO);erase_prefix=-1;verify(old,0);}
 for(int i=0;i<=5*256;i++){memcpy(flash,baseline,TOTAL);budget=i;int rc=save(newer,"NEW");budget=-1;verify(old,newer);if(rc==PS_OK)assert(!memcmp(out,newer,LENGTH));}
 /* A partial overwrite preserves the valid copy but blocks later saves:
  * the unknown nonblank sibling is deliberately not erased. */
 memcpy(flash,baseline,TOTAL);budget=1;assert(save(newer,"NEW")==PS_IO);budget=-1;verify(old,0);
 { static uint8_t damaged[TOTAL];memcpy(damaged,flash,TOTAL);
   assert(save(newer,"RETRY")==PS_CORRUPT);assert(!memcmp(damaged,flash,TOTAL)); }
 /* A torn first write is never mistaken for an empty, reclaimable slot. */
 memset(flash,255,TOTAL);budget=1;assert(save(newer,"TORN")==PS_IO);budget=-1;uint32_t n=0;assert(pps_load(&store,0,out,sizeof out,&n,scratch,0)==PS_CORRUPT);assert(save(old,"RETRY")==PS_CORRUPT);
 puts("PASS: v4 pair survives every partial erase byte and 1281 program cut points; other project sectors unchanged; torn first save fails closed; retries safely blocked");
 return 0;
}
