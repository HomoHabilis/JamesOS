#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SMK-37 Pro polyphony unit tests against the host Plaits objects (never hardware):

    test_poly_allocator.c         voice allocation, stealing and note-off generations
    test_poly_storage.cc          compact voice storage in the primary/extra arenas
    test_poly_source.cc           polyphonic Plaits source: note on, render, metadata per voice
    test_poly_model_arenas.cc     model-specific arenas render like the full-size arena
    test_poly_model_sweep.cc      full-capacity held-note transitions and canaries
    test_model_sweep_plan.c       bounded staged transition planning
    test_poly_sweep_no_prewarm.c  no-op prewarming cannot commit a transition
    test_plaits_compact_state.cc  compact Plaits state renders like the full state

Built for SMK-37 Pro with polyphony (-DBR_PRODUCT=BR_PRODUCT_ID_SMK37, BR_CAP_POLY=1). Needs
the host DSP objects (tools/build_braids_host.sh). The poly headers are found in src/ or src/poly/.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import host_target  # noqa: E402

ROOT = HERE.parent
CXX = ['-std=c++14', '-O2', '-DTEST', '-ffp-contract=off', '-fno-exceptions', '-fno-rtti',
       '-fno-threadsafe-statics', '-Dstmlib=plaits_stmlib']
INCLUDES = ['-Ifirmware/src/poly', '-Ifirmware/src', '-Ifirmware/plaits', '-Ifirmware/plaits/upstream',
            '-Ifirmware/braids/upstream', '-Ifirmware/gen']
TESTS = ['test_poly_allocator.c', 'test_model_sweep_plan.c', 'test_poly_sweep_no_prewarm.c', 'test_poly_storage.cc', 'test_poly_source.cc', 'test_poly_model_sweep.cc',
         'test_poly_model_arenas.cc', 'test_plaits_compact_state.cc']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--reuse-dsp', action='store_true', help='use the existing build/host objects')
    a = ap.parse_args()
    flags = host_target.activate(host_target.target(), with_poly=True)
    if not a.reuse_dsp:
        subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
    objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise SystemExit('Build the host DSP objects with sh tools/build_braids_host.sh first')
    with tempfile.TemporaryDirectory(prefix='poly-units-') as tmp:
        for name in TESTS:
            exe = Path(tmp) / Path(name).stem
            if name.endswith('.c'):
                cmd = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *flags, *INCLUDES,
                       f'tests/{name}', '-o', str(exe)]
            else:
                cmd = ['c++', *CXX, *flags, *INCLUDES, f'tests/{name}', *map(str, objects), '-o', str(exe)]
            subprocess.run(cmd, cwd=ROOT, check=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
            print(f'{name}: passed', flush=True)
    print('SMK-37 Pro polyphony unit tests passed')


if __name__ == '__main__':
    main()
