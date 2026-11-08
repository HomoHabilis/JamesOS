"""Offline tests for read-only backup guard and MIDI framing."""
import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fm1_project_full_backup_device import ReadOnlyLink
from fm1_install import pack7,unpack7
class Fake:
 lost=False
 def drain(self):pass
 def send(self,packet):
  raw=unpack7(packet[1:-1]);self.cmd=raw[2];assert self.cmd in (0x9d,0x6e,0xd2)
  if self.cmd==0x9d:body=bytes([1,0,0,1,9,0,0,0,1,0,255,0])
  elif self.cmd==0x6e:body=struct.pack('<8I',7,24,0,0,0,0,0,0)
  else:body=b'x'*32
  if self.cmd==0x9d and getattr(self,'playing',False):body=body[:6]+b'\1'+body[7:]
  response=bytes([0,0x59,self.cmd,len(body),0,0])+body+bytes([~sum(body)&255])
  self.response=b'\xf0'+pack7(response)+b'\xf7';return True
 def read(self,timeout):return self.response
f=Fake();link=ReadOnlyLink(f);link.guard();assert link.request(0xd2,b'\0\0\0\x20')==b'x'*32
try:link.request(0x9d,b'BR\x01\x01\x09\x00');raise AssertionError('panel input accepted')
except ValueError:pass
try:link.request(0x65,b'BR\x01\x00');raise AssertionError('write accepted')
except ValueError:pass
f.playing=True
try:link.guard();raise AssertionError('playing accepted')
except RuntimeError:pass
f.playing=False;f.lost=True
try:link.guard();raise AssertionError('disconnection accepted')
except RuntimeError:pass
print('PASS: backup transport permits read-only commands and rejects playback/disconnect')
