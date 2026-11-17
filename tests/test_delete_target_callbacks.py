#!/usr/bin/env python3
"""Exercise the actual target deletion callbacks with a mocked flash/runtime."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'firmware/src/instrument_diagnostic.c').read_text()
def extract(prefix):
    start=source.index(prefix); brace=source.index('{',start); depth=0
    for i in range(brace,len(source)):
        depth+=(source[i]=='{')-(source[i]=='}')
        if not depth:return source[start:i+1]
    raise ValueError(prefix)
body='\n'.join(extract(x) for x in ('static int br_delete_review(', 'static int br_delete_perform('))
with tempfile.TemporaryDirectory(prefix='fm1-delete-callback-') as td:
    folder=Path(td);(folder/'callbacks.inc').write_text(body)
    for opt in ('-O0','-O2'):
        exe=folder/('test'+opt[1:])
        subprocess.run(['cc','-std=c11',opt,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-I{folder}','-Ifirmware/src','tests/test_delete_target_callbacks.c','firmware/src/project_delete_protocol.c','firmware/src/project_delete_gate.c','firmware/src/pattern_sha256.c','-o',str(exe)],cwd=root,check=True)
        subprocess.run([str(exe)],cwd=root,check=True)
