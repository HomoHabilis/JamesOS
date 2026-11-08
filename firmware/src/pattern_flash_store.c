/* Experimental append-only proof of concept. No hardware integration. */
#include "pattern_flash_store.h"
#include <string.h>
#define MAGIC 0x31534650u
#define CHUNK 1u
#define MANIFEST 2u
#define HEADER 32u
#define MAX_ID 13u
static void put(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t get(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t crc(const void *v, uint32_t n) { const uint8_t *p=v; uint32_t c=~0u,i; while(n--) { c^=*p++; for(i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1u))); } return ~c; }
static int config(const pfs_store *s) { return s && s->read && s->program && !(s->base%PFS_SECTOR) && s->base<=UINT32_MAX-PFS_COUNT*PFS_SECTOR; }
static int readsec(const pfs_store *s, uint32_t i, uint8_t *b) { return s->read(s->ctx,s->base+i*PFS_SECTOR,b,PFS_SECTOR) ? PFS_IO : PFS_OK; }
static int blank(const uint8_t *b) { uint32_t i; for(i=0;i<PFS_SECTOR;i++) if(b[i]!=255u) return 0; return 1; }
static int pfs_overlap(const void *a,uint32_t an,const void *b,uint32_t bn) {
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    return an && bn && (aa<=bb ? bb-aa<an : aa-bb<bn);
}
/* Each header covers its own metadata. Data and erased tail are separately checked. */
static int valid(const uint8_t *b, uint32_t kind) {
    uint32_t n=get(b+20), i;
    if(get(b)!=MAGIC || get(b+4)!=kind || get(b+8)>=MAX_ID || n>PFS_CHUNK || get(b+28)!=crc(b,28) || get(b+24)!=crc(b+HEADER,n)) return 0;
    /* The writer leaves all slack and the updater-reserved tail erased. */
    for(i=HEADER+n;i<PFS_SECTOR;i++) if(b[i]!=255u) return 0;
    return 1;
}
static void header(uint8_t *h, uint32_t kind, uint32_t id, uint32_t gen, uint32_t index, uint32_t n, const void *data) {
    put(h,MAGIC); put(h+4,kind); put(h+8,id); put(h+12,gen); put(h+16,index); put(h+20,n); put(h+24,crc(data,n)); put(h+28,crc(h,28));
}
/* Manifest body: total length, count, then sector indices. Fully validate all
 * chunks on scan: an incomplete or corrupted generation is never selected. */
