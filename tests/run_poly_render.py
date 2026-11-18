#!/usr/bin/env python3
"""Build and run the isolated six-voice/envelope host test."""
from pathlib import Path
import subprocess
import host_target
# SMK-37 Pro: poly scheduler off as in JamesOS-K (the test drives the voices itself); FM-1 has the
# polyphony sources (BR_POLY_SUPPORT) only with polyphony on.
host_target.activate(host_target.target(), with_poly=host_target.target() == 'fm1')
from run_drive import FLAGS, ROOT  # noqa: E402

out = ROOT / 'build/poly-render-test'
out.mkdir(parents=True, exist_ok=True)
objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Build the host DSP objects with sh tools/build_braids_host.sh first')
planner = out / 'planner.o'
subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, check=True)
binary = out / 'test_poly_render'
subprocess.run(['c++', '-x', 'c', *FLAGS, '-Ifirmware/src/poly', 'tests/test_poly_render.c', '-x', 'none', str(planner),
                *map(str, objects), '-o', str(binary)], cwd=ROOT, check=True)
subprocess.run([str(binary)], cwd=ROOT, check=True)
print('polyphonic source and independent envelopes: passed')
