from pathlib import Path
import importlib.util
import struct

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fm1_export', root / 'tools/fm1_project_sector_export.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
flash = bytearray((i * 17 + 3) & 255 for i in range(26 * 4096))
calls = []

def read(cmd, body):
    assert cmd == 0xD2 and len(body) == 4
    sector, offset, count = struct.unpack('<BHB', body)
    calls.append((sector, offset, count))
    return bytes(flash[sector * 4096 + offset:sector * 4096 + offset + count])

result = module.read_pair_twice(read, 12)
assert result == bytes(flash[24 * 4096:26 * 4096])
assert len(calls) == 512 and calls[0] == (24, 0, 32) and calls[-1] == (25, 4064, 32)

calls.clear()
def changing(cmd, body):
    if len(calls) == 256:
        flash[24 * 4096] ^= 1
    return read(cmd, body)
try:
    module.read_pair_twice(changing, 12)
    raise AssertionError('changed pair was accepted')
except ValueError as exc:
    assert 'changed' in str(exc)

for bad in (-1, 13):
    try:
        module.read_pair_twice(read, bad)
        raise AssertionError('invalid slot was accepted')
    except ValueError:
        pass
try:
    module.read_pair_twice(lambda cmd, body: b'bad', 12)
    raise AssertionError('short response was accepted')
except ValueError:
    pass
print('double-read sector export validates ranges, lengths and changed data')
