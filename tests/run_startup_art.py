#!/usr/bin/env python3
"""Verify the startup framebuffer against the reduced-color PNG and make a preview."""
import argparse
import hashlib
import re
from array import array
from pathlib import Path
import struct
import subprocess
import sys
import zlib
from run_ui_polish import FLAGS

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from generate_startup_art import read_png, encode, quantize16, encode16

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

def decode_stream(palette, data, pixel_count, short_runs):
    if len(palette) > (32 if short_runs else 256):
        raise ValueError('palette too large')
    decoded = []
    offset = 0
    while offset < len(data):
        first = data[offset]; offset += 1
        if short_runs:
            index, count = first >> 3, first & 7
            if not count:
                if offset == len(data): raise ValueError('truncated extended run')
                count = data[offset]; offset += 1
                if count < 8: raise ValueError('invalid extended run')
        else:
            count = first
            if offset == len(data): raise ValueError('truncated indexed run')
            index = data[offset]; offset += 1
        if not count or index >= len(palette) or count > pixel_count - len(decoded):
            raise ValueError('run or palette index out of bounds')
        decoded.extend([palette[index]] * count)
    if len(decoded) != pixel_count: raise ValueError('incorrect pixel count')
    return decoded

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--reuse-dsp', action='store_true')
    ap.add_argument('--preview', type=Path)
    args = ap.parse_args()
    out = ROOT / 'build/startup-art-test'
    out.mkdir(parents=True, exist_ok=True)
    if not args.reuse_dsp:
        subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT, check=True)
    objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
    objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise RuntimeError('host DSP objects missing')
    planner = out / 'planner.o'
    with (out / 'compile.log').open('w') as log:
        subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        binary = out / 'test_startup_art'
        subprocess.run(['c++', '-x', 'c', *FLAGS, 'tests/test_startup_art.c', '-x', 'none', str(planner), *map(str, objects), '-o', str(binary)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    raw = out / 'frame.rgb565'
    subprocess.run([str(binary), str(raw)], cwd=ROOT, check=True)
    pixels = array('H'); pixels.frombytes(raw.read_bytes())
    assert len(pixels) == 240 * 240
    original = read_png(ROOT / 'assets/jamesos-start.png')
    top, bottom = 91, 149
    expected = [0] * (top*240) + quantize16(original[top*240:bottom*240]) + [0] * ((240-bottom)*240)
    assert sum(a != b for a, b in zip(original, expected)) == 276
    for y in range(240):
        for x in range(240):
            if pixels[y * 240 + x] != expected[y * 240 + x]:
                raise AssertionError('frame differs at (%d,%d)' % (x, y))
    # Verify the committed generated asset is deterministic without modifying it.
    include = (ROOT / 'firmware/gen/jamesos_start_rle.inc').read_text()
    def values(name):
        match = re.search(r'\b'+name+r'\[\]\s*=\s*\{([^}]*)\}', include, re.S)
        assert match, name
        return [int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+', match.group(1))]
    palette = values('br_startup_art_palette')
    actual = bytes(values('br_startup_art_rle'))
    expected_palette, expected_data = encode16(expected[top*240:bottom*240])
    assert palette == expected_palette and actual == expected_data and len(palette) == 16
    decoded_crop = []
    offset = 0
    while offset < len(actual):
        first = actual[offset]; offset += 1
        index, count = first >> 4, first & 15
        if not count:
            count = actual[offset]; offset += 1
            assert count >= 16
        assert index < len(palette) and count <= (bottom-top)*240-len(decoded_crop)
        decoded_crop.extend([palette[index]]*count)
    assert decoded_crop == expected[top*240:bottom*240]
    decoded = [0]*(top*240) + decoded_crop + [0]*((240-bottom)*240)
    for boundary in range(240 * 40, 240 * 240, 240 * 40):
        assert decoded[boundary-1:boundary+1] == expected[boundary-1:boundary+1]
    for run in (0,1,7,8,9,254,255,256,510,511):
        colors, data, short = encode([0x1234] * run)
        assert colors == ([0x1234] if run else []) and short
        assert decode_stream(colors, data, run, short) == [0x1234] * run
    # Exhaust every count/index encoding for both formats.
    for count in range(1,256):
        for index in range(256):
            if index < 32:
                packed = bytes(((index << 3) | (count if count <= 7 else 0),)) + (bytes((count,)) if count > 7 else b'')
                assert decode_stream(list(range(32)), packed, count, True) == [index] * count
            assert decode_stream(list(range(256)), bytes((count,index)), count, False) == [index] * count
    for ncolors in range(1,257):
        colors, data, short = encode(list(range(ncolors)))
        assert colors == list(range(ncolors)) and short == (ncolors <= 32)
        assert decode_stream(colors,data,ncolors,short) == colors
    for bad,total,short in ((b'\x00',1,True),(b'\x00\x00',0,True),
                            (b'\x00\x07',7,True),(b'\xff',7,True),
                            (b'\x01',1,False),(b'\x00\x00',0,False),
                            (b'\x01\xff',1,False),(b'\x02\x00',1,False),
                            (b'\x01\x00',2,False)):
        try: decode_stream(palette,bad,total,short)
        except ValueError: pass
        else: raise AssertionError('malformed stream accepted')
    for invalid in ([-1], [65536], list(range(257))):
        try: encode(invalid)
        except ValueError: pass
        else: raise AssertionError('invalid palette input accepted')
    print('startup: expected 16-color frame, run/index bounds and strip boundaries verified; %d bytes including palette' % (len(actual)+2*len(palette)))
    if args.preview:
        scanlines = bytearray()
        for y in range(240):
            scanlines.append(0)
            for c in pixels[y * 240:(y + 1) * 240]:
                r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
                scanlines.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 240, 240, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b'')
        args.preview.write_bytes(png)
        print('preview:', args.preview)

if __name__ == '__main__':
    main()
