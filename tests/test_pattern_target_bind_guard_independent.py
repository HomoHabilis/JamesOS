#!/usr/bin/env python3
"""Exercise actual target bridge bind guard without device or hardware I/O."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
src = (root / 'firmware/src/pattern_project_target_bridge.c').read_text()
start = src.index('static const br_pp_target_config *config;')
end = src.index('static int allowed(', start)
body = src[start:end]
code = '#include "pattern_project_target_bridge.h"\n' + body + r'''
#include <assert.h>
#include <stdio.h>
static int allowed(void *p,const pfs_store *s,void *b,uint32_t id,int w){(void)p;(void)s;(void)b;(void)id;(void)w;return 1;}
static int factory(void *p,uint8_t *b,size_t n){(void)p;(void)b;(void)n;return 1;}
static int empty(void *p,const pfs_store *s,void *b,uint32_t id){(void)p;(void)s;(void)b;(void)id;return 1;}
int main(void){br_pp_target_config c={0,allowed,factory,empty};
#if FM1_DEVICE
 assert(br_pp_target_bind(&c)==PFS_INVALID&&!br_pp_target_ready());
#else
 assert(br_pp_target_bind(&c)==PFS_OK&&br_pp_target_ready());
#endif
 puts("target project bind guard passed");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='fm1-target-bind-') as tmp:
    probe = Path(tmp) / 'probe.c'
    binary = Path(tmp) / 'probe'
    probe.write_text(code)
    base=['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-Wno-unused-function',f'-I{root / "firmware/src"}','-DFM1_PATTERN_PROJECT_TARGET_EXPERIMENT=1','-DFM1_PATTERN_TARGET_EXPERIMENT=1']
    for device in (0,1):
        subprocess.run([*base,f'-DFM1_DEVICE={device}',str(probe),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    failed=subprocess.run([*base,'-DFM1_DEVICE=1','-DFM1_PATTERN_FIXED_STORAGE_QUALIFIED=1','-fsyntax-only',str(probe)],capture_output=True,text=True)
    assert failed.returncode and 'no qualified target backend' in failed.stderr
