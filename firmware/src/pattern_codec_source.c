#include "pattern_codec_source.h"
#include "pattern_storage_codec.h"
#include <string.h>

#define CACHESIZE 2048u
#define SOURCE_BLOCK 128u
#define TABLESIZE 512u
#define WINDOW 1024u
#define HEADER 12u
typedef struct { fm1_codec_source_read read; void *ctx; uint32_t size,end; uint8_t data[CACHESIZE]; int error; } cache;
static uint8_t get(cache *c,uint32_t i){uint32_t n;if(i>=c->size){c->error=1;return 0;}while(i>=c->end){n=c->size-c->end;if(n>SOURCE_BLOCK)n=SOURCE_BLOCK;if(c->read(c->ctx,c->end,c->data+(c->end%CACHESIZE),n)){c->error=1;return 0;}c->end+=n;}if(c->end-i>CACHESIZE){c->error=1;return 0;}return c->data[i%CACHESIZE];}
static uint32_t hash(cache *c,uint32_t i){return ((uint32_t)get(c,i)*251u+(uint32_t)get(c,i+1u)*31u+get(c,i+2u))&(TABLESIZE-1u);}
static uint32_t crc_byte(uint32_t c,uint8_t b){unsigned i;c^=b;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));return c;}
static void emit(uint8_t *out,size_t cap,size_t *at,uint8_t b){if(out&&*at<cap)out[*at]=b;++*at;}
static void literals(uint8_t *out,size_t cap,size_t *at,uint8_t *buf,size_t *count){size_t j;if(!*count)return;emit(out,cap,at,(uint8_t)(*count-1u));for(j=0;j<*count;j++)emit(out,cap,at,buf[j]);*count=0;}
static int pass(fm1_codec_source_read read,void *ctx,uint32_t n,uint8_t *out,size_t cap,size_t *written){
 uint32_t table[TABLESIZE],i=0,crc=~0u,j;uint8_t lit[64];size_t pos=HEADER,count=0;cache a={0};
 a.read=read;a.ctx=ctx;a.size=n;memset(table,0xff,sizeof table);
 while(i<n){uint32_t repeat=1,match=0,distance=0,run;uint8_t kind,v=get(&a,i);
  while(repeat<64u&&repeat<n-i&&get(&a,i+repeat)==v)++repeat;
  if(n-i>=3u){uint32_t prior=table[hash(&a,i)];if(prior!=UINT32_MAX&&i>prior&&i-prior<=WINDOW){while(match<64u&&match<n-i&&get(&a,prior+match)==get(&a,i+match))++match;distance=i-prior;}}
  if(v==0&&repeat>=2u){kind=1;run=repeat;}else if(repeat>=3u&&repeat>=match){kind=2;run=repeat;}else if(match>=4u){kind=3;run=match;}else{kind=0;run=1;}
  if(a.error)return FM1_CODEC_INVALID;
  if(kind){literals(out,cap,&pos,lit,&count);emit(out,cap,&pos,(uint8_t)((kind<<6)|(run-1u)));if(kind==2)emit(out,cap,&pos,v);if(kind==3){emit(out,cap,&pos,(uint8_t)distance);emit(out,cap,&pos,(uint8_t)(distance>>8));}}
  else{lit[count++]=v;if(count==sizeof lit)literals(out,cap,&pos,lit,&count);}
  for(j=0;j<run;j++){crc=crc_byte(crc,get(&a,i+j));if(n-(i+j)>=3u)table[hash(&a,i+j)]=i+j;}
  if(a.error)return FM1_CODEC_INVALID;i+=run;
 }
 literals(out,cap,&pos,lit,&count);if(out){uint32_t k;out[0]='P';out[1]='S';out[2]='C';out[3]=3;for(k=0;k<4;k++){out[4+k]=(uint8_t)(n>>(8u*k));out[8+k]=(uint8_t)((~crc)>>(8u*k));}}*written=pos;return FM1_CODEC_OK;
}
int fm1_codec_source_encode(fm1_codec_source_read read,void *ctx,uint32_t n,uint8_t *out,size_t cap,size_t *written){size_t need,actual;if(!read||!written||(!out&&cap)||n>84480u)return FM1_CODEC_INVALID;if(pass(read,ctx,n,0,0,&need)!=FM1_CODEC_OK)return FM1_CODEC_INVALID;*written=need;if(need>cap)return FM1_CODEC_FULL;if(pass(read,ctx,n,out,cap,&actual)!=FM1_CODEC_OK||actual!=need)return FM1_CODEC_INVALID;return FM1_CODEC_OK;}
