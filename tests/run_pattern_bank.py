#!/usr/bin/env python3
"""Run the bank reclamation and cancellation checks with sanitizers."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-pattern-bank-') as d:
    exe=Path(d)/'test'
    subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all','-DBR_PATTERN_BANK_EXPERIMENT=1',
        '-Ifirmware/src','-Ifirmware/gen','tests/test_pattern_bank.c',
        'firmware/src/pattern_bank.c','firmware/src/pattern_storage_codec.c',
        '-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe)],cwd=root,check=True)
