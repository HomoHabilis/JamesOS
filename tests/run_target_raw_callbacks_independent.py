#!/usr/bin/env python3
"""Extract and exercise the real target raw callback bodies with mock flash."""
from pathlib import Path
import host_target
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=host_target.select_product((root/'firmware/src/dinghy.c').read_text())  # the selected product's lines (BR_HOST_TARGET)
def extract(prefix):
    start=source.index(prefix); brace=source.index('{',start); depth=0
    for i in range(brace,len(source)):
        depth+=(source[i]=='{')-(source[i]=='}')
        if not depth: return source[start:i+1]
    raise ValueError(prefix)
with tempfile.TemporaryDirectory(prefix='fm1-raw-') as td:
    d=Path(td); body='\n'.join(extract(x) for x in ('static int br_ps_target_read_raw(', 'static int br_ps_target_reserve_sector(', 'static int br_ps_target_program_raw(', 'static int br_ps_target_erase_raw(', 'static int br_ps_target_may_erase('))
    if 'smk37_project_physical(' in body:
        # SMK-37 Pro reads the project window through its sector remap (hal/smk37/smk37_flash_map.h);
        # the mock flash here is linear, so the remap is the identity: this tests the callbacks only.
        body='static uint32_t smk37_project_physical(uint32_t off){return off;}\n'+body
    (d/'target_raw_extracted.inc').write_text(body)
    for opt in ('-O0','-O2'):
        for deletion in (False,True):
            exe=d/('test'+opt[1:]+str(deletion)); flags=['-DFM1_PATTERN_DELETE_EXPERIMENT=1','-DFM1_PATTERN_RUNTIME_TARGET_LINK_ONLY=1'] if deletion else []
            subprocess.run(['cc','-std=c11',opt,'-DDINGHY_HOST=1',*flags,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(d),str(root/'tests/test_target_raw_callbacks_independent.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
