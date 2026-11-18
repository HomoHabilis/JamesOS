#!/usr/bin/env python3
"""Hash the Plaits drums and all 24 synth models as the firmware drives them (tests/test_drum_render_hash.cc).

    python3 tests/run_drum_render_hash.py [--print]

Covers the three drum engines called directly with the kit's trigger values and chunk sizes
(162 settings each, plus the kick's half-rate kit entry) and synth-track models 21-23 through
the C API (27 settings each), then all 24 synth models through the C API (27 settings each:
Voice::Render and every engine). The expected hashes were recorded from 279d1ae, before the
unreachable unpatched-trigger code was compiled out; any change to these sounds fails here. --print shows the current hashes without checking them.
"""
import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=c++14', '-O2', '-DTEST', '-ffp-contract=off', '-fno-exceptions', '-fno-rtti',
         '-fno-threadsafe-statics', '-Dstmlib=plaits_stmlib', '-Ifirmware/plaits', '-Ifirmware/src',
         '-Ifirmware/plaits/upstream', '-Ifirmware/braids/upstream']
BRAIDS_FLAGS = ['-std=c++14', '-O2', '-fwrapv', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                '-Ifirmware/braids/upstream', '-Ifirmware/braids']
EXPECTED = {
    'engine bass': '49a93ecc186fe371',
    'engine bass_rate2': '6f4da1376e092e2e',
    'engine snare': '2306451fe73ec3da',
    'engine hat': '49417f98fb346f88',
    'model 21': '6b896ae63664e3f6',
    'model 22': '23dadff557ca60a6',
    'model 23': '7a5e59ccfbb84f48',
    'synth 0': '7ada13d29ec2e048',
    'synth 1': '57398e6d0c3df9a5',
    'synth 2': '2e67820bd90a57bc',
    'synth 3': 'fd04b478177629b2',
    'synth 4': 'c32c11e3420a2bd6',
    'synth 5': 'ad3454fb6d1bd253',
    'synth 6': '53983787ef532eb2',
    'synth 7': 'b3c3923dcafa1b3c',
    'synth 8': '0a88194b05a5bf64',
    'synth 9': '30196ece4467799b',
    'synth 10': 'd2d44963b6b719d5',
    'synth 11': '46a41b14077927dd',
    'synth 12': '502f877da915a795',
    'synth 13': '145908c036759c9f',
    'synth 14': 'f4a8022177d9c01b',
    'synth 15': 'b6bb1706faadc155',
    'synth 16': '80b7b6195ee98c96',
    'synth 17': 'cc3e40ecd25a8772',
    'synth 18': '0705393b8fd69d81',
    'synth 19': 'd69301a1efd9ab92',
    'synth 20': '30153f63aa55d569',
    'synth 21': '18d82eab91e52cf3',
    'synth 22': '3815a9d0b93439b6',
    'synth 23': '58fb69d4e23eba10',
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--print', action='store_true', help='print the hashes without checking them')
    args = ap.parse_args()
    out = ROOT / 'build/drum-render-hash'
    out.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / 'firmware/plaits/plaits_c_api.cc']
    for sub in ('plaits', 'stmlib'):
        sources += sorted((ROOT / 'firmware/plaits/upstream' / sub).rglob('*.cc'))

    # The packed Plaits wavetables are initialised from the Braids resource bank.
    sources.append(ROOT / 'firmware/braids/upstream/braids/resources.cc')

    def compile_one(src):
        obj = out / (str(src.relative_to(ROOT)).replace('/', '_') + '.o')
        flags = BRAIDS_FLAGS if 'braids' in src.parts else FLAGS
        subprocess.run(['c++', *flags, '-c', str(src), '-o', str(obj)], cwd=ROOT, check=True,
                       stderr=subprocess.DEVNULL if 'braids' in src.parts else None)
        return obj

    with ThreadPoolExecutor() as pool:
        objects = list(pool.map(compile_one, sources))
    binary = out / 'test_drum_render_hash'
    subprocess.run(['c++', *FLAGS, 'tests/test_drum_render_hash.cc', *map(str, objects), '-o', str(binary)],
                   cwd=ROOT, check=True, stderr=subprocess.DEVNULL)
    lines = subprocess.check_output([str(binary)], cwd=ROOT, text=True).splitlines()
    got = {' '.join(l.split()[:2]): l.split()[2] for l in lines}
    if args.print:
        print('\n'.join(lines))
        return 0
    bad = [k for k in sorted(set(EXPECTED) | set(got)) if EXPECTED.get(k) != got.get(k)]
    for k in bad:
        print(f'{k}: expected {EXPECTED.get(k)} got {got.get(k)}')
    if bad:
        print(f'FAIL: {len(bad)} of {len(EXPECTED)} Plaits render hashes differ')
        return 1
    print(f'PASS: all {len(EXPECTED)} Plaits render hashes match 279d1ae (4 drum engine paths, 24 synth models)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
