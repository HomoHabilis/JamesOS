/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_journal_gate.h"
#include <string.h>
#define FIRST 256u
#define ENTRY 48u
#define LIMIT (PFS_SECTOR-256u)
#define COMMIT 0x32434e50u
static uint32_t get(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int blank(const uint8_t*p,size_t n){while(n--)if(*p++!=255u)return 0;return 1;}
static int zeroes(const uint8_t*p,size_t n){while(n--)if(*p++)return 0;return 1;}
static int overlaps(const void*a,size_t an,const void*b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
static int root_read(pns_journal_gate*g) {return g->ns->store->read(g->ns->store->ctx,g->ns->store->base+g->ns->root*PFS_SECTOR,g->scratch,PFS_SECTOR);}
static int hash(pns_journal_gate*g,uint8_t out[32]) {return g->ns->digest(g->ns->ctx,g->scratch,PFS_SECTOR,out);}
int pns_journal_gate_arm(pns_journal_gate*g,const pns_namespace*n,void*ctx,
 int (*exclusive)(void *),int (*write_authorized)(void *,uint32_t,const void *,uint32_t),
 const uint8_t event[48],void *scratch)
{
 pns_namespace selected;uint32_t slot,type,at;uint8_t digest[32];
 if(!g||!n||!event||!scratch||!exclusive||!write_authorized||exclusive(ctx)!=1||g->phase ||
    overlaps(event,48,scratch,PFS_SECTOR)||overlaps(g,sizeof *g,scratch,PFS_SECTOR))return PNS_INVALID;
 selected=*n;if(pns_open_pair(&selected,scratch)!=PNS_OK||selected.root!=n->root||selected.peer!=n->peer)return PNS_INVALID;
 slot=get(event);type=get(event+4);
 if(slot>=PFS_COUNT||slot==n->root|| (slot==n->peer && type!=3u) ||(type!=1u&&type!=2u&&type!=3u)||get(event+44)!=COMMIT||get(event+40)!=pns_crc32(event,40))return PNS_INVALID;
 if(type==1u) {
  if(n->reserved(n->ctx,n->store->base+slot*PFS_SECTOR)!=1 ||
     n->store->read(n->store->ctx,n->store->base+slot*PFS_SECTOR,scratch,PFS_SECTOR) ||
     n->digest(n->ctx,scratch,PFS_SECTOR,digest) || memcmp(digest,event+8,32))return PNS_INVALID;
 } else if(type==3u) {
  if(slot!=n->peer || n->reserved(n->ctx,n->store->base+slot*PFS_SECTOR)!=1 ||
     n->store->read(n->store->ctx,n->store->base+slot*PFS_SECTOR,scratch,PFS_SECTOR) ||
     !blank(scratch,PFS_SECTOR) || pns_rotation_expected(&selected,scratch,digest)!=PNS_OK ||
     memcmp(digest,event+8,32))return PNS_INVALID;
 } else if(n->reclaimable(n->ctx,n->store->base+slot*PFS_SECTOR)!=1 || pns_owned(n,slot,scratch)!=1 || !zeroes(event+8,32)) return PNS_INVALID;
 memset(g,0,sizeof *g);g->ns=n;g->pinned=*n;g->pinned_store=*n->store;g->ctx=ctx;g->scratch=scratch;g->exclusive=exclusive;g->write_authorized=write_authorized;
 if(root_read(g)||hash(g,g->before))return PNS_IO;
 for(at=FIRST;at+ENTRY<=LIMIT;at+=ENTRY)if(blank(g->scratch+at,ENTRY))break;
 if(at+ENTRY>LIMIT || (type!=3u && at+2u*ENTRY>LIMIT))return PNS_FULL;
 g->cursor=at;memcpy(g->entry,event,ENTRY);memcpy(g->scratch+at,event,44);
 if(hash(g,g->body))return PNS_IO;memcpy(g->scratch+at+44,event+44,4);
 if(hash(g,g->after))return PNS_IO;g->phase=1;return PNS_OK;
}
int pns_journal_gate_program(void *v,uint32_t off,const void *data,uint32_t n)
{
 pns_journal_gate*g=v;uint8_t actual[32];uint32_t base;
 if(!g||!g->phase||!g->ns||!data||!pns_namespace_unchanged(g->ns,&g->pinned,&g->pinned_store)||g->exclusive(g->ctx)!=1)return -1;
 base=g->ns->store->base+g->ns->root*PFS_SECTOR+g->cursor;
 if((g->phase==1 && (off!=base||n!=44u||memcmp(data,g->entry,44))) ||
    (g->phase==2 && (off!=base+44u||n!=4u||memcmp(data,g->entry+44,4))) || g->phase>2)return -1;
 if(root_read(g)||hash(g,actual)||memcmp(actual,g->phase==1?g->before:g->body,32))return -1;
 if(g->write_authorized(g->ctx,off,data,n))return -1;
 if(!pns_namespace_unchanged(g->ns,&g->pinned,&g->pinned_store)||g->exclusive(g->ctx)!=1||root_read(g)||hash(g,actual)||memcmp(actual,g->phase==1?g->body:g->after,32))return -1;
 ++g->phase;return 0;
}
void pns_journal_gate_disarm(pns_journal_gate*g){if(g)memset(g,0,sizeof *g);}
