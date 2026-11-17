#!/usr/bin/env python3
"""Hash the PSC3 pattern encoders' output over many inputs and every slice (tests/test_pattern_encoder_hash.c).

    python3 tests/run_pattern_encoder_hash.py [--print]

fm1_pattern_codec_encode, fm1_pattern_codec_encode_slice and fm1_codec_source_encode: return codes,
lengths and output bytes, including FULL and a failing source read, plus a decode round trip. The
expected hashes were recorded from integ 0943a05, before the two encoders shared one pass; any change
to the encoded bytes fails here. --print shows the current hashes without checking them.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wno-unused-function', '-Ifirmware/src']
EXPECTED = {
    'full': 'f5f184ed42f6ef6e',
    'slice': 'b03842fb4153f73d',
    'source': '3a0e3b4f3437ecca',
    'roundtrip': '705a0918e2de3a43',
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--print', action='store_true', help='print the hashes without checking them')
    ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite runner; unused')
    args = ap.parse_args()
    out = ROOT / 'build/pattern-encoder-hash'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test_pattern_encoder_hash'
    subprocess.run(['cc', *FLAGS, 'tests/test_pattern_encoder_hash.c', 'firmware/src/pattern_storage_codec.c',
                    'firmware/src/pattern_codec_source.c', '-o', str(binary)], cwd=ROOT, check=True)
    got = dict(line.split() for line in subprocess.check_output([str(binary)], cwd=ROOT, text=True).splitlines())
    if args.print:
        for k, v in got.items():
            print(f"    '{k}': '{v}',")
        return 0
    bad = [k for k in sorted(set(EXPECTED) | set(got)) if EXPECTED.get(k) != got.get(k)]
    for k in bad:
        print(f'{k}: expected {EXPECTED.get(k)} got {got.get(k)}')
    if bad:
        print(f'FAIL: {len(bad)} of {len(EXPECTED)} pattern encoder hashes differ')
        return 1
    print(f'PASS: all {len(EXPECTED)} pattern encoder hashes match 0943a05')
    return 0


if __name__ == '__main__':
    sys.exit(main())
