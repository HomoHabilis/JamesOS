#!/usr/bin/env python3
"""Run the delay-law test (TIME list, feedback law, bandwidth, tail across
changes) on the host at the device rate (32 kHz); never access hardware.
  --ref DIR   also build DIR (another copy of the source) and print its
              measurements (no checks), for before/after tables."""
import argparse
from pathlib import Path
import subprocess
import host_target                      # product selection: BR_HOST_TARGET

ROOT = Path(__file__).resolve().parents[1]
FLAGS = [
    '-std=c11', '-O2', '-fwrapv', '-Wno-unused-function',
    '-DFM1_NATIVE_OUTPUT_32K=1', '-DFM1_NATIVE_OUTPUT_48K=0',
    '-DBR_AUDIO_INTERNAL_BLOCK160=1', '-DBR_AUDIO_INTERNAL_BLOCK256=0',
    '-DPLAITS_INTERNAL_SAMPLE_RATE=44100', '-DFM1_PLAITS_SOURCE_RATE_32K=0',
    '-DFM1_PLAITS_SOURCE_RATE_44100=1', '-DFM1_PLAITS_SOURCE_RATE_38400=0',
    '-DFM1_PLAITS_SOURCE_RATE_48000=0', '-DFM1_PLAITS_SRC_LAGRANGE4=0',
    '-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0', '-DFM1_PLAITS_PHASE_LUT=0',
    '-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0', '-DFM1_DEVELOPMENT_DIAGNOSTICS=1',
] + host_target.product_flags()          # FM-1: nothing

def run_or_report(cmd, log, **kw):
    if subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, **kw).returncode:
        log.flush()
        print(Path(log.name).read_text()[-3000:])
        raise SystemExit('delay laws: build failed (see %s)' % log.name)

def build(tree, out, log, extra=()):
    out.mkdir(parents=True, exist_ok=True)
    flags = FLAGS + ['-I' + str(tree / 'firmware/src'), '-I' + str(tree / 'firmware/gen'), *extra]
    objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
    objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise RuntimeError('No host DSP objects; run without --reuse-dsp')
    planner = out / 'planner.o'
    run_or_report(['cc', *flags, '-c', str(tree / 'firmware/src/audio_fixed8_plan_unit.c'),
                   '-o', str(planner)], log, cwd=ROOT)
    binary = out / 'test_delay_laws'
    source = out / 'test_delay_laws.c'
    source.write_text((ROOT / 'tests/test_delay_laws.c').read_text().replace(
        '"../firmware/src/dinghy.c"', '"%s"' % (tree / 'firmware/src/dinghy.c')))
    run_or_report(['c++', '-x', 'c', *flags, str(source), '-x', 'none', str(planner),
                   *map(str, objects), '-o', str(binary)], log, cwd=ROOT)
    return binary

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reuse-dsp', action='store_true',
                    help='Use existing host objects only when their C++ sources/headers are unchanged')
    ap.add_argument('--ref', help='another source tree to measure (older TIME list)')
    ap.add_argument('--ref-notes', default='11', help='number of TIME entries in --ref')
    args = ap.parse_args()
    out = ROOT / 'build/delay-laws-test'
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'compile.log').open('w') as log:
        if not args.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        if args.ref:
            ref = build(Path(args.ref).resolve(), out / 'ref', log,
                        ['-DBR_FX_DELAY_NOTES=%su' % args.ref_notes])
        binary = build(ROOT, out / 'new', log, ['-DBR_FX_DELAY_NOTES=14u'])
    if args.ref:
        r = subprocess.run([str(ref), '--measure'], cwd=ROOT, capture_output=True, text=True)
        (out / 'ref.log').write_text(r.stdout + r.stderr)
        print('== reference', args.ref); print(r.stdout + r.stderr, end='')
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    (out / 'result.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()

if __name__ == '__main__':
    main()
