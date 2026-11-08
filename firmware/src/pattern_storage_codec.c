/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_storage_codec.h"
#include <stdint.h>
#include <string.h>

/* Experimental host-only format. Both versions have magic, decoded length,
 * and CRC32 in a 12-byte header. V1 uses 128-byte zero/literal runs. V2 uses
 * up to 64-byte literal, zero, repeated-byte, and 16-bit-distance match runs.
 * V3 limits the match window to 1 KiB and uses a 512-entry encoder table.
 * Legacy V2 still requires a 16 KiB rolling buffer. Decode validates the
 * entire stream and CRC before writing caller output. */
#define HEADER 12u
#define WINDOW 1024u
#define LEGACY_WINDOW 16384u
#define HASH_SIZE 512u
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p, uint32_t n) {
    p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24);
}
static uint32_t crc_byte(uint32_t c, uint8_t b) {
    unsigned i; c^=b; for(i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1u))); return c;
}
static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if(!an || !bn) return 0;
    return x<=y ? y-x<an : x-y<bn;
}
static uint32_t hash3(const uint8_t *p) {
    return ((uint32_t)p[0]*251u+(uint32_t)p[1]*31u+p[2]) & (HASH_SIZE-1u);
}
static void emit(uint8_t *out, size_t start, size_t cap, size_t *o, uint8_t b) { if(out && *o>=start && *o-start<cap) out[*o-start]=b; ++*o; }
static void literals(uint8_t *out, size_t start, size_t cap, size_t *o, const uint8_t *lit, size_t *count) {
    size_t j; if(!*count) return;
    emit(out,start,cap,o,(uint8_t)(*count-1u));
    for(j=0;j<*count;j++) emit(out,start,cap,o,lit[j]);
    *count=0;
}
static size_t pass(const uint8_t *in, size_t n, uint8_t *out, size_t start, size_t cap) {
    uint32_t table[HASH_SIZE], crc=~0u;
    uint8_t lit[64]; size_t i=0, o=HEADER, count=0, j;
    memset(table,0xff,sizeof table);
    while(i<n) {
        size_t repeat=1, match=0, distance=0, run;
        uint8_t kind;
        while(repeat<64u && repeat<n-i && in[i+repeat]==in[i]) ++repeat;
        if(n-i>=3u) {
            uint32_t prior=table[hash3(in+i)];
            if(prior!=UINT32_MAX && i>(size_t)prior && i-prior<=WINDOW) {
                while(match<64u && match<n-i && in[prior+match]==in[i+match]) ++match;
                distance=i-prior;
            }
        }
        if(in[i]==0 && repeat>=2u) { kind=1; run=repeat; }
        else if(repeat>=3u && repeat>=match) { kind=2; run=repeat; }
        else if(match>=4u) { kind=3; run=match; }
        else { kind=0; run=1; }
        if(kind) {
            literals(out,start,cap,&o,lit,&count);
            emit(out,start,cap,&o,(uint8_t)((kind<<6)|(run-1u)));
            if(kind==2) emit(out,start,cap,&o,in[i]);
            if(kind==3) { emit(out,start,cap,&o,(uint8_t)distance); emit(out,start,cap,&o,(uint8_t)(distance>>8)); }
        } else {
            lit[count++]=in[i];
            if(count==sizeof lit) literals(out,start,cap,&o,lit,&count);
        }
        for(j=0;j<run;j++) {
            crc=crc_byte(crc,in[i+j]);
            if(n-(i+j)>=3u) table[hash3(in+i+j)]=(uint32_t)(i+j);
        }
        i+=run;
    }
    literals(out,start,cap,&o,lit,&count);
    if(out) { uint8_t h[HEADER]; size_t k; h[0]='P'; h[1]='S'; h[2]='C'; h[3]=3; put32(h+4,(uint32_t)n); put32(h+8,~crc); for(k=0;k<HEADER;k++)if(k>=start && k-start<cap)out[k-start]=h[k]; }
    return o;
}
int fm1_pattern_codec_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *written) {
    size_t need;
    if(!written || (!in && n) || (!out && cap) || n>UINT32_MAX ||
       n>(SIZE_MAX-HEADER)/2u || overlap(in,n,out,cap)) return FM1_CODEC_INVALID;
    need=pass(in,n,0,0,0); *written=need;
    if(cap<need) return FM1_CODEC_FULL;
    pass(in,n,out,0,cap); return FM1_CODEC_OK;
}
int fm1_pattern_codec_encode_slice(const uint8_t *in,size_t n,size_t offset,uint8_t *out,size_t count,size_t *written) {
    size_t need;
    if(!written||(!in&&n)||(!out&&count)||n>UINT32_MAX||n>(SIZE_MAX-HEADER)/2u||overlap(in,n,out,count))return FM1_CODEC_INVALID;
    need=pass(in,n,0,0,0);*written=need;
    if(offset>need||count>need-offset)return FM1_CODEC_FULL;
    if(count)pass(in,n,out,offset,count);
    return FM1_CODEC_OK;
}
/* Decode in a bounded ring, validating everything before touching output.
 * The window is supplied by the caller-specific wrapper, so the current
 * version needs 1 KiB stack; the unshipped V2 prototype still decodes with
 * its original 16 KiB window in a separate legacy call. */
