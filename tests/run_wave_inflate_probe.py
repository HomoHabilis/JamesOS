#!/usr/bin/env python3
"""Offline raw-DEFLATE bounded decoder regression; no device access.

The same 120 varied streams and their truncations as always, all fed through one ASan/UBSan
process (tests/test_wave_inflate_probe.cc --batch) instead of one process per stream; a few
streams also go through the single-stream mode, so that path stays checked."""
import random, struct, subprocess, tempfile, zlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
CAP=12000                     # the decoder's input buffer: a longer stream is cut there, as fread did
with tempfile.TemporaryDirectory(prefix='fm1-wave-inflate-') as td:
    binary=Path(td)/'decoder'
    subprocess.run(['clang++','-std=c++14','-O1','-fsanitize=address,undefined',str(root/'tests/test_wave_inflate_probe.cc'),'-o',str(binary)],check=True)
    r=random.Random(9314)
    cases=[]                  # (case, stream, expected decoded bytes or None for "refused")
    for i in range(120):
        n=r.randrange(7757)
        data=(bytes(r.randrange(256) for _ in range(n)) if i%3==0 else bytes(r.randrange(12) for _ in range(n)) if i%3==1 else bytes([r.randrange(256)])*n)
        c=zlib.compressobj(i%10,zlib.DEFLATED,-15); packed=c.compress(data)+c.flush()
        cases.append(((i,n),packed[:CAP],data))
        if len(packed)>1:
            cases.append(((i,'truncated'),packed[:-1][:CAP],None))
    feed=b''.join(struct.pack('<I',len(p))+p for _,p,_ in cases)
    result=subprocess.run([str(binary),'--batch'],input=feed,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert result.returncode==0,(result.returncode,result.stderr[-2000:])
    out,pos=result.stdout,0
    for case,packed,want in cases:
        assert pos+5<=len(out),(case,'no reply',result.stderr[-2000:])
        ok,m=out[pos],struct.unpack_from('<I',out,pos+1)[0]; got=out[pos+5:pos+5+m]; pos+=5+m
        if want is None:
            assert ok==0,(case,'truncated stream decoded')
        else:
            assert ok==1 and got==want,(case,ok,m)
    assert pos==len(out),('trailing output',len(out)-pos)
    for case,packed,want in cases[:3]+cases[-3:]:      # the single-stream mode (one process each)
        result=subprocess.run([binary],input=packed,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if want is None:
            assert result.returncode==2,(case,result.returncode,result.stderr)
        else:
            assert result.returncode==0 and result.stdout==want,(case,result.returncode,result.stderr)
print('PASS: 120 varied streams and truncations under ASan/UBSan')
