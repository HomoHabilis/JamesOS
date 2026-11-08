#!/usr/bin/env python3
"""Compare two real DSP/FX frames with clean versus poisoned abandoned scratch.

Builds candidate host objects in a temporary directory and runs each case in a
fresh process. This verifies reconstruction, not target IRQ or DMA timing.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import platform
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
fw = root / "firmware"
profile = ["-DBR_AUDIO_INTERNAL_BLOCK160=0", "-DBR_AUDIO_INTERNAL_BLOCK256=1",
           "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0",
           "-DPLAITS_INTERNAL_SAMPLE_RATE=44100", "-DFM1_PLAITS_SOURCE_RATE_32K=0",
           "-DFM1_PLAITS_SOURCE_RATE_44100=1", "-DFM1_PLAITS_SOURCE_RATE_38400=0",
           "-DFM1_PLAITS_SOURCE_RATE_48000=0", "-DFM1_PLAITS_SRC_LAGRANGE4=0",
           "-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0", "-DFM1_PLAITS_PHASE_LUT=0",
           "-DBR_FX_RATE_SIGNED_441_RECIP=0", "-DBR_FX_TAPE_WOW_DELTA_LUT=0",
           "-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0", "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0",
           "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0"]
plaits = [fw / "plaits/plaits_c_api.cc", fw / "plaits/plaits_kit_c_api.cc"]
plaits += sorted((fw / "plaits/upstream/plaits").rglob("*.cc"))
plaits += sorted((fw / "plaits/upstream/stmlib").rglob("*.cc"))
braids = [fw / "braids/braids_c_api.cc", fw / "braids/braids_perc_c_api.cc"]
braids += [fw / "braids/upstream/braids" / (name + ".cc") for name in
           ("analog_oscillator", "digital_oscillator", "macro_oscillator", "resources")]
includes = ["-I" + str(fw / path) for path in
            ("src", "gen", "plaits", "plaits/upstream", "braids", "braids/upstream")]

with tempfile.TemporaryDirectory(prefix="fm1-partial-frame-") as temp:
    directory = Path(temp)
    jobs = [(i, path, True) for i, path in enumerate(plaits)]
    jobs += [(len(plaits) + i, path, False) for i, path in enumerate(braids)]

    def compile_cpp(job):
        index, path, is_plaits = job
        output = directory / f"{index:03d}.o"
        extra = ["-DTEST", "-Dstmlib=plaits_stmlib"] if is_plaits else []
        subprocess.run(["c++", "-std=c++14", "-O2", "-fwrapv", "-ffp-contract=off",
                        "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                        *profile, *extra, *includes, "-c", str(path), "-o", str(output)],
                       check=True)
        return output

    with ThreadPoolExecutor(max_workers=6) as pool:
        objects = list(pool.map(compile_cpp, jobs))
    for source, name in ((fw / "src/audio_fixed8_plan_unit.c", "fixed8.o"),):
        target = directory / name
        subprocess.run(["cc", "-std=c11", "-O2", "-fwrapv", *profile, *includes,
                        "-c", str(source), "-o", str(target)], check=True)
        objects.append(target)
    binary = directory / "partial_frame"
    linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
    subprocess.run(["c++", "-x", "c", "-std=gnu11", "-O2", "-fwrapv", "-pthread",
                    "-ffunction-sections", "-fdata-sections", *profile, *includes,
                    str(root / "tests/test_partial_frame_reconstruction.c"),
                    "-x", "none", *map(str, objects), linker, "-o", str(binary)], check=True)
    for kit in (48, 49, 50, 76):  # BRAIDS, 808, 909, and Plaits kits
        for lane in (0, 3):
            for pattern in (0, 1):  # pending kit finalizer, or pending synths
                args = [str(binary), str(kit), str(lane), str(pattern)]
                clean = subprocess.run([*args, "0"], check=True, stdout=subprocess.PIPE).stdout
                poison = subprocess.run([*args, "1"], check=True, stdout=subprocess.PIPE).stdout
                if clean != poison:
                    raise SystemExit(
                        f"scratch poisoned following frame: kit={kit}, lane={lane}, pattern={pattern}")
                print(f"PASS: kit {kit}, key lane {lane}, pattern {pattern}, "
                      f"{len(clean)} matching output/scratch bytes")
