# SPDX-License-Identifier: GPL-3.0-only
"""Build and run one HD look test (tests/test_<name>_hd.c) on the host; never touches hardware.

Each HD look test builds the real app (always the HD interface) for the selected product
(tests/host_target.py) and asserts the HD look itself. A test writes PPM crops of any defect it reports into
build/<name>-hd-test/crops/ (its first argument).
"""
import argparse
import subprocess
from pathlib import Path

import host_target
from run_drive import FLAGS, ROOT

def main(name, doc):
    ap = argparse.ArgumentParser(description=doc)
    ap.add_argument('--reuse-dsp', action='store_true', help='use the existing build/host objects')
    a = ap.parse_args()
    out = ROOT / f'build/{name.replace("_", "-")}-hd-test'
    crops = out / 'crops'
    crops.mkdir(parents=True, exist_ok=True)
    for f in crops.glob('*.ppm'):
        f.unlink()
    with (out / 'compile.log').open('w') as log:
        if not a.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
        objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
        if not objects:
            raise SystemExit('No host DSP objects; run without --reuse-dsp')
        planner = out / 'planner.o'
        subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        binary = out / f'test_{name}_hd'
        subprocess.run(['c++', '-x', 'c', *FLAGS, f'tests/test_{name}_hd.c', '-x', 'none', str(planner),
                        *map(str, objects), '-o', str(binary)], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    result = subprocess.run([str(binary), str(crops)], cwd=ROOT, capture_output=True, text=True)
    (out / 'result.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    product = 'SMK-37 Pro' if host_target.target() == 'smk37' else 'FM-1'
    if result.returncode:
        raise SystemExit(f'{name} (HD, {product}): failed; crops in {crops}')
    print(f'{name} (HD, {product}): passed')
