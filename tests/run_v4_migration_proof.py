#!/usr/bin/env python3
"""Offline proof using an archived v16/v4 project fixture; never accesses a device."""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
u32 = lambda b, i: struct.unpack_from('<I', b, i)[0]

sys.path.insert(0, str(ROOT / 'tools'))
from project_psc_decode import unpack_psc

def main():
    image = (ROOT / 'tests/fixtures/project-v16-store.bin').read_bytes()
    header = image[:256]
    length = u32(header, 32)
    assert header[:4] == b'PPS2' and 0 < length <= 3584
    assert u32(header, 88) == zlib.crc32(header[:88])
    payload = image[256:256 + length]
    assert u32(header, 36) == zlib.crc32(payload)
    old = unpack_psc(payload)
    assert old[:8] == b'FMP1\x04\x00\x14\x00' and u32(old, 8) == len(old)
    check = bytearray(old); check[12:16] = bytes(4)
    assert u32(old, 12) == zlib.crc32(check)
    assert old[20:24] == b'1DRB' and old[27] == 16
    with tempfile.TemporaryDirectory(prefix='jamesos-v4-proof-') as tmp:
        tmp = Path(tmp)
        source = tmp / 'session.v16'
        source.write_bytes(old[20:3504])
        subprocess.run([sys.executable, 'tools/session_v16_to_v17.py', '--out', str(tmp),
                        '--build-dir', str(tmp / 'build'),
                        str(source)], cwd=ROOT, check=True)
        outputs = list(tmp.glob('*.v17.bin'))
        assert len(outputs) == 1
        session = outputs[0].read_bytes()
        assert len(session) == 3584 and session[4] == 17
        header = bytearray(24); header[:20] = old[:20]
        struct.pack_into('<HH', header, 4, 5, 24)
        struct.pack_into('<H', header, 20, len(session))
        converted = header + session + old[3504:]
        struct.pack_into('<I', converted, 8, len(converted))
        converted[12:16] = bytes(4)
        struct.pack_into('<I', converted, 12, zlib.crc32(converted))
        assert converted[24 + len(session):] == old[3504:]
        project = tmp / 'converted.fmp'
        project.write_bytes(converted)
        exe = tmp / 'validate'
        subprocess.run(['cc', '-std=c11', '-O1', '-DBR_PATTERN_BANK_EXPERIMENT=1',
                        '-Ifirmware/src', '-Ifirmware/gen', 'tests/test_v4_migration_proof.c',
                        'firmware/src/pattern_project.c', 'firmware/src/pattern_bank.c',
                        'firmware/src/pattern_storage_codec.c', 'firmware/src/pattern_song.c',
                        '-o', str(exe)], cwd=ROOT, check=True)
        subprocess.run([str(exe), str(project)], cwd=ROOT, check=True)
    print('offline proof only: old pattern records unchanged; no device or project files modified')

if __name__ == '__main__':
    main()
