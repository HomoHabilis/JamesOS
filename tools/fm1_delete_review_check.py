#!/usr/bin/env python3
"""Check delete capability and a read-only review on an identified FM-1.

This never sends a delete/commit command, writes a project, or restores a
session. Playback must be stopped and controls left untouched.
"""
import argparse
import secrets
import struct
from fm1_install import MidoBackend, handshake
from fm1_project_import import ImportLink


def check(request, nonce=None):
    if request(0xe0, b'', timeout=3) != b'PDL\x01':
        raise RuntimeError('delete capability is unavailable or invalid')
    pages = [request(0xdf, bytes([start]), timeout=8, attempts=3) for start in (0, 6)]
    present = []
    for start, page in zip((0, 6), pages):
        if len(page) != 86 or page[:2] != bytes((start, 0)):
            raise RuntimeError('could not read saved-slot metadata')
        for i in range(6):
            entry = page[2+i*14:16+i*14]
            if entry[0] == 1:
                name = entry[1:].split(b'\0', 1)[0]
                if not name:
                    raise RuntimeError('invalid saved-slot name')
                present.append((start+i, name))
    if not present:
        return {'capability': True, 'review': 'skipped: no readable saved project'}
    slot, name = present[0]
    token = nonce or secrets.token_bytes(16)
    if len(token) != 16 or not any(token):
        raise ValueError('nonce must contain 16 nonzero-capable random bytes')
    try:
        result = request(0xe1, token+bytes((slot, 0)), timeout=8, attempts=3)
        if len(result) != 65 or result[:16] != token or struct.unpack_from('<i', result, 16)[0] != 0:
            raise RuntimeError('device did not return a successful read-only review')
        if result[20:33].split(b'\0', 1)[0] != name:
            raise RuntimeError('the reviewed project changed; no deletion was sent')
    finally:
        canceled = request(0xe3, token, timeout=3, attempts=3)
        if len(canceled) != 20 or canceled[:16] != token or struct.unpack_from('<i', canceled, 16)[0] != 0:
            raise RuntimeError('could not confirm cancellation of the review')
    after = [request(0xdf, bytes([start]), timeout=8, attempts=3) for start in (0, 6)]
    if after != pages:
        raise RuntimeError('saved-slot metadata changed during the check')
    return {'capability': True, 'review': 'passed and canceled', 'slot': slot+1, 'delete_commands_sent': 0}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True)
    ap.add_argument('--expect', required=True)
    args = ap.parse_args()
    backend = MidoBackend()
    if args.port not in backend.input_names() or args.port not in backend.output_names():
        raise RuntimeError('exact MIDI endpoint not found')
    link = backend.open(args.port, args.port)
    try:
        ident = handshake(link)
        if not ident or ident.text != args.expect or ident.model.lower() != 'fm-1':
            raise RuntimeError('unexpected device or firmware identity')
        print(check(ImportLink(link).request))
    finally:
        link.close()

if __name__ == '__main__':
    main()
