/* SPDX-License-Identifier: GPL-3.0-only */
#if !defined(BR_PATTERN_BANK_EXPERIMENT) || !BR_PATTERN_BANK_EXPERIMENT
#error "Pattern bank is an opt-in host-only experiment"
#endif
#include "pattern_bank.h"
#include "pattern_storage_codec.h"
#include <string.h>

static int intersects(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if(!an || !bn) return 0;
    return x<=y ? y-x<an : x-y<bn;
}
static int valid(const br_pattern_bank *b) {
    size_t sum=0; unsigned i,j;
    if(!b || !b->arena || !b->capacity || b->size>b->capacity ||
       b->active>=FM1_PATTERN_COUNT || !(b->used&(1u<<b->active)) ||
       (b->used & ~((1u<<FM1_PATTERN_COUNT)-1u)) || b->reserved>1u ||
       (b->reserved && (b->reserved_target>=FM1_PATTERN_COUNT || b->reserved_target==b->active))) return 0;
    for(i=0;i<FM1_PATTERN_COUNT;i++) {
        size_t o=b->offset[i], n=b->length[i];
        if(!n) { if(i!=b->active && (b->used&(1u<<i))) return 0; continue; }
        if(!(b->used&(1u<<i)) || o>b->size || n>b->size-o || sum>b->size-n) return 0;
        for(j=0;j<i;j++) if(b->length[j] && o<b->offset[j]+b->length[j] && b->offset[j]<o+n) return 0;
        sum+=n;
    }
    if(b->clip_length) {
        size_t o=b->clip_offset, n=b->clip_length;
        if(o>b->size || n>b->size-o || sum>b->size-n) return 0;
        for(i=0;i<FM1_PATTERN_COUNT;i++)
            if(b->length[i] && o<b->offset[i]+b->length[i] && b->offset[i]<o+n) return 0;
        sum+=n;
    }
    return sum==b->size;
}
int br_pattern_bank_verify(const br_pattern_bank *b) {
    unsigned i;
    if(!valid(b)) return BR_BANK_INVALID;
    for(i=0;i<FM1_PATTERN_COUNT;i++) if(b->length[i] &&
       fm1_pattern_codec_validate(b->arena+b->offset[i],b->length[i],sizeof(br_pattern_snapshot))!=FM1_CODEC_OK)
        return BR_BANK_CORRUPT;
    if(b->clip_length && fm1_pattern_codec_validate(b->arena+b->clip_offset,b->clip_length,
       sizeof(br_pattern_snapshot))!=FM1_CODEC_OK) return BR_BANK_CORRUPT;
    return BR_BANK_OK;
}
int br_pattern_bank_init(br_pattern_bank *b, uint8_t *arena, size_t capacity) {
    if(!b || !arena || !capacity || intersects(b,sizeof *b,arena,capacity)) return BR_BANK_INVALID;
    memset(b,0,sizeof *b); b->arena=arena; b->capacity=capacity; b->used=1u; return BR_BANK_OK;
}
static int buffers(const br_pattern_bank *b, const void *live, const void *work,
                   const void *encoded, size_t cap) {
    size_t sz=sizeof(br_pattern_snapshot);
    if(!live || !work || !encoded || !cap) return 0;
    return !intersects(live,sz,work,sz) && !intersects(live,sz,encoded,cap) &&
      !intersects(work,sz,encoded,cap) && !intersects(b->arena,b->capacity,live,sz) &&
      !intersects(b->arena,b->capacity,work,sz) && !intersects(b->arena,b->capacity,encoded,cap) &&
      !intersects(b,sizeof *b,live,sz) && !intersects(b,sizeof *b,work,sz) &&
      !intersects(b,sizeof *b,encoded,cap);
}
static int pack(const br_pattern_snapshot *s, uint8_t *out, size_t cap, size_t *n) {
    int r=fm1_pattern_codec_encode((const uint8_t *)s,sizeof *s,out,cap,n);
    return r==FM1_CODEC_OK?BR_BANK_OK:r==FM1_CODEC_FULL?BR_BANK_FULL:BR_BANK_INVALID;
}
static int unpack(const br_pattern_bank *b, unsigned slot, br_pattern_snapshot *dst) {
    return fm1_pattern_codec_decode(b->arena+b->offset[slot],b->length[slot],
             (uint8_t *)dst,sizeof *dst)==FM1_CODEC_OK?BR_BANK_OK:BR_BANK_CORRUPT;
}
static int fits(const br_pattern_bank *b, unsigned slot, size_t n) {
    return n<=b->capacity-(b->size-b->length[slot]);
}
/* The decoded target becomes the active live snapshot and its old encoded
 * copy can be reclaimed when the outgoing snapshot is committed. */
