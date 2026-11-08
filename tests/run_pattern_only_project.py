#!/usr/bin/env python3
"""Host-only v4 pattern project tests; no instrument access."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-pattern-project-') as temp:
    exe = str(Path(temp) / 'test')
    subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',
                    '-DBR_PATTERN_BANK_EXPERIMENT=1','-Ifirmware/src','-Ifirmware/gen',
                    'tests/test_pattern_only_project.c','firmware/src/pattern_project.c',
                    'firmware/src/pattern_bank.c','firmware/src/pattern_storage_codec.c',
                    'firmware/src/pattern_song.c','-o',exe],cwd=root,check=True)
    subprocess.run([exe],cwd=root,check=True)
