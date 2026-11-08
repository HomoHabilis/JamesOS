#!/usr/bin/env python3
"""Compile the actual target delay/readiness/ops functions into a host fixture."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/hal/fm1_vendor360_target.h").read_text()


def function(signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if not depth:
                return source[start : i + 1]
    raise ValueError(f"unclosed target function: {signature}")


with tempfile.TemporaryDirectory(prefix="fm1-396-delay-") as temp:
    directory = Path(temp)
    extracted = directory / "target_396_delay_extracted.h"
    eligibility, count = re.subn(
        r"\*\(volatile uint32_t \*\)\(uintptr_t\)(0x[0-9a-fA-F]+u)",
        r"test_mmio_read(\1)", function("static int fm1_vp_target_eligible("))
    if count != 5:
        raise ValueError(f"expected five target clock MMIO reads, got {count}")
    extracted.write_text("\n\n".join((
        function("static int fm1_vp_target_handoff_loop("),
        function("static int fm1_vp_target_timer("),
        eligibility,
        function("static fm1_vp_io_t fm1_vp_target_ops("),
    )) + "\n")
    for optimization in ("-O0", "-O2"):
        binary = directory / ("delay-test-" + optimization[1:])
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        optimization, "-I", str(directory),
                        str(root / "tests/test_396_target_delay.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
