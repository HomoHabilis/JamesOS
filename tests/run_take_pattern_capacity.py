#!/usr/bin/env python3
"""Offline capacity measurements using the validation tree's actual codecs."""
from pathlib import Path
import argparse, hashlib, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[1]; src=root
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--take',type=Path,action='append',required=True,help='read-only v16 session capture; repeat for multiple takes')
inputs=parser.parse_args().take
flags=['-std=c11','-O2','-fwrapv','-Wno-unused-function','-DFM1_SHARED_PSC_HOST_TEST=1','-DFM1_NATIVE_OUTPUT_32K=0','-DFM1_NATIVE_OUTPUT_48K=0','-DBR_AUDIO_INTERNAL_BLOCK160=0','-DBR_AUDIO_INTERNAL_BLOCK256=1','-DPLAITS_INTERNAL_SAMPLE_RATE=44100','-DFM1_PLAITS_SOURCE_RATE_32K=0','-DFM1_PLAITS_SOURCE_RATE_44100=1','-DFM1_PLAITS_SOURCE_RATE_38400=0','-DFM1_PLAITS_SOURCE_RATE_48000=0','-DFM1_PLAITS_SRC_LAGRANGE4=0','-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0','-DFM1_PLAITS_PHASE_LUT=0','-DBR_FX_RATE_SIGNED_441_RECIP=0','-DBR_FX_TAPE_WOW_DELTA_LUT=0','-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0','-DBR_PATTERN_BANK_EXPERIMENT=1']
with tempfile.TemporaryDirectory(prefix='fm1-take-capacity-') as d:
 t=Path(d)
 for folder in ('firmware','assets'):shutil.copytree(src/folder,t/folder)
 (t/'tests').mkdir();shutil.copy2(src/'tests/host.c',t/'tests/host.c');shutil.copy2(root/'tests/test_take_pattern_capacity.c',t/'tests/test_take_pattern_capacity.c')
 objs=sorted((src/'build/host/braids').glob('*.o'))+sorted((src/'build/host/plaits_reference').glob('*.o'))
 if not objs:raise SystemExit('Missing host DSP objects')
 exe=t/'test';files=['audio_fixed8_plan_unit.c','pattern_project.c','pattern_bank.c','pattern_storage_codec.c','pattern_song.c','pattern_codec_source.c','pattern_compressed_project_shared.c','pattern_stream_project.c']
 subprocess.run(['c++','-x','c',*flags,'-I'+str(t/'firmware/src'),'-I'+str(t/'firmware/gen'),str(t/'tests/test_take_pattern_capacity.c'),*[str(t/'firmware/src'/f) for f in files],'-x','none',*map(str,objs),'-o',str(exe)],check=True)
 for p in inputs:
  print('input:',p,'sha256:',hashlib.sha256(p.read_bytes()).hexdigest(),flush=True)
  subprocess.run([str(exe),str(p)],check=True)
