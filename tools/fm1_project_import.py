#!/usr/bin/env python3
"""Upload a PSC3 project to a saved FM-1 slot using import-capable firmware.

This writes the selected project slot. It does not install firmware or load the
project into the live instrument. Stop playback and leave controls untouched.
Requires mido and python-rtmidi. Exact firmware identity is required.
"""
import argparse
import hashlib
import secrets
import struct
import time
from pathlib import Path

from fm1_install import MidoBackend, handshake, pack7, unpack7


class ImportLink:
    def __init__(self, link):
        self.link = link

    def request(self, cmd, body, timeout=3, attempts=1):
        raw = bytes((0, 0x59, cmd, len(body) & 255, len(body) >> 8, 0)) + body + bytes((~sum(body) & 255,))
        for _ in range(attempts):
            self.link.drain()
            if not self.link.send(b'\xf0' + pack7(raw) + b'\xf7'):
                raise RuntimeError('MIDI send failed')
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                pkt = self.link.read(deadline - time.monotonic())
                if not pkt:
                    break
                if len(pkt) < 2 or pkt[0] != 0xf0 or pkt[-1] != 0xf7:
                    continue
                data = unpack7(pkt[1:-1])
                if (len(data) >= 7 and data[:3] == bytes((0, 0x59, cmd))
                        and len(data) == int.from_bytes(data[3:6], 'little') + 7
                        and data[-1] == (~sum(data[6:-1]) & 255)):
                    return data[6:-1]
        raise TimeoutError('command %02x timed out' % cmd)


def transfer(request, payload, slot, name, nonce=None):
    if not payload.startswith(b'PSC\x03') or not 12 <= len(payload) <= 3584:
        raise ValueError('expected a PSC3 payload of 12–3584 bytes')
    if not 0 <= slot < 12:
        raise ValueError('slot must be 0–11')
    name = name.encode('ascii')
    if not 1 <= len(name) <= 12 or any(c < 32 or c > 126 for c in name):
        raise ValueError('name must be 1–12 printable ASCII characters')
    nonce = nonce or secrets.randbelow(0xffffffff) + 1
    token = struct.pack('<I', nonce)
    begin = token + bytes((slot,)) + struct.pack('<H', len(payload)) + name.ljust(13, b'\0')
    if request(0xd6, begin, attempts=3) != begin[:7]:
        raise RuntimeError('unexpected begin response')
    for off in range(0, len(payload), 32):
        prefix = token + struct.pack('<H', off)
        if request(0xd7, prefix + payload[off:off + 32], attempts=3) != prefix:
            raise RuntimeError('unexpected chunk response')
    def close_known_result():
        try:
            request(0xda, token, attempts=3)
        except (TimeoutError, RuntimeError):
            pass

    try:
        reply = request(0xd8, token + hashlib.sha256(payload).digest(), timeout=15)
    except TimeoutError:
        # A lost response does not establish whether the flash write occurred.
        # Never resend commit; query the cached completion instead.
        reply = request(0xd9, token, timeout=15, attempts=3)
        if len(reply) != 9 or reply[:4] != token or reply[4] not in (2, 3):
            raise RuntimeError('commit outcome uncertain; inspect device before retrying')
        if reply[4] == 3:
            result = struct.unpack('<i', reply[5:])[0]
            close_known_result()
            raise RuntimeError('device rejected import (result %d)' % result)
        reply = reply[:4] + reply[5:]
    if len(reply) != 8 or reply[:4] != token:
        raise RuntimeError('invalid commit response; outcome uncertain')
    result = struct.unpack('<i', reply[4:])[0]
    if result != 0:
        close_known_result()
        raise RuntimeError('device rejected import (result %d)' % result)
    # Do not turn a successful, verified write into a reported failure if the
    # cleanup response is lost. The device retains the result until abort.
    close_known_result()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', type=Path)
    parser.add_argument('--slot', type=int, required=True, help='saved slot index, 0–11')
    parser.add_argument('--name', required=True, help='project name, up to 12 ASCII characters')
    parser.add_argument('--expect', required=True, help='exact identity of import-capable FM-1 firmware')
    parser.add_argument('--port', required=True, help='exact MIDI input/output endpoint name')
    args = parser.parse_args()
    payload = args.project.read_bytes()
    backend = MidoBackend()
    if args.port not in backend.input_names() or args.port not in backend.output_names():
        raise RuntimeError('exact MIDI input/output endpoint not found')
    link = backend.open(args.port, args.port)
    try:
        ident = handshake(link)
        if not ident or ident.text != args.expect or ident.model.lower() != 'fm-1':
            raise RuntimeError('unexpected device or firmware identity')
        print('Uploading SHA-256 ' + hashlib.sha256(payload).hexdigest(), flush=True)
        transfer(ImportLink(link).request, payload, args.slot, args.name)
        print('Saved and device-readback verified.', flush=True)
    finally:
        link.close()


if __name__ == '__main__':
    main()
