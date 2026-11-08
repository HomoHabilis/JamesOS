#!/usr/bin/env python3
"""Exercise the experimental v16-only session gate on the host."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
objects=sorted((ROOT/'build/host/braids').glob('*.o'))+sorted((ROOT/'build/host/plaits_reference').glob('*.o'))
if not objects: raise SystemExit('Missing host DSP objects; build them before running')
flags=['-std=c11','-O2','-fwrapv','-Wno-unused-function','-DFM1_NATIVE_OUTPUT_32K=0','-DFM1_NATIVE_OUTPUT_48K=0','-DBR_AUDIO_INTERNAL_BLOCK160=0','-DBR_AUDIO_INTERNAL_BLOCK256=1','-DPLAITS_INTERNAL_SAMPLE_RATE=44100','-DFM1_PLAITS_SOURCE_RATE_32K=0','-DFM1_PLAITS_SOURCE_RATE_44100=1','-DFM1_PLAITS_SOURCE_RATE_38400=0','-DFM1_PLAITS_SOURCE_RATE_48000=0','-DFM1_PLAITS_SRC_LAGRANGE4=0','-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0','-DFM1_PLAITS_PHASE_LUT=0','-DBR_FX_RATE_SIGNED_441_RECIP=0','-DBR_FX_TAPE_WOW_DELTA_LUT=0','-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0','-Ifirmware/src','-Ifirmware/gen']
out=ROOT/'build/pattern-v16-only-test';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['cc',*flags,'-c','firmware/src/audio_fixed8_plan_unit.c','-o',str(out/'planner.o')],cwd=ROOT,check=True)
subprocess.run(['c++','-x','c',*flags,'tests/test_pattern_v16_only.c','-x','none',str(out/'planner.o'),*map(str,objects),'-o',str(out/'test')],cwd=ROOT,check=True)
subprocess.run([str(out/'test')],cwd=ROOT,check=True)
