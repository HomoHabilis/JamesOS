"""Run the actual loader's record-cleanup function over project sectors."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
loader = (root/'firmware/loader/ldr_core.c').read_text()
ota = (root/'firmware/src/ota.c').read_text()
cleanup = loader[loader.index('static void ldr_records_drop(void)\n{'):loader.index('\nstatic int ldr_pass(void)')]
crc = ota[ota.index('static uint32_t ota_crc16('):ota.index('\nstatic void ota_jl_enc(')]
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "firmware/src/project_store.c"
#define LDR_REC_LO 0x93000u
#define LDR_REC_HI 0xfc000u
static uint8_t flash[0x100000], snapshot[PS_TOTAL_SLOTS*2u*PS_SECTOR];
static uint8_t payload[3484], scratch[3484];
static unsigned erased, cleared;
static int rd(void *c,uint32_t off,void *out,uint32_t n) { (void)c;memcpy(out,flash+off,n);return 0; }
static int erase(void *c,uint32_t off) { (void)c;memset(flash+off,0xff,PS_SECTOR);return 0; }
static int prog(void *c,uint32_t off,const void *src,uint32_t n) {
    (void)c;const uint8_t *p=src;
    for(uint32_t i=0;i<n;i++){assert((flash[off+i]&p[i])==p[i]);flash[off+i]&=p[i];}return 0;
}
static int valid(const void *p,uint32_t n) { return n==sizeof payload && ((const uint8_t *)p)[0]==0x42; }
static int ldr_fread(uint32_t off,void *out,uint32_t n) { return rd(0,off,out,n); }
static int ldr_erase(uint32_t off) { assert(off==0xe4000u);erased++;return erase(0,off); }
static void ldr_record_clear(void) { cleared++; }
static uint32_t ota_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8; }
''' + crc + '\n' + cleanup + r'''
int main(void) {
    ps_store s={0,rd,erase,prog,valid,0x97000u,sizeof payload};ps_info info;
    memset(flash,0xff,sizeof flash);memset(payload,0x42,sizeof payload);
    for(unsigned i=0;i<PS_TOTAL_SLOTS;i++) {
        payload[1]=(uint8_t)i;
        assert(ps_save(&s,i,"PROJECT",payload,scratch,&info)==PS_OK);
        payload[2]++;
        assert(ps_save(&s,i,"PROJECT",payload,scratch,&info)==PS_OK);
    }
    memcpy(snapshot,flash+0x97000u,sizeof snapshot);
    /* Create one valid update record, at the loader staging address. */
    uint8_t *record=flash+0xe4f00u;memset(record,0,80);
    record[6]=0x41;record[7]=0x54;
    uint32_t sum=ota_crc16(record+2,78,0);record[0]=(uint8_t)sum;record[1]=(uint8_t)(sum>>8);
    assert(ota_rd16(record)!=0);
    ldr_records_drop();
    assert(cleared==1 && erased==1);
    assert(!memcmp(snapshot,flash+0x97000u,sizeof snapshot));
    for(unsigned i=0;i<PS_TOTAL_SLOTS;i++)assert(ps_load(&s,i,scratch,&info)==PS_OK);
    puts("PASS: actual update-record cleanup preserves all project and working copies");
}
'''
with tempfile.TemporaryDirectory(prefix='fm1-loader-store-') as tmp:
    src=Path(tmp)/'test.c'; exe=Path(tmp)/'test';src.write_text(source)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
