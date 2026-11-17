#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/validate_package.py: the padding between the file directory and the first file must be 0xFF.

    python3 tests/run_validate_package_padding.py [PACKAGE.fwsc ...]

Logical package bytes 224 (end of the two 80-byte directory entries after the 64-byte header) up
to the first file's offset (1024 in every package the tools produce) are covered by no checksum and
never read by the device; the validator now requires them to be 0xFF. Checked on every package
under releases/ plus any given on the command line (e.g. the WP0 references and trial packages):

  - its verdict without mutation is accepted, and its padding is all 0xFF;
  - flipping a padding byte (both edges, the 47-byte block interleave boundary at logical 939/940,
    and points between) is rejected for the padding, while flips of the last directory byte (223)
    and the first file byte (1024) are still rejected for their own checksums.

Host only; reads packages, never writes them. The full differential against the previous
validator (every package accepted before is still accepted) is
a difference test kept outside the tree (it needs packages and the old
validators that are not part of it).
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_package as vp  # noqa: E402

DIR_END, FIRST = 224, 1024
SAMPLE = (224, 225, 300, 511, 700, 938, 939, 940, 941, 1000, 1022, 1023)
FAILED = []


def check(ok, what):
    print(('ok    ' if ok else 'FAIL  ') + what, flush=True)
    if not ok:
        FAILED.append(what)


def raw_offset(logical):
    """the raw file offset of a logical byte: 20 blocks of 47 logical bytes + 1 identity byte"""
    return (logical // 47) * 48 + logical % 47 if logical < 940 else logical + 20


def logical(raw):
    out = bytearray()
    for i in range(20):
        out += raw[i * 48:i * 48 + 47]
    return bytes(out + raw[960:])


def verdict(raw):
    try:
        vp.validate(raw)
        return 'ACCEPT'
    except (ValueError, OSError, struct.error, UnicodeError, KeyError, IndexError) as exc:
        return f'REJECT {exc}'


def main(argv):
    packages = sorted((ROOT / 'releases').glob('*.fwsc')) + [Path(p) for p in argv]
    check(len(packages) >= 8, f'{len(packages)} packages to check (releases/ has 8)')
    for path in packages:
        raw = path.read_bytes()
        lg = logical(raw)
        first = min(struct.unpack_from('<I', vp.decrypt(lg[64 + i * 80:144 + i * 80]), 8)[0] for i in range(2))
        base = verdict(raw)
        expected = 'ACCEPT'
        check(base == expected and first == FIRST and lg[DIR_END:first] == b'\xff' * (first - DIR_END),
              f'{path.name}: {base}; first file at {first}; padding {DIR_END}..{first} all 0xFF')
        for lo in SAMPLE:
            for x in (0x01, 0xFF):
                m = bytearray(raw)
                m[raw_offset(lo)] ^= x
                got = verdict(bytes(m))
                if 'padding between the file directory and the first file' not in got:
                    check(False, f'{path.name}: padding byte {lo} ^0x{x:02X} -> {got}')
        for lo, why in ((223, 'package directory CRC'), (1024, 'payload CRC')):
            m = bytearray(raw)
            m[raw_offset(lo)] ^= 1
            got = verdict(bytes(m))
            check(why in got, f'{path.name}: logical byte {lo} flipped -> {got}')
        print(f'      {path.name}: {len(SAMPLE) * 2} padding flips rejected for the padding', flush=True)
    if FAILED:
        raise SystemExit(f'{len(FAILED)} package padding check(s) failed')
    print(f'validate_package: 0xFF padding required between directory and first file ({len(packages)} packages)')


if __name__ == '__main__':
    main(sys.argv[1:])
