#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the tests that assert one product's flash, loader and update constants, for the
selected product (BR_HOST_TARGET or --target; default fm1). Host only, never hardware.

    test_flash_ids.c                    JEDEC IDs the storage/update gate accepts
    test_loader_restart_path.py         the loader's completion branch (reboot paths)
    test_project_update_preservation.py update-record cleanup keeps the project sectors
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import host_target  # noqa: E402

ROOT = HERE.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--target', choices=host_target.TARGETS)
    a = ap.parse_args()
    if a.target:
        host_target.activate(a.target)
    target = host_target.target()
    with tempfile.TemporaryDirectory(prefix='product-tests-') as tmp:
        exe = Path(tmp) / 'test_flash_ids'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', *host_target.product_flags(target),
                        str(HERE / 'test_flash_ids.c'), '-o', str(exe)], cwd=ROOT, check=True)
        subprocess.run([str(exe)], check=True)
        print('JEDEC IDs accepted by the storage/update gate: passed', flush=True)
    for test in ('test_loader_restart_path.py', 'test_project_update_preservation.py'):
        subprocess.run([sys.executable, str(HERE / test)], cwd=ROOT, check=True)
    print(f'{"SMK-37 Pro" if target == "smk37" else "FM-1"} flash IDs, loader restart path and '
          'update-record cleanup: passed')


if __name__ == '__main__':
    main()