static int fits_switch(const br_pattern_bank *b,unsigned target,size_t n) {
    return n<=b->capacity-(b->size-b->length[b->active]-b->length[target]);
}
/* Caller has checked all capacity and validation; this cannot fail. */
static void replace(br_pattern_bank *b, unsigned slot, const uint8_t *data, size_t n) {
    size_t old=b->length[slot], off=b->offset[slot]; unsigned i;
    if(old) {
        memmove(b->arena+off,b->arena+off+old,b->size-off-old);
        b->size-=old;
        for(i=0;i<FM1_PATTERN_COUNT;i++) if(i!=slot && b->length[i] && b->offset[i]>off) b->offset[i]-=old;
        if(b->clip_length && b->clip_offset>off) b->clip_offset-=old;
    }
    b->offset[slot]=b->size; b->length[slot]=n;
    memcpy(b->arena+b->size,data,n); b->size+=n; b->used|=(uint16_t)(1u<<slot);
}
int br_pattern_bank_prepare(br_pattern_bank *b, unsigned target,
    const br_pattern_snapshot *live, br_pattern_snapshot *work, size_t encoded_capacity) {
    /* A full 64-byte literal group costs one header. A short literal group
     * immediately followed by a special run can charge its header to that
     * run's saving: zero runs save >=1 byte, as do repeat/match runs. Only
     * a trailing short group may remain uncharged. Thus the total is bounded
     * by n + ceil(n/64) + the 12-byte codec header, for all byte values. */
    const size_t worst=sizeof *live+(sizeof *live+63u)/64u+12u;
    if(!valid(b) || b->reserved || !live || !work || target>=FM1_PATTERN_COUNT || target==b->active ||
       intersects(live,sizeof *live,work,sizeof *work) ||
       intersects(b->arena,b->capacity,live,sizeof *live) ||
       intersects(b->arena,b->capacity,work,sizeof *work) ||
       intersects(b,sizeof *b,live,sizeof *live) || intersects(b,sizeof *b,work,sizeof *work)) return BR_BANK_INVALID;
    if(encoded_capacity<worst || !fits_switch(b,target,worst)) return BR_BANK_FULL;
    if(b->used&(1u<<target)) {
        int r=unpack(b,target,work); if(r!=BR_BANK_OK) return r;
    } else memcpy(work,live,sizeof *work);
    b->reserved=1; b->reserved_target=(uint8_t)target; return BR_BANK_OK;
}
int br_pattern_bank_cancel(br_pattern_bank *b) {
    if(!valid(b) || !b->reserved) return BR_BANK_INVALID;
    b->reserved=0; return BR_BANK_OK;
}
int br_pattern_bank_commit(br_pattern_bank *b, br_pattern_snapshot *live,
    const br_pattern_snapshot *work, uint8_t *encoded, size_t cap) {
    size_t n; int r; unsigned target;
    if(!valid(b) || !b->reserved || !buffers(b,live,work,encoded,cap)) return BR_BANK_INVALID;
    r=pack(live,encoded,cap,&n); if(r!=BR_BANK_OK) return r;
    target=b->reserved_target;
    if(!fits_switch(b,target,n)) return BR_BANK_FULL;
    /* The target is already staged and the only remaining operations cannot
     * fail. Do not release its bytes at prepare: cancel must be lossless. */
    if(b->length[target]) replace(b,target,encoded,0u);
    replace(b,b->active,encoded,n);
    memcpy(live,work,sizeof *live); b->active=(uint8_t)target;
    b->used|=(uint16_t)(1u<<target); b->reserved=0; return BR_BANK_OK;
}
int br_pattern_bank_select(br_pattern_bank *b, unsigned target, br_pattern_snapshot *live,
    br_pattern_snapshot *work, uint8_t *encoded, size_t cap) {
    size_t n; int r; unsigned active;
    if(!valid(b) || b->reserved || target>=FM1_PATTERN_COUNT || !buffers(b,live,work,encoded,cap)) return BR_BANK_INVALID;
    active=b->active; if(target==active) return BR_BANK_OK;
    r=pack(live,encoded,cap,&n); if(r!=BR_BANK_OK) return r;
    if(!fits_switch(b,target,n)) return BR_BANK_FULL;
    if(b->used&(1u<<target)) { r=unpack(b,target,work); if(r!=BR_BANK_OK) return r; }
    else memcpy(work,live,sizeof *work);
    if(b->length[target]) replace(b,target,encoded,0u);
    replace(b,active,encoded,n);
    memcpy(live,work,sizeof *live); b->active=(uint8_t)target; b->used|=(uint16_t)(1u<<target);
    return BR_BANK_OK;
}
int br_pattern_bank_copy(const br_pattern_bank *b, unsigned source,
    const br_pattern_snapshot *live, br_pattern_snapshot *clipboard) {
    size_t n=sizeof *live;
    if(!valid(b) || source>=FM1_PATTERN_COUNT || !(b->used&(1u<<source)) || !live || !clipboard ||
       intersects(live,n,clipboard,n) || intersects(b->arena,b->capacity,clipboard,n) ||
       intersects(b,sizeof *b,clipboard,n)) return BR_BANK_INVALID;
    if(source==b->active) { memcpy(clipboard,live,n); return BR_BANK_OK; }
    return unpack(b,source,clipboard);
}
int br_pattern_bank_paste(br_pattern_bank *b, unsigned dest, br_pattern_snapshot *live,
    const br_pattern_snapshot *clipboard, uint8_t *encoded, size_t cap) {
    size_t n; int r;
    if(!valid(b) || b->reserved || dest>=FM1_PATTERN_COUNT || !buffers(b,live,clipboard,encoded,cap)) return BR_BANK_INVALID;
    if(dest==b->active) { memcpy(live,clipboard,sizeof *live); return BR_BANK_OK; }
    r=pack(clipboard,encoded,cap,&n); if(r!=BR_BANK_OK) return r;
    if(!fits(b,dest,n)) return BR_BANK_FULL;
    replace(b,dest,encoded,n); return BR_BANK_OK;
}

