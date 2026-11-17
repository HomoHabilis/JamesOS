#!/usr/bin/env python3
import hashlib
import struct
import unittest
from fm1_project_export import transfer


class Fake:
    def __init__(self):
        self.data = b'PSC\x03' + bytes(range(61))
        self.closed = False
        self.corrupt = False
        self.cap = b'PEX\x01'
        self.result = 0
    def __call__(self, cmd, body, **kw):
        if cmd == 0xdb:
            return self.cap
        if cmd == 0xdc:
            token = body[:4]
            if self.result:
                return token + struct.pack('<i', self.result)
            return token + struct.pack('<iH', 0, len(self.data)) + b'Test\0'.ljust(13, b'\0') + hashlib.sha256(self.data).digest()
        if cmd == 0xdd:
            off = int.from_bytes(body[4:6], 'little')
            part = self.data[off:off+32]
            if self.corrupt: part = bytes([part[0] ^ 1]) + part[1:]
            return body + part
        if cmd == 0xde:
            self.closed = True
            return body
        raise AssertionError(cmd)


class ExportTest(unittest.TestCase):
    def test_success(self):
        f = Fake(); payload, name = transfer(f, 4, 123)
        self.assertEqual((payload, name), (f.data, 'Test')); self.assertTrue(f.closed)
    def test_corruption_closes(self):
        f = Fake(); f.corrupt = True
        with self.assertRaisesRegex(RuntimeError, 'digest'): transfer(f, 0, 123)
        self.assertTrue(f.closed)
    def test_empty_closes(self):
        f = Fake(); f.result = 1
        with self.assertRaisesRegex(RuntimeError, 'empty'): transfer(f, 0, 123)
        self.assertTrue(f.closed)
    def test_capability_and_slot(self):
        f = Fake(); f.cap = b'bad'
        with self.assertRaisesRegex(RuntimeError, 'advertise'): transfer(f, 0, 123)
        with self.assertRaises(ValueError): transfer(f, 12, 123)

if __name__ == '__main__': unittest.main()
