#!/usr/bin/env python3
"""FM-1 installer adapter preserving duplicate CoreMIDI port names by index."""
import queue
import re
import sys
import time
import rtmidi
import fm1_install

class IndexedLink:
    def __init__(self, backend, name, index, output_index):
        self.backend, self.name = backend, name
        self.q = queue.Queue()
        self.inp, self.out = rtmidi.MidiIn(), rtmidi.MidiOut()
        self.inp.ignore_types(sysex=False, timing=True, active_sense=True)
        self.inp.open_port(index)
        try:
            self.out.open_port(output_index)
            self.inp.set_callback(self._rx)
        except Exception:
            self.inp.close_port()
            raise
        time.sleep(fm1_install.DELAY['open'])
    def _rx(self, event, _data=None):
        message = bytes(event[0])
        if message and message[0] == 0xf0 and message[-1] == 0xf7:
            self.q.put(message)
    @property
    def lost(self):
        return self.name not in self.backend.input_names()
    def send(self, packet):
        try:
            self.out.send_message(list(packet))
            return True
        except Exception:
            return False
    def read(self, timeout):
        try: return self.q.get(timeout=max(timeout, 0))
        except queue.Empty: return None
    def drain(self):
        while not self.q.empty(): self.q.get_nowait()
    def close(self):
        self.inp.cancel_callback()
        self.inp.close_port()
        self.out.close_port()

class IndexedBackend:
    @staticmethod
    def _ports(cls):
        obj = cls()
        try: return [f'{name} [index {i}]' for i, name in enumerate(obj.get_ports())]
        finally: del obj
    def input_names(self): return self._ports(rtmidi.MidiIn)
    def output_names(self): return self._ports(rtmidi.MidiOut)
    def open(self, in_name, out_name):
        def index(name):
            match = re.search(r' \[index (\d+)\]$', name)
            if not match: raise ValueError('Expected indexed MIDI endpoint')
            return int(match.group(1))
        return IndexedLink(self, in_name, index(in_name), index(out_name))

if __name__ == '__main__':
    sys.exit(fm1_install.main(backend=IndexedBackend()))
