#!/usr/bin/env python3
"""Exercise the real target-project UI settings-lock behavior on host."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
source=root/'tests/run_midi_session_v16.py'
import importlib.util
spec=importlib.util.spec_from_file_location('fm1_flags',source);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
dsp=root/'build/host'
if not (dsp/'braids').exists(): dsp=root.parent/'pattern-only-implementation/build/host'
objects=sorted((dsp/'braids').glob('*.o'))+sorted((dsp/'plaits_reference').glob('*.o'))
if not objects: raise SystemExit('Missing host DSP objects')
out=root/'build/pattern-target-ui-lock2-test';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['cc',*m.FLAGS,'-c','firmware/src/audio_fixed8_plan_unit.c','-o',str(out/'planner.o')],cwd=root,check=True)
subprocess.run(['c++','-x','c',*m.FLAGS,'tests/test_pattern_target_ui_lock2.c','-x','none',str(out/'planner.o'),*map(str,objects),'-o',str(out/'test')],cwd=root,check=True)
subprocess.run([str(out/'test')],cwd=root,check=True)
