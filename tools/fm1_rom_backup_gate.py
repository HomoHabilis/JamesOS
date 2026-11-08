"""Read-only command gate for ROM backup. No USB library or device access.

Permits a pinned RAM loader upload, its entry point, chip queries, and bounded
flash reads only. The gate does not itself enter ROM mode or send commands.
"""
import hashlib

LOADER_ADDRESS=0x01c02000
LOADER_SHA='d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
LOADER_SIZE=24064
FLASH_SIZE=0x100000

def cdb(op,args=b''):
 data=op.to_bytes(2,'big')+args
 if len(data)>16:raise ValueError('oversize command')
 return data+b'\xff'*(16-len(data))

def crc16(buf):
 k=0
 for x in buf:
  k^=x<<8
  for _ in range(8):k=((k<<1)^(0x1021 if k&0x8000 else 0))&0xffff
 return k

def address_data(addr,data):
 return addr.to_bytes(4,'big')+len(data).to_bytes(2,'big')+b'\0'+crc16(data).to_bytes(2,'little')

class ReadOnlyGate:
 def __init__(self,loader):
  if not isinstance(loader,bytes) or len(loader)!=LOADER_SIZE or hashlib.sha256(loader).hexdigest()!=LOADER_SHA:raise ValueError('untrusted RAM loader')
  self.loader=loader;self.chunks=set();self.started=False;self.identified=False
 def check(self,command,out=None,n_in=0):
  if not isinstance(command,bytes) or type(n_in) is not int or n_in<0:raise ValueError('invalid command')
  if command==b'\x12\x00\x00\x00\x24\x00' and out is None and n_in==36:return ('inquiry',None)
  if len(command)!=16:raise ValueError('invalid command length')
  op=int.from_bytes(command[:2],'big')
  if op==0xfb06 and not self.started and isinstance(out,bytes) and n_in==0:
   addr=int.from_bytes(command[2:6],'big');off=addr-LOADER_ADDRESS
   if off<0 or off%512 or off>=len(self.loader):raise ValueError('RAM loader range')
   expected=self.loader[off:off+512]
   if out!=expected or command!=cdb(op,address_data(addr,out)):raise ValueError('RAM loader chunk mismatch')
   return ('chunk',off)
  if op==0xfb08 and out is None and n_in==16 and command==cdb(op,LOADER_ADDRESS.to_bytes(4,'big')+b'\0\1'):
   if self.started or self.chunks!=set(range(0,len(self.loader),512)):raise ValueError('incomplete loader')
   return ('start',None)
  if self.started and out is None and n_in==16:
   if command==cdb(0xfc14):return ('query',None)
   if command==cdb(0xfc09,(0x00ac6900).to_bytes(4,'big')):return ('query',None)
   if command==cdb(0xfc0a):return ('query',None)
  if self.started and self.identified and op==0xfd05 and out is None:
   addr=int.from_bytes(command[2:6],'big');length=int.from_bytes(command[6:8],'big')
   if 1<=length<=16384 and n_in==length and addr<=FLASH_SIZE-length and command==cdb(op,command[2:8]):return ('read',None)
  raise ValueError('command prohibited by read-only ROM gate')
 def completed(self,action):
  if action[0]=='chunk':self.chunks.add(action[1])
  elif action[0]=='start':self.started=True
 def confirm_chip(self,key,device_type,jedec):
  if not self.started or key!=0x980f or (type(device_type) is not int or not 0<=device_type<=255) or jedec not in (0x856014,0xb36014):raise ValueError('unexpected chip identity')
  self.identified=True
