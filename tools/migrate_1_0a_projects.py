#!/usr/bin/env python3
"""Convert a verified 1.0a full-partition backup into an Editor collection.

This only reads the original backup. A new directory is created exclusively,
with the extracted originals, converted projects and a per-slot report. It
never touches an instrument or overwrites any existing file.
"""
import argparse
import base64
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from fm1_project_full_backup import unpack_backup, PAIR_SIZE
from project_backup_extract import extract
from project_psc_decode import unpack_psc

ROOT = Path(__file__).resolve().parents[1]

def migrate(backup, output):
    blob = backup.read_bytes()
    metadata, partition = unpack_backup(blob)
    output.mkdir(mode=0o700)  # fail rather than reuse or overwrite a directory
    report = {'backup_sha256': hashlib.sha256(blob).hexdigest(),
              'source_identity': metadata['identity'], 'slots': []}
    projects = []
    for slot in range(13):
        pair = partition[slot * PAIR_SIZE:(slot + 1) * PAIR_SIZE]
        if pair == b'\xff' * PAIR_SIZE:
            continue
        entry = {'slot': slot + 1 if slot < 12 else 'working'}
        report['slots'].append(entry)
        try:
            (generation, _, name, payload), bad_peer = extract(blob, slot)
            entry.update(name=name, generation=generation, damaged_other_copy=bad_peer)
            if bad_peer:
                entry['warning'] = 'The other storage copy is damaged; retain the original backup.'
            prefix = f'slot-{slot + 1:02d}' if slot < 12 else 'working'
            source = output / f'{prefix}-original.psc'
            converted = output / f'{prefix}-converted.psc'
            source.write_bytes(payload)
            raw = unpack_psc(payload)
            if raw[:4] == b'FMP1' and raw[4:6] in (b'\x05\0', b'\x06\0'):
                # Mixed archives can contain a later-format project: validate
                # it with the actual decoder and retain its original payload.
                with tempfile.TemporaryDirectory(prefix='jamesos-current-check-') as td:
                    temp=Path(td); envelope=temp/'current.fmp'; encoded=temp/'validated.psc'; helper=temp/'validator'
                    envelope.write_bytes(raw)
                    built=subprocess.run(['cc','-std=c11','-O1','-DBR_PATTERN_BANK_EXPERIMENT=1','-DBR_MODEL_MOD_EXPERIMENT=1',
                        '-Ifirmware/src','-Ifirmware/gen','tools/project_v4_validate_encode.c',
                        'firmware/src/pattern_project.c','firmware/src/pattern_bank.c',
                        'firmware/src/pattern_storage_codec.c','firmware/src/pattern_song.c','-o',str(helper)],
                        cwd=ROOT,capture_output=True,text=True)
                    result=(subprocess.run([str(helper),str(envelope),str(encoded)],cwd=ROOT,capture_output=True,text=True)
                            if built.returncode==0 else built)
                    if result.returncode==0 and unpack_psc(encoded.read_bytes())==raw:
                        converted.write_bytes(payload)
                        result.stdout='Already current; verified and preserved.\n'+result.stdout
                    elif result.returncode==0:
                        raise ValueError('Current project verification did not round-trip')
            else:
                result = subprocess.run([sys.executable, str(ROOT / 'tools/project_v4_to_v5.py'),
                                         str(source), str(converted)], cwd=ROOT,
                                        capture_output=True, text=True)
            entry['conversion_notes'] = result.stdout.strip().splitlines()
            entry['source_format'] = 'current' if raw[:4] == b'FMP1' and raw[4:6] in (b'\x05\0', b'\x06\0') else 'legacy'
            if result.returncode:
                entry.update(status='failed', error=result.stderr.strip() or 'Conversion failed')
                continue
            data = converted.read_bytes()
            entry.update(status='converted', sha256=hashlib.sha256(data).hexdigest())
            item = {'name': name, 'data': base64.b64encode(data).decode('ascii'), 'sha256': entry['sha256']}
            if slot < 12:
                projects.append({'slot': slot, **item})
            else:
                working = {'format':'JamesOS-project', 'version':1, 'projectFormat':'PSC3', **item}
                (output / 'Current working project.jproject').write_text(json.dumps(working, indent=2) + '\n')
        except (OSError, ValueError, UnicodeError) as error:
            entry.update(status='failed', error=str(error))
    report['status'] = 'complete' if all(x['status'] == 'converted' for x in report['slots']) else 'incomplete'
    if projects:
        collection = {'format':'JamesOS-project-collection', 'version':1, 'projectFormat':'PSC3', 'projects':projects}
        (output / 'Converted projects.jproject').write_text(json.dumps(collection, indent=2) + '\n')
    (output / 'Migration report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path, help='verified 1.0a .josbank backup')
    parser.add_argument('output', type=Path, help='new output directory')
    args = parser.parse_args()
    try:
        report = migrate(args.backup, args.output)
    except (OSError, ValueError) as error:
        print(f'Migration failed: {error}', file=sys.stderr)
        return 1
    for slot in report['slots']:
        print(f"Slot {slot['slot']}: {slot.get('name', 'unreadable')} — {slot['status']}")
        for note in slot.get('conversion_notes', []): print('  ' + note)
        if slot.get('error'): print('  ' + slot['error'], file=sys.stderr)
    print(f"Migration {report['status']}. Original backup retained. Results: {args.output}")
    print('No projects were imported to an instrument. Review Migration report.json and retain the backup.')
    return 0 if report['status'] == 'complete' else 1

if __name__ == '__main__':
    sys.exit(main())
