#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce the local V15 install package from two independently held inputs.

This is the test-specific combined boot/app image, not a universal installer.
It writes the boot head and requires hardware/recovery qualification.
"""
import argparse
import binascii
import hashlib
import struct
from pathlib import Path
from fm1pkg_make import ufw, chipkey_decode
from validate_package import decrypt

APP_SHA = '267b9b58a47e60cd6675ffb494adfdbeacd1a76e8e36a678522a8b875d25f89e'
V14_SHA = '7e7b6d260163cdf9d716f7fab44f19be600edeffcd390de74370b83c917fd0c6'
OUTPUT_SHA = 'a54bb59fcb4e2a932bb1a4bb613971a1174e50c828a7bc69e94d72a23ae813ad'

def read_files(path, expected):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected: raise ValueError('Input hash differs from the tested package')
    if len(raw) < 960: raise ValueError('Input too short')
    data = b''.join(raw[i*48:i*48+47] for i in range(20)) + raw[960:]
    h = decrypt(data[:64]); crc=lambda d:binascii.crc_hqx(d,0)
    count=struct.unpack_from('<H',h,8)[0]
    if crc(h[2:]) != struct.unpack_from('<H',h)[0] or count != 2: raise ValueError('Invalid package header')
    if crc(data[64:64+80*count]) != struct.unpack_from('<H',h,2)[0]: raise ValueError('Invalid directory CRC')
    result={}
    for i in range(count):
        e=decrypt(data[64+i*80:144+i*80]); kind,_,checksum,_,off,size,_=struct.unpack_from('<HHHHIII',e)
        part=data[off:off+size]
        if len(part)!=size or crc(part)!=checksum or kind in result: raise ValueError('Invalid file entry')
        result[kind]=part
    if set(result)!={0,100}: raise ValueError('Unexpected package contents')
    return result

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--app',required=True,type=Path);ap.add_argument('--v14',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);a=ap.parse_args()
    app=read_files(a.app,APP_SHA); boot=read_files(a.v14,V14_SHA)
    head = bytearray(boot[0][:0x4000])
    for offset, value in ((0x39a7, 0x7b), (0x39a8, 0x9a), (0x39a9, 0x17), (0x39f6, 0x0e), (0x3a02, 0x04)):
        head[offset] = value
    entry = bytearray(decrypt(head[64:96]))
    pos, size = struct.unpack_from('<II', entry, 4)
    if pos + size > len(head): raise ValueError('Unexpected boot entry')
    struct.pack_into('<H', entry, 2, binascii.crc_hqx(head[pos:pos + size], 0))
    struct.pack_into('<H', entry, 0, binascii.crc_hqx(entry[2:], 0))
    head[64:96] = decrypt(entry)
    flash=bytes(head)+app[0][0x4000:]
    if len(flash)!=0x93000: raise ValueError('Unexpected flash image size')
    entry=decrypt(flash[64:96]); pos=struct.unpack_from('<I',entry,4)[0]
    if pos>0x4000-32 or chipkey_decode(flash[pos:pos+32])!=0x980f: raise ValueError('Unexpected boot key')
    result=ufw(flash,boot[100],'FM-1_9262103')
    if hashlib.sha256(result).hexdigest()!=OUTPUT_SHA: raise ValueError('Output does not match hardware-tested package')
    a.output.write_bytes(result);print(OUTPUT_SHA)

if __name__=='__main__': main()
