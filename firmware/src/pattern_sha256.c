/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_sha256.h"
#include <stddef.h>
#include <string.h>

static const uint32_t k[64] = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static uint32_t rotate(uint32_t v, unsigned n) { return (v >> n) | (v << (32u - n)); }
static uint32_t read_be(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void block(fm1_pattern_sha256 *c, const uint8_t *p)
{
    uint32_t w[16], a,b,d,e,f,g,h,t1,t2,cc;
    unsigned i;
    for (i=0; i<16; ++i) w[i]=read_be(p+4u*i);
    a=c->state[0];b=c->state[1];cc=c->state[2];d=c->state[3];
    e=c->state[4];f=c->state[5];g=c->state[6];h=c->state[7];
    for (i=0; i<64; ++i) {
        uint32_t v;
        if (i>=16) {
            uint32_t x=w[(i-15u)&15u], y=w[(i-2u)&15u];
            uint32_t s0=rotate(x,7)^rotate(x,18)^(x>>3);
            uint32_t s1=rotate(y,17)^rotate(y,19)^(y>>10);
            w[i&15u] += s0+w[(i-7u)&15u]+s1;
        }
        v=w[i&15u];
        t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+k[i]+v;
        t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&cc)^(b&cc));
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->state[0]+=a;c->state[1]+=b;c->state[2]+=cc;c->state[3]+=d;
    c->state[4]+=e;c->state[5]+=f;c->state[6]+=g;c->state[7]+=h;
}
int fm1_pattern_sha256_init(fm1_pattern_sha256 *c)
{
    static const uint32_t iv[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    if (!c) return -1;
    memcpy(c->state,iv,sizeof iv);c->bytes=0;c->used=0;c->finalized=0;
    return 0;
}
int fm1_pattern_sha256_update(fm1_pattern_sha256 *c,const void *data,uint32_t n)
{
    const uint8_t *p=(const uint8_t *)data;
    if (!c || c->finalized || (!p && n) || c->used>=64u ||
        c->bytes>UINT64_MAX/8u || (uint64_t)n>UINT64_MAX/8u-c->bytes) return -1;
    c->bytes+=n;
    while(n) {
        uint32_t take=64u-c->used; if(take>n)take=n;
        memcpy(c->buffer+c->used,p,take);c->used=(uint8_t)(c->used+take);p+=take;n-=take;
        if(c->used==64u){block(c,c->buffer);c->used=0;}
    }
    return 0;
}
int fm1_pattern_sha256_final(fm1_pattern_sha256 *c,uint8_t out[32])
{
    uint64_t bits; unsigned i;
    if(!c || !out || c->finalized || c->used>=64u || c->bytes>UINT64_MAX/8u) return -1;
    bits=c->bytes*8u;c->buffer[c->used++]=0x80u;
    if(c->used>56u){memset(c->buffer+c->used,0,64u-c->used);block(c,c->buffer);c->used=0;}
    memset(c->buffer+c->used,0,56u-c->used);
    for(i=0;i<8;i++)c->buffer[63u-i]=(uint8_t)(bits>>(8u*i));
    block(c,c->buffer);
    for(i=0;i<8;i++){uint32_t v=c->state[i];out[4*i]=(uint8_t)(v>>24);out[4*i+1]=(uint8_t)(v>>16);out[4*i+2]=(uint8_t)(v>>8);out[4*i+3]=(uint8_t)v;}
    c->finalized=1u;return 0;
}
