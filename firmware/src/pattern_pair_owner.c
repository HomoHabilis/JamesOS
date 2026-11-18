/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_pair_owner.h"
#include "pattern_overlap.h"
#include <string.h>
#define PAGE 256u
static int same_ops(const br_pattern_park_ops*a,const br_pattern_park_ops*b){return a->ctx==b->ctx&&a->fence_and_drain==b->fence_and_drain&&a->release_fence==b->release_fence&&a->park==b->park&&a->is_parked==b->is_parked&&a->read_raw==b->read_raw&&a->program_raw==b->program_raw&&a->erase_raw==b->erase_raw&&a->may_erase==b->may_erase&&a->revoke_loans==b->revoke_loans&&a->resume==b->resume&&a->hold_reset==b->hold_reset&&a->is_contained==b->is_contained;}
static int read_cb(void*,uint32_t,void*,uint32_t);static int erase_cb(void*,uint32_t);static int program_cb(void*,uint32_t,const void*,uint32_t);
static int admitted(pps_owner*o,uint32_t off,uint32_t n){int valid;
 if(!o||!o->active||o->retired)return 0;
 valid=o->park&&o->lease&&o->lease->active==1&&o->lease->holder==o&&o->token&&o->lease->token==o->token&&o->park->fenced==1&&o->park->owner_spent==1&&o->park->owner_claim==o&&o->park->base==o->store.base&&o->park->end>=o->park->base&&o->park->end-o->park->base>=PS_TOTAL_SLOTS*2u*PS_SECTOR&&same_ops(&o->park->ops,&o->pinned_ops)&&o->park->ops.is_parked&&o->park->ops.is_parked(o->park->ops.ctx)==1&&o->store.ctx==o&&o->store.read==read_cb&&o->store.erase==erase_cb&&o->store.program==program_cb&&o->store.validate==o->validate&&o->store.validate_ctx==o->validate_ctx&&!memcmp(o->store.role,o->role,16);
 if(!valid){o->active=0;o->retired=1;o->has_erased=0;o->erase_armed=0;return 0;}
 return n&&off>=o->first&&off-o->first<2u*PS_SECTOR&&n<=2u*PS_SECTOR-(off-o->first);
}
static int read_cb(void*v,uint32_t off,void*dst,uint32_t n){pps_owner*o=v;return !dst||!admitted(o,off,n)?-1:br_pattern_park_read(o->park,off,dst,n);}
int pps_owner_may_erase(pps_owner*o,uint32_t off){
 if(!o||!o->erase_armed||o->armed!=off||!admitted(o,off,PS_SECTOR))return 0;
#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_DELETE_EXPERIMENT) && FM1_PATTERN_DELETE_EXPERIMENT && defined(FM1_DEVICE))
 if(o->delete_guard&&(o->delete_guard(o->delete_guard_ctx)!=1||!admitted(o,off,PS_SECTOR)))return 0;
#endif
 return 1;
}
int pps_owner_reclaim_incompatible(pps_owner*o){unsigned mask=0;int rc;if(!o||!admitted(o,o->first,PS_SECTOR)||o->erase_armed)return PS_INVALID;rc=pps_incompatible_mask(&o->store,o->slot,o->scratch,&mask);if(rc!=PS_OK||!admitted(o,o->first,PS_SECTOR))return rc==PS_IO?PS_IO:PS_INVALID;for(unsigned i=0;i<2;i++)if(mask&(1u<<i)){uint32_t off=o->first+i*PS_SECTOR;unsigned check=0;if(pps_incompatible_mask(&o->store,o->slot,o->scratch,&check)!=PS_OK||!(check&(1u<<i))||!admitted(o,off,PS_SECTOR))return PS_IO;o->armed=off;o->erase_armed=1;rc=br_pattern_park_erase(o->park,off);o->erase_armed=0;if(rc)return PS_IO;}return PS_OK;}
static int erase_cb(void*v,uint32_t off){pps_owner*o=v;unsigned copy;int r;if(!admitted(o,off,PS_SECTOR)||off%PS_SECTOR||o->erase_armed)return -1;copy=(off-o->first)/PS_SECTOR;o->has_erased=0;
 if(pps_erase_allowed(&o->store,o->slot,copy,o->scratch)!=1||!admitted(o,off,PS_SECTOR))return -1;
 o->armed=off;o->erase_armed=1;r=br_pattern_park_erase(o->park,off);o->erase_armed=0;if(r)return -1;
 o->erased=off;o->programmed=0;o->has_erased=1;return 0;
}
static int program_cb(void*v,uint32_t off,const void*src,uint32_t n){pps_owner*o=v;unsigned page;uint16_t bit;
 if(!src||n!=PAGE||off%PAGE||!admitted(o,off,n)||!o->has_erased||off<o->erased||off-o->erased>=PS_SECTOR)return -1;
 page=(off-o->erased)/PAGE;if(page==15)return -1;bit=(uint16_t)(1u<<page);if(o->programmed&bit)return -1;o->programmed|=bit;return br_pattern_park_program(o->park,off,src,n);
}
int pps_owner_bind(pps_owner*o,br_pattern_park_io*park,pps_owner_lease*lease,uint32_t token,uint32_t slot,const uint8_t role[16],pps_validate valid,void*ctx,void*scratch){uint32_t first;
 if(!o||!park||!lease||!token||!role||!valid||!scratch||o->active||o->retired||lease->active!=1||lease->consumed||lease->holder||lease->token!=token||slot>=PS_TOTAL_SLOTS||!park->fenced||park->owner_spent||park->owner_claim||!park->ops.is_parked||park->ops.is_parked(park->ops.ctx)!=1||park->base%PS_SECTOR||park->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR||park->end<=park->base||park->end-park->base<PS_TOTAL_SLOTS*2u*PS_SECTOR||fm1_mem_overlap(o,sizeof *o,park,sizeof *park)||fm1_mem_overlap(o,sizeof *o,lease,sizeof *lease)||fm1_mem_overlap(park,sizeof *park,lease,sizeof *lease)||fm1_mem_overlap(o,sizeof *o,scratch,PAGE)||fm1_mem_overlap(park,sizeof *park,scratch,PAGE)||fm1_mem_overlap(lease,sizeof *lease,scratch,PAGE)||fm1_mem_overlap(role,16,scratch,PAGE)||fm1_mem_overlap(role,16,o,sizeof *o))return 0;
 first=park->base+slot*2u*PS_SECTOR;memset(o,0,sizeof *o);o->park=park;o->pinned_ops=park->ops;o->lease=lease;o->scratch=scratch;o->token=token;o->slot=slot;o->first=first;o->validate=valid;o->validate_ctx=ctx;memcpy(o->role,role,16);
 o->store.ctx=o;o->store.read=read_cb;o->store.program=program_cb;o->store.erase=erase_cb;o->store.base=park->base;memcpy(o->store.role,role,16);o->store.validate=valid;o->store.validate_ctx=ctx;lease->holder=o;lease->consumed=1;park->owner_claim=o;park->owner_spent=1;o->active=1;return 1;
}
pps_store *pps_owner_store(pps_owner*o){return o&&o->active&&!o->retired?&o->store:0;}
int pps_owner_handoff_working(pps_owner*old,pps_owner*next,pps_owner_lease*lease,uint32_t token,void*scratch){br_pattern_park_io*park;uint32_t first;
 if(!old||!next||!lease||!scratch||!token||next==old||next->active||next->retired||old->erase_armed||lease->active!=1||lease->consumed||lease->holder||lease->token!=token||token==old->token||old->slot>=PS_SLOTS||!old->park||lease==old->lease||
    !old->lease||fm1_mem_overlap(next,sizeof *next,old,sizeof *old)||fm1_mem_overlap(next,sizeof *next,old->park,sizeof *old->park)||fm1_mem_overlap(next,sizeof *next,old->lease,sizeof *old->lease)||fm1_mem_overlap(next,sizeof *next,lease,sizeof *lease)||fm1_mem_overlap(next,sizeof *next,scratch,PAGE)||fm1_mem_overlap(lease,sizeof *lease,old,sizeof *old)||fm1_mem_overlap(lease,sizeof *lease,old->park,sizeof *old->park)||fm1_mem_overlap(lease,sizeof *lease,old->lease,sizeof *old->lease)||fm1_mem_overlap(lease,sizeof *lease,scratch,PAGE)||fm1_mem_overlap(old,sizeof *old,scratch,PAGE)||fm1_mem_overlap(old->park,sizeof *old->park,scratch,PAGE)||fm1_mem_overlap(old->lease,sizeof *old->lease,scratch,PAGE))return 0;
 if(!admitted(old,old->first,1))return 0;
 park=old->park;first=park->base+PS_SLOTS*2u*PS_SECTOR;
 memset(next,0,sizeof *next);next->park=park;next->pinned_ops=old->pinned_ops;next->lease=lease;next->scratch=scratch;next->token=token;next->slot=PS_SLOTS;next->first=first;next->validate=old->validate;next->validate_ctx=old->validate_ctx;memcpy(next->role,old->role,16);
 next->store.ctx=next;next->store.read=read_cb;next->store.program=program_cb;next->store.erase=erase_cb;next->store.base=park->base;memcpy(next->store.role,next->role,16);next->store.validate=next->validate;next->store.validate_ctx=next->validate_ctx;
 old->lease->active=0;old->lease->holder=0;old->active=0;old->retired=1;old->has_erased=0;
 lease->holder=next;lease->consumed=1;park->owner_claim=next;next->active=1;return 1;
}
void pps_owner_end(pps_owner*o){if(o&&!o->erase_armed){if(o->park&&o->park->owner_claim==o)o->park->owner_claim=0;if(o->lease&&o->lease->holder==o){o->lease->active=0;o->lease->holder=0;}o->active=0;o->token=0;o->has_erased=0;o->erase_armed=0;o->retired=1;}}

