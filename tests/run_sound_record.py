#!/usr/bin/env python3
"""Build and run the portable sound-record tests; no device access."""
from pathlib import Path
import subprocess
import tempfile
import host_target

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="fm1-sound-test-") as temp:
        for target in ("fm1", "smk37"):
            binary = Path(temp) / "sound-record-test"
            env = host_target.environment(target, host_target.poly())
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-DDINGHY_HOST",
                "-Ifirmware/src", "firmware/src/sound_record.c",
                "tests/sound_record_test.c", "-o", str(binary),
            ], cwd=ROOT, env=env, check=True)
            print(f"{target}: ", end="", flush=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
