/* Host-only NOR emulator for the actual device SAVE UI and adapter. */
#define FL_IN(o,n,l,h) ((uint32_t)(o)>=(l) && (uint32_t)(o)<=(h) && (uint32_t)(n)<=(h)-(uint32_t)(o))
static uint8_t host_project_flash[0x1a000], flash_ok=1;
static int host_project_fail;
static int flash_worker_park(void) { return 0; }
static int flash_worker_resume(void) { return 0; }
static int st_read(uint32_t off,void *dst,uint32_t n) {
    if(host_project_fail==3 || !FL_IN(off,n,0x97000u,0xb1000u))return -1;
    memcpy(dst,host_project_flash+off-0x97000u,n);return 0;
}
static int fl_erase4k(uint32_t off,uint32_t *took) {
    if(host_project_fail==1 || (off&4095u) || !FL_IN(off,4096u,0x97000u,0xb1000u))return -1;
    memset(host_project_flash+off-0x97000u,0xff,4096);*took=1;return 0;
}
static int fl_write(uint32_t off,const uint8_t *src,uint32_t n) {
    uint32_t i;
    if(host_project_fail==2 || !FL_IN(off,n,0x97000u,0xb1000u))return -1;
    for(i=0;i<n;i++)if((host_project_flash[off-0x97000u+i]&src[i])!=src[i])return -1;
    for(i=0;i<n;i++)host_project_flash[off-0x97000u+i]&=src[i];
    return 0;
}