int br_pattern_bank_clip_copy(br_pattern_bank *b, unsigned source,
    const br_pattern_snapshot *live, br_pattern_snapshot *work,
    uint8_t *encoded, size_t cap)
{
    size_t n,off,old; unsigned i; int r;
    if(!valid(b) || b->reserved || source>=FM1_PATTERN_COUNT || !(b->used&(1u<<source)) ||
       !buffers(b,live,work,encoded,cap)) return BR_BANK_INVALID;
    if(source==b->active) memcpy(work,live,sizeof *work);
    else { r=unpack(b,source,work); if(r!=BR_BANK_OK) return r; }
    r=pack(work,encoded,cap,&n); if(r!=BR_BANK_OK) return r;
    if(n>b->capacity-(b->size-b->clip_length)) return BR_BANK_FULL;
    old=b->clip_length; off=b->clip_offset;
    if(old) {
        memmove(b->arena+off,b->arena+off+old,b->size-off-old); b->size-=old;
        for(i=0;i<FM1_PATTERN_COUNT;i++) if(b->length[i] && b->offset[i]>off) b->offset[i]-=old;
    }
    b->clip_offset=b->size; b->clip_length=n;
    memcpy(b->arena+b->size,encoded,n); b->size+=n; return BR_BANK_OK;
}
int br_pattern_bank_clip_paste(br_pattern_bank *b, unsigned target,
    br_pattern_snapshot *live, br_pattern_snapshot *work,
    uint8_t *encoded, size_t cap)
{
    size_t n; int r;
    if(!valid(b) || b->reserved || !b->clip_length || target>=FM1_PATTERN_COUNT ||
       !buffers(b,live,work,encoded,cap)) return BR_BANK_INVALID;
    if(fm1_pattern_codec_decode(b->arena+b->clip_offset,b->clip_length,
        (uint8_t *)work,sizeof *work)!=FM1_CODEC_OK) return BR_BANK_CORRUPT;
    if(target==b->active) { memcpy(live,work,sizeof *live); return BR_BANK_OK; }
    r=pack(work,encoded,cap,&n); if(r!=BR_BANK_OK) return r;
    if(!fits(b,target,n)) return BR_BANK_FULL;
    replace(b,target,encoded,n); return BR_BANK_OK;
}
