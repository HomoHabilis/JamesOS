#!/usr/bin/env python3
"""Test the sector-only project-sector read against the actual host app handler."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
dsp=root/'build/host'
if not (dsp/'braids').exists(): dsp=root.parent/'pattern-only-implementation/build/host'
objects=sorted((dsp/'braids').glob('*.o'))+sorted((dsp/'plaits_reference').glob('*.o'))
if not objects: raise SystemExit('Missing host DSP objects')
flags=['-DFM1_PATTERN_PANEL_TEST=1','-std=c11','-O2','-fwrapv','-Wno-unused-function','-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0','-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0','-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1','-DFM1_NATIVE_OUTPUT_32K=0','-DFM1_NATIVE_OUTPUT_48K=0','-DBR_AUDIO_INTERNAL_BLOCK160=0','-DBR_AUDIO_INTERNAL_BLOCK256=1','-DPLAITS_INTERNAL_SAMPLE_RATE=44100','-DFM1_PLAITS_SOURCE_RATE_32K=0','-DFM1_PLAITS_SOURCE_RATE_44100=1','-DFM1_PLAITS_SRC_LAGRANGE4=0','-DBR_FX_RATE_SIGNED_441_RECIP=0','-DBR_FX_TAPE_WOW_DELTA_LUT=0','-Ifirmware/src','-Ifirmware/gen']
out=root/'build/pattern-sector-panel-combined-test';out.mkdir(parents=True,exist_ok=True)
for enabled in (0,1):
 binary=out/('test-enabled' if enabled else 'test-disabled')
 subprocess.run(['c++','-x','c',*flags,f'-DFM1_PATTERN_SECTOR_TEST={enabled}','tests/test_pattern_sector_diagnostic.c','firmware/src/audio_fixed8_plan_unit.c','-x','none',*map(str,objects),'-o',str(binary)],cwd=root,check=True)
 subprocess.run([str(binary)],cwd=root,check=True)
