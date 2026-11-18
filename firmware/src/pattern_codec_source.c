#include "pattern_codec_source.h"
#include "pattern_storage_codec.h"
#include "pattern_codec_pass.h"
#include <string.h>

#define CACHESIZE 2048u
#define SOURCE_BLOCK 128u
typedef struct { fm1_codec_bytes bytes; fm1_codec_source_read read; void *ctx; uint32_t size,end; uint8_t data[CACHESIZE]; } cache;
static uint8_t get(fm1_codec_bytes *b,uint32_t i){cache *c=(cache *)b;uint32_t n;if(i>=c->size){b->error=1;return 0;}while(i>=c->end){n=c->size-c->end;if(n>SOURCE_BLOCK)n=SOURCE_BLOCK;if(c->read(c->ctx,c->end,c->data+(c->end%CACHESIZE),n)){b->error=1;return 0;}c->end+=n;}if(c->end-i>CACHESIZE){b->error=1;return 0;}return c->data[i%CACHESIZE];}
/* The encoder pass itself is shared with the in-memory encoders
 * (fm1_codec_encode_pass, pattern_storage_codec.c); this wrapper only supplies
 * the bounded read cache. */
static int pass(fm1_codec_source_read read,void *ctx,uint32_t n,uint8_t *out,size_t cap,size_t *written){
 cache a;size_t pos;
 a.bytes.mem=0;a.bytes.get=get;a.bytes.error=0;a.read=read;a.ctx=ctx;a.size=n;a.end=0;
 pos=fm1_codec_encode_pass(&a.bytes,n,out,0,cap);
 if(a.bytes.error)return FM1_CODEC_INVALID;
 *written=pos;return FM1_CODEC_OK;
}
int fm1_codec_source_encode(fm1_codec_source_read read,void *ctx,uint32_t n,uint8_t *out,size_t cap,size_t *written){size_t need,actual;if(!read||!written||(!out&&cap)||n>84480u)return FM1_CODEC_INVALID;if(pass(read,ctx,n,0,0,&need)!=FM1_CODEC_OK)return FM1_CODEC_INVALID;*written=need;if(need>cap)return FM1_CODEC_FULL;if(pass(read,ctx,n,out,cap,&actual)!=FM1_CODEC_OK||actual!=need)return FM1_CODEC_INVALID;return FM1_CODEC_OK;}
