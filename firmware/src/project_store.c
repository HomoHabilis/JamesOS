/* Portable, bounded two-copy NOR store. No heap and no large stack objects. */
#include "project_store.h"
#include <string.h>

#define PS_MAGIC 0x31504d46u
#define PS_VERSION 1u
#define PS_HEADER 44u

static void ps_put(uint8_t *p, uint32_t x) {
    p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); p[2]=(uint8_t)(x>>16); p[3]=(uint8_t)(x>>24);
}
static uint32_t ps_get(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t ps_crc(const void *data, uint32_t n) {
    const uint8_t *p=(const uint8_t *)data; uint32_t c=~0u, i;
    while(n--) { c^=*p++; for(i=0;i<8;i++) c=(c>>1) ^ (0xedb88320u & (0u-(c&1u))); }
    return ~c;
}
static int ps_config(const ps_store *s, uint32_t slot) {
    return s && s->read && s->erase && s->program && s->valid && slot<PS_TOTAL_SLOTS &&
        s->payload_size && s->payload_size<=PS_MAX_PAYLOAD && !(s->base % PS_SECTOR) &&
        s->base <= UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR;
}
static uint32_t ps_addr(const ps_store *s, uint32_t slot, int copy) {
    return s->base+(slot*2u+(uint32_t)copy)*PS_SECTOR;
}
static int ps_copy(const ps_store *s, uint32_t slot, int copy, void *scratch, ps_info *info) {
    uint8_t h[PS_HEADER]; uint32_t i, off=ps_addr(s,slot,copy); int empty=1;
    if(s->read(s->context,off,h,sizeof h)) return PS_IO;
    for(i=0;i<sizeof h;i++) if(h[i]!=0xffu) { empty=0; break; }
    if(empty) {
        /* A blank header with programmed payload may be an interrupted first
         * write or foreign data. Do not silently erase it as an empty slot. */
        uint8_t *chunk=(uint8_t *)scratch; uint32_t pos,n;
        for(pos=PS_HEADER;pos<PS_SECTOR;pos+=n) {
            n=PS_SECTOR-pos; if(n>s->payload_size) n=s->payload_size;
            if(s->read(s->context,off+pos,chunk,n)) return PS_IO;
            for(i=0;i<n;i++) if(chunk[i]!=0xffu) return PS_CORRUPT;
        }
        return PS_EMPTY;
    }
    if(ps_get(h)!=PS_MAGIC || ps_get(h+4)!=PS_VERSION || ps_get(h+8)!=slot ||
       ps_get(h+16)!=s->payload_size || h[36]!=0 || ps_get(h+40)!=ps_crc(h,40)) return PS_CORRUPT;
    if(s->read(s->context,off+PS_PAYLOAD_OFFSET,scratch,s->payload_size)) return PS_IO;
    if(ps_get(h+20)!=ps_crc(scratch,s->payload_size) || !s->valid(scratch,s->payload_size)) return PS_CORRUPT;
    {
        uint8_t tail[256];
        if(s->read(s->context,off+PS_SECTOR-sizeof tail,tail,sizeof tail)) return PS_IO;
        for(i=0;i<sizeof tail;i++) if(tail[i]!=0xffu) return PS_CORRUPT;
    }
    info->generation=ps_get(h+12); memcpy(info->name,h+24,PS_NAME_BYTES); info->copy=copy;
    return PS_OK;
}
int ps_load(const ps_store *s, uint32_t slot, void *scratch, ps_info *out) {
    ps_info a,b; int ra,rb,selected;
    if(!scratch || !ps_config(s,slot)) return PS_INVALID;
    ra=ps_copy(s,slot,0,scratch,&a); if(ra==PS_IO) return PS_IO;
    rb=ps_copy(s,slot,1,scratch,&b); if(rb==PS_IO) return PS_IO;
    if(ra!=PS_OK && rb!=PS_OK) return ra==PS_EMPTY && rb==PS_EMPTY ? PS_EMPTY : PS_CORRUPT;
    selected=rb==PS_OK && (ra!=PS_OK || (int32_t)(b.generation-a.generation)>0);
    /* Reload the selected payload, including revalidation, after scanning both copies. */
    ra=ps_copy(s,slot,selected,scratch,&a);
    if(ra!=PS_OK) return ra;
    if(out) *out=a;
    return PS_OK;
}
static int ps_save_impl(const ps_store *s, uint32_t slot, const char *name,
            const void *payload, void *scratch, ps_info *out, int reclaim) {
    uint8_t h[PS_HEADER]; ps_info previous, verify; uint32_t i,off,gen; int r,copy;
    if(!name || !payload || !scratch || !ps_config(s,slot) ||
       ((uintptr_t)payload <= (uintptr_t)scratch ?
        (uintptr_t)scratch-(uintptr_t)payload < s->payload_size :
        (uintptr_t)payload-(uintptr_t)scratch < s->payload_size) ||
       !s->valid(payload,s->payload_size)) return PS_INVALID;
    r=ps_load(s,slot,scratch,&previous);
    if(r!=PS_OK && r!=PS_EMPTY && !(reclaim && r==PS_CORRUPT)) return r;
    copy=r==PS_OK ? !previous.copy : 0; gen=r==PS_OK ? previous.generation+1u : 1u;
    memset(h,0,sizeof h); ps_put(h,PS_MAGIC); ps_put(h+4,PS_VERSION); ps_put(h+8,slot);
    ps_put(h+12,gen); ps_put(h+16,s->payload_size); ps_put(h+20,ps_crc(payload,s->payload_size));
    for(i=0;i<PS_NAME_BYTES-1u && name[i];i++) h[24+i]=(uint8_t)name[i];
    ps_put(h+40,ps_crc(h,40)); off=ps_addr(s,slot,copy);
    if(s->erase(s->context,off) || s->program(s->context,off+PS_PAYLOAD_OFFSET,payload,s->payload_size)) return PS_IO;
    if(s->read(s->context,off+PS_PAYLOAD_OFFSET,scratch,s->payload_size)) return PS_IO;
    if(memcmp(scratch,payload,s->payload_size)) return PS_IO;
    /* A valid header is the commit marker and is always written last. */
    if(s->program(s->context,off,h,sizeof h)) return PS_IO;
    r=ps_copy(s,slot,copy,scratch,&verify);
    if(r!=PS_OK || memcmp(scratch,payload,s->payload_size) || verify.generation!=gen) return PS_IO;
    if(out) *out=verify;
    return PS_OK;
}
int ps_save(const ps_store *s, uint32_t slot, const char *name,
            const void *payload, void *scratch, ps_info *out) {
    return ps_save_impl(s,slot,name,payload,scratch,out,0);
}
int ps_save_reclaim(const ps_store *s, uint32_t slot, const char *name,
                    const void *payload, void *scratch, ps_info *out) {
    return ps_save_impl(s,slot,name,payload,scratch,out,1);
}
