#include "pattern_unified_store.h"
#include <string.h>
#define MAGIC 0x31504d46u
static uint32_t get(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t crc(const void *v,uint32_t n) { const uint8_t *p=v; uint32_t c=~0u,i; while(n--) { c^=*p++; for(i=0;i<8;i++) c=(c>>1)^(0xedb88320u&(0u-(c&1u))); } return ~c; }
static int read_guard(void *v,uint32_t off,void *dst,uint32_t n) { pus_guard *g=v; return g->legacy->read(g->legacy->context,off,dst,n); }
static int erase_guard(void *v,uint32_t off) {
    pus_guard *g=v; const ps_store *s=g->legacy; uint8_t *b=g->sector; uint32_t i,slot; int empty=1;
    g->armed=UINT32_MAX;
    if(off<s->base || (off-s->base)%PS_SECTOR || off-s->base>=PS_TOTAL_SLOTS*2u*PS_SECTOR || s->read(s->context,off,b,PS_SECTOR)) return -1;
    for(i=0;i<PS_SECTOR;i++) if(b[i]!=255u) { empty=0; break; }
    if(!empty) {
        slot=(off-s->base)/(2u*PS_SECTOR);
        if(get(b)!=MAGIC||get(b+4)!=1u||get(b+8)!=slot||get(b+16)!=s->payload_size||b[36]!=0||b[37]!=0||b[38]!=0||b[39]!=0||get(b+40)!=crc(b,40)||get(b+20)!=crc(b+PS_PAYLOAD_OFFSET,s->payload_size)||!s->valid(b+PS_PAYLOAD_OFFSET,s->payload_size)) return -1;
        for(i=PS_SECTOR-256;i<PS_SECTOR;i++) if(b[i]!=255u) return -1;
        /* Unknown data in any unused legacy record bytes forbids erasure. */
        for(i=44;i<PS_PAYLOAD_OFFSET;i++) if(b[i]!=255u) return -1;
        for(i=PS_PAYLOAD_OFFSET+s->payload_size;i<PS_SECTOR-256;i++) if(b[i]!=255u) return -1;
    }
    if(s->erase(s->context,off)) return -1;
    g->armed=off; return 0;
}
static int program_guard(void *v,uint32_t off,const void *data,uint32_t n) {
    pus_guard *g=v; if(g->armed==UINT32_MAX||off<g->armed||off-g->armed>PS_SECTOR||n>PS_SECTOR-(off-g->armed)) return -1;
    return g->legacy->program(g->legacy->context,off,data,n);
}
int pus_legacy_save(const ps_store *s,uint32_t slot,const char *name,const void *payload,void *ps_scratch,void *guard_sector,ps_info *info) {
    pus_guard guard; ps_store wrapped;
    if(!s||!s->read||!s->erase||!s->program||!s->valid||!guard_sector||!ps_scratch||s->payload_size>PS_MAX_PAYLOAD||!s->payload_size||s->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR) return PS_INVALID;
    if((uintptr_t)guard_sector<=(uintptr_t)ps_scratch ? (uintptr_t)ps_scratch-(uintptr_t)guard_sector<PS_SECTOR : (uintptr_t)guard_sector-(uintptr_t)ps_scratch<s->payload_size) return PS_INVALID;
    if((uintptr_t)guard_sector<=(uintptr_t)payload ? (uintptr_t)payload-(uintptr_t)guard_sector<PS_SECTOR : (uintptr_t)guard_sector-(uintptr_t)payload<s->payload_size) return PS_INVALID;
    guard.legacy=s; guard.sector=guard_sector; guard.armed=UINT32_MAX; wrapped=*s; wrapped.context=&guard; wrapped.read=read_guard; wrapped.erase=erase_guard; wrapped.program=program_guard;
    return ps_save(&wrapped,slot,name,payload,ps_scratch,info);
}
