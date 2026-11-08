#!/usr/bin/env python3
"""Export the saved project partition through sector-test firmware. Read only.

Stop playback, release notes, allow effect tails to decay, and do not touch any
controls for the entire capture. This exports committed flash, not unsaved RAM.
It never installs firmware, changes settings, saves, loads, or restores projects.
"""
import argparse,struct,time
from pathlib import Path
from fm1_install import MidoBackend,handshake,pack7,unpack7
from fm1_project_full_backup import capture_partition,pack_backup,write_new

class ReadOnlyLink:
 def __init__(self,link): self.link=link
 def request(self,cmd,body=b''):
  if cmd not in (0x9d,0x6e,0xd2) or (cmd in (0x9d,0x6e) and body):raise ValueError('command is not a read-only backup request')
  raw=bytes([0,0x59,cmd,len(body)&255,len(body)>>8,0])+body+bytes([~sum(body)&255])
  self.link.drain()
  if not self.link.send(b'\xf0'+pack7(raw)+b'\xf7'): raise RuntimeError('MIDI send failed')
  deadline=time.monotonic()+3
  while time.monotonic()<deadline:
   packet=self.link.read(deadline-time.monotonic())
   if not packet:break
   data=unpack7(packet[1:-1])
   if len(data)>=7 and data[:3]==bytes([0,0x59,cmd]) and len(data)==int.from_bytes(data[3:6],'little')+7 and data[-1]==(~sum(data[6:-1])&255):return data[6:-1]
  raise TimeoutError('read-only diagnostic %02x timed out'%cmd)
 def guard(self):
  if self.link.lost:raise RuntimeError('device disconnected')
  u=self.request(0x9d);q=self.request(0x6e)
  if len(u)!=12 or u[0]!=1 or u[1] or u[5] or u[6] or u[7] or u[9]:raise RuntimeError('project menu, recording, playback or project operation active')
  if len(q)!=32 or (struct.unpack_from('<I',q,4)[0]&31)!=24:raise RuntimeError('device is not stopped and quiet')

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--expect',required=True,help='exact identity of the read-only backup firmware');ap.add_argument('--output',type=Path,required=True)
 a=ap.parse_args();backend=MidoBackend();names=[n for n in backend.input_names() if any(s in n.lower() for s in ('jamesos','felucca','fm-1'))]
 if len(names)!=1 or names[0] not in backend.output_names():raise RuntimeError('expected exactly one FM-1 MIDI endpoint')
 link=backend.open(names[0],names[0])
 try:
  ident=handshake(link)
  if not ident or ident.text!=a.expect:raise RuntimeError('unexpected firmware; backup diagnostic may not exist')
  r=ReadOnlyLink(link);r.guard();print('Reading all 26 project sectors twice; leave controls untouched.',flush=True)
  data=capture_partition(r.request,r.guard);r.guard();write_new(a.output,pack_backup(data,ident.text))
  print('Verified full saved partition backup:',a.output,flush=True)
 finally:link.close()
if __name__=='__main__':main()
