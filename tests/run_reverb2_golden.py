#!/usr/bin/env python3
"""Check the firmware's global reverb against the prototype's golden hashes, for the
shipped build options and for each option behind its -D; never accesses hardware."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# The firmware's strict floating point (no contraction) and wrapping integer flags.
FLAGS = ['-std=gnu11', '-O2', '-fwrapv', '-ffp-contract=off', '-fno-fast-math',
         '-Wall', '-Wno-unused-function']
BUILDS = [
    ('shipped', []),
    ('up4', ['-DRV2_UPSAMPLE_TAPS=4']),
    ('intrd', ['-DRV2_INT_READS=1']),
    ('linestruct', ['-DRV2_LINE_STRUCT=1']),
]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite runner; unused')
    ap.parse_args()
    out = ROOT / 'build/reverb2-golden'
    out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for name, defines in BUILDS:
        binary = out / ('test_reverb2_golden_' + name)
        subprocess.run(['cc', *FLAGS, *defines, 'tests/test_reverb2_golden.c', '-lm', '-o', str(binary)],
                       cwd=ROOT, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        print(result.stdout.splitlines()[-1] if result.stdout else result.stderr, flush=True)
        failed += result.returncode != 0
    if failed:
        raise SystemExit(f'reverb2 golden: {failed} of {len(BUILDS)} builds differ from the prototype')
    print(f'reverb2 golden: all {len(BUILDS)} builds match the prototype bit for bit')

if __name__ == '__main__':
    main()