static int find(const pfs_store *s, uint32_t id, uint8_t *b, uint32_t *best, uint32_t *generation, uint8_t *selected, uint32_t *selected_crc) {
    uint32_t i,j,k,n,total,sz,g,refs[PFS_MAX_CHUNKS], candidate_gen, expected, candidate_crc[PFS_MAX_CHUNKS]; uint8_t candidate[HEADER+8+4*PFS_MAX_CHUNKS]; int found=0;
    for(i=0;i<PFS_COUNT;i++) {
        if(readsec(s,i,b)) return PFS_IO;
        if(!valid(b,MANIFEST) || get(b+8)!=id) continue;
        candidate_gen=get(b+12); sz=get(b+20); if(sz<8) continue;
        total=get(b+HEADER); n=get(b+HEADER+4);
        if(!n || n>PFS_MAX_CHUNKS || sz!=8+4*n || !total || total>PFS_MAX_DATA || n!=(total+PFS_CHUNK-1)/PFS_CHUNK) continue;
        memcpy(candidate,b,HEADER+sz);
        for(j=0;j<n;j++) refs[j]=get(b+HEADER+8+4*j);
        for(j=0;j<n;j++) {
            if(refs[j]>=PFS_COUNT || refs[j]==i) break;
            for(k=0;k<j;k++) if(refs[j]==refs[k]) break;
            if(k<j) break;
            if(readsec(s,refs[j],b)) return PFS_IO;
            expected=total-j*PFS_CHUNK; if(expected>PFS_CHUNK) expected=PFS_CHUNK;
            if(!valid(b,CHUNK) || get(b+8)!=id || get(b+12)!=candidate_gen || get(b+16)!=j || get(b+20)!=expected) break;
            candidate_crc[j]=get(b+24);
        }
        if(j!=n) continue;
        g=candidate_gen;
        if(!found || (int32_t)(g-*generation)>0) { *best=i; *generation=g; if(selected) memcpy(selected,candidate,HEADER+sz); if(selected_crc) memcpy(selected_crc,candidate_crc,n*sizeof(uint32_t)); found=1; }
    }
    return found?PFS_OK:PFS_EMPTY;
}
int pfs_load(const pfs_store *s,uint32_t id,void *out,uint32_t cap,uint32_t *len,void *scratch) {
    uint8_t *b=scratch, *dst=out, selected[HEADER+8+4*PFS_MAX_CHUNKS]; uint32_t best=0,g=0,total,n,i,j,refs[PFS_MAX_CHUNKS],selected_crc[PFS_MAX_CHUNKS],off=0,part; int r;
    if(!config(s)||id>=MAX_ID||!out||!len||!b ||
       pfs_overlap(out,cap,b,PFS_SECTOR) || pfs_overlap(len,sizeof *len,b,PFS_SECTOR) ||
       pfs_overlap(len,sizeof *len,out,cap)) return PFS_INVALID;
    r=find(s,id,b,&best,&g,selected,selected_crc); if(r) return r;
    if(readsec(s,best,b)) return PFS_IO;
    /* Flash reads can change or fail independently. Never trust values merely
     * because the earlier scan validated a previous read of this sector. */
    if(!valid(b,MANIFEST) || get(b+8)!=id || get(b+12)!=g || get(b+20)<8 || get(b+20)!=get(selected+20) || memcmp(b,selected,HEADER+get(b+20))) return PFS_IO;
    total=get(b+HEADER); n=get(b+HEADER+4);
    if(!n || n>PFS_MAX_CHUNKS || get(b+20)!=8+4*n || !total || total>PFS_MAX_DATA || n!=(total+PFS_CHUNK-1)/PFS_CHUNK) return PFS_IO;
    for(i=0;i<n;i++) {
        refs[i]=get(b+HEADER+8+4*i);
        if(refs[i]>=PFS_COUNT || refs[i]==best) return PFS_IO;
        for(j=0;j<i;j++) if(refs[i]==refs[j]) return PFS_IO;
    }
    *len=total; if(cap<total) return PFS_SMALL;
    for(i=0;i<n;i++) {
        if(readsec(s,refs[i],b)) return PFS_IO;
        part=total-off; if(part>PFS_CHUNK) part=PFS_CHUNK;
        if(!valid(b,CHUNK) || get(b+8)!=id || get(b+12)!=g || get(b+16)!=i || get(b+20)!=part || get(b+24)!=selected_crc[i]) return PFS_IO;
        memcpy(dst+off,b+HEADER,part); off+=part;
    }
    return PFS_OK;
}
int pfs_save(const pfs_store *s,uint32_t id,const void *data,uint32_t len,void *scratch) {
    const uint8_t *src=data; uint8_t *b=scratch, h[HEADER], body[8+4*PFS_MAX_CHUNKS]; uint32_t refs[PFS_MAX_CHUNKS+1], best=0,g=0,i,n,free_count=0,off=0,part; int r;
    if(!config(s)||id>=MAX_ID||!src||!b||!len||len>PFS_MAX_DATA) return PFS_INVALID;
    /* Scratch must not overlap source. */
    if((uintptr_t)src<=(uintptr_t)b ? (uintptr_t)b-(uintptr_t)src<len : (uintptr_t)src-(uintptr_t)b<PFS_SECTOR) return PFS_INVALID;
    r=find(s,id,b,&best,&g,0,0); if(r!=PFS_OK && r!=PFS_EMPTY) return r;
    if(g==UINT32_MAX) return PFS_INVALID;
    n=(len+PFS_CHUNK-1)/PFS_CHUNK;
    for(i=0;i<PFS_COUNT;i++) { if(readsec(s,i,b)) return PFS_IO; if(blank(b) && free_count<n+1) refs[free_count++]=i; }
    if(free_count<n+1) return PFS_FULL;
    g++;
    for(i=0;i<n;i++) {
        part=len-off; if(part>PFS_CHUNK) part=PFS_CHUNK;
        header(h,CHUNK,id,g,i,part,src+off);
        if(s->program(s->ctx,s->base+refs[i]*PFS_SECTOR+HEADER,src+off,part) || s->program(s->ctx,s->base+refs[i]*PFS_SECTOR,h,HEADER)) return PFS_IO;
        if(readsec(s,refs[i],b) || !valid(b,CHUNK) || memcmp(b,h,HEADER) || memcmp(b+HEADER,src+off,part)) return PFS_IO;
        off+=part;
    }
    put(body,len); put(body+4,n); for(i=0;i<n;i++) put(body+8+4*i,refs[i]);
    part=8+4*n; header(h,MANIFEST,id,g,0,part,body);
    /* Header is the commit marker; an incomplete manifest cannot validate. */
    if(s->program(s->ctx,s->base+refs[n]*PFS_SECTOR+HEADER,body,part) || s->program(s->ctx,s->base+refs[n]*PFS_SECTOR,h,HEADER)) return PFS_IO;
    if(readsec(s,refs[n],b) || !valid(b,MANIFEST) || memcmp(b,h,HEADER) || memcmp(b+HEADER,body,part)) return PFS_IO;
    return PFS_OK;
}

