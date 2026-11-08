#!/usr/bin/env python3
"""Compile-only Plaits target probe for the JieLi pi32v2 R3 toolchain.

This does not link an app, package firmware, or access a device. It emits
probe assembly, undefined-symbol lists, and section-size reports under
build/plaits-target-check/.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "tools"))
import build as dinghy_build  # noqa: E402

FW = SRC / "firmware"
PL = FW / "plaits"
UP = PL / "upstream"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, default=SRC / "build" / "plaits-target-check",
                   help="report/object directory (default: build/plaits-target-check)")
    return p.parse_args()


def compiler_call(args: list[object]) -> str:
    return dinghy_build.tc("cc", *args)


def main() -> int:
    args = parse_args()
    try:
        toolchain = dinghy_build.toolchain()
    except SystemExit as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    out = args.out.resolve()
    try:
        out.relative_to(SRC)
    except ValueError:
        print("ERROR: --out must be inside this checkout (the Docker build mounts only the checkout).",
              file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    # Keep this aligned with the product C/C++ options, plus JieLi's documented
    # R3 single-precision selector. Do not silently fall back to another CPU.
    common = ["-mcpu=r3", "-Os", "-ffunction-sections", "-ffp-contract=off",
              "-fno-builtin", "-fwrapv", "-Wall", "-Wno-unused-function",
              "-DTEST", "-Dstmlib=plaits_stmlib"]
    cxx = ["-std=c++14", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
    includes = ["-Ifirmware/plaits", "-Ifirmware/plaits/upstream",
                "-Ifirmware/plaits/upstream/plaits", "-Ifirmware/plaits/upstream/stmlib"]
    diagnostics: list[str] = []
    statuses: list[tuple[str, bool]] = []

    def compile_one(label: str, command: list[object]) -> None:
        if "-o" in command:
            output = Path(command[command.index("-o") + 1])
            output.unlink(missing_ok=True)
        try:
            compiler_call(command)
            statuses.append((label, True))
        except SystemExit as exc:
            statuses.append((label, False))
            diagnostics.append(f"{label}: {exc}")

    probe = out / "float_probe.c"
    probe.write_text(
        "volatile float plaits_probe_sink;\n"
        "float plaits_float_probe(float a, float b) {\n"
        "  volatile float x = (a + b) * (a - b);\n"
        "  x = x / (b + 0.25f);\n"
        "  plaits_probe_sink = x;\n"
        "  return x;\n"
        "}\n"
        "float plaits_math_probe(float x) { return __builtin_sqrtf(x); }\n",
        encoding="utf-8")
    probe_asm = out / "float_probe.s"
    probe_obj = out / "float_probe.o"
    compile_one("float probe assembly", [*common, "-S", probe, "-o", probe_asm])
    compile_one("float probe object", [*common, "-c", probe, "-o", probe_obj])

    sources = sorted((UP / "plaits").rglob("*.cc")) + sorted((UP / "stmlib").rglob("*.cc"))
    sources += [PL / "plaits_c_api.cc", PL / "plaits_kit_c_api.cc"]
    objects: list[Path] = []
    for source in sources:
        rel = source.relative_to(SRC).as_posix()
        obj = out / (re.sub(r"[^A-Za-z0-9_.-]+", "_", rel) + ".o")
        compile_one(f"compile {rel}", [*common, *cxx, *includes, "-x", "c++", "-c", source, "-o", obj])
        if obj.exists():
            objects.append(obj)

    report: list[str] = [
        "Plaits pi32v2 R3 compile-only report",
        "No app link, firmware package, or device operation was performed.",
        f"Compiler: {toolchain / 'pi32v2/bin/clang'}",
        "Target flags: -target pi32v2 -mcpu=r3 -Os -fno-builtin -ffp-contract=off -DTEST -Dstmlib=plaits_stmlib",
        f"Sources discovered: {len(sources)}; objects emitted: {len(objects)}",
        "",
        "Compilation results:",
    ]
    report += [f"  {'PASS' if ok else 'FAIL'} {name}" for name, ok in statuses]
    if diagnostics:
        report += ["", "Compiler failures:", *[f"  {d}" for d in diagnostics]]

    # Assembly and undefined references are useful even when other TUs fail.
    if probe_asm.exists():
        report += ["", "Float probe assembly:", probe_asm.read_text(encoding="utf-8", errors="replace")]
    if probe_obj.exists() or objects:
        nm_tool = "pi32v2/bin/nm"
        objdump_tool = "common/bin/objdump"
        for tool in (nm_tool, objdump_tool):
            if not (toolchain / tool).exists():
                report.append(f"\nUnavailable report utility: {tool}")
        if (toolchain / nm_tool).exists():
            report += ["", "Undefined symbols by object (candidate runtime/library requirements):"]
            symbol_objects = ([probe_obj] if probe_obj.exists() else []) + objects
            for obj in symbol_objects:
                try:
                    symbols = dinghy_build.tc(nm_tool, "-u", obj).strip()
                except SystemExit as exc:
                    symbols = f"ERROR: {exc}"
                report.append(f"[{obj.name}]\n{symbols or '(none)'}")
        if (toolchain / objdump_tool).exists():
            report += ["", "Object section sizes:"]
            size_objects = ([probe_obj] if probe_obj.exists() else []) + objects
            for obj in size_objects:
                report.append(f"[{obj.name}]")
                try:
                    report.append(dinghy_build.tc(objdump_tool, "-h", obj).strip())
                except SystemExit as exc:
                    report.append(f"ERROR: {exc}")

    report_path = out / "report.txt"
    report_path.write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"Wrote {report_path}")
    failed = any(not ok for _, ok in statuses)
    print(f"Compile-only result: {'FAIL' if failed else 'PASS'}; this is not a target runtime test.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
