"""Read-only, double-pass project partition backup; no device-writing transport.

Requires a firmware exposing diagnostic 0xD2. Captures all twelve named pairs
and the working pair, not uncommitted RAM edits. Equal reads do not make the
partition atomic: caller must prevent every writer throughout both passes.
"""
import hashlib
import json
import os
import struct

SECTOR_SIZE = 4096
SECTORS = 26
PAIR_SIZE = SECTOR_SIZE * 2
SIZE = SECTOR_SIZE * SECTORS
MAGIC = b'JOSBANK1'
HEADER_LIMIT = 16384


def _pass(request, guard):
    chunks = []
    for sector in range(SECTORS):
        guard()  # caller verifies identity, stopped state and writer exclusion
        for offset in range(0, SECTOR_SIZE, 32):
            data = request(0xD2, struct.pack('<BHB', sector, offset, 32))
            if not isinstance(data, bytes) or len(data) != 32:
                raise ValueError('invalid project-sector response')
            chunks.append(data)
    guard()
    return b''.join(chunks)


def capture_partition(request, guard):
    first = _pass(request, guard)
    second = _pass(request, guard)
    if first != second:
        raise ValueError('project partition changed during capture')
    return first


def pack_backup(partition, identity):
    if not isinstance(partition, bytes) or len(partition) != SIZE:
        raise ValueError('invalid partition size')
    if not isinstance(identity, str) or not identity or len(identity) > 128 or not identity.isascii():
        raise ValueError('invalid firmware identity')
    meta = {'format': 1, 'identity': identity, 'sector_size': SECTOR_SIZE,
            'sectors': SECTORS, 'sha256': hashlib.sha256(partition).hexdigest(),
            'pairs': [hashlib.sha256(partition[i:i+PAIR_SIZE]).hexdigest()
                      for i in range(0, SIZE, PAIR_SIZE)]}
    header = json.dumps(meta, sort_keys=True, separators=(',', ':')).encode('ascii')
    return MAGIC + struct.pack('<I', len(header)) + header + partition


def unpack_backup(blob):
    if not isinstance(blob, bytes) or len(blob) < 12 or blob[:8] != MAGIC:
        raise ValueError('invalid backup header')
    n = struct.unpack_from('<I', blob, 8)[0]
    if n > HEADER_LIMIT or len(blob) != 12 + n + SIZE:
        raise ValueError('invalid backup length')
    try:
        meta = json.loads(blob[12:12+n].decode('ascii'))
    except (ValueError, UnicodeError) as exc:
        raise ValueError('invalid backup metadata') from exc
    data = blob[12+n:]
    if not isinstance(meta, dict) or set(meta) != {'format','identity','sector_size','sectors','sha256','pairs'}:
        raise ValueError('invalid backup schema')
    if type(meta['format']) is not int or meta['format'] != 1 or type(meta['sector_size']) is not int or meta['sector_size'] != SECTOR_SIZE or type(meta['sectors']) is not int or meta['sectors'] != SECTORS:
        raise ValueError('unsupported backup format')
    if not isinstance(meta['identity'],str) or not meta['identity'] or len(meta['identity'])>128 or not meta['identity'].isascii():
        raise ValueError('invalid backup identity')
    pairs = [hashlib.sha256(data[i:i+PAIR_SIZE]).hexdigest() for i in range(0,SIZE,PAIR_SIZE)]
    if meta['sha256'] != hashlib.sha256(data).hexdigest() or meta['pairs'] != pairs:
        raise ValueError('backup digest mismatch')
    return meta, data


def write_new(path, blob):
    unpack_backup(blob)  # never create a file for an invalid archive
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(blob); f.flush(); os.fsync(f.fileno())
    except BaseException:
        # Retain a partial file for inspection; never overwrite it automatically.
        raise
    with open(path, 'rb') as f:
        if f.read() != blob:
            raise ValueError('backup readback mismatch')
