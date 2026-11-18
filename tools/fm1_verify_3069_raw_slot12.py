#!/usr/bin/env python3
"""Read slot 12 from FM-1_9673069 and compare it with a preserved PSC file.

Read-only device commands: identity, raw-export capability/snapshot/chunks and
snapshot close. No project delete, import, firmware update or project write.
Stop playback and leave the instrument idle. Output must be a new file.
"""
import argparse
import hashlib
import os
from pathlib import Path
import secrets
import struct
import sys

from fm1_install import MidoBackend, handshake
from fm1_project_import import ImportLink


def snapshot(request, nonce=None):
    if request(0xe4, b'', attempts=3) != b'PRX\x01':
        raise RuntimeError('device does not advertise raw project export version 1')
    nonce = nonce if nonce is not None else secrets.randbelow(0xffffffff) + 1
    if not 1 <= nonce <= 0xffffffff:
        raise ValueError('nonce must be a nonzero 32-bit integer')
    token = struct.pack('<I', nonce)
    try:
        # Attempt close even if the begin response is lost.
        response = request(0xe4, token + b'\x0b', timeout=15, attempts=3)
        if len(response) < 8 or response[:4] != token:
            raise RuntimeError('invalid raw export response')
        status = struct.unpack_from('<i', response, 4)[0]
        if status:
            if len(response) != 8:
                raise RuntimeError('invalid raw export error response')
            raise RuntimeError('raw export refused (result %d)' % status)
        if len(response) != 55:
            raise RuntimeError('invalid raw export metadata')
        length = int.from_bytes(response[8:10], 'little')
        name, separator, padding = response[10:23].partition(b'\0')
        if not 1 <= length <= 3584 or not separator or not 1 <= len(name) <= 12 or any(c < 32 or c > 126 for c in name) or any(padding):
            raise RuntimeError('invalid raw export metadata')
        payload = bytearray()
        for offset in range(0, length, 32):
            prefix = token + struct.pack('<H', offset)
            part = request(0xdd, prefix, attempts=3)
            if len(part) != 6 + min(32, length-offset) or part[:6] != prefix:
                raise RuntimeError('invalid raw export chunk')
            payload.extend(part[6:])
        if hashlib.sha256(payload).digest() != response[23:55]:
            raise RuntimeError('raw export SHA-256 mismatch')
        return bytes(payload), name.decode('ascii')
    finally:
        try:
            request(0xde, token, attempts=3)
        except (RuntimeError, TimeoutError):
            pass  # Firmware expires an abandoned snapshot after 120 seconds.


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True, help='exact unique FM-1 MIDI input/output name')
    ap.add_argument('--reference', type=Path, required=True, help='preserved slot-12-original.psc')
    ap.add_argument('--output', type=Path, required=True, help='new file for read-back snapshot')
    args = ap.parse_args()
    expected = args.reference.read_bytes()
    if not expected:
        raise ValueError('reference is empty')
    if args.output.exists() or args.output.is_symlink():
        raise FileExistsError('output already exists')
    backend = MidoBackend()
    if backend.mido.get_input_names().count(args.port) != 1 or backend.mido.get_output_names().count(args.port) != 1:
        raise RuntimeError('the exact MIDI input/output port must each occur once')
    link = backend.open(args.port, args.port)
    try:
        ident = handshake(link)
        if not ident or ident.text != 'FM-1_9673069' or ident.model.lower() != 'fm-1':
            raise RuntimeError('selected device is not FM-1_9673069')
        payload, name = snapshot(ImportLink(link).request)
    finally:
        link.close()
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    with os.fdopen(fd, 'wb') as out:
        out.write(payload)
        out.flush()
        os.fsync(out.fileno())
    if args.output.read_bytes() != payload:
        raise RuntimeError('output readback mismatch')
    print('Slot 12: %s; %d bytes; SHA-256 %s' % (name, len(payload), hashlib.sha256(payload).hexdigest()))
    print('Snapshot saved:', args.output)
    if payload != expected:
        print('Reference comparison: DIFFERENT', file=sys.stderr)
        return 1
    print('Reference comparison: identical')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, TimeoutError) as error:
        print('Verification failed:', error, file=sys.stderr)
        sys.exit(1)
