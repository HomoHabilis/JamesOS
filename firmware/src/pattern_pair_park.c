#include "pattern_pair_park.h"
#include <string.h>
#define PAGE 256u
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b; return an && bn && (x<=y ? y-x<an : x-y<bn); }
static int admitted(pps_park_adapter *a,uint32_t off,uint32_t n)
{
    return a && a->park && a->store && a->token && n && off>=a->first && off-a->first<2u*PS_SECTOR &&
        n<=2u*PS_SECTOR-(off-a->first) && !memcmp(a->store->role,a->role,16) &&
        a->fenced(a->proof_ctx,a->token,a->slot)==1 &&
        a->reserved(a->proof_ctx,a->token,a->first,a->role)==1 &&
        a->reserved(a->proof_ctx,a->token,a->first+PS_SECTOR,a->role)==1 &&
        !memcmp(a->store->role,a->role,16);
}
static int read_cb(void *v,uint32_t off,void *dst,uint32_t n)
{ pps_park_adapter *a=v; return !dst || !admitted(a,off,n) ? -1 : br_pattern_park_read(a->park,off,dst,n); }
static int erase_cb(void *v,uint32_t off)
{
    pps_park_adapter *a=v;
    if(!admitted(a,off,PS_SECTOR) || (off%PS_SECTOR)) return -1;
    a->has_erased=0; /* failed erase never enables a later program */
    if(br_pattern_park_erase(a->park,off)) return -1;
    a->erased=off;a->programmed=0;a->has_erased=1;return 0;
}
static int program_cb(void *v,uint32_t off,const void *src,uint32_t n)
{
    pps_park_adapter *a=v; unsigned page; uint16_t bit;
    if(!src || n!=PAGE || (off%PAGE) || !admitted(a,off,n) || !a->has_erased ||
       off<a->erased || off-a->erased>=PS_SECTOR) return -1;
    page=(off-a->erased)/PAGE; if(page==15u)return -1; /* updater-owned tail */
    bit=(uint16_t)(1u<<page);
    if(a->programmed&bit)return -1;
    /* An interrupted program is not retryable without another proven erase. */
    a->programmed|=bit;
    return br_pattern_park_program(a->park,off,src,n);
}
int pps_park_bind(pps_park_adapter *a,pps_store *out,br_pattern_park_io *park,uint32_t token,uint32_t slot,
                  const uint8_t role[16],pps_validate validate,void *validate_ctx,
                  pps_park_proof fenced,pps_park_reserved_proof reserved,void *ctx)
{
    uint32_t first;
    if(!a||!out||!park||!token||slot>=PS_TOTAL_SLOTS||!role||!validate||!fenced||!reserved ||
       park->base%PS_SECTOR || park->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR ||
       park->end<=park->base || park->end-park->base<PS_TOTAL_SLOTS*2u*PS_SECTOR ||
       overlap(a,sizeof *a,out,sizeof *out) || overlap(a,sizeof *a,park,sizeof *park) ||
       overlap(out,sizeof *out,park,sizeof *park) || overlap(a,sizeof *a,role,16) ||
       overlap(out,sizeof *out,role,16)) return 0;
    first=park->base+slot*2u*PS_SECTOR;
    if(fenced(ctx,token,slot)!=1 || reserved(ctx,token,first,role)!=1 ||
       reserved(ctx,token,first+PS_SECTOR,role)!=1) return 0;
    memset(a,0,sizeof *a);a->park=park;a->store=out;a->token=token;a->slot=slot;a->first=first;
    a->fenced=fenced;a->reserved=reserved;a->proof_ctx=ctx;memcpy(a->role,role,16);
    memset(out,0,sizeof *out);out->ctx=a;out->read=read_cb;out->program=program_cb;out->erase=erase_cb;
    out->base=park->base;memcpy(out->role,role,16);out->validate=validate;out->validate_ctx=validate_ctx;return 1;
}
