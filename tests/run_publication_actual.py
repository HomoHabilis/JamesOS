#!/usr/bin/env python3
"""Compile the real audio unit with deterministic host timer/mailbox hooks."""
from pathlib import Path
import platform
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = ["-std=gnu11", "-pthread", "-fwrapv", "-ffunction-sections",
         "-fdata-sections", "-DBR_PUBLICATION_TEST=1",
         "-DBR_AUDIO_RETIRE_LOCK_SPINS=8u", "-DBR_AUDIO_C1_PREFLIGHT_SPINS=8u",
         "-DBR_AUDIO_INTERNAL_BLOCK160=0", "-DBR_AUDIO_INTERNAL_BLOCK256=1",
         "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
         "-DPLAITS_INTERNAL_SAMPLE_RATE=44100", "-DFM1_PLAITS_SOURCE_RATE_32K=0",
         "-DFM1_PLAITS_SOURCE_RATE_44100=1", "-DFM1_PLAITS_SRC_LAGRANGE4=0",
         "-DBR_FX_RATE_SIGNED_441_RECIP=0", "-DBR_FX_TAPE_WOW_DELTA_LUT=0",
         "-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=1", "-DBR_RELEASE_PROFILE_ASSERT=1",
         "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0",
         "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0",
         "-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1"]
linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
with tempfile.TemporaryDirectory(prefix="fm1-publication-") as temp:
    for optimization in ("-O0", "-O2"):
        binary = Path(temp) / ("test" + optimization[1:])
        subprocess.run(["cc", optimization, *flags, linker,
                        str(root / "tests/test_publication_actual.c"),
                        "-o", str(binary), "-lm"], check=True)
        subprocess.run([str(binary)], check=True)
