#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pick or name the tag of a release of this fork, and the build identity it implies.

  tools/release_tag.py [--tag TAG] [--commit SHA]

Tags look like v1.0a-midi-2150: the JamesOS version, then the build number,
which is also the package identity (FM-1_926 + build, here FM-1_9262150).

- With --tag: an existing tag is released as it is (its own commit); a new one
  is created on --commit (default HEAD).
- Without --tag: a release tag already on --commit is reused; otherwise the next
  build after the highest existing one (at least 2150, above upstream's 2108).

Prints key=value lines for $GITHUB_OUTPUT: tag, version, build, product, commit, create.
"""
import argparse
import re
import subprocess
import sys

TAG = re.compile(r"v(?P<version>\d+\.\d+[a-z]?)-midi-(?P<build>\d{4})")
FIRST_BUILD = 2150
DEFAULT_VERSION = "1.0a"


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout.strip()


def tags_with_commits():
    """{tag: commit} for every release tag in the repository."""
    out = git("for-each-ref", "--format=%(refname:short) %(objectname) %(*objectname)", "refs/tags")
    tags = {}
    for line in out.splitlines():
        name, obj, peeled = (line.split() + [""])[:3]
        if TAG.fullmatch(name):
            tags[name] = peeled or obj
    return tags


def pick(requested, commit, tags):
    """The release to make: (tag, commit, create)."""
    if requested:
        if not TAG.fullmatch(requested):
            raise ValueError(f"tag {requested!r} does not look like v1.0a-midi-2150")
        if requested in tags:
            return requested, tags[requested], False
        build = TAG.fullmatch(requested)["build"]
        taken = [t for t in tags if TAG.fullmatch(t)["build"] == build]
        if taken:
            raise ValueError(f"build {build} is already released as {taken[0]}")
        return requested, commit, True
    here = sorted((t for t, c in tags.items() if c == commit), key=lambda t: int(TAG.fullmatch(t)["build"]))
    if here:
        return here[-1], commit, False
    if tags:
        latest = max(tags, key=lambda t: int(TAG.fullmatch(t)["build"]))
        build = max(FIRST_BUILD, int(TAG.fullmatch(latest)["build"]) + 1)
        version = TAG.fullmatch(latest)["version"]
    else:
        build, version = FIRST_BUILD, DEFAULT_VERSION
    if build > 9999:
        raise ValueError("no build numbers left")
    return f"v{version}-midi-{build}", commit, True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", default="")
    ap.add_argument("--commit", default="HEAD")
    a = ap.parse_args()
    try:
        tag, commit, create = pick(a.tag.strip(), git("rev-parse", a.commit + "^{commit}"), tags_with_commits())
    except ValueError as e:
        sys.exit(f"release_tag: {e}")
    m = TAG.fullmatch(tag)
    print(f"tag={tag}\nversion={m['version']}\nbuild={m['build']}\nproduct=FM-1_926{m['build']}\n"
          f"commit={commit}\ncreate={'true' if create else 'false'}")


if __name__ == "__main__":
    main()
