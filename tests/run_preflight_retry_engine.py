#!/usr/bin/env python3
"""Run the real source queue/worker with a shortened fake stalled-T4 cap."""
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
flags = ["-std=gnu11", "-O2", "-pthread", "-fwrapv", "-ffunction-sections",
         "-fdata-sections", "-DBR_AUDIO_C1_PREFLIGHT_SPINS=8u",
         "-DBR_AUDIO_INTERNAL_BLOCK160=0", "-DBR_AUDIO_INTERNAL_BLOCK256=1",
         "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
         "-DPLAITS_INTERNAL_SAMPLE_RATE=44100", "-DFM1_PLAITS_SOURCE_RATE_32K=0",
         "-DFM1_PLAITS_SOURCE_RATE_44100=1", "-DFM1_PLAITS_SRC_LAGRANGE4=0",
         "-DBR_FX_RATE_SIGNED_441_RECIP=0", "-DBR_FX_TAPE_WOW_DELTA_LUT=0",
         "-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=1"]
if "--owner-off" in sys.argv:
    flags.remove("-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=1")
    flags += ["-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0",
              "-DBR_AUDIO_FUSION_EWMA_SCHEDULER=0",
              "-DBR_AUDIO_FUSION_JOB_TIMING_DIAG=0",
              "-DBR_AUDIO_FUSION_ACTIVITY_EWMA=0",
              "-DBR_AUDIO_FUSION_ROBUST_COST_PREDICTOR=0",
              "-DBR_AUDIO_FUSION_FIXED8_HISTORY_PLAN=0",
              "-DBR_AUDIO_FUSION_KIT_VOICE_SIGNATURE=0",
              "-DBR_AUDIO_FUSION_BRAIDS_LANE_HISTORY=0",
              "-DBR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE=0",
              "-DBR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE=0",
              "-DBR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE=0",
              "-DBR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE=0",
              "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0",
              "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0"]
elif "--profile-on" in sys.argv:
    # Contrast the recorder-on 1492 profile using the same production source.
    flags += ["-DBR_TEST_PROFILE_ON_ASSERT=1",
              "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=1",
              "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=1",
              "-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1"]
else:
    # Match the release flags explicitly; the fixture checks the selected
    # owner/EWMA gate after including the real audio translation unit.
    flags += ["-DBR_RELEASE_PROFILE_ASSERT=1",
              "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0",
              "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0",
              "-DBR_AUDIO_FUSION_EWMA_SCHEDULER=1"]
linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
with tempfile.TemporaryDirectory(prefix="fm1-preflight-engine-") as temp:
    binary = Path(temp) / "engine"
    subprocess.run(["cc", *flags, linker, str(root / "tests/test_preflight_retry_engine.c"),
                    "-o", str(binary), "-lm"], check=True)
    subprocess.run([str(binary)], check=True)
