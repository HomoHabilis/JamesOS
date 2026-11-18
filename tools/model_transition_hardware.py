#!/usr/bin/env python3
"""Measure a manually configured MODEL LFO on the experimental FM-1 build.

Checks identity, stopped transport, recording/SAVE state, selected synth track,
MIDI channel and active MODEL routing before sending any notes. Does not change
project settings, save, restore, or access flash. Audio counters are reset for
the measurement. Always releases the notes it sends, including on failure.
"""
import argparse
import json
import struct
import time
from pathlib import Path
from ui_timing import Link, identity, command, where

MODEL_TARGET = 27
EXPECTED = 'FM-1_9263020'
NOTES = (48, 52, 55, 59, 62, 67)


def read_chunk(link, offset, count):
    body = offset.to_bytes(2, 'little') + bytes((count,))
    result = command(link, 0x63, body)
    if result is None or len(result) != count:
        raise RuntimeError(f'session read failed at offset {offset}')
    return result


def metrics(link):
    data = command(link, 0x60)
    if data is None or len(data) != 40:
        raise RuntimeError('audio metrics unavailable')
    keys = ('last', 'worst', 'overruns', 'halves', 'usb0', 'usb1', 'usb2', 'usb3', 'average', 'overruns_boot')
    return dict(zip(keys, struct.unpack('<10I', data)))


def validate_snapshot(header, global_fields, track, track_no, notes, baseline=False):
    if header[:4] != b'JSES' or int.from_bytes(header[4:6], 'little') != 17:
        raise ValueError('expected a v17 session')
    channel = header[24 + track_no]
    if not 1 <= channel <= 16:
        raise ValueError('the selected track has no MIDI input channel')
    if any(header[24 + i] == channel for i in range(7) if i != track_no):
        raise ValueError('the selected MIDI channel is shared with another track or drum lane')
    if not (52 <= track[0] < 76):
        raise ValueError('the selected track is not using a melodic Plaits model')
    poly = track[108] == 2 or (track[108] == 0 and header[21] == 1)
    if notes == 6 and (not poly or header[20]):
        raise ValueError('enable live polyphony on this track and turn ARP off for the six-note test')
    lfo1, lfo2 = track[28:32], track[36:40]
    model_active = any(row[2] and row[3] == MODEL_TARGET for row in (lfo1, lfo2, global_fields))
    if baseline and model_active:
        raise ValueError('disable all MODEL modulation for a baseline measurement')
    if not baseline and not model_active:
        raise ValueError('set an LFO target to MODEL with nonzero depth before measuring')
    return channel


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='FM-1 JamesOS')
    parser.add_argument('--notes', type=int, choices=(1, 6), required=True)
    parser.add_argument('--seconds', type=float, default=10.0)
    parser.add_argument('--baseline', action='store_true', help='measure with MODEL modulation disabled')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('--seconds must be between 1 and 60')
    if args.output.exists():
        parser.error('output already exists')
    link = Link(args.port)
    sent = []
    try:
        current = identity(link)
        if current != EXPECTED:
            raise RuntimeError(f'expected {EXPECTED}, found {current!r}; no notes sent')
        state = where(link)
        if state['playing'] or state['recording'] or state['save_open'] or state['buttons_held'] or state['track'] >= 3:
            raise RuntimeError('stop playback and recording, close SAVE, and select a synth track')
        header = read_chunk(link, 0, 32)
        global_fields = read_chunk(link, 32, 4)
        offset = 76 + 112 * state['track']
        track = b''.join(read_chunk(link, offset + i, min(32, 112 - i)) for i in range(0, 112, 32))
        channel = validate_snapshot(header, global_fields, track, state['track'], args.notes, args.baseline)
        before = metrics(link)
        if command(link, 0x61, b'BR\x01\x00') is None:
            raise RuntimeError('audio counter reset refused; no notes sent')
        for note in NOTES[:args.notes]:
            link.out.send(link.mido.Message('note_on', channel=channel - 1, note=note, velocity=96))
            sent.append(note)
        time.sleep(args.seconds)
        after = metrics(link)
        diagnostics = command(link, 0x9b)
        result = {'identity': current, 'track': state['track'] + 1, 'channel': channel,
                  'notes': args.notes, 'seconds': args.seconds, 'baseline': args.baseline, 'before': before, 'after': after,
                  'job_timing_hex': diagnostics.hex() if diagnostics else None}
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
    finally:
        try:
            for note in sent:
                try:
                    link.out.send(link.mido.Message('note_off', channel=channel - 1, note=note, velocity=0))
                except Exception as exc:
                    print(f'could not release note {note}: {exc}')
        finally:
            link.close()


if __name__ == '__main__':
    main()
