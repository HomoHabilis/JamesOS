#!/usr/bin/env python3
"""Exercise the actual rescue dispatcher with fake flash and fault records."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/src/platform.c').read_text()
start = source.index('static int fm1_rescue_command(')
brace = source.index('{', start)
depth = 0
for pos in range(brace, len(source)):
    depth += (source[pos] == '{') - (source[pos] == '}')
    if depth == 0:
        function = source[start:pos + 1]
        break
else:
    raise AssertionError('rescue dispatcher missing')
prefix = '''#include <stdint.h>
#include <string.h>
#include <assert.h>
#define FM1_FAULT_RAM_CODE_SIDECAR 1
#define FM1_FAULT_RAM_CODE_CAPTURE_READ 0
#define FM1_FOREGROUND_DIAG89_WORDS 14u
#define FM1_FOREGROUND_DIAG89_BYTES 56u
#define SETTINGS_AT 0x1000u
static uint32_t recovery_active, flash_ok;
static struct { uint32_t magic,count,vec,pc,dbg,uptime_ms; } fm1_crash;
static struct { uint32_t p3_rst,rst_src,wdt_con; } fm1_boot;
static uint32_t fm1_fault_bundle;
static uint32_t fm1_cpu1_worker_irq_save(void) { return 0u; }
static void fm1_cpu1_worker_irq_restore(uint32_t irq) { (void)irq; }
static void fm1_rescue_word(uint8_t *out, uint32_t v) {
  out[0]=v;out[1]=v>>8;out[2]=v>>16;out[3]=v>>24;
}
static int st_read(uint32_t off,uint8_t *out,uint32_t n) {
  for(uint32_t i=0;i<n;++i) out[i]=(uint8_t)(off+i); return 0;
}
static uint32_t fm1_fault_bundle_chunk(void *bundle,uint32_t off,uint32_t n,uint8_t *out) {
  (void)bundle;if(off!=3u||n!=1u)return 0u;out[0]=77u;return 1u;
}
static void fm1_clock_snapshot(uint32_t out[8]) { memset(out,0,32); }
static int fm1_power_read_command(const uint8_t *b,uint32_t n,uint8_t *r,uint32_t *rn)
{ (void)b;(void)n;r[0]=1;*rn=1;return 1; }
static int fm1_vendor360_diag_command(uint32_t c,const uint8_t *b,uint32_t n,uint8_t *r,uint32_t *rn)
{ (void)c;return fm1_power_read_command(b,n,r,rn); }
static void fm1_p33_snapshot_words(uint32_t out[14]) { memset(out,0,56); }
static void fm1_foreground_journal_snapshot_reply(uint32_t out[14]) { memset(out,0,56); }
'''
test = '''
int main(void) {
  uint8_t reply[128], read[3]={3,0,1}, bad[3]={255,15,2}; uint32_t n;
  assert(!fm1_rescue_command(0xa2u,0,0,reply,&n));
  recovery_active=flash_ok=1u;
  assert(fm1_rescue_command(0xa2u,0,0,reply,&n)&&n==5u&&reply[4]==1u);
  assert(fm1_rescue_command(0xa0u,read,3,reply,&n)&&n==1u&&reply[0]==3u);
  assert(!fm1_rescue_command(0xa0u,bad,3,reply,&n));
  flash_ok=0u; assert(!fm1_rescue_command(0xa0u,read,3,reply,&n)); flash_ok=1u;
  fm1_crash.magic=0x1234u;
  assert(fm1_rescue_command(0x6fu,0,0,reply,&n)&&n==24u&&reply[0]==0x34u);
  assert(fm1_rescue_command(0x7du,read,3,reply,&n)&&n==1u&&reply[0]==77u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
  assert(fm1_rescue_command(0x6au,0,0,reply,&n)&&n==32u);
  assert(fm1_rescue_command(0xaeu,0,0,reply,&n));
  assert(fm1_rescue_command(0x99u,0,0,reply,&n)&&n==56u);
  assert(fm1_rescue_command(0x89u,0,0,reply,&n)&&n==56u);
#else
  assert(!fm1_rescue_command(0x6au,0,0,reply,&n));
  assert(!fm1_rescue_command(0xaeu,0,0,reply,&n));
  assert(!fm1_rescue_command(0x99u,0,0,reply,&n));
  assert(!fm1_rescue_command(0x89u,0,0,reply,&n));
#endif
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='jamesos-rescue-') as tmp:
    path = Path(tmp) / 'rescue.c'
    path.write_text(prefix + function + test)
    for mode in (0, 1):
        exe = Path(tmp) / str(mode)
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable',
                        f'-DFM1_DEVELOPMENT_DIAGNOSTICS={mode}', str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: rescue identity, bounded settings read, fault records and diagnostic selection')
