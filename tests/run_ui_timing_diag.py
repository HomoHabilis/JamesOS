#!/usr/bin/env python3
"""UI timing diagnostic (BR_UI_TIMING_DIAG; firmware/src/ui_timing_diag.c, commands 0xB5..0xB8) through
the actual app command handler and main-loop tick with the device SAVE menu (tests/test_ui_timing_diag.c):

  * define on: remote buttons and encoders change pages through the panel's input path, a long HOME
    press toggles HOLD, the SAVE page refuses (and strips, in a race) everything but HOME and encoder
    turns, REC + SAVE never quick-saves, and the counters move plausibly;
  * define off: the four commands are refused, and the program has no diagnostic symbol (br_uit*,
    fm1_input_diag*), as the define-on program does.

    python3 tests/run_ui_timing_diag.py [--reuse-dsp]

Never touches hardware."""
from pathlib import Path
import argparse
import subprocess
import host_target

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite; the host DSP objects are reused')
ap.parse_args()

root = Path(__file__).resolve().parents[1]
objects = sorted((root / 'build/host/braids').glob('*.o')) + sorted((root / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Missing host DSP objects; run tools/build_braids_host.sh first')
flags = ['-std=c11', '-O2', '-fwrapv', '-Wno-unused-function', '-DFM1_DEVELOPMENT_DIAGNOSTICS=0',
         '-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0', '-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0',
         '-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1', '-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=0',
         '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=1',
         '-DPLAITS_INTERNAL_SAMPLE_RATE=44100', '-DFM1_PLAITS_SOURCE_RATE_32K=0',
         '-DFM1_PLAITS_SOURCE_RATE_44100=1', '-DFM1_PLAITS_SRC_LAGRANGE4=0',
         '-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DBR_FX_TAPE_WOW_DELTA_LUT=0',
         '-Ifirmware/src', '-Ifirmware/gen'] + host_target.product_flags()
out = root / 'build/ui-timing-diag-test'
out.mkdir(parents=True, exist_ok=True)
symbols = {}
for enabled in (1, 0):
    binary = out / ('test-on' if enabled else 'test-off')
    subprocess.run(['c++', '-x', 'c', *flags, '-DBR_UI_TIMING_DIAG=%d' % enabled, 'tests/test_ui_timing_diag.c',
                    'firmware/src/audio_fixed8_plan_unit.c', '-x', 'none', *map(str, objects), '-o', str(binary)],
                   cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
    names = subprocess.run(['nm', str(binary)], capture_output=True, text=True, check=True).stdout
    symbols[enabled] = sorted({l.split()[-1] for l in names.splitlines()
                               if l.split() and ('br_uit' in l.split()[-1] or 'fm1_input_diag' in l.split()[-1])})
if not symbols[1]:
    raise SystemExit('FAIL: the define-on program shows no br_uit symbol (the symbol check would prove nothing)')
if symbols[0]:
    raise SystemExit('FAIL: diagnostic symbols in the define-off program: %s' % ', '.join(symbols[0]))
print('ui timing diag: all checks passed (on: remote pages, SAVE guard, counters; off: refused, no symbols)')
