#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render screens of the real firmware on the host (tests/host.c) to PNG: for the website, the manual and
reviews. No hardware is touched.

    python3 tools/render_screens.py OUTDIR [--scale 2] [--only name,name]

Each screen is a small script of button presses and encoder turns, then a screenshot."""
import argparse, struct, subprocess, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from run_drive import FLAGS            # the same build flags as the host tests

BTN = dict(oct_down=0, oct_up=1, fx=2, sel=3, env=4, lfo=5, edit=6, glo=7, home=8, save=9, arp=10, seq=11, play=12, rec=13)
ENC = dict(select=0, algorithm=1, knob1=2, knob2=3, knob3=4, knob4=5, presets=6)

# name -> list of (action, arg, arg2); times are assigned automatically, 150 ms apart
SCREENS = {
    'p0-sound-0': [],
    'p0-env-0': [('btn', 'env')],
    'p0-fx-0': [('btn', 'fx')],
    'p0-fx-1': [('btn', 'fx')] * 2,
    'p0-fx-3': [('btn', 'fx')] * 4,
    'punch-loop': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 1))],
    'punch-echo': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 8))],
    'punch-filter': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 13))],
    'punch-tremolo': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 20))],
    'dk-seq-0': [('enc', 'algorithm', 3), ('btn', 'seq')],
    'p0-fx-2': [('btn', 'fx')] + [('btn', 'fx')] * 2,
    'p0-fx-4': [('btn', 'fx')] + [('btn', 'fx')] * 4,
    'p0-glo-0': [('btn', 'glo')],
    'p0-glo-0-signal': [('btn', 'glo'), ('keys', 1 << 7)],
    'p0-glo-1': [('btn', 'glo')] * 2,
    'p0-glo-2': [('btn', 'glo')] * 7,
    'p0-glo-2-monitor': [('btn', 'glo')] * 7 + [('enc', 'knob4', 1)],
    'p0-glo-3': [('btn', 'glo')] * 3,
    'p0-glo-4': [('btn', 'glo')] * 5,
    'p0-glo-5': [('btn', 'glo')] * 6,
    'p0-glo-6': [('btn', 'glo')] * 4,
    'p0-seq-0': [('btn', 'seq')],
    'p0-sel-0': [('btn', 'sel')],
    'p0-lfo-0': [('btn', 'lfo')],
    'p0-arp-0': [('btn', 'arp')],
    'p0-edit-2': [('btn', 'edit')] * 3,
    'dk-retrig-edit': [('enc', 'algorithm', 3), ('btn', 'seq'), ('keys', 1), ('keys', 0), ('btn', 'arp'), ('keys', 1), ('enc', 'knob1', 3), ('enc', 'knob2', 3)],
    'dk-arp-message': [('enc', 'algorithm', 3), ('btn', 'arp')],
    'dk-sound-0': [('enc', 'algorithm', 3)],
    'dk-kit-solo': [('enc', 'algorithm', 3), ('btn', 'edit'), ('btn', 'oct_up')],
    'dk-kit-mute': [('enc', 'algorithm', 3), ('btn', 'edit'), ('btn', 'oct_down')],
    'dk-kit-level': [('enc', 'algorithm', 3), ('btn', 'edit')],
    'dk-sel-0': [('enc', 'algorithm', 3), ('btn', 'sel')],
    'dk-braids': [('enc', 'algorithm', 3)],
    'dk-plaits': [('enc', 'algorithm', 3), ('enc', 'presets', 1)],
    'dk-80s': [('enc', 'algorithm', 3), ('enc', 'presets', 2)],
    'dk-90s': [('enc', 'algorithm', 3), ('enc', 'presets', 3)],
}


def build(binary):
    objs = sorted((ROOT / 'build/host/braids').glob('*.o')) + sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objs:
        raise SystemExit('Host DSP objects are missing: run tools/build_braids_host.sh first')
    planner = binary.parent / 'planner.o'
    subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, check=True)
    subprocess.run(['c++', '-x', 'c', *FLAGS, '-DDINGHY_HOST', 'firmware/src/dinghy.c', '-x', 'none', str(planner),
                    *map(str, objs), '-o', str(binary)], cwd=ROOT, check=True, stderr=subprocess.DEVNULL)


def script_for(steps):
    t, lines = 400, []
    for s in steps:
        if s[0] == 'keys':                        # hold keyboard keys (bit mask) from here on
            lines.append('%d keys %d' % (t, s[1])); t += 600
        elif s[0] == 'btn':
            lines.append('%d btn %d' % (t, BTN[s[1]])); t += 700
        else:
            for _ in range(abs(s[2])):
                lines.append('%d enc %d %d' % (t, ENC[s[1]], 1 if s[2] > 0 else -1)); t += 150
            t += 100
    lines.append('%d shot' % (t + 300)); lines.append('%d end' % (t + 500))
    return '\n'.join(lines) + '\n', t + 300


def ppm(path):
    d = path.read_bytes(); i = 0; parts = []
    while len(parts) < 4:
        while d[i:i + 1].isspace(): i += 1
        j = i
        while not d[j:j + 1].isspace(): j += 1
        parts.append(d[i:j]); i = j
    i += 1
    return int(parts[1]), int(parts[2]), d[i:]


def png(path, w, h, rgb, scale):
    row = lambda y: b''.join(rgb[(y * w + x) * 3:(y * w + x) * 3 + 3] * scale for x in range(w))
    raw = b''.join((b'\0' + row(y)) * scale for y in range(h))
    def chunk(tag, data): return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('out'); ap.add_argument('--scale', type=int, default=2); ap.add_argument('--only', default='')
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'build/render-screens'; work.mkdir(parents=True, exist_ok=True)
    binary = work / 'dinghy'
    build(binary)
    for name, steps in SCREENS.items():
        if a.only and name not in a.only.split(','): continue
        script, at = script_for(steps)
        (work / 'script.txt').write_text(script)
        subprocess.run([str(binary), str(work / name), str(work / 'script.txt')], cwd=ROOT, capture_output=True, check=True)
        shot = work / ('%s-%d.ppm' % (name, at))
        if not shot.exists(): print('no screenshot for', name); continue
        w, h, rgb = ppm(shot); png(out / (name + '.png'), w, h, rgb, a.scale); print('wrote', name)


if __name__ == '__main__':
    main()
