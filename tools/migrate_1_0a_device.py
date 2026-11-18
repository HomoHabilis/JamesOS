#!/usr/bin/env python3
"""Back up and convert saved projects from the 1.0a migration bridge.

Select the exact FM-1 MIDI port; other instruments can remain connected. The
bridge must already be installed. This tool only reads the instrument; it does
not install firmware or import the resulting projects. Keep the backup.
"""
import argparse
from pathlib import Path
from fm1_install import MidoBackend, handshake
from fm1_project_full_backup import capture_partition, pack_backup, write_new
from fm1_project_full_backup_device import ReadOnlyLink
from migrate_1_0a_projects import migrate

BRIDGE_IDENTITY = 'FM-1_9673046'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True, help='exact target FM-1 MIDI endpoint')
    ap.add_argument('--output', required=True, type=Path, help='new output directory')
    ap.add_argument('--backup-only', action='store_true',
                    help='save the verified backup now; convert it separately later')
    args=ap.parse_args()
    if args.output.exists() or args.output.is_symlink():
        raise FileExistsError('Output directory already exists; choose a new directory')
    if not args.output.parent.is_dir():
        raise FileNotFoundError('Output parent directory does not exist')
    backend=MidoBackend()
    # MidoBackend.input_names() deduplicates names: inspect the original lists
    # so identical endpoints cannot silently select a different instrument.
    if (backend.mido.get_input_names().count(args.port) != 1 or
            backend.mido.get_output_names().count(args.port) != 1):
        raise RuntimeError('The exact MIDI input and output must each occur once; disconnect duplicate endpoints')
    link=backend.open(args.port,args.port)
    try:
        ident=handshake(link)
        if not ident or ident.text!=BRIDGE_IDENTITY or ident.model.lower()!='fm-1':
            raise RuntimeError('The selected instrument is not running the 1.0a migration bridge')
        reader=ReadOnlyLink(link);reader.guard()
        args.output.mkdir(mode=0o700)
        print('Reading the saved project partition twice. Leave the instrument idle.',flush=True)
        partition=capture_partition(reader.request,reader.guard);reader.guard()
        backup=args.output/'Original 1.0a backup.josbank'
        write_new(backup,pack_backup(partition,ident.text))
        print('Original backup verified:',backup,flush=True)
    finally:
        link.close()
    if args.backup_only:
        print('Backup only: keep this file safe; convert it later with migrate_1_0a_projects.py.')
        return
    report=migrate(backup,args.output/'Converted')
    for slot in report['slots']:
        print(f"Slot {slot['slot']}: {slot.get('name','unreadable')} — {slot['status']}")
        if slot.get('error'): print('  '+slot['error'])
    if report['status']!='complete':
        raise SystemExit('Some projects could not be converted; the original backup is intact. See Migration report.json.')
    print('Conversion complete; no projects have been imported to the instrument.')
    print('Review the report and retain the original backup before importing with the Editor.')

if __name__=='__main__':main()
