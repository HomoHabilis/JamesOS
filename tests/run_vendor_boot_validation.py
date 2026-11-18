#!/usr/bin/env python3
"""Validate vendor bank decoding and reject bootloader corruption (host-only)."""
import struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_package as v
p=ROOT/'releases/JamesOS-1.0a-build-2108-from-v15.fwsc'
raw=p.read_bytes(); logical=b''.join(raw[i*48:i*48+47] for i in range(20))+raw[960:]
e=v.decrypt(logical[64:144]);off,size=struct.unpack_from('<II',e,8);flash=logical[off:off+size]
h=b''.join(v.decrypt(flash[i:i+32]) for i in range(0,160,32));entry=v.jlfs(h,32,'head')
plain=v.boot_data(flash,entry)
assert v.crc(plain)==entry['crc'] and len(plain)==entry['size']
for rel in (0, 15, 16, 100, entry['size']-1):
 bad=bytearray(flash);bad[entry['offset']+rel]^=1
 try:v.boot_data(bad,entry)
 except ValueError:pass
 else:raise AssertionError('boot corruption accepted at %d'%rel)
try:v.lz4_block(bytes([0,1,0]),4)
except ValueError:pass
else:raise AssertionError('LZ4 accepted a backreference without history')
assert v.lz4_block(bytes([0,1,0]),4,b'X')==b'XXXX'
print('Vendor encoded boot and cross-block LZ4 validation passed; corruptions rejected')
