/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_project.h"
#include "pattern_storage_codec.h"
#include <string.h>
#include <limits.h>
_Static_assert(CHAR_BIT == 8 && sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4 && INT8_MIN == -128 && INT8_MAX == 127, "canonical integer representation");
_Static_assert(BR_PARTS == 4 && BR_STEPS == 64 && BR_SYNTH_PARTS == 3 && BR_DRUM_LANES == 4, "canonical pattern dimensions");
/* In-place canonicalization is admitted only on little-endian systems with
 * this proven contiguous layout. On target, in-place serialization is an
 * identity; host builds retain the explicit reference implementation. */
_Static_assert(sizeof(br_pattern_snapshot)==FM1_PROJECT_PATTERN_BYTES,"snapshot size");
_Static_assert(offsetof(br_pattern_snapshot,length)==1024,"length offset");
_Static_assert(offsetof(br_pattern_snapshot,division)==1040,"division offset");
_Static_assert(offsetof(br_pattern_snapshot,synth_length)==1056,"synth length offset");
_Static_assert(offsetof(br_pattern_snapshot,synth_condition)==1440,"synth condition offset");
_Static_assert(offsetof(br_pattern_snapshot,synth_note)==1632,"synth note offset");
_Static_assert(offsetof(br_pattern_snapshot,synth_velocity)==1824,"synth velocity offset");
_Static_assert(offsetof(br_pattern_snapshot,synth_micro)==2016,"synth micro offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_length)==2208,"kit length offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_condition)==2720,"kit condition offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_velocity)==2976,"kit velocity offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_retrig)==3232,"kit retrig offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_note)==3488,"kit note offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_pitch)==3744,"kit pitch offset");
_Static_assert(offsetof(br_pattern_snapshot,kit_micro)==4000,"kit micro offset");
static int little_endian(void){const uint16_t one=1;return *(const uint8_t *)&one==1;}

/* Version 4: pattern-only header, canonical legacy v16 image, then one
 * independently compressed canonical LE snapshot per used slot. CRC32 covers
 * the complete envelope with the CRC field zero. The v16 image is an existing
 * separately specified little-endian legacy format, checked by the caller. */
