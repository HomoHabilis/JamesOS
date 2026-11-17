#!/usr/bin/env python3
"""Render HOME cues with the actual split-audio, poly-enabled host app."""
from pathlib import Path
import subprocess
import host_target
host_target.activate('fm1', with_poly=True)
from run_drive import FLAGS, ROOT

out = ROOT / 'build/home-poly-cue-test'
out.mkdir(parents=True, exist_ok=True)
objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Host DSP objects are required')
binary = out / 'test_home_poly_cue'
flags = [f for f in FLAGS if not f.startswith(('-DBR_PRODUCT=', '-DBR_CAP_POLY='))]
subprocess.run(['c++', '-x', 'c', *flags, '-DBR_PRODUCT=BR_PRODUCT_ID_FM1',
                '-DBR_CAP_POLY=1', '-DFM1_DEVELOPMENT_DIAGNOSTICS=0',
                'tests/test_home_poly_cue.c', 'firmware/src/audio_fixed8_plan_unit.c',
                '-x', 'none', *map(str, objects), '-o', str(binary)], cwd=ROOT, check=True)
subprocess.run([str(binary)], cwd=ROOT, check=True)
