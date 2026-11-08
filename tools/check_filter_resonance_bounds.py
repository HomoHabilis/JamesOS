#!/usr/bin/env python3
"""Verify every precomputed resonance bound against exact integer sqrt."""
import math
import re
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / 'firmware/src/instrument_filter.c').read_text()
match = re.search(r'br_filter_resonance_limit\[(\d+)\] = \{([^}]+)\}', source)
assert match, 'missing bound table'
count = int(match[1])
values = [int(v) for v in re.findall(r'\d+', match[2])]
assert len(values) == count
assert f'resonance < {count}u ? br_filter_resonance_limit[resonance] : 32767u' in source
for resonance in range(128):
    damping = 65534 - resonance * 477
    expected = math.isqrt(damping*damping + 4*32768*32768) - damping
    expected = 32767 if expected > 32767 else expected - 2 if expected > 2 else expected
    actual = values[resonance] if resonance < count else 32767
    assert actual == expected, (resonance, actual, expected)
print('PASS: all 128 cached filter bounds exactly match the original integer-square-root calculation')
