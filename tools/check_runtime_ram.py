#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linked scanner/VCF RAM closure checks used by builds and offline audits.

Importing this module has no CLI side effects. The build workflow calls
``audit_candidate`` only when a RAM-placement selector is enabled; the command
line entry point can also audit an already-linked candidate without rebuilding.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCANNER_RAM_SYMBOL_ALLOWLIST = (
    "fm1_timer5_irq", "fm1_input_tick", "fm1__led_lines", "fm1__sr_bit",
    "fm1__sr_latch", "fm1__sr_word", "fm1__rows", "fm1__key",
    "fm1__keys", "fm1__frame",
)
SCANNER_REQUIRED_ROOTS = ("fm1_timer5_irq", "fm1_input_tick", "fm1__sr_word")
SCANNER_INLINE_LEAVES = (
    "fm1__pa_out_update", "fm1__ph_out_update",
    "fm1__led_pwm_priority_mask_at", "fm1_timer5_usb_phase_due",
    "fm1_timer5_usb_phase_next",
)
VCF_RAM_SYMBOL_ALLOWLIST = (
    "_ZN6plaits23VariableShapeOscillator6RenderILb0ELb0ELb0EEEvfffffPfm",
    "_ZN6plaits22VirtualAnalogVCFEngine6RenderERKNS_16EngineParametersEPfS4_mPb",
)

POLICY_PATH = "firmware/src/instrument_pipeline_policy.h"
INPUT_PATH = "firmware/hal/fm1_input.h"
GPIO_INPUT_SELECTORS = (
    "FM1_INPUT_FAST_SHIFT", "FM1_INPUT_FAST_SR_SHADOW",
    "FM1_INPUT_COMPACT_SHIFT",
)
GPIO_TRACE_SCHEMA = "fm1.gpio-shift-trace-contract/v1"
VCF_SELECTOR_PATHS = (
    "firmware/plaits/upstream/plaits/dsp/oscillator/variable_shape_oscillator.h",
    "firmware/plaits/upstream/plaits/dsp/engine2/virtual_analog_vcf_engine.cc",
)
ARTIFACT_PATHS = ("build/dinghy.bin", "build/dinghy.dis", "build/dinghy.symbols")


def _read_text(candidate: Path, relative: str, errors: list[str]) -> str:
    try:
        return (candidate / relative).read_text()
    except OSError as exc:
        errors.append(f"missing or unreadable {relative}: {exc}")
        return ""


def _macro(candidate: Path, relative: str, name: str, default: Any = None) -> Any:
    try:
        text = (candidate / relative).read_text()
    except OSError:
        return default
    match = re.search(r"^\s*#define\s+" + re.escape(name) + r"\s+([^\s/]+)",
                      text, re.M)
    if not match:
        return default
    token = match[1]
    if token in ("0", "1"):
        return int(token)
    return token


def _selector_is_configured(value: Any) -> bool:
    # Treat malformed nonzero tokens as selected so the audit fails closed.
    return value not in (None, 0, "0")


def configured_selectors(candidate: Path) -> dict[str, Any]:
    """Read selector values without compiling or requiring linked artifacts."""
    candidate = Path(candidate).resolve()
    timer5 = _macro(candidate, POLICY_PATH, "FM1_TIMER5_RAM", 0)
    scanner = _macro(candidate, POLICY_PATH, "FM1_INPUT_SCANNER_RAM", 0)
    vcf_values = [_macro(candidate, path, "FM1_PLAITS_VCF_RENDER_RAM", 0)
                  for path in VCF_SELECTOR_PATHS]
    irq_trace = _macro(candidate, POLICY_PATH, "FM1_RUNTIME_IRQ_PROFILE", 0)
    chunk_trace = _macro(candidate, POLICY_PATH, "FM1_RUNTIME_CHUNK_PROFILE", 0)
    drum_profile = _macro(candidate, "firmware/hal/fm1_drum_profile.h",
                          "FM1_DRUM_PROFILE", 0)
    return {
        "scanner": _selector_is_configured(timer5) or _selector_is_configured(scanner),
        "vcf": any(_selector_is_configured(value) for value in vcf_values),
        "timer5_ram": timer5,
        "scanner_ram": scanner,
        "vcf_ram": vcf_values,
        "runtime_irq_profile": irq_trace,
        "runtime_chunk_profile": chunk_trace,
        "drum_profile": drum_profile,
        "gpio_shift": {
            name: _macro(candidate, INPUT_PATH, name, 0)
            for name in GPIO_INPUT_SELECTORS
        },
    }


