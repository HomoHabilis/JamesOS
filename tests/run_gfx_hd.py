#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and run tests/test_gfx_hd.c (the HD anti-aliased shapes) on the host.

    python3 tests/run_gfx_hd.py [OUTDIR] [--size]

Writes OUTDIR/gfx_hd.png (240x240, rendered by six 40-row strips) and
OUTDIR/gfx_hd_4x.png (nearest-neighbour 4x, for looking at). --size also
prints a rough code-size estimate of the shapes (clang -Os, 32-bit x86 and
Thumb-2; the FM-1 compiler is different, so treat it as an estimate)."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ)
FLAGS = ['-std=c11', '-O2', '-fwrapv', '-Wall', '-Wextra', '-Wno-unused-function']


def extract_bw_sine(dst):
    """Copy the firmware's sine table and bw_sine verbatim, so arcs use the real table."""
    src = (ROOT / 'firmware/src/instrument_widgets.c').read_text()
    m = re.search(r'static const int16_t bw_sine_quarter\[.*?\nstatic int32_t bw_sine\(uint32_t phase\)\n\{.*?\n\}\n', src, re.S)
    if not m:
        raise SystemExit('bw_sine not found in instrument_widgets.c')
    dst.write_text(m.group(0))


def to_png(ppm, png, scale):
    from PIL import Image
    im = Image.open(ppm)
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(png)


SIZE_TU = r'''
#include <stdint.h>
void lcd_sync(void);
void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p);
#include "gfx.c"
#include "bw_sine.inc"
volatile int32_t v[8];
void use(void)
{
    cv_begin((uint32_t)v[0], (uint32_t)v[1], (uint16_t)v[2]);
    cv_oy = v[3];
    cv_rect(v[0], v[1], v[2], v[3], (uint16_t)v[4]);
    v[5] = bw_sine((uint32_t)v[6]);
#if USE_HD
    int32_t px[4] = {v[0], v[1], v[2], v[3]}, py[4] = {v[4], v[5], v[6], v[7]};
    hd_line(v[0], v[1], v[2], v[3], (uint16_t)v[4]);
    hd_line_q8(v[0], v[1], v[2], v[3], (uint32_t)v[5], (uint16_t)v[4]);
    hd_circle(v[0], v[1], v[2], (uint16_t)v[4], (uint32_t)v[3]);
    hd_circle_q8(v[0], v[1], v[2], v[3], (uint16_t)v[4]);
    hd_arc(v[0], v[1], (uint32_t)v[2], (uint32_t)v[3], (uint32_t)v[5], (uint32_t)v[6], (uint16_t)v[4]);
    hd_glow_dot(v[0], v[1], v[2], (uint16_t)v[4]);
    hd_rrect(v[0], v[1], v[2], v[3], v[5], (uint16_t)v[4]);
    hd_rrect_grad(v[0], v[1], v[2], v[3], v[5], (uint16_t)v[4], (uint16_t)v[6]);
    hd_rrect_frame(v[0], v[1], v[2], v[3], v[5], (uint16_t)v[4]);
    hd_bevel(v[0], v[1], v[2], v[3], v[5], (uint16_t)v[4]);
    hd_poly4(px, py, (uint16_t)v[4]);
    v[7] = hd_lighten((uint16_t)v[0], (uint32_t)v[1]) + hd_darken((uint16_t)v[2], (uint32_t)v[3]);
#endif
    cv_blit(0, 0);
}
'''


def size_estimate(work):
    tu = work / 'size_tu.c'
    tu.write_text(SIZE_TU)
    targets = [('x86 -m32', ['-m32', '-ffreestanding']), ('thumbv7m', ['--target=thumbv7m-none-eabi', '-mthumb', '-ffreestanding'])]
    for name, tflags in targets:
        sizes = []
        for use in (0, 1):
            obj = work / ('size_%d.o' % use)
            r = subprocess.run(['clang', '-std=c11', '-Os', '-fwrapv', '-w', *tflags, '-DDINGHY_HOST',
                                '-DUSE_HD=%d' % use, '-I', str(work), '-I', str(ROOT / 'firmware/src'),
                                '-c', str(tu), '-o', str(obj)], env=ENV, capture_output=True, text=True)
            if r.returncode:
                print('size (%s): could not compile: %s' % (name, r.stderr.strip().splitlines()[-1:]))
                break
            out = subprocess.run(['size', '-A', str(obj)], capture_output=True, text=True).stdout
            text = sum(int(l.split()[1]) for l in out.splitlines() if l.startswith(('.text', '.rodata')))
            sizes.append(text)
        if len(sizes) == 2:
            print('size (%s, -Os): HD shapes add %d bytes of .text+.rodata (includes ~12 call sites)'
                  % (name, sizes[1] - sizes[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?', default=str(ROOT / 'build/gfx-hd'))
    ap.add_argument('--size', action='store_true')
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'build/gfx-hd'; work.mkdir(parents=True, exist_ok=True)
    extract_bw_sine(work / 'bw_sine.inc')
    binary = work / 'test_gfx_hd'
    subprocess.run(['cc', *FLAGS, '-I', str(work), '-I', str(ROOT / 'firmware/src'),
                    str(ROOT / 'tests/test_gfx_hd.c'), '-o', str(binary)], env=ENV, check=True)
    ppm = out / 'gfx_hd.ppm'
    r = subprocess.run([str(binary), str(ppm)], capture_output=True, text=True)
    print(r.stdout + r.stderr, end='')
    if ppm.exists():
        try:
            to_png(ppm, out / 'gfx_hd.png', 1)
            to_png(ppm, out / 'gfx_hd_4x.png', 4)
            print('wrote', out / 'gfx_hd.png', 'and', out / 'gfx_hd_4x.png')
        except ImportError:
            print('PIL not available; image left as', ppm)
    if a.size:
        size_estimate(work)
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
