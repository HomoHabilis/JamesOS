#!/usr/bin/env python3
"""Exercise all adaptive polyphony capacities through the host audio queue."""
from pathlib import Path
import os
import argparse
import platform
import subprocess
import host_target

os.environ['BR_HOST_EXTRA_DEFINES'] = '-DBR_ADAPTIVE_POLY=1 -DBR_MODEL_MOD_EXPERIMENT=1'
host_target.activate(host_target.target(), with_poly=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-dsp', action='store_true')
args = parser.parse_args()
from run_drive import FLAGS, ROOT
out = ROOT / 'build/adaptive-poly-test'
out.mkdir(parents=True, exist_ok=True)
if not args.reuse_dsp and os.environ.get('BR_REUSE_DSP') != '1':
    subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, check=True)
objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Build host DSP objects first')
planner = out / 'planner.o'
subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, check=True)
linker = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
binary = out / 'test_adaptive_poly_engine'
subprocess.run(['c++', '-x', 'c', *FLAGS, '-pthread', '-ffunction-sections', '-fdata-sections',
                'tests/test_adaptive_poly_engine.c', '-x', 'none', str(planner), *map(str, objects),
                linker, '-o', str(binary)], cwd=ROOT, check=True)
subprocess.run([str(binary)], cwd=ROOT, check=True)
