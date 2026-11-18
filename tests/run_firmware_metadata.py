#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check firmware.json (fm1-base firmware.json schema 1) and its agreement with fmhub.json.

    python3 tests/run_firmware_metadata.py

firmware.json is read by firmware hubs (fm1-base docs/FIRMWARE-JSON.md, schema 1, fm1-base
commit f66ac4a6af0d45e672e06fffad05767eb7b2901f); fmhub.json is read by The Firmware Hub.
Both sit at the repository root. This checks: the file parses; the required fields (schema,
id, name) and the type and pattern of every field schema 1 defines; that it carries no
version (schema 1 describes the firmware, not a release, so the build number stays in
tools/build.py only); and that the facts shared with fmhub.json agree:
author = author, description = what, screenshot = image. The screenshot must be a
committed square PNG, at least 240 px, at most 2 MiB. Host only; no hardware, no build.
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

TAGS = {'synth', 'drum-machine', 'groovebox', 'sequencer', 'sampler', 'controller', 'midi', 'chords',
        'arranger', 'ambient', 'generative', 'fm', 'game', 'utility', 'general-midi', 'experimental'}
FEATURES = {'bluetooth-midi', 'bluetooth-midi-devices', 'bluetooth-audio', 'usb-audio-in', 'usb-audio-out',
            'usb-midi-host', 'usb-disk', 'wifi', 'second-core', 'velocity', 'pressure', 'screen-reader',
            'screen-mirror', 'backup', 'resources'}
KNOWN = {'schema', 'id', 'name', 'author', 'license', 'tagline', 'description', 'tags', 'install',
         'site', 'source', 'screenshot', 'hardware', 'credits'}
SCREENSHOT = re.compile(r'^(?!(.*/)?\.\.?(/|$))[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*\.png$')
PRODUCTS = {'FM-1', 'SMK-37 Pro'}   # tools/build.py --target fm1|smk37

errors = []


def check(cond, msg):
    if not cond:
        errors.append(msg)


def text(d, key, required=False):
    if key not in d:
        check(not required, f'missing required {key!r}')
        return None
    v = d[key]
    check(isinstance(v, str) and len(v) >= 1, f'{key}: must be a non-empty string')
    return v if isinstance(v, str) else None


def str_list(d, key, where, vocab=None, pattern=None):
    if key not in d:
        return None
    v = d[key]
    if not isinstance(v, list) or not all(isinstance(x, str) and x for x in v):
        errors.append(f'{where}{key}: must be a list of non-empty strings')
        return None
    check(len(set(v)) == len(v), f'{where}{key}: items must be unique')
    for x in v:
        check(vocab is None or x in vocab, f'{where}{key}: {x!r} not in the schema 1 vocabulary')
        check(pattern is None or re.fullmatch(pattern, x), f'{where}{key}: {x!r} does not match {pattern}')
    return v


def png_size(path):
    head = path.read_bytes()[:24]
    if head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        return None
    return struct.unpack('>II', head[16:24])


def main():
    raw = (ROOT / 'firmware.json').read_bytes()
    fw = json.loads(raw.decode('utf-8'))
    hub = json.loads((ROOT / 'fmhub.json').read_text(encoding='utf-8'))
    check(isinstance(fw, dict), 'top level must be an object')
    check(raw.endswith(b'\n'), 'file must end with a newline')

    check(fw.get('schema') == 1 and type(fw.get('schema')) is int, 'schema: must be 1')
    fid = text(fw, 'id', required=True)
    check(fid is None or re.fullmatch(r'[a-z0-9][a-z0-9_-]*', fid), f'id: {fid!r} bad pattern')
    text(fw, 'name', required=True)
    for k in ('author', 'license', 'tagline', 'credits'):
        text(fw, k)
    desc = text(fw, 'description')
    check(desc is None or (len(desc) <= 600 and '\n' not in desc), 'description: one line, <= 600 chars')
    str_list(fw, 'tags', '', vocab=TAGS)
    for k in ('install', 'site', 'source'):
        v = text(fw, k)
        check(v is None or v.startswith('https://'), f'{k}: must start with https://')
    for k in fw:
        check(k in KNOWN or k.startswith('x-'), f'unknown key {k!r} (schema 1 keys or x- only)')
    check(not any(k in fw for k in ('version', 'build')), 'schema 1 has no version; do not add one')

    hw = fw.get('hardware')
    if hw is not None:
        check(isinstance(hw, dict), 'hardware: must be an object')
        hw = hw if isinstance(hw, dict) else {}
        models = str_list(hw, 'models', 'hardware.') or []
        check(set(models) <= PRODUCTS, f'hardware.models: {models} not products this tree builds')
        str_list(hw, 'flash', 'hardware.', pattern=r'[0-9A-F]{6}')
        str_list(hw, 'features', 'hardware.', vocab=FEATURES)
        for k in hw:
            check(k in ('models', 'flash', 'features') or k.startswith('x-'), f'hardware: unknown key {k!r}')

    shot = text(fw, 'screenshot')
    if shot is not None:
        check(SCREENSHOT.fullmatch(shot), f'screenshot: {shot!r} bad path')
        p = ROOT / shot
        size = png_size(p) if p.is_file() else None
        check(size is not None, f'screenshot: {shot} is not a PNG in the repository')
        if size:
            check(size[0] == size[1] and size[0] >= 240, f'screenshot: {size} not square >= 240')
            check(p.stat().st_size <= 2 * 1024 * 1024, 'screenshot: over 2 MiB')

    # Facts shared with fmhub.json: one wording, so the two listings cannot disagree.
    for fk, hk in (('author', 'author'), ('description', 'what'), ('screenshot', 'image')):
        check((fk in fw) == (hk in hub), f'{fk} / fmhub {hk}: present in one file only')
        if fk in fw and hk in hub:
            check(fw[fk] == hub[hk], f'{fk} differs from fmhub.json {hk}')

    for e in errors:
        print('FAIL', e)
    if errors:
        print(f'firmware metadata: {len(errors)} problem(s)')
        return 1
    print(f'firmware metadata: firmware.json schema 1 valid ({len(fw)} keys), agrees with fmhub.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
