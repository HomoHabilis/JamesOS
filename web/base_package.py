#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch the JamesOS base package that the installer writes over the official firmware.

  web/base_package.py DEST.fwsc

The base is upstream's build 2108 combined "from V15" package, which carries
the 396 MHz boot configuration. It is no longer tracked in this tree; it is
read from the last commit that held it and accepted only with its published
SHA-256. An existing DEST with that digest is kept.
"""
import hashlib
import sys
import urllib.request
from pathlib import Path

URL = ("https://raw.githubusercontent.com/HomoHabilis/JamesOS/4e25c583990bda210cf4e519a31c254b83b9b17e/"
       "releases/JamesOS-1.0a-build-2108-from-v15.fwsc")
SHA256 = "75c0ea9251db9ab08d3886bfc784a7ddd1638a679257b16f714975cff597c729"


def fetch(dest):
    """Write the base package to dest, verified; returns its bytes."""
    dest = Path(dest)
    if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() == SHA256:
        return dest.read_bytes()
    with urllib.request.urlopen(URL, timeout=60) as r:
        raw = r.read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SHA256:
        raise SystemExit(f"{URL}: SHA-256 {digest}, expected {SHA256}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(raw)
    return raw


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fetch(sys.argv[1])
    print(f"{sys.argv[1]}: build 2108 from-V15 base, {SHA256}")
