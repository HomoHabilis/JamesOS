"""Helpers for sector-test-only, read-only project-pair diagnostic 0xD2.

The caller owns the connected session, verifies device identity, keeps the
unit stopped and quiet, and chooses an external backup destination. Equal
passes do not prove an atomic snapshot or confer any write permission.
"""
import struct


def read_pair_twice(request, slot):
    """Return two complete sectors only if two bounded reads match exactly."""
    if not isinstance(slot, int) or not 0 <= slot <= 12:
        raise ValueError('project slot must be between 0 and 12')

    def read_once():
        chunks = []
        for sector in (slot * 2, slot * 2 + 1):
            for offset in range(0, 4096, 32):
                data = request(0xD2, struct.pack('<BHB', sector, offset, 32))
                if not isinstance(data, bytes) or len(data) != 32:
                    raise ValueError('project-sector read returned an invalid chunk')
                chunks.append(data)
        return b''.join(chunks)

    first = read_once()
    second = read_once()
    if first != second:
        raise ValueError('project pair changed during export')
    return first