enum { HDR=FM1_PROJECT_HEADER_BYTES, CRC_POS=12, PATTERN_VERSION=4 };
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){put16(p,(uint16_t)v);put16(p+2,(uint16_t)(v>>16));}
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t get32(const uint8_t *p){return get16(p)|((uint32_t)get16(p+2)<<16);}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;size_t i;unsigned j;for(i=0;i<n;i++){c^=(i>=CRC_POS&&i<CRC_POS+4)?0:p[i];for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static int pattern_state_valid(const fm1_pattern_song *s,uint16_t used,uint8_t active){return s && s->used==used && s->active==active && active<FM1_PATTERN_COUNT && (used&(1u<<active)) && !(used&~2047u);}
/* Host reference serializes each field; target admits only the proven in-place layout. */
static int pattern(br_pattern_snapshot *s,uint8_t *buf,int read){unsigned p,i,l;
#if (defined(FM1_DEVICE) && FM1_DEVICE) || defined(FM1_PROJECT_INPLACE_IDENTITY_TEST)
 if (!little_endian() || buf != (uint8_t *)s) return 0;
 (void)read;
#else
 size_t n=0;
#define U8(x) do { if(read)(x)=buf[n];else buf[n]=(uint8_t)(x);n++; } while(0)
#define I8(x) do { if(read)(x)=(int8_t)buf[n];else buf[n]=(uint8_t)(x);n++; } while(0)
#define U16(x) do { if(read)(x)=get16(buf+n);else put16(buf+n,(x));n+=2; } while(0)
#define U32(x) do { if(read)(x)=get32(buf+n);else put32(buf+n,(x));n+=4; } while(0)
 for(p=0;p<4;p++)for(i=0;i<64;i++)U32(s->step[p][i]);
 for(p=0;p<4;p++)U32(s->length[p]);for(p=0;p<4;p++)U32(s->division[p]);
 for(p=0;p<3;p++)for(i=0;i<64;i++)U16(s->synth_length[p][i]);
 for(p=0;p<3;p++)for(i=0;i<64;i++)U8(s->synth_condition[p][i]);
 for(p=0;p<3;p++)for(i=0;i<64;i++)U8(s->synth_note[p][i]);
 for(p=0;p<3;p++)for(i=0;i<64;i++)U8(s->synth_velocity[p][i]);
 for(p=0;p<3;p++)for(i=0;i<64;i++)I8(s->synth_micro[p][i]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)U16(s->kit_length[i][l]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)U8(s->kit_condition[i][l]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)U8(s->kit_velocity[i][l]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)U8(s->kit_retrig[i][l]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)U8(s->kit_note[i][l]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)U8(s->kit_pitch[i][l]);
 for(i=0;i<64;i++)for(l=0;l<4;l++)I8(s->kit_micro[i][l]);
#undef U8
#undef I8
#undef U16
#undef U32
 if(n!=FM1_PROJECT_PATTERN_BYTES)return 0;
#endif
 for(p=0;p<4;p++)if(!s->length[p]||s->length[p]>64||s->division[p]>2)return 0;
 for(p=0;p<3;p++)for(i=0;i<64;i++)if(s->synth_length[p][i]>BR_STEP_LENGTH_MAX||s->synth_condition[p][i]>=BR_STEP_COND_COUNT||(s->synth_note[p][i]!=255&&s->synth_note[p][i]>127)||(s->synth_velocity[p][i]!=255&&s->synth_velocity[p][i]>127)||s->synth_micro[p][i]<-16||s->synth_micro[p][i]>15)return 0;
 for(i=0;i<64;i++)for(l=0;l<4;l++)if(s->kit_length[i][l]>BR_STEP_LENGTH_MAX||s->kit_condition[i][l]>=BR_STEP_COND_COUNT||s->kit_velocity[i][l]>127||(s->kit_retrig[i][l]&0xc0)||(s->kit_note[i][l]!=255&&(s->kit_note[i][l]>=BR_DRUM_VOICE_COUNT||br_drum_voice_lane(s->kit_note[i][l])!=l))||(s->kit_pitch[i][l]!=255&&(s->kit_pitch[i][l]<36||s->kit_pitch[i][l]>84))||s->kit_micro[i][l]<-16||s->kit_micro[i][l]>15)return 0;
 return 1;}
int fm1_project_encode(const uint8_t *session,const br_pattern_bank *b,const br_pattern_snapshot *live,const fm1_pattern_song *song,fm1_project_session_valid valid,void *ctx,fm1_project_work *w,uint8_t *out,size_t cap,size_t *written){size_t pos,n,need[11]={0};unsigned i,pass;
 if(!little_endian()||!session||!b||!live||!song||!valid||!w||!w->pattern||!w->session||!w->compressed||!w->compressed_capacity||!out||!written||!valid(session,FM1_PROJECT_SESSION,ctx)||!pattern_state_valid(song,b->used,b->active))return FM1_PROJECT_INVALID;
 /* First pass validates every record and sizes the complete output. Inputs
  * must remain immutable until pass two completes. A failed output is scratch. */
 for(pass=0;pass<2;pass++){
 pos=HDR+FM1_PROJECT_SESSION;
 if(pass){memset(out,0,HDR);memcpy(out,"FMP1",4);put16(out+4,PATTERN_VERSION);put16(out+6,HDR);put16(out+16,b->used);out[18]=b->active;memcpy(out+HDR,session,FM1_PROJECT_SESSION);}
 for(i=0;i<11;i++)if(b->used&(1u<<i)){if(br_pattern_bank_copy(b,i,live,w->pattern)!=BR_BANK_OK||!pattern(w->pattern,(uint8_t *)w->pattern,0))return FM1_PROJECT_INVALID;if(fm1_pattern_codec_encode((uint8_t *)w->pattern,sizeof *w->pattern,w->compressed,w->compressed_capacity,&n)!=FM1_CODEC_OK||pos>FM1_PROJECT_MAX-5||n>FM1_PROJECT_MAX-pos-5)return FM1_PROJECT_FULL;if(pass){if(n!=need[i])return FM1_PROJECT_INVALID;out[pos]=(uint8_t)i;put32(out+pos+1,(uint32_t)n);memcpy(out+pos+5,w->compressed,n);}else need[i]=n;pos+=5+n;}
 if(!pass){*written=pos;if(pos>cap)return FM1_PROJECT_FULL;}
 }
 put32(out+8,(uint32_t)pos);put32(out+CRC_POS,crc(out,pos));return FM1_PROJECT_OK;}
int fm1_project_decode(const uint8_t *in,size_t len,fm1_project_session_valid valid,void *ctx,fm1_project_work *w,uint8_t *session,br_pattern_bank *bank,uint8_t *arena,size_t capacity,br_pattern_snapshot *live,fm1_pattern_song *song){size_t pos=HDR+FM1_PROJECT_SESSION,n,active_pos=0,active_len=0;uint16_t used,seen=0;unsigned i;uint8_t active;if(!little_endian()||!in||!valid||!w||!w->pattern||!w->session||!w->compressed||!w->compressed_capacity||!session||!bank||!arena||!live||!song||len<pos||len>FM1_PROJECT_MAX||memcmp(in,"FMP1",4)||get16(in+4)!=PATTERN_VERSION||get16(in+6)!=HDR||get32(in+8)!=len||get32(in+12)!=crc(in,len)||!valid(in+HDR,FM1_PROJECT_SESSION,ctx))return FM1_PROJECT_INVALID;used=get16(in+16);active=in[18];fm1_pattern_song_init(&w->song);w->song.used=used;w->song.active=active;if(in[19])return FM1_PROJECT_INVALID;if(!pattern_state_valid(&w->song,used,active))return FM1_PROJECT_INVALID;
 if(!w->arena||!w->arena_capacity||br_pattern_bank_init(&w->bank,w->arena,w->arena_capacity)!=BR_BANK_OK)return FM1_PROJECT_INVALID;w->bank.used=(uint16_t)(1u<<active);w->bank.active=active;
 while(pos<len){unsigned slot;if(len-pos<5)return FM1_PROJECT_INVALID;slot=in[pos++];n=get32(in+pos);pos+=4;if(slot>=11||!(used&(1u<<slot))||(seen&(1u<<slot))||n>len-pos||fm1_pattern_codec_decode(in+pos,n,(uint8_t *)w->pattern,sizeof *w->pattern)!=FM1_CODEC_OK||!pattern(w->pattern,(uint8_t *)w->pattern,1))return FM1_PROJECT_INVALID;seen|=(uint16_t)(1u<<slot);pos+=n;if(slot==active){active_pos=pos-n;active_len=n;}else if(br_pattern_bank_paste(&w->bank,slot,live,w->pattern,w->compressed,w->compressed_capacity)!=BR_BANK_OK)return FM1_PROJECT_FULL;}
 if(seen!=used)return FM1_PROJECT_INVALID;if(w->bank.size>capacity)return FM1_PROJECT_FULL;if(fm1_pattern_codec_decode(in+active_pos,active_len,(uint8_t *)w->pattern,sizeof *w->pattern)!=FM1_CODEC_OK||!pattern(w->pattern,(uint8_t *)w->pattern,1))return FM1_PROJECT_INVALID;memcpy(session,in+HDR,FM1_PROJECT_SESSION);if(arena!=w->arena)memcpy(arena,w->arena,w->bank.size);*bank=w->bank;bank->arena=arena;bank->capacity=capacity;*live=*w->pattern;*song=w->song;return FM1_PROJECT_OK;}
/* Stream source fragments are regenerated from immutable snapshots. */
static int source_pattern(fm1_project_source *s,unsigned slot){if((!(slot==s->bank->active&&s->live==s->work->pattern)&&br_pattern_bank_copy(s->bank,slot,s->live,s->work->pattern)!=BR_BANK_OK)||!pattern(s->work->pattern,(uint8_t *)s->work->pattern,0))return -1;return 0;}
int fm1_project_source_read(void *ctx,uint32_t offset,void *dst,uint32_t len){fm1_project_source *s=ctx;uint8_t *out=dst;uint32_t count;unsigned i;if(!s||(!dst&&len)||offset>s->total||len>s->total-offset)return -1;while(len){if(offset<HDR){count=HDR-offset;if(count>len)count=len;memcpy(out,s->header+offset,count);}else if(offset<HDR+FM1_PROJECT_SESSION){count=HDR+FM1_PROJECT_SESSION-offset;if(count>len)count=len;memcpy(out,s->session+offset-HDR,count);}else{uint32_t relative;uint8_t rec[5];for(i=0;i<11;i++)if(s->length[i]&&offset>=s->offset[i]&&offset<s->offset[i]+5+s->length[i])break;if(i==11)return -1;relative=offset-s->offset[i];if(relative<5){rec[0]=(uint8_t)i;put32(rec+1,s->length[i]);count=5-relative;if(count>len)count=len;memcpy(out,rec+relative,count);}else{size_t actual;if(source_pattern(s,i))return -1;relative-=5;count=s->length[i]-relative;if(count>len)count=len;if(fm1_pattern_codec_encode_slice((uint8_t *)s->work->pattern,sizeof *s->work->pattern,relative,out,count,&actual)!=FM1_CODEC_OK||actual!=s->length[i])return -1;}}out+=count;offset+=count;len-=count;}return 0;}
int fm1_project_source_init(fm1_project_source *s,const uint8_t *session,const br_pattern_bank *b,const br_pattern_snapshot *live,const fm1_pattern_song *song,fm1_project_session_valid valid,void *ctx,fm1_project_work *w){uint32_t pos=HDR+FM1_PROJECT_SESSION,c=~0u;unsigned i,j;size_t n;uint8_t temp[256];if(!little_endian()||!s||!session||!b||!live||!song||!valid||!w||!w->pattern||!w->session||!valid(session,FM1_PROJECT_SESSION,ctx)||!pattern_state_valid(song,b->used,b->active)||(live==w->pattern&&b->used!=(uint16_t)(1u<<b->active)))return FM1_PROJECT_INVALID;memset(s,0,sizeof *s);s->session=session;s->bank=b;s->live=live;s->work=w;memcpy(s->header,"FMP1",4);put16(s->header+4,PATTERN_VERSION);put16(s->header+6,HDR);put16(s->header+16,b->used);s->header[18]=b->active;for(i=0;i<11;i++)if(b->used&(1u<<i)){if((!(i==b->active&&live==w->pattern)&&br_pattern_bank_copy(b,i,live,w->pattern)!=BR_BANK_OK)||!pattern(w->pattern,(uint8_t *)w->pattern,0))return FM1_PROJECT_INVALID;if(fm1_pattern_codec_encode_slice((uint8_t *)w->pattern,sizeof *w->pattern,0,0,0,&n)!=FM1_CODEC_OK||pos>FM1_PROJECT_MAX-5||n>FM1_PROJECT_MAX-pos-5)return FM1_PROJECT_FULL;s->offset[i]=pos;s->length[i]=(uint32_t)n;pos+=5+(uint32_t)n;}s->total=pos;put32(s->header+8,pos);/* CRC each fragment once; do not repeatedly recompress per small read. */
 for(pos=0;pos<HDR;pos++){c^=s->header[pos];for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
 for(pos=0;pos<FM1_PROJECT_SESSION;pos++){c^=session[pos];for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
 for(i=0;i<11;i++)if(s->length[i]){unsigned k;temp[0]=(uint8_t)i;put32(temp+1,s->length[i]);if(source_pattern(s,i))return FM1_PROJECT_INVALID;for(k=0;k<5;k++){c^=temp[k];for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}for(pos=0;pos<s->length[i];){size_t actual;uint32_t part=s->length[i]-pos;if(part>sizeof temp)part=sizeof temp;if(fm1_pattern_codec_encode_slice((uint8_t *)w->pattern,sizeof *w->pattern,pos,temp,part,&actual)!=FM1_CODEC_OK||actual!=s->length[i])return FM1_PROJECT_INVALID;for(k=0;k<part;k++){c^=temp[k];for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}pos+=part;}}
 put32(s->header+CRC_POS,~c);return FM1_PROJECT_OK;}
int fm1_project_decode_stream(fm1_project_read_fn read,void *ctx,uint32_t len,fm1_project_session_valid valid,void *vctx,fm1_project_work *w,uint8_t *session,br_pattern_bank *bank,uint8_t *arena,size_t capacity,br_pattern_snapshot *live,fm1_pattern_song *song){uint8_t head[HDR],record[5],temp[256];uint32_t pos,c=~0u,stored,active_pos=0,active_len=0,n;uint16_t used,seen=0;uint8_t active;unsigned i,j;if(!little_endian()||!read||!valid||!w||!w->pattern||!w->session||!session||!bank||!arena||!live||!song||len<HDR+FM1_PROJECT_SESSION||len>FM1_PROJECT_MAX||read(ctx,0,head,HDR)||memcmp(head,"FMP1",4)||get16(head+4)!=PATTERN_VERSION||get16(head+6)!=HDR||get32(head+8)!=len)return FM1_PROJECT_INVALID;stored=get32(head+12);for(pos=0;pos<len;){n=len-pos;if(n>sizeof temp)n=sizeof temp;if(read(ctx,pos,temp,n))return FM1_PROJECT_INVALID;for(i=0;i<n;i++){uint32_t at=pos+i;c^=(at>=12&&at<16)?0:temp[i];for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}pos+=n;}if((~c)!=stored||read(ctx,HDR,w->session,FM1_PROJECT_SESSION)||!valid(w->session,FM1_PROJECT_SESSION,vctx))return FM1_PROJECT_INVALID;used=get16(head+16);active=head[18];fm1_pattern_song_init(&w->song);w->song.used=used;w->song.active=active;if(head[19])return FM1_PROJECT_INVALID;if(!pattern_state_valid(&w->song,used,active)||!w->arena||!w->arena_capacity||br_pattern_bank_init(&w->bank,w->arena,w->arena_capacity)!=BR_BANK_OK)return FM1_PROJECT_INVALID;w->bank.used=(uint16_t)(1u<<active);w->bank.active=active;
 pos=HDR+FM1_PROJECT_SESSION;while(pos<len){unsigned slot;if(len-pos<5||read(ctx,pos,record,5))return FM1_PROJECT_INVALID;slot=record[0];n=get32(record+1);pos+=5;if(slot>=11||!(used&(1u<<slot))||(seen&(1u<<slot))||n>len-pos||n>FM1_PROJECT_RECORD_MAX||fm1_pattern_codec_decode_stream(read,ctx,pos,n,(uint8_t *)w->pattern,sizeof *w->pattern)!=FM1_CODEC_OK||!pattern(w->pattern,(uint8_t *)w->pattern,1))return FM1_PROJECT_INVALID;seen|=(uint16_t)(1u<<slot);if(slot==active){active_pos=pos;active_len=n;}else{if(n>w->bank.capacity-w->bank.size)return FM1_PROJECT_FULL;if(read(ctx,pos,w->arena+w->bank.size,n)||fm1_pattern_codec_decode(w->arena+w->bank.size,n,(uint8_t *)w->pattern,sizeof *w->pattern)!=FM1_CODEC_OK||!pattern(w->pattern,(uint8_t *)w->pattern,1))return FM1_PROJECT_INVALID;w->bank.offset[slot]=w->bank.size;w->bank.length[slot]=n;w->bank.size+=n;w->bank.used|=(uint16_t)(1u<<slot);}pos+=n;}
 if(seen!=used)return FM1_PROJECT_INVALID;if(w->bank.size>capacity)return FM1_PROJECT_FULL;if(fm1_pattern_codec_decode_stream(read,ctx,active_pos,active_len,(uint8_t *)w->pattern,sizeof *w->pattern)!=FM1_CODEC_OK||!pattern(w->pattern,(uint8_t *)w->pattern,1))return FM1_PROJECT_INVALID;if(session!=w->session)memcpy(session,w->session,FM1_PROJECT_SESSION);if(arena!=w->arena)memcpy(arena,w->arena,w->bank.size);*bank=w->bank;bank->arena=arena;bank->capacity=capacity;*live=*w->pattern;*song=w->song;return FM1_PROJECT_OK;}
