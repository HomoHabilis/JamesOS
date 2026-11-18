#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ROM backup chip identity per product and the SMK-37 Pro backup path, with fakes only (no USB)."""
import sys
import tempfile
import types
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from fm1_rom_backup_gate import ReadOnlyGate, FLASH_SIZE  # noqa: E402
import fm1_rom_backup  # noqa: E402


def gate(smk):
    g = ReadOnlyGate.__new__(ReadOnlyGate)        # the pinned RAM loader is not part of the repository
    g.chunks, g.started, g.identified, g.smk37_pro = set(), True, False, smk
    return g


def accepts(g, key, kind, jedec):
    try:
        g.confirm_chip(key, kind, jedec)
        return g.identified
    except ValueError:
        return False


fm1, smk = gate(False), gate(True)
for jedec in (0x856014, 0xB36014):
    assert accepts(gate(False), 0x980F, 0x10, jedec)
for jedec in (0xEB6014, 0x856015, 0, 0xFFFFFF):
    assert not accepts(gate(False), 0x980F, 0x10, jedec), hex(jedec)
for jedec in (0xEB6014, 0x856014, 0xB36014):
    assert accepts(gate(True), 0x980F, 0x10, jedec)
for jedec in (0x856015, 0xEB6013, 0, 0xFFFFFF):
    assert not accepts(gate(True), 0x980F, 0x10, jedec), hex(jedec)
for g in (gate(False), gate(True)):
    assert not accepts(g, 0x1234, 0x10, 0x856014)                 # chip key
    assert not accepts(g, 0x980F, 256, 0x856014)                  # device type
    g.started = False
    assert not accepts(g, 0x980F, 0x10, 0x856014)                 # loader not started


class Flash:
    def __init__(self, data):
        self.data, self.reads = data, 0

    def start_loader(self):
        pass

    def info(self):
        return 0x980F, 0x10, 0xEB6014

    def read(self, off, n, label):
        self.reads += 1
        return self.data[off:off + n]


given = []
upstream = types.SimpleNamespace(give_to_user=lambda p: given.append(Path(p).name))
image = bytearray(FLASH_SIZE)
image[0xC1224:0xC122C] = b'trimdat\0'
with tempfile.TemporaryDirectory() as tmp:
    a = types.SimpleNamespace(output_dir=Path(tmp) / 'backup')
    fm1_rom_backup.smk37_backup(a, upstream, Flash(bytes(image)), gate(True))
    assert (a.output_dir / 'full-flash.bin').read_bytes() == bytes(image)
    assert (a.output_dir / 'flash-id.txt').read_text() == 'EB6014\n'
    assert not (a.output_dir / 'projects.josbank').exists()
    assert given == ['full-flash.bin', 'backup', 'flash-id.txt']
    image[0xC1224] = 0
    a = types.SimpleNamespace(output_dir=Path(tmp) / 'nomarker')
    try:
        fm1_rom_backup.smk37_backup(a, upstream, Flash(bytes(image)), gate(True))
        raise AssertionError('missing calibration marker accepted')
    except ValueError as e:
        assert 'calibration marker' in str(e) and not a.output_dir.exists()
print('ROM backup chip identity (FM-1, SMK-37 Pro) and SMK full-flash backup path passed (fakes only)')
