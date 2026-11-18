#!/usr/bin/env python3
"""Exercise the offline converter with a historical project, no device access."""
import subprocess
import struct
import sys
import tempfile
import zlib
from pathlib import Path
from run_v4_migration_proof import unpack_psc, u32

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from fm1_project_full_backup import pack_backup, SIZE
from project_backup_extract import extract

def main():
    image = (ROOT / 'tests/fixtures/project-v16-store.bin').read_bytes()
    original = image[256:256 + u32(image, 32)]
    old = unpack_psc(original)
    partition = bytearray(b'\xff' * SIZE)
    partition[:8192] = image[:8192]
    partition[24 * 4096:26 * 4096] = image[8192:]
    backup = pack_backup(bytes(partition), 'FM-1_9262108')
    selected, bad = extract(backup, 0)
    assert selected[2] == 'OLD SONG' and selected[3] == original and not bad
    # A newer verified copy wins; ambiguous generations fail closed.
    other = bytearray(partition[:4096])
    struct.pack_into('<II', other, 24, 1, 2)
    struct.pack_into('<I', other, 88, zlib.crc32(other[:88]))
    paired = bytearray(partition); paired[4096:8192] = other
    selected, bad = extract(pack_backup(bytes(paired), 'FM-1_9262108'), 0)
    assert selected[0] == 2 and selected[3] == original and not bad
    # A damaged peer is reported while the intact older copy is retained.
    paired[4096 + 256] ^= 1
    selected, bad = extract(pack_backup(bytes(paired), 'FM-1_9262108'), 0)
    assert selected[0] == 1 and selected[3] == original and bad
    paired[4096:8192] = other
    struct.pack_into('<I', other, 28, 4)
    struct.pack_into('<I', other, 88, zlib.crc32(other[:88]))
    paired[4096:8192] = other
    try:
        extract(pack_backup(bytes(paired), 'FM-1_9262108'), 0)
        raise AssertionError('ambiguous generations accepted')
    except ValueError:
        pass
    for invalid in (original[:-1], b'PSC\x03' + struct.pack('<II', 1, 0) + b'\xc0\x01\x00',
                    b'PSC\x03' + struct.pack('<II', 84481, 0)):
        try:
            unpack_psc(invalid)
            raise AssertionError('invalid compressed payload accepted')
        except ValueError:
            pass
    tampered = bytearray(backup); tampered[-1] ^= 1
    try:
        extract(bytes(tampered), 0)
        raise AssertionError('corrupt backup accepted')
    except ValueError:
        pass
    with tempfile.TemporaryDirectory(prefix='jamesos-v4-cli-test-') as temp:
        temp = Path(temp)
        source, dest = temp / 'old.psc', temp / 'new.psc'
        helpers = str(temp / 'helpers')       # converter helper programs: built by the first case, reused by the others
        archive = temp / 'old.josbank'
        archive.write_bytes(backup)
        subprocess.run([sys.executable, 'tools/project_backup_extract.py', str(archive),
                        '1', str(source)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        assert source.read_bytes() == original and archive.read_bytes() == backup
        cmd = [sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(source), str(dest)]
        subprocess.run(cmd, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        result = unpack_psc(dest.read_bytes())
        assert result[:8] == b'FMP1\x05\x00\x18\x00'
        assert result[24 + 3584:] == old[20 + 3484:]
        assert source.read_bytes() == original
        raw, raw_result = temp / 'old.fmp', temp / 'raw-result.psc'
        raw.write_bytes(old)
        subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(raw), str(raw_result)],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        assert unpack_psc(raw_result.read_bytes()) == result
        invalid_raw = bytearray(old); invalid_raw[-1] ^= 1
        raw.write_bytes(invalid_raw)
        bad_output = temp / 'bad-raw.psc'
        assert subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(raw),
                               str(bad_output)], cwd=ROOT, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0
        assert not bad_output.exists()
        # 1.0a's visible patches were routes 0 and 1 on each track. They
        # must become editable panel patches once, without duplicating audio.
        assigned = bytearray(old)
        for part, delay in enumerate((0, 32, 64, 127)):
            module = 20 + 240 + 165 * part
            assigned[module + 29] = delay
            assigned[module + 14] = delay
            assigned[module + 18] = delay
            assigned[module + 15:module + 17] = bytes([13, 1])
            assigned[module + 37:module + 49] = bytes([5, 2, 236, 1, 4, 4, 24, 0, 8, 1, 13, 1])
        assigned[12:16] = bytes(4)
        struct.pack_into('<I', assigned, 12, zlib.crc32(assigned))
        raw.write_bytes(assigned)
        mapped = temp / 'mapped.psc'
        subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(raw), str(mapped)],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        session = unpack_psc(mapped.read_bytes())[24:24 + 3584]
        assert session[4:6] == b'\x14\x00'
        # Newly introduced LPG gate lengths default to OFF for legacy songs.
        assert all((session[565 + 2 * track] >> 3) == 0 for track in range(3))
        for part in range(4):
            base = 76 + 112 * part
            for slot, expected in enumerate(((5, 2, -20, 1), (4, 4, 24, 0))):
                value = 0
                for bit in range(19):
                    at = slot * 19 + bit
                    value |= ((session[base + 68 + at // 7] >> (1 + at % 7)) & 1) << bit
                amount = (value >> 10) & 255
                if amount >= 128: amount -= 256
                assert (value & 31, (value >> 5) & 31, amount, (value >> 18) & 1) == expected
                assert session[base + 44 + slot] == 0 and session[base + 52 + slot] == 0
                assert session[base + 60 + slot] == 0 and not (session[base + 68 + slot] & 1)
            assert (session[base + 46], session[base + 54], session[base + 62],
                    session[base + 70] & 1) == (8, 1, 41, 1)
            assert session[base + 30:base + 32] == bytes([41, 1])
            original_delay = (0, 32, 64, 127)[part]
            old_24ths = (3, 4, 6, 8, 12, 24, 24, 48, 48, 72, 96)
            new_24ths = (3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 72, 96)
            assert old_24ths[original_delay * 11 // 128] == new_24ths[session[base + 82] * 14 // 128]
            assert session[base + 29] == session[base + 37] == (7, 102, 114, 127)[part]
        rate_modulated = bytearray(assigned)
        rate_modulated[20 + 240 + 38] = 18
        rate_modulated[12:16] = bytes(4)
        struct.pack_into('<I', rate_modulated, 12, zlib.crc32(rate_modulated))
        raw.write_bytes(rate_modulated)
        result = subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(raw),
                                 str(temp / 'rate-warning.psc')], cwd=ROOT, check=True,
                                stdout=subprocess.PIPE, text=True)
        assert 'Warning: track 1 modulates LFO rate' in result.stdout
        # Old HOME depths are additive LFO 1 routes, not attenuverters.
        nonneutral = bytearray(assigned)
        nonneutral[20 + 2824 + 2:20 + 2824 + 5] = bytes([96, 32, 127])
        nonneutral[12:16] = bytes(4)
        struct.pack_into('<I', nonneutral, 12, zlib.crc32(nonneutral))
        raw.write_bytes(nonneutral)
        restored = temp / 'legacy-depth.psc'
        subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(raw), str(restored)],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        track = unpack_psc(restored.read_bytes())[24 + 76:24 + 76 + 112]
        assert track[17:20] == bytes([64, 64, 64])
        for slot, (source_id, target_id, amount) in ((0, (5, 2, 64)), (1, (5, 1, -89)), (3, (5, 3, 127))):
            assert (track[44 + slot], track[52 + slot], (track[60 + slot] + 128) % 256 - 128,
                    track[68 + slot] & 1) == (source_id, target_id, amount, 1)
        # If all remaining legacy routes carry data, no settings are discarded.
        crowded = bytearray(nonneutral)
        route_base = 20 + 240 + 37
        for slot in range(2, 8):
            crowded[route_base + slot*4:route_base + slot*4 + 4] = bytes([5, 2, slot, 1])
        crowded[12:16] = bytes(4)
        struct.pack_into('<I', crowded, 12, zlib.crc32(crowded))
        raw.write_bytes(crowded)
        refused = temp / 'no-free-route.psc'
        result = subprocess.run([sys.executable, 'tools/project_v4_to_v5.py', '--helper-dir', helpers, str(raw), str(refused)],
                                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        assert result.returncode != 0 and 'no free route' in result.stderr and not refused.exists()
        preserved = dest.read_bytes()
        assert subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0
        assert dest.read_bytes() == preserved
        corrupt = bytearray(original); corrupt[-1] ^= 1
        source.write_bytes(corrupt)
        failed = temp / 'must-not-exist.psc'
        assert subprocess.run([sys.executable, 'tools/project_v4_to_v5.py',
                               str(source), str(failed)], cwd=ROOT,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
        assert not failed.exists()
        assert subprocess.run([sys.executable, 'tools/project_backup_extract.py', str(archive),
                               '1', str(source)], cwd=ROOT, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0
        assert source.read_bytes() == corrupt
    print('v4 converter: current decoder accepts result; patterns preserved; originals and failures protected')

if __name__ == '__main__':
    main()
