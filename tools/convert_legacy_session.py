#!/usr/bin/env python3
"""Explicit, offline v1-v9 -> v10 FM-1 session conversion.

Input is the raw session payload exported by diagnostic command 0x68 (or a
12-byte settings header followed by that payload). The input is never changed.
Conversion is deliberately opt-in: the caller selects the kit track, all eight
legacy-lane destinations, the four new voice choices, and acknowledges that
legacy roles/voices may not have an exact four-lane equivalent. A JSON report
records byte hashes, choices, dropped/collapsed events, and normalized controls.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

SESSION_MAGIC = 0x42524431
SETTINGS_MAGIC = 0x59474E44
V10_SIZE = 968
V9_SIZE = 964
PARTS = 4
PART_SIZE = 58
MODULE_SIZE = 165
PART_BASE = 8
MODULE_BASE = PART_BASE + PARTS * PART_SIZE
LANE_GATE_BASE = 920
COMPRESSOR_BASE = 952
HOME_GATE_BASE = 960
LEGACY_SIZES = {1: 240, 2: 240, 3: 900, 4: 908, 5: 916,
                6: 920, 7: 952, 8: 960, 9: 964}
KIT_PRESETS = {48, 49, 50, 76}
PLAIT_HARMONICS = (48, 12, 12, 12, 32, 48, 64, 32, 64, 48, 64, 64,
                   32, 48, 64, 32, 64, 64, 48, 64, 64, 64, 64, 64)


class ConversionRefused(ValueError):
    def __init__(self, message: str, report: dict | None = None):
        super().__init__(message)
        self.report = report or {"ready": False, "errors": [message]}


def _u8(data: bytes | bytearray, offset: int) -> int:
    return data[offset]


def _i8(value: int) -> int:
    return value - 256 if value >= 128 else value


def _put_i8(buf: bytearray, offset: int, value: int) -> None:
    buf[offset] = value & 0xFF


def _clip(value: int, low: int, high: int) -> int:
    return min(high, max(low, value))


def _part_offset(part: int) -> int:
    return PART_BASE + part * PART_SIZE


def _module_offset(part: int) -> int:
    return MODULE_BASE + part * MODULE_SIZE


def _is_kit(preset: int) -> bool:
    return preset in KIT_PRESETS


def _default_module(snap: int = 64) -> bytearray:
    module = bytearray(MODULE_SIZE)
    module[0] = snap
    module[1:5] = bytes((0, 64, 0, 24))
    module[6] = 4  # BR_TARGET_FILTER
    module[8] = 1  # retrigger
    for lfo in range(3):
        at = 13 + lfo * 4
        module[at:at + 4] = bytes((0, 32 + 16 * lfo, 0, 0))
    module[25:37] = bytes((0, 64, 127, 0, 64, 57, 0, 64, 0, 40, 48, 0))
    return module


def _default_part(preset: int = 0) -> bytearray:
    part = bytearray(PART_SIZE)
    # Match a fresh app's saved melodic track: CSAW, 16 steps, open gate.
    part[0:18] = bytes((preset, 74, 62, 0, 127, 2, 30, 112, 24, 0,
                        0, 0, 0, 128, 16, 1, 0, 80))
    return part


def _decode_source(input_bytes: bytes) -> tuple[bytes, dict]:
    info = {"input_sha256": hashlib.sha256(input_bytes).hexdigest(),
            "input_bytes": len(input_bytes)}
    if len(input_bytes) in set(LEGACY_SIZES.values()):
        payload = input_bytes
        info["input_container"] = "raw-session-payload"
    elif len(input_bytes) >= 12:
        magic, length, expected_crc = struct.unpack_from("<III", input_bytes)
        if magic != SETTINGS_MAGIC or length not in set(LEGACY_SIZES.values()):
            raise ConversionRefused("input is neither a supported raw v1-v9 payload nor a settings record")
        if len(input_bytes) not in (12 + length, 4096):
            raise ConversionRefused("settings record must be header+payload or one 4096-byte settings sector")
        payload = input_bytes[12:12 + length]
        actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ConversionRefused("settings-record CRC32 does not match; original preserved, no v10 written",
                                     {**info, "ready": False, "errors": ["settings-record CRC32 mismatch"],
                                      "settings_header_length": length,
                                      "settings_header_crc32": f"{expected_crc:08x}",
                                      "payload_crc32": f"{actual_crc:08x}"})
        if len(input_bytes) == 4096 and any(b != 0xFF for b in input_bytes[12 + length:]):
            raise ConversionRefused("settings sector has non-erased bytes outside the record; refusing to discard them")
        info.update(input_container="settings-record", settings_header_length=length,
                    settings_header_crc32=f"{expected_crc:08x}",
                    settings_sector_padding="erased" if len(input_bytes) == 4096 else None)
    else:
        raise ConversionRefused("unsupported input length")
    if len(payload) not in set(LEGACY_SIZES.values()):
        raise ConversionRefused("payload length is not a known v1-v9 session size")
    magic, bpm, selected, octave, version = struct.unpack_from("<IBBbB", payload)
    if magic != SESSION_MAGIC or version not in LEGACY_SIZES or len(payload) != LEGACY_SIZES[version]:
        raise ConversionRefused("session magic, version, or exact legacy length is invalid",
                                 {**info, "ready": False, "errors": ["session magic/version/length mismatch"]})
    info.update(payload_bytes=len(payload), payload_sha256=hashlib.sha256(payload).hexdigest(),
                payload_crc32=f"{binascii.crc32(payload) & 0xFFFFFFFF:08x}",
                legacy_version=version)
    return payload, info


def _parse_lane_map(value: str | list[int | None]) -> list[int | None]:
    if isinstance(value, list):
        result = value
    else:
        tokens = value.split(",")
        if len(tokens) != 8:
            raise ConversionRefused("--lane-map needs exactly eight comma-separated entries (0..3 or x)")
        try:
            result = [None if token.strip().lower() in ("x", "drop", "-")
                      else int(token.strip(), 0) for token in tokens]
        except ValueError as error:
            raise ConversionRefused(f"invalid --lane-map: {error}") from error
    if len(result) != 8 or any(dest is not None and not 0 <= dest < 4 for dest in result):
        raise ConversionRefused("lane map must contain eight destinations in 0..3 or x")
    return result


def _parse_control_lanes(value: str | list[int | None] | None) -> list[int | None] | None:
    if value is None or isinstance(value, list):
        result = value
    else:
        tokens = value.split(",")
        if len(tokens) != 4:
            raise ConversionRefused("--control-lanes needs four comma-separated source lane indices (0..7 or x)")
        try:
            result = [None if token.strip().lower() in ("x", "-")
                      else int(token.strip(), 0) for token in tokens]
        except ValueError as error:
            raise ConversionRefused(f"invalid --control-lanes: {error}") from error
    if result is not None and (len(result) != 4 or
                              any(lane is not None and not 0 <= lane < 8 for lane in result)):
        raise ConversionRefused("control lanes must contain four source lane indices in 0..7 or x")
    return result


def _normalize_module(module: bytearray, part: int, report: dict) -> bytearray:
    out = bytearray(module)

    def set_value(index: int, value: int, label: str) -> None:
        before = out[index]
        out[index] = value & 0xFF
        if before != out[index]:
            report["normalized_fields"].append({"field": label, "from": before, "to": out[index]})

    def clip_u(index: int, high: int, label: str) -> None:
        set_value(index, _clip(out[index], 0, high), label)

    clip_u(0, 127, f"track{part}.module.snap")
    for i in range(4): clip_u(1 + i, 127, f"track{part}.module.env[{i}]")
    signed = _clip(_i8(out[5]), -127, 127)
    if signed != _i8(out[5]):
        report["normalized_fields"].append({"field": f"track{part}.module.amount",
                                             "from": _i8(out[5]), "to": signed})
    _put_i8(out, 5, signed)
    clip_u(6, 13, f"track{part}.module.target")
    set_value(7, int(out[7] != 0), f"track{part}.module.loop")
    set_value(8, int(out[8] != 0), f"track{part}.module.retrigger")
    clip_u(9, 7, f"track{part}.module.scale")
    set_value(10, out[10] % 12, f"track{part}.module.root")
    set_value(11, int(out[11] != 0), f"track{part}.module.quantize")
    signed = _clip(_i8(out[12]), -24, 24)
    if signed != _i8(out[12]):
        report["normalized_fields"].append({"field": f"track{part}.module.transpose",
                                             "from": _i8(out[12]), "to": signed})
    _put_i8(out, 12, signed)
    for lfo in range(3):
        at = 13 + lfo * 4
        clip_u(at, 4, f"track{part}.module.lfo[{lfo}].wave")
        clip_u(at + 1, 127, f"track{part}.module.lfo[{lfo}].rate")
        clip_u(at + 2, 127, f"track{part}.module.lfo[{lfo}].depth")
        clip_u(at + 3, 13, f"track{part}.module.lfo[{lfo}].target")
    for index, high, name in (
        (25, 3, "drive_engine"), (26, 127, "drive_tone"), (27, 127, "drive_mix"),
        (28, 3, "delay_engine"), (29, 127, "delay_time"), (30, 127, "delay_feedback"),
        (31, 4, "reverb_engine"), (32, 127, "reverb_tone"),
        (33, 3, "mod_engine"), (34, 127, "mod_rate"), (35, 127, "mod_depth"),
        (36, 127, "mod_mix")):
        clip_u(index, high, f"track{part}.module.{name}")
    for route in range(8):
        at = 37 + route * 4
        clip_u(at, 10, f"track{part}.module.route[{route}].source")
        clip_u(at + 1, 13, f"track{part}.module.route[{route}].target")
        signed = _clip(_i8(out[at + 2]), -127, 127)
        if signed != _i8(out[at + 2]):
            report["normalized_fields"].append({"field": f"track{part}.module.route[{route}].amount",
                                                 "from": _i8(out[at + 2]), "to": signed})
        _put_i8(out, at + 2, signed)
        set_value(at + 3, int(out[at + 3] != 0), f"track{part}.module.route[{route}].enabled")
    return out


def _default_plaits(preset: int) -> tuple[int, int]:
    if 52 <= preset <= 75:
        return PLAIT_HARMONICS[preset - 52], 0
    return 64, 0


def _normalize_part(old: bytes, version: int, source_part: int, report: dict) -> bytearray:
    out = bytearray(old)
    prefix = f"track{source_part}"

    def clip_byte(index: int, high: int, name: str) -> None:
        before = out[index]
        out[index] = _clip(before, 0, high)
        if before != out[index]:
            report["normalized_fields"].append({"field": f"{prefix}.{name}",
                                                 "from": before, "to": out[index]})

    if out[0] > 76:
        raise ConversionRefused(f"{prefix} has unsupported preset id {out[0]}")
    clip_byte(1, 127, "timbre")
    clip_byte(2, 127, "color")
    signed = _clip(_i8(out[3]), -24, 24)
    if signed != _i8(out[3]):
        report["normalized_fields"].append({"field": f"{prefix}.tune", "from": _i8(out[3]), "to": signed})
    _put_i8(out, 3, signed)
    for index, name in enumerate(("level", "attack", "decay", "sustain", "release",
                                  "drive", "delay", "reverb", "space"), start=4):
        clip_byte(index, 127, name)
    if version == 1:
        before = out[13]
        out[13] = 128 | int(before != 0)  # v1 only stored mute; filter defaulted to 64.
        if out[13] != before:
            report["normalized_fields"].append({"field": f"{prefix}.mute/filter(v1)",
                                                 "from": before, "to": out[13]})
    clip_byte(14, 64, "length")
    if out[14] == 0:
        out[14] = 1
        report["normalized_fields"].append({"field": f"{prefix}.length", "from": 0, "to": 1})
    clip_byte(15, 2, "division")
    clip_byte(16, 64, "swing")
    clip_byte(17, 127, "gate")
    if out[17] == 0:
        out[17] = 1
        report["normalized_fields"].append({"field": f"{prefix}.gate", "from": 0, "to": 1})
    for lane in range(8): clip_byte(18 + lane, 127, f"lane_level[{lane}]")
    if not _is_kit(out[0]):
        for step in range(16):
            at = 26 + 2 * step
            if out[at] > 128:
                report["normalized_fields"].append({"field": f"{prefix}.step[{step}].note", "from": out[at], "to": 0})
                out[at] = 0
    return out


def _parse_compressor_override(value: str | None) -> int | None:
    if value is None: return None
    mapping = {"off": 0, "all": 1, "t1": 2, "t2": 3, "t3": 4,
               "drum1": 5, "drum2": 6, "drum3": 7, "drum4": 8}
    try:
        return mapping[value.lower()]
    except KeyError as error:
        raise ConversionRefused("--compressor-source must be off, all, t1, t2, t3, or drum1..drum4") from error


def convert_payload(payload: bytes, *, kit_track: int, lane_map: str | list[int | None],
                    choices: int, allow_lossy: bool,
                    control_lanes: str | list[int | None] | None = None,
                    compressor_source: str | None = None) -> tuple[bytes, dict]:
    payload, source_info = _decode_source(payload)
    version = source_info["legacy_version"]
    lane_map = _parse_lane_map(lane_map)
    control_lanes = _parse_control_lanes(control_lanes)
    override_comp = _parse_compressor_override(compressor_source)
    if not allow_lossy:
        raise ConversionRefused("conversion changes the legacy eight-lane/track-role model; pass --allow-lossy after reviewing the report",
                                 {**source_info, "ready": False,
                                  "errors": ["explicit --allow-lossy acknowledgement is required"]})
    if not 0 <= kit_track < 4:
        raise ConversionRefused("--kit-track must be 0..3")
    if not 0 <= choices <= 0x0F:
        raise ConversionRefused("--choices must be a four-bit mask (0..15)")

    original_parts = [payload[_part_offset(p):_part_offset(p) + PART_SIZE] for p in range(4)]
    kit_tracks = [p for p, part in enumerate(original_parts) if _is_kit(part[0])]
    synth_tracks = [p for p, part in enumerate(original_parts) if not _is_kit(part[0])]
    if kit_track not in kit_tracks:
        raise ConversionRefused(f"selected source track {kit_track} is not a kit; detected kit tracks={kit_tracks}",
                                 {**source_info, "ready": False, "kit_tracks": kit_tracks,
                                  "errors": [f"selected source track {kit_track} is not a kit"]})
    extra_kits = [p for p in kit_tracks if p != kit_track]
    if extra_kits:
        raise ConversionRefused(f"additional legacy kit tracks {extra_kits} have no four-track role mapping; simplify or explicitly export a one-kit source project first",
                                 {**source_info, "ready": False, "kit_tracks": kit_tracks,
                                  "selected_kit_track": kit_track,
                                  "errors": [f"additional kit tracks require a separate user-authored mapping: {extra_kits}"]})
    if len(synth_tracks) != 3:
        raise ConversionRefused("source project does not contain exactly three melodic tracks and one kit track")
    track_sources = synth_tracks + [kit_track]
    source_to_destination = {source: dest for dest, source in enumerate(track_sources)}

    report = {**source_info, "ready": True, "errors": [], "normalized_fields": [],
              "acknowledged_lossy_conversion": True,
              "track_mapping": {f"source_track_{source}": f"v10_track_{dest}"
                                for source, dest in source_to_destination.items()},
              "kit_track_source": kit_track,
              "lane_map_old0_to_old7": lane_map,
              "kit_choice_mask": choices,
              "compressor": {"legacy": None, "v10": None},
              "dropped_lane_events": [], "collapsed_lane_events": [],
              "lane_groups": [], "normalized_fields": []}

    bpm = _clip(payload[4], 40, 240)
    if bpm != payload[4]: report["normalized_fields"].append({"field": "bpm", "from": payload[4], "to": bpm})
    old_selected = payload[5]
    selected_source = old_selected if old_selected < 4 else 0
    selected = source_to_destination[selected_source]
    if selected_source != old_selected:
        report["normalized_fields"].append({"field": "selected_track", "from": old_selected,
                                             "to_source_track": selected_source,
                                             "to_v10_track": selected})
    octave = _clip(_i8(payload[6]), -2, 2)
    if octave != _i8(payload[6]): report["normalized_fields"].append({"field": "octave", "from": _i8(payload[6]), "to": octave})
    out = bytearray(V10_SIZE)
    struct.pack_into("<IBBbB", out, 0, SESSION_MAGIC, bpm, selected, octave, 10)

    # Expand the old prefix into v3 form. v1/v2 lacked modules and used byte 13
    # as mute only; all newer legacy records already contain the complete v3 prefix.
    if version >= 3:
        old_modules = [payload[_module_offset(p):_module_offset(p) + MODULE_SIZE] for p in range(4)]
    else:
        old_modules = []
        for p, part in enumerate(original_parts):
            old_modules.append(_default_module(_clip(part[5], 0, 127) if _is_kit(part[0]) else 64))

    # Reorder only the four track records and their parallel session fields.
    for dest, source in enumerate(track_sources):
        part = _normalize_part(original_parts[source], version, source, report)
        module = bytearray(old_modules[source])
        if version < 3 and _is_kit(part[0]):
            module[0] = part[5]
            part[5] = 0  # v1/v2 used the saved attack byte as kit snap.
        if version >= 3:
            module = _normalize_module(module, source, report)
        if dest < 3:
            for lane in range(8):
                if part[18 + lane]:
                    report["normalized_fields"].append({"field": f"track{source}.lane_level[{lane}]",
                                                         "from": part[18 + lane], "to": 0,
                                                         "reason": "v10 melodic tracks have no drum-lane level storage"})
                part[18 + lane] = 0
        else:
            groups = [[lane for lane, mapped in enumerate(lane_map) if mapped == new_lane]
                      for new_lane in range(4)]
            report["lane_groups"] = groups
            old_levels = original_parts[source][18:26]
            if version >= 7:
                old_gates = list(payload[920 + source * 8:920 + source * 8 + 8])
            else:
                old_gates = [128] * 8
            normalized_gates = [_clip(gate, 1, 128) for gate in old_gates]
            for lane, gate in enumerate(old_gates):
                if normalized_gates[lane] != gate:
                    report["normalized_fields"].append({"field": f"track{source}.lane_gate[{lane}]",
                                                         "from": gate, "to": normalized_gates[lane]})
            chosen_controls: list[int | None] = []
            for new_lane, group in enumerate(groups):
                requested = control_lanes[new_lane] if control_lanes is not None else None
                if requested is not None and requested not in group:
                    raise ConversionRefused(f"control lane {requested} is not mapped to destination lane {new_lane}")
                if requested is None and len(group) > 1:
                    pairs = {(old_levels[lane], normalized_gates[lane]) for lane in group}
                    if len(pairs) > 1:
                        raise ConversionRefused(f"legacy lane controls conflict for v10 lane {new_lane}: "
                                                f"{[(lane, old_levels[lane], normalized_gates[lane]) for lane in group]}; "
                                                "choose --control-lanes explicitly")
                chosen = requested if requested is not None else (group[0] if group else None)
                chosen_controls.append(chosen)
                if chosen is None:
                    part[18 + new_lane] = 0
                else:
                    part[18 + new_lane] = _clip(old_levels[chosen], 0, 127)
            part[22:26] = bytes(4)
            # The old step bytes are a pair of eight-bit hit/accent masks.
            for step in range(16):
                at = 26 + 2 * step
                old_hits, old_accents = part[at], part[at + 1]
                new_hits = new_accents = 0
                for old_lane, new_lane in enumerate(lane_map):
                    bit = 1 << old_lane
                    if old_hits & bit:
                        if new_lane is None:
                            report["dropped_lane_events"].append({"step": step, "old_lane": old_lane,
                                                                  "accent": bool(old_accents & bit)})
                        else:
                            target_bit = 1 << new_lane
                            if new_hits & target_bit:
                                report["collapsed_lane_events"].append({
                                    "step": step, "v10_lane": new_lane,
                                    "old_lane": old_lane,
                                    "also_mapped_old_lanes": [prior for prior in range(old_lane)
                                                               if lane_map[prior] == new_lane and old_hits & (1 << prior)]})
                            new_hits |= target_bit
                            if old_accents & bit: new_accents |= target_bit
                # Any unmapped bits are explicitly recorded as source events.
                part[at], part[at + 1] = new_hits, new_accents
            # Map the remaining 48 pattern steps stored in module.extra_step.
            for step in range(16, 64):
                at = 69 + 2 * (step - 16)
                old_hits, old_accents = module[at], module[at + 1]
                new_hits = new_accents = 0
                for old_lane, new_lane in enumerate(lane_map):
                    bit = 1 << old_lane
                    if old_hits & bit:
                        if new_lane is None:
                            report["dropped_lane_events"].append({"step": step, "old_lane": old_lane,
                                                                  "accent": bool(old_accents & bit)})
                        else:
                            target_bit = 1 << new_lane
                            if new_hits & target_bit:
                                report["collapsed_lane_events"].append({
                                    "step": step, "v10_lane": new_lane,
                                    "old_lane": old_lane,
                                    "also_mapped_old_lanes": [prior for prior in range(old_lane)
                                                               if lane_map[prior] == new_lane and old_hits & (1 << prior)]})
                            new_hits |= target_bit
                            if old_accents & bit: new_accents |= target_bit
                module[at], module[at + 1] = new_hits, new_accents
            report["selected_control_sources"] = chosen_controls

        # Melodic steps and extra steps use one note code, not a lane mask.
        if dest < 3:
            for step in range(16):
                at = 26 + 2 * step
                if part[at] > 128: part[at] = 0
            for step in range(48):
                at = 69 + 2 * step
                if module[at] > 128: module[at] = 0
        part_at = _part_offset(dest)
        out[part_at:part_at + PART_SIZE] = part
        module_at = _module_offset(dest)
        out[module_at:module_at + MODULE_SIZE] = module

        # Append arrays added by legacy versions, remapping them alongside tracks.
        if version >= 5:
            plaits = payload[908 + source * 2:908 + source * 2 + 2]
            normalized_plaits = bytes(_clip(value, 0, 127) for value in plaits)
            if plaits != normalized_plaits:
                report["normalized_fields"].append({"field": f"track{source}.plaits",
                                                     "from": list(plaits),
                                                     "to": list(normalized_plaits)})
            out[908 + dest * 2:908 + dest * 2 + 2] = normalized_plaits
        else:
            harmonics, aux = _default_plaits(part[0])
            out[908 + dest * 2:908 + dest * 2 + 2] = bytes((harmonics, aux))
        if version >= 6:
            pan = _clip(payload[916 + source], 0, 127)
            if pan != payload[916 + source]:
                report["normalized_fields"].append({"field": f"track{source}.pan",
                                                     "from": payload[916 + source], "to": pan})
        else:
            pan = 64
        out[916 + dest] = pan
        if version >= 7:
            gates = payload[920 + source * 8:920 + source * 8 + 8]
            if dest < 3:
                if any(gate != 128 for gate in gates):
                    report["normalized_fields"].append({"field": f"track{source}.lane_gate",
                                                         "from": list(gates), "to": [128] * 8,
                                                         "reason": "v10 melodic tracks have no drum-lane gate storage"})
                out[920 + dest * 8:920 + dest * 8 + 8] = bytes([128] * 8)
            else:
                chosen_gates = [128 if lane is None else _clip(gates[lane], 1, 128)
                                for lane in chosen_controls]
                out[920 + dest * 8:920 + dest * 8 + 8] = bytes(chosen_gates + [128] * 4)
        else:
            out[920 + dest * 8:920 + dest * 8 + 8] = bytes([128] * 8)
        if version >= 9:
            home_gate = _clip(payload[960 + source], 0, 128)
            if home_gate != payload[960 + source]:
                report["normalized_fields"].append({"field": f"track{source}.home_gate",
                                                     "from": payload[960 + source], "to": home_gate})
        else:
            home_gate = part[6]
        out[960 + dest] = home_gate

    # Added v4-v9 global controls, with firmware-equivalent defaults for absent versions.
    if version >= 4:
        for index, label in enumerate(("filter", "distortion", "saturation", "wow")):
            value = _clip(payload[900 + index], 0, 127)
            if value != payload[900 + index]:
                report["normalized_fields"].append({"field": f"master_fx.{label}",
                                                     "from": payload[900 + index], "to": value})
            out[900 + index] = value
        target = _clip(payload[904], 0, 6)
        if target != payload[904]:
            report["normalized_fields"].append({"field": "select_target",
                                                 "from": payload[904], "to": target})
        out[904] = target
    else:
        out[900:905] = bytes((64, 0, 0, 0, 0))
    if version >= 5:
        reserved = payload[905:908]
        if any(reserved):
            report["normalized_fields"].append({"field": "reserved_v4", "from": list(reserved), "to": [0, 0, 0]})
    out[905:908] = bytes(3)

    if version >= 8:
        comp = bytearray(payload[952:960])
    else:
        comp = bytearray((0, 18, 3, 0, 12, 40, 0, 127))
    legacy_source = _clip(comp[0], 0, 5)
    if comp[0] != legacy_source:
        report["normalized_fields"].append({"field": "compressor.source", "from": comp[0], "to": legacy_source})
    if override_comp is not None:
        comp[0] = override_comp
        report["compressor"]["override"] = compressor_source.lower()
    elif legacy_source >= 2:
        source_track = legacy_source - 2
        mapped = source_to_destination[source_track]
        if mapped == 3:
            raise ConversionRefused(f"legacy compressor source T{source_track + 1} targets the kit as a whole; "
                                    "choose an explicit --compressor-source drum1..drum4 or off/all")
        comp[0] = 2 + mapped
        if comp[0] != legacy_source:
            report["compressor"]["remapped_track_source"] = {"legacy_track": source_track,
                                                              "v10_track": mapped}
    comp[1] = _clip(comp[1], 0, 48)
    comp[2] = _clip(comp[2], 0, 7)
    comp[3] = _clip(comp[3], 0, 24)
    for i in range(4, 8): comp[i] = _clip(comp[i], 0, 127)
    out[952:960] = comp
    report["compressor"]["legacy"] = legacy_source
    report["compressor"]["v10"] = comp[0]

    out[964] = choices
    out[965:968] = bytes(3)
    if any(report["dropped_lane_events"]) or any(report["collapsed_lane_events"]):
        report["loss_summary"] = {
            "dropped_trigger_events": len(report["dropped_lane_events"]),
            "collapsed_same_step_lane_events": len(report["collapsed_lane_events"]),
            "dropped_events": report["dropped_lane_events"],
            "collapsed_events": report["collapsed_lane_events"]}
    else:
        report["loss_summary"] = {"dropped_trigger_events": 0,
                                  "collapsed_same_step_lane_events": 0,
                                  "dropped_events": [], "collapsed_events": []}
    _validate_v10(bytes(out))
    report.update(output_bytes=len(out), output_sha256=hashlib.sha256(out).hexdigest(),
                  output_crc32=f"{binascii.crc32(out) & 0xFFFFFFFF:08x}",
                  output_format="raw-v10-payload-for-0x64/0x65-upload")
    return bytes(out), report


def _validate_v10(data: bytes) -> None:
    if len(data) != V10_SIZE:
        raise ConversionRefused("internal error: output length is not 968")
    magic, bpm, selected, octave, version = struct.unpack_from("<IBBbB", data)
    if magic != SESSION_MAGIC or version != 10 or not 40 <= bpm <= 240 or selected >= 4 or not -2 <= octave <= 2:
        raise ConversionRefused("converted top-level session fields are invalid")
    if data[905:908] != bytes(3) or data[964] & 0xF0 or data[965:968] != bytes(3):
        raise ConversionRefused("converted reserved bytes or kit choice mask are invalid")
    if data[904] >= 7 or any(value > 127 for value in data[900:904]):
        raise ConversionRefused("converted master controls are invalid")
    comp = data[952:960]
    if comp[0] >= 9 or comp[1] > 48 or comp[2] > 7 or comp[3] > 24 or any(value > 127 for value in comp[4:]):
        raise ConversionRefused("converted compressor controls are invalid")
    kit_groups = []
    for part_index in range(4):
        at = _part_offset(part_index)
        part = data[at:at + PART_SIZE]
        preset = part[0]
        kit = _is_kit(preset)
        if kit != (part_index == 3):
            raise ConversionRefused(f"internal role error in v10 track {part_index}")
        if preset > 76 or part[1] > 127 or part[2] > 127 or not -24 <= _i8(part[3]) <= 24 or \
                any(value > 127 for value in part[4:13]) or not 1 <= part[14] <= 64 or \
                part[15] > 2 or part[16] > 64 or not 1 <= part[17] <= 127:
            raise ConversionRefused(f"converted controls are invalid on v10 track {part_index}")
        if part_index < 3 and any(part[18:26]):
            raise ConversionRefused(f"noncanonical lane levels on melodic track {part_index}")
        lane_gates = data[920 + part_index * 8:920 + part_index * 8 + 8]
        if kit:
            if any(value > 127 for value in part[18:22]) or any(not 1 <= value <= 128 for value in lane_gates[:4]) or \
                    any(value != 0 for value in part[22:26]) or any(value != 128 for value in lane_gates[4:]):
                raise ConversionRefused("converted kit lane controls are invalid")
            for step in range(16):
                if part[26 + 2 * step] & 0xF0 or part[27 + 2 * step] & 0xF0:
                    raise ConversionRefused("converted kit pattern contains a bit outside lanes 0..3")
            module = data[_module_offset(part_index):_module_offset(part_index) + MODULE_SIZE]
            if any(module[69 + 2 * step] & 0xF0 or module[70 + 2 * step] & 0xF0 for step in range(48)):
                raise ConversionRefused("converted kit extra pattern contains a bit outside lanes 0..3")
        elif any(value != 128 for value in lane_gates):
            raise ConversionRefused(f"noncanonical lane gates on melodic track {part_index}")
        module = data[_module_offset(part_index):_module_offset(part_index) + MODULE_SIZE]
        if module[0] > 127 or any(value > 127 for value in module[1:5]) or _i8(module[5]) < -127 or \
                module[6] > 13 or module[7] > 1 or module[8] > 1 or module[9] > 7 or module[10] > 11 or \
                module[11] > 1 or not -24 <= _i8(module[12]) <= 24:
            raise ConversionRefused(f"converted module controls are invalid on v10 track {part_index}")
        for lfo in range(3):
            l = module[13 + lfo * 4:17 + lfo * 4]
            if l[0] > 4 or l[1] > 127 or l[2] > 127 or l[3] > 13:
                raise ConversionRefused(f"converted LFO is invalid on v10 track {part_index}")
        for route in range(8):
            r = module[37 + route * 4:41 + route * 4]
            if r[0] > 10 or r[1] > 13 or _i8(r[2]) < -127 or r[3] > 1:
                raise ConversionRefused(f"converted route is invalid on v10 track {part_index}")
        if any(value > 127 for value in data[908 + part_index * 2:910 + part_index * 2]) or \
                data[916 + part_index] > 127 or data[960 + part_index] > 128:
            raise ConversionRefused(f"converted appended controls are invalid on v10 track {part_index}")
        if not kit:
            for step in range(16):
                if part[26 + 2 * step] > 128: raise ConversionRefused("melodic note code out of range")
            if any(module[69 + 2 * step] > 128 for step in range(48)):
                raise ConversionRefused("melodic extra note code out of range")


def _write_exclusive(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "wb") as output:
            written = output.write(data)
            if written != len(data):
                raise OSError("short write while creating conversion artifact")
            output.flush()
            os.fsync(output.fileno())
    except BaseException:
        try:
            path.unlink()
        except OSError:
            pass
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="read-only raw 0x68 payload or exact settings record")
    parser.add_argument("output", type=Path, help="new 968-byte v10 payload; existing files are never overwritten")
    parser.add_argument("--report", type=Path, help="new JSON report path (default: OUTPUT.migration.json)")
    parser.add_argument("--kit-track", type=int, required=True, choices=range(4),
                        help="legacy source track to become permanent v10 kit track 4")
    parser.add_argument("--lane-map", required=True,
                        help="eight old-lane destinations, comma-separated 0..3 or x/drop")
    parser.add_argument("--choices", type=lambda value: int(value, 0), required=True,
                        help="explicit four-bit v10 A/B choice mask, e.g. 0x0 or 0xA")
    parser.add_argument("--control-lanes",
                        help="optional four old-lane indices for v10 level/gate controls; x uses defaults")
    parser.add_argument("--compressor-source",
                        help="explicit replacement if the old detector points at the kit track: off/all/t1/t2/t3/drum1..drum4")
    parser.add_argument("--allow-lossy", action="store_true",
                        help="acknowledge role reorder, voice substitution, lane collapse/drop, and normalized old values")
    args = parser.parse_args(argv)
    report_path = args.report or args.output.with_suffix(args.output.suffix + ".migration.json")
    input_path = args.input.resolve()
    if args.output.resolve() == input_path or report_path.resolve() == input_path or args.output.resolve() == report_path.resolve():
        parser.error("input, output, and report must be distinct paths")
    try:
        source = args.input.read_bytes()
        result, report = convert_payload(source, kit_track=args.kit_track,
                                         lane_map=args.lane_map, choices=args.choices,
                                         allow_lossy=args.allow_lossy,
                                         control_lanes=args.control_lanes,
                                         compressor_source=args.compressor_source)
        report.update(input_path=str(args.input), output_path=str(args.output),
                      report_path=str(report_path))
        # Exclusive creation protects both the untouched source and prior outputs.
        _write_exclusive(args.output, result)
        try:
            _write_exclusive(report_path, (json.dumps(report, indent=2, sort_keys=True) + "\n").encode())
        except Exception:
            args.output.unlink(missing_ok=True)
            raise
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except ConversionRefused as error:
        report = error.report
        report.update(input_path=str(args.input), output_path=str(args.output),
                      report_path=str(report_path), output_written=False)
        try:
            _write_exclusive(report_path, (json.dumps(report, indent=2, sort_keys=True) + "\n").encode())
        except FileExistsError:
            pass
        print(f"conversion refused: {error}", file=sys.stderr)
        return 2
    except FileExistsError as error:
        print(f"refusing to overwrite existing output/report: {error.filename}", file=sys.stderr)
        return 2
    except OSError as error:
        print(f"conversion I/O failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
