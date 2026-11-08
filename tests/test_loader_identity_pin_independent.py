#!/usr/bin/env python3
"""Exercise update-loader identity admission using fake links only."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import fm1_install as fm
class Link:
    def __init__(self): self.closed = False
    def close(self): self.closed = True
class Device:
    def __init__(self, text, model):
        self.id = fm.Identity(text, model, int(text.split('_')[-1])); self.link = Link()
u = fm.Updater(None)
writes = []
u.write = lambda device, image, step: writes.append(device.id.text)
u.verify = lambda product, step: product
old = Device('ota-FM-1_900', 'ota-FM-1')
try:
    u.install(old, b'x', 'FM-1_9261904', 'ota-FM-1_904')
    raise AssertionError('foreign loader accepted')
except fm.InstallError as error:
    assert error.code == 'foreign'
assert old.link.closed and not writes
new = Device('ota-FM-1_904', 'ota-FM-1')
assert u.install(new, b'x', 'FM-1_9261904', 'ota-FM-1_904') == 'FM-1_9261904'
assert writes == ['ota-FM-1_904']
print('old loader refused before write; matching loader accepted (fake only)')