static int decode_window(const uint8_t *in, size_t n, uint8_t *out, size_t expected,
                         uint8_t *window, size_t window_size, int version) {
    size_t i,o,j; uint32_t crc; int round;
    for(round=0;round<(out?2:1);round++) {
        i=HEADER; o=0; crc=~0u;
        while(i<n) {
            uint8_t token=in[i++], kind; size_t run, distance=0; uint8_t value=0;
            if(version==1) { kind=(token&128u)?1u:0u; run=(token&127u)+1u; }
            else { kind=token>>6; run=(token&63u)+1u; }
            if(run>expected-o) return FM1_CODEC_INVALID;
            if(kind==0 && run>n-i) return FM1_CODEC_INVALID;
            if(kind==2) { if(i>=n) return FM1_CODEC_INVALID; value=in[i++]; }
            if(kind==3) {
                if(n-i<2u) return FM1_CODEC_INVALID;
                distance=(size_t)in[i]|(size_t)in[i+1]<<8; i+=2;
                if(!distance || distance>window_size || distance>o) return FM1_CODEC_INVALID;
            }
            for(j=0;j<run;j++) {
                uint8_t v=kind==0?in[i+j]:kind==1?0:kind==2?value:window[(o+j-distance)%window_size];
                crc=crc_byte(crc,v); window[(o+j)%window_size]=v;
                if(round) out[o+j]=v;
            }
            if(kind==0) i+=run;
            o+=run;
        }
        if(o!=expected || ~crc!=get32(in+8)) return FM1_CODEC_INVALID;
    }
    return FM1_CODEC_OK;
}
#if !defined(FM1_DEVICE)
static int decode_legacy_v2(const uint8_t *in, size_t n, uint8_t *out, size_t expected) {
    uint8_t window[LEGACY_WINDOW];
    return decode_window(in,n,out,expected,window,sizeof window,2);
}
#endif
int fm1_pattern_codec_decode(const uint8_t *in, size_t n, uint8_t *out, size_t expected) {
    uint8_t window[WINDOW]; int version;
    if(!in || n<HEADER || (!out && expected) || expected>UINT32_MAX ||
       overlap(in,n,out,expected) || in[0]!='P' || in[1]!='S' || in[2]!='C' ||
       (in[3]!=1 && in[3]!=2 && in[3]!=3) || get32(in+4)!=expected) return FM1_CODEC_INVALID;
    version=in[3];
    if(version==2) {
#if defined(FM1_DEVICE)
        /* V2 was host-only and never written by the device; its 16 KiB stack
         * requirement is not acceptable on the target. */
        return FM1_CODEC_INVALID;
#else
        return decode_legacy_v2(in,n,out,expected);
#endif
    }
    return decode_window(in,n,out,expected,window,sizeof window,version);
}
int fm1_pattern_codec_validate(const uint8_t *in, size_t n, size_t expected) {
    uint8_t window[WINDOW]; int version;
    if(!in || n<HEADER || expected>UINT32_MAX || in[0]!='P' || in[1]!='S' || in[2]!='C' ||
       (in[3]!=1 && in[3]!=2 && in[3]!=3) || get32(in+4)!=expected) return FM1_CODEC_INVALID;
    version=in[3];
    if(version==2) {
#if defined(FM1_DEVICE)
        return FM1_CODEC_INVALID;
#else
        return decode_legacy_v2(in,n,0,expected);
#endif
    }
    return decode_window(in,n,0,expected,window,sizeof window,version);
}
/* Small bounded read cache + V3 rolling window. Both validation and output
 * passes reread the immutable source. No output on first-pass failure. */
typedef struct { fm1_codec_read_fn read; void *ctx; uint32_t base; size_t n,at,first,have; uint8_t cache[128]; } codec_reader;
static int codec_byte(codec_reader *r,uint8_t *v){size_t take;if(r->at>=r->n)return -1;if(r->at<r->first||r->at-r->first>=r->have){r->first=r->at;take=r->n-r->at;if(take>sizeof r->cache)take=sizeof r->cache;if(r->read(r->ctx,r->base+(uint32_t)r->at,r->cache,(uint32_t)take))return -1;r->have=take;}*v=r->cache[r->at++-r->first];return 0;}
int fm1_pattern_codec_decode_stream(fm1_codec_read_fn read,void *ctx,uint32_t base,size_t n,uint8_t *out,size_t expected){uint8_t window[WINDOW],head[HEADER];unsigned round;size_t o,j;uint32_t crc;codec_reader r;if(!read||!out||n<HEADER||n>UINT32_MAX||expected>UINT32_MAX||base>UINT32_MAX-(uint32_t)n||read(ctx,base,head,HEADER)||head[0]!='P'||head[1]!='S'||head[2]!='C'||head[3]!=3||get32(head+4)!=expected)return FM1_CODEC_INVALID;
 for(round=0;round<2;round++){memset(&r,0,sizeof r);r.read=read;r.ctx=ctx;r.base=base;r.n=n;r.at=HEADER;o=0;crc=~0u;while(r.at<n){uint8_t token,kind,value=0,lo,hi;size_t run,distance=0;if(codec_byte(&r,&token))return FM1_CODEC_INVALID;kind=token>>6;run=(token&63u)+1u;if(run>expected-o)return FM1_CODEC_INVALID;if(kind==2&&codec_byte(&r,&value))return FM1_CODEC_INVALID;if(kind==3){if(codec_byte(&r,&lo)||codec_byte(&r,&hi))return FM1_CODEC_INVALID;distance=(size_t)lo|((size_t)hi<<8);if(!distance||distance>WINDOW||distance>o)return FM1_CODEC_INVALID;}for(j=0;j<run;j++){uint8_t v;if(kind==0){if(codec_byte(&r,&v))return FM1_CODEC_INVALID;}else if(kind==1)v=0;else if(kind==2)v=value;else v=window[(o+j-distance)%WINDOW];crc=crc_byte(crc,v);window[(o+j)%WINDOW]=v;if(round)out[o+j]=v;}o+=run;}if(o!=expected||~crc!=get32(head+8))return FM1_CODEC_INVALID;}
 return FM1_CODEC_OK;}
