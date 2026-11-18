#!/usr/bin/env python3
"""Check the exact development 1.0a migration bridge package; no device access.

Passing confirms the file identity and structure, not update-path safety or
permission to distribute the package.
"""
import argparse
import hashlib
from pathlib import Path
import sys
import validate_package

BRIDGE_HASH = '32c1fb8fea18df5b97b6c10e8b0ec400124a7e13ab6d402f7a6f9a80c28eeba5'
BRIDGE_ID = 'FM-1_9673046'


def verify(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != BRIDGE_HASH:
        raise ValueError('Bridge package SHA-256 does not match the pinned development build')
    result = validate_package.validate(data, expected_identity=BRIDGE_ID)
    if result['loader_identity'] != 'ota-FM-1_904':
        raise ValueError('Unexpected update loader identity')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('package', type=Path)
    args = ap.parse_args()
    try:
        result = verify(args.package)
    except (OSError, ValueError) as error:
        print('Bridge check failed:', error, file=sys.stderr)
        return 1
    print('Verified development bridge:', result['identity'])
    print('SHA-256:', result['sha256'])
    print('This does not qualify installation or recovery from stock 1.0a.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
