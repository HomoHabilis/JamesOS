#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SMK-37 Pro unit tests without the app (host only, never hardware):

    test_smk37_loader.c      the loader's write gate against every 4 KiB sector of the SMK-37 map
                             (real firmware/loader/ldr_core.c, mocked transport and flash)
    test_smk37_punch_pads.c  punch-in pad latching (src/smk37/smk37_punch_pads.h)

Always built for SMK-37 Pro (-DBR_PRODUCT=BR_PRODUCT_ID_SMK37). Include paths cover the
unified layout (hal/smk37/, src/smk37/) and the JamesOS-K layout (hal/, src/).
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import host_target  # noqa: E402

ROOT = HERE.parent
INCLUDES = ['-Ifirmware/hal/smk37', '-Ifirmware/hal', '-Ifirmware/src/smk37', '-Ifirmware/src']
TESTS = [('test_smk37_loader.c', 'SMK-37 loader write gate'),
         ('test_smk37_punch_pads.c', 'SMK-37 punch pads')]


def main():
    flags = host_target.activate('smk37')
    with tempfile.TemporaryDirectory(prefix='smk37-units-') as tmp:
        for name, what in TESTS:
            exe = Path(tmp) / Path(name).stem
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            *flags, *INCLUDES, f'tests/{name}', '-o', str(exe)], cwd=ROOT, check=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
            print(f'{what}: passed', flush=True)
    print('SMK-37 Pro loader and punch-pad unit tests passed')


if __name__ == '__main__':
    main()
