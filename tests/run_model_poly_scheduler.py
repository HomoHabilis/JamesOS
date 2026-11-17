#!/usr/bin/env python3
"""Exercise experimental MODEL changes through the real poly source queue."""
from pathlib import Path
import platform
import subprocess
import host_target
host_target.activate(host_target.target(), with_poly=True)
from run_drive import FLAGS, ROOT

out = ROOT / 'build/model-poly-scheduler-test'
out.mkdir(parents=True, exist_ok=True)
objects = sorted((ROOT / 'build/poly-two-cores').glob('[0-9][0-9][0-9].o'))
shim = ROOT / 'build/poly-two-cores/core.o'
if not objects:
    raise SystemExit('Build isolated per-core DSP objects with tests/run_poly_two_cores.py first')
if not shim.exists():
    raise SystemExit('Missing per-core RNG shim; run tests/run_poly_two_cores.py first')
flags = [*FLAGS, '-DBR_MODEL_MOD_EXPERIMENT=1']
planner = out / 'planner.o'
subprocess.run(['cc', *flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, check=True)
exe = out / 'test_model_poly_scheduler'
linker = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
outputs = []
for parallel in (0, 1):
    subprocess.run(['c++', '-x', 'c', *flags, f'-DBR_MODEL_PARALLEL={parallel}', '-pthread', '-ffunction-sections', '-fdata-sections', 'tests/test_model_poly_scheduler.c', '-x', 'none', str(planner), str(shim), *map(str, objects), linker, '-o', str(exe)], cwd=ROOT, check=True)
    result = subprocess.check_output([str(exe)], cwd=ROOT, text=True)
    print(('two workers: ' if parallel else 'one worker: ') + result, end='')
    outputs.append(result)
if outputs[0] != outputs[1]:
    raise SystemExit('MODEL sweep audio differs between serial and parallel workers')
