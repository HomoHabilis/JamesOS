#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/project_store.c"

static unsigned char flash[PS_TOTAL_SLOTS*2*PS_SECTOR], old[3484], newer[3484], scratch[3484], saved[sizeof flash];
static int remaining=-1, bad_read=0, erase_limit=-1;
static int read_flash(void *c, uint32_t off, void *p, uint32_t n) {
    (void)c; if(bad_read || off>sizeof flash || n>sizeof flash-off) return -1;
    memcpy(p,flash+off,n); return 0;
}
static int erase_flash(void *c, uint32_t off) {
    (void)c; if(off%PS_SECTOR || off>sizeof flash-PS_SECTOR) return -1;
    if(erase_limit>=0) { memset(flash+off,0xff,(unsigned)erase_limit); return -1; }
    if(remaining==0) return -1;
    if(remaining>0) remaining--;
    memset(flash+off,0xff,PS_SECTOR); return 0;
}
static int program_flash(void *c, uint32_t off, const void *data, uint32_t n) {
    const unsigned char *p=data; (void)c;
    if(off>sizeof flash || n>sizeof flash-off) return -1;
    for(uint32_t i=0;i<n;i++) {
        if(remaining==0) return -1;
        if(remaining>0) remaining--;
        if((flash[off+i]&p[i])!=p[i]) return -1;
        flash[off+i]&=p[i];
    }
    return 0;
}
static int valid(const void *p,uint32_t n) { return n==sizeof old && ((const unsigned char *)p)[0]==0x42; }
int main(void) {
    ps_store s={0,read_flash,erase_flash,program_flash,valid,0,sizeof old}; ps_info info;
    memset(flash,0xff,sizeof flash); memset(old,0x32,sizeof old); old[0]=0x42;
    memset(newer,0x73,sizeof newer); newer[0]=0x42;
    assert(ps_load(&s,0,scratch,&info)==PS_EMPTY);
    for(unsigned slot=0;slot<PS_SLOTS;slot++) {
        old[1]=(unsigned char)slot;
        assert(ps_save(&s,slot,"A PROJECT",old,scratch,&info)==PS_OK);
        assert(ps_load(&s,slot,scratch,&info)==PS_OK && !memcmp(scratch,old,sizeof old));
        for(unsigned byte=PS_SECTOR-256;byte<PS_SECTOR;byte++)
            assert(flash[slot*2u*PS_SECTOR+byte]==0xff);
    }
    old[1]=0; memcpy(saved,flash,sizeof flash);
    /* Stop after every byte of the erase/program/commit sequence. */
    for(int limit=0;limit<=1+(int)sizeof old+(int)PS_HEADER;limit++) {
        memcpy(flash,saved,sizeof flash); remaining=limit;
        int result=ps_save(&s,0,"B PROJECT",newer,scratch,&info); remaining=-1;
        assert(ps_load(&s,0,scratch,&info)==PS_OK);
        assert(!memcmp(scratch,old,sizeof old) || !memcmp(scratch,newer,sizeof newer));
        if(result==PS_OK) assert(!memcmp(scratch,newer,sizeof newer));
    }
    memcpy(flash,saved,sizeof flash); remaining=-1;
    assert(ps_save(&s,0,"B PROJECT",newer,scratch,&info)==PS_OK);
    memcpy(saved,flash,sizeof flash);
    /* Interrupt erasure of the older copy at every byte boundary. */
    for(int limit=0;limit<=(int)PS_SECTOR;limit++) {
        memcpy(flash,saved,sizeof flash); erase_limit=limit;
        assert(ps_save(&s,0,"C PROJECT",old,scratch,&info)==PS_IO);
        erase_limit=-1;
        assert(ps_load(&s,0,scratch,&info)==PS_OK && !memcmp(scratch,newer,sizeof newer));
    }
    memcpy(flash,saved,sizeof flash);
    flash[PS_SECTOR+PS_PAYLOAD_OFFSET+8]^=1;
    assert(ps_load(&s,0,scratch,&info)==PS_OK && !memcmp(scratch,old,sizeof old));
    memcpy(flash,saved,sizeof flash); flash[2*PS_SECTOR-1]=0;
    assert(ps_load(&s,0,scratch,&info)==PS_OK && !memcmp(scratch,old,sizeof old));
    for(unsigned byte=0;byte<PS_HEADER;byte++) for(unsigned bit=0;bit<8;bit++) {
        memcpy(flash,saved,sizeof flash); flash[PS_SECTOR+byte]^=(unsigned char)(1u<<bit);
        assert(ps_load(&s,0,scratch,&info)==PS_OK && !memcmp(scratch,old,sizeof old));
    }
    memcpy(flash,saved,sizeof flash);
    for(unsigned slot=1;slot<PS_SLOTS;slot++) {
        old[1]=(unsigned char)slot;
        assert(ps_load(&s,slot,scratch,&info)==PS_OK && !memcmp(scratch,old,sizeof old));
    }
    old[1]=0;
    bad_read=1; assert(ps_save(&s,0,"NO",newer,scratch,&info)==PS_IO); bad_read=0;
    assert(ps_load(&s,PS_TOTAL_SLOTS,scratch,&info)==PS_INVALID);
    /* Foreign data or a torn first write must never be mistaken for empty. */
    memset(flash,0xff,sizeof flash); flash[PS_PAYLOAD_OFFSET]=0;
    assert(ps_load(&s,0,scratch,&info)==PS_CORRUPT);
    assert(ps_save(&s,0,"NO",newer,scratch,&info)==PS_CORRUPT);
    assert(ps_save_reclaim(&s,0,"RECLAIM",newer,scratch,&info)==PS_OK);
    assert(ps_load(&s,0,scratch,&info)==PS_OK && !memcmp(scratch,newer,sizeof newer));
    memset(flash,0xff,sizeof flash);
    remaining=2; assert(ps_save(&s,0,"TORN",newer,scratch,&info)==PS_IO); remaining=-1;
    assert(ps_load(&s,0,scratch,&info)==PS_CORRUPT);
    assert(ps_save(&s,0,"OVERLAP",newer,newer+1,&info)==PS_INVALID);
    /* Generation rollover selects zero after UINT32_MAX. */
    memset(flash,0xff,sizeof flash);
    assert(ps_save(&s,0,"OLD",old,scratch,&info)==PS_OK);
    ps_put(flash+12,UINT32_MAX); ps_put(flash+40,ps_crc(flash,40));
    assert(ps_save(&s,0,"NEW",newer,scratch,&info)==PS_OK && info.generation==0);
    assert(ps_load(&s,0,scratch,&info)==PS_OK && info.generation==0 && !memcmp(scratch,newer,sizeof newer));
    puts("PASS: 12 slots, interrupted writes, CRC fallback and read errors");
    return 0;
}
