#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host model of the SPI flash busy wait (firmware/hal/fm1_flash.h) on a simulated slow part;
never touches hardware.

The driver is the device code with its SFR primitives redirected (FL_HOST_MODEL). Runs, in
every suite configuration:
  1. FM-1 product: unit cases (erase 18 ms .. stuck, page 1 ms .. stuck, TIMER4 wrap) and the
     real callers end to end: project SAVE (instrument_project_device.c + project_store.c),
     update staging (ota.c), the loader's ldr_erase / ldr_prog (extracted from loader.c) and
     the pattern target raw callbacks (extracted from dinghy.c), each with 800 ms erases and
     with a hard-failed erase.
  2. SMK-37 Pro product: the unit cases with the SMK-37 app write window.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import host_target  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
CC = ['cc', '-std=c11', '-O1', '-fwrapv', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
      '-Wno-unused-parameter', '-Wno-int-to-pointer-cast', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
      '-Ifirmware/src', '-Ifirmware/hal']


def extract(source, prefix):
    start = source.index(prefix)
    brace = source.index('{', start)
    depth = 0
    for i in range(brace, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if not depth:
            return source[start:i + 1]
    raise ValueError(prefix)


def main():
    loader = host_target.select_product((ROOT / 'firmware/loader/loader.c').read_text(), 'fm1')
    dinghy = host_target.select_product((ROOT / 'firmware/src/dinghy.c').read_text(), 'fm1')
    parts = ['#define FLW_HAVE_LOADER 1',
             extract(loader, 'static int ldr_erase(uint32_t off)'),
             extract(loader, 'static int ldr_prog(uint32_t off, const void *p, uint32_t n)'),
             '#define FLW_HAVE_RAW 1']
    parts += [extract(dinghy, p) for p in ('static int br_ps_target_read_raw(',
                                           'static int br_ps_target_reserve_sector(',
                                           'static int br_ps_target_program_raw(',
                                           'static int br_ps_target_erase_raw(')]
    body = '\n'.join(parts)
    # the raw callbacks must reach the driver's wrappers, not something else
    assert re.search(r'fl_erase4k\(', body) and re.search(r'fl_write\(', body)
    env = dict(os.environ, BR_HOST_CFLAGS='')        # the product is chosen below, per build
    with tempfile.TemporaryDirectory(prefix='fm1-flash-wait-') as td:
        d = Path(td)
        (d / 'flw_extracted.inc').write_text(body)
        builds = [('fm1', ['-DBR_PRODUCT=BR_PRODUCT_ID_FM1', '-DFLW_E2E=1', f'-I{d}']),
                  ('smk37', ['-DBR_PRODUCT=BR_PRODUCT_ID_SMK37', '-DFLW_RANGE_SMK37=1'])]
        for name, flags in builds:
            exe = d / f'flw-{name}'
            subprocess.run([*CC, *flags, 'tests/test_flash_wait.c', '-o', str(exe)],
                           cwd=ROOT, env=env, check=True)
            print(f'--- {name}', flush=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
    print('flash wait: all checks passed (FM-1 units + callers, SMK-37 Pro units)')


if __name__ == '__main__':
    main()
