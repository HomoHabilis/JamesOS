#!/usr/bin/env python3
"""Played-note timing through the real loop and engine (tests/test_note_timing.c); never touches hardware.

    python3 tests/run_note_timing.py [--reuse-dsp] [--ref OLD_TREE]

Checks that a note's onset does not depend on how long the loop was held (up to BR_NOTE_DELAY_MS),
that longer holds start at the next block, that bursts and fast runs keep order and lose nothing, and
that a note recorded while the loop was held lands where it was played. With --ref, also prints the
same tables for another tree (e.g. the code before this change) for comparison; that tree is not checked.
"""
import argparse
import re
import subprocess
from pathlib import Path
import host_target
from run_bank import FLAGS

ROOT = Path(__file__).resolve().parents[1]


def build(tree, out, log, objects):
    planner = out / 'planner.o'
    subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)],
                   cwd=tree, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = out / 'test_note_timing'
    subprocess.run(['c++', '-x', 'c', *FLAGS, str(ROOT / 'tests/test_note_timing.c'), '-x', 'none',
                    str(planner), *map(str, objects), '-o', str(binary)],
                   cwd=tree, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--reuse-dsp', action='store_true', help='use the existing host DSP objects')
    ap.add_argument('--ref', help='another tree to print the same tables for (not checked)')
    args = ap.parse_args()
    out = ROOT / 'build/note-timing-test'
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'compile.log').open('w') as log:
        if not args.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
        objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
        if not objects:
            raise RuntimeError('No host DSP objects; run without --reuse-dsp')
        binary = build(ROOT, out, log, objects)
        if args.ref:
            ref_out = out / 'ref'
            ref_out.mkdir(exist_ok=True)
            # The test file includes ../firmware/src/dinghy.c relative to itself: compile a copy in the ref tree.
            ref = Path(args.ref).resolve()
            (ref / 'tests/test_note_timing.c').write_text((ROOT / 'tests/test_note_timing.c').read_text())
            planner = ref_out / 'planner.o'
            subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)],
                           cwd=ref, stdout=log, stderr=subprocess.STDOUT, check=True)
            ref_bin = ref_out / 'test_note_timing'
            subprocess.run(['c++', '-x', 'c', *FLAGS, 'tests/test_note_timing.c', '-x', 'none', str(planner),
                            *map(str, objects), '-o', str(ref_bin)], cwd=ref, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
    failed = 0
    if args.ref:
        r = subprocess.run([str(ref_bin)], cwd=ROOT, capture_output=True, text=True)
        print('--- reference tree:', args.ref)
        print(r.stdout + r.stderr, end='')
        for hold in (0, 20):
            r = subprocess.run([str(ref_bin), 'rec', str(hold)], cwd=ROOT, capture_output=True, text=True)
            print(r.stdout + r.stderr, end='')
        print('--- this tree')
    r = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    print(r.stdout + r.stderr, end='')
    failed |= r.returncode != 0
    recorded = {}
    for hold in (0, 5, 20, 50):
        r = subprocess.run([str(binary), 'rec', str(hold)], cwd=ROOT, capture_output=True, text=True)
        print(r.stdout + r.stderr, end='')
        m = re.search(r'step (\d+) micro (-?\d+)', r.stdout)
        if r.returncode or not m:
            failed = 1
            continue
        recorded[hold] = (int(m.group(1)), int(m.group(2)))
    if len(set(recorded.values())) != 1:
        print('FAIL: the recorded position depends on the hold:', recorded)
        failed = 1
    if failed:
        raise SystemExit('note timing: FAILED')
    print('note timing: all checks passed (tables, burst, fast run, recording)')


if __name__ == '__main__':
    main()
