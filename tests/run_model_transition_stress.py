#!/usr/bin/env python3
"""Build and run full-size Plaits transition stress; host timings are comparative only."""
from pathlib import Path
import argparse
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--reuse-dsp', action='store_true')
args = parser.parse_args()
out = root / 'build/model-transition-stress'
tree = out / 'tree'
out.mkdir(parents=True, exist_ok=True)          # (with --reuse-dsp nothing else creates it)
if not args.reuse_dsp:
    (tree / 'tools').mkdir(parents=True, exist_ok=True)
    if not (tree / 'firmware').exists():
        (tree / 'firmware').symlink_to(root / 'firmware', target_is_directory=True)
    shutil.copy2(root / 'tools/build_braids_host.sh', tree / 'tools/build_braids_host.sh')
    subprocess.run(['sh', str(tree / 'tools/build_braids_host.sh')], cwd=tree, check=True)
# --reuse-dsp: the suite's shared objects (tools/build_braids_host.sh, same script and flags as the private copy
# below; verified byte-identical), not a private rebuild
base = root if args.reuse_dsp else tree
objects = sorted((base / 'build/host/braids').glob('*.o'))
objects += sorted((base / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('DSP objects missing (build/host); run without --reuse-dsp' if args.reuse_dsp
                     else 'Stress DSP objects missing; run without --reuse-dsp')
subprocess.run(['c++', '-std=c++14', '-O2', '-DTEST', '-ffp-contract=off',
                '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                '-Dstmlib=plaits_stmlib', '-Ifirmware/plaits', '-Ifirmware/src',
                '-Ifirmware/plaits/upstream', '-Ifirmware/braids/upstream',
                'tests/test_model_transition_stress.cc', *map(str, objects),
                '-o', str(out / 'stress')], cwd=root, check=True)
subprocess.run([str(out / 'stress')], cwd=root, check=True)
