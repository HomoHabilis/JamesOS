#!/usr/bin/env python3
"""Release tag selection (tools/release_tag.py); no git or network."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from release_tag import pick  # noqa: E402

A, B = "a" * 40, "b" * 40


def fails(*args):
    try:
        pick(*args)
    except ValueError:
        return True
    return False


assert pick("", A, {}) == ("v1.0a-midi-2150", A, True)
assert pick("", B, {"v1.0a-midi-2150": A}) == ("v1.0a-midi-2151", B, True)
assert pick("", A, {"v1.0a-midi-2150": A}) == ("v1.0a-midi-2150", A, False)
assert pick("", A, {"v1.0a-midi-2150": A, "v1.0a-midi-2153": A}) == ("v1.0a-midi-2153", A, False)
assert pick("", B, {"v1.1-midi-2160": A, "v1.0a-midi-2150": A}) == ("v1.1-midi-2161", B, True)
assert pick("v1.0a-midi-2150", B, {"v1.0a-midi-2150": A}) == ("v1.0a-midi-2150", A, False)
assert pick("v1.1-midi-2200", B, {"v1.0a-midi-2150": A}) == ("v1.1-midi-2200", B, True)
assert fails("v1.1-midi-2150", B, {"v1.0a-midi-2150": A})
assert fails("v1.0a-2108", B, {})
assert fails("1.0a-midi-2150", B, {})
assert fails("v1.0a-midi-215", B, {})
print("release tag selection passed")
