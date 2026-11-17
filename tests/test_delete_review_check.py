#!/usr/bin/env python3
import importlib.util
import sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
spec=importlib.util.spec_from_file_location('delete_review_check',root/'tools/fm1_delete_review_check.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
seen=[]
def request(cmd,body,**kw):
    seen.append(cmd)
    if cmd==0xe0:return b'PDL\x01'
    if cmd==0xdf:
        start=body[0];entries=[bytes([1])+b'TEST'+bytes(9)]+[bytes(14)]*5 if start==0 else [bytes(14)]*6
        return bytes([start,0])+b''.join(entries)
    if cmd==0xe1:return body[:16]+bytes(4)+b'TEST'+bytes(9)+bytes([7])*32
    if cmd==0xe3:return body[:16]+bytes(4)
    raise AssertionError('unexpected or destructive command')
result=module.check(request,bytes([1])*16)
assert result['review']=='passed and canceled' and result['delete_commands_sent']==0
assert seen==[0xe0,0xdf,0xdf,0xe1,0xe3,0xdf,0xdf]
print('PASS: read-only device checker never sends a delete command')
