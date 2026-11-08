/* Conservative bridge from the existing two-copy project store to an
 * experimental chunk store. Device integration and proven ownership absent. */
#include "pattern_legacy_migrate.h"
#include <string.h>
#define OLD_MAGIC 0x31504d46u
#define OLD_VERSION 1u
#define NEW_MAGIC 0x314d4c50u
static uint32_t get(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put(uint8_t *p,uint32_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24); }
static uint32_t crc(const void *v,uint32_t n) { const uint8_t *p=v; uint32_t c=~0u,i; while(n--) { c^=*p++; for(i=0;i<8;i++) c=(c>>1)^(0xedb88320u&(0u-(c&1u))); } return ~c; }
typedef struct { uint32_t generation, payload_crc; uint8_t name[PS_NAME_BYTES]; int valid, blank; } old_info;
static int read_old(const ps_store *s,uint32_t slot,uint32_t copy,uint8_t *b,old_info *o) {
    uint32_t i,off=s->base+(slot*2u+copy)*PS_SECTOR;
    memset(o,0,sizeof *o);
    if(s->read(s->context,off,b,PS_SECTOR)) return PFS_IO;
    o->blank=1; for(i=0;i<PS_SECTOR;i++) if(b[i]!=255u) { o->blank=0; break; }
    if(get(b)!=OLD_MAGIC||get(b+4)!=OLD_VERSION||get(b+8)!=slot||get(b+16)!=PLM_LEGACY_BYTES||b[36]!=0||get(b+40)!=crc(b,40)||get(b+20)!=crc(b+PS_PAYLOAD_OFFSET,PLM_LEGACY_BYTES)||!s->valid(b+PS_PAYLOAD_OFFSET,PLM_LEGACY_BYTES)) return PFS_OK;
    for(i=PS_SECTOR-256;i<PS_SECTOR;i++) if(b[i]!=255u) return PFS_OK;
    o->valid=1; o->generation=get(b+12); o->payload_crc=get(b+20); memcpy(o->name,b+24,PS_NAME_BYTES); return PFS_OK;
}
static int config(const ps_store *s) { return s&&s->read&&s->valid&&s->payload_size==PLM_LEGACY_BYTES&&!(s->base%PS_SECTOR)&&s->base<=UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR; }
int plm_load(const ps_store *s,const pfs_store *p,uint32_t slot,char name[PS_NAME_BYTES],void *legacy,void *work,void *scratch) {
    uint8_t *w=work; uint32_t n=0; int r;
    if(!config(s)||!name||!legacy||!w||!scratch||slot>=PS_TOTAL_SLOTS) return PFS_INVALID;
    r=pfs_load(p,slot,w,PLM_SIZE,&n,scratch); if(r) return r;
    if(n!=PLM_SIZE||get(w)!=NEW_MAGIC||get(w+4)!=PLM_LEGACY_BYTES||w[20]||w[21]||w[22]||w[23]||!s->valid(w+PLM_PREFIX,PLM_LEGACY_BYTES)) return PFS_INVALID;
    memcpy(name,w+8,PS_NAME_BYTES); memcpy(legacy,w+PLM_PREFIX,PLM_LEGACY_BYTES); return PFS_OK;
}
int plm_migrate(const ps_store *s,const pfs_store *p,uint32_t slot,void *work,void *scratch) {
    uint8_t *w=work,*b=scratch; old_info a,c,again,*chosen; uint32_t selected,d,n=0; int r;
    if(!config(s)||!p||!w||!b||slot>=PS_TOTAL_SLOTS) return PFS_INVALID;
    if((uintptr_t)w<=(uintptr_t)b ? (uintptr_t)b-(uintptr_t)w<PS_SECTOR : (uintptr_t)w-(uintptr_t)b<PS_SECTOR) return PFS_INVALID;
    r=read_old(s,slot,0,b,&a); if(r) return r; r=read_old(s,slot,1,b,&c); if(r) return r;
    if(!a.valid&&!c.valid) return a.blank&&c.blank?PFS_EMPTY:PFS_INVALID;
    if(a.valid&&c.valid) {
        d=a.generation-c.generation;
        if(!d||d==0x80000000u) return PFS_INVALID;
        selected=(int32_t)d>0?0u:1u;
    } else selected=a.valid?0u:1u;
    chosen=selected?&c:&a; r=read_old(s,slot,selected,b,&again); if(r) return r;
    if(!again.valid||again.generation!=chosen->generation||again.payload_crc!=chosen->payload_crc||memcmp(again.name,chosen->name,PS_NAME_BYTES)) return PFS_IO;
    memset(w,0,PLM_PREFIX); put(w,NEW_MAGIC); put(w+4,PLM_LEGACY_BYTES); memcpy(w+8,again.name,PS_NAME_BYTES); memcpy(w+PLM_PREFIX,b+PS_PAYLOAD_OFFSET,PLM_LEGACY_BYTES);
    /* Detect any existing target before attempting a save. Scratch cannot hold
     * another full blob, so compare it after reconstructing the source. */
    r=pfs_load(p,slot,b,PS_SECTOR,&n,w); /* separate buffers, but overwrites w */
    if(r==PFS_OK) {
        if(n!=PLM_SIZE||get(b)!=NEW_MAGIC||get(b+4)!=PLM_LEGACY_BYTES||memcmp(b+8,again.name,PS_NAME_BYTES)||b[21]||b[22]||b[23]) return PFS_INVALID;
        /* Reload the source and compare every byte, including name. */
        r=read_old(s,slot,selected,w,&a); if(r) return r;
        if(!a.valid||a.generation!=again.generation||a.payload_crc!=again.payload_crc||memcmp(a.name,again.name,PS_NAME_BYTES)||memcmp(b+PLM_PREFIX,w+PS_PAYLOAD_OFFSET,PLM_LEGACY_BYTES)) return PFS_INVALID;
        return PFS_OK;
    }
    if(r!=PFS_EMPTY) return r==PFS_SMALL?PFS_INVALID:r;
    /* pfs_load used work as its 4 KiB scratch: caller's work must actually
     * be at least one sector. Reconstruct after that read. */
    r=read_old(s,slot,selected,b,&a); if(r) return r;
    if(!a.valid||a.generation!=again.generation||a.payload_crc!=again.payload_crc||memcmp(a.name,again.name,PS_NAME_BYTES)) return PFS_IO;
    memset(w,0,PLM_PREFIX); put(w,NEW_MAGIC); put(w+4,PLM_LEGACY_BYTES); memcpy(w+8,a.name,PS_NAME_BYTES); memcpy(w+PLM_PREFIX,b+PS_PAYLOAD_OFFSET,PLM_LEGACY_BYTES);
    return pfs_save(p,slot,w,PLM_SIZE,b);
}
