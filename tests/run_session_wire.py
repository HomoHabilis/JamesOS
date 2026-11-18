#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the real-app session wiring test on the host; never access hardware.

    python3 tests/run_session_wire.py [--reuse-dsp] [--fixtures DIR ...]

The test sets every saved setting through the paths the UI uses, captures, clears,
restores and compares; fuzzes random in-range states; and checks that rejected
records leave the running state untouched. Fixture files (session captures) are
optional: each one given is restored and captured again and must come back
byte-identical.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import host_target  # noqa: E402

FLAGS = [
    '-std=c11', '-O2', '-fwrapv', '-Wno-unused-function',
    '-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=0',
    '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=1',
    '-DPLAITS_INTERNAL_SAMPLE_RATE=44100', '-DFM1_PLAITS_SOURCE_RATE_32K=0',
    '-DFM1_PLAITS_SOURCE_RATE_44100=1', '-DFM1_PLAITS_SOURCE_RATE_38400=0',
    '-DFM1_PLAITS_SOURCE_RATE_48000=0', '-DFM1_PLAITS_SRC_LAGRANGE4=0',
    '-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0', '-DFM1_PLAITS_PHASE_LUT=0',
    '-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DBR_FX_TAPE_WOW_DELTA_LUT=0',
    '-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0', '-Ifirmware/src', '-Ifirmware/gen',
]
# Optional captures kept outside the repository (used only when present): BR_SESSION_FIXTURES names
# their folder; the default is the developer's work folder two levels above this checkout.
_WORK = Path(__file__).resolve().parents[3]
CONVERTED = Path(os.environ.get('BR_SESSION_FIXTURES', _WORK / 'batch-1113/session-format/converted'))
OWNER_FIXTURES = [CONVERTED / '00-S0-session.v17.bin', CONVERTED / '10-S1-session.v17.bin',
                  CONVERTED / 'probe-fx-base-session.v17.bin']
V16_SAMPLE = Path(os.environ.get('BR_SESSION_V16_SAMPLE', _WORK / 'fm1-poly-timing/results/10-S1/session.bin'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--reuse-dsp', action='store_true',
                    help='Use existing host objects only when their C++ sources/headers are unchanged')
    ap.add_argument('--fixtures', nargs='*', default=None,
                    help='session files to restore and capture byte-identically (default: the owner\'s converted captures when present)')
    args = ap.parse_args()
    fixtures = args.fixtures
    if fixtures is None:
        fixtures = [str(p) for p in OWNER_FIXTURES if p.is_file()]
    out = ROOT / 'build/session_wire-test'
    out.mkdir(parents=True, exist_ok=True)
    flags = FLAGS + host_target.product_flags()
    with (out / 'compile.log').open('w') as log:
        if not args.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
        objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
        if not objects:
            raise RuntimeError('No host DSP objects; run without --reuse-dsp')
        planner = out / 'planner.o'
        subprocess.run(['cc', *flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c',
                        '-o', str(planner)], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
        binary = out / 'test_session_wire'
        r = subprocess.run(['c++', '-x', 'c', *flags, 'tests/test_session_wire.c',
                            '-x', 'none', str(planner), *map(str, objects),
                            '-o', str(binary)], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT)
    if r.returncode:
        print((out / 'compile.log').read_text()[-4000:])
        print('session wire: build failed')
        return 1
    env = dict(os.environ)
    if V16_SAMPLE.is_file():
        env['SESSION_WIRE_V16'] = str(V16_SAMPLE)      # an old record must be rejected
    result = subprocess.run([str(binary), *fixtures], cwd=ROOT, capture_output=True, text=True, env=env)
    (out / 'result.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
