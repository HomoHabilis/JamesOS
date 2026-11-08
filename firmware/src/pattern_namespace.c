/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_namespace.h"
#include <string.h>
#define ROOT_MAGIC 0x32534e50u
#define ENTRY_MAGIC 0x32434e50u
#define HEADER 36u
#define FIRST 256u
#define ENTRY 48u
#define LIMIT (PFS_SECTOR-256u)
#define CLAIM 1u
#define REVOKE 2u
#define INTENT 3u
static uint32_t get(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);}
uint32_t pns_crc32(const void *v,size_t n){const uint8_t*p=v;uint32_t c=~0u;unsigned i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static int blank(const uint8_t*p,size_t n){while(n--)if(*p++!=255u)return 0;return 1;}
static int config(const pns_namespace*n,void *scratch){unsigned i;int any=0;if(!n||!scratch||!n->store||!n->store->read||!n->store->program||!n->digest||!n->journal_program||!n->reserved||!n->recover_peer||!n->reclaimable||!n->erase||n->root>=PFS_COUNT||n->peer>=PFS_COUNT||n->root==n->peer||n->store->base%PFS_SECTOR||n->store->base>UINT32_MAX-PFS_COUNT*PFS_SECTOR)return 0;for(i=0;i<PNS_DOMAIN_BYTES;i++)any|=n->expected[i];return any!=0;}
int pns_namespace_unchanged(const pns_namespace *n,const pns_namespace *p,const pfs_store *q)
{
    const pfs_store *s;
    if(!n || !p || !q || !(s=n->store))return 0;
    return s==p->store && n->ctx==p->ctx && n->digest==p->digest && n->journal_program==p->journal_program &&
        n->reserved==p->reserved && n->recover_peer==p->recover_peer && n->reclaimable==p->reclaimable && n->erase==p->erase &&
        n->root==p->root && n->peer==p->peer && !memcmp(n->expected,p->expected,PNS_DOMAIN_BYTES) &&
        s->ctx==q->ctx && s->read==q->read && s->program==q->program && s->base==q->base;
}
static uint32_t addr(const pns_namespace*n,uint32_t sector){return n->store->base+sector*PFS_SECTOR;}
static int readsector(const pns_namespace*n,uint32_t sector,void *out){return n->store->read(n->store->ctx,addr(n,sector),out,PFS_SECTOR)?PNS_IO:PNS_OK;}
static int root_check(const pns_namespace*n,const uint8_t*b){unsigned i;if(get(b)!=ROOT_MAGIC||get(b+4)!=3u||memcmp(b+8,n->expected,PNS_DOMAIN_BYTES)||!get(b+24)||get(b+28)!=pns_crc32(b,28)||get(b+32)!=0u)return 0;for(i=HEADER;i<FIRST;i++)if(b[i]!=255u)return 0;for(i=LIMIT;i<PFS_SECTOR;i++)if(b[i]!=255u)return 0;return 1;}
static int scan(const pns_namespace*n,uint8_t*b,uint32_t sector,uint8_t expected_hash[32],int *owned,uint32_t *free_at){uint32_t at;int seen_blank=0,seen_intent=0;*owned=0;*free_at=0;for(at=FIRST;at+ENTRY<=LIMIT;at+=ENTRY){uint8_t *e=b+at;uint32_t type,index;if(blank(e,ENTRY)){if(!seen_blank){*free_at=at;seen_blank=1;}continue;}/* A torn or malformed journal disables all reclamation; never resurrect an older claim. */if(seen_blank||seen_intent||get(e+44)!=ENTRY_MAGIC||get(e+40)!=pns_crc32(e,40))return PNS_CORRUPT;index=get(e);type=get(e+4);if(index>=PFS_COUNT||index==n->root||(type!=CLAIM&&type!=REVOKE&&type!=INTENT)||(type==INTENT ? index!=n->peer : index==n->peer))return PNS_CORRUPT;if(type==INTENT)seen_intent=1;if(index==sector){*owned=type==CLAIM;if(*owned)memcpy(expected_hash,e+8,32);}}if(!blank(b+at,LIMIT-at))return PNS_CORRUPT;return PNS_OK;}
int pns_provision(pns_namespace*n,void *scratch){uint8_t*b=scratch,h[HEADER];uint32_t i;if(!config(n,scratch)||n->reserved(n->ctx,addr(n,n->root))!=1||n->reserved(n->ctx,addr(n,n->peer))!=1)return PNS_INVALID;/* Refuse multiple valid root markers, including foreign domains. */for(i=0;i<PFS_COUNT;i++){if(readsector(n,i,b))return PNS_IO;if(i==n->root||i==n->peer){if(!blank(b,PFS_SECTOR))return PNS_INVALID;}else if(get(b)==ROOT_MAGIC)return PNS_INVALID;}memset(h,0,sizeof h);put(h,ROOT_MAGIC);put(h+4,3u);memcpy(h+8,n->expected,16);put(h+24,1u);put(h+28,pns_crc32(h,28));/* Entire header is the final root commit. */if(n->store->program(n->store->ctx,addr(n,n->root),h,sizeof h))return PNS_IO;return pns_open(n,scratch);}
int pns_open(const pns_namespace*n,void *scratch){uint8_t*b=scratch,hash[32];uint32_t free_at;int own,r;if(!config(n,scratch))return PNS_INVALID;if(readsector(n,n->root,b))return PNS_IO;if(!root_check(n,b))return PNS_CORRUPT;r=scan(n,b,PFS_COUNT,hash,&own,&free_at);return r;}
/* A committed intent is terminal in a root and blocks ordinary writes. */
static int last_intent(const uint8_t *b,uint8_t hash[32])
{
    uint32_t at; int found=0;
    for(at=FIRST;at+ENTRY<=LIMIT;at+=ENTRY) {
        const uint8_t *e=b+at; if(blank(e,ENTRY))break;
        if(get(e+4)==INTENT) { if(hash)memcpy(hash,e+8,32); found=1; }
    }
    return found;
}
static int current_root(const pns_namespace *n,void *scratch)
{
    pns_namespace selected=*n; int r=pns_open_pair(&selected,scratch);
    if(r!=PNS_OK)return r;
    if(selected.root!=n->root || selected.peer!=n->peer)return PNS_INVALID;
    if(readsector(n,n->root,scratch))return PNS_IO;
    return last_intent(scratch,0)?PNS_PENDING:PNS_OK;
}
static int append(const pns_namespace*n,uint32_t sector,uint32_t type,const uint8_t hash[32],void *scratch)
{
    uint8_t *b=scratch,e[ENTRY],unused[32],before[32],again[32],after[32];uint32_t at;int own,r;
    if(readsector(n,n->root,b))return PNS_IO;if(!root_check(n,b))return PNS_CORRUPT;
    r=scan(n,b,sector,unused,&own,&at);if(r)return r;
    if(!at || (type!=INTENT && at+2u*ENTRY>LIMIT))return PNS_FULL;
    memset(e,0,sizeof e);put(e,sector);put(e+4,type);if(hash)memcpy(e+8,hash,32);
    put(e+40,pns_crc32(e,40));put(e+44,ENTRY_MAGIC);
    /* Pin the entire root image, not just the next cursor: changes between
     * scan and write or outside this entry are never silently accepted. */
    if(n->digest(n->ctx,b,PFS_SECTOR,before))return PNS_IO;
    if(readsector(n,n->root,b))return PNS_IO;
    if(n->digest(n->ctx,b,PFS_SECTOR,again)||memcmp(before,again,32))return PNS_CORRUPT;
    memcpy(b+at,e,ENTRY);if(n->digest(n->ctx,b,PFS_SECTOR,after))return PNS_IO;
    if(n->journal_program(n->ctx,addr(n,n->root)+at,e,44u)||
       n->journal_program(n->ctx,addr(n,n->root)+at+44u,e+44,4u))return PNS_IO;
    if(readsector(n,n->root,b))return PNS_IO;
    if(n->digest(n->ctx,b,PFS_SECTOR,again)||memcmp(after,again,32))return PNS_CORRUPT;
    return pns_open(n,scratch);
}

int pns_claim(const pns_namespace*n,uint32_t sector,void *scratch){uint8_t*b=scratch,hash[32];if(!config(n,scratch)||sector>=PFS_COUNT||(sector==n->root||sector==n->peer)||n->reserved(n->ctx,addr(n,sector))!=1)return PNS_INVALID;if(current_root(n,scratch)!=PNS_OK)return PNS_CORRUPT;if(readsector(n,sector,b))return PNS_IO;if(blank(b,PFS_SECTOR)||n->digest(n->ctx,b,PFS_SECTOR,hash))return PNS_INVALID;return append(n,sector,CLAIM,hash,scratch);}
int pns_owned(const pns_namespace*n,uint32_t sector,void *scratch){uint8_t*b=scratch,expected[32],actual[32];uint32_t at;int own,r;if(!config(n,scratch)||sector>=PFS_COUNT||(sector==n->root||sector==n->peer))return PNS_INVALID;if(current_root(n,scratch)!=PNS_OK)return PNS_CORRUPT;if(readsector(n,n->root,b))return PNS_IO;if(!root_check(n,b))return PNS_CORRUPT;r=scan(n,b,sector,expected,&own,&at);if(r||!own)return r?r:0;if(readsector(n,sector,b))return PNS_IO;if(n->digest(n->ctx,b,PFS_SECTOR,actual))return PNS_IO;return memcmp(expected,actual,32)==0?1:0;}
int pns_revoke_erase(const pns_namespace*n,uint32_t sector,void *scratch){int r;if(!config(n,scratch)||sector>=PFS_COUNT||n->reclaimable(n->ctx,addr(n,sector))!=1)return PNS_INVALID;r=pns_owned(n,sector,scratch);if(r!=1)return r<0?r:PNS_INVALID;r=append(n,sector,REVOKE,0,scratch);if(r)return r;if(n->erase(n->ctx,addr(n,sector)))return PNS_IO;if(readsector(n,sector,scratch))return PNS_IO;return blank(scratch,PFS_SECTOR)?PNS_OK:PNS_IO;}

int pns_open_pair(pns_namespace *n,void *scratch)
{
    uint8_t *b=scratch; uint32_t roots[2],epochs[2]={0,0};
    int good[2]={0,0},bad[2]={0,0},intent[2]={0,0},i,r,selected;
    if(!config(n,scratch))return PNS_INVALID;
    roots[0]=n->root; roots[1]=n->peer;
    for(i=0;i<2;i++) {
        n->root=roots[i]; n->peer=roots[1-i];
        if(readsector(n,n->root,b)) { n->root=roots[0];n->peer=roots[1];return PNS_IO; }
        if(blank(b,PFS_SECTOR)) continue;
        if(!root_check(n,b)) { bad[i]=1; continue; }
        epochs[i]=get(b+24); r=pns_open(n,scratch);
        /* A committed but corrupt root may contain newer revocations; never
         * fall back to an older root in this case. */
        if(r!=PNS_OK) { n->root=roots[0];n->peer=roots[1];return r; }
        good[i]=1; intent[i]=last_intent(b,0);
    }
    if(!good[0]&&!good[1]) { n->root=roots[0];n->peer=roots[1];return PNS_CORRUPT; }
    if(good[0]&&good[1]&&epochs[0]==epochs[1]) { n->root=roots[0];n->peer=roots[1];return PNS_CORRUPT; }
    selected=good[1]&&(!good[0]||epochs[1]>epochs[0]);
    n->root=roots[selected];n->peer=roots[1-selected];
    if(bad[1-selected] && !intent[selected])return PNS_CORRUPT;
    return intent[selected]?PNS_PENDING:PNS_OK;
}

static int rotation_image(pns_namespace *n,uint8_t *b,uint8_t intent[32],int require_intent,
                          uint32_t *event_end)
{
    uint8_t hashes[PFS_COUNT][32], active[PFS_COUNT], actual[32];
    uint32_t at,index,type,epoch,i; int have_intent=0;
    if(pns_open(n,b)!=PNS_OK)return PNS_CORRUPT;
    if(readsector(n,n->root,b))return PNS_IO; epoch=get(b+24); if(epoch==UINT32_MAX)return PNS_FULL;
    memset(active,0,sizeof active);
    for(at=FIRST;at+ENTRY<=LIMIT;at+=ENTRY) {
        uint8_t *entry=b+at; if(blank(entry,ENTRY))break;
        index=get(entry);type=get(entry+4);
        if(type==INTENT) { memcpy(intent,entry+8,32); have_intent=1; break; }
        active[index]=(uint8_t)(type==CLAIM); if(active[index])memcpy(hashes[index],entry+8,32);
    }
    if(have_intent!=require_intent)return PNS_INVALID;
    for(i=0;i<PFS_COUNT;i++)if(active[i]) {
        if(readsector(n,i,b))return PNS_IO;
        if(n->digest(n->ctx,b,PFS_SECTOR,actual)||memcmp(actual,hashes[i],32))return PNS_CORRUPT;
    }
    memset(b,255,PFS_SECTOR);at=FIRST;
    for(i=0;i<PFS_COUNT;i++)if(active[i]) {
        uint8_t *e=b+at; if(at+2u*ENTRY>LIMIT)return PNS_FULL;
        memset(e,0,ENTRY);put(e,i);put(e+4,CLAIM);memcpy(e+8,hashes[i],32);
        put(e+40,pns_crc32(e,40));put(e+44,ENTRY_MAGIC);at+=ENTRY;
    }
    memset(b,0,HEADER);put(b,ROOT_MAGIC);put(b+4,3u);memcpy(b+8,n->expected,16);
    put(b+24,epoch+1u);put(b+28,pns_crc32(b,28));*event_end=at;return PNS_OK;
}
int pns_rotation_expected(pns_namespace *n,void *scratch,uint8_t out[32])
{
    uint8_t intent[32]; uint32_t end; int r;
    if(!config(n,scratch)||!out||current_root(n,scratch)!=PNS_OK)return PNS_INVALID;
    r=rotation_image(n,scratch,intent,0,&end); if(r)return r;
    return n->digest(n->ctx,scratch,PFS_SECTOR,out)?PNS_IO:PNS_OK;
}
static int prefix_region(pns_namespace *n,const uint8_t *image,uint32_t begin,uint32_t end,int *missing)
{
    uint8_t chunk[64];uint32_t at,j,k;
    for(at=begin;at<end;at+=k) {
        k=end-at;if(k>sizeof chunk)k=sizeof chunk;
        if(n->store->read(n->store->ctx,addr(n,n->peer)+at,chunk,k))return PNS_IO;
        for(j=0;j<k;j++) {
            uint8_t actual=chunk[j],expected=image[at+j];
            if(*missing) { if(actual!=255u)return PNS_CORRUPT; }
            else if(actual!=expected) { if(actual!=255u)return PNS_CORRUPT; *missing=1; }
        }
    }
    return PNS_OK;
}
static int peer_prefix(pns_namespace *n,const uint8_t *image,uint32_t end)
{
    uint8_t chunk[64];uint32_t at,j,k;int missing=0,r;
    /* Every byte outside the two programmed regions must remain erased. */
    for(at=HEADER;at<FIRST;at+=k) {
        k=FIRST-at;if(k>sizeof chunk)k=sizeof chunk;
        if(n->store->read(n->store->ctx,addr(n,n->peer)+at,chunk,k))return PNS_IO;
        for(j=0;j<k;j++)if(chunk[j]!=255u)return PNS_CORRUPT;
    }
    for(at=end;at<PFS_SECTOR;at+=k) {
        k=PFS_SECTOR-at;if(k>sizeof chunk)k=sizeof chunk;
        if(n->store->read(n->store->ctx,addr(n,n->peer)+at,chunk,k))return PNS_IO;
        for(j=0;j<k;j++)if(chunk[j]!=255u)return PNS_CORRUPT;
    }
    r=prefix_region(n,image,FIRST,end,&missing);if(r)return r;
    return prefix_region(n,image,0,HEADER,&missing);
}
static int write_rotation(pns_namespace *n,uint8_t *image,uint32_t end,void *scratch)
{
    uint32_t at;int r;pns_namespace next;
    for(at=FIRST;at<end;at+=ENTRY) {
        if(n->store->program(n->store->ctx,addr(n,n->peer)+at,image+at,44u)||
           n->store->program(n->store->ctx,addr(n,n->peer)+at+44u,image+at+44u,4u))return PNS_IO;
    }
    if(n->store->program(n->store->ctx,addr(n,n->peer),image,HEADER))return PNS_IO;
    next=*n;next.root=n->peer;next.peer=n->root;r=pns_open(&next,scratch);
    if(r)return r;*n=next;return PNS_OK;
}
int pns_rotate(pns_namespace *n,void *scratch)
{
    uint8_t *b=scratch, intent[32], image_hash[32];uint32_t end;int r;
    if(!config(n,scratch)||current_root(n,scratch)!=PNS_OK)return PNS_INVALID;
    if(n->reserved(n->ctx,addr(n,n->peer))!=1)return PNS_INVALID;
    if(readsector(n,n->peer,b))return PNS_IO;if(!blank(b,PFS_SECTOR))return PNS_FULL;
    r=rotation_image(n,b,intent,0,&end);if(r)return r;
    if(n->digest(n->ctx,b,PFS_SECTOR,image_hash))return PNS_IO;
    /* Reserve one journal entry for a committed intent before peer writes. */
    r=append(n,n->peer,INTENT,image_hash,scratch);if(r)return r;
    r=rotation_image(n,b,intent,1,&end);if(r)return r;
    if(n->digest(n->ctx,b,PFS_SECTOR,intent)||memcmp(intent,image_hash,32))return PNS_CORRUPT;
    return write_rotation(n,b,end,scratch);
}
int pns_recover_rotation(pns_namespace *n,void *scratch)
{
    uint8_t *b=scratch,intent[32],actual[32],original[32];uint32_t end;int r;
    if(!config(n,scratch))return PNS_INVALID;
    r=rotation_image(n,b,intent,1,&end);if(r)return r;
    if(n->digest(n->ctx,b,PFS_SECTOR,actual)||memcmp(actual,intent,32))return PNS_CORRUPT;
    r=peer_prefix(n,b,end);if(r)return r;
    memcpy(original,intent,sizeof original);
    if(n->recover_peer(n->ctx,addr(n,n->root),addr(n,n->peer),intent)!=1)return PNS_INVALID;
    /* Rebuild from the still-valid root after granting the lease; neither the
     * intent nor any source claim may have changed during the callback. */
    r=rotation_image(n,b,intent,1,&end);if(r)return r;
    if(memcmp(original,intent,32)||n->digest(n->ctx,b,PFS_SECTOR,actual)||memcmp(actual,original,32))return PNS_CORRUPT;
    r=peer_prefix(n,b,end);if(r)return r;
    return write_rotation(n,b,end,scratch);
}

int pns_retire_peer(pns_namespace *n,void *scratch)
{
    uint8_t *b=scratch;uint32_t epoch; pns_namespace old;
    if(!config(n,scratch)||current_root(n,scratch)!=PNS_OK)return PNS_INVALID;
    if(readsector(n,n->root,b))return PNS_IO;epoch=get(b+24);
    old=*n;old.root=n->peer;old.peer=n->root;
    if(pns_open(&old,scratch)!=PNS_OK||get(b+24)>=epoch)return PNS_INVALID;
    if(n->reclaimable(n->ctx,addr(n,n->peer))!=1)return PNS_INVALID;
    if(n->erase(n->ctx,addr(n,n->peer))||readsector(n,n->peer,b))return PNS_IO;
    return blank(b,PFS_SECTOR)?PNS_OK:PNS_IO;
}
