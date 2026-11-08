/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_park_io.h"
#include <string.h>
enum { IDLE, PARKED, FAILED };
static int fail(br_pattern_park_io *t) {
    if(t->state!=FAILED) { t->state=FAILED; t->ops.hold_reset(t->ops.ctx); }
    return BR_PARK_IO;
}
static int valid_range(const br_pattern_park_io *t,uint32_t off,uint32_t n) {
    return n && off>=t->base && off<t->end && n<=t->end-off;
}
static int check(br_pattern_park_io *t) {
    if(!t || t->state!=PARKED) return BR_PARK_INVALID;
    return t->fenced && t->ops.is_parked(t->ops.ctx)==1 ? BR_PARK_OK : fail(t);
}
int br_pattern_park_begin(br_pattern_park_io *t,const br_pattern_park_ops *o,uint32_t base,uint32_t end) {
    if(!t || !o || !o->fence_and_drain || !o->release_fence || !o->park || !o->is_parked || !o->read_raw || !o->program_raw ||
       !o->erase_raw || !o->may_erase || !o->revoke_loans || !o->resume || !o->hold_reset || !o->is_contained ||
       base>=end || (base&4095u) || (end&4095u)) return BR_PARK_INVALID;
    if(t->state!=IDLE) return BR_PARK_BUSY;
    t->ops=*o; t->base=base; t->end=end;
    { int r=o->fence_and_drain(o->ctx);
      if(r>0) { memset(t,0,sizeof *t); return BR_PARK_BUSY; }
      if(r<0) return fail(t); }
    t->fenced=1;
    if(o->park(o->ctx) || o->is_parked(o->ctx)!=1) return fail(t);
    t->state=PARKED; return BR_PARK_OK;
}
int br_pattern_park_read(void *v,uint32_t off,void *dst,uint32_t n) {
    br_pattern_park_io *t=v; uint8_t *p=dst; uint32_t k;
    if(!t || !p || !valid_range(t,off,n)) return BR_PARK_INVALID;
    while(n) { if(check(t)) return BR_PARK_IO; k=n>256u?256u:n;
        if(t->ops.read_raw(t->ops.ctx,off,p,k)) return fail(t);
        off+=k; p+=k; n-=k;
    }
    return BR_PARK_OK;
}
int br_pattern_park_program(void *v,uint32_t off,const void *src,uint32_t n) {
    br_pattern_park_io *t=v; const uint8_t *p=src; uint32_t k;
    if(!t || !p || !valid_range(t,off,n)) return BR_PARK_INVALID;
    while(n) { if(check(t)) return BR_PARK_IO; k=256u-(off&255u); if(k>n) k=n;
        if(t->ops.program_raw(t->ops.ctx,off,p,k)) return fail(t);
        off+=k; p+=k; n-=k;
    }
    return BR_PARK_OK;
}
int br_pattern_park_erase(void *v,uint32_t off) {
    br_pattern_park_io *t=v;
    if(!t || (off&4095u) || !valid_range(t,off,4096u)) return BR_PARK_INVALID;
    if(check(t)) return BR_PARK_IO;
    if(t->ops.may_erase(t->ops.ctx,off)!=1) return BR_PARK_INVALID;
    if(check(t)) return BR_PARK_IO;
    return t->ops.erase_raw(t->ops.ctx,off) ? fail(t) : BR_PARK_OK;
}
int br_pattern_park_end(br_pattern_park_io *t) {
    int r;
    if(!t || (t->state!=PARKED && t->state!=FAILED)) return BR_PARK_INVALID;
    /* An active owner may still hold page-write capabilities and scratch.
     * It must retire before loans can be revoked or the fence released. */
    if(t->owner_claim) return BR_PARK_BUSY;
    /* Never release borrowed memory while a failed reset leaves an unknown
     * CPU1 reader. Keep fence and loans held until containment is verified. */
    if(t->state==FAILED && t->fenced && t->ops.is_contained(t->ops.ctx)!=1) return BR_PARK_IO;
    r=t->fenced ? t->ops.revoke_loans(t->ops.ctx) : 0;
    if(r || t->state==FAILED) return fail(t);
    if(t->ops.is_parked(t->ops.ctx)!=1 || t->ops.resume(t->ops.ctx)) return fail(t);
    if(t->ops.release_fence(t->ops.ctx)) return fail(t);
    memset(t,0,sizeof *t); return BR_PARK_OK;
}

int br_pattern_park_faulted(const br_pattern_park_io *t) { return t && t->state==FAILED; }

int br_pattern_park_abort(br_pattern_park_io *t) { return t && t->state==PARKED ? fail(t) : BR_PARK_INVALID; }
