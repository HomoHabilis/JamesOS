/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_fixed_authority.h"
#include <string.h>
static const uint8_t identity[16]={0x92,0x71,0x5d,0xb3,0x0f,0xc8,0x48,0x91,0xa2,0x77,0xe4,0x16,0x6b,0x90,0x32,0xd5};
int ppa_fixed_configure(ppa_store *s){if(!s||s->pairs_base%PS_SECTOR||s->pairs_base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR)return PPA_INVALID;s->authority=s->pairs_base+PPA_FIXED_SLOT*2u*PS_SECTOR;memcpy(s->identity,identity,sizeof identity);return PPA_OK;}
int ppa_fixed_certificate(void *v,uint32_t address){ppa_fixed_certificate_context *c=v;uint8_t states[PS_TOTAL_SLOTS];uint32_t first;
 if(!c||!c->authority||!c->scratch||c->slot>=PS_TOTAL_SLOTS||c->slot==PPA_FIXED_SLOT||(c->state!=PPA_INITIALIZING&&c->state!=PPA_ACTIVE)||
    c->authority->authority!=c->authority->pairs_base+PPA_FIXED_SLOT*2u*PS_SECTOR||memcmp(c->authority->identity,identity,sizeof identity)||
    c->authority->pairs_base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR)return 0;
 first=c->authority->pairs_base+c->slot*2u*PS_SECTOR;
 if((address!=first&&address!=first+PS_SECTOR)||ppa_open(c->authority,c->scratch,states)!=PPA_OK||states[c->slot]!=c->state)return 0;
 return c->authority->reserved(c->authority->ctx,first)==1&&c->authority->reserved(c->authority->ctx,first+PS_SECTOR)==1;
}
int ppa_fixed_legacy_write_allowed(void){return 0;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static int unchanged(const ppa_store *a,const ppa_store *b){return a&&b&&a->ctx==b->ctx&&a->read==b->read&&a->program==b->program&&a->reserved==b->reserved&&a->project_ready==b->project_ready&&a->authority==b->authority&&a->pairs_base==b->pairs_base&&!memcmp(a->identity,b->identity,sizeof a->identity);}
int ppa_fixed_snapshot_begin(ppa_fixed_snapshot *snap,const ppa_store *s,void *scratch,uint32_t slot,unsigned state,uint32_t token,void *ctx,int (*fence)(void *,uint32_t)){
 ppa_store pinned;uint8_t states[PS_TOTAL_SLOTS];int r;
 if(!snap||!s||!scratch||overlap(snap,sizeof *snap,s,sizeof *s)||overlap(snap,sizeof *snap,scratch,PS_SECTOR)||overlap(s,sizeof *s,scratch,PS_SECTOR))return PPA_INVALID;
 memset(snap,0,sizeof *snap);
 if(!fence||!token||slot>=PS_TOTAL_SLOTS||slot==PPA_FIXED_SLOT||(state!=PPA_INITIALIZING&&state!=PPA_ACTIVE)||
    s->pairs_base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR||s->authority!=s->pairs_base+PPA_FIXED_SLOT*2u*PS_SECTOR||memcmp(s->identity,identity,sizeof identity)||fence(ctx,token)!=1)return PPA_INVALID;
 pinned=*s;r=ppa_open(s,scratch,states);if(r)return r;
 if(!unchanged(s,&pinned)||fence(ctx,token)!=1||states[slot]!=state)return PPA_INVALID;
 memset(snap,0,sizeof *snap);snap->live=s;snap->pinned=pinned;snap->token=token;snap->slot=slot;snap->state=state;snap->fence_ctx=ctx;snap->fence=fence;memcpy(snap->states,states,sizeof states);snap->active=1;
 return PPA_OK;
}
int ppa_fixed_snapshot_certificate(void *v,uint32_t address){ppa_fixed_snapshot_handle *handle=v;ppa_fixed_snapshot *snap;uint32_t first;
 if(!handle||!(snap=handle->snapshot)||!snap->active||!handle->token||handle->token!=snap->token)return 0;
 if(!unchanged(snap->live,&snap->pinned)||!snap->fence||snap->fence(snap->fence_ctx,snap->token)!=1||snap->slot>=PS_TOTAL_SLOTS||snap->slot==PPA_FIXED_SLOT||snap->states[snap->slot]!=snap->state)goto denied;
 first=snap->pinned.pairs_base+snap->slot*2u*PS_SECTOR;
 if(address!=first&&address!=first+PS_SECTOR)return 0;
 if(snap->pinned.reserved(snap->pinned.ctx,snap->pinned.authority)!=1||snap->pinned.reserved(snap->pinned.ctx,snap->pinned.authority+PS_SECTOR)!=1||snap->pinned.reserved(snap->pinned.ctx,first)!=1||snap->pinned.reserved(snap->pinned.ctx,first+PS_SECTOR)!=1||snap->fence(snap->fence_ctx,snap->token)!=1)goto denied;
 return 1;
denied:
 snap->active=0;return 0;
}
void ppa_fixed_snapshot_end(ppa_fixed_snapshot *snap){if(snap)memset(snap,0,sizeof *snap);}
