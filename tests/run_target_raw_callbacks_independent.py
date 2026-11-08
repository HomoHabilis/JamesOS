#!/usr/bin/env python3
"""Extract and exercise the real target raw callback bodies with mock flash."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'firmware/src/dinghy.c').read_text()
def extract(prefix):
    start=source.index(prefix); brace=source.index('{',start); depth=0
    for i in range(brace,len(source)):
        depth+=(source[i]=='{')-(source[i]=='}')
        if not depth: return source[start:i+1]
    raise ValueError(prefix)
with tempfile.TemporaryDirectory(prefix='fm1-raw-') as td:
    d=Path(td); (d/'target_raw_extracted.inc').write_text('\n'.join(extract(x) for x in ('static int br_ps_target_read_raw(', 'static int br_ps_target_reserve_sector(', 'static int br_ps_target_program_raw(', 'static int br_ps_target_erase_raw(', 'static int br_ps_target_may_erase(')))
    for opt in ('-O0','-O2'):
        exe=d/('test'+opt[1:]);subprocess.run(['cc','-std=c11',opt,'-DDINGHY_HOST=1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(d),str(root/'tests/test_target_raw_callbacks_independent.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
