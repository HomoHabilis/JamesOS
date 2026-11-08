#!/usr/bin/env python3
"""Run actual legacy project flash callbacks with fake flash; never access hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/src/instrument_project_device.c').read_text()
start = source.index('static int br_project_erase(')
end = source.index('static int br_project_valid(', start)
functions = source[start:end]
prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define BR_PROJECT_BASE 0x97000u
#define BR_PROJECT_END 0xb1000u
#define PS_SECTOR 4096u
#define FL_IN(off,n,lo,hi) ((off)>=(lo) && (n)<=(hi)-(off))
static int flash_ok=1, parks, erases, programs;
static int flash_worker_park(void){parks++;return 0;}
static int flash_worker_resume(void){return 0;}
static int fl_erase4k(uint32_t off,uint32_t *took){(void)off;*took=1;erases++;return 0;}
static int fl_write(uint32_t off,const uint8_t *p,uint32_t n){(void)off;(void)p;(void)n;programs++;return 0;}
'''
main = r'''
int main(void){uint8_t b[16]={0};int a=br_project_erase(0,BR_PROJECT_BASE),p=br_project_program(0,BR_PROJECT_BASE,b,sizeof b);
#if EXPECT_DENIED
 assert(a==-1&&p==-1&&!parks&&!erases&&!programs);
#else
 assert(!a&&!p&&parks==2&&erases==1&&programs==1);
#endif
 puts("legacy project write gate passed");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='fm1-legacy-guard-') as tmp:
    src = Path(tmp) / 'probe.c'
    binary = Path(tmp) / 'probe'
    src.write_text(prefix + functions + main)
    for deny in (0, 1):
        flags = ['-DFM1_DEVICE=1', f'-DFM1_PATTERN_TARGET_EXPERIMENT={deny}', f'-DEXPECT_DENIED={deny}']
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-fsanitize=address,undefined', *flags, str(src), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
