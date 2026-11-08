#!/usr/bin/env python3
from pathlib import Path
import subprocess
from run_midi_clock_engine import FLAGS
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/midi-clock-diag-test';out.mkdir(parents=True,exist_ok=True)
objects=sorted((ROOT/'build/host/braids').glob('*.o'))+sorted((ROOT/'build/host/plaits_reference').glob('*.o'))
if not objects: raise RuntimeError('host DSP objects missing')
for split in (False, True):
    args=['c++','-x','c',*FLAGS,'-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0','-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0']
    if split: args += ['-DTEST_SPLIT']
    binary=out/('split' if split else 'single')
    args += ['tests/test_midi_clock_control.c','firmware/src/audio_fixed8_plan_unit.c','-x','none',*map(str,objects),'-o',str(binary)]
    subprocess.run(args,cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True)

binary=out/'disabled'
subprocess.run(['c++','-x','c',*FLAGS,'-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0',
                '-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0',
                'tests/test_midi_clock_diag_disabled.c','firmware/src/audio_fixed8_plan_unit.c',
                '-x','none',*map(str,objects),'-o',str(binary)],cwd=ROOT,check=True)
subprocess.run([str(binary)],cwd=ROOT,check=True)
