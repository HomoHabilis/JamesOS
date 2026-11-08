"""Derive non-authenticating content fingerprints from a full flash backup.

Neither a nonblank factory sector nor any hash is proof of physical identity,
unique ownership or permission to write. Cloned/changed flash may match/mismatch.
"""
import hashlib

FLASH_SIZE=0x100000
SECTORS={'bluetooth_region':(0xe9000,0xea000),'factory_region':(0xff000,0x100000)}

def identity_hints(image):
 if not isinstance(image,bytes) or len(image)!=FLASH_SIZE:raise ValueError('need one complete 1 MiB flash capture')
 result={}
 for name,(lo,hi) in SECTORS.items():
  part=image[lo:hi]
  if all(b==0xff for b in part) or all(b==0 for b in part):
   result[name]={'usable_hint':False,'sha256':None}
  else:result[name]={'usable_hint':True,'sha256':hashlib.sha256(part).hexdigest()}
 return result
