#!/usr/bin/env python3
"""Run the experimental polyphonic source jobs in the actual host worker queue."""
from pathlib import Path
import platform
import subprocess
import host_target
host_target.activate(host_target.target(), with_poly=True)   # polyphony (BR_CAP_POLY=1) for the selected product
from run_drive import FLAGS, ROOT  # noqa: E402

out = ROOT / 'build/poly-scheduler-test'
out.mkdir(parents=True, exist_ok=True)
objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Build host DSP objects with sh tools/build_braids_host.sh first')
planner = out / 'planner.o'
subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, check=True)
binary = out / 'test_poly_scheduler'
linker = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
subprocess.run(['c++', '-x', 'c', *FLAGS, '-pthread', '-ffunction-sections', '-fdata-sections',
                'tests/test_poly_scheduler.c', '-x', 'none', str(planner), *map(str, objects),
                linker, '-o', str(binary)], cwd=ROOT, check=True)
subprocess.run([str(binary)], cwd=ROOT, check=True)
