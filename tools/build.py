#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Dinghy (on Felucca / SLOOP's platform): the app, the update loader and an installable .fwsc package.

  tools/build.py [--target fm1|smk37] [--release X.Y[-suffix]]

--target selects the product (default fm1, FM-1); smk37 is the SMK-37 Pro.
Everything that differs between them is the TARGETS table below; the firmware
sees the choice through firmware/product.h (-DBR_PRODUCT=..., force-included).
--print-config prints the resolved configuration as JSON and builds nothing.

Outputs in build/: dinghy.bin (app), loader/ota.bin (update loader),
dinghy.fwsc (package). See README.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402
import check_runtime_ram  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-save-temps=obj", "-Os", "-DBR_AUDIO_INTERNAL_BLOCK160=0", "-DBR_AUDIO_INTERNAL_BLOCK240=0", "-DBR_AUDIO_INTERNAL_BLOCK64=0", "-DBR_AUDIO_INTERNAL_BLOCK128=0", "-DBR_AUDIO_INTERNAL_BLOCK256=1", "-DFM1_NATIVE_OUTPUT_32K=0", "-DFM1_NATIVE_OUTPUT_48K=0", "-DFM1_NATIVE48_EXPERIMENTAL_CLOCK=0", "-DPLAITS_INTERNAL_SAMPLE_RATE=44100", "-DFM1_PLAITS_SOURCE_RATE_32K=0", "-DFM1_PLAITS_SOURCE_RATE_44100=1", "-DFM1_PLAITS_SOURCE_RATE_38400=0", "-DFM1_PLAITS_SOURCE_RATE_48000=0", "-DFM1_PLAITS_SRC_LAGRANGE4=0", "-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0", "-DFM1_PLAITS_PHASE_LUT=0", "-DFM1_PLAITS_RATE_TIME_CALIBRATION=1", "-DFM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION=1", "-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=1", "-DBR_AUDIO_FIXED8_SOLVER_RAM=0", "-DBR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0", "-DBR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0", "-DBR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG=0", "-DBR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG=0", "-DBR_FX_RATE_SIGNED_441_RECIP=0", "-DBR_FX_TAPE_WOW_DELTA_LUT=0", "-DBR_PLAITS_HIHAT_FUSION=0", "-DFM1_HARMONIC_TARGET_AMPLITUDE_CACHE=1", "-DFM1_HARMONIC_CACHE_TEST=0", "-DFM1_HARMONIC_TRIPLE_RAM=0", "-DFM1_PLAITS_KIT_RENDER_OUT_RESTRICT=1", "-DFM1_PLAITS_WAVES_STACK_SCRATCH=0", "-DFM1_PLAITS_WAVES_BOOT_SCRATCH_BSS=1", "-DFM1_BRAIDS_PACK_WT_WAVES=0", "-DFM1_DEVICE=1", "-DBR_CPU1_AUDIO=1", "-fwrapv", "-ffunction-sections", "-fdata-sections", "-fno-fast-math", "-ffp-contract=off",
          "-fno-builtin", "-Wall", "-Wno-unused-function"]
