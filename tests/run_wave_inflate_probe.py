#!/usr/bin/env python3
"""Offline raw-DEFLATE bounded decoder regression; no device access."""
import random, subprocess, tempfile, zlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-wave-inflate-') as td:
    binary=Path(td)/'decoder'
    subprocess.run(['clang++','-std=c++14','-O1','-fsanitize=address,undefined',str(root/'tests/test_wave_inflate_probe.cc'),'-o',str(binary)],check=True)
    r=random.Random(9314)
    for i in range(120):
        n=r.randrange(7757)
        data=(bytes(r.randrange(256) for _ in range(n)) if i%3==0 else bytes(r.randrange(12) for _ in range(n)) if i%3==1 else bytes([r.randrange(256)])*n)
        c=zlib.compressobj(i%10,zlib.DEFLATED,-15); packed=c.compress(data)+c.flush()
        result=subprocess.run([binary],input=packed,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        assert result.returncode==0 and result.stdout==data,(i,n,result.returncode,result.stderr)
        if len(packed)>1:
            result=subprocess.run([binary],input=packed[:-1],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            assert result.returncode==2,(i,result.returncode,result.stderr)
print('PASS: 120 varied streams and truncations under ASan/UBSan')
