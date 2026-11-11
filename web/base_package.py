#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch the packages the installer page writes but this tree does not track.

  web/base_package.py DEST.fwsc            the JamesOS base package
  web/base_package.py --v15 DEST.fwsc      the official FM-1 V15

The base is upstream's build 2108 combined "from V15" package, which carries
the 396 MHz boot configuration, read from the last commit of this repository
that held it. The official V15 is FM-1_015.fwsc as M-VAVE publishes it in
m-vave/fm1_open under Apache-2.0 (web/licenses/). Each is read at a fixed
commit and accepted only with its SHA-256. An existing DEST with that digest
is kept.
"""
import hashlib
import sys
import urllib.request
from pathlib import Path

URL = ("https://raw.githubusercontent.com/HomoHabilis/JamesOS/4e25c583990bda210cf4e519a31c254b83b9b17e/"
       "releases/JamesOS-1.0a-build-2108-from-v15.fwsc")
SHA256 = "75c0ea9251db9ab08d3886bfc784a7ddd1638a679257b16f714975cff597c729"

V15_URL = ("https://raw.githubusercontent.com/m-vave/fm1_open/3be7fbdbd3bb137bacbd65d68412c210eb977870/"
           "sdk/firmware/FM-1_015.fwsc")
V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"


def fetch(dest, url=URL, sha256=SHA256):
    """Write the package at url to dest, verified; returns its bytes."""
    dest = Path(dest)
    if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() == sha256:
        return dest.read_bytes()
    with urllib.request.urlopen(url, timeout=60) as r:
        raw = r.read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != sha256:
        raise SystemExit(f"{url}: SHA-256 {digest}, expected {sha256}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(raw)
    return raw


def fetch_v15(dest):
    """Write the official V15 to dest, verified; returns its bytes."""
    return fetch(dest, V15_URL, V15_SHA256)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--v15"] and len(args) == 2:
        fetch_v15(args[1])
        print(f"{args[1]}: official FM-1 V15, {V15_SHA256}")
    elif len(args) == 1 and not args[0].startswith("-"):
        fetch(args[0])
        print(f"{args[0]}: build 2108 from-V15 base, {SHA256}")
    else:
        sys.exit(__doc__)
