#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and run the HD text test (tests/test_hd_text.c) on the host.

    python3 tests/run_hd_text.py [--out DIR] [--zoom 4]

Checks that firmware/src/james_font_hd.h is what tools/gen_hd_font.py
generates, builds with cc, runs, and writes the specimen sheet as DIR/hd_text_specimen.png
(native) and DIR/hd_text_specimen_x4.png (nearest-neighbour upscale).
Never touches the device or the shared build folders."""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def ppm_to_png(ppm, png, zoom):
    """the specimen preview; a convenience only (Pillow), never part of the verdict"""
    try:
        from PIL import Image
    except ImportError:
        print('preview: Pillow is not installed; the specimen stays a PPM:', ppm)
        return False
    im = Image.open(ppm)
    im.save(png)
    if zoom > 1:
        im.resize((im.width * zoom, im.height * zoom), Image.NEAREST).save(png.with_name(png.stem + '_x%d.png' % zoom))
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', default=str(ROOT / 'build/hd-text'))
    ap.add_argument('--zoom', type=int, default=4)
    ap.add_argument('--skip-gen-check', action='store_true')
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ok = True
    if not a.skip_gen_check:
        r = subprocess.run([sys.executable, str(ROOT / 'tools/gen_hd_font.py'), '--check'], cwd=ROOT,
                           capture_output=True, text=True)
        print(r.stdout, end='')
        if r.returncode:
            print(r.stderr, end='')
            print('FAIL: firmware/src/james_font_hd.h is not what tools/gen_hd_font.py generates')
            ok = False
    binary = out / 'test_hd_text'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-undefined-internal',
                    '-I', str(ROOT / 'firmware/src'),
                    str(ROOT / 'tests/test_hd_text.c'), '-o', str(binary)], cwd=ROOT, check=True)
    ppm = out / 'hd_text_specimen.ppm'
    r = subprocess.run([str(binary), str(ppm)], cwd=ROOT)
    if ppm.exists() and ppm_to_png(ppm, out / 'hd_text_specimen.png', a.zoom):
        print('specimen:', out / 'hd_text_specimen.png')
    return 0 if ok and r.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
