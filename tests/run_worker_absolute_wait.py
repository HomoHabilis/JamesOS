#!/usr/bin/env python3
"""Compile the actual target wait functions against scripted mailbox/T4 I/O."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/hal/fm1_cpu1_worker_target.c").read_text()


def function(prefix):
    start = source.index(prefix)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[start:pos + 1]
    raise AssertionError(f"unterminated function: {prefix}")


with tempfile.TemporaryDirectory(prefix="fm1-absolute-wait-") as temp:
    directory = Path(temp)
    snippets = ["static uint32_t worker_wait_absolute(",
                "uint32_t fm1_cpu1_worker_poll_until(",
                "uint32_t fm1_cpu1_worker_wait_until(",
                "void fm1_cpu1_worker_note_terminal_timeout("]
    recorder = function(snippets[-1])
    assert "done_seq" not in recorder and "fm1_ticks(" not in recorder
    assert "fm1_cpu1_worker_abort_hold_reset(" not in recorder
    (directory / "worker_wait_extracted.inc").write_text(
        "\n\n".join(function(s) for s in snippets) + "\n"
    )
    for enabled in (0, 1):
        for optimization in ("-O0", "-O2"):
            binary = directory / (f"worker_wait_{enabled}_" + optimization[1:])
            subprocess.run(["cc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            f"-DFM1_DEVELOPMENT_DIAGNOSTICS={enabled}",
                            "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
                            "-I", str(directory), "-I", str(root / "firmware/hal"),
                            str(root / "tests/test_worker_absolute_wait.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
