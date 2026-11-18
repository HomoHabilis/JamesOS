#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the tools/fm1_install.py and ROM-backup tests with fake MIDI/USB only (never opens a port).

    test_fm1_install_select.py            device selection (FM-1 and SMK-37 Pro together), model
                                          refusal, --info, install/resume/mismatch paths
    test_loader_identity_pin_independent.py  update-loader identity admission
    test_usb_identity.py                  USB strings and the installer's port filter
    test_rom_backup_gate.py               read-only ROM backup gate: chip identity per product
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
for test in ('test_fm1_install_select.py', 'test_indexed_midi.py', 'test_loader_identity_pin_independent.py', 'test_usb_identity.py',
             'test_rom_backup_gate.py'):
    subprocess.run([sys.executable, str(HERE / test)], cwd=HERE.parent, check=True)
print('installer and ROM backup gate tests passed (fakes only)')
