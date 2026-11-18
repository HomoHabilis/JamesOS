#!/usr/bin/env python3
"""Run the filter knob-law test on the host (track filter at 44.1, 48 and 32 kHz; global
high-pass through the master frame at 44.1 kHz); never access hardware."""
import argparse
import concurrent.futures
from pathlib import Path
import subprocess
import host_target                      # product selection: BR_HOST_TARGET

ROOT = Path(__file__).resolve().parents[1]
FLAGS = [
    '-std=c11', '-O2', '-fwrapv', '-Wno-unused-function',
    '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=1',
    '-DPLAITS_INTERNAL_SAMPLE_RATE=44100', '-DFM1_PLAITS_SOURCE_RATE_32K=0',
    '-DFM1_PLAITS_SOURCE_RATE_44100=1', '-DFM1_PLAITS_SOURCE_RATE_38400=0',
    '-DFM1_PLAITS_SOURCE_RATE_48000=0', '-DFM1_PLAITS_SRC_LAGRANGE4=0',
    '-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0', '-DFM1_PLAITS_PHASE_LUT=0',
    '-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DBR_FX_TAPE_WOW_DELTA_LUT=0',
    '-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0', '-Ifirmware/src', '-Ifirmware/gen',
] + host_target.product_flags()          # FM-1: nothing
RATES = {'44k': ['-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=0'],
         '48k': ['-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=1'],
         '32k': ['-DFM1_NATIVE_OUTPUT_32K=1', '-DFM1_NATIVE_OUTPUT_48K=0']}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reuse-dsp', action='store_true',
                    help='Use existing host objects only when their C++ sources/headers are unchanged')
    args = ap.parse_args()
    out = ROOT / 'build/filter-laws-test'
    out.mkdir(parents=True, exist_ok=True)
    binaries = []
    with (out / 'compile.log').open('w') as log:
        subprocess.run(['python3', 'tools/gen_filter_ladder.py', '--check'], cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        if not args.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
        objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
        if not objects:
            raise RuntimeError('No host DSP objects; run without --reuse-dsp')
    # The four programs are independent: build them at the same time (one log each, appended
    # to compile.log in the old order).
    def compile_steps(name, steps):
        with (out / f'compile-{name}.log').open('w') as log:
            for cmd in steps:
                subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    jobs = [(name, [['cc', *FLAGS, *rate, 'tests/test_filter_laws.c', '-o',
                     str(out / ('test_filter_laws_' + name)), '-lm']]) for name, rate in RATES.items()]
    planner = out / 'planner.o'
    binary = out / 'test_filter_laws_master'
    jobs.append(('master', [
        ['cc', *FLAGS, *RATES['44k'], '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)],
        ['c++', '-x', 'c', *FLAGS, *RATES['44k'], '-DFILTER_LAWS_MASTER=1', 'tests/test_filter_laws.c',
         '-x', 'none', str(planner), *map(str, objects), '-o', str(binary)]]))
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(jobs)) as pool:
        for f in [pool.submit(compile_steps, name, steps) for name, steps in jobs]:
            f.result()
    with (out / 'compile.log').open('a') as log:
        for name, _ in jobs:
            log.write((out / f'compile-{name}.log').read_text())
    binaries = [out / ('test_filter_laws_' + name) for name in RATES] + [binary]
    procs = [subprocess.Popen([str(b)], cwd=ROOT, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True) for b in binaries]
    text, failed = '', False
    for p in procs:
        text += p.communicate()[0]
        failed |= p.returncode != 0
    (out / 'result.log').write_text(text)
    print(text, end='')
    if failed:
        raise SystemExit(1)
    print('filter laws: all checks passed')

if __name__ == '__main__':
    main()
