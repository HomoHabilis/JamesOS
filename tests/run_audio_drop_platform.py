#!/usr/bin/env python3
"""Compile and exercise the actual platform wrapper and IRQ with fake I/O."""
from pathlib import Path
import subprocess
import tempfile
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--diagnostics', choices=('development', 'distribution'), default='development')
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/src/platform.c").read_text()


def function(signature):
    start = source.index(signature + "\n{")
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[start : pos + 1]
    raise AssertionError(f"Unterminated function: {signature}")


signatures = (
    "static uint32_t platform_audio_render_half(int32_t *out, uint32_t frames)",
    "static void fm1_audio_metrics_reset(void)",
    "void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */",
)
with tempfile.TemporaryDirectory(prefix="fm1-drop-platform-") as temp:
    directory = Path(temp)
    parts = [function(s) for s in signatures]
    parts[1] = '#if FM1_DEVELOPMENT_DIAGNOSTICS\n' + parts[1] + '\n#endif'
    (directory / "audio_drop_extracted.inc").write_text("\n\n".join(parts) + "\n")
    for optimization in ("-O0", "-O2"):
        executable = directory / ("audio_drop_platform_" + optimization[1:])
        subprocess.run(
            ["cc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-variable", "-Wno-unused-function",
             "-DFM1_DEVELOPMENT_DIAGNOSTICS=" + ("1" if args.diagnostics == "development" else "0"),
             "-I", str(directory), "-I",
             str(root / "firmware/hal"),
             str(root / "tests/test_audio_drop_platform.c"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
