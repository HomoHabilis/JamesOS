#!/usr/bin/env python3
"""Build the experimental MODEL destination against the real host app."""
import argparse
import subprocess
from pathlib import Path
import host_target
host_target.activate(host_target.target(), with_poly=True)
from run_mod_target_tests import FLAGS, ROOT

parser = argparse.ArgumentParser()
parser.add_argument('--reuse-dsp', action='store_true')
args = parser.parse_args()
out = ROOT / 'build/model-modulation-test'
out.mkdir(parents=True, exist_ok=True)
with (out / 'compile.log').open('w') as log:
    if not args.reuse_dsp:
        subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    objects = sorted((ROOT / 'build/host/braids').glob('*.o')) + sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise RuntimeError('Host DSP objects missing; run without --reuse-dsp')
    flags = [*FLAGS, '-DBR_MODEL_MOD_EXPERIMENT=1']
    planner = out / 'planner.o'
    subprocess.run(['cc', *flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    exe = out / 'test_model_modulation'
    subprocess.run(['c++', '-x', 'c', *flags, 'tests/test_model_modulation.c', '-x', 'none', str(planner), *map(str, objects), '-o', str(exe)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
subprocess.run([str(exe)], cwd=ROOT, check=True)
