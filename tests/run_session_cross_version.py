#!/usr/bin/env python3
"""Compare 1681 diagnostic capture to candidate diagnostic restore; no device."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

candidate = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True, help='1681 source tree with host DSP objects')
baseline = parser.parse_args().baseline.resolve()
flags = ['-std=c11','-O2','-fwrapv','-Wno-unused-function',
         '-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0','-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0',
         '-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1','-DFM1_NATIVE_OUTPUT_32K=0',
         '-DFM1_NATIVE_OUTPUT_48K=0','-DBR_AUDIO_INTERNAL_BLOCK160=0',
         '-DBR_AUDIO_INTERNAL_BLOCK256=1','-DPLAITS_INTERNAL_SAMPLE_RATE=44100',
         '-DFM1_PLAITS_SOURCE_RATE_32K=0','-DFM1_PLAITS_SOURCE_RATE_44100=1',
         '-DFM1_PLAITS_SRC_LAGRANGE4=0','-DBR_FX_RATE_SIGNED_441_RECIP=0',
         '-DBR_FX_TAPE_WOW_DELTA_LUT=0']
with tempfile.TemporaryDirectory(prefix='fm1-session-cross-') as t:
    tmp=Path(t); binaries=[]
    for label,source in [('1681',baseline),('candidate',candidate)]:
        stage=tmp/label;stage.mkdir()
        for directory in ('firmware','assets'):
            shutil.copytree(source/directory,stage/directory)
        (stage/'tests').mkdir()
        for filename in ('host.c','test_session_cross_version.c'):
            shutil.copy2((source if filename=='host.c' else candidate)/'tests'/filename,stage/'tests'/filename)
        objects=sorted((source/'build/host/braids').glob('*.o'))+sorted((source/'build/host/plaits_reference').glob('*.o'))
        if not objects: raise SystemExit(f'Missing host DSP objects in {source}; build them first')
        binary=stage/'test';binaries.append(binary)
        pattern_flags = ['-DCROSS_PATTERN=1','-DFM1_PATTERN_V16_ONLY=1','-DBR_PATTERN_SONG_EXPERIMENT=1','-DBR_PATTERN_BANK_EXPERIMENT=1',
                         '-DBR_PATTERN_OWNER_HOST=1'] if label=='candidate' else []
        pattern_files = [str(stage/'firmware/src'/name) for name in
                         ('pattern_bank.c','pattern_song.c','pattern_storage_codec.c')] if label=='candidate' else []
        subprocess.run(['c++','-x','c',*flags,*pattern_flags,'-I'+str(stage/'firmware/src'),
                        '-I'+str(stage/'firmware/gen'),'-o',str(binary),
                        str(stage/'tests/test_session_cross_version.c'),
                        str(stage/'firmware/src/audio_fixed8_plan_unit.c'),*pattern_files,'-x','none',
                        *map(str,objects)],cwd=stage,check=True)
    fixture=tmp/'1681.bin'
    subprocess.run([str(binaries[0]),'create',str(fixture)],check=True)
    subprocess.run([str(binaries[1]),'verify',str(fixture)],check=True)
    print('PASS: 1681 diagnostic capture → candidate diagnostic restore → identical capture')
