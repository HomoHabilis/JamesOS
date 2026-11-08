#!/usr/bin/env python3
"""Generate cropped 16-color indexed RGB565 RLE from the 240x240 RGBA PNG.

Uses only Python's standard library. Transparent pixels are composited over black.
Run from any directory; writes firmware/gen/jamesos_start_rle.inc.
"""
from pathlib import Path
import struct
import zlib
from collections import Counter

ROOT = Path(__file__).resolve().parents[1]

def read_png(path):
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    offset = 8
    compressed = bytearray()
    header = None
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + size]
        if len(chunk) != size or offset + size + 12 > len(data):
            raise ValueError('truncated PNG chunk')
        checksum = struct.unpack_from('>I', data, offset + 8 + size)[0]
        if zlib.crc32(kind + chunk) & 0xffffffff != checksum:
            raise ValueError('PNG checksum mismatch')
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            compressed.extend(chunk)
        elif kind == b'IEND':
            break
        offset += size + 12
    if header != (240, 240, 8, 6, 0, 0, 0):
        raise ValueError('expected 240x240, 8-bit non-interlaced RGBA PNG')
    raw = zlib.decompress(compressed)
    stride = 240 * 4
    if len(raw) != (stride + 1) * 240:
        raise ValueError('unexpected decompressed image size')
    previous = bytearray(stride)
    pixels = []
    for y in range(240):
        rowtype = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if rowtype > 4:
            raise ValueError('unknown PNG filter')
        for i in range(stride):
            a = row[i - 4] if i >= 4 else 0
            b = previous[i]
            c = previous[i - 4] if i >= 4 else 0
            if rowtype == 1:
                pred = a
            elif rowtype == 2:
                pred = b
            elif rowtype == 3:
                pred = (a + b) // 2
            elif rowtype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
            else:
                pred = 0
            row[i] = (row[i] + pred) & 255
        for i in range(0, stride, 4):
            r, g, b, alpha = row[i:i + 4]
            r, g, b = ((v * alpha + 127) // 255 for v in (r, g, b))
            pixels.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        previous = row
    return pixels

def encode(pixels):
    palette = []
    indices = {}
    for value in pixels:
        if not 0 <= value <= 0xffff:
            raise ValueError('invalid RGB565 value')
        if value not in indices:
            if len(palette) == 256:
                raise ValueError('more than 256 colors')
            indices[value] = len(palette)
            palette.append(value)
    short_runs = len(palette) <= 32
    packed = bytearray()
    at = 0
    while at < len(pixels):
        value = pixels[at]
        end = at + 1
        while end < len(pixels) and pixels[end] == value and end - at < 255:
            end += 1
        count, index = end - at, indices[value]
        if short_runs:
            packed.append((index << 3) | (count if count <= 7 else 0))
            if count > 7:
                packed.append(count)
        else:
            packed.extend((count, index))
        at = end
    return palette, packed, short_runs

def quantize16(pixels):
    counts = Counter(pixels)
    colors = set(counts)
    mapping = {v: v for v in colors}
    def rgb(v):
        return ((v >> 11) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31)
    def distance(a, b):
        x, y = rgb(a), rgb(b)
        return sum((x[i] - y[i]) ** 2 for i in range(3))
    while len(colors) > 16:
        _, old, new = min((sum(counts[v] * distance(v, b) for v in mapping if mapping[v] == a), a, b)
                          for a in colors if a not in (0, 0xf77f) for b in colors if a != b)
        for value in mapping:
            if mapping[value] == old: mapping[value] = new
        colors.remove(old)
    return [mapping[v] for v in pixels]

def encode16(pixels):
    palette = list(dict.fromkeys(pixels))
    if len(palette) > 16: raise ValueError('more than 16 colors')
    indices = {v: i for i, v in enumerate(palette)}
    packed = bytearray(); at = 0
    while at < len(pixels):
        end = at + 1
        while end < len(pixels) and pixels[end] == pixels[at] and end - at < 255: end += 1
        count = end - at
        packed.append((indices[pixels[at]] << 4) | (count if count <= 15 else 0))
        if count > 15: packed.append(count)
        at = end
    return palette, packed

def main():
    pixels = read_png(ROOT / 'assets/jamesos-start.png')
    rows = [y for y in range(240) if any(pixels[y*240:(y+1)*240])]
    top, bottom = min(rows), max(rows) + 1
    palette, packed = encode16(quantize16(pixels[top*240:bottom*240]))
    lines = ['/* Generated by tools/generate_startup_art.py; cropped 16-color RGB565 runs. */',
             '#define BR_STARTUP_ART_CROPPED 1u',
             '#define BR_STARTUP_ART_TOP %du' % top,
             '#define BR_STARTUP_ART_BOTTOM %du' % bottom,
             'static const uint16_t br_startup_art_palette[] = {']
    lines += ['    ' + ', '.join('0x%04x' % x for x in palette[i:i + 12]) + ','
              for i in range(0, len(palette), 12)]
    lines += ['};', 'static const uint8_t br_startup_art_rle[] = {']
    lines += ['    ' + ', '.join('0x%02x' % x for x in packed[i:i + 18]) + ','
              for i in range(0, len(packed), 18)]
    lines.append('};')
    target = ROOT / 'firmware/gen/jamesos_start_rle.inc'
    target.write_text('\n'.join(lines) + '\n')
    print('%d RGB565 pixels, %d colors, %d bytes including palette -> %s' %
          ((bottom - top) * 240, len(palette), len(packed) + 2 * len(palette), target))

if __name__ == '__main__':
    main()
