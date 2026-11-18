#!/usr/bin/env python3
"""Run the pair-store deletion safety regression on a host compiler."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "project_delete_safety"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DFM1_PAIR_STREAM_HOST_TEST=1", "-Ifirmware/src",
        "tests/project_delete_safety.c", "firmware/src/pattern_pair_stream.c",
        "-o", str(binary),
    ], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)

with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "project_delete_owner"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DFM1_PAIR_STREAM_HOST_TEST=1", "-Ifirmware/src",
        "tests/project_delete_owner.c", "firmware/src/pattern_pair_stream.c",
        "firmware/src/pattern_pair_owner.c", "firmware/src/pattern_park_io.c",
        "firmware/src/pattern_sha256.c", "-o", str(binary),
    ], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)

with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "project_delete_gate"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Ifirmware/src", "tests/project_delete_gate.c",
        "firmware/src/project_delete_gate.c", "-o", str(binary),
    ], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)

with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "project_delete_protocol"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Ifirmware/src", "tests/project_delete_protocol.c",
        "firmware/src/project_delete_protocol.c", "firmware/src/project_delete_gate.c",
        "-o", str(binary),
    ], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
