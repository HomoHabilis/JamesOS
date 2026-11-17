#!/usr/bin/env python3
"""Extract one project payload from a verified .josbank backup; never accesses a device."""
import argparse
import os
import struct
import sys
import zlib
from pathlib import Path
from fm1_project_full_backup import unpack_backup, PAIR_SIZE, SECTOR_SIZE

def word(b, offset):
    return struct.unpack_from('<I', b, offset)[0]

def inspect(sector, slot, copy):
    if sector == b'\xff' * SECTOR_SIZE:
        return None
    h = sector[:256]
    size, generation = word(h, 32), word(h, 28)
    if (h[:4] != b'PPS2' or word(h, 20) != slot or word(h, 24) != copy or
            not generation or not 0 < size <= 3584 or h[52:56] != bytes(4) or
            word(h, 88) != zlib.crc32(h[:88]) or h[92:96] != b'PPC2' or
            h[56:88] != b'\xff' * 32 or h[96:] != b'\xff' * 160 or
            h[4:20] in (bytes(16), b'\xff' * 16)):
        raise ValueError('Project sector header is invalid')
    payload = sector[256:256 + size]
    if zlib.crc32(payload) != word(h, 36) or sector[256 + size:] != b'\xff' * (SECTOR_SIZE - 256 - size):
        raise ValueError('Project sector data is invalid')
    name = h[40:53].split(b'\0', 1)[0].decode('ascii')
    if not name or any(ord(c) < 32 or ord(c) > 126 for c in name):
        raise ValueError('Project name is invalid')
    return generation, h[4:20], name, payload

def extract(blob, slot):
    _, partition = unpack_backup(blob)
    if not 0 <= slot <= 12:
        raise ValueError('Slot index must be 0–12')
    pair = partition[slot * PAIR_SIZE:(slot + 1) * PAIR_SIZE]
    valid, errors = [], []
    for copy in range(2):
        try:
            item = inspect(pair[copy * SECTOR_SIZE:(copy + 1) * SECTOR_SIZE], slot, copy)
            if item: valid.append(item)
        except (ValueError, UnicodeError) as e:
            errors.append(e)
    if not valid:
        raise ValueError('No verified project found in this slot')
    if len(valid) == 2:
        a, b = valid
        if a[1] != b[1] or abs(a[0] - b[0]) != 1:
            raise ValueError('Project copies have ambiguous identity or generation')
        valid.sort(key=lambda item: item[0], reverse=True)
    return valid[0], bool(errors)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('backup', type=Path)
    ap.add_argument('slot', type=int, help='saved slot number, 1–12')
    ap.add_argument('output', type=Path, help='new PSC3 output file')
    args = ap.parse_args()
    try:
        if not 1 <= args.slot <= 12: raise ValueError('Slot must be 1–12')
        (generation, _, name, payload), bad_copy = extract(args.backup.read_bytes(), args.slot - 1)
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
        with os.fdopen(fd, 'wb') as f:
            f.write(payload)
        print(f'Extracted slot {args.slot}, {name}, generation {generation} to {args.output}.')
        if bad_copy:
            print('The other copy was invalid; original backup remains available.')
    except (OSError, ValueError) as e:
        print(f'Extraction failed: {e}', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
