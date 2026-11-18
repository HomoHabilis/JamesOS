#!/usr/bin/env python3
"""Test SMK track-arrow navigation against the host app."""
import argparse
import subprocess
from pathlib import Path
import host_target
host_target.activate('smk37')            # SMK-37 Pro only: -DBR_PRODUCT=... in every compile
from run_bank import FLAGS  # noqa: E402

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--reuse-dsp', action='store_true', help='use the existing build/host objects')
a = ap.parse_args()
ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build/smk-track-arrows-test'                  # its own folder: run_bank.py uses build/bank-test
out.mkdir(parents=True, exist_ok=True)
if not a.reuse_dsp:
    subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
planner = out / 'planner.o'
subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c',
                '-o', str(planner)], cwd=ROOT, check=True)
binary = out / 'test_smk_track_arrows'
subprocess.run(['c++', '-x', 'c', *FLAGS, '-Ifirmware/src/smk37', 'tests/test_smk_track_arrows.c',
                '-x', 'none', str(planner), *map(str, objects), '-o', str(binary)],
               cwd=ROOT, check=True)
subprocess.run([str(binary)], cwd=ROOT, check=True)
print('SMK track navigation passed')
