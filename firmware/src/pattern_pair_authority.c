/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_pair_authority.h"
#include <string.h>
#define HEAD 32u
#define ENTRY 16u
#define COMMIT 0x31415050u
#define MAGIC 0x31415550u
static uint32_t get(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static uint32_t crc(const uint8_t *p,uint32_t n){uint32_t c=~0u,i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static int blank(const uint8_t *p,uint32_t n){while(n--)if(*p++!=255u)return 0;return 1;}
static int overlap(const void *a,uint32_t an,const void *b,uint32_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
int ppa_authority_slot(const ppa_store *s){uint32_t d;if(!s||s->pairs_base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR)return -2;if(s->authority<s->pairs_base||s->authority>=s->pairs_base+PS_TOTAL_SLOTS*2u*PS_SECTOR)return -1;d=s->authority-s->pairs_base;if(d%(2u*PS_SECTOR)||d/(2u*PS_SECTOR)>=PS_SLOTS)return -2;return (int)(d/(2u*PS_SECTOR));}
static int root_reserved(const ppa_store *s){int slot=ppa_authority_slot(s);return slot!=-2&&s->reserved(s->ctx,s->authority)==1&&(slot<0||s->reserved(s->ctx,s->authority+PS_SECTOR)==1);}
static int config(const ppa_store *s,const void *scratch){return s&&scratch&&s->read&&s->program&&s->reserved&&s->project_ready&&!(s->authority%PS_SECTOR)&&!(s->pairs_base%PS_SECTOR)&&s->authority<=UINT32_MAX-PS_SECTOR&&s->pairs_base<=UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR&&root_reserved(s);}
static int read_root(const ppa_store *s,void *scratch){return !root_reserved(s)?PPA_INVALID:(s->read(s->ctx,s->authority,scratch,PS_SECTOR)?PPA_IO:PPA_OK);}
static int parse(const ppa_store *s,const uint8_t *b,uint8_t *states,uint32_t *cursor){uint32_t pos,slot,state;memset(states,0,PS_TOTAL_SLOTS);
 if(blank(b,PS_SECTOR))return PPA_EMPTY;
 if(get(b)!=MAGIC||memcmp(b+4,s->identity,16)||get(b+20)!=crc(b,20)||get(b+24)!=COMMIT||!blank(b+28,4))return PPA_CORRUPT;
 for(pos=HEAD;pos+ENTRY<=PS_SECTOR;pos+=ENTRY){
  if(blank(b+pos,ENTRY)){if(!blank(b+pos,PS_SECTOR-pos))return PPA_CORRUPT;*cursor=pos;return PPA_OK;}
  slot=get(b+pos);state=get(b+pos+4);
  if(slot>=PS_TOTAL_SLOTS||(int)slot==ppa_authority_slot(s)||get(b+pos+8)!=crc(b+pos,8)||get(b+pos+12)!=COMMIT||state!=states[slot]+1u||state>PPA_ACTIVE)return PPA_CORRUPT;
  states[slot]=(uint8_t)state;
 }
 *cursor=PS_SECTOR;return PPA_OK;
}
int ppa_open(const ppa_store *s,void *scratch,uint8_t states[PS_TOTAL_SLOTS]){uint32_t pos;int r;if(!states||!config(s,scratch)||overlap(states,PS_TOTAL_SLOTS,scratch,PS_SECTOR))return PPA_INVALID;r=read_root(s,scratch);if(r)return r;return parse(s,scratch,states,&pos);}
int ppa_provision(const ppa_store *s,void *scratch){uint8_t h[28];int r;if(!config(s,scratch))return PPA_INVALID;r=read_root(s,scratch);if(r)return r;if(!blank(scratch,PS_SECTOR))return PPA_CORRUPT;
 if(ppa_authority_slot(s)>=0){if(s->read(s->ctx,s->authority+PS_SECTOR,scratch,PS_SECTOR))return PPA_IO;if(!blank(scratch,PS_SECTOR))return PPA_CORRUPT;r=read_root(s,scratch);if(r)return r;if(!blank(scratch,PS_SECTOR))return PPA_CORRUPT;}
 memset(h,255,sizeof h);put(h,MAGIC);memcpy(h+4,s->identity,16);put(h+20,crc(h,20));put(h+24,COMMIT);
 if(!root_reserved(s)||s->program(s->ctx,s->authority,h,24)||!root_reserved(s)||s->program(s->ctx,s->authority+24,h+24,4))return PPA_IO;
 r=read_root(s,scratch);if(r)return r;return memcmp(scratch,h,28)||!blank((uint8_t *)scratch+28,PS_SECTOR-28)?PPA_IO:PPA_OK;
}
int ppa_transition(const ppa_store *s,uint32_t slot,unsigned state,void *scratch){uint8_t states[PS_TOTAL_SLOTS],entry[ENTRY];uint32_t pos,base,i,expected;int r;
 if(!config(s,scratch)||slot>=PS_TOTAL_SLOTS||(int)slot==ppa_authority_slot(s)||(state!=PPA_INITIALIZING&&state!=PPA_ACTIVE))return PPA_INVALID;
 r=read_root(s,scratch);if(r)return r;r=parse(s,scratch,states,&pos);if(r)return r;
 if(state!=states[slot]+1u)return PPA_INVALID;if(pos+ENTRY>PS_SECTOR)return PPA_FULL;
 base=s->pairs_base+slot*2u*PS_SECTOR;
 if(s->reserved(s->ctx,base)!=1||s->reserved(s->ctx,base+PS_SECTOR)!=1)return PPA_INVALID;
 if(state==PPA_INITIALIZING){for(i=0;i<2;i++){if(s->read(s->ctx,base+i*PS_SECTOR,scratch,PS_SECTOR))return PPA_IO;if(!blank(scratch,PS_SECTOR))return PPA_INVALID;}}
 else if(s->project_ready(s->ctx,slot)!=1)return PPA_INVALID;
 /* Revalidate the root after preflight before appending. */
 r=read_root(s,scratch);if(r)return r;r=parse(s,scratch,states,&i);if(r)return r;if(i!=pos||state!=states[slot]+1u)return PPA_INVALID;
 put(entry,slot);put(entry+4,state);put(entry+8,crc(entry,8));put(entry+12,COMMIT);
 memcpy((uint8_t *)scratch+pos,entry,ENTRY);expected=crc(scratch,PS_SECTOR);
 if(!root_reserved(s)||s->reserved(s->ctx,base)!=1||s->reserved(s->ctx,base+PS_SECTOR)!=1||s->program(s->ctx,s->authority+pos,entry,12)||!root_reserved(s)||s->reserved(s->ctx,base)!=1||s->reserved(s->ctx,base+PS_SECTOR)!=1||s->program(s->ctx,s->authority+pos+12,entry+12,4))return PPA_IO;
 r=read_root(s,scratch);if(r)return r;if(crc(scratch,PS_SECTOR)!=expected)return PPA_IO;r=parse(s,scratch,states,&i);return r==PPA_OK&&states[slot]==state&&i==pos+ENTRY?PPA_OK:PPA_IO;
}
/* Recover only an externally identified, exact-prefix interrupted final entry.
 * Prior entries and the complete suffix are validated before any program. */
int ppa_recover_tail(const ppa_store *s,uint32_t slot,unsigned state,void *scratch,ppa_recovery_proof_fn proof,void *proof_ctx){
 uint8_t states[PS_TOTAL_SLOTS]={0},entry[ENTRY];uint8_t *b=scratch;uint32_t pos,i,old_crc,expected,base,previous_slot,previous_state,first;int r;
 if(!config(s,scratch)||!proof||slot>=PS_TOTAL_SLOTS||(int)slot==ppa_authority_slot(s)||(state!=PPA_INITIALIZING&&state!=PPA_ACTIVE))return PPA_INVALID;
 r=read_root(s,scratch);if(r)return r;
 if(get(b)!=MAGIC||memcmp(b+4,s->identity,16)||get(b+20)!=crc(b,20)||get(b+24)!=COMMIT||!blank(b+28,4))return PPA_CORRUPT;
 for(pos=HEAD;pos+ENTRY<=PS_SECTOR;pos+=ENTRY){
  if(blank(b+pos,ENTRY))return PPA_INVALID;
  previous_slot=get(b+pos);previous_state=get(b+pos+4);
  if(previous_slot>=PS_TOTAL_SLOTS||(int)previous_slot==ppa_authority_slot(s)||get(b+pos+8)!=crc(b+pos,8)||get(b+pos+12)!=COMMIT||previous_state!=states[previous_slot]+1u||previous_state>PPA_ACTIVE)break;
  states[previous_slot]=(uint8_t)previous_state;
 }
 if(pos+ENTRY>PS_SECTOR||state!=states[slot]+1u||!blank(b+pos+ENTRY,PS_SECTOR-pos-ENTRY))return PPA_CORRUPT;
 put(entry,slot);put(entry+4,state);put(entry+8,crc(entry,8));put(entry+12,COMMIT);
 for(first=0;first<ENTRY&&b[pos+first]==entry[first];first++){}
 if(first==ENTRY||!blank(b+pos+first,ENTRY-first))return PPA_CORRUPT;
 base=s->pairs_base+slot*2u*PS_SECTOR;
 if(s->reserved(s->ctx,base)!=1||s->reserved(s->ctx,base+PS_SECTOR)!=1||proof(proof_ctx,slot,state)!=1)return PPA_INVALID;
 old_crc=crc(b,PS_SECTOR);
 if(state==PPA_INITIALIZING){for(i=0;i<2;i++){if(s->read(s->ctx,base+i*PS_SECTOR,scratch,PS_SECTOR))return PPA_IO;if(!blank(scratch,PS_SECTOR))return PPA_INVALID;}}
 r=read_root(s,scratch);if(r)return r;if(crc(scratch,PS_SECTOR)!=old_crc)return PPA_CORRUPT;
 memcpy(b+pos,entry,ENTRY);expected=crc(b,PS_SECTOR);
 if(!root_reserved(s)||s->reserved(s->ctx,base)!=1||s->reserved(s->ctx,base+PS_SECTOR)!=1||proof(proof_ctx,slot,state)!=1)return PPA_INVALID;
 if(first<12u){if(s->program(s->ctx,s->authority+pos+first,entry+first,12u-first))return PPA_IO;first=12u;}
 if(!root_reserved(s)||s->reserved(s->ctx,base)!=1||s->reserved(s->ctx,base+PS_SECTOR)!=1||proof(proof_ctx,slot,state)!=1)return PPA_IO;
 if(s->program(s->ctx,s->authority+pos+first,entry+first,ENTRY-first))return PPA_IO;
 r=read_root(s,scratch);if(r)return r;if(crc(scratch,PS_SECTOR)!=expected)return PPA_IO;r=parse(s,scratch,states,&i);return r==PPA_OK&&states[slot]==state&&i==pos+ENTRY?PPA_OK:PPA_IO;
}
