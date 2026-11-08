#!/usr/bin/env python3
"""Compile the actual preflight function with deterministic peripheral fakes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/src/instrument_audio.c").read_text()
start = source.index("static uint32_t br_audio_source_c1_late_preflight(")
opening = source.index("{", start)
depth = 0
for position in range(opening, len(source)):
    depth += (source[position] == "{") - (source[position] == "}")
    if depth == 0:
        function = source[start:position + 1]
        break
else:
    raise AssertionError("unterminated preflight")
with tempfile.TemporaryDirectory(prefix="fm1-preflight-retry-") as tmp:
    directory = Path(tmp)
    (directory / "preflight_under_test.inc").write_text(function + "\n")
    for opt in ("-O0", "-O2"):
        binary = directory / ("retry" + opt[1:])
        subprocess.run(["cc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror",
                        "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
                        "-I", str(directory), "-I", str(root / "firmware/hal"),
                        str(root / "tests/test_preflight_retry.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
