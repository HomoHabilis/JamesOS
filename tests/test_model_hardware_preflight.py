#!/usr/bin/env python3
"""Hardware stress preflight is strict and never requires a device."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from model_transition_hardware import validate_snapshot

header = bytearray(32)
header[:4] = b'JSES'; header[4:6] = (17).to_bytes(2, 'little')
header[21] = 1
header[24:31] = bytes(range(1, 8))
global_lfo = bytearray(4)
track = bytearray(112)
track[0] = 52; track[30] = 127; track[31] = 27; track[108] = 2
assert validate_snapshot(header, global_lfo, track, 0, 6) == 1
track[30] = 0
assert validate_snapshot(header, global_lfo, track, 0, 1, True) == 1
track[30] = 127
try:
    validate_snapshot(header, global_lfo, track, 0, 1, True)
    raise AssertionError('active model target accepted as baseline')
except ValueError:
    pass
for mutate, restore in (
    (lambda: header.__setitem__(25, 1), lambda: header.__setitem__(25, 2)),
    (lambda: header.__setitem__(20, 1), lambda: header.__setitem__(20, 0)),
    (lambda: track.__setitem__(31, 0), lambda: track.__setitem__(31, 27)),
    (lambda: track.__setitem__(108, 1), lambda: track.__setitem__(108, 2)),
):
    mutate()
    try:
        validate_snapshot(header, global_lfo, track, 0, 6)
        raise AssertionError('unsafe preflight was accepted')
    except ValueError:
        pass
    finally:
        restore()
print('MODEL hardware measurement preflight passed')
