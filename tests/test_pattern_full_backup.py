"""Host-only archive, consistency and fault tests; never opens a device."""
from pathlib import Path
import importlib.util,struct,tempfile
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('backup',root/'tools/fm1_project_full_backup.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
flash=bytearray((i*17+3)&255 for i in range(b.SIZE));calls=0;guards=0
def guard():
 global guards; guards+=1
def read(cmd,body):
 global calls
 assert cmd==0xd2
 sector,offset,count=struct.unpack('<BHB',body);assert sector<26 and offset+count<=4096 and count==32
 calls+=1;return bytes(flash[sector*4096+offset:sector*4096+offset+count])
image=b.capture_partition(read,guard);assert image==bytes(flash) and calls==2*26*128 and guards==54
blob=b.pack_backup(image,'FM-1_9261956');meta,decoded=b.unpack_backup(blob)
assert decoded==image and len(meta['pairs'])==13
for cut in [0,7,11,12,len(blob)-1]:
 try:b.unpack_backup(blob[:cut]);raise AssertionError('truncation accepted')
 except ValueError:pass
for i in [12,len(blob)//2,len(blob)-1]:
 damaged=bytearray(blob);damaged[i]^=1
 try:b.unpack_backup(bytes(damaged));raise AssertionError('corruption accepted')
 except ValueError:pass
calls=0
def changed(cmd,body):
 if calls==26*128:flash[0]^=1
 return read(cmd,body)
try:b.capture_partition(changed,guard);raise AssertionError('changed partition accepted')
except ValueError:pass
try:b.capture_partition(lambda c,d:b'x',guard);raise AssertionError('short chunk accepted')
except ValueError:pass
with tempfile.TemporaryDirectory() as d:
 path=Path(d)/'backup.josbank';b.write_new(path,blob);assert path.read_bytes()==blob
 try:b.write_new(path,blob);raise AssertionError('overwrite accepted')
 except FileExistsError:pass
print('PASS: full 26-sector double-pass backup, all 13 pairs, archive integrity and no-clobber')