#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_DELETE_EXPERIMENT) && FM1_PATTERN_DELETE_EXPERIMENT && defined(FM1_DEVICE))
/* Experimental only; it has no diagnostic or user-facing entry point. */
#include "pattern_sha256.h"
typedef struct { pps_owner *owner; int (*guard)(void *); void *ctx; } delete_guard;
static int delete_read(void *v,uint32_t off,void *dst,uint32_t n){delete_guard *g=v;return g->guard(g->ctx)==1 && !read_cb(g->owner,off,dst,n) && g->guard(g->ctx)==1 ? 0 : -1;}
static int delete_no_program(void *v,uint32_t off,const void *p,uint32_t n){(void)v;(void)off;(void)p;(void)n;return -1;}
static int delete_no_erase(void *v,uint32_t off){(void)v;(void)off;return -1;}
static int delete_blank(pps_store *s,uint32_t off,uint8_t *page){for(unsigned i=0;i<PS_SECTOR;i+=PAGE){if(s->read(s->ctx,off+i,page,PAGE))return PS_IO;for(unsigned j=0;j<PAGE;j++)if(page[j]!=255u)return PS_CORRUPT;}return PS_OK;}
static int delete_identity(pps_store *s,uint32_t slot,const char *name,const uint8_t *digest,ps_info *info,uint8_t *scratch){pps_reader r;fm1_pattern_sha256 sha;uint8_t page[PAGE],sum[32];uint32_t off,n;int rc=pps_reader_open(s,slot,&r,scratch);if(rc!=PS_OK)return rc;if(pps_reader_info(&r,info)!=PS_OK || memcmp(name,info->name,PS_NAME_BYTES))return PS_INVALID;if(fm1_pattern_sha256_init(&sha))return PS_INVALID;for(off=0;off<r.length;off+=n){n=r.length-off;if(n>PAGE)n=PAGE;if(s->read(s->ctx,s->base+(slot*2u+r.copy)*PS_SECTOR+PAGE+off,page,n))return PS_IO;if(fm1_pattern_sha256_update(&sha,page,n))return PS_INVALID;}if(s->read(s->ctx,s->base+(slot*2u+r.copy)*PS_SECTOR,scratch,PAGE)||memcmp(scratch,r.header,PAGE))return PS_IO;if(fm1_pattern_sha256_final(&sha,sum)||memcmp(sum,digest,32))return PS_INVALID;return PS_OK;}
int pps_owner_delete_experiment(pps_owner *o,const char name[PS_NAME_BYTES],const uint8_t digest[32],int (*guard)(void *),void *ctx){
 delete_guard g;pps_store s;ps_info before,after;uint8_t scratch[PAGE],page[PAGE],hash[32];char nm[PS_NAME_BYTES];unsigned mask;uint32_t oldoff,current;int rc;
 if(!o||!name||!digest||!guard||o->slot>=PS_SLOTS||o->erase_armed||!admitted(o,o->first,PS_SECTOR)||guard(ctx)!=1)return PS_INVALID;
 memcpy(nm,name,sizeof nm);memcpy(hash,digest,sizeof hash);g=(delete_guard){o,guard,ctx};s=o->store;s.ctx=&g;s.read=delete_read;s.program=delete_no_program;s.erase=delete_no_erase;
 rc=pps_incompatible_mask(&s,o->slot,scratch,&mask);if(rc!=PS_OK)return rc;if(mask)return PS_CORRUPT;
 rc=delete_identity(&s,o->slot,nm,hash,&before,scratch);if(rc!=PS_OK)return rc;
 oldoff=o->first+(unsigned)!before.copy*PS_SECTOR;current=o->first+(unsigned)before.copy*PS_SECTOR;
 rc=delete_blank(&s,oldoff,page);if(rc==PS_IO)return rc;if(rc!=PS_OK){if(guard(ctx)!=1)return PS_IO;o->delete_guard=guard;o->delete_guard_ctx=ctx;rc=erase_cb(o,oldoff);o->delete_guard=0;o->delete_guard_ctx=0;if(rc||guard(ctx)!=1)return PS_IO;if(delete_blank(&s,oldoff,page)!=PS_OK)return PS_IO;}
 rc=delete_identity(&s,o->slot,nm,hash,&after,scratch);if(rc!=PS_OK)return rc;if(before.copy!=after.copy||before.generation!=after.generation)return PS_INVALID;
 if(delete_blank(&s,oldoff,page)!=PS_OK||guard(ctx)!=1||!admitted(o,current,PS_SECTOR))return PS_IO;
 o->has_erased=0;o->armed=current;o->delete_guard=guard;o->delete_guard_ctx=ctx;o->erase_armed=1;rc=br_pattern_park_erase(o->park,current);o->erase_armed=0;o->delete_guard=0;o->delete_guard_ctx=0;if(rc||guard(ctx)!=1)return PS_IO;
 if(delete_blank(&s,oldoff,page)!=PS_OK||delete_blank(&s,current,page)!=PS_OK||guard(ctx)!=1)return PS_IO;
 return PS_OK;
}
/* A fresh caller explicitly authorizes discarding the incompatible sibling.
 * Pin and validate the surviving identity before allowing any erase. */
