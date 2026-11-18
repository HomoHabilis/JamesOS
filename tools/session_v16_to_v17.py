#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert v16 session captures to v17 on the host and prove the mapping. Never touches hardware.

    python3 tools/session_v16_to_v17.py --out DIR IN.v16 [IN.v16 ...]

Builds tools/session_v16_to_v17.c (it shares the field table in
firmware/src/instrument_session_v17.h), converts each input to DIR/<name>.v17.bin, writes
DIR/SHA256SUMS.txt (input sha, input path -> output sha, output path), then decodes every
output with the same table and prints a dozen representative settings next to the values
read here, independently, straight from the v16 bytes. Exits 1 on any mismatch.
Inputs are only read.
"""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PART = lambda p: 8 + 58 * p        # noqa: E731
MOD = lambda p: 240 + 165 * p      # noqa: E731


def v16_reference(b):
    """A dozen settings decoded directly from the v16 byte map (1-CURRENT-FORMAT.md)."""
    i8 = lambda x: x - 256 if x > 127 else x  # noqa: E731
    word = 0
    for i in range(15):
        word |= (b[2568 + i] >> 6) << (i * 2)
    ch1 = 0 if b[2568 + 15] & 0x40 else (word & 15) + 1
    arp = b[PART(0) + 18:PART(0) + 26]
    punch = b[PART(3) + 22:PART(3) + 26]
    pv = punch[2] | punch[3] << 8
    return {
        'global.bpm': b[4] + (256 if b[967] & 0x80 else 0),
        'global.swing': b[PART(0) + 16],
        'global.midi_ch_t1': ch1,
        'global.midi_clock_in': (word >> 28) & 1,
        'master.filter': b[900],
        'master.resonance': b[906] if (b[905], b[907]) == (0x52, 0xA7) else 0,
        'master.env_depth': b[960] - 128,
        'master.comp_threshold': b[953],
        'master.reverb_type': {0: 0, 1: 1, 2: 0, 3: 2, 4: 2}[b[MOD(0) + 31]],
        'master.reverb_size': b[PART(0) + 12],
        'track1.preset': b[PART(0)],
        'track1.filter': b[PART(0) + 13] >> 1,
        'track2.tune': i8(b[PART(1) + 3]),
        'track3.lfo1_rate': b[MOD(2) + 14],
        'track1.route_amount[0]': i8(b[MOD(0) + 39]),
        'track1.arp_mode': arp[1] if arp[0] == 0x41 else 0,
        'track4.rec_quantize': b[966] >> 4,
        'track1.pan': b[916],
        'kit.lane_level[2]': b[PART(3) + 20] & 0x7F,
        'kit.lane_mute[1]': b[PART(3) + 19] >> 7,
        'kit.drum_snap': b[MOD(3)],
        'kit.punch_echo_feedback': (pv & 31) if punch[0] == 0x50 else 12,
    }


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True, help='output directory')
    ap.add_argument('--build-dir', type=Path, help='isolated helper build directory')
    ap.add_argument('--reuse-build', action='store_true',
                    help='use the helper already built in --build-dir instead of compiling it again')
    ap.add_argument('inputs', nargs='+')
    a = ap.parse_args()
    out_dir = Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    build = a.build_dir or ROOT / 'build/session-v17-tools'
    build.mkdir(parents=True, exist_ok=True)
    tool = build / 'session_v16_to_v17'
    if not (a.reuse_build and a.build_dir and tool.exists()):
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', 'tools/session_v16_to_v17.c',
                        '-o', str(tool)], cwd=ROOT, check=True)
    sums, bad = [], 0
    for inp in map(Path, a.inputs):
        name = f'{inp.parent.name}-{inp.stem}' if inp.stem == 'session' else inp.stem.removesuffix('-v16')
        outp = out_dir / f'{name}.v17.bin'
        r = subprocess.run([str(tool), str(inp), str(outp)], capture_output=True, text=True)
        print(r.stdout + r.stderr, end='')
        if r.returncode:
            bad += 1
            continue
        sums.append(f'{sha(inp)}  {inp}  ->  {sha(outp)}  {outp}')
        dump = subprocess.run([str(tool), '--dump', str(outp)], capture_output=True, text=True, check=True).stdout
        got = dict(line.split('=', 1) for line in dump.splitlines())
        if got.get('clamps') != '0':
            print(f'  decode clamps: {got.get("clamps")}')
            bad += 1
        ref = v16_reference(inp.read_bytes())
        print(f'  {"setting":28s} {"v16 bytes":>10s} {"v17 decoded":>12s}')
        for k, v in ref.items():
            ok = str(v) == got.get(k)
            bad += not ok
            print(f'  {k:28s} {v:>10} {got.get(k, "MISSING"):>12s}{"" if ok else "   MISMATCH"}')
    (out_dir / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n')
    print(f'{len(sums)} converted, {bad} problems; SHA-256 list in {out_dir / "SHA256SUMS.txt"}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
