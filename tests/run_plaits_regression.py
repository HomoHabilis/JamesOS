#!/usr/bin/env python3
"""Compare the refactored full-size voice with the repository baseline."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build/plaits-regression'
out.mkdir(parents=True, exist_ok=True)
source = subprocess.check_output(['git', 'show', 'HEAD:firmware/plaits/plaits_c_api.cc'], cwd=ROOT, text=True)
source = source.replace('#include "../hal/fm1_audio_rate.h"',
                        f'#include "{ROOT / "firmware/hal/fm1_audio_rate.h"}"')
baseline = out / 'baseline.cc'
baseline.write_text(source)
flags = ['-std=c++14', '-O2', '-DTEST', '-ffp-contract=off', '-fno-exceptions', '-fno-rtti',
         '-fno-threadsafe-statics', '-Dstmlib=plaits_stmlib', '-Ifirmware/plaits', '-Ifirmware/src',
         '-Ifirmware/plaits/upstream', '-Ifirmware/braids/upstream']
baseline_obj = out / 'baseline.o'
subprocess.run(['c++', *flags, '-c', str(baseline), '-o', str(baseline_obj)], cwd=ROOT, check=True)
objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
current_obj = ROOT / 'build/host/plaits_reference/plaits_plaits_c_api.cc.o'
if current_obj not in objects:
    raise SystemExit('Build host DSP objects with sh tools/build_braids_host.sh first')
common = [str(obj) for obj in objects if obj != current_obj]
outputs = []
for name, api in [('baseline', baseline_obj), ('current', current_obj)]:
    binary = out / name
    subprocess.run(['c++', *flags, 'tests/plaits_regression_fixture.cc', str(api), *common,
                    '-o', str(binary)], cwd=ROOT, check=True)
    outputs.append(subprocess.check_output([str(binary)], cwd=ROOT))
if outputs[0] != outputs[1]:
    raise SystemExit('Plaits audio differs from the repository baseline')
print('all 24 Plaits models match the original full-size renderer')
