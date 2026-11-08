#!/usr/bin/env python3
"""Check append-only FM-1 retained fault-record addresses and sizes."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


LEGACY = {
    "fm1_crash": (0x01C7C000, 0x40),
    "bootguard": (0x01C7C040, 0x0C),
    "fm1_cpu1_early_record": (0x01C7C04C, 0x28),
    "fm1_cpu1_early_cookie": (0x01C7C074, 0x08),
    "fm1_cpu1_early_snapshot": (0x01C7C07C, 0x24),
    "fm1_cpu1_early_trace": (0x01C7C0A0, 0x18),
    "fm1_fault_diag": (0x01C7C0B8, 0x188),
    "fm1_fault_bundle": (0x01C7C240, 0x340),
    "br_fx_progress_retained": (0x01C7C580, 0x14),
    "fm1_foreground_journal": (0x01C7C594, 0x2C),
    "fm1_fault_popframe_diag": (0x01C7C5C0, 0x50),
}
OPTIONAL = {
    "fm1_fault_c1_timeout_record": (0x01C7C610, 0x1A4),
    "fm1_fault_c0_stack_window_record": (0x01C7C7B4, 0x48),
}
RAM_CODE = {"fm1_fault_ram_code_record": (0x01C7C7FC, 0x40)}


def parse_symbols(text: str) -> dict[str, tuple[int, int]]:
    found: dict[str, tuple[int, int]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 5 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            # llvm-objdump/objdump symbol table: addr bind section size name
            try:
                addr, size = int(fields[0], 16), int(fields[-2], 16)
            except ValueError:
                continue
            found[fields[-1]] = (addr, size)
        elif len(fields) == 4 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]) and \
                re.fullmatch(r"[0-9a-fA-F]+", fields[1]):
            # nm -S -n: addr size type name
            found[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
    return found


def check_linker_order(text: str) -> None:
    sections = [".noinit.end", ".noinit.end.c1_timeout",
                ".noinit.end.c0_stack_window", ".noinit.end.ram_code"]
    positions = [text.find(section + " (NOLOAD)") for section in sections]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise ValueError("linker script must append the optional records after .noinit.end in order")


def validate(symbols: dict[str, tuple[int, int]], mode: str, linker_text: str,
             ram_code_mode: str = "off") -> None:
    check_linker_order(linker_text)
    for name, expected in LEGACY.items():
        actual = symbols.get(name)
        if actual != expected:
            raise ValueError(f"legacy retained symbol {name}: expected {expected!r}, got {actual!r}")
    if mode == "off":
        for name in OPTIONAL:
            if name in symbols:
                raise ValueError(f"optional symbol {name} is present in feature-off image")
        if ram_code_mode != "off":
            raise ValueError("RAM-code sidecar requires the existing timeout and stack records")
        for name in RAM_CODE:
            if name in symbols:
                raise ValueError(f"optional symbol {name} is present in feature-off image")
        return
    for name, expected in OPTIONAL.items():
        actual = symbols.get(name)
        if actual != expected:
            raise ValueError(f"appended retained symbol {name}: expected {expected!r}, got {actual!r}")
    pop_addr, pop_size = LEGACY["fm1_fault_popframe_diag"]
    c1_addr, c1_size = OPTIONAL["fm1_fault_c1_timeout_record"]
    c0_addr, c0_size = OPTIONAL["fm1_fault_c0_stack_window_record"]
    if c1_addr != pop_addr + pop_size or c0_addr != c1_addr + c1_size:
        raise ValueError("optional records are not contiguous after the unchanged 80-byte sidecar")
    if c0_addr + c0_size > 0x01C7FD50:
        raise ValueError("appended records exceed reserved NOINIT space")
    if ram_code_mode == "off":
        for name in RAM_CODE:
            if name in symbols:
                raise ValueError(f"optional symbol {name} is present with RAM-code sidecar disabled")
    else:
        for name, expected in RAM_CODE.items():
            actual = symbols.get(name)
            if actual != expected:
                raise ValueError(f"appended retained symbol {name}: expected {expected!r}, got {actual!r}")
        code_addr, code_size = RAM_CODE["fm1_fault_ram_code_record"]
        if code_addr != c0_addr + c0_size:
            raise ValueError("RAM-code record is not appended after the unchanged C0 stack record")
        if code_addr + code_size > 0x01C7FD50:
            raise ValueError("appended records exceed reserved NOINIT space")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--symbols", required=True, type=Path,
                        help="nm -S -n or objdump symbol table text")
    parser.add_argument("--linker-script", required=True, type=Path)
    parser.add_argument("--mode", choices=("off", "on"), required=True)
    parser.add_argument("--ram-code-mode", choices=("off", "on"), default="off")
    args = parser.parse_args()
    try:
        validate(parse_symbols(args.symbols.read_text()), args.mode,
                 args.linker_script.read_text(), args.ram_code_mode)
    except (OSError, ValueError) as exc:
        print(f"noinit layout check failed: {exc}", file=sys.stderr)
        return 1
    print(f"noinit layout check passed ({args.mode}, ram-code {args.ram_code_mode})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