/* Conservative collection: validate every same-ID manifest, reject duplicate
 * generations, retain the two newest complete versions, and reclaim only one
 * older complete version. The caller must establish exclusive provenance; a
 * plausible CRC or magic alone never authorizes erasure. */
typedef struct {
    uint32_t sector, gen, n, total, refs[PFS_MAX_CHUNKS], checks[PFS_MAX_CHUNKS];
    uint8_t manifest[HEADER+8+4*PFS_MAX_CHUNKS];
} gc_record;
int pfs_gc_one(const pfs_store *s, uint32_t id, pfs_owned_fn owned,
               pfs_erase_fn erase, void *scratch) {
    uint8_t *b=scratch, candidate[HEADER+8+4*PFS_MAX_CHUNKS];
    gc_record victim; uint32_t generations[PFS_COUNT], checks[PFS_MAX_CHUNKS];
    uint32_t count=0,i,j,k,n,total,sz,part,refs[PFS_MAX_CHUNKS],gen;
    if(!config(s)||id>=MAX_ID||!owned||!erase||!b) return PFS_INVALID;
    memset(&victim,0,sizeof victim);
    for(i=0;i<PFS_COUNT;i++) {
        if(readsec(s,i,b)) return PFS_IO;
        if(!valid(b,MANIFEST)||get(b+8)!=id) continue;
        gen=get(b+12); sz=get(b+20); if(!gen || sz<8) return PFS_INVALID;
        total=get(b+HEADER); n=get(b+HEADER+4);
        if(!n||n>PFS_MAX_CHUNKS||sz!=8+4*n||!total||total>PFS_MAX_DATA||n!=(total+PFS_CHUNK-1)/PFS_CHUNK) return PFS_INVALID;
        memcpy(candidate,b,HEADER+sz);
        for(j=0;j<n;j++) {
            refs[j]=get(b+HEADER+8+4*j);
            if(refs[j]>=PFS_COUNT||refs[j]==i) return PFS_INVALID;
            for(k=0;k<j;k++) if(refs[j]==refs[k]) return PFS_INVALID;
        }
        for(j=0;j<n;j++) {
            if(readsec(s,refs[j],b)) return PFS_IO;
            part=total-j*PFS_CHUNK; if(part>PFS_CHUNK) part=PFS_CHUNK;
            if(!valid(b,CHUNK)||get(b+8)!=id||get(b+12)!=gen||get(b+16)!=j||get(b+20)!=part) return PFS_INVALID;
            checks[j]=get(b+24);
        }
        for(j=0;j<count;j++) if(generations[j]==gen) return PFS_INVALID;
        if(!count || gen<victim.gen) {
            victim.sector=i; victim.gen=gen; victim.n=n; victim.total=total;
            memcpy(victim.refs,refs,n*sizeof(uint32_t)); memcpy(victim.checks,checks,n*sizeof(uint32_t));
            memcpy(victim.manifest,candidate,HEADER+sz);
        }
        generations[count++]=gen;
    }
    if(count<3) return PFS_EMPTY;
    for(i=0;i<count;i++) for(j=0;j<i;j++) {
        uint32_t a=generations[i],bg=generations[j];
        if((a>bg?a-bg:bg-a)>=0x80000000u) return PFS_INVALID;
    }
    /* Exact 1 is an external exclusive-provenance certificate, never a
     * magic/CRC guess. All candidate sectors are certified before erasure. */
    if(owned(s->ctx,s->base+victim.sector*PFS_SECTOR)!=1) return PFS_INVALID;
    for(i=0;i<victim.n;i++) if(owned(s->ctx,s->base+victim.refs[i]*PFS_SECTOR)!=1) return PFS_INVALID;
    /* Re-read the complete victim and every referenced chunk after scanning,
     * and refuse any second-read change before erasing its manifest. */
    if(readsec(s,victim.sector,b)) return PFS_IO;
    if(!valid(b,MANIFEST) || memcmp(b,victim.manifest,HEADER+8+4*victim.n)) return PFS_INVALID;
    for(i=0;i<victim.n;i++) {
        if(readsec(s,victim.refs[i],b)) return PFS_IO;
        part=victim.total-i*PFS_CHUNK; if(part>PFS_CHUNK) part=PFS_CHUNK;
        if(!valid(b,CHUNK)||get(b+8)!=id||get(b+12)!=victim.gen||get(b+16)!=i||
           get(b+20)!=part||get(b+24)!=victim.checks[i]) return PFS_INVALID;
    }
    /* Manifest first: cuts may strand chunks but cannot destroy newer data. */
    if(erase(s->ctx,s->base+victim.sector*PFS_SECTOR)||readsec(s,victim.sector,b)||!blank(b)) return PFS_IO;
    for(i=0;i<victim.n;i++) {
        uint32_t sector=victim.refs[i];
        if(erase(s->ctx,s->base+sector*PFS_SECTOR)||readsec(s,sector,b)||!blank(b)) return PFS_IO;
    }
    return PFS_OK;
}

