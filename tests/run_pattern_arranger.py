#!/usr/bin/env python3
"""Song arrangement engine (firmware/src/pattern_song.c) as a pure module: edits, loops,
play order, edits during play, limits and the saved record. Host only; never touches hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-pattern-arranger-') as temp:
    exe = str(Path(temp) / 'test')
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-Ifirmware/src', 'tests/test_pattern_arranger.c', 'firmware/src/pattern_song.c', '-o', exe],
                   cwd=root, check=True)
    subprocess.run([exe], cwd=root, check=True)
