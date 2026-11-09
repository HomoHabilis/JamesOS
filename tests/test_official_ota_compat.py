#!/usr/bin/env python3
"""Test the compatibility gate with in-memory flash only."""
import argparse, binascii, pathlib, struct, subprocess, sys, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from validate_package import decrypt
parser=argparse.ArgumentParser();parser.add_argument('--v15',required=True,type=pathlib.Path);parser.add_argument('--flash',required=True,type=pathlib.Path);args=parser.parse_args()
raw=args.v15.read_bytes()
flash=args.flash
crc=lambda x:binascii.crc_hqx(x,0)
def logical(raw):return bytearray(b''.join(raw[i*48:i*48+47] for i in range(20))+raw[960:])
def pack(l):return b''.join(l[i*47:(i+1)*47]+raw[i*48+47:i*48+48] for i in range(20))+l[940:]
def update_checksums(l,entry,off,size):
    struct.pack_into('<H',entry,4,crc(l[off:off+size]));l[64:144]=decrypt(entry)
    h=bytearray(decrypt(l[:64]));count=struct.unpack_from('<H',h,8)[0]
    struct.pack_into('<H',h,2,crc(l[64:64+count*80]));struct.pack_into('<H',h,0,crc(h[2:]));l[:64]=decrypt(h)
with tempfile.TemporaryDirectory() as temp:
    d=pathlib.Path(temp); exe=d/'test'
    subprocess.run(['cc','-std=c11','-O2',str(root/'tests/official_ota_compat_host.c'),'-o',str(exe)],check=True)
    def run(data):
        p=d/'package';p.write_bytes(data)
        return subprocess.run([str(exe),str(p),str(flash)],check=True,capture_output=True,text=True).stdout
    assert 'ota_stage=0 staging_erases=5' in run(raw)
    l=logical(raw);e=bytearray(decrypt(l[64:144]));off,size=struct.unpack_from('<II',e,8)
    l[off+0x10000]^=1
    assert 'ota_stage=-5 staging_erases=0' in run(pack(l))
    update_checksums(l,e,off,size)
    assert 'ota_stage=0 staging_erases=5' in run(pack(l)), 'Valid modified image must not require a pinned ID or hash'
    l=logical(raw);e=bytearray(decrypt(l[64:144]));off,size=struct.unpack_from('<II',e,8)
    l[off+0x4000]^=1
    update_checksums(l,e,off,size)
    assert 'ota_stage=-5 staging_erases=0' in run(pack(l)), 'Invalid encrypted app header must fail'
    l=logical(raw);e=bytearray(decrypt(l[64:144]));off,size=struct.unpack_from('<II',e,8)
    isd=decrypt(l[off+64:off+96]);pos=struct.unpack_from('<I',isd,4)[0]
    l[off+pos+16]=l[off+pos+15]^0xff
    struct.pack_into('<H',l,off+pos+32,crc(l[off+pos:off+pos+32]))
    update_checksums(l,e,off,size)
    assert 'ota_stage=-5 staging_erases=0' in run(pack(l)), 'Different key must fail'
print('V15 and a checksum-consistent variant accepted; corrupt image and mismatched key rejected before staging.')
