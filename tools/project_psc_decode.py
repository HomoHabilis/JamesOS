"""Bounded PSC3 decoder for offline migration and inspection."""
import struct
import zlib

def unpack_psc(data):
    if len(data) < 12 or data[:4] != b'PSC\3':
        raise ValueError('invalid PSC3 header')
    size, checksum = struct.unpack_from('<II', data, 4)
    if not 0 < size <= 84480:
        raise ValueError('invalid decoded length')
    out = bytearray()
    at = 12
    while len(out) < size:
        if at >= len(data):
            raise ValueError('truncated token')
        token = data[at]; at += 1
        kind, run = token >> 6, (token & 63) + 1
        if run > size - len(out):
            raise ValueError('run exceeds output')
        if kind == 0:
            if at + run > len(data): raise ValueError('truncated literal')
            out.extend(data[at:at + run]); at += run
        elif kind == 1:
            out.extend(bytes(run))
        elif kind == 2:
            if at >= len(data): raise ValueError('truncated repeat')
            out.extend(bytes([data[at]]) * run); at += 1
        else:
            if at + 2 > len(data): raise ValueError('truncated distance')
            distance = struct.unpack_from('<H', data, at)[0]; at += 2
            if not 0 < distance <= min(1024, len(out)): raise ValueError('invalid distance')
            for _ in range(run): out.append(out[-distance])
    if at != len(data) or zlib.crc32(out) != checksum:
        raise ValueError('PSC3 integrity failure')
    return bytes(out)

