#!/usr/bin/env python3
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from fm1_verify_3069_raw_slot12 import snapshot

class Fake:
    def __init__(self): self.data=b'PSC\x03'+bytes(range(120));self.calls=[];self.corrupt=False;self.cap=True
    def __call__(self,cmd,body,**kwargs):
        self.calls.append((cmd,body))
        if cmd==0xe4 and not body: return b'PRX\x01' if self.cap else b'nope'
        if cmd==0xe4:
            assert body[4]==11
            return body[:4]+struct.pack('<iH',0,len(self.data))+b'OLD SONG\0'.ljust(13,b'\0')+hashlib.sha256(self.data).digest()
        if cmd==0xdd:
            off=int.from_bytes(body[4:6],'little'); data=self.data[off:off+32]
            if self.corrupt: data=bytes([data[0]^1])+data[1:]
            return body+data
        if cmd==0xde:return body
        raise AssertionError('unexpected command')
class Tests(unittest.TestCase):
    def test_snapshot(self):
        f=Fake(); self.assertEqual(snapshot(f,42),(f.data,'OLD SONG'));self.assertEqual(f.calls[-1][0],0xde);self.assertTrue(all(cmd in (0xe4,0xdd,0xde) for cmd,_ in f.calls))
    def test_digest_rejects_and_closes(self):
        f=Fake();f.corrupt=True
        with self.assertRaisesRegex(RuntimeError,'SHA-256'):snapshot(f,42)
        self.assertEqual(f.calls[-1][0],0xde)
    def test_missing_capability(self):
        f=Fake();f.cap=False
        with self.assertRaisesRegex(RuntimeError,'advertise'):snapshot(f,42)
        self.assertEqual([cmd for cmd,_ in f.calls],[0xe4])
if __name__=='__main__':unittest.main()
