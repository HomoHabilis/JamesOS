#!/usr/bin/env python3
"""Compare original and fast-prefix kit code in one host process."""
from pathlib import Path
import os, re, subprocess, tempfile, argparse
root=Path(__file__).resolve().parents[1];source=root/'firmware/plaits/plaits_kit_c_api.cc'
ap=argparse.ArgumentParser();ap.add_argument('--baseline',type=Path,required=True);baseline=ap.parse_args().baseline.resolve()
flags=['-std=c++14','-O2','-fwrapv','-ffp-contract=off','-DTEST','-DBR_PLAITS_KIT_DEADLINE_TEST=1','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-Dstmlib=plaits_stmlib','-DFM1_NATIVE_OUTPUT_32K=0','-DFM1_NATIVE_OUTPUT_48K=0','-DBR_AUDIO_INTERNAL_BLOCK160=0','-DBR_AUDIO_INTERNAL_BLOCK256=1','-DPLAITS_INTERNAL_SAMPLE_RATE=44100','-DFM1_PLAITS_SOURCE_RATE_32K=0','-DFM1_PLAITS_SOURCE_RATE_44100=1','-DFM1_PLAITS_SOURCE_RATE_38400=0','-DFM1_PLAITS_SOURCE_RATE_48000=0','-DFM1_PLAITS_SRC_LAGRANGE4=0','-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0','-DFM1_PLAITS_PHASE_LUT=0','-Ifirmware/plaits','-Ifirmware/src','-Ifirmware/plaits/upstream','-Ifirmware/braids/upstream']
if os.environ.get('KIT_FP_RELAXED') == '1':
 flags += ['-ffp-contract=fast','-fno-signed-zeros','-fno-trapping-math','-fassociative-math']
objects=sorted((root/'build/host/plaits_reference').glob('*.o'))
objects=[p for p in objects if p.name not in ('plaits_plaits_kit_c_api.cc.o','plaits_plaits_c_api.cc.o')]
objects += sorted((root/'build/host/braids').glob('*.o'))
if len(objects)<30:raise SystemExit('Build DSP objects first with tools/build_braids_host.sh and the 44.1-kHz flags in this script')
names=sorted(set(re.findall(r'\b(plaits_kit_[A-Za-z_0-9]+)\s*\(',source.read_text())))
with tempfile.TemporaryDirectory() as d:
 d=Path(d);ref=d/'ref.o';fast=d/'fast.o'
 subprocess.run(['c++',*flags,'-DBR_PLAITS_KIT_SAFE_PREFIX=1',*[f'-D{x}=ref_{x}' for x in names],'-c',str(baseline),'-o',str(ref)],cwd=root,check=True)
 subprocess.run(['c++',*flags,'-DBR_PLAITS_KIT_SAFE_PREFIX=1','-c',str(source),'-o',str(fast)],cwd=root,check=True)
 subprocess.run(['c++',*flags,str(root/'tests/test_plaits_kit_safe_prefix.cpp'),str(ref),str(fast),*map(str,objects),'-o',str(d/'test')],cwd=root,check=True)
 subprocess.run([str(d/'test')],cwd=root,check=True)
