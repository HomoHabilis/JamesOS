#!/usr/bin/env python3
"""Audio overrun and load counters (0x60, 0x61, 0xB2 and the MONITOR window) at both build levels.

Two programs from tests/test_overrun_counter.c, each built with -DFM1_DEVELOPMENT_DIAGNOSTICS=1 and =0:
  platform  the actual audio IRQ and counter functions extracted from firmware/src/platform.c, driven
            with late and over-budget halves through the real render wrapper (fake DMA and app);
  protocol  the actual app command handler (dinghy.c with BR_DIAGNOSTIC_TEST, as run_late_drop_protocol.py)
            with the same extracted counter functions in place of stubs: exact reply bytes, and the
            distribution build refuses the development-only commands.
Never touches hardware."""
from pathlib import Path
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import tempfile
import host_target

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite; the host objects are reused')
ap.add_argument('--diagnostics', choices=('development', 'distribution', 'both'), default='both')
args = ap.parse_args()
levels = ('development', 'distribution') if args.diagnostics == 'both' else (args.diagnostics,)

root = Path(__file__).resolve().parents[1]
platform = host_target.select_product((root / 'firmware/src/platform.c').read_text())


def function(signature):
    start = platform.index(signature + '\n{')
    depth = 0
    for pos in range(platform.index('{', start), len(platform)):
        depth += (platform[pos] == '{') - (platform[pos] == '}')
        if depth == 0:
            return platform[start:pos + 1]
    raise AssertionError('unterminated ' + signature)


# The counters, their snapshot/take/reset functions and the late-drop words: one contiguous region.
start = platform.index('/* Audio load counters in crystal ticks')
counters = platform[start:platform.index('static uint32_t app_audio(int32_t *out, uint32_t n);', start)]
irq = '\n\n'.join(function(s) for s in (
    'static uint32_t platform_audio_render_half(int32_t *out, uint32_t frames)',
    'void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */'))

objects = sorted((root / 'build/host/braids').glob('*.o')) + sorted((root / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Missing host DSP objects; run tools/build_braids_host.sh first')
env = dict(os.environ)   # run_suite.py puts the product compiler shim on PATH
with tempfile.TemporaryDirectory(prefix='fm1-overrun-') as tmp:
    tmp = Path(tmp)
    shutil.copytree(root / 'firmware', tmp / 'firmware', copy_function=os.link)
    shutil.copytree(root / 'assets', tmp / 'assets', copy_function=os.link)
    (tmp / 'tests').mkdir()
    shutil.copy2(root / 'tests/host.c', tmp / 'tests/host.c')
    shutil.copy2(root / 'tests/test_overrun_counter.c', tmp / 'tests/test_overrun_counter.c')
    (tmp / 'tests/overrun_counters.inc').write_text(counters)
    (tmp / 'tests/overrun_irq.inc').write_text(irq)
    # Four independent programs (two per level): build them at the same time, then run them in
    # the old order (platform, protocol for each level).
    builds = []
    for level in levels:
        dev = '1' if level == 'development' else '0'
        exe = tmp / ('platform-' + level)
        builds.append((exe, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                             '-Wno-unused-function', '-Wno-unused-but-set-variable', '-DOVERRUN_PLATFORM=1',
                             '-DFM1_DEVELOPMENT_DIAGNOSTICS=' + dev, '-I', str(tmp / 'tests'),
                             '-I', str(tmp / 'firmware/hal'), str(tmp / 'tests/test_overrun_counter.c'),
                             '-o', str(exe)]))
        exe = tmp / ('protocol-' + level)
        flags = ['-std=c11', '-O2', '-fwrapv', '-Wno-unused-function', '-DOVERRUN_PROTOCOL=1',
                 '-DFM1_DEVELOPMENT_DIAGNOSTICS=' + dev,
                 '-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0', '-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0',
                 '-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1',
                 '-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=0',
                 '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=1',
                 '-DPLAITS_INTERNAL_SAMPLE_RATE=44100', '-DFM1_PLAITS_SOURCE_RATE_32K=0',
                 '-DFM1_PLAITS_SOURCE_RATE_44100=1', '-DFM1_PLAITS_SRC_LAGRANGE4=0',
                 '-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DBR_FX_TAPE_WOW_DELTA_LUT=0'] + host_target.product_flags()
        builds.append((exe, ['c++', '-x', 'c', *flags, '-I' + str(tmp / 'tests'), '-I' + str(tmp / 'firmware/src'),
                             '-I' + str(tmp / 'firmware/gen'), '-o', str(exe),
                             str(tmp / 'tests/test_overrun_counter.c'),
                             str(tmp / 'firmware/src/audio_fixed8_plan_unit.c'), '-x', 'none', *map(str, objects)]))
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(builds)) as pool:
        for f in [pool.submit(subprocess.run, cmd, check=True, env=env) for _, cmd in builds]:
            f.result()
    for exe, _ in builds:
        subprocess.run([str(exe)], check=True)
print('overrun counter: all checks passed (' + ', '.join(levels) + ')')
