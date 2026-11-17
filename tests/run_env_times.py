#!/usr/bin/env python3
"""Run the envelope time test (32 kHz device rate, then 44.1 and 48 kHz) on the host; never access hardware."""
import argparse
import concurrent.futures
from pathlib import Path
import subprocess
import host_target                      # product selection: BR_HOST_TARGET

ROOT = Path(__file__).resolve().parents[1]
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
] + host_target.product_flags()          # FM-1: nothing

# The device runs at 32 kHz; 44.1 and 48 kHz are build options.
RATES = [
    ('32k', ['-DFM1_NATIVE_OUTPUT_32K=1', '-DFM1_NATIVE_OUTPUT_48K=0', '-DBR_AUDIO_INTERNAL_BLOCK160=1', '-DBR_AUDIO_INTERNAL_BLOCK256=0']),
    ('44k', ['-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=0', '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=1']),
    ('48k', ['-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=1', '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=0', '-DBR_AUDIO_INTERNAL_BLOCK240=1']),
]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reuse-dsp', action='store_true',
                    help='Use existing host objects only when their C++ sources/headers are unchanged')
    ap.add_argument('--diagnostics', choices=('development', 'distribution'), default='development')
    args = ap.parse_args()
    FLAGS.append('-DFM1_DEVELOPMENT_DIAGNOSTICS=' + ('1' if args.diagnostics == 'development' else '0'))
    out = ROOT / ('build/env-times-test-' + args.diagnostics)
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'compile.log').open('w') as log:
        if not args.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
        objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
        if not objects:
            raise RuntimeError('No host DSP objects; run without --reuse-dsp')
    # The three rates are independent programs: build them at the same time (each with its own
    # planner and binary), then run them in rate order as before.
    def build(rate, flags):
        rate_flags = [f for f in FLAGS if not f.startswith(
            ('-DFM1_NATIVE_OUTPUT_', '-DBR_AUDIO_INTERNAL_BLOCK', '-DBR_FX_RATE_SIGNED_441_RECIP'))]
        rate_flags += flags
        with (out / f'compile-{rate}.log').open('w') as log:
            planner = out / f'planner-{rate}.o'
            subprocess.run(['cc', *rate_flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c',
                            '-o', str(planner)], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
            binary = out / f'test_env_times_{rate}'
            subprocess.run(['c++', '-x', 'c', *rate_flags, 'tests/test_env_times.c',
                            '-x', 'none', str(planner), *map(str, objects),
                            '-o', str(binary)], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        return binary
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(RATES)) as pool:
        futures = [pool.submit(build, rate, flags) for rate, flags in RATES]
        binaries = [f.result() for f in futures]
    with (out / 'compile.log').open('a') as log:
        for rate, _ in RATES:
            log.write((out / f'compile-{rate}.log').read_text())
    results = []
    for binary in binaries:
        result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
        results.append(result)
        print(result.stdout + result.stderr, end='')
    (out / 'result.log').write_text(''.join(r.stdout + r.stderr for r in results))
    for r in results:
        r.check_returncode()

if __name__ == '__main__':
    main()
