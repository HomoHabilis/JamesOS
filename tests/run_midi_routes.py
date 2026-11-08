#!/usr/bin/env python3
"""Compile and run bounded MIDI note-routing tests without hardware."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build' / 'midi-routes-test'
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                'tests/test_midi_routes.c', '-o', str(out / 'test')], cwd=ROOT, check=True)
subprocess.run([str(out / 'test')], cwd=ROOT, check=True)
print('MIDI routing ledger passed')