CFLAGS.append("-DBR_MIDI_CLOCK_EXPERIMENT=1")
CXXFLAGS = ["-save-temps=obj", "-std=c++14", "-fwrapv", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
BRAIDS_CPP = [
    FW / "braids" / "upstream" / "braids" / "analog_oscillator.cc",
    FW / "braids" / "upstream" / "braids" / "digital_oscillator.cc",
    FW / "braids" / "upstream" / "braids" / "macro_oscillator.cc",
    FW / "braids" / "upstream" / "braids" / "resources.cc",
    FW / "braids" / "braids_perc_c_api.cc",
]
APP_C = [FW / "src" / "audio_fixed8_plan_unit.c"]
PATTERN_TARGET_EXPERIMENT = False
PATTERN_DEVICE_CANDIDATE = False
PATTERN_NAMESPACE_TARGET_EXPERIMENT = False
PATTERN_PROJECT_UI_TARGET_EXPERIMENT = False
POLY_RECORDED = False               # FM-1 with polyphony: build-settings.json records "polyphony": true
POLY_SIZE_CONSTRAINED = False       # = firmware/product.h BR_POLY_SIZE_CONSTRAINED (FM-1 with polyphony)
DSP_C = []
HAL_C = [FW / "hal" / "fm1_cpu1_snapshot.c",
         FW / "hal" / "fm1_cpu1_worker_target.c",
         FW / "hal" / "fm1_fault_diag.c"]
PLAITS_CPP = [FW / "plaits" / "plaits_c_api.cc", FW / "plaits" / "plaits_kit_c_api.cc",
              *sorted((FW / "plaits" / "upstream" / "plaits").rglob("*.cc")),
              *sorted((FW / "plaits" / "upstream" / "stmlib").rglob("*.cc"))]
# Exact allowlist of Plaits translation units compiled at -Os (selector
# FM1_PLAITS_COLD_TU_OS). Do not extend this to shared DSP/common code, the
# kit/drum engines or anything placed in a RAM section.
PLAITS_COLD_TU_OS_SOURCES = frozenset({
    "upstream/plaits/dsp/engine/fm_engine.cc",
    "upstream/plaits/dsp/engine/string_engine.cc",
    "upstream/plaits/dsp/engine2/chiptune_engine.cc",
    "upstream/plaits/dsp/engine2/six_op_engine.cc",
    "upstream/plaits/resources.cc",                         # boot-only wave inflate: -682 B
    # Non-kit engines that render audio: -Os costs CPU per block. One line each
    # (linked O3->Os bytes, FM-1 and SMK-37 alike) so any can be backed out
    # after hardware timing. naive_speech_synth.cc is left at O3 (+14 B at Os).
    "upstream/plaits/dsp/engine/chord_engine.cc",           # -86 B
    "upstream/plaits/dsp/engine/particle_engine.cc",        # -246 B
    "upstream/plaits/dsp/engine/swarm_engine.cc",           # -206 B
    "upstream/plaits/dsp/engine/wavetable_engine.cc",       # -228 B
    "upstream/plaits/dsp/engine2/string_machine_engine.cc", # -246 B
    "upstream/plaits/dsp/engine2/wave_terrain_engine.cc",   # -38 B
    "upstream/plaits/dsp/speech/sam_speech_synth.cc",       # -98 B
})
# Size-constrained polyphony (FM-1 with polyphony only, = product.h BR_POLY_SIZE_CONSTRAINED):
# Plaits translation units compiled at -Os instead of the DSP level so the HD image fits the
# app slot. Only the -O level changes (floating-point flags stay those of the DSP build).
# Every other configuration (FM-1 without polyphony, SMK-37) is unaffected. Each entry is a
# timing change for the models that use it and must be benchmarked on a unit.
POLY_SIZE_OS_SOURCES = frozenset({
    "upstream/plaits/dsp/chords/chord_bank.cc",     # chord table setup and inversion: per chord-engine block
})
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

# Product targets. Everything product-specific in this script is data here.
# The SMK-37 Pro values are those of the JamesOS-K build.py (smk37-import).
TARGETS = {
    "fm1": {
        "name": "FM-1",
        "product_define": "BR_PRODUCT_ID_FM1",     # -DBR_PRODUCT=..., see firmware/product.h
        "identity_prefix": "FM-1_",                 # --product must be prefix + digits
        "default_product": "FM-1_9261389",          # private hybrid source/FX scheduling diagnostic
        "version": "1.0b-device-test",
        "package_name": "fm1-braids-plaits-{version}.fwsc",
        "release_package_name": "fm1-braids-plaits-{release}.fwsc",
        "app_slot": 0x8DFBC,                        # = XIP LENGTH in firmware/app.ld
        "polyphony": True,                          # device candidates (HD and standard): on, -DBR_CAP_POLY=1;
                                                    # --no-polyphony: the previous default exactly (-nopoly dirs)
        "poly_define_when_off": False,              # off: no define (product.h FM-1 default 0), flags as before
        "poly_split_outputs": True,                 # --no-polyphony gets its own output directories, and
                                                    # polyphony on is recorded in build-settings.json
        "poly_size_constrained": True,              # with polyphony: POLY_SIZE_OS_SOURCES at -Os
                                                    # (product.h BR_POLY_SIZE_CONSTRAINED)
        "out_suffix": "",                           # output directories as before
        "scanner_helpers": "SCANNER_RAM_SYMBOL_ALLOWLIST",
        "smk37_layout": False,                      # fm1pkg_make/check_runtime_ram smk37= keyword
        "package_requires_device_candidate": False,
        "diagnostics_free_scope": "development-only instrumentation excluded; operational CPU monitor, fault records, installer and recovery retained",
        "settings_tail": False,                     # build-settings.json keeps the FM-1 shape
        "link_ready": True,
    },
    "smk37": {
        "name": "SMK-37 Pro",
        "product_define": "BR_PRODUCT_ID_SMK37",
        "identity_prefix": "SMK-37 Pro_",
        "default_product": "SMK-37 Pro_9650001",  # JamesOS-K prefix 965, four-digit build number
        "version": "1.0b-bring-up",
        "package_name": "JamesOS-K-SMK-37-Pro.fwsc",
        "release_package_name": "JamesOS-K-SMK-37-Pro-{release}.fwsc",
        "app_slot": 0x97400,                        # XIP LENGTH substituted into app.ld
        "polyphony": True,                          # device candidates: on unless --no-polyphony
        "poly_define_when_off": True,               # --no-polyphony passes -DBR_CAP_POLY=0
        "poly_split_outputs": False,                # --no-polyphony: same output directory (as JamesOS-K)
        "poly_size_constrained": False,             # 36 KB free: keeps its code generation
        "out_suffix": "smk37",                      # build/<dir>-smk37: never mixes with FM-1 outputs
        "scanner_helpers": "SMK_SCANNER_HELPERS",
        "smk37_layout": True,
        "package_requires_device_candidate": True,  # other SMK-37 builds are link checks only
        "diagnostics_free_scope": "development-only instrumentation excluded; operational CPU monitor and fault records retained",
        "settings_tail": True,                      # target, app_identity, app_slot_bytes, installable
        # --target smk37 links and packages from this tree (WP5). Sources the SMK-37 build needs:
        "link_ready": True,
        "required_sources": ("hal/smk37/smk37_input.h", "hal/smk37/smk37_flash_map.h",
                             "hal/smk37/smk37_lcd_hw.h", "src/smk37/smk37_panel.c"),
    },
}
TARGET = TARGETS["fm1"]
PRODUCT = TARGET["default_product"]
VERSION = TARGET["version"]


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        vendor_root = "/opt/jieli" if use_docker() else str(toolchain())
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", "-mcpu=r3",
                                         "-isystem", f"{vendor_root}/pi32v2/include",
                                         "-Ifirmware/target/include", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    # Each job may be a Linux container. Keep compiler memory and process counts bounded.
    jobs = max(1, min(int(os.environ.get("JIELI_JOBS", "4")), len(cmds)))
    with ThreadPoolExecutor(jobs) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def configured_macro(source_path, name, default=0):
    """Return the effective build selector, honoring a compiler -D override."""
    define = "-D" + name
    matches = [flag for flag in CFLAGS
               if flag == define or flag.startswith(define + "=")]
    if len(matches) > 1:
        raise SystemExit(f"duplicate compiler selector for {name}: {matches}")
    if matches:
        value = matches[0][len(define):]
        if not value:
            return 1
        try:
            return int(value[1:])
        except ValueError as exc:
            raise SystemExit(f"invalid compiler selector for {name}: {matches[0]}") from exc
    return check_runtime_ram._macro(SRC, source_path, name, default)


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src"]
    # crt0_ldr.S carries the product's loader mark, so it must see the product define too.
    tc_all(("cc", f"-DBR_PRODUCT={TARGET['product_define']}", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

DSP_RELAXED_FP_FLAGS = ["-ffp-contract=fast", "-fno-signed-zeros",
                        "-fno-trapping-math", "-fassociative-math"]


def dsp_relaxed_fp():
    value = check_runtime_ram._macro(
        SRC, "firmware/hal/fm1_math_policy.h", "FM1_DSP_RELAXED_FP", 0)
    if value not in (0, 1):
        raise ValueError("FM1_DSP_RELAXED_FP must be 0 or 1")
    return bool(value)


def plaits_cold_tu_os():
    value = check_runtime_ram._macro(
        SRC, "firmware/hal/fm1_math_policy.h", "FM1_PLAITS_COLD_TU_OS", 0)
    if value not in (0, 1):
        raise ValueError("FM1_PLAITS_COLD_TU_OS must be 0 or 1")
    return bool(value)


def plaits_source_dsp_flags(dsp_flags, relative, cold_tu_os_enabled):
    if not cold_tu_os_enabled or relative not in PLAITS_COLD_TU_OS_SOURCES:
        return dsp_flags
    # Replace only the optimization level. Keep the common strict/relaxed
    # floating-point flags and every other DSP option identical.
    return [flag for flag in dsp_flags if flag not in ("-Os", "-O2", "-O3")] + ["-Os"]


def braids_perc_no_jump_tables():
    value = check_runtime_ram._macro(
        SRC, "firmware/hal/fm1_math_policy.h", "FM1_BRAIDS_PERC_NO_JUMP_TABLES", 0)
    if value not in (0, 1):
        raise ValueError("FM1_BRAIDS_PERC_NO_JUMP_TABLES must be 0 or 1")
    return bool(value)


# RAM bound of the shared reverb's per-sample core (rv2_core_run); see check(). The
# shipped core measured 2,252 B (fm1 and smk37); the bound leaves room for the -D
# timing options (RV2_LINE_STRUCT=1, RV2_UPSAMPLE_TAPS=4, RV2_INT_READS=1) and stays
# under the 4,320 B of comb kernel it replaced.
RV2_CORE_RAM_LIMIT = 3072
# RAM bound of the DRIVE per-sample loop (br_fx_drive_run, all three shapers and the mix);
# measured 1,362 B on fm1. Its block setup, br_fx_drive_block, stays in XIP flash.
DRIVE_RUN_RAM_LIMIT = 1400
# RAM bounds of the Plaits kit drum loops (AnalogBassDrum::RenderRam and
# SquareNoise::RenderRam, both call-free); their callers stay in XIP flash.
PLAITS_KICK_RAM_LIMIT = 1152
PLAITS_SQUARE_NOISE_RAM_LIMIT = 416


def ram_code_limit():
    value = check_runtime_ram._macro(
        SRC, "firmware/hal/fm1_hot_code_policy.h", "FM1_EXTENDED_RAM_AUDIO", 0)
    if value not in (0, 1):
        raise ValueError("FM1_EXTENDED_RAM_AUDIO must be 0 or 1")
    return 0x01C07F00 if value else 0x01C06000


XIP_MAP = re.compile(r"XIP    \(rx\)  : ORIGIN = 0x02000120, LENGTH = (0x[0-9A-Fa-f]+)\n")


def app_linker_script():
    """firmware/app.ld for FM-1; for another target the same script with its XIP length."""
    source = FW / "app.ld"
    text = source.read_text()
    found = XIP_MAP.findall(text)
    if len(found) != 1 or int(found[0], 16) != fm1pkg_make.APP_SLOT:
        raise ValueError("unexpected XIP map in firmware/app.ld")
    if TARGET["app_slot"] != fm1pkg_make.APP_SLOT:
        text = XIP_MAP.sub(f"XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x{TARGET['app_slot']:X}\n", text)
    if ram_code_limit() == 0x01C06000:
        if TARGET["app_slot"] == fm1pkg_make.APP_SLOT:
            return source
        GEN.mkdir(parents=True, exist_ok=True)
        target = GEN / f"app-{TARGET['out_suffix']}.ld"
        target.write_text(text)
        return target
    original = "RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000"
    if text.count(original) != 1:
        raise ValueError("unexpected baseline RAMTEXT map")
    text = text.replace(original,
        "RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00")
    text += "\nASSERT(_ra_end <= 0x01C07F00, \"RAM audio overlaps boot STATUS page\");\n"
    GEN.mkdir(parents=True, exist_ok=True)
    target = GEN / "app-extended-ram.ld"
    target.write_text(text)
    return target

def build_app(optimization, dsp_optimization):
    cold_tu_os_enabled = plaits_cold_tu_os()
    plaits_sources = {str(src.relative_to(FW / "plaits")) for src in PLAITS_CPP}
    missing_cold_sources = (PLAITS_COLD_TU_OS_SOURCES | POLY_SIZE_OS_SOURCES) - plaits_sources
    if missing_cold_sources:
        raise ValueError("cold Plaits TU allowlist is missing build sources: " +
                         ", ".join(sorted(missing_cold_sources)))
    # -DBR_UI_HD=1: the HD interface is the only one (the standard interface was removed; it is also
    # firmware/src/gfx_hd.h's default, kept explicit so the flags and bytes stay as they were).
    common_flags = [*CFLAGS, "-DBR_ENABLE_PLAITS=1", "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen", f'-DFELUCCA_ID="{PRODUCT}"', f'-DAPP_VER="{VERSION}"', "-DBR_UI_HD=1"]
    flags = [*common_flags, "-" + optimization]
    dsp_flags = [*common_flags, "-" + dsp_optimization]
    if dsp_relaxed_fp():
        dsp_flags = [flag for flag in dsp_flags if flag != "-ffp-contract=off"]
        dsp_flags.extend(DSP_RELAXED_FP_FLAGS)
    hal_flags = [*common_flags, "-Os"]
    cmds = [("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
            ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
            ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
            ("cc", *flags, "-DBR_SPLIT_AUDIO=1", "-c", FW / "src" / "dinghy.c", "-o", OUT / "dinghy.o"),
            ("cc", *dsp_flags, "-DBR_SPLIT_AUDIO=1", "-c", FW / "src" / "instrument_audio_unit.c",
             "-o", OUT / "instrument_audio_unit.o")]
    for src in APP_C:
        app_flags = [*flags, "-Oz", "-DBR_SPLIT_AUDIO=1"] if PATTERN_TARGET_EXPERIMENT and src.stem.startswith("pattern_") else flags
        if PATTERN_DEVICE_CANDIDATE and src.stem == "audio_fixed8_plan_unit":
            app_flags = [*app_flags, "-Os"]
        cmds.append(("cc", *app_flags, "-c", src, "-o", OUT / (src.stem + ".o")))
    for src in DSP_C:
        cmds.append(("cc", *dsp_flags, "-c", src, "-o", OUT / (src.stem + ".o")))
    for src in HAL_C:
        cmds.append(("cc", *hal_flags, "-c", src, "-o", OUT / (src.stem + ".o")))
    for src in BRAIDS_CPP:
        obj = OUT / ("braids_" + src.stem + ".o")
        file_dsp_flags = [*dsp_flags]
        if src.name == "braids_perc_c_api.cc" and braids_perc_no_jump_tables():
            file_dsp_flags.append("-fno-jump-tables")
        cmds.append(("cc", *file_dsp_flags, *CXXFLAGS, "-Ifirmware/braids", "-Ifirmware/braids/upstream",
                     "-x", "c++", "-c", src, "-o", obj))
    plaits_objects = []
    for src in PLAITS_CPP:
        relative = str(src.relative_to(FW / "plaits"))
        obj = OUT / ("plaits_" + relative.replace("/", "_") + ".o")
        plaits_objects.append(obj)
        file_dsp_flags = plaits_source_dsp_flags(
            dsp_flags, relative, cold_tu_os_enabled)
        if POLY_SIZE_CONSTRAINED and relative in POLY_SIZE_OS_SOURCES:
            file_dsp_flags = [flag for flag in file_dsp_flags if flag not in ("-Os", "-O2", "-O3")] + ["-Os"]
        if (src.name == "additive_engine.cc" and
                check_runtime_ram._macro(
                    SRC, "firmware/plaits/upstream/plaits/dsp/oscillator/harmonic_oscillator.h",
                    "FM1_HARMONIC_TRIPLE_RAM", 0)):
            file_dsp_flags = [*file_dsp_flags, "-DFM1_HARMONIC_TRIPLE_RAM_TU=1"]
        cmds.append(("cc", *file_dsp_flags, *CXXFLAGS, "-DTEST", "-Dstmlib=plaits_stmlib",
                     "-Ifirmware/plaits", "-Ifirmware/plaits/upstream", "-Ifirmware/braids/upstream",
                     "-x", "c++", "-c", src, "-o", obj))
    tc_all(*cmds)
    elf = OUT / "dinghy.elf"
    library_root = "/opt/jieli" if use_docker() else str(toolchain())
    extra_link = (["-u", "fm1_project_adapter_save", "-u", "fm1_project_adapter_load",
                   "-u", "br_ps_target_project_begin", "-u", "br_ps_target_project_end", "-u", "br_ps_target_project_store"]
                  if PATTERN_TARGET_EXPERIMENT and not PATTERN_DEVICE_CANDIDATE else [])
    if PATTERN_NAMESPACE_TARGET_EXPERIMENT:
        for symbol in ("pns_provision", "pns_open_pair", "pns_rotate", "pns_recover_rotation",
                       "pns_retire_peer", "pns_claim", "pns_owned", "pns_revoke_erase", "pns_sha256_digest"):
            extra_link.extend(("-u", symbol))
    tc("pi32v2/bin/ld", "--gc-sections", *extra_link, "-e", "_start", "-T", app_linker_script(), OUT / "crt0.o", OUT / "fm1_vec.o",
       OUT / "fm1_isr.o", OUT / "dinghy.o", OUT / "instrument_audio_unit.o",
       *(OUT / (src.stem + ".o") for src in APP_C),
       *(OUT / (src.stem + ".o") for src in DSP_C),
       *(OUT / (src.stem + ".o") for src in HAL_C),
       *(OUT / ("braids_" + src.stem + ".o") for src in BRAIDS_CPP), *plaits_objects,
       f"-L{library_root}/pi32v2/lib/r3", "-lm", "-lcompiler-rt", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin", "ramaudio.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt, ra = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_audio", elf, OUT / "ramaudio.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_audio", elf))
    (OUT / "dinghy.dis").write_text(dis)
    (OUT / "dinghy.symbols").write_text(syms)
    (OUT / "dinghy.sections").write_text(tc("common/bin/objdump", "-h", elf))

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # RAM code/data follow .text at their load addresses; startup copies them by words.
    for sect, lname in (("ramtext.bin", "_rt_load"), ("ramaudio.bin", "_ra_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "dinghy.bin").write_bytes(img)
    return bytes(img), syms, dis, rt, ra


def ram_text_calls(disassembly):
    """Find call opcodes, including pi32v2's indirect `callns` variant."""
    calls = []
    for line in disassembly.splitlines():
        instruction = LINE.match(line)
        if instruction and re.search(r"\bcall[a-z0-9]*\b",
                                     instruction.group(3).split("<", 1)[0]):
            calls.append(line)
    return calls


def fixed8_solver_ram_check(symbols, ram_audio_disassembly):
    """Validate the one explicitly approved bounded RAM planner leaf."""
    symbol = "br_audio_fixed8_plan_solve"
    errors = []
    records = []
    matches = []
    for line in symbols.splitlines():
        fields = line.split()
        if fields and fields[-1] == symbol:
            records.append(fields)
            if len(fields) >= 6 and fields[2] == "F":
                try:
                    matches.append((int(fields[0], 16), fields[3],
                                    int(fields[-2], 16)))
                except ValueError:
                    errors.append(f"unparseable {symbol} symbol record: {line}")
    if len(records) != 1 or len(matches) != 1:
        errors.append(f"expected exactly one {symbol} function symbol record; found {len(records)} record(s)")
        return errors

    start, section, size = matches[0]
    if section != ".ram_audio":
        errors.append(f"{symbol} is in {section}, expected .ram_audio")
    if size <= 0 or size > 1536:
        errors.append(f"{symbol} size {size} B is outside 1..1536 B")

    end = start + size
    decoded = []
    malformed = []
    call_lines = []
    address_prefix = re.compile(r"^\s*([0-9a-fA-F]+):")
    byte_token = re.compile(r"^[0-9a-fA-F]{2}$")
    for line in ram_audio_disassembly.splitlines():
        address_match = address_prefix.match(line)
        if not address_match:
            continue
        address = int(address_match.group(1), 16)
        if not start <= address < end:
            continue
        instruction = LINE.match(line)
        if not instruction:
            malformed.append(line)
            continue
        byte_tokens = instruction.group(2).split()
        if not byte_tokens or any(not byte_token.fullmatch(token) for token in byte_tokens):
            malformed.append(line)
            continue
        asm = instruction.group(3).split("<", 1)[0].strip()
        if re.search(r"\b(?:call[a-z0-9]*|syscall)\b", asm):
            call_lines.append(line)
        decoded.append((address, len(byte_tokens), line))

    if malformed:
        errors.append(f"{symbol} has unparsed instruction line(s): {malformed[0].strip()}")
    if not decoded:
        errors.append(f"{symbol} has no parsed instructions in its symbol range")
    else:
        cursor = start
        for address, byte_count, line in sorted(decoded):
            if address != cursor:
                errors.append(
                    f"{symbol} disassembly has a gap or overlap at {cursor:x}: "
                    f"next instruction starts at {address:x}: {line.strip()}")
                break
            cursor += byte_count
        if cursor != end:
            errors.append(f"{symbol} disassembly covers {max(0, cursor - start)} of {size} symbol bytes")
    if call_lines:
        errors.append(f"{symbol} is not call-free: {call_lines[0].strip()}")
    return errors


def fixed8_solver_xip_check(symbols):
    """Require the non-RAM planner selector to resolve to one XIP function."""
    symbol = "br_audio_fixed8_plan_solve"
    records = []
    functions = []
    errors = []
    for line in symbols.splitlines():
        fields = line.split()
        if fields and fields[-1] == symbol:
            records.append(fields)
            if len(fields) >= 6 and fields[2] == "F":
                try:
                    functions.append((fields[3], int(fields[-2], 16)))
                except ValueError:
                    errors.append(f"unparseable {symbol} function symbol record: {line}")
    if len(records) != 1 or len(functions) != 1:
        errors.append(f"expected exactly one {symbol} function symbol record; found {len(records)} record(s)")
        return errors
    section, size = functions[0]
    if section != ".text":
        errors.append(f"{symbol} is in {section}, expected XIP .text when the RAM selector is off")
    if size <= 0:
        errors.append(f"{symbol} has invalid XIP function size {size} B")
    return errors


def check(img, syms, dis, rt, ra):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = ram_text_calls(rt)
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    rt_start, rt_end = sym("_rt_start"), sym("_rt_end")
    ra_start, ra_end = sym("_ra_start"), sym("_ra_end")
    if not rt_start or rt_end != ra_start or ra_end <= ra_start:
        errors.append(f"RAM code is not contiguous/nonempty: .ram_text {rt_start:#x}..{rt_end:#x}, .ram_audio {ra_start:#x}..{ra_end:#x}")
    if ra_end > ram_code_limit():
        errors.append(f"RAM code exceeds selected RAMTEXT boundary {ram_code_limit():#x}: end {ra_end:#x}")
    renderer_functions = [ln.split() for ln in syms.splitlines()
                          if len(ln.split()) >= 6 and ln.split()[2] == "F" and "RenderOperators" in ln]
    if len(renderer_functions) != 8 or any(
            fields[3] != ".ram_audio" or not (ra_start <= int(fields[0], 16) < ra_end)
            for fields in renderer_functions):
        errors.append(f"expected exactly 8 RenderOperators functions in .ram_audio; found {len(renderer_functions)}")
    ram_audio_functions = [ln.split() for ln in syms.splitlines()
                           if len(ln.split()) >= 6 and ln.split()[2] == "F" and ln.split()[3] == ".ram_audio"]
    ram_audio_names = {fields[-1] for fields in ram_audio_functions}
    expected_ram_audio = {fields[-1] for fields in renderer_functions} | {
        "br_fx_process_ram", "br_fx_process_track_job", "br_fx_process_track_job_cpu1", "br_fx_worker_track_batch",
        "br_fx_run_cpu0_batch", "br_fx_build_batches",
        "br_fx_prepare_block", "br_fx_finish_block", "br_fx_mute_block",
        "br_fx_process_track_job_impl", "br_fx_process_track_input_cpu1", "br_fx_fusion_process_track",
        "br_fx_fusion_shared_reverb",
        "br_fx_fusion_cost", "br_fx_fusion_prepare", "br_fx_fusion_cancel",
        "br_fx_stage_drive_block", "br_fx_stage_mod_block", "br_fx_stage_delay_block",
        "br_fx_stage_mod_rate2_block", "br_fx_stage_delay_rate2_tape_block",
        "br_fx_process_track_job_staged",
        "br_fx_stage_send_block", "br_gr_job", "br_gr_add_range",
        "br_fx_process_track_job_staged_block",
        "br_fx_delay_rate2_step", "br_master_fx_begin_block", "br_master_fx_process_frame",
        "br_master_fx_process", "br_master_limit_block", "br_master_snapshot_ram", "br_master_slew", "br_fx_abs",
        "br_fx_div_pow2", "br_fx_mul_q15", "br_fx_wet_gain", "br_master_shape",
        "br_fx_pole", "br_fx_pole_bounded", "br_fx_softclip", "br_fx_tri", "br_comp_ratio", "br_comp_snapshot",
        "br_comp_abs", "br_comp_slew", "br_comp_mul_q15", "br_comp_makeup_q15",
        "br_comp_slew_recip_q16",
        "br_comp_threshold_q15", "br_comp_log2_q8", "br_comp_gain_for_coeff",
        "br_comp_gain_for_level", "br_comp_mix_gain", "br_comp_bypassed",
        "br_comp_reset_detector", "br_comp_process_tracks", "br_fx_any_enabled",
        "br_fx_pan_stereo", "br_fx_drive", "br_fx_mod_delay_q8", "br_fx_mod",
        "br_fx_delay_samples_ram", "br_fx_delay", "br_fx_progress_get",
        "br_fx_delay_note_samples", "br_fx_delay_long_decimate", "br_fx_delay_storage_reset",
        "br_fx_process_pipeline",
        # Silent-finish fast path: the compressor rest check outlines the gain step.
        "br_comp_next_mix_gain", "br_fx_comp_at_rest", "br_fx_finish_inputs_silent",
        "fm1_cpu1_worker_start_stub", "fm1_cpu1_worker_main",
        "worker_dispatch_target"
    } | set(getattr(check_runtime_ram, TARGET["scanner_helpers"])) | \
        set(check_runtime_ram.VCF_RAM_SYMBOL_ALLOWLIST)
    development_diagnostics = configured_macro("firmware/src/instrument_diagnostic.c", "FM1_DEVELOPMENT_DIAGNOSTICS", 1)
    if development_diagnostics:
        expected_ram_audio.add("worker_smoke_callback")
    # Diagnostic-only startup CRC relocation. Keep the added RAM leaf bounded
    # and require a single named symbol; normal source builds retain the old
    # allowlist and cannot silently place new functions here.
    crc_fields = [f for f in ram_audio_functions if f[-1] == "crc32"]
    if len(crc_fields) != 1 or int(crc_fields[0][-2], 16) > 128:
        errors.append("diagnostic RAM CRC must be unique and <=128 bytes")
    else:
        expected_ram_audio.add("crc32")
    # The shared reverb (reverb2, instrument_reverb2*.h/.c). Its per-sample core is the
    # only reverb DSP in RAM: one call-free leaf, bounded below the old comb kernel it
    # replaced (br_gr_job 2,992 + br_fx_reverb 1,084 + tap 244 B). Its design, apply and
    # block driver run from XIP flash and reach the core through an indirect call.
    # br_gr_job is now only the RAM entry (guards, timing, the indirect call); keep it
    # small (70 B measured) so the DSP cannot drift back into it.
    for symbol, size_limit, call_free in (("rv2_core_run", RV2_CORE_RAM_LIMIT, True),
                                          ("br_gr_job", 128, False)):
        fields = [f for f in ram_audio_functions if f[-1] == symbol]
        if len(fields) != 1 or int(fields[0][-2], 16) > size_limit:
            errors.append(f"shared reverb RAM symbol {symbol} missing, duplicated, or over {size_limit} bytes")
            continue
        expected_ram_audio.add(symbol)
        if call_free:
            first, length = int(fields[0][0], 16), int(fields[0][-2], 16)
            body = "\n".join(line for line in ra.splitlines()
                             if (match := LINE.match(line)) and
                             first <= int(match[1], 16) < first + length)
            if not body or ram_text_calls(body):
                errors.append(f"shared reverb core {symbol} must remain call-free")
                continue
        notes.append(f"shared reverb: {symbol} {int(fields[0][-2], 16)} B of <= {size_limit}"
                     + (", call-free" if call_free else ""))
    # DRIVE: only the per-sample loop is RAM code, one bounded call-free leaf; the block setup
    # runs from XIP flash and reaches it through an indirect call. pi32v2 objdump decodes some
    # FPU opcodes as a spurious `call`, so the compiler listing is audited instead.
    drive_symbol = "br_fx_drive_run"
    drive_fields = [f for f in ram_audio_functions if f[-1] == drive_symbol]
    if len(drive_fields) != 1 or int(drive_fields[0][-2], 16) > DRIVE_RUN_RAM_LIMIT:
        errors.append(f"DRIVE RAM loop {drive_symbol} missing, duplicated, or over {DRIVE_RUN_RAM_LIMIT} bytes")
    else:
        expected_ram_audio.add(drive_symbol)
        try:
            drive_asm = (OUT / "instrument_audio_unit.s").read_text()
        except OSError:
            drive_asm = ""
        drive_body = re.search(r"(?ms)^" + drive_symbol + r":.*?^\s*\.size\s+" + drive_symbol + r"\s*,",
                               drive_asm)
        if not drive_body or re.search(r"(?m)^\s*(?:call[a-z0-9]*|syscall)\b", drive_body.group(0)):
            errors.append(f"DRIVE RAM loop {drive_symbol}: compiler listing must be present and call-free")
        else:
            notes.append(f"DRIVE: {drive_symbol} {int(drive_fields[0][-2], 16)} B of <= "
                         f"{DRIVE_RUN_RAM_LIMIT}, compiler body call-free")
    # Delay lines keep the companded int16 storage of the old reverb: two bounded,
    # call-free codec leaves.
    for codec_symbol, leaf_limit in (("br_fx_reverb_compress16", 64),
                                      ("br_fx_reverb_decompress16", 64)):
        if codec_symbol not in ram_audio_names:
            continue
        expected_ram_audio.add(codec_symbol)
        codec_fields = [f for f in ram_audio_functions if f[-1] == codec_symbol]
        codec_body = re.search(r"^" + codec_symbol + r":\n(.*?)(?=^\S|\Z)", ra, re.M | re.S)
        if (len(codec_fields) != 1 or int(codec_fields[0][-2], 16) > leaf_limit or
                not codec_body or ram_text_calls(codec_body.group(1))):
            errors.append(f"feedback leaf must be unique, <={leaf_limit} bytes, and call-free: " + codec_symbol)
        else:
            notes.append(f"feedback leaf: exact symbol, <={leaf_limit} bytes, call-free: " + codec_symbol)
    fusion_only = check_runtime_ram._macro(
        SRC, "firmware/src/instrument.h", "BR_AUDIO_FUSION_ONLY_BUILD", 0)
    if fusion_only not in (0, 1):
        errors.append("BR_AUDIO_FUSION_ONLY_BUILD must be exactly zero or one")
    required_ram_audio = {fields[-1] for fields in renderer_functions} | {
        "br_fx_process_ram", "br_fx_process_track_job", "br_fx_process_track_job_cpu1",
        "br_fx_worker_track_batch", "fm1_cpu1_worker_start_stub", "fm1_cpu1_worker_main",
        "worker_dispatch_target"}
    if development_diagnostics:
        required_ram_audio.add("worker_smoke_callback")
    fixed8_solver_ram = configured_macro(
        "firmware/src/audio_fixed8_plan.h", "BR_AUDIO_FIXED8_SOLVER_RAM", 0)
    fixed8_solver_symbol = "br_audio_fixed8_plan_solve"
    if fixed8_solver_ram not in (0, 1):
        errors.append("BR_AUDIO_FIXED8_SOLVER_RAM must be exactly zero or one")
    elif fixed8_solver_ram:
        expected_ram_audio.add(fixed8_solver_symbol)
        required_ram_audio.add(fixed8_solver_symbol)
        errors.extend(fixed8_solver_ram_check(syms, ra))
        if not any(f[-1] == fixed8_solver_symbol for f in ram_audio_functions):
            errors.append(f"{fixed8_solver_symbol} must be linked into .ram_audio when selected")
        else:
            notes.append("fixed-eight solver: exact .ram_audio symbol, <=1536 bytes, fully decoded and call-free")
    elif any(fields and fields[-1] == fixed8_solver_symbol and
             ".ram_audio" in fields[:-1] for fields in
             (line.split() for line in syms.splitlines())):
        errors.append(f"{fixed8_solver_symbol} is in .ram_audio while its selector is disabled")
    elif fixed8_solver_ram == 0:
        xip_errors = fixed8_solver_xip_check(syms)
        errors.extend(xip_errors)
        if not xip_errors:
            notes.append("fixed-eight solver: unique XIP .text function; RAM selector disabled")
    finish_stream = check_runtime_ram._macro(
        SRC, "firmware/src/instrument_fx.c", "BR_FX_FINISH_MASTER_STREAM", 0)
    range_symbol = "br_master_fx_process_range"
    if finish_stream not in (0, 1):
        errors.append("BR_FX_FINISH_MASTER_STREAM must be exactly zero or one")
    if finish_stream:
        if (not fusion_only or not check_runtime_ram._macro(
                SRC, "firmware/src/instrument_fx.c", "BR_FX_FINISH_MASTER_FUSE", 0) or
                not check_runtime_ram._macro(
                SRC, "firmware/src/instrument_fx.c", "BR_FX_MASTER_FRAME_INLINE_FINISH", 0)):
            errors.append("stream master requires the approved fusion/inline configuration")
        expected_ram_audio.add(range_symbol)
        required_ram_audio.add(range_symbol)
        range_fields = [f for f in ram_audio_functions if f[-1] == range_symbol]
        if len(range_fields) != 1 or int(range_fields[0][-2], 16) > 3072:
            errors.append("master range RAM helper missing, duplicated, or larger than 3072 bytes")
        if "br_master_fx_process_frame" in ram_audio_names:
            errors.append("stream master must inline its frame kernel into the range helper")
        else:
            notes.append("stream master: exact approved range symbol, <=3072 bytes, no frame helper")
    elif range_symbol in ram_audio_names:
        errors.append("master range RAM helper present while stream selector is disabled")
    # The native48 HEAT selector shares a compact stereo BIT kernel in RAM.
    # Keep a unique bounded symbol and preserve the total STATUS-page limit.
    bit_symbol = "br_master_bit_process"
    if bit_symbol in ram_audio_names:
        bit_fields = [f for f in ram_audio_functions if f[-1] == bit_symbol]
        expected_ram_audio.add(bit_symbol)
        if len(bit_fields) != 1 or int(bit_fields[0][-2], 16) > 768:
            errors.append("stereo BIT RAM helper missing, duplicated, or larger than 768 bytes")
        else:
            notes.append("HEAT BIT: unique stereo RAM helper, <=768 bytes")
    quantize_symbol = "br_master_bit_quantize"
    if quantize_symbol in ram_audio_names:
        expected_ram_audio.add(quantize_symbol)
        rows = [f for f in ram_audio_functions if f[-1] == quantize_symbol]
        body = re.search(r"^" + quantize_symbol + r":\n(.*?)(?=^\S|\Z)", ra, re.M | re.S)
        if len(rows) != 1 or int(rows[0][-2], 16) > 64 or not body or ram_text_calls(body.group(1)):
            errors.append("BIT quantizer must be a unique <=64-byte call-free RAM leaf")
    master_prepare_flash = check_runtime_ram._macro(
        SRC, "firmware/src/instrument_fx.c", "BR_FX_MASTER_PREPARE_FLASH", 0)
    if master_prepare_flash not in (0, 1):
        errors.append("BR_FX_MASTER_PREPARE_FLASH must be exactly zero or one")
    if master_prepare_flash:
        for cold_symbol, cold_limit in (("br_master_fx_begin_block", 768),
                                         ("br_master_snapshot_ram", 128)):
            rows = [line.split() for line in syms.splitlines()
                    if len(line.split()) >= 6 and line.split()[2] == "F" and line.split()[-1] == cold_symbol]
            if (len(rows) != 1 or rows[0][3] != ".text" or
                    int(rows[0][-2], 16) > cold_limit or cold_symbol in ram_audio_names):
                errors.append(f"cold master setup must be unique bounded XIP code: {cold_symbol}")
        notes.append("master setup: bounded XIP control/coefficient preparation; sample kernels remain in RAM")
    fx_prepare_flash = check_runtime_ram._macro(
        SRC, "firmware/src/instrument_fx.c", "BR_FX_PREPARE_FLASH", 1)
    if fx_prepare_flash not in (0, 1):
        errors.append("BR_FX_PREPARE_FLASH must be exactly zero or one")
    if fx_prepare_flash:
        # Once-per-block (or rarer) FX helpers share br_fx_prepare_block's XIP
        # placement; RAM callers reach them through BR_FX_PREPARE_INVOKE.
        for cold_symbol in ("br_comp_snapshot", "br_comp_ratio", "br_comp_makeup_q15",
                            "br_comp_threshold_q15", "br_comp_bypassed", "br_comp_reset_detector",
                            "br_fx_delay_storage_reset", "br_fx_fusion_prepare"):
            if cold_symbol in ram_audio_names:
                errors.append(f"cold FX helper must stay in XIP while BR_FX_PREPARE_FLASH=1: {cold_symbol}")
        notes.append("FX prepare helpers: snapshot/bypass/detector reset/delay storage reset/fusion prepare in XIP")
    temporal_ram = check_runtime_ram._macro(
        SRC, "firmware/plaits/upstream/plaits/dsp/oscillator/harmonic_oscillator.h",
        "FM1_HARMONIC_TEMPORAL_RAM", 0)
    if temporal_ram not in (0, 1):
        errors.append("FM1_HARMONIC_TEMPORAL_RAM must be exactly zero or one")
    temporal_symbol = "_ZN6plaits18HarmonicOscillatorILi12EE14RenderTemporalILi13EEEbfPKfPfm"
    triple_ram = check_runtime_ram._macro(
        SRC, "firmware/plaits/upstream/plaits/dsp/oscillator/harmonic_oscillator.h",
        "FM1_HARMONIC_TRIPLE_RAM", 0)
    if triple_ram not in (0, 1):
        errors.append("FM1_HARMONIC_TRIPLE_RAM must be exactly zero or one")
    temporal_ram_placed = bool(temporal_ram and not triple_ram)
    if temporal_ram_placed:
        expected_ram_audio.add(temporal_symbol)
        required_ram_audio.add(temporal_symbol)
    elif temporal_symbol in ram_audio_names:
        errors.append("Harmonic Temporal kernel present in RAM while its placement selector is disabled")
    triple_symbol = "_ZN6plaits18HarmonicOscillatorILi12EE12RenderTripleILi1EEEbRS1_S3_fPKfPfS5_S5_S6_m"
    if triple_ram:
        expected_ram_audio.add(triple_symbol)
        required_ram_audio.add(triple_symbol)
        triple_fields = [f for f in ram_audio_functions if f[-1] == triple_symbol]
        if len(triple_fields) != 1 or int(triple_fields[0][-2], 16) > 2304:
            errors.append("Harmonic triple RAM kernel missing, duplicated, or larger than 2304 bytes")
        # pi32v2 objdump can decode byte sequences from this function's
        # undocumented instructions as a spurious local `call`. Audit the
        # compiler-emitted function listing, not that incomplete disassembly.
        triple_asm_path = OUT / "additive_engine.s"
        try:
            triple_asm = triple_asm_path.read_text()
        except OSError:
            triple_asm = ""
        triple_asm_body = re.search(
            r"(?ms)^" + re.escape(triple_symbol) + r":.*?^\s*\.size\s+" +
            re.escape(triple_symbol) + r"\s*,", triple_asm)
        triple_asm_calls = (re.findall(
            r"(?m)^\s*(?:call[a-z0-9]*|syscall)\b", triple_asm_body.group(0))
            if triple_asm_body else None)
        if triple_asm_calls is None or triple_asm_calls:
            errors.append("Harmonic triple RAM compiler listing must be present and call-free")
        else:
            listing_sha = hashlib.sha256(triple_asm_path.read_bytes()).hexdigest()
            notes.append(
                "Harmonic triple RAM: exact approved symbol, <=2304 bytes, compiler body call-free; "
                "additive_engine.s sha256 " + listing_sha)
    elif triple_symbol in ram_audio_names:
        errors.append("Harmonic triple RAM kernel present while selector is disabled")
    float_mix_gain = check_runtime_ram._macro(
        SRC, "firmware/src/instrument_fx.c", "BR_FX_FLOAT_MIX_GAIN", 0)
    wide_mix_symbol = "br_fx_blend127_wide"
    if float_mix_gain not in (0, 1):
        errors.append("BR_FX_FLOAT_MIX_GAIN must be exactly zero or one")
    # The same exact leaf also serves master saturation/wow fallbacks,
    # independently of the cached float-mix selector.
    if float_mix_gain or wide_mix_symbol in ram_audio_names:
        expected_ram_audio.add(wide_mix_symbol)
        required_ram_audio.add(wide_mix_symbol)
        wide_fields = [f for f in ram_audio_functions if f[-1] == wide_mix_symbol]
        if len(wide_fields) != 1 or int(wide_fields[0][-2], 16) > 256:
            errors.append("FX wide blend helper missing, duplicated, or larger than 256 bytes")
        wide_body = re.search(r"^" + wide_mix_symbol + r":\n(.*?)(?=^\S|\Z)", ra, re.M | re.S)
        if not wide_body or ram_text_calls(wide_body.group(1)):
            errors.append("FX wide blend helper must be present and call-free")
        else:
            notes.append("FX wide blend fallback: exact symbol, <=256 bytes, call-free")
    modal_kernel_ram = check_runtime_ram._macro(
        SRC, "firmware/plaits/upstream/plaits/dsp/physical_modelling/resonator.h",
        "FM1_MODAL_DF_KERNEL_RAM", 0)
    if modal_kernel_ram not in (0, 1):
        errors.append("FM1_MODAL_DF_KERNEL_RAM must be exactly zero or one")
    modal_kernel_symbol = "_ZN6plaits12ResonatorSvfILi4EE24RenderPairedDirectKernelILb1EEEvPKfPfmPNS1_14HybridModeWorkES7_iff"
    scalar_modal_kernel_symbol = "_ZN6plaits12ResonatorSvfILi4EE18RenderDirectKernelILb1EEEvPKfPfmPNS1_17DirectKernelStateE"
    if modal_kernel_ram:
        expected_ram_audio.add(modal_kernel_symbol)
        required_ram_audio.add(modal_kernel_symbol)
        kernel_fields = [f for f in ram_audio_functions if f[-1] == modal_kernel_symbol]
        if len(kernel_fields) != 1 or int(kernel_fields[0][-2], 16) > 1024:
            errors.append("Modal paired RAM kernel missing, duplicated, or larger than the 1024-byte dual-recurrence budget")
        kernel_body = re.search(r"^" + re.escape(modal_kernel_symbol) + r":\n(.*?)(?=^\S|\Z)", ra, re.M | re.S)
        if not kernel_body or ram_text_calls(kernel_body.group(1)):
            errors.append("Modal paired RAM recurrence kernel must be present and call-free")
        else:
            notes.append("Modal paired RAM recurrence: exact approved symbol, <=1024 bytes, call-free")
        if scalar_modal_kernel_symbol in ram_audio_names:
            errors.append("Scalar modal RAM kernel is not approved while paired recurrence is selected")
    elif modal_kernel_symbol in ram_audio_names or scalar_modal_kernel_symbol in ram_audio_names:
        errors.append("Modal RAM kernel present while its selector is disabled")
    # Plaits kit drum loops: exact call-free RAM leaves, reached from XIP callers
    # through a volatile pointer. pi32v2 objdump decodes some FPU opcodes as a
    # spurious `call`, so the compiler listing is audited instead.
    for selector, header, drum_symbol, drum_limit, drum_listing in (
            ("FM1_PLAITS_KICK_RAM", "analog_bass_drum.h",
             "_ZN6plaits14AnalogBassDrum9RenderRamEbbffffffPfm",
             PLAITS_KICK_RAM_LIMIT, "bass_drum_engine.s"),
            ("FM1_PLAITS_SQUARE_NOISE_RAM", "hi_hat.h",
             "_ZN6plaits11SquareNoise9RenderRamEfPfS1_S1_m",
             PLAITS_SQUARE_NOISE_RAM_LIMIT, "hi_hat_engine.s")):
        drum_ram = check_runtime_ram._macro(
            SRC, "firmware/plaits/upstream/plaits/dsp/drums/" + header, selector, 0)
        if drum_ram not in (0, 1):
            errors.append(f"{selector} must be exactly zero or one")
            continue
        if not drum_ram:
            if drum_symbol in ram_audio_names:
                errors.append(f"Plaits drum RAM loop present while {selector}=0: {drum_symbol}")
            continue
        expected_ram_audio.add(drum_symbol)
        required_ram_audio.add(drum_symbol)
        drum_fields = [f for f in ram_audio_functions if f[-1] == drum_symbol]
        if len(drum_fields) != 1 or int(drum_fields[0][-2], 16) > drum_limit:
            errors.append(f"Plaits drum RAM loop {drum_symbol} missing, duplicated, or over {drum_limit} bytes")
            continue
        try:
            drum_asm = (OUT / drum_listing).read_text()
        except OSError:
            drum_asm = ""
        drum_body = re.search(r"(?ms)^" + re.escape(drum_symbol) + r":.*?^\s*\.size\s+" +
                              re.escape(drum_symbol) + r"\s*,", drum_asm)
        if not drum_body or re.search(r"(?m)^\s*(?:call[a-z0-9]*|syscall)\b", drum_body.group(0)):
            errors.append(f"Plaits drum RAM loop {drum_symbol}: compiler listing must be present and call-free")
        else:
            notes.append(f"Plaits drum RAM loop: {drum_symbol} {int(drum_fields[0][-2], 16)} B of <= "
                         f"{drum_limit}, compiler body call-free")
    if not fusion_only:
        required_ram_audio.add("br_fx_process_pipeline")
    elif any("br_audio_pipeline_frames" in line for line in syms.splitlines()):
        errors.append("fusion-only build unexpectedly retains experimental pipeline frame buffers")
    if len(renderer_functions) != 8 or not ram_audio_names.issubset(expected_ram_audio) or not (
            required_ram_audio.issubset(ram_audio_names)):
        errors.append(".ram_audio must contain eight FM callbacks and the FX entry/worker, with only approved render functions")
    ra_calls = ram_text_calls(ra)
    worker_roots = {"fm1_cpu1_worker_start_stub", "fm1_cpu1_worker_main", "worker_dispatch_target"}
    if development_diagnostics:
        worker_roots.add("worker_smoke_callback")
    worker_present = worker_roots.issubset(ram_audio_names)
    if not worker_present:
        errors.append("required resident CPU1 worker functions must all be in .ram_audio")
    if not development_diagnostics and "worker_smoke_callback" in ram_audio_names:
        errors.append("distribution build must not contain the development worker smoke task")
    for ln in ra_calls:
        ins = LINE.match(ln)
        target = re.search(r"\bcall[a-z0-9]*\s+-?\d+\s+<[^:>]*:\s*([0-9a-f]+)\s*>", ins.group(3)) if ins else None
        if target and rt_start <= int(target.group(1), 16) < rt_end:
            errors.append(f".ram_audio calls into flash-critical .ram_text: {ln}")
        if target and worker_present and any(name in ln for name in ("fm1_cpu1_worker_main", "worker_dispatch_target")):
            addr = int(target.group(1), 16)
            if not ra_start <= addr < ra_end:
                errors.append(f"CPU1 worker RAM call targets outside .ram_audio: {ln}")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")

    ram = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - 0x01C20000
    notes.append(f"image {len(img)} B of {APP_SLOT}; RAM .data+.bss {ram} B of 98304; pool {pool} B of 344064; RAM code {(rt_end-rt_start)+(ra_end-ra_start)} B of {ram_code_limit()-0x01C00000} (STATUS page reserved)")
    if ram > 96 * 1024:
        errors.append(f"RAM .data+.bss {ram} B > 96 KiB")
    if pool > 344064 or pool < 0:
        errors.append(f"pool {pool} B outside 344064-byte reservation")
    for helper in ("__udivdi3", "__divdi3", "__umoddi3", "__moddi3"):
        if re.search(r"\b" + helper + r"$", syms, re.M):
            errors.append(f"unsupported 64-bit division helper linked: {helper}")
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    source_files = [*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c"),
                    *BRAIDS_CPP, *APP_C, *DSP_C,
                    *((FW / "braids").rglob("*.cc")), *((FW / "braids").rglob("*.h"))]
    for f in sorted(source_files):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def build_settings(a, capture, runtime_ram_settings, report):
    """build-settings.json; report is the package validation (None: no package)."""
    per_source_optimization = {
        **{str(src.relative_to(FW)): a.app_opt for src in [FW / "src" / "dinghy.c", *APP_C]},
        **{str(src.relative_to(FW)): a.dsp_opt for src in [FW / "src" / "instrument_audio_unit.c", *DSP_C, *BRAIDS_CPP, *PLAITS_CPP]},
        **{str(src.relative_to(FW)): "Os" for src in HAL_C},
        "loader/loader.c": "Os",
    }
    if PATTERN_DEVICE_CANDIDATE:
        per_source_optimization["src/audio_fixed8_plan_unit.c"] = "Os"
    if POLY_SIZE_CONSTRAINED:
        for relative in sorted(POLY_SIZE_OS_SOURCES):
            per_source_optimization["plaits/" + relative] = "Os"
    cold_tu_os_enabled = plaits_cold_tu_os()
    cold_tu_os_selected_sources = []
    if cold_tu_os_enabled:
        cold_tu_os_selected_sources = sorted(PLAITS_COLD_TU_OS_SOURCES)
        for relative in cold_tu_os_selected_sources:
            per_source_optimization["plaits/" + relative] = "Os"
    harmonic_triple_ram_enabled = check_runtime_ram._macro(
        SRC, "firmware/plaits/upstream/plaits/dsp/oscillator/harmonic_oscillator.h",
        "FM1_HARMONIC_TRIPLE_RAM", 0)
    harmonic_listing_path = OUT / "additive_engine.s"
    harmonic_listing_sha256 = (
        hashlib.sha256(harmonic_listing_path.read_bytes()).hexdigest()
        if harmonic_triple_ram_enabled and harmonic_listing_path.is_file() else None)
    fixed8_solver_ram_enabled = configured_macro(
        "firmware/src/audio_fixed8_plan.h", "BR_AUDIO_FIXED8_SOLVER_RAM", 0)
    failure_profile_settings = {
        "base": check_runtime_ram._macro(
            SRC, "firmware/src/instrument_failure_job_profile.h",
            "BR_AUDIO_FAILURE_JOB_PROFILE_DIAG", 0),
        "stage": check_runtime_ram._macro(
            SRC, "firmware/src/instrument_failure_job_profile.h",
            "BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG", 0),
        "fx_substage": check_runtime_ram._macro(
            SRC, "firmware/src/instrument_failure_job_profile.h",
            "BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG", 0),
        "model_substage": check_runtime_ram._macro(
            SRC, "firmware/src/instrument_failure_job_profile.h",
            "BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG", 0),
    }
    wave_scratch_bss_flags = [flag for flag in CFLAGS
                              if flag.startswith("-DFM1_PLAITS_WAVES_BOOT_SCRATCH_BSS=")]
    if len(wave_scratch_bss_flags) != 1:
        raise SystemExit("expected one explicit waveform boot-scratch BSS selector in CFLAGS")
    wave_scratch_bss = wave_scratch_bss_flags[0].rsplit("=", 1)[1]
    if wave_scratch_bss not in ("0", "1"):
        raise SystemExit("FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS must be zero or one")
    settings = {
        "product_identity": PRODUCT,
        "app_version": VERSION,
        "requested_defines": list(a.define),
        "compiler_flags": list(CFLAGS),
        "sdk_required_sha256": dict(SDK_SHA256),
        "app_optimization": a.app_opt, "dsp_optimization": a.dsp_opt,
        "loader_optimization": "Os",
        "per_source_optimization": per_source_optimization,
        "braids_perc_no_jump_tables": braids_perc_no_jump_tables(),
        "plaits_cold_tu_os": {
            "selector_path": "firmware/hal/fm1_math_policy.h",
            "selector_name": "FM1_PLAITS_COLD_TU_OS",
            "enabled": cold_tu_os_enabled,
            "allowlist": sorted(PLAITS_COLD_TU_OS_SOURCES),
            "selected_sources": cold_tu_os_selected_sources,
            "optimization": "Os",
            "floating_point_flags": "identical to global DSP flags; only -O level is overridden",
        },
        "floating_point_contraction": "dsp-fast/app-hal-loader-off" if dsp_relaxed_fp() else "off",
        "fast_math": False,
        "dsp_relaxed_fp": dsp_relaxed_fp(),
        "dsp_relaxed_fp_flags": DSP_RELAXED_FP_FLAGS if dsp_relaxed_fp() else [],
        "diagnostics_configuration": a.diagnostics,
        "ui_hd": True,                  # always: read by other tooling and the reference settings files
        "diagnostics_free": a.diagnostics == "distribution",
        "diagnostics_free_scope": TARGET["diagnostics_free_scope"],
        "fault_ram_code_capture_read": bool(capture),
        "optional_app_p33_and_work_read_commands": bool(capture),
        "runtime_profile_explicit": a.runtime_profile,
        "pattern_panel_test": a.pattern_panel_test,
        "panel_test_omits_popframe_export_0x94": False,
        "panel_test_omits_c1_sidecar_export_0x96": a.pattern_panel_test,
        "pattern_sector_test": a.pattern_sector_test,
        "harmonic_triple_ram": {
            "enabled": bool(harmonic_triple_ram_enabled),
            "compiler_listing": "build/additive_engine.s" if harmonic_triple_ram_enabled else None,
            "compiler_listing_sha256": harmonic_listing_sha256,
        },
        "fixed8_solver": {
            "ram_selected": bool(fixed8_solver_ram_enabled),
            "audited_section": ".ram_audio" if fixed8_solver_ram_enabled else ".text",
        },
        "failure_job_profile": failure_profile_settings,
        "plaits_wave_boot_scratch_bss": wave_scratch_bss == "1",
        "runtime_ram_audit": runtime_ram_settings,
        "ram_tables": True,
    }
    if POLY_RECORDED:               # FM-1 without polyphony keeps the exact settings shape it always had
        settings["polyphony"] = True
    if POLY_SIZE_CONSTRAINED:
        settings["poly_size_os_sources"] = sorted(POLY_SIZE_OS_SOURCES)
    if not TARGET["settings_tail"]:
        settings["package_sha256"] = report["sha256"] if report else None
    else:
        settings.update({
            "target": TARGET["name"],
            "app_identity": PRODUCT,
            "app_slot_bytes": APP_SLOT,
            "installable": report is not None,
            "package_sha256": report["sha256"] if report else None,
        })
    return settings


def resolved_config(a, capture, polyphony, package_name, packaged, missing):
    """--print-config: the resolved target configuration, nothing built."""
    app_defines = [f'-DFELUCCA_ID="{PRODUCT}"', f'-DAPP_VER="{VERSION}"', "-DBR_UI_HD=1"]
    sdk = a.sdk or os.environ.get("AC79_SDK")
    sdk_pin = {}
    for rel, sha in SDK_SHA256.items():
        path = Path(sdk) / "cpu" / "wl82" / "tools" / rel if sdk else None   # as fm1pkg_make.sdk_file
        sdk_pin[rel] = {"sha256": sha, "matches": (hashlib.sha256(path.read_bytes()).hexdigest() == sha)
                        if path and path.is_file() else None}
    settings = build_settings(a, capture, None, None)
    settings["harmonic_triple_ram"]["compiler_listing_sha256"] = None   # needs a build
    if "installable" in settings:
        settings["installable"] = packaged
    return {
        "target": a.target,
        "product_name": TARGET["name"],
        "product_define": f"-DBR_PRODUCT={TARGET['product_define']}",
        "product_header": "firmware/product.h (force-included: -include firmware/product.h)",
        "polyphony": polyphony,
        "identity": PRODUCT,
        "identity_pattern": TARGET["identity_prefix"] + "[0-9]+",
        "version": VERSION,
        "release": a.release,
        "app_slot": APP_SLOT,
        "app_slot_hex": f"0x{APP_SLOT:X}",
        "linker_script": "firmware/app.ld" if APP_SLOT == fm1pkg_make.APP_SLOT else
                         f"generated from firmware/app.ld with XIP LENGTH = 0x{APP_SLOT:X}",
        "package_layout": "smk37" if TARGET["smk37_layout"] else "fm1",
        "packaged": packaged,
        "output_dir": str(OUT.relative_to(SRC)) if OUT.is_relative_to(SRC) else str(OUT),
        "package_path": str((OUT / package_name).relative_to(SRC)) if packaged and OUT.is_relative_to(SRC) else None,
        "scanner_helpers": TARGET["scanner_helpers"],
        "cflags": CFLAGS,
        "app_defines": app_defines,
        "sdk": {"path": str(sdk) if sdk else None, "pin": sdk_pin},
        "link_ready": TARGET["link_ready"],
        "missing_sources": missing,
        "build_settings": settings,
        "build_settings_pending": ["runtime_ram_audit", "package_sha256",
                                   "harmonic_triple_ram.compiler_listing_sha256"],
    }


def main():
    global PRODUCT, VERSION, OUT, GEN, LDR, PATTERN_TARGET_EXPERIMENT
    global PATTERN_NAMESPACE_TARGET_EXPERIMENT, PATTERN_PROJECT_UI_TARGET_EXPERIMENT, PATTERN_DEVICE_CANDIDATE
    global TARGET, APP_SLOT, POLY_RECORDED, POLY_SIZE_CONSTRAINED
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", choices=sorted(TARGETS), default="fm1",
                    help="product: fm1 (FM-1, default) or smk37 (SMK-37 Pro)")
    ap.add_argument("--print-config", action="store_true",
                    help="print the resolved target configuration as JSON and exit without building")
    ap.add_argument("--force-incomplete-target", action="store_true",
                    help="porting only: try to build a target whose sources are not complete yet")
    ap.add_argument("--release", metavar="X.Y", help="release build: version string X.Y (identity from --product)")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--product", help="private build's numeric package identity: FM-1_<digits> "
                    "(--target fm1, default FM-1_9261389) or \"SMK-37 Pro_<digits>\" "
                    "(--target smk37, default SMK-37 Pro_9650001)")
    ap.add_argument("--build-number", help="four-digit paired JamesOS build number; prefixes the selected instrument identity")
    ap.add_argument("--app-opt", choices=("Oz", "Os", "O2", "O3"), default="Os",
                    help="application optimization level; loader always uses Os (default: Os)")
    ap.add_argument("--dsp-opt", choices=("Os", "O2", "O3"), default="O3",
                    help="sound-engine optimization level (default: O3)")
    ap.add_argument("--runtime-profile", action="store_true",
                    help="allow explicitly instrumented scanner/VCF builds and classify them as diagnostic-only")
    ap.add_argument("--define", action="append", default=[], metavar="NAME=VALUE",
                    help="extra compiler define for every unit (trial switches, e.g. BR_MASTER_LEVELER=1); repeatable")
    ap.add_argument("--diagnostics", choices=("development", "distribution"), default="development",
                    help="development enables enhanced diagnostics; distribution excludes development-only diagnostic features")
    ap.add_argument("--pattern-sector-test", action="store_true", help="enable bounded read-only project-sector diagnostics")
    ap.add_argument("--pattern-panel-test", action="store_true", help="enable guarded panel test controls in a candidate")
    ap.add_argument("--pattern-device-candidate", action="store_true",
                    help="experimental pattern-only device test package; no automatic installation")
    poly_opt = ap.add_mutually_exclusive_group()
    poly_opt.add_argument("--polyphony", action="store_true",
                          help="build the device candidate with stopped-transport polyphony (-DBR_CAP_POLY=1): "
                               "the default for both targets (accepted, changes nothing)")
    poly_opt.add_argument("--no-polyphony", action="store_true",
                          help="build the device candidate without stopped-transport polyphony. --target smk37: "
                               "-DBR_CAP_POLY=0, same output directory. --target fm1: the pre-polyphony FM-1 build "
                               "exactly (no BR_CAP_POLY define), outputs in a separate -nopoly directory")
    ap.add_argument("--pattern-target-experiment", action="store_true",
                    help="build-only unshippable pattern owner RAM/link experiment")
    ap.add_argument("--pattern-namespace-target-experiment", action="store_true",
                    help="force-link namespace prototype; never provision or install")
    ap.add_argument("--pattern-project-ui-target-experiment", action="store_true",
                    help="link gated, policy-unbound project UI; never install")
    ap.add_argument("--pattern-provision-target-experiment", action="store_true",
                    help="link unbound provisioning protocol for offline tests; never install")
    ap.add_argument("--pattern-provisional-pin", type=Path,
                    help="private provisional identity pin for gated link-only build")
    ui_opt = ap.add_mutually_exclusive_group()
    ui_opt.add_argument("--ui-hd", action="store_true",
                        help="the HD interface (-DBR_UI_HD=1), the only interface (accepted, changes nothing)")
    ui_opt.add_argument("--ui-standard", action="store_true",
                        help="REMOVED: the standard (pixel) interface was removed; this flag is an error")
    a = ap.parse_args()
    if a.ui_standard:
        ap.error("the standard interface was removed")
    TARGET = TARGETS[a.target]
    PRODUCT, VERSION, APP_SLOT = TARGET["default_product"], TARGET["version"], TARGET["app_slot"]
    if a.build_number is not None:
        if a.product is not None:
            ap.error("use either --build-number or --product, not both")
        if not re.fullmatch(r"[0-9]{4}", a.build_number):
            ap.error("--build-number must contain exactly four digits")
        a.product = ("FM-1_967" if a.target == "fm1" else "SMK-37 Pro_965") + a.build_number
    if a.product is None:
        a.product = PRODUCT
    # Polyphony (firmware/product.h BR_CAP_POLY) is a capability of both products; it exists only
    # in device candidates. TARGET["polyphony"] is the target's default (on for both). FM-1 without
    # polyphony gets no BR_CAP_POLY define, so it keeps exactly the flags and bytes it always had;
    # FM-1 with polyphony is the size-constrained configuration, whichever flag selected it.
    if a.polyphony and not a.pattern_device_candidate:
        ap.error("--polyphony requires --pattern-device-candidate (polyphony exists only in device candidates)")
    polyphony = a.pattern_device_candidate and (a.polyphony or (TARGET["polyphony"] and not a.no_polyphony))
    POLY_RECORDED = bool(polyphony and TARGET["poly_split_outputs"])
    POLY_SIZE_CONSTRAINED = bool(polyphony and TARGET["poly_size_constrained"])
    # FM-1 --no-polyphony candidate: its own output directory, never mixed with the default's objects
    split = a.pattern_device_candidate and TARGET["poly_split_outputs"] and not polyphony
    poly_out_suffix = "nopoly" if split else ""
    # One product switch for every C/C++ unit (app, DSP, HAL, loader): see firmware/product.h.
    CFLAGS.extend([f"-DBR_PRODUCT={TARGET['product_define']}", "-include", "firmware/product.h"])
    for define in a.define:
        if not re.fullmatch(r"[A-Z_][A-Z0-9_]*(=[A-Za-z0-9_]+)?", define):
            ap.error(f"--define {define!r}: expected NAME or NAME=VALUE")
        CFLAGS.append("-D" + define)
    if polyphony or TARGET["poly_define_when_off"]:
        CFLAGS.append(f"-DBR_CAP_POLY={int(polyphony)}")
    if a.diagnostics == "distribution" and (a.runtime_profile or a.pattern_sector_test or a.pattern_panel_test or
            a.pattern_namespace_target_experiment or a.pattern_provision_target_experiment or a.pattern_provisional_pin or
            (a.pattern_target_experiment and not a.pattern_device_candidate) or
            (a.pattern_project_ui_target_experiment and not a.pattern_device_candidate)):
        ap.error("distribution diagnostics cannot be combined with profiling or experimental test modes")
    capture = 1 if a.diagnostics == "development" else 0
    CFLAGS.extend([f"-DFM1_FAULT_RAM_CODE_CAPTURE_READ={capture}",
                   f"-DFM1_ENABLE_P33_REGISTER_SNAPSHOT={capture}",
                   f"-DFM1_DEVELOPMENT_DIAGNOSTICS={capture}"])
    if not a.print_config:          # --print-config: stdout is the JSON only
        print(f"diagnostics configuration: {a.diagnostics} (RAM code capture/read={capture}, optional app read commands={capture})")
    if a.diagnostics == "distribution" and not a.pattern_device_candidate:
        OUT = SRC / "build" / "distribution"
        GEN = OUT / "gen"
        LDR = OUT / "loader"
    if a.pattern_sector_test and not a.pattern_device_candidate:
        ap.error("pattern sector test requires a device candidate")
    if a.pattern_panel_test and not a.pattern_device_candidate:
        ap.error("pattern panel test requires a device candidate")
    if a.pattern_device_candidate:
        if a.release or a.pattern_namespace_target_experiment or a.pattern_provision_target_experiment or a.pattern_provisional_pin:
            ap.error("pattern device candidate cannot use release, namespace, provisioning or identity flags")
        if a.app_opt != "Oz" or a.dsp_opt != "O3":
            ap.error("pattern device candidate requires explicit --app-opt Oz and default O3 DSP")
        PATTERN_DEVICE_CANDIDATE = True
        a.pattern_target_experiment = True
        a.pattern_project_ui_target_experiment = True
    if a.pattern_provisional_pin and not (a.pattern_provision_target_experiment and a.pattern_project_ui_target_experiment):
        ap.error("provisional pin requires provisioning and project UI experiments")
    if a.pattern_provisional_pin and (a.app_opt != "Os" or a.dsp_opt != "O3" or a.runtime_profile or a.product != PRODUCT):
        ap.error("provisional pin requires the default private-build recipe")
    if a.pattern_provision_target_experiment and not a.pattern_namespace_target_experiment:
        ap.error("pattern provisioning experiment requires --pattern-namespace-target-experiment")
    if (a.pattern_namespace_target_experiment or a.pattern_project_ui_target_experiment) and not a.pattern_target_experiment:
        ap.error("pattern namespace/UI experiments require --pattern-target-experiment")
    if a.pattern_target_experiment:
        if a.release:
            ap.error("pattern target experiment cannot be a release")
        PATTERN_TARGET_EXPERIMENT = True
        out_name = "pattern-device-candidate" if PATTERN_DEVICE_CANDIDATE else "pattern-target-experiment"
        OUT = SRC / "build" / (out_name + ("-distribution" if a.diagnostics == "distribution" else ""))
        if a.pattern_provisional_pin:
            # The generated header, preprocessed compiler output and binary
            # contain the per-build secret. Never reuse public build outputs.
            os.umask(0o077)
            private_root = SRC / "build" / "pattern-private"
            private_root.mkdir(mode=0o700, parents=True, exist_ok=True)
            if private_root.is_symlink() or not private_root.is_dir() or private_root.stat().st_mode & 0o077:
                raise SystemExit("private pattern build directory is not private")
            OUT = Path(tempfile.mkdtemp(prefix="fm1-", dir=private_root))
        GEN = OUT / "gen"
        LDR = OUT / "loader"
        CFLAGS.extend(["-DFM1_PATTERN_TARGET_EXPERIMENT=1", "-DFM1_PATTERN_AUDIO_IRQ_PROTOTYPE=1",
                       "-DFM1_PATTERN_TICK_CLAIM_PROTOTYPE=1", "-DBR_PATTERN_SONG_EXPERIMENT=1",
                       "-DBR_PATTERN_OWNER_HOST=1", "-DBR_PATTERN_BANK_EXPERIMENT=1",
                       "-DBR_PATTERN_SESSION_LOAN_EXPERIMENT=1"])
        APP_C.extend(FW / "src" / name for name in ("pattern_snapshot.c", "pattern_storage_codec.c",
                    "pattern_bank.c", "pattern_owner.c", "pattern_song.c", "pattern_project.c",
                    "pattern_project_adapter.c", "pattern_flash_store.c", "pattern_park_io.c"))
        if a.pattern_panel_test:
            CFLAGS.append("-DFM1_PATTERN_PANEL_TEST=1")
        if a.pattern_sector_test:
            CFLAGS.append("-DFM1_PATTERN_SECTOR_TEST=1")
        if PATTERN_DEVICE_CANDIDATE:
            CFLAGS.extend(["-DFM1_PATTERN_DEVICE_CANDIDATE=1", "-DFM1_PATTERN_RUNTIME_TARGET_LINK_ONLY=1", "-DFM1_PATTERN_STREAM_ONLY=1", "-DFM1_PATTERN_V16_ONLY=1"])
            APP_C.extend(FW / "src" / name for name in ("pattern_codec_source.c", "pattern_compressed_project_shared.c", "pattern_owned_ps.c", "pattern_fixed_project.c", "pattern_fixed_bridge.c", "pattern_fixed_stage.c", "pattern_pair_stream.c", "pattern_pair_owner.c", "pattern_pair_bridge.c", "pattern_pair_project.c", "pattern_stream_project.c", "pattern_fixed_runtime.c", "pattern_sha256.c"))
            if "FM1_PATTERN_DELETE_SIZING=1" in a.define:
                if "FM1_PATTERN_DELETE_EXPERIMENT=1" not in a.define:
                    ap.error("delete sizing requires the guarded deletion experiment")
                APP_C.append(FW / "src" / "project_delete_gate.c")
            if "FM1_PATTERN_DELETE_PROTOCOL_EXPERIMENT=1" in a.define:
                if "FM1_PATTERN_DELETE_EXPERIMENT=1" not in a.define:
                    ap.error("delete protocol requires the guarded deletion experiment")
                if "FM1_PATTERN_DELETE_SIZING=1" not in a.define:
                    APP_C.append(FW / "src" / "project_delete_gate.c")
                APP_C.append(FW / "src" / "project_delete_protocol.c")
        if a.pattern_namespace_target_experiment:
            PATTERN_NAMESPACE_TARGET_EXPERIMENT = True
            CFLAGS.append("-DFM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT=1")
            APP_C.extend(FW / "src" / name for name in ("pattern_namespace.c", "pattern_namespace_sha256.c", "pattern_journal_gate.c"))
            if not PATTERN_DEVICE_CANDIDATE:
                APP_C.append(FW / "src" / "pattern_sha256.c")
        if a.pattern_provision_target_experiment:
            CFLAGS.append("-DFM1_PATTERN_PROVISION_EXPERIMENT=1")
            APP_C.append(FW / "src" / "pattern_provision_protocol.c")
        if a.pattern_provisional_pin:
            from fm1_pattern_identity import write_header
            from fm1_pattern_provision import ProvisionError
            try:
                write_header(a.pattern_provisional_pin, GEN, SRC)
            except ProvisionError as exc:
                raise SystemExit(f"provisional identity: {exc}")
            CFLAGS.extend(["-DFM1_PATTERN_IDENTITY_PINNED=1", "-I"+str(GEN.relative_to(SRC))])
        if a.pattern_project_ui_target_experiment:
            PATTERN_PROJECT_UI_TARGET_EXPERIMENT = True
            CFLAGS.append("-DFM1_PATTERN_PROJECT_TARGET_EXPERIMENT=1")
            if not PATTERN_DEVICE_CANDIDATE:
                APP_C.append(FW / "src" / "pattern_project_target_bridge.c")
    if TARGET["out_suffix"] and not a.pattern_provisional_pin:
        suffix = TARGET["out_suffix"]
        OUT = OUT / suffix if OUT == SRC / "build" else OUT.parent / (OUT.name + "-" + suffix)
        GEN = OUT / "gen"
        LDR = OUT / "loader"
    if poly_out_suffix:             # FM-1 --no-polyphony candidate: never mix the outputs
        OUT = OUT.parent / (OUT.name + "-" + poly_out_suffix)   # a device candidate: never build/ itself
        GEN = OUT / "gen"
        LDR = OUT / "loader"
    cold_tu_os_enabled = plaits_cold_tu_os()
    prefix = TARGET["identity_prefix"]
    require_product = re.fullmatch(re.escape(prefix) + r"[0-9]+", a.product)
    if not require_product or len(a.product) > 20:
        ap.error(f"--product must be {prefix} followed by digits, at most 20 total characters"
                 f" (--target {a.target})")
    PRODUCT = a.product
    name = TARGET["package_name"].format(version=VERSION)
    if a.release:                   # one digit each: the identity has room for two (X.Y; a .Z only in the text)
        m = re.fullmatch(r"(\d)\.(\d)(\.\d)?(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y, X.Y.Z or X.Y-suffix, one digit each")
        VERSION = m[1] + "." + m[2] + (m[3] or "")
        name = TARGET["release_package_name"].format(release=a.release)
    packaged = not (PATTERN_TARGET_EXPERIMENT and not PATTERN_DEVICE_CANDIDATE) and \
        not (TARGET["package_requires_device_candidate"] and not PATTERN_DEVICE_CANDIDATE)
    if a.release and not packaged:
        # Releases are never device candidates (rule above, both targets), and SMK-37 Pro
        # packages only device candidates: a release would produce no package.
        ap.error(f"--release needs a packaged build; --target {a.target} packages only device candidates, "
                 "which cannot be releases")
    missing = [rel for rel in TARGET.get("required_sources", ()) if not (FW / rel).is_file()]
    if a.print_config:
        print(json.dumps(resolved_config(a, capture, polyphony, name, packaged, missing), indent=2))
        return 0
    if not TARGET["link_ready"] and not a.force_incomplete_target:
        raise SystemExit(
            f"build: --target {a.target} ({TARGET['name']}) cannot be built from this tree yet: its "
            f"firmware sources are not ported (missing: {', '.join(missing) or 'none, but not marked link_ready'}). "
            f"Nothing was compiled. Use --print-config to inspect the resolved configuration.")
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            raise SystemExit(f"SDK {rel} differs from the pinned AC79NN_SDK_V1.2.1 bytes")
    OUT.mkdir(parents=True, exist_ok=True)
    if PATTERN_TARGET_EXPERIMENT:
        # Never leave a prior successful runtime-only binary looking like a
        # current full experiment after a later failed compile/link.
        for filename in ("dinghy.elf", "dinghy.bin", "dinghy.dis", "dinghy.symbols"):
            (OUT / filename).unlink(missing_ok=True)
    # Failed recompiles must not leave a stale package/report looking current.
    (OUT / name).unlink(missing_ok=True)
    (OUT / "package-validation.json").unlink(missing_ok=True)
    (OUT / "runtime-ram-audit.json").unlink(missing_ok=True)
    ota = build_loader()
    img, syms, dis, rt, ra = build_app(a.app_opt, a.dsp_opt)
    errors, notes = check(img, syms, dis, rt, ra)
    hal_err = mmio_check()
    errors += hal_err
    runtime_ram_audit = None
    selectors = check_runtime_ram.configured_selectors(SRC)
    if selectors["scanner"] or selectors["vcf"]:
        smk37 = TARGET["smk37_layout"]
        if PATTERN_TARGET_EXPERIMENT or TARGET["out_suffix"]:
            # Keep build outputs isolated without letting the linked audit read
            # stale binaries from the normal build directory (or another target's).
            with tempfile.TemporaryDirectory(prefix="pattern-audit-", dir=OUT) as temp:
                audit_root = Path(temp)
                (audit_root / "firmware").symlink_to(FW, target_is_directory=True)
                (audit_root / "build").symlink_to(OUT, target_is_directory=True)
                runtime_ram_audit = check_runtime_ram.audit_candidate(
                    audit_root, require_original_gpio=True, runtime_profile=a.runtime_profile, smk37=smk37)
                runtime_ram_audit["candidate"] = str(SRC)
        else:
            runtime_ram_audit = check_runtime_ram.audit_candidate(
                SRC, require_original_gpio=True, runtime_profile=a.runtime_profile, smk37=smk37)
        (OUT / "runtime-ram-audit.json").write_text(
            json.dumps(runtime_ram_audit, indent=2) + "\n")
        errors += runtime_ram_audit["errors"]
        notes.append("linked RAM placement: " +
                     ("passed" if runtime_ram_audit["passed"] else "failed") +
                     f"; classification {runtime_ram_audit['classification']}")
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    if a.pattern_provisional_pin:
        try:
            write_header(a.pattern_provisional_pin, GEN, SRC)
        except ProvisionError as exc:
            for filename in ("dinghy.elf", "dinghy.bin", "dinghy.dis", "dinghy.symbols"):
                (OUT / filename).unlink(missing_ok=True)
            raise SystemExit(f"provisional identity: source changed during build: {exc}")
    if PATTERN_TARGET_EXPERIMENT and not PATTERN_DEVICE_CANDIDATE:
        print("pattern target experiment: link/check only; no installable package generated")
        return
    report = None
    if packaged:
        pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY, smk37=TARGET["smk37_layout"]),
                              ota, PRODUCT)
        from validate_package import validate
        report = validate(pkg, img, (LDR / "loader.bin").read_bytes(), PRODUCT)
        (OUT / name).write_bytes(pkg)
        (OUT / "package-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    runtime_ram_settings = None
    if runtime_ram_audit is not None:
        runtime_ram_settings = {
            "classification": runtime_ram_audit["classification"],
            "qualification_eligible": runtime_ram_audit["qualification_eligible"],
            "passed": runtime_ram_audit["passed"],
            "report": "build/runtime-ram-audit.json",
            "report_sha256": hashlib.sha256((OUT / "runtime-ram-audit.json").read_bytes()).hexdigest(),
        }
    (OUT / "build-settings.json").write_text(json.dumps(
        build_settings(a, capture, runtime_ram_settings, report), indent=2) + "\n")
    print(f"app      {OUT / 'dinghy.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    if report:
        print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    else:
        print(f"{TARGET['name']} link check for {PRODUCT}; no package (not a device candidate)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
