#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and run the session v17 codec test on the host; never access hardware.

    python3 tests/run_session_v17.py               run the test, check docs/SESSION-V17.md
    python3 tests/run_session_v17.py --write-doc   regenerate docs/SESSION-V17.md from the table

Two builds run at the same time: -O1 with -fsanitize=address,undefined (when the host compiler supports it) runs
the test with 20,000 fuzz buffers; a plain -O2 build runs it in full (100,000 fuzz buffers). Without sanitizer
support the plain build alone runs in full. (The suite runs this runner at its own flags, not the -O0 logic
class, which would override them.)
It also prints a host -Os size of the codec + table built as the device would include it
(BR_V17_NAMES=0): a rough host estimate, not a device measurement.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import host_target

host_target.activate(host_target.target())

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / ('docs/SESSION-V20-MODEL.md' if '-DBR_MODEL_MOD_EXPERIMENT=1' in
              os.environ.get('BR_HOST_EXTRA_DEFINES', '').split() else 'docs/SESSION-V20.md')
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-Ifirmware/src']
SAN = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer']
DEVICE_TU = r'''
#define BR_V17_NAMES 0
#include "instrument_session_v17.h"
br_session_v17_t g_s; uint8_t g_b[BR_V17_TOTAL_LEN];
int v17_entry(uint32_t n) {
    uint32_t c = 0u; int r = br_v17_decode(g_b, n, &g_s, &c);
    br_v17_defaults(&g_s);
    return r + (int)br_v17_encode(&g_s, g_b, sizeof g_b, &c);
}
'''


SAN_FUZZ = '20000'      # fuzz buffers under the sanitizers; the plain -O2 build runs the default 100,000


def build(out, sanitize):
    cmd = ['cc', *FLAGS, *(['-O1', '-g', *SAN] if sanitize else ['-O2']), 'tests/test_session_v17.c', '-o', str(out)]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def size_estimate(out_dir):
    src = out_dir / 'v17_device_tu.c'
    obj = out_dir / 'v17_device_tu.o'
    src.write_text(DEVICE_TU)
    r = subprocess.run(['cc', '-std=c11', '-Os', '-Ifirmware/src', '-c', str(src), '-o', str(obj)],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        return 'size estimate: compile failed'
    r = subprocess.run(['size', str(obj)], capture_output=True, text=True)
    return 'host -Os size of codec + tables (BR_V17_NAMES=0; host estimate, not device):\n' + r.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--write-doc', action='store_true', help='regenerate docs/SESSION-V17.md and exit')
    ap.add_argument('--reuse-dsp', action='store_true', help=argparse.SUPPRESS)  # suite compatibility; no DSP used
    a = ap.parse_args()
    out_dir = ROOT / 'build/session-v17-test'
    out_dir.mkdir(parents=True, exist_ok=True)
    plain, asan = out_dir / 'test_session_v17', out_dir / 'test_session_v17_asan'
    with ThreadPoolExecutor(max_workers=2) as pool:
        fa = pool.submit(build, asan, True)
        fp = pool.submit(build, plain, False)
        r, ra = fp.result(), fa.result()
    if r.returncode:
        print(r.stdout + r.stderr, end='')
        print('session v17: build failed')
        return 1
    sanitized = ra.returncode == 0
    binary = plain
    doc = subprocess.run([str(binary), '--doc'], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    if a.write_doc:
        DOC.parent.mkdir(parents=True, exist_ok=True)
        DOC.write_text(doc)
        print(f'wrote {DOC.relative_to(ROOT)}')
        return 0
    with ThreadPoolExecutor(max_workers=2) as pool:
        fp = pool.submit(subprocess.run, [str(plain)], cwd=ROOT, capture_output=True, text=True)
        fa = pool.submit(subprocess.run, [str(asan), '--fuzz', SAN_FUZZ], cwd=ROOT, capture_output=True,
                         text=True) if sanitized else None
        res = fp.result()
        resa = fa.result() if fa else None
    print(res.stdout + res.stderr, end='')
    if resa is not None:
        print('--- same test, -O1 + address,undefined sanitizers, fuzz limited to ' + SAN_FUZZ + ' buffers ---')
        print(resa.stdout + resa.stderr, end='')
    print(size_estimate(out_dir))
    print('sanitizers: ' + (f'address,undefined (fuzz {SAN_FUZZ} of 100000 buffers)' if sanitized
                            else 'NOT AVAILABLE (plain build)'))
    if resa is not None and resa.returncode:
        res = resa
    if not DOC.exists() or DOC.read_text() != doc:
        print(f'session v17: {DOC.relative_to(ROOT)} differs from the table; run tests/run_session_v17.py --write-doc')
        return 1
    print(f'{DOC.relative_to(ROOT)} matches the table')
    if res.returncode:
        print('session v17: FAILED')
        return 1
    print('session v17: all checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