int pps_owner_delete_retry_experiment(pps_owner *o,const char name[PS_NAME_BYTES],const uint8_t digest[32],int (*guard)(void *),void *ctx){
 delete_guard g;pps_store s;ps_info info;uint8_t scratch[PAGE],hash[32];char nm[PS_NAME_BYTES];unsigned mask;int rc;
 if(!o||!name||!digest||!guard||o->erase_armed||!admitted(o,o->first,PS_SECTOR)||guard(ctx)!=1)return PS_INVALID;
 memcpy(nm,name,sizeof nm);memcpy(hash,digest,sizeof hash);g=(delete_guard){o,guard,ctx};s=o->store;s.ctx=&g;s.read=delete_read;s.program=delete_no_program;s.erase=delete_no_erase;
 rc=delete_identity(&s,o->slot,nm,hash,&info,scratch);if(rc!=PS_OK)return rc;
 rc=pps_incompatible_mask(&s,o->slot,scratch,&mask);if(rc!=PS_OK)return rc;
 if(mask && mask!=(1u<<(unsigned)!info.copy))return PS_CORRUPT;
 if(mask){if(guard(ctx)!=1)return PS_IO;o->delete_guard=guard;o->delete_guard_ctx=ctx;rc=pps_owner_reclaim_incompatible(o);o->delete_guard=0;o->delete_guard_ctx=0;if(rc!=PS_OK)return rc;}
 if(guard(ctx)!=1)return PS_IO;
 return pps_owner_delete_experiment(o,nm,hash,guard,ctx);
}
/* An unavailable slot has no reliably identifiable project name. This path
 * requires its own explicit authorization and an exact raw-pair fingerprint;
 * it refuses a pair with a loadable survivor, which uses the named path. */
