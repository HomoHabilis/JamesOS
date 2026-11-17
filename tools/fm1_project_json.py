#!/usr/bin/env python3
"""Encode an FM-1 example score as a PSC3 project, using the firmware codec.

This score format describes sounds in prose, not preset IDs. The three synth
presets default to bank indices 3, 8, 41 and the kit to index 9. The host
helper validates a complete encode/decode within the device's loading limits.
Run tests/build_project_json_host.py first. No device connection is made.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def convert(path, output, sounds=(3, 8, 41)):
    song = json.loads(Path(path).read_text())
    patterns = song['patterns']
    entries = song['song']['entries']
    if song.get('format') != 'fm1-pattern-song-example-v1' or len(patterns) != 11:
        raise ValueError('expected an 11-pattern FM-1 example score')
    if song.get('step_indexing') != 'zero_based':
        raise ValueError('score must use zero-based step indexing')
    loop = song['song']['loop']
    if not isinstance(loop, bool):
        raise ValueError('song loop must be true or false')
    lines = [f"{song['tempo_bpm']} {song['swing']} {len(patterns)} {len(entries)} {int(loop)}",
             ' '.join(str(x) for x in sounds)]
    for p in patterns:
        events = []
        if set(p['tracks']) != {'T1', 'T2', 'T3', 'DRUMS'}:
            raise ValueError('each pattern must have exactly T1, T2, T3 and DRUMS')
        for index, name in enumerate(('T1', 'T2', 'T3', 'DRUMS')):
            for event in p['tracks'][name]:
                events.append((index, event['step'], event['lane'] if index == 3 else event['note'],
                               event['velocity'], event['length_units']))
        lines.append(f"{p['slot']} {p['length_steps']} {p['division']} {len(events)}")
        lines.extend(' '.join(str(x) for x in event) for event in events)
    lines.extend(f"{entry['pattern']} {entry['bars']}" for entry in entries)
    helper = ROOT / 'build/session-v5-store-test/project_json_host'
    if not helper.is_file():
        raise FileNotFoundError('build the host helper with tests/build_project_json_host.py first')
    subprocess.run([str(helper), str(output)], input='\n'.join(lines) + '\n', text=True, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('score')
    parser.add_argument('output')
    parser.add_argument('--sounds', nargs=3, type=int, default=(3, 8, 41),
                        metavar=('T1', 'T2', 'T3'), help='three synth bank indices')
    args = parser.parse_args()
    convert(args.score, args.output, args.sounds)


if __name__ == '__main__':
    main()
