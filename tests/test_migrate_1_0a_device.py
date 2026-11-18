#!/usr/bin/env python3
"""The migration device flow addresses only the chosen port and refuses wrong firmware."""
import contextlib, io, sys, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import migrate_1_0a_device as m
class Link:
 def __init__(self): self.closed=False
 def close(self): self.closed=True
class Backend:
 @property
 def mido(self): return self
 def get_input_names(self): return self.input_names()
 def get_output_names(self): return self.output_names()
 def input_names(self): return ['FM-1 JamesOS','FM-1']
 def output_names(self): return ['FM-1 JamesOS','FM-1']
 def open(self,i,o): assert i==o=='FM-1 JamesOS'; return link
class Reader:
 def __init__(self,obj): assert obj is link
 def guard(self): pass
 def request(self,*args): raise AssertionError('capture is mocked')
class Identity:
 model='FM-1'; text='FM-1_9673046'
link=Link();m.MidoBackend=Backend;m.ReadOnlyLink=Reader
m.handshake=lambda _:Identity(); m.capture_partition=lambda req,guard:b'partition'
m.pack_backup=lambda p,i:b'archive';m.write_new=lambda p,b:p.write_bytes(b)
m.migrate=lambda backup,out: {'slots':[{'slot':1,'name':'OLD SONG','status':'converted'}],'status':'complete'}
with tempfile.TemporaryDirectory() as tmp:
 sys.argv=['migrate_1_0a_device.py','--port','FM-1 JamesOS','--output',str(Path(tmp)/'out')]
 with contextlib.redirect_stdout(io.StringIO()):m.main()
 assert link.closed and (Path(tmp)/'out'/'Original 1.0a backup.josbank').read_bytes()==b'archive'
 link=Link();m.migrate=lambda *args: (_ for _ in ()).throw(AssertionError('backup-only converted'))
 sys.argv=['migrate_1_0a_device.py','--port','FM-1 JamesOS','--output',str(Path(tmp)/'backup-only'),'--backup-only']
 with contextlib.redirect_stdout(io.StringIO()):m.main()
 assert link.closed and (Path(tmp)/'backup-only'/'Original 1.0a backup.josbank').read_bytes()==b'archive'
 link=Link();m.handshake=lambda _:type('Bad',(),{'model':'FM-1','text':'FM-1_9673061'})()
 sys.argv=['migrate_1_0a_device.py','--port','FM-1 JamesOS','--output',str(Path(tmp)/'wrong')]
 try:m.main();raise AssertionError('wrong firmware accepted')
 except RuntimeError:pass
 assert link.closed and not (Path(tmp)/'wrong').exists()
 class Duplicate(Backend):
  def get_input_names(self): return ['FM-1 JamesOS','FM-1 JamesOS']
 m.MidoBackend=Duplicate;m.handshake=lambda _:Identity();link=Link()
 sys.argv=['migrate_1_0a_device.py','--port','FM-1 JamesOS','--output',str(Path(tmp)/'duplicate')]
 try:m.main();raise AssertionError('duplicate endpoint accepted')
 except RuntimeError:pass
 assert not link.closed and not (Path(tmp)/'duplicate').exists()
print('1.0a migration: targeted port and firmware guard passed')
