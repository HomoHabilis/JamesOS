#!/usr/bin/env python3
"""Exercise the project transfer protocol without hardware."""
import hashlib
import struct
import unittest
from fm1_project_import import transfer


class Fake:
    def __init__(self, commit_timeout=False, state=2, result=0):
        self.payload = bytearray(73)
        self.chunks = []
        self.commit_timeout = commit_timeout
        self.state = state
        self.result = result
        self.calls = []

    def request(self, cmd, body, **kwargs):
        self.calls.append(cmd)
        token = body[:4]
        if cmd == 0xd6:
            return body[:7]
        if cmd == 0xd7:
            off = struct.unpack('<H', body[4:6])[0]
            self.chunks.append(off)
            self.payload[off:off+len(body[6:])] = body[6:]
            return body[:6]
        if cmd == 0xd8:
            if body[4:] != hashlib.sha256(self.payload).digest():
                raise AssertionError('wrong payload digest')
            if self.commit_timeout:
                raise TimeoutError()
            return token + struct.pack('<i', self.result)
        if cmd == 0xd9:
            return token + bytes([self.state]) + struct.pack('<i', self.result)
        return token


class TestTransfer(unittest.TestCase):
    def test_chunks_and_verified_commit(self):
        payload = b'PSC\x03' + bytes(range(69))
        fake = Fake()
        transfer(fake.request, payload, 4, 'SUNDAY', nonce=123)
        self.assertEqual(fake.chunks, [0, 32, 64])
        self.assertEqual(fake.payload, payload)
        self.assertEqual(fake.calls[-2:], [0xd8, 0xda])

    def test_lost_commit_reply_queries_status_without_recommit(self):
        fake = Fake(commit_timeout=True)
        transfer(fake.request, b'PSC\x03' + bytes(69), 0, 'SONG', nonce=123)
        self.assertEqual(fake.calls.count(0xd8), 1)
        self.assertEqual(fake.calls[-2:], [0xd9, 0xda])

    def test_uncertain_commit_never_retried_or_aborted(self):
        fake = Fake(commit_timeout=True, state=1)
        with self.assertRaisesRegex(RuntimeError, 'uncertain'):
            transfer(fake.request, b'PSC\x03' + bytes(69), 0, 'SONG', nonce=123)
        self.assertEqual(fake.calls.count(0xd8), 1)
        self.assertNotIn(0xda, fake.calls)

    def test_failed_commit_status_is_not_success(self):
        fake = Fake(commit_timeout=True, state=3, result=-2)
        with self.assertRaisesRegex(RuntimeError, 'rejected'):
            transfer(fake.request, b'PSC\x03' + bytes(69), 0, 'SONG', nonce=123)
        self.assertEqual(fake.calls[-2:], [0xd9, 0xda])

    def test_direct_failed_commit_releases_session(self):
        fake = Fake(result=-2)
        with self.assertRaisesRegex(RuntimeError, 'result -2'):
            transfer(fake.request, b'PSC\x03' + bytes(69), 0, 'SONG', nonce=123)
        self.assertEqual(fake.calls[-2:], [0xd8, 0xda])

    def test_bad_input_before_sending(self):
        fake = Fake()
        with self.assertRaises(ValueError):
            transfer(fake.request, b'bad', 0, 'SONG')
        self.assertEqual(fake.calls, [])


if __name__ == '__main__':
    unittest.main()
