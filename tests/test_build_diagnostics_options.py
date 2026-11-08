#!/usr/bin/env python3
"""Distribution builds must not silently acquire optional diagnostic test modes."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
base = [sys.executable, str(root / 'tools/build.py'), '--diagnostics', 'distribution',
        '--pattern-device-candidate', '--app-opt', 'Oz']
for option in ('--runtime-profile', '--pattern-panel-test', '--pattern-sector-test',
               '--pattern-namespace-target-experiment', '--pattern-provision-target-experiment'):
    result = subprocess.run(base + [option], capture_output=True, text=True)
    assert result.returncode == 2, (option, result.returncode, result.stderr)
    assert 'distribution diagnostics cannot be combined' in result.stderr, (option, result.stderr)
for macro in ('FM1_FAULT_C1_TIMEOUT_CAPTURE', 'FM1_FAULT_C1_CONTEXT_CAPTURE',
              'FM1_FAULT_RAM_CODE_CAPTURE_READ'):
    result = subprocess.run(['cc', '-E', '-x', 'c', '-DFM1_DEVELOPMENT_DIAGNOSTICS=0',
                             '-D' + macro + '=1', '-include',
                             str(root / 'firmware/hal/fm1_fault_diag.h'), '-'],
                            input='', capture_output=True, text=True)
    assert result.returncode != 0 and 'Distribution firmware cannot enable' in result.stderr, macro
for macro, header in (('FM1_RAM_IMAGE_CRC_DIAG', 'fm1_ram_image_crc_diag.h'),
                      ('FM1_CPU1_STACK_MARCH_DIAG', 'fm1_cpu1_stack_march.h')):
    result = subprocess.run(['cc', '-E', '-x', 'c', '-DFM1_DEVELOPMENT_DIAGNOSTICS=0',
                             '-D' + macro + '=1', '-include', str(root / 'firmware/hal' / header), '-'],
                            input='', capture_output=True, text=True)
    assert result.returncode != 0 and 'Distribution firmware cannot enable' in result.stderr, macro
print('PASS: distribution diagnostics rejects profiling and experimental diagnostic modes')