def gpio_trace_contract(candidate: Path, *, experimental: bool = False
                        ) -> tuple[dict[str, Any], list[str]]:
    """Describe the modeled SER/SRCLK/RCLK write sequence and enforce its selector tuple.

    The trace is a register-write contract, not an electrical timing proof.
    The compact experimental sequence deliberately remains outside release
    qualification until target setup/hold and pulse-width evidence is supplied.
    """
    values = {
        name: _macro(candidate, INPUT_PATH, name, 0)
        for name in GPIO_INPUT_SELECTORS
    }
    expected = {name: 1 for name in GPIO_INPUT_SELECTORS} if experimental else {
        name: 0 for name in GPIO_INPUT_SELECTORS
    }
    errors = [
        f"GPIO trace selector {name} must be {value}, got {values[name]!r}"
        for name, value in expected.items() if values[name] != value
    ]
    if experimental:
        expected_mode = "experimental-compact-shadow"
        writes_per_word = 34
        bit_writes = [
            {"write": "SER=data[i], SRCLK=0", "edge": "may lower the prior bit's SRCLK"},
            {"write": "SER=data[i], SRCLK=1", "edge": "SRCLK rising captures data[i]"},
        ]
        latch_writes = [
            {"write": "RCLK=1, SRCLK=1", "edge": "latch rising after bit 15"},
            {"write": "RCLK=0, SRCLK=1", "edge": "latch falling"},
        ]
        setup_hold = {
            "setup": "SER is written with SRCLK low, then one PA OUT write raises SRCLK.",
            "hold": "After each rising edge, the next bit's SER update coincides with SRCLK falling.",
            "pulse_width": "SRCLK high and low phases are each separated by one PA OUT write; no explicit delay is inserted.",
        }
    else:
        expected_mode = "original-three-write"
        writes_per_word = 50
        bit_writes = [
            {"write": "SER=data[i], SRCLK held low", "edge": "data setup"},
            {"write": "SER=data[i], SRCLK=1", "edge": "SRCLK rising captures data[i]"},
            {"write": "SER=data[i], SRCLK=0", "edge": "SRCLK falling"},
        ]
        latch_writes = [
            {"write": "RCLK=1, SRCLK=0", "edge": "latch rising after bit 15"},
            {"write": "RCLK=0, SRCLK=0", "edge": "latch falling"},
        ]
        setup_hold = {
            "setup": "SER is written with SRCLK low, then one PA OUT write raises SRCLK.",
            "hold": "SRCLK remains high until a separate PA OUT write lowers it; SER stays unchanged through that edge.",
            "pulse_width": "SRCLK high and low phases are each separated by one PA OUT write; no explicit delay is inserted.",
        }
    selector_tuple = tuple(values[name] for name in GPIO_INPUT_SELECTORS)
    mode = ({(1, 1, 1): "experimental-compact-shadow",
             (0, 0, 0): "original-three-write"}.get(selector_tuple, "selector-mismatch"))
    report = {
        "schema": GPIO_TRACE_SCHEMA,
        "mode": mode,
        "expected_mode": expected_mode,
        "selectors": values,
        "selector_tuple_matches": not errors,
        "pin_mapping": {
            "SER": "PA4",
            "SRCLK": "PA3",
            "RCLK": "PA1",
        },
        "word_order": "MSB first: data[i] = (word >> (15-i)) & 1 for i=0..15",
        "per_bit_write_sequence": bit_writes,
        "latch_write_sequence": latch_writes,
        "rising_edges_per_word": 16,
        "latch_pulses_per_word": 1,
        "gpio_out_writes_per_word": writes_per_word,
        "input_sample_schedule": {
            "polled": "fm1_input_scan waits FM1_SETTLE_US (10 us by default) after the latch before sampling rows",
            "timer5": "the newly latched column is sampled on the next scanner tick before the next shift",
        },
        "setup_hold_and_timing": setup_hold,
        "electrical_timing_verified": False,
        "timing_scope": "Register-write order only. GPIO bus completion time, pin skew, serial-register setup/hold, and SRCLK/RCLK pulse widths require measurement on the target board.",
        "host_trace_oracle": "tests/run_fm1_input_compact_shift_test.sh exhaustively exercises both original and compact 16-bit word paths",
        "experimental_requires_hardware_control_check": experimental,
    }
    return report, errors


