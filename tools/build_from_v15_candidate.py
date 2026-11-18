#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Combine a validated FM-1 app package with a validated V15 boot package."""
import argparse
import hashlib
import json
from pathlib import Path
from build_v15_install_package import read_files
from fm1pkg_make import ufw
from validate_package import validate


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--app-package', type=Path, required=True)
    ap.add_argument('--boot-package', type=Path, required=True)
    ap.add_argument('--identity', required=True)
    ap.add_argument('--boot-sha256', required=True, help='expected validated boot package SHA-256')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    app_raw = args.app_package.read_bytes()
    boot_raw = args.boot_package.read_bytes()
    if hashlib.sha256(boot_raw).hexdigest() != args.boot_sha256.lower():
        raise ValueError('boot package SHA-256 differs from the expected source')
    app_info = validate(app_raw, expected_identity=args.identity)
    boot_info = validate(boot_raw)
    if boot_info['loader_identity_source'] != 'pinned V15 loader; hardware-observed':
        raise ValueError('boot package does not contain the pinned V15 loader')
    app = read_files(args.app_package, hashlib.sha256(app_raw).hexdigest())
    boot = read_files(args.boot_package, hashlib.sha256(boot_raw).hexdigest())
    flash = boot[0][:0x4000] + app[0][0x4000:]
    data = ufw(flash, boot[100], args.identity)
    result = validate(data, expected_identity=args.identity)
    args.output.write_bytes(data)
    print(json.dumps({'app': app_info['sha256'], 'boot_source': boot_info['sha256'],
                      'output': result['sha256'], 'identity': args.identity}, indent=2))

if __name__ == '__main__':
    main()
