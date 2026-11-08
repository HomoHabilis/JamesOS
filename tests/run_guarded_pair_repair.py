#!/usr/bin/env python3
"""Host-only sanitized guard tests; assert model cannot compile for device."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-guarded-repair-') as tmp:
 binary=str(Path(tmp)/'repair-test')
 flags=['-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DFM1_PATTERN_DEVICE_CANDIDATE=1','-DJOS_HOST_RECOVERY_MODEL=1','-Ifirmware/src']
 subprocess.run(['cc',*flags,'tests/test_guarded_pair_repair.c','tests/recovery/guarded_pair_repair.c','-o',binary],cwd=root,check=True)
 subprocess.run([binary],cwd=root,check=True)
 bad=subprocess.run(['cc','-std=c11','-DJOS_HOST_RECOVERY_MODEL=1','-DFM1_DEVICE=1','-Ifirmware/src','-c','tests/recovery/guarded_pair_repair.c','-o',str(Path(tmp)/'never.o')],cwd=root,capture_output=True,text=True)
 if bad.returncode==0 or 'no qualified hardware backend' not in bad.stderr:raise RuntimeError('device guard failed')
 print('PASS: model cannot compile into device firmware')
