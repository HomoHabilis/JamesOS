/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_pair_stream.h"
#include <string.h>
#define PAGE 256u
#define MAGIC 0x32535050u
#define COMMIT 0x32435050u
static uint32_t get(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static uint32_t crcadd(uint32_t c,const uint8_t*p,uint32_t n){uint32_t i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return c;}
static uint32_t crc(const uint8_t*p,uint32_t n){return ~crcadd(~0u,p,n);}
static int blank(const uint8_t*p,uint32_t n){while(n--)if(*p++!=255u)return 0;return 1;}
static int overlap(const void*a,size_t an,const void*b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
static int config(const pps_store*s,uint32_t slot,const void*scratch){uint32_t i;unsigned nonzero=0,nonff=0;if(!s||!scratch||!s->read||!s->program||!s->erase||!s->validate||slot>=PS_TOTAL_SLOTS||s->base%PS_SECTOR||s->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR)return 0;for(i=0;i<sizeof s->role;i++){nonzero|=s->role[i]!=0;nonff|=s->role[i]!=255u;}return nonzero&&nonff;}
static uint32_t addr(const pps_store*s,uint32_t slot,unsigned copy){return s->base+(slot*2u+copy)*PS_SECTOR;}
typedef struct {const pps_store*s;uint32_t start,len;} reader;
static int bounded_read(const pps_store*s,uint32_t off,void*out,uint32_t n){uint8_t*p=out;uint32_t part;while(n){part=n>PAGE?PAGE:n;if(s->read(s->ctx,off,p,part))return -1;off+=part;p+=part;n-=part;}return 0;}
static int storage_read(void*v,uint32_t off,void*out,uint32_t n){reader*r=v;if(!r||(!out&&n)||off>r->len||n>r->len-off)return -1;return bounded_read(r->s,r->start+off,out,n);}
static int scan_blank(const pps_store*s,uint32_t off,uint8_t*scratch){uint32_t i;for(i=0;i<PS_SECTOR;i+=PAGE){if(s->read(s->ctx,off+i,scratch,PAGE))return PS_IO;if(!blank(scratch,PAGE))return PS_CORRUPT;}return PS_EMPTY;}
static int inspect(const pps_store*s,uint32_t slot,unsigned copy,uint8_t*scratch,uint8_t head[PAGE]){uint8_t again[PAGE];uint32_t i,n,remaining,c=~0u,part;reader r;
 if(s->read(s->ctx,addr(s,slot,copy),head,PAGE))return PS_IO;
 if(get(head)!=MAGIC||memcmp(head+4,s->role,16)||get(head+20)!=slot||get(head+24)!=copy||!get(head+28)||!(n=get(head+32))||n>PPS_MAX_PAYLOAD||head[52]||head[53]||head[54]||head[55]||get(head+88)!=crc(head,88)||get(head+92)!=COMMIT||!blank(head+96,PAGE-96))return PS_CORRUPT;
 remaining=n;
 for(i=1;i<16;i++){if(s->read(s->ctx,addr(s,slot,copy)+i*PAGE,scratch,PAGE))return PS_IO;part=remaining<PAGE?remaining:PAGE;if(part){c=crcadd(c,scratch,part);remaining-=part;}if(!blank(scratch+part,PAGE-part))return PS_CORRUPT;}
 if(remaining||~c!=get(head+36)||!blank(head+56,32))return PS_CORRUPT;
 r.s=s;r.start=addr(s,slot,copy)+PAGE;r.len=n;if(s->validate(s->validate_ctx,storage_read,&r,n)!=1)return PS_CORRUPT;
 if(s->read(s->ctx,addr(s,slot,copy),again,PAGE))return PS_IO;if(memcmp(head,again,PAGE))return PS_IO;return PS_OK;
}
static int select_pair(const pps_store*s,uint32_t slot,uint8_t*scratch,uint8_t head[2][PAGE],int*selected,int state[2]){uint32_t a,b;state[0]=inspect(s,slot,0,scratch,head[0]);state[1]=inspect(s,slot,1,scratch,head[1]);if(state[0]==PS_IO||state[1]==PS_IO)return PS_IO;if(state[0]!=PS_OK&&state[1]!=PS_OK){*selected=-1;return PS_EMPTY;}if(state[0]==PS_OK&&state[1]!=PS_OK){*selected=0;return PS_OK;}if(state[1]==PS_OK&&state[0]!=PS_OK){*selected=1;return PS_OK;}a=get(head[0]+28);b=get(head[1]+28);if(a==b||(a>b?a-b:b-a)!=1)return PS_CORRUPT;*selected=a>b?0:1;return PS_OK;}
int pps_erase_allowed(const pps_store*s,uint32_t slot,unsigned target,void*scratch){uint8_t heads[2][PAGE];int state[2],selected,r;
 if(!config(s,slot,scratch)||target>1||overlap(s,sizeof *s,scratch,PAGE))return 0;
 r=select_pair(s,slot,scratch,heads,&selected,state);
 if(r==PS_EMPTY)return scan_blank(s,addr(s,slot,0),scratch)==PS_EMPTY&&scan_blank(s,addr(s,slot,1),scratch)==PS_EMPTY;
 if(r!=PS_OK||target==(unsigned)selected)return 0;
 if(state[target]==PS_OK)return 1;
 return scan_blank(s,addr(s,slot,target),scratch)==PS_EMPTY;
}
static void build(uint8_t*h,const pps_store*s,uint32_t slot,unsigned copy,uint32_t gen,uint32_t len,const uint8_t name[PS_NAME_BYTES],uint32_t sum){memset(h,255,PAGE);put(h,MAGIC);memcpy(h+4,s->role,16);put(h+20,slot);put(h+24,copy);put(h+28,gen);put(h+32,len);put(h+36,sum);memcpy(h+40,name,PS_NAME_BYTES);memset(h+53,0,3);put(h+88,crc(h,88));put(h+92,COMMIT);}
int pps_save(const pps_store*s,uint32_t slot,const char*name,uint32_t len,pps_read_at source,void*source_ctx,void*scratch,ps_info*info){uint8_t heads[2][PAGE],header[PAGE],nm[PS_NAME_BYTES]={0};uint32_t i,part,c=~0u,gen,off;unsigned target;int state[2],k,r;
 if(!config(s,slot,scratch)||!name||!source||!len||len>PPS_MAX_PAYLOAD||overlap(s,sizeof *s,scratch,PAGE)||(info&&(overlap(info,sizeof *info,scratch,PAGE)||overlap(info,sizeof *info,s,sizeof *s))))return PS_INVALID;for(i=0;i<PS_NAME_BYTES-1&&name[i];i++)nm[i]=(uint8_t)name[i];
 if(s->validate(s->validate_ctx,source,source_ctx,len)!=1)return PS_INVALID;
 for(i=0;i<len;i+=part){part=len-i;if(part>PAGE)part=PAGE;if(source(source_ctx,i,scratch,part))return PS_IO;c=crcadd(c,scratch,part);}
 r=select_pair(s,slot,scratch,heads,&k,state);if(r!=PS_OK&&r!=PS_EMPTY)return r;
 if(k<0){r=scan_blank(s,addr(s,slot,0),scratch);if(r!=PS_EMPTY)return r;r=scan_blank(s,addr(s,slot,1),scratch);if(r!=PS_EMPTY)return r;target=0;gen=1;}
 else{target=(unsigned)!k;gen=get(heads[k]+28);if(gen==UINT32_MAX)return PS_INVALID;gen++;if(state[target]!=PS_OK){r=scan_blank(s,addr(s,slot,target),scratch);if(r!=PS_EMPTY)return r;}}
 off=addr(s,slot,target);if(s->erase(s->ctx,off))return PS_IO;r=scan_blank(s,off,scratch);if(r!=PS_EMPTY)return PS_IO;
 for(i=0;i<len;i+=part){part=len-i;if(part>PAGE)part=PAGE;memset(scratch,255,PAGE);if(source(source_ctx,i,scratch,part))return PS_IO;if(!blank(scratch,PAGE)&&s->program(s->ctx,off+PAGE+i,scratch,PAGE))return PS_IO;}
 build(header,s,slot,target,gen,len,nm,~c);if(s->program(s->ctx,off,header,PAGE))return PS_IO;
 r=inspect(s,slot,target,scratch,heads[target]);if(r!=PS_OK||memcmp(header,heads[target],PAGE))return PS_IO;if(info){info->copy=(int)target;info->generation=gen;memcpy(info->name,nm,PS_NAME_BYTES);}return PS_OK;
}
int pps_load(const pps_store*s,uint32_t slot,void*out,uint32_t cap,uint32_t*length,void*scratch,ps_info*info){uint8_t heads[2][PAGE];int state[2],k,r;uint32_t n;
 if(!config(s,slot,scratch)||!out||!length||overlap(s,sizeof *s,scratch,PAGE)||overlap(out,cap,scratch,PAGE)||overlap(length,sizeof *length,scratch,PAGE)||overlap(out,cap,length,sizeof *length)||overlap(s,sizeof *s,out,cap)||overlap(s,sizeof *s,length,sizeof *length)||(info&&(overlap(info,sizeof *info,scratch,PAGE)||overlap(info,sizeof *info,out,cap)||overlap(info,sizeof *info,length,sizeof *length)||overlap(info,sizeof *info,s,sizeof *s))))return PS_INVALID;
 r=select_pair(s,slot,scratch,heads,&k,state);if(r==PS_EMPTY){r=scan_blank(s,addr(s,slot,0),scratch);if(r!=PS_EMPTY)return r;return scan_blank(s,addr(s,slot,1),scratch);}if(r)return r;n=get(heads[k]+32);*length=n;if(cap<n)return PS_INVALID;
 if(bounded_read(s,addr(s,slot,(unsigned)k)+PAGE,out,n))return PS_IO;
 /* Caller must discard output if the pinned header or payload changed. */
 if(crc(out,n)!=get(heads[k]+36))return PS_IO;
 if(s->read(s->ctx,addr(s,slot,(unsigned)k),scratch,PAGE)||memcmp(scratch,heads[k],PAGE))return PS_IO;
 if(info){info->copy=k;info->generation=get(heads[k]+28);memcpy(info->name,heads[k]+40,PS_NAME_BYTES);}return PS_OK;
}
int pps_reader_open(const pps_store*s,uint32_t slot,pps_reader*out,void*scratch){uint8_t heads[2][PAGE];int state[2],k,r;
 if(!config(s,slot,scratch)||!out||overlap(out,sizeof *out,scratch,PAGE)||overlap(out,sizeof *out,s,sizeof *s)||overlap(s,sizeof *s,scratch,PAGE))return PS_INVALID;
 r=select_pair(s,slot,scratch,heads,&k,state);if(r==PS_EMPTY){r=scan_blank(s,addr(s,slot,0),scratch);if(r!=PS_EMPTY)return r;return scan_blank(s,addr(s,slot,1),scratch);}if(r)return r;
 memset(out,0,sizeof *out);out->store=s;out->slot=slot;out->copy=(uint32_t)k;out->length=get(heads[k]+32);memcpy(out->header,heads[k],PAGE);return PS_OK;
}
int pps_reader_info(const pps_reader*r,ps_info*info){const uint8_t*h;if(!r||!info||!r->store||overlap(info,sizeof *info,r,sizeof *r)||overlap(info,sizeof *info,r->store,sizeof *r->store))return PS_INVALID;
 h=r->header;if(r->slot>=PS_TOTAL_SLOTS||r->copy>1||!r->length||r->length>PPS_MAX_PAYLOAD||get(h)!=MAGIC||memcmp(h+4,r->store->role,16)||get(h+20)!=r->slot||get(h+24)!=r->copy||!get(h+28)||get(h+32)!=r->length||h[52]||h[53]||h[54]||h[55]||!blank(h+56,32)||get(h+88)!=crc(h,88)||get(h+92)!=COMMIT||!blank(h+96,PAGE-96))return PS_INVALID;
 info->copy=(int)r->copy;info->generation=get(h+28);memcpy(info->name,h+40,PS_NAME_BYTES);return PS_OK;
}
int pps_reader_equal(const pps_reader*r,const uint8_t*expected,uint32_t len,void*scratch){uint8_t head[PAGE];ps_info info;uint32_t off,part;int rc;
 if(!r||!expected||!config(r->store,r->slot,scratch)||len!=r->length||overlap(r,sizeof *r,scratch,PAGE)||overlap(r->store,sizeof *r->store,scratch,PAGE)||overlap(expected,len,scratch,PAGE)||overlap(expected,len,r,sizeof *r)||overlap(expected,len,r->store,sizeof *r->store)||pps_reader_info(r,&info)!=PS_OK)return PS_INVALID;
 rc=inspect(r->store,r->slot,r->copy,scratch,head);if(rc!=PS_OK||memcmp(head,r->header,PAGE))return rc==PS_IO?PS_IO:PS_CORRUPT;
 for(off=0;off<len;off+=part){part=len-off;if(part>PAGE)part=PAGE;if(r->store->read(r->store->ctx,addr(r->store,r->slot,r->copy)+PAGE+off,scratch,part))return PS_IO;if(memcmp(scratch,expected+off,part))return PS_INVALID;}
 if(r->store->read(r->store->ctx,addr(r->store,r->slot,r->copy),scratch,PAGE)||memcmp(scratch,r->header,PAGE))return PS_IO;return PS_OK;
}
int pps_reader_read_at(const pps_reader*r,uint32_t offset,void*out,uint32_t n,void*scratch){uint8_t head[PAGE];int rc;
 if(!r||!config(r->store,r->slot,scratch)||r->copy>1||!r->length||r->length>PPS_MAX_PAYLOAD||get(r->header+32)!=r->length||offset>r->length||n>r->length-offset||(!out&&n)||overlap(r,sizeof *r,scratch,PAGE)||overlap(r->store,sizeof *r->store,scratch,PAGE)||(n&&(overlap(out,n,scratch,PAGE)||overlap(out,n,r,sizeof *r)||overlap(out,n,r->store,sizeof *r->store))))return PS_INVALID;
 rc=inspect(r->store,r->slot,r->copy,scratch,head);if(rc!=PS_OK||memcmp(head,r->header,PAGE))return rc==PS_IO?PS_IO:PS_CORRUPT;
 if(n&&bounded_read(r->store,addr(r->store,r->slot,r->copy)+PAGE+offset,out,n))return PS_IO;
 if(r->store->read(r->store->ctx,addr(r->store,r->slot,r->copy),scratch,PAGE)||memcmp(scratch,r->header,PAGE))return PS_IO;return PS_OK;
}
