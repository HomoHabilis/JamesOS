#!/usr/bin/env python3
"""Run the actual app command handler with staged host dependencies."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--diagnostics', choices=('development', 'distribution'), default='development')
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
objects = sorted((root / "build/host/braids").glob("*.o"))
objects += sorted((root / "build/host/plaits_reference").glob("*.o"))
if not objects:
    raise SystemExit("Missing host DSP objects; run tools/build_braids_host.sh first")
with tempfile.TemporaryDirectory(prefix="fm1-session-protection-") as tmp:
    tmp = Path(tmp)
    shutil.copytree(root / "firmware", tmp / "firmware", copy_function=os.link)
    shutil.copytree(root / "assets", tmp / "assets", copy_function=os.link)
    (tmp / "tests").mkdir()
    shutil.copy2(root / "tests/host.c", tmp / "tests/host.c")
    shutil.copy2(root / "tests/test_session_protection.c", tmp / "tests/test_session_protection.c")
    flags = ["-std=c11", "-O2", "-fwrapv", "-Wno-unused-function",
             "-DFM1_DEVELOPMENT_DIAGNOSTICS=" + ("1" if args.diagnostics == "development" else "0"),
             "-DFM1_FAULT_RAM_CODE_CAPTURE_READ=" + ("1" if args.diagnostics == "development" else "0"),
             "-DFM1_ENABLE_P33_REGISTER_SNAPSHOT=" + ("1" if args.diagnostics == "development" else "0"),
             "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0",
             "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0",
             "-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1",
             "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
             "-DBR_AUDIO_INTERNAL_BLOCK160=0", "-DBR_AUDIO_INTERNAL_BLOCK256=1",
             "-DPLAITS_INTERNAL_SAMPLE_RATE=44100", "-DFM1_PLAITS_SOURCE_RATE_32K=0",
             "-DFM1_PLAITS_SOURCE_RATE_44100=1", "-DFM1_PLAITS_SRC_LAGRANGE4=0",
             "-DBR_FX_RATE_SIGNED_441_RECIP=0", "-DBR_FX_TAPE_WOW_DELTA_LUT=0"]
    binary = tmp / "session-protection"
    subprocess.run(["c++", "-x", "c", *flags, "-I" + str(tmp / "firmware/src"),
                    "-I" + str(tmp / "firmware/gen"), "-o", str(binary),
                    str(tmp / "tests/test_session_protection.c"),
                    str(tmp / "firmware/src/audio_fixed8_plan_unit.c"), "-x", "none",
                    *map(str, objects)], check=True)
    subprocess.run([str(binary)], check=True)
