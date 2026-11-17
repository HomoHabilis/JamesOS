#!/usr/bin/env python3
"""Export one saved FM-1 project using export-capable firmware.

Read-only: does not load or change the live project. Requires mido and
python-rtmidi. The output file must not already exist.
"""
import argparse
import hashlib
import secrets
import struct
from pathlib import Path

from fm1_install import MidoBackend, handshake
from fm1_project_import import ImportLink


def transfer(request, slot, nonce=None):
    if not 0 <= slot < 12:
        raise ValueError('slot must be 0–11')
    if request(0xdb, b'', attempts=3) != b'PEX\x01':
        raise RuntimeError('device does not advertise project export version 1')
    if nonce is None:
        nonce = secrets.randbelow(0xffffffff) + 1
    if not 1 <= nonce <= 0xffffffff:
        raise ValueError('nonce must be a nonzero 32-bit integer')
    token = struct.pack('<I', nonce)
    active = False
    try:
        # Treat a lost begin reply as potentially active so cleanup is attempted.
        active = True
        reply = request(0xdc, token + bytes((slot,)), timeout=15, attempts=3)
        if len(reply) < 8 or reply[:4] != token:
            raise RuntimeError('invalid export begin response')
        result = struct.unpack('<i', reply[4:8])[0]
        if result:
            if len(reply) != 8:
                raise RuntimeError('invalid export error response')
            raise RuntimeError('saved slot is empty' if result == 1 else
                               'device rejected export (result %d)' % result)
        if len(reply) != 55:
            raise RuntimeError('invalid export metadata')
        length = int.from_bytes(reply[8:10], 'little')
        raw_name, digest = reply[10:23], reply[23:55]
        name, sep, padding = raw_name.partition(b'\0')
        if (not 12 <= length <= 3584 or not sep or not 1 <= len(name) <= 12
                or any(c < 32 or c > 126 for c in name) or any(padding)):
            raise RuntimeError('invalid export metadata')
        payload = bytearray()
        for off in range(0, length, 32):
            prefix = token + struct.pack('<H', off)
            part = request(0xdd, prefix, attempts=3)
            count = min(32, length - off)
            if len(part) != 6 + count or part[:6] != prefix:
                raise RuntimeError('invalid export chunk')
            payload.extend(part[6:])
        if hashlib.sha256(payload).digest() != digest or not payload.startswith(b'PSC\x03'):
            raise RuntimeError('export digest or project format did not verify')
        return bytes(payload), name.decode('ascii')
    finally:
        if active:
            try:
                request(0xde, token, attempts=3)
            except (TimeoutError, RuntimeError):
                pass  # The device expires abandoned snapshots automatically.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new output PSC3 file')
    parser.add_argument('--slot', type=int, required=True, help='saved slot index, 0–11')
    parser.add_argument('--expect', required=True, help='exact identity of export-capable FM-1 firmware')
    parser.add_argument('--port', required=True, help='exact MIDI input/output endpoint name')
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('output already exists')
    backend = MidoBackend()
    if args.port not in backend.input_names() or args.port not in backend.output_names():
        raise RuntimeError('exact MIDI input/output endpoint not found')
    link = backend.open(args.port, args.port)
    try:
        ident = handshake(link)
        if not ident or ident.text != args.expect or ident.model.lower() != 'fm-1':
            raise RuntimeError('unexpected device or firmware identity')
        payload, name = transfer(ImportLink(link).request, args.slot)
    finally:
        link.close()
    with args.output.open('xb') as output:
        output.write(payload)
    print('Exported %s (%d bytes), SHA-256 %s' % (name, len(payload), hashlib.sha256(payload).hexdigest()), flush=True)


if __name__ == '__main__':
    main()
