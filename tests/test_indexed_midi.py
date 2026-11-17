#!/usr/bin/env python3
"""Duplicate MIDI endpoint names remain individually addressable, without hardware."""
import sys
import types
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
class Input:
    def get_ports(self): return ['FM-1', 'FM-1']
    def ignore_types(self, **kw): pass
    def open_port(self, index): self.index = index
    def close_port(self): pass
    def set_callback(self, cb): self.callback = cb
    def cancel_callback(self): pass
class Output:
    def get_ports(self): return ['FM-1', 'FM-1']
    def open_port(self, index): self.index = index
    def close_port(self): pass
    def send_message(self, data): self.message = data
sys.modules['rtmidi'] = types.SimpleNamespace(MidiIn=Input, MidiOut=Output)
import fm1_install as fm
import fm1_rtmidi_indexed as indexed
fm.DELAY['open'] = 0
backend = indexed.IndexedBackend()
names = backend.input_names()
assert names == ['FM-1 [index 0]', 'FM-1 [index 1]']
assert backend.output_names() == names
assert fm.pair_output(names[1], backend.output_names()) == names[1]
link = backend.open(names[1], names[1])
assert link.inp.index == link.out.index == 1
assert link.send(b'\xf0\x01\xf7') and link.out.message == [0xf0, 1, 0xf7]
link._rx(([0xf0, 2, 0xf7], 0.0))
assert link.read(0) == b'\xf0\x02\xf7' and not link.lost
link.close()
print('PASS: duplicate MIDI endpoint names remain distinct')
