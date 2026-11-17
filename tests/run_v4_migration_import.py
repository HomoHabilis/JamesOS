#!/usr/bin/env python3
"""Test one migrated fixture through host application import and load."""
import subprocess
import struct
import sys
import tempfile
import zlib
from pathlib import Path
from run_pattern_import_runtime import FLAGS, ROOT
from run_v4_migration_proof import u32, unpack_psc

SOURCES = ('pattern_flash_store', 'pattern_project', 'pattern_project_adapter',
           'pattern_project_host_bridge', 'pattern_project_target_bridge',
           'pattern_storage_codec', 'pattern_codec_source', 'pattern_compressed_project',
           'project_store', 'pattern_owned_ps', 'pattern_fixed_project', 'pattern_fixed_stage',
           'pattern_fixed_bridge', 'pattern_fixed_target', 'pattern_pair_stream', 'pattern_sha256',
           'pattern_stream_project', 'pattern_pair_project', 'pattern_pair_bridge',
           'pattern_pair_owner', 'pattern_fixed_runtime', 'pattern_park_io',
           'pattern_pair_authority', 'pattern_fixed_authority', 'pattern_bank',
           'pattern_song', 'example_song_import')

def main():
    image = (ROOT / 'tests/fixtures/project-v16-store.bin').read_bytes()
    payload = image[256:256 + u32(image, 32)]
    old_project = bytearray(unpack_psc(payload))
    for part in range(4):
        base = 20 + 240 + 165 * part + 37
        old_project[base:base + 12] = bytes([5, 2, 236, 1, 4, 4, 24, 0, 8, 1, 13, 1])
        old_project[base - 22:base - 20] = bytes([13, 1])
    old_project[20 + 2824 + 2:20 + 2824 + 5] = bytes([96, 32, 127])
    old_project[12:16] = bytes(4)
    struct.pack_into('<I', old_project, 12, zlib.crc32(old_project))
    objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
    objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects: raise SystemExit('Host DSP objects are required')
    with tempfile.TemporaryDirectory(prefix='jamesos-v4-import-') as tmp:
        tmp = Path(tmp)
        old, converted = tmp / 'old.fmp', tmp / 'converted.psc'
        old.write_bytes(old_project)
        subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', str(old), str(converted)],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        planner, binary = tmp / 'planner.o', tmp / 'test'
        subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c',
                        '-o', str(planner)], cwd=ROOT, check=True)
        subprocess.run(['c++', '-x', 'c', *FLAGS, 'tests/test_v4_migration_import.c',
                        *(f'firmware/src/{s}.c' for s in SOURCES), '-x', 'none',
                        str(planner), *map(str, objects), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary), str(converted)], cwd=ROOT, check=True)

if __name__ == '__main__':
    main()
