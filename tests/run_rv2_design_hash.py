#!/usr/bin/env python3
"""Hash the global reverb designer (rv2_design, rv2_target_rt) over its whole input space.

    python3 tests/run_rv2_design_hash.py [--print]

tests/test_rv2_design_hash.c: every TYPE x SIZE 0..127 x DECAY 0..127, each override swept
0..127, 4096 mixed override settings, and the target RT. Compiled with the firmware's strict
floating point (no contraction, no fast math), in the shipped and RV2_LINE_STRUCT builds.
The expected hashes were recorded from integ 38cdc16, before the designer's helpers were
moved out of line; any change to the designed coefficients fails here.
--print shows the current hashes without checking them.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=gnu11', '-O2', '-fwrapv', '-ffp-contract=off', '-fno-fast-math',
         '-Wall', '-Wno-unused-function']
BUILDS = [('shipped', []), ('linestruct', ['-DRV2_LINE_STRUCT=1'])]
EXPECTED = {
    'shipped plain': '98a11dd1ed68db3e',
    'shipped override0': '73c652457bae40d3',
    'shipped override1': '067b5a24dda413fc',
    'shipped override2': '1ce328077729623f',
    'shipped override3': '581324ca696a1195',
    'shipped override4': '0a12673b1e123a5b',
    'shipped mixed': 'f5256ebfa4db4781',
    'shipped target_rt': '5cfe165054daedb5',
    'linestruct plain': '98a11dd1ed68db3e',
    'linestruct override0': '73c652457bae40d3',
    'linestruct override1': '067b5a24dda413fc',
    'linestruct override2': '1ce328077729623f',
    'linestruct override3': '581324ca696a1195',
    'linestruct override4': '0a12673b1e123a5b',
    'linestruct mixed': 'f5256ebfa4db4781',
    'linestruct target_rt': '5cfe165054daedb5',
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--print', action='store_true', help='print the hashes without checking them')
    ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite runner; unused')
    args = ap.parse_args()
    out = ROOT / 'build/rv2-design-hash'
    out.mkdir(parents=True, exist_ok=True)
    got = {}
    for name, defines in BUILDS:
        binary = out / ('test_rv2_design_hash_' + name)
        subprocess.run(['cc', *FLAGS, *defines, 'tests/test_rv2_design_hash.c', '-lm', '-o', str(binary)],
                       cwd=ROOT, check=True)
        for line in subprocess.check_output([str(binary)], cwd=ROOT, text=True).splitlines():
            key, value = line.split()
            got[f'{name} {key}'] = value
    if args.print:
        for k, v in got.items():
            print(f"    '{k}': '{v}',")
        return 0
    bad = [k for k in sorted(set(EXPECTED) | set(got)) if EXPECTED.get(k) != got.get(k)]
    for k in bad:
        print(f'{k}: expected {EXPECTED.get(k)} got {got.get(k)}')
    if bad:
        print(f'FAIL: {len(bad)} of {len(EXPECTED)} reverb designer hashes differ')
        return 1
    print(f'PASS: all {len(EXPECTED)} reverb designer hashes match 38cdc16')
    return 0


if __name__ == '__main__':
    sys.exit(main())
