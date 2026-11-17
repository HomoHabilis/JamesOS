#!/usr/bin/env python3
"""Build tests/test_midi_clock_control.c (single and -DTEST_SPLIT) and tests/test_midi_clock_diag_disabled.c and run
them in that order; the three programs are built at the same time. Host only."""
from pathlib import Path
import concurrent.futures
import subprocess
from run_midi_clock_engine import FLAGS
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/midi-clock-control-test';out.mkdir(parents=True,exist_ok=True)
objects=sorted((ROOT/'build/host/braids').glob('*.o'))+sorted((ROOT/'build/host/plaits_reference').glob('*.o'))
if not objects: raise RuntimeError('host DSP objects missing')
builds=[]
for split in (False, True):
    args=['c++','-x','c',*FLAGS,'-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0','-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0']
    if split: args += ['-DTEST_SPLIT']
    binary=out/('split' if split else 'single')
    args += ['tests/test_midi_clock_control.c','firmware/src/audio_fixed8_plan_unit.c','-x','none',*map(str,objects),'-o',str(binary)]
    builds.append((binary,args))

binary=out/'disabled'
builds.append((binary,['c++','-x','c',*FLAGS,'-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0',
                '-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0',
                'tests/test_midi_clock_diag_disabled.c','firmware/src/audio_fixed8_plan_unit.c',
                '-x','none',*map(str,objects),'-o',str(binary)]))
with concurrent.futures.ThreadPoolExecutor(max_workers=len(builds)) as pool:
    for f in [pool.submit(subprocess.run,args,cwd=ROOT,check=True) for _,args in builds]:
        f.result()
for binary,_ in builds:
    subprocess.run([str(binary)],cwd=ROOT,check=True)
