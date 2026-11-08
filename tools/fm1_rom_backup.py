#!/usr/bin/env python3
"""Read-only full-flash backup after the unit is already in ROM mode.

No automatic restart, no flash erase/program, and no project restore. This
requires prior consent to restart and discard unsaved RAM, a separately
verified unit, and a physical power cycle afterwards. The loader runs in RAM.
"""
import argparse,hashlib,importlib.util,os
from pathlib import Path
from fm1_project_full_backup import pack_backup,write_new
from fm1_rom_backup_gate import ReadOnlyGate,FLASH_SIZE
from fm1_device_fingerprint import identity_hints

UPSTREAM_SHA='7f1a6bb994878fe561a1eb7ada9b5e544cc7c869e99de58cff4490145e035c7c'

def load_reference(path,libusb):
 path=Path(path)
 if hashlib.sha256(path.read_bytes()).hexdigest()!=UPSTREAM_SHA:raise ValueError('recovery reference source differs from reviewed version')
 spec=importlib.util.spec_from_file_location('fm1_recovery_reference',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.LIBUSB=(str(Path(libusb)),)
 return module

def protected_class(base):
 class Protected(base):
  def execute(self,command,out=None,n_in=0):
   action=self.readonly_gate.check(command,out,n_in)
   reply=super().execute(command,out,n_in)
   self.readonly_gate.completed(action)
   return reply
 return Protected

def collect(f):
 first=f.read(0,FLASH_SIZE,'Reading 1/2')
 second=f.read(0,FLASH_SIZE,'Reading 2/2')
 if not isinstance(first,bytes) or len(first)!=FLASH_SIZE or first!=second:raise ValueError('full flash reads failed or differed')
 return first

def write_once(path,data):
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o600)
 with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
 with open(path,'rb') as f:
  if f.read()!=data:raise ValueError('backup readback differs')

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--reference',type=Path,required=True,help='reviewed fm1_recover.py')
 ap.add_argument('--libusb',type=Path,required=True)
 ap.add_argument('--output-dir',type=Path,required=True,help='new private directory')
 ap.add_argument('--already-in-rom',action='store_true',required=True)
 a=ap.parse_args()
 upstream=load_reference(a.reference,a.libusb)
 loader=(a.reference.parent/'loader'/upstream.LOADER[1]).read_bytes()
 gate=ReadOnlyGate(loader)
 usb=upstream.usb_module();backend=upstream.backend()
 devices=list(usb.core.find(find_all=True,idVendor=upstream.VID,idProduct=upstream.PID,backend=backend))
 if len(devices)!=1:raise ValueError('expected exactly one ROM device')
 expected=(devices[0].bus,devices[0].address)
 cls=protected_class(upstream.Fm1);f=cls.__new__(cls);f.readonly_gate=gate
 try:
  cls.__init__(f,wait=0)
  if (f.dev.bus,f.dev.address)!=expected:raise ValueError('ROM device changed during connect')
  if f.inquiry()!=('WL82','UBOOT1.00'):raise ValueError('unexpected ROM inquiry')
  f.start_loader();key,kind,jedec=f.info();gate.confirm_chip(key,kind,jedec)
  data=collect(f)
  bank=data[0x97000:0xb1000];archive=pack_backup(bank,'ROM-WL82-flash-%06X'%jedec)
  a.output_dir.mkdir(mode=0o700,parents=False,exist_ok=False)
  write_once(a.output_dir/'full-flash.bin',data);write_new(a.output_dir/'projects.josbank',archive)
  dfd=os.open(a.output_dir,os.O_RDONLY)
  try:os.fsync(dfd)
  finally:os.close(dfd)
  print('Read-only full-flash and saved-project backups verified:',a.output_dir)
  print('Full-flash SHA-256:',hashlib.sha256(data).hexdigest())
  hint=identity_hints(data)['factory_region']
  print('Factory-sector content hint:',hint['sha256'] if hint['usable_hint'] else 'unavailable (blank or uniform)', '(not authenticated device identity)')
 finally:
  if getattr(f,'dev',None) is not None:usb.util.dispose_resources(f.dev)
if __name__=='__main__':main()
