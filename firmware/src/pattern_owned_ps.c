/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_owned_ps.h"
#include <stdint.h>
#include <string.h>
typedef struct { const ps_store *store; uint32_t first,armed; pops_certify_fn certify; void *ctx; } pops_guard;
static int certified(const pops_guard *g) {
 return g->certify(g->ctx,g->first)==1 && g->certify(g->ctx,g->first+PS_SECTOR)==1;
}
static int setup(pops_guard *g,const ps_store *s,uint32_t slot,pops_certify_fn certify,void *ctx) {
 if(!g||!s||!s->read||!s->erase||!s->program||!s->valid||!certify||slot>=PS_TOTAL_SLOTS||
    !s->payload_size||s->payload_size>PS_MAX_PAYLOAD||s->base%PS_SECTOR||
    s->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR) return 0;
 g->store=s;g->first=s->base+slot*2u*PS_SECTOR;g->armed=UINT32_MAX;g->certify=certify;g->ctx=ctx;
 return certified(g);
}
static int guarded_read(void *ctx,uint32_t off,void *dst,uint32_t n) {
 pops_guard *g=ctx;
 if(!certified(g)||off<g->first||off-g->first>2u*PS_SECTOR||n>2u*PS_SECTOR-(off-g->first))return -1;
 return g->store->read(g->store->context,off,dst,n);
}
static int guarded_erase(void *ctx,uint32_t off) {
 pops_guard *g=ctx;g->armed=UINT32_MAX;
 if(!certified(g)||(off!=g->first&&off!=g->first+PS_SECTOR)||g->store->erase(g->store->context,off))return -1;
 g->armed=off;return 0;
}
static int guarded_program(void *ctx,uint32_t off,const void *src,uint32_t n) {
 pops_guard *g=ctx;
 if(!certified(g)||g->armed==UINT32_MAX||off<g->armed||off-g->armed>PS_SECTOR||n>PS_SECTOR-(off-g->armed))return -1;
 return g->store->program(g->store->context,off,src,n);
}
static ps_store wrap(pops_guard *g) {
 ps_store s=*g->store;s.context=g;s.read=guarded_read;s.erase=guarded_erase;s.program=guarded_program;return s;
}
/* Inspect each copy through the real PS validator. An erased view of the
 * other copy allows ps_load to validate one independently, without cloning
 * its record parser or trusting unverified header generations. */
typedef struct { pops_guard *guard; uint32_t masked; } pops_view;
static int view_read(void *ctx,uint32_t off,void *dst,uint32_t n) {
 pops_view *v=ctx;
 if(off>=v->masked && off-v->masked<PS_SECTOR) {
  if(n>PS_SECTOR-(off-v->masked) || !certified(v->guard))return -1;
  memset(dst,255,n);return 0;
 }
 return guarded_read(v->guard,off,dst,n);
}
static int order_ok(pops_guard *g,uint32_t slot,void *scratch) {
 ps_store w=wrap(g);pops_view view;ps_info a,b;int ra,rb;uint32_t distance;
 view.guard=g;view.masked=g->first+PS_SECTOR;w.context=&view;w.read=view_read;
 ra=ps_load(&w,slot,scratch,&a);
 view.masked=g->first;rb=ps_load(&w,slot,scratch,&b);
 if(ra==PS_IO||rb==PS_IO)return PS_IO;
 if(ra==PS_INVALID||rb==PS_INVALID)return PS_INVALID;
 if(ra==PS_OK&&rb==PS_OK) {
  distance=a.generation>b.generation?a.generation-b.generation:b.generation-a.generation;
  /* This wrapper never wraps generations. Equal or widely separated copies
   * have no safe unambiguous chronological ordering. */
  if(!distance||distance>=0x80000000u)return PS_INVALID;
 }
 return PS_OK;
}
int pops_load(const ps_store *s,uint32_t slot,void *scratch,ps_info *info,pops_certify_fn certify,void *ctx) {
 pops_guard g;ps_store w;int r;if(!scratch||!setup(&g,s,slot,certify,ctx))return PS_INVALID;
 r=order_ok(&g,slot,scratch);if(r)return r;w=wrap(&g);return ps_load(&w,slot,scratch,info);
}
int pops_save(const ps_store *s,uint32_t slot,const char *name,const void *payload,void *scratch,ps_info *info,pops_certify_fn certify,void *ctx) {
 pops_guard g;ps_store w;ps_info prev;char saved_name[PS_NAME_BYTES]={0};uint32_t i;int r;
 if(!name||!payload||!scratch||!setup(&g,s,slot,certify,ctx))return PS_INVALID;
 if((uintptr_t)payload<=(uintptr_t)scratch ? (uintptr_t)scratch-(uintptr_t)payload<s->payload_size :
    (uintptr_t)payload-(uintptr_t)scratch<s->payload_size)return PS_INVALID;
 for(i=0;i<PS_NAME_BYTES-1u&&name[i];i++)saved_name[i]=name[i];
 r=order_ok(&g,slot,scratch);if(r)return r;w=wrap(&g);
 r=ps_load(&w,slot,scratch,&prev);
 if(r==PS_OK&&prev.generation==UINT32_MAX)return PS_INVALID;
 if(r!=PS_OK&&r!=PS_EMPTY)return r;
 return ps_save(&w,slot,saved_name,payload,scratch,info);
}
int pops_initialize(const ps_store *s,uint32_t slot,const char *name,const void *payload,void *scratch,ps_info *info,pops_certify_fn certify,void *ctx) {
 pops_guard g;ps_store w;ps_info prev;char expected[PS_NAME_BYTES]={0};uint32_t i;int r;
 if(!name||!payload||!scratch||!setup(&g,s,slot,certify,ctx))return PS_INVALID;
 if((uintptr_t)payload<=(uintptr_t)scratch ? (uintptr_t)scratch-(uintptr_t)payload<s->payload_size :
    (uintptr_t)payload-(uintptr_t)scratch<s->payload_size)return PS_INVALID;
 for(i=0;i<PS_NAME_BYTES-1u&&name[i];i++)expected[i]=name[i];
 if(!s->valid(payload,s->payload_size))return PS_INVALID;
 r=order_ok(&g,slot,scratch);if(r)return r;w=wrap(&g);r=ps_load(&w,slot,scratch,&prev);
 if(r==PS_OK) {
  if(memcmp(scratch,payload,s->payload_size)||memcmp(prev.name,expected,sizeof expected))return PS_INVALID;
  if(info)*info=prev;return PS_OK;
 }
 if(r==PS_EMPTY)return ps_save(&w,slot,expected,payload,scratch,info);
 if(r==PS_CORRUPT)return ps_save_reclaim(&w,slot,expected,payload,scratch,info);
 return r;
}