int pps_owner_clear_unreadable_experiment(pps_owner *o,const uint8_t pair_digest[32],int (*guard)(void *),void *ctx){
 delete_guard g;pps_store s;pps_reader reader;fm1_pattern_sha256 sha;uint8_t page[PAGE],sum[32],expected[32];uint32_t off;int rc;
 if(!o||!pair_digest||!guard||o->erase_armed||!admitted(o,o->first,PS_SECTOR)||guard(ctx)!=1)return PS_INVALID;
 memcpy(expected,pair_digest,sizeof expected);g=(delete_guard){o,guard,ctx};s=o->store;s.ctx=&g;s.read=delete_read;s.program=delete_no_program;s.erase=delete_no_erase;
 rc=pps_reader_open(&s,o->slot,&reader,page);if(rc!=PS_CORRUPT)return rc==PS_IO?PS_IO:PS_INVALID;
 if(fm1_pattern_sha256_init(&sha))return PS_INVALID;
 for(off=0;off<2u*PS_SECTOR;off+=PAGE){if(s.read(s.ctx,o->first+off,page,PAGE)||fm1_pattern_sha256_update(&sha,page,PAGE))return PS_IO;}
 if(fm1_pattern_sha256_final(&sha,sum)||memcmp(sum,expected,32))return PS_INVALID;
 for(unsigned copy=0;copy<2;copy++){
  off=o->first+copy*PS_SECTOR;rc=delete_blank(&s,off,page);if(rc==PS_IO)return rc;if(rc==PS_OK)continue;
  if(guard(ctx)!=1||!admitted(o,off,PS_SECTOR))return PS_IO;
  o->armed=off;o->delete_guard=guard;o->delete_guard_ctx=ctx;o->erase_armed=1;
  rc=br_pattern_park_erase(o->park,off);o->erase_armed=0;o->delete_guard=0;o->delete_guard_ctx=0;
  if(rc||guard(ctx)!=1||delete_blank(&s,off,page)!=PS_OK)return PS_IO;
 }
 return delete_blank(&s,o->first,page)==PS_OK&&delete_blank(&s,o->first+PS_SECTOR,page)==PS_OK&&guard(ctx)==1?PS_OK:PS_IO;
}
#endif
