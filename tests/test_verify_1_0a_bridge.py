#!/usr/bin/env python3
"""Offline bridge pinning checks; never opens a device."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import verify_1_0a_bridge as bridge

class TestBridge(unittest.TestCase):
    def test_hash_and_loader_guard(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bridge.fwsc';path.write_bytes(b'fixture')
            with patch.object(bridge, 'BRIDGE_HASH', hashlib.sha256(b'fixture').hexdigest()):
                with patch.object(bridge.validate_package, 'validate', return_value={'loader_identity':'ota-FM-1_904'}) as validate:
                    bridge.verify(path)
                    validate.assert_called_once_with(b'fixture', expected_identity=bridge.BRIDGE_ID)
                with patch.object(bridge.validate_package, 'validate', return_value={'loader_identity':'wrong'}):
                    with self.assertRaisesRegex(ValueError,'loader'): bridge.verify(path)
                path.write_bytes(b'changed')
                with patch.object(bridge.validate_package, 'validate') as validate:
                    with self.assertRaisesRegex(ValueError,'SHA-256'): bridge.verify(path)
                    validate.assert_not_called()
if __name__=='__main__':unittest.main()
