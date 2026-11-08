/* Host-only, deliberately non-exported repair experiment; never access device. */
#define main interrupted_test_main
#include "test_pattern_pair_interrupted_save.c"
#undef main
#include "../firmware/src/pattern_pair_stream.c"
static uint8_t damaged[TOTAL];
/* The caller must hold an independently proved writer fence, own the sector,
 * and retain an independently verified complete backup. These are simulated
 * preconditions here, NOT established by the live FM-1 implementation. */
static int repair_model(const uint8_t *snapshot,unsigned slot,int authorized,int fenced){
 uint8_t h[2][256],check[256];int st[2],target,survivor;uint32_t off;
 if(!snapshot||slot>=PS_TOTAL_SLOTS||!authorized||!fenced)return PS_INVALID;
 for(off=0;off<TOTAL;off+=256){if(rd(0,off,check,256)||memcmp(check,snapshot+off,256))return PS_INVALID;}
 st[0]=inspect(&store,slot,0,scratch,h[0]);st[1]=inspect(&store,slot,1,scratch,h[1]);
 if(st[0]==PS_OK&&st[1]==PS_CORRUPT){survivor=0;target=1;}
 else if(st[1]==PS_OK&&st[0]==PS_CORRUPT){survivor=1;target=0;}
 else return PS_INVALID;
 /* Require backup to include an exact stable, valid survivor. Recheck it just
  * before the erase; no repair of a blank or a valid second record. */
 off=(slot*2u+(unsigned)target)*PS_SECTOR;
 {int allblank=1;for(uint32_t j=0;j<PS_SECTOR;j++)allblank&=snapshot[off+j]==255;if(allblank)return PS_INVALID;}
 {uint8_t again[256];if(inspect(&store,slot,(unsigned)survivor,scratch,again)!=PS_OK || memcmp(again,h[survivor],256))return PS_IO;}
 if(erase(0,off))return PS_IO;
 for(uint32_t j=0;j<PS_SECTOR;j+=256){if(rd(0,off+j,check,256))return PS_IO;for(unsigned k=0;k<256;k++)if(check[k]!=255)return PS_IO;}
 return inspect(&store,slot,(unsigned)survivor,scratch,h[survivor])==PS_OK?PS_OK:PS_IO;
}
int main(void){
 for(unsigned i=0;i<LENGTH;i++){old[i]=(uint8_t)(i*17u+3u);newer[i]=(uint8_t)(i*29u+7u);}
 memset(flash,255,TOTAL);assert(save(old,"FIRST")==PS_OK);assert(save(old,"OLD")==PS_OK);memcpy(baseline,flash,TOTAL);
 budget=1;assert(save(newer,"NEW")==PS_IO);budget=-1;memcpy(damaged,flash,TOTAL);verify(old,0);
 assert(repair_model(damaged,0,0,1)==PS_INVALID&&!memcmp(flash,damaged,TOTAL));
 assert(repair_model(damaged,0,1,0)==PS_INVALID&&!memcmp(flash,damaged,TOTAL));
 assert(repair_model(baseline,0,1,1)==PS_INVALID&&!memcmp(flash,damaged,TOTAL));
 for(int cut=0;cut<=PS_SECTOR;cut++){
  memcpy(flash,damaged,TOTAL);erase_prefix=cut;assert(repair_model(damaged,0,1,1)==PS_IO);erase_prefix=-1;verify(old,0);
  if(memcmp(flash,damaged,TOTAL)) {static uint8_t partial[TOTAL];memcpy(partial,flash,TOTAL);assert(repair_model(damaged,0,1,1)==PS_INVALID&&!memcmp(partial,flash,TOTAL));}
 }
 memcpy(flash,damaged,TOTAL);assert(repair_model(damaged,0,1,1)==PS_OK);verify(old,0);
 assert(save(newer,"AFTER")==PS_OK);verify(newer,0);
 memcpy(flash,baseline,TOTAL);assert(repair_model(baseline,0,1,1)==PS_INVALID&&!memcmp(flash,baseline,TOTAL));
 memset(flash,0,TOTAL);memcpy(damaged,flash,TOTAL);assert(repair_model(damaged,0,1,1)==PS_INVALID&&!memcmp(flash,damaged,TOTAL));
 puts("PASS: host-only approved repair model preserves valid survivor at 4097 erase cut points, denies stale/ambiguous input, allows later save");
}
