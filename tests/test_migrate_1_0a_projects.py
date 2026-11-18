#!/usr/bin/env python3
"""Collection migration: old fixture, complete/incomplete reports and no overwrite."""
import base64, hashlib, json, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from fm1_project_full_backup import SIZE, pack_backup
from project_psc_decode import unpack_psc

def run(backup, output):
    return subprocess.run([sys.executable, str(ROOT/'tools/migrate_1_0a_projects.py'), str(backup), str(output)],
                          cwd=ROOT, capture_output=True, text=True)

def main():
    fixture=(ROOT/'tests/fixtures/project-v16-store.bin').read_bytes()
    partition=bytearray(b'\xff'*SIZE); partition[:8192]=fixture[:8192]
    partition[24*4096:26*4096]=fixture[8192:]
    second=bytearray(fixture[:4096]); struct.pack_into('<I',second,20,1); struct.pack_into('<I',second,88,zlib.crc32(second[:88]))
    partition[8192:12288]=second
    with tempfile.TemporaryDirectory() as name:
        root=Path(name); backup=root/'old.josbank'; backup.write_bytes(pack_backup(bytes(partition),'FM-1_9262108'))
        before=backup.read_bytes(); output=root/'converted'; result=run(backup,output); assert result.returncode==0, result.stderr
        collection=json.loads((output/'Converted projects.jproject').read_text()); assert [p['slot'] for p in collection['projects']]==[0,1]
        for item in collection['projects']:
            data=base64.b64decode(item['data'],validate=True); assert hashlib.sha256(data).hexdigest()==item['sha256']
            assert unpack_psc(data)[:8]==b'FMP1\x05\0\x18\0'
        working=json.loads((output/'Current working project.jproject').read_text())
        assert working['format']=='JamesOS-project' and working['name']=='W01'
        data=base64.b64decode(working['data'],validate=True); assert hashlib.sha256(data).hexdigest()==working['sha256']
        assert backup.read_bytes()==before
        assert run(backup,output).returncode!=0 and backup.read_bytes()==before
        partition[8192+256]^=1; damaged=root/'damaged.josbank';damaged.write_bytes(pack_backup(bytes(partition),'FM-1_9262108'))
        partial=root/'partial'; assert run(damaged,partial).returncode==1
        report=json.loads((partial/'Migration report.json').read_text()); assert report['status']=='incomplete' and report['slots'][1]['status']=='failed'
        assert len(json.loads((partial/'Converted projects.jproject').read_text())['projects'])==1
        invalid=root/'bad.josbank'; invalid.write_bytes(before[:-1]); assert run(invalid,root/'bad-out').returncode!=0 and not (root/'bad-out').exists()
    print('1.0a collection migration: slot fidelity, checksums, partial failures and no-overwrite passed')
if __name__=='__main__':main()
