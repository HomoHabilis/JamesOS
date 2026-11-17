#!/usr/bin/env python3
"""Convert a saved v4/1.0a project payload offline; never accesses an instrument.

Input is the exact PSC3 payload from a project backup (or an uncompressed
FMP1 v4 envelope). Output is a new PSC3 file accepted by the current project
decoder. The original is never modified. This tool requires a C compiler and
the source tree. It does not extract a project from an instrument.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
from project_psc_decode import unpack_psc

def build_helper(cmd, output, helpers):
    """compile `cmd` -> `output`; with a `helpers` directory (--helper-dir) a program already built there is reused"""
    if helpers is None or not output.exists():
        subprocess.run(cmd + ['-o', str(output)], cwd=ROOT, check=True)


def convert(source, destination, helpers=None):
    data = source.read_bytes()
    old = unpack_psc(data) if data.startswith(b'PSC\3') else data
    if len(old) < 3504 or len(old) > 84480 or old[:8] != b'FMP1\x04\x00\x14\x00':
        raise ValueError('Expected a version-4 project envelope')
    if struct.unpack_from('<I', old, 8)[0] != len(old):
        raise ValueError('Invalid project length')
    check = bytearray(old); check[12:16] = bytes(4)
    if struct.unpack_from('<I', old, 12)[0] != zlib.crc32(check):
        raise ValueError('Project checksum failed')
    if old[20:24] != b'1DRB' or old[27] != 16:
        raise ValueError('Only version-16 sessions are supported')
    with tempfile.TemporaryDirectory(prefix='jamesos-project-migration-') as temp:
        temp = Path(temp)
        raw = temp / 'session.v16'
        raw.write_bytes(old[20:3504])
        if helpers is not None:
            helpers.mkdir(parents=True, exist_ok=True)
        subprocess.run([sys.executable, 'tools/session_v16_to_v17.py', '--out', str(temp),
                        '--build-dir', str(helpers if helpers is not None else temp / 'build'),
                        *(['--reuse-build'] if helpers is not None else []),
                        str(raw)], cwd=ROOT, check=True)
        candidates = list(temp.glob('*.v17.bin'))
        if len(candidates) != 1: raise ValueError('Session converter produced unexpected output')
        legacy = candidates[0].read_bytes()
        if len(legacy) != 3584 or legacy[4:6] != b'\x11\0':
            raise ValueError('Session converter produced an invalid record')
        mover, moved = (helpers or temp) / 'move-track-patches', temp / 'session.v20'
        build_helper(['cc', '-std=c11', '-O1', 'tools/session_v17_track_patches.c'], mover, helpers)
        subprocess.run([str(mover), str(candidates[0]), str(raw), str(moved)], cwd=ROOT, check=True)
        session = moved.read_bytes()
        if len(session) != 3584 or session[4:6] != b'\x14\0':
            raise ValueError('Track patch migration produced an invalid record')
        header = bytearray(24); header[:20] = old[:20]
        struct.pack_into('<HH', header, 4, 5, 24)
        struct.pack_into('<H', header, 20, len(session))
        project = header + session + old[3504:]
        struct.pack_into('<I', project, 8, len(project))
        project[12:16] = bytes(4)
        struct.pack_into('<I', project, 12, zlib.crc32(project))
        envelope, output, helper = temp / 'converted.fmp', temp / 'converted.psc', (helpers or temp) / 'validate'
        envelope.write_bytes(project)
        build_helper(['cc', '-std=c11', '-O1', '-DBR_PATTERN_BANK_EXPERIMENT=1', '-DBR_MODEL_MOD_EXPERIMENT=1',
                      '-Ifirmware/src', '-Ifirmware/gen', 'tools/project_v4_validate_encode.c',
                      'firmware/src/pattern_project.c', 'firmware/src/pattern_bank.c',
                      'firmware/src/pattern_storage_codec.c', 'firmware/src/pattern_song.c'], helper, helpers)
        subprocess.run([str(helper), str(envelope), str(output)], cwd=ROOT, check=True)
        result = output.read_bytes()
        if unpack_psc(result) != project: raise ValueError('Compressed output verification failed')
        # O_EXCL prevents replacing an original or an existing backup.
        fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, 'wb') as f:
                f.write(result)
        except BaseException:
            destination.unlink(missing_ok=True)
            raise
        print(f'Created {destination}; original left unchanged.')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('destination', type=Path)
    ap.add_argument('--helper-dir', type=Path, help='keep the helper programs here and reuse them '
                    '(for many conversions in one run; do not share between source versions)')
    args = ap.parse_args()
    try:
        convert(args.source, args.destination, args.helper_dir)
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        print(f'Conversion failed: {e}', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
