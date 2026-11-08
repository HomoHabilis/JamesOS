#!/usr/bin/env python3
"""Render every track effect on the host and compare it with a reference tree.

  run_fx_render.py --ref DIR   render this tree and DIR (another copy of the source), then report,
                               per case, whether the two are bit-identical and if not the
                               difference in dB relative to the signal, plus both RMS levels.
  --wav                        also write 16-bit WAVs of both renders for listening.
Never accesses hardware."""
import argparse, math, struct, subprocess, sys, wave
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from run_master_silence import FLAGS

def build(tree, out, log):
    out.mkdir(parents=True, exist_ok=True)
    objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
    objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise RuntimeError('No host DSP objects; run tests/run_master_silence.py first')
    flags = [f if not f.startswith('-Ifirmware') else '-I' + str(tree / f[2:]) for f in FLAGS]
    planner = out / 'planner.o'
    subprocess.run(['cc', *flags, '-c', str(tree / 'firmware/src/audio_fixed8_plan_unit.c'), '-o', str(planner)],
                   cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = out / 'test_fx_render'
    subprocess.run(['c++', '-x', 'c', *flags, '-DFX_RENDER_DINGHY="%s"' % (tree / 'firmware/src/dinghy.c'),
                    'tests/test_fx_render.c', '-x', 'none', str(planner), *map(str, objects), '-o', str(binary)],
                   cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run([str(binary), str(out)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)

def load(p):
    a = array('i'); a.frombytes(p.read_bytes()); return a

def wav(p, a):
    peak = max(1, max(abs(x) for x in a))
    with wave.open(str(p), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(struct.pack('<%dh' % len(a), *(max(-32767, min(32767, x)) for x in a)))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ref', help='another copy of the source tree to compare with')
    ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite runner; always reuses')
    ap.add_argument('--wav', action='store_true')
    args = ap.parse_args()
    if not args.ref:
        print('fx render: comparison tool, skipped without --ref')
        return
    out = ROOT / 'build/fx-render'
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'compile.log').open('w') as log:
        build(ROOT, out / 'new', log)
        build(Path(args.ref).resolve(), out / 'ref', log)
    for p in sorted((out / 'new').glob('*.raw')):
        a, b = load(p), load(out / 'ref' / p.name)
        n = min(len(a), len(b))
        sig = sum(float(x) * x for x in b[:n]) / n
        err = sum(float(a[i] - b[i]) ** 2 for i in range(n)) / n
        rms_a, rms_b = math.sqrt(sum(float(x) * x for x in a[:n]) / n), math.sqrt(sig)
        verdict = 'identical' if err == 0 else 'diff %6.1f dB' % (10 * math.log10(err / sig) if sig else 0)
        print('%-22s %-16s rms new %8.1f  ref %8.1f' % (p.stem, verdict, rms_a, rms_b))
        if args.wav:
            wav(out / (p.stem + '-new.wav'), a); wav(out / (p.stem + '-ref.wav'), b)

if __name__ == '__main__':
    main()