int pfs_reader_open(const pfs_store *s,uint32_t id,pfs_reader *out,void *scratch) {
    uint8_t *b=scratch, selected[HEADER+8+4*PFS_MAX_CHUNKS]; uint32_t best=0,g=0,checks[PFS_MAX_CHUNKS],total,n,i; int r;
    if(!config(s)||id>=MAX_ID||!out||!b||pfs_overlap(out,sizeof *out,b,PFS_SECTOR)) return PFS_INVALID;
    r=find(s,id,b,&best,&g,selected,checks); if(r) return r;
    /* find has already validated this exact selected manifest and all chunks. */
    total=get(selected+HEADER); n=get(selected+HEADER+4);
    memset(out,0,sizeof *out); out->store=s; out->id=id; out->generation=g; out->manifest_sector=best; out->length=total; out->count=n;
    memcpy(out->manifest,selected,HEADER+8+4*n);
    for(i=0;i<n;i++) { out->sectors[i]=get(selected+HEADER+8+4*i); out->chunk_crc[i]=checks[i]; }
    return PFS_OK;
}
int pfs_reader_open_owned(const pfs_store *s,uint32_t id,pfs_reader *out,void *scratch,pfs_eligible_fn owned,void *owner_ctx) {
    uint32_t i; int r;
    if(!owned) return PFS_INVALID;
    r=pfs_reader_open(s,id,out,scratch); if(r) return r;
    if(owned(owner_ctx,s->base+out->manifest_sector*PFS_SECTOR)!=1) goto denied;
    for(i=0;i<out->count;i++) if(owned(owner_ctx,s->base+out->sectors[i]*PFS_SECTOR)!=1) goto denied;
    return PFS_OK;
denied:
    memset(out,0,sizeof *out);
    return PFS_INVALID;
}
int pfs_reader_read_at(const pfs_reader *r,uint32_t off,void *dst,uint32_t n,void *scratch) {
    uint8_t *b=scratch,*out=dst; uint32_t at,part,chunklen,take,within;
    if(!r||!config(r->store)||r->id>=MAX_ID||!r->count||r->count>PFS_MAX_CHUNKS||r->manifest_sector>=PFS_COUNT||!b||(!out&&n)||off>r->length||n>r->length-off) return PFS_INVALID;
    if(pfs_overlap(r,sizeof *r,b,PFS_SECTOR) || (n && (pfs_overlap(out,n,b,PFS_SECTOR)||pfs_overlap(out,n,r,sizeof *r)))) return PFS_INVALID;
    if(readsec(r->store,r->manifest_sector,b)) return PFS_IO;
    if(!valid(b,MANIFEST)||get(b+20)!=8+4*r->count||memcmp(b,r->manifest,HEADER+8+4*r->count)) return PFS_IO;
    while(n) {
        at=off/PFS_CHUNK; within=off%PFS_CHUNK;
        if(at>=r->count||r->sectors[at]>=PFS_COUNT) return PFS_INVALID;
        if(readsec(r->store,r->sectors[at],b)) return PFS_IO;
        chunklen=r->length-at*PFS_CHUNK; if(chunklen>PFS_CHUNK) chunklen=PFS_CHUNK;
        if(!valid(b,CHUNK)||get(b+8)!=r->id||get(b+12)!=r->generation||get(b+16)!=at||get(b+20)!=chunklen||get(b+24)!=r->chunk_crc[at]) return PFS_IO;
        part=chunklen-within; take=n<part?n:part; memcpy(out,b+HEADER+within,take);
        out+=take; off+=take; n-=take;
    }
    return PFS_OK;
}
static int save_stream(const pfs_store *s,uint32_t id,uint32_t len,pfs_read_at_fn source,void *ctx,void *scratch,pfs_eligible_fn eligible,void *eligible_ctx) {
    uint8_t *b=scratch,h[HEADER],body[8+4*PFS_MAX_CHUNKS]; uint32_t refs[PFS_MAX_CHUNKS+1],best=0,g=0,i,n,free_count=0,off=0,part,expected_crc; int r;
    if(!config(s)||id>=MAX_ID||!source||!b||!len||len>PFS_MAX_DATA) return PFS_INVALID;
    r=find(s,id,b,&best,&g,0,0); if(r!=PFS_OK && r!=PFS_EMPTY) return r;
    if(g==UINT32_MAX) return PFS_INVALID;
    n=(len+PFS_CHUNK-1)/PFS_CHUNK;
    for(i=0;i<PFS_COUNT;i++) { if(readsec(s,i,b)) return PFS_IO; if(blank(b)&&free_count<n+1&&(!eligible||eligible(eligible_ctx,s->base+i*PFS_SECTOR)==1)) refs[free_count++]=i; }
    if(free_count<n+1) return PFS_FULL;
    g++;
    for(i=0;i<n;i++) {
        part=len-off; if(part>PFS_CHUNK) part=PFS_CHUNK;
        if(source(ctx,off,b+HEADER,part)) return PFS_IO;
        header(h,CHUNK,id,g,i,part,b+HEADER); expected_crc=get(h+24);
        if(s->program(s->ctx,s->base+refs[i]*PFS_SECTOR+HEADER,b+HEADER,part)||s->program(s->ctx,s->base+refs[i]*PFS_SECTOR,h,HEADER)) return PFS_IO;
        if(readsec(s,refs[i],b)||!valid(b,CHUNK)||get(b+8)!=id||get(b+12)!=g||get(b+16)!=i||get(b+20)!=part||get(b+24)!=expected_crc) return PFS_IO;
        off+=part;
    }
    put(body,len); put(body+4,n); for(i=0;i<n;i++) put(body+8+4*i,refs[i]); part=8+4*n;
    header(h,MANIFEST,id,g,0,part,body);
    if(s->program(s->ctx,s->base+refs[n]*PFS_SECTOR+HEADER,body,part)||s->program(s->ctx,s->base+refs[n]*PFS_SECTOR,h,HEADER)) return PFS_IO;
    if(readsec(s,refs[n],b)||!valid(b,MANIFEST)||memcmp(b+HEADER,body,part)||memcmp(b,h,HEADER)) return PFS_IO;
    return PFS_OK;
}
int pfs_save_stream(const pfs_store *s,uint32_t id,uint32_t len,pfs_read_at_fn source,void *ctx,void *scratch) {
    return save_stream(s,id,len,source,ctx,scratch,0,0);
}
int pfs_save_stream_eligible(const pfs_store *s,uint32_t id,uint32_t len,pfs_read_at_fn source,void *ctx,void *scratch,pfs_eligible_fn eligible,void *eligible_ctx) {
    if(!eligible) return PFS_INVALID;
    return save_stream(s,id,len,source,ctx,scratch,eligible,eligible_ctx);
}