def parse_symbols(text: str) -> dict[str, tuple[int, str, bool]]:
    """Parse objdump -t lines into name -> (address, section, is_function)."""
    symbols: dict[str, tuple[int, str, bool]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 4 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            section = fields[-3] if len(fields) >= 3 else ""
            is_function = "F" in fields[1:-2]
            try:
                symbols[fields[-1]] = (int(fields[0], 16), section, is_function)
            except ValueError:
                continue
    return symbols


def _ram_bounds(symbols: dict[str, tuple[int, str, bool]], errors: list[str]) -> tuple[int, int] | None:
    start = symbols.get("_ra_start")
    end = symbols.get("_ra_end")
    if not start:
        errors.append("missing required RAM-audio start symbol: _ra_start")
    if not end:
        errors.append("missing required RAM-audio end symbol: _ra_end")
    if not start or not end:
        return None
    lo, hi = start[0], end[0]
    if start[1] != ".ram_audio" or end[1] != ".ram_audio" or hi <= lo:
        errors.append("malformed RAM-audio bounds (_ra_start/_ra_end)")
        return None
    return lo, hi


def _read_linked(candidate: Path, errors: list[str]) -> tuple[dict[str, tuple[int, str, bool]], str,
                                                           dict[str, str]]:
    texts: dict[str, str] = {}
    hashes: dict[str, str] = {}
    for relative in ARTIFACT_PATHS:
        path = candidate / relative
        try:
            raw = path.read_bytes()
        except OSError as exc:
            errors.append(f"missing or unreadable {relative}: {exc}")
            texts[relative] = ""
            continue
        hashes[relative] = hashlib.sha256(raw).hexdigest()
        if relative.endswith(".symbols") or relative.endswith(".dis"):
            texts[relative] = raw.decode("utf-8", errors="replace")
        else:
            texts[relative] = ""
    symbols = parse_symbols(texts.get("build/dinghy.symbols", ""))
    return symbols, texts.get("build/dinghy.dis", ""), hashes


def _belongs(name: str, roots: tuple[str, ...]) -> bool:
    return any(name == root or name.startswith(root + ".") for root in roots)


def _disassembly_bodies(disassembly: str) -> dict[str, bool]:
    """Return labels and whether each has at least one decoded instruction."""
    bodies: dict[str, bool] = {}
    current: str | None = None
    instruction = re.compile(
        r"^\s*[0-9a-fA-F]+:\s+(?:(?:[0-9a-fA-F]{2})\s+)+\S")
    for line in disassembly.splitlines():
        label = re.fullmatch(r"([A-Za-z_.$][\w.$]*):", line.strip())
        if label:
            current = label[1]
            bodies.setdefault(current, False)
        elif current is not None and instruction.match(line):
            bodies[current] = True
    return bodies


def audit_scanner(candidate: Path, require_original_gpio: bool = True,
                  experimental_gpio_shift: bool = False) -> dict[str, Any]:
    """Check scanner roots and their decoded direct-call closure in RAM audio."""
    candidate = Path(candidate).resolve()
    errors: list[str] = []
    symbols, disassembly, hashes = _read_linked(candidate, errors)
    bounds = _ram_bounds(symbols, errors)
    lo, hi = bounds if bounds else (0, 0)
    gpio_trace, gpio_errors = gpio_trace_contract(
        candidate, experimental=experimental_gpio_shift)
    errors.extend(gpio_errors)

    for name in SCANNER_REQUIRED_ROOTS:
        symbol = symbols.get(name)
        if not symbol or not symbol[2]:
            errors.append(f"missing required scanner root: {name}")

    scanner_functions = {name for name, symbol in symbols.items()
                         if symbol[2] and _belongs(name, SCANNER_RAM_SYMBOL_ALLOWLIST)}
    for name in sorted(scanner_functions):
        address, section, _ = symbols[name]
        if not bounds or section != ".ram_audio" or not lo <= address < hi:
            errors.append("scanner root is outside copied RAM audio: " + name)
    for name in SCANNER_INLINE_LEAVES:
        symbol = symbols.get(name)
        if symbol and symbol[2]:
            errors.append("small scanner leaf remains out of line: " + name)

    edges: list[dict[str, Any]] = []
    bodies = _disassembly_bodies(disassembly)
    current: str | None = None
    for line in disassembly.splitlines():
        label = re.fullmatch(r"([A-Za-z_.$][\w.$]*):", line.strip())
        if label:
            current = label[1]
        if current not in scanner_functions or _belongs(current, ("fm1_timer5_irq",)):
            # Timer5 intentionally retains fixed USB/journal callouts.
            continue
        if re.search(r"\b(?:call[a-z0-9_]*|goto)\s+r\d+\b", line):
            errors.append("indirect call in scanner helper: " + line.strip())
        target = re.search(
            r"\b(?:call[a-z0-9_]*|goto)\b[^<]*<([^:>]+):\s*([0-9a-fA-F]+)\s*>",
            line)
        if target:
            address = int(target[2], 16)
            edges.append({"from": current, "to": target[1].strip(), "address": address})
            if not bounds or not lo <= address < hi:
                errors.append("scanner helper reaches outside copied RAM: " + line.strip())
    for name in SCANNER_REQUIRED_ROOTS:
        if name not in bodies:
            errors.append("required scanner disassembly body is missing: " + name)
        elif not bodies[name]:
            errors.append("required scanner disassembly body has no instructions: " + name)

    for name, expected in (("FM1_TIMER5_RAM", 1), ("FM1_INPUT_SCANNER_RAM", 1)):
        if _macro(candidate, POLICY_PATH, name) != expected:
            errors.append(f"unexpected selector: {name} must be {expected}")
    if require_original_gpio and not experimental_gpio_shift:
        for name in GPIO_INPUT_SELECTORS:
            if _macro(candidate, INPUT_PATH, name, 0) != 0:
                errors.append(f"qualified scanner path requires {name}=0")

    return {
        "schema": "fm1.scanner-linked-closure/v2",
        "candidate": str(candidate),
        "artifact_sha256": hashes,
        "ram_audio_start": lo if bounds else None,
        "ram_audio_end": hi if bounds else None,
        "required_roots": list(SCANNER_REQUIRED_ROOTS),
        "scanner_functions": {name: symbols[name][0] for name in sorted(scanner_functions)},
        "direct_edges": edges,
        "inline_leaf_allowlist": list(SCANNER_INLINE_LEAVES),
        "original_gpio_required": require_original_gpio and not experimental_gpio_shift,
        "experimental_gpio_shift": experimental_gpio_shift,
        "gpio_trace_contract": gpio_trace,
        "scope": "decoded scanner-helper edges only; Timer5 USB/journal callouts may remain XIP; not flash-off-safe",
        "errors": errors,
        "passed": not errors,
    }


def audit_vcf(candidate: Path) -> dict[str, Any]:
    """Check exact VCF render symbols and decoded direct edges in RAM audio."""
    candidate = Path(candidate).resolve()
    errors: list[str] = []
    symbols, disassembly, hashes = _read_linked(candidate, errors)
    bounds = _ram_bounds(symbols, errors)
    lo, hi = bounds if bounds else (0, 0)
    functions: dict[str, Any] = {}
    for name in VCF_RAM_SYMBOL_ALLOWLIST:
        symbol = symbols.get(name)
        if not symbol or not symbol[2]:
            errors.append("missing required VCF RAM function: " + name)
            functions[name] = None
            continue
        address, section, _ = symbol
        functions[name] = {"address": address, "section": section}
        if not bounds or section != ".ram_audio" or not lo <= address < hi:
            errors.append("intended function is not in copied RAM: " + name)

    edges: list[dict[str, Any]] = []
    unresolved: list[str] = []
    bodies = _disassembly_bodies(disassembly)
    current = ""
    for line in disassembly.splitlines():
        label = re.fullmatch(r"([A-Za-z_.$][\w.$]*):", line.strip())
        if label:
            current = label[1]
        if current not in VCF_RAM_SYMBOL_ALLOWLIST:
            continue
        if re.search(r"\b(?:call[a-z0-9_]*|goto)\s+r\d+\b", line):
            unresolved.append(line.strip())
        target = re.search(
            r"\b(?:call[a-z0-9_]*|goto)\b[^<]*<([^:>]+):\s*([0-9a-fA-F]+)\s*>",
            line)
        if target:
            address = int(target[2], 16)
            edges.append({"from": current, "to": target[1].strip(), "address": address})
            if not bounds or not lo <= address < hi:
                errors.append("VCF direct edge escapes RAM group: " + line.strip())

    for name in VCF_RAM_SYMBOL_ALLOWLIST:
        if name not in bodies:
            errors.append("required VCF disassembly body is missing: " + name)
        elif not bodies[name]:
            errors.append("required VCF disassembly body has no instructions: " + name)

    selector_values = []
    for relative in VCF_SELECTOR_PATHS:
        value = _macro(candidate, relative, "FM1_PLAITS_VCF_RENDER_RAM")
        selector_values.append(value)
        if value != 1:
            errors.append("VCF placement selector must be 1 in " + relative)

    return {
        "schema": "fm1.vcf-ram-placement-audit/v2",
        "candidate": str(candidate),
        "artifact_sha256": hashes,
        "ram_audio_start": lo if bounds else None,
        "ram_audio_end": hi if bounds else None,
        "functions": functions,
        "direct_edges": edges,
        "unresolved_decoded_register_edges": unresolved,
        "selector_values": selector_values,
        "scope": "linked placement and decoded direct edges only; unresolved register-indirect edges are reported but not resolved by this checker; not flash-off, sound, or timing proof",
        "errors": errors,
        "passed": not errors,
    }


def audit_candidate(candidate: Path, *, scanner: bool | None = None,
                    vcf: bool | None = None, require_original_gpio: bool = True,
                    runtime_profile: bool = False,
                    experimental_gpio_shift: bool = False) -> dict[str, Any]:
    """Run selected linked checks and classify qualification vs profiling.

    By default selector values in the candidate sources choose which checks
    run. Explicit scanner/vcf booleans are useful for negative offline audits.
    """
    candidate = Path(candidate).resolve()
    selection = configured_selectors(candidate)
    scanner_selected = selection["scanner"] if scanner is None else scanner
    vcf_selected = selection["vcf"] if vcf is None else vcf
    errors: list[str] = []
    gpio_trace, gpio_errors = gpio_trace_contract(
        candidate, experimental=experimental_gpio_shift)
    if not experimental_gpio_shift and not require_original_gpio:
        errors.append("the original GPIO guard can only be relaxed with --experimental-gpio-shift")
    if experimental_gpio_shift and not scanner_selected:
        errors.append("--experimental-gpio-shift requires the scanner RAM audit to be selected")
        errors.extend(gpio_errors)
    trace_values = {
        "FM1_RUNTIME_IRQ_PROFILE": selection["runtime_irq_profile"],
        "FM1_RUNTIME_CHUNK_PROFILE": selection["runtime_chunk_profile"],
        "FM1_DRUM_PROFILE": selection["drum_profile"],
    }
    for name, value in trace_values.items():
        if value not in (0, 1):
            errors.append(f"invalid runtime trace selector {name}: expected 0 or 1, got {value!r}")
    traces_enabled = any(value == 1 for value in trace_values.values())
    if not scanner_selected and not vcf_selected and not experimental_gpio_shift:
        return {
            "schema": "fm1.runtime-ram-build-audit/v1",
            "candidate": str(candidate),
            "selected": {"scanner": False, "vcf": False},
            "classification": "not-selected",
            "qualification_eligible": False,
            "trace_selectors_enabled": traces_enabled,
            "gpio_trace_contract": gpio_trace,
            "scanner": None,
            "vcf": None,
            "errors": errors,
            "passed": not errors,
        }

    if traces_enabled and not runtime_profile:
        errors.append("runtime trace selectors must be off for qualification; use --runtime-profile only for diagnostic builds")
    if runtime_profile and not traces_enabled:
        errors.append("--runtime-profile requires FM1_RUNTIME_IRQ_PROFILE=1 or FM1_RUNTIME_CHUNK_PROFILE=1")
    if experimental_gpio_shift:
        classification = ("experimental-gpio-shift-diagnostic-profile-only"
                          if runtime_profile and traces_enabled
                          else "experimental-gpio-shift")
    else:
        classification = "diagnostic-profile-only" if runtime_profile and traces_enabled else "qualification"

    # Scanner/VCF placement builds retain the original GPIO path by default,
    # even if the scanner itself is not selected for linked-closure checking.
    if require_original_gpio and not experimental_gpio_shift and not scanner_selected:
        for name in GPIO_INPUT_SELECTORS:
            if _macro(candidate, INPUT_PATH, name, 0) != 0:
                errors.append(f"qualified RAM placement requires {name}=0")

    scanner_report = audit_scanner(candidate, require_original_gpio,
                                   experimental_gpio_shift) if scanner_selected else None
    vcf_report = audit_vcf(candidate) if vcf_selected else None
    if scanner_report:
        errors.extend(scanner_report["errors"])
    if vcf_report:
        errors.extend(vcf_report["errors"])
    return {
        "schema": "fm1.runtime-ram-build-audit/v1",
        "candidate": str(candidate),
        "selected": {"scanner": bool(scanner_selected), "vcf": bool(vcf_selected)},
        "selectors": selection,
        "classification": classification,
        "qualification_eligible": classification == "qualification" and not errors,
        "trace_selectors_enabled": traces_enabled,
        "gpio_trace_contract": gpio_trace,
        "scanner": scanner_report,
        "vcf": vcf_report,
        "errors": errors,
        "passed": not errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path,
                        help="linked source/build directory containing build/dinghy.{bin,dis,symbols}")
    parser.add_argument("--scanner", action="store_true", default=None,
                        help="audit scanner RAM closure even if its source selector is off")
    parser.add_argument("--vcf", action="store_true", default=None,
                        help="audit VCF RAM placement even if its source selector is off")
    parser.add_argument("--experimental-gpio-shift", action="store_true",
                        help="audit the exact FAST_SHIFT=1, FAST_SR_SHADOW=1, COMPACT_SHIFT=1 experimental sequence; remains ineligible for release qualification")
    parser.add_argument("--require-original-gpio", action="store_true", default=True,
                        help="require the original GPIO shift path (the default; retained for compatibility)")
    parser.add_argument("--runtime-profile", action="store_true",
                        help="classify explicitly instrumented output as diagnostic-only")
    parser.add_argument("--output", type=Path,
                        help="report path (default: candidate/build/runtime-ram-audit.json)")
    args = parser.parse_args(argv)
    report = audit_candidate(args.candidate, scanner=args.scanner, vcf=args.vcf,
                             runtime_profile=args.runtime_profile,
                             experimental_gpio_shift=args.experimental_gpio_shift)
    output = args.output or (args.candidate / "build" / "runtime-ram-audit.json")
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
