import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fm1_device_fingerprint import *
blank=b'\xff'*FLASH_SIZE
h=identity_hints(blank);assert not h['factory_region']['usable_hint'] and not h['bluetooth_region']['usable_hint']
a=bytearray(blank);a[0xff005]=5;a[0xe900a]=9
h=identity_hints(bytes(a));assert h['factory_region']['usable_hint'] and h['bluetooth_region']['usable_hint']
# Clones have the same hints: these never prove which physical unit is present.
assert h==identity_hints(bytes(a))
a[0xff005]=6;assert h['factory_region']['sha256']!=identity_hints(bytes(a))['factory_region']['sha256']
try:identity_hints(b'bad');raise AssertionError('partial image accepted')
except ValueError:pass
print('PASS: blank regions rejected as identity hints; clone and changes handled without treating hashes as authentication')
