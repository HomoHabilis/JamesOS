#!/usr/bin/env python3
from pathlib import Path
import subprocess
from run_bank import FLAGS
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/midi-app-regression';out.mkdir(parents=True,exist_ok=True)
objs=sorted((ROOT/'build/host/braids').glob('*.o'))+sorted((ROOT/'build/host/plaits_reference').glob('*.o'))
if not objs: raise RuntimeError('host DSP objects missing')
subprocess.run(['cc',*FLAGS,'-c','firmware/src/audio_fixed8_plan_unit.c','-o',str(out/'planner.o')],cwd=ROOT,check=True)
subprocess.run(['c++','-x','c',*FLAGS,'tests/test_midi_app_regression.c','-x','none',str(out/'planner.o'),*map(str,objs),'-o',str(out/'test')],cwd=ROOT,check=True)
subprocess.run([str(out/'test')],cwd=ROOT,check=True)
