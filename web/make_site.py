#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the installer site (GitHub Pages):

  index.html            index_pkg.html with fm1pkg.js, fm1ota.js, installer.js and the metadata inlined
  firmware/APP.fwsc     this build's application update package
  firmware/BASE.fwsc    the combined "from V15" package that writes the boot configuration

  web/make_site.py APP.fwsc VERSION OUT_DIR [--base BASE.fwsc] [--source URL]

The page installs only bytes that match the identities and SHA-256 digests
recorded here. The base package is upstream's build 2108 from-V15 package
(web/base_package.py): fetched into build/ when --base is not given, and
accepted only with its published digest.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "tools"))
import validate_package  # noqa: E402
import base_package  # noqa: E402

BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
IDENTITY = re.compile(r"FM-1_9262\d{3}")


def strip_module(src):
    """A module's source for inlining: no export keywords, no imports."""
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """The package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)."""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def entry(raw, name, **more):
    return {"product": product_of(raw), "pkg": "firmware/" + name,
            "sha256": hashlib.sha256(raw).hexdigest(), **more}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("app", type=Path)
    ap.add_argument("version")
    ap.add_argument("out", type=Path)
    ap.add_argument("--base", type=Path, default=ROOT / "build" / "base" / "jamesos-base-from-v15.fwsc")
    ap.add_argument("--v15", type=Path, default=ROOT / "build" / "base" / "FM-1_015.fwsc")
    ap.add_argument("--source", default="https://github.com/aarongrando/JamesOS")
    a = ap.parse_args()

    app = a.app.read_bytes()
    product = product_of(app)
    if not IDENTITY.fullmatch(product):
        raise SystemExit(f"{a.app}: identity {product!r} is not a JamesOS package (FM-1_9262xxx)")
    loader = validate_package.validate(app, expected_identity=product).get("loader_identity")
    if not isinstance(loader, str) or not re.fullmatch(r"ota-FM-1_9\d\d", loader):
        raise SystemExit(f"{a.app}: validation did not identify the embedded update loader")
    base = base_package.fetch(a.base)
    if hashlib.sha256(base).hexdigest() != base_package.SHA256 or not IDENTITY.fullmatch(product_of(base)):
        raise SystemExit(f"{a.base}: not the released build 2108 from-V15 package")
    v15 = base_package.fetch_v15(a.v15)

    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8"))
                    for f in ("fm1pkg.js", "fm1ota.js", "installer.js"))
    app_name = f"jamesos-{re.sub(r'[^A-Za-z0-9.-]', '-', a.version)}.fwsc"
    base_name = "jamesos-base-from-v15.fwsc"
    v15_name = "FM-1_015.fwsc"
    meta = json.dumps({"version": a.version, "source": a.source,
                       "app": entry(app, app_name, loader=loader), "base": entry(base, base_name),
                       "stock": entry(v15, v15_name)})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)

    fw = a.out / "firmware"
    fw.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("*.fwsc"):
        old.unlink()
    (fw / app_name).write_bytes(app)
    (fw / base_name).write_bytes(base)
    (fw / v15_name).write_bytes(v15)
    licenses = a.out / "licenses"
    licenses.mkdir(exist_ok=True)
    for f in (HERE / "licenses").glob("*.txt"):
        (licenses / f.name).write_bytes(f.read_bytes())
    (a.out / "index.html").write_text(html, encoding="utf-8")
    (a.out / ".nojekyll").write_text("", encoding="utf-8")
    print(f"site: {a.out}: index.html ({len(html)} B), {app_name} ({len(app)} B, {product}, {loader}), {base_name}, {v15_name} with its licence")


if __name__ == "__main__":
    main()
