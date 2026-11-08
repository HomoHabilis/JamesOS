#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently inspect an FM-1 package, its flash image and its RAM loader.

Uses Python's CRC implementation and a bounds-checked LZ4 decoder, rather than
round-tripping the packer's own parsing code. Never opens a MIDI port.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import re
import struct
from pathlib import Path

APP_SLOT = 0x8DFBC
FLASH_SIZE = 0x93000
SDK_HASHES = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}


def require(test, message):
    if not test:
        raise ValueError(message)


def crc(data):
    return binascii.crc_hqx(data, 0)


def decrypt(data, key=0xFFFF):
    result = bytearray()
    for value in data:
        result.append(value ^ (key & 255))
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(result)


def cut(data, offset, size, label):
    require(offset >= 0 and size >= 0 and offset + size <= len(data), f"{label}: outside image")
    return data[offset:offset + size]


def jlfs(data, offset, label):
    raw = cut(data, offset, 32, label)
    require(crc(raw[2:]) == struct.unpack_from("<H", raw)[0], f"{label}: header CRC")
    _, dcrc, position, size, flags, reserved, index, name = struct.unpack("<HHIIBBH16s", raw)
    return dict(crc=dcrc, offset=position, size=size, flags=flags, reserved=reserved,
                index=index, name=name.split(b"\0")[0].decode("ascii"))


def file_data(data, entry, label):
    result = cut(data, entry["offset"], entry["size"], label)
    require(crc(result) == entry["crc"], f"{label}: content CRC")
    return result


def lz4_block(raw, expected):
    out = bytearray()
    pos = 0
    while pos < len(raw):
        token = raw[pos]
        pos += 1
        literal = token >> 4
        if literal == 15:
            while True:
                require(pos < len(raw), "LZ4: truncated literal length")
                count = raw[pos]; pos += 1; literal += count
                if count != 255: break
        out += cut(raw, pos, literal, "LZ4 literals")
        pos += literal
        require(len(out) <= expected, "LZ4: expanded past output block")
        if pos == len(raw): break
        require(pos + 2 <= len(raw), "LZ4: truncated offset")
        distance = raw[pos] | raw[pos + 1] << 8
        pos += 2
        require(0 < distance <= len(out), "LZ4: invalid match offset")
        match = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                require(pos < len(raw), "LZ4: truncated match length")
                count = raw[pos]; pos += 1; match += count
                if count != 255: break
        require(len(out) + match <= expected, "LZ4: match expands past output block")
        for _ in range(match): out.append(out[-distance])
    require(pos == len(raw) and len(out) == expected, "LZ4: wrong output length")
    return bytes(out)


def loader_identity(image):
    """Read the updater's own identity, independent of the app's identity.

    Read the exact identity from the packaged updater. Its version may differ
    from both the app identity and previously installed updater versions.
    """
    identities = set(re.findall(rb"ota-FM-1_[0-9]+\x00", image))
    require(len(identities) == 1, "missing or ambiguous RAM update-loader identity")
    identity = identities.pop()[:-1].decode("ascii")
    require(len(identity) <= 27, "RAM update-loader identity exceeds handshake field")
    return identity


def validate(raw, app=None, loader=None, expected_identity=None):
    require(len(raw) >= 1044, "package is too short")
    chars, ended = [], False
    logical = bytearray()
    for index in range(20):
        block = raw[index * 48:(index + 1) * 48]
        logical += block[:47]
        if block[47] == 0x7D: ended = True
        else:
            require(not ended, "identity has a gap")
            chars.append(chr((block[47] - index - 1) & 255))
    identity = "".join(chars)
    require(re.fullmatch(r"FM-1_[0-9]+", identity), "not an FM-1 numeric identity")
    if expected_identity: require(identity == expected_identity, "unexpected package identity")
    logical += raw[960:]
    header = decrypt(logical[:64])
    require(crc(header[2:]) == struct.unpack_from("<H", header)[0], "package header CRC")
    total, count = struct.unpack_from("<IH", header, 4)
    require(total == len(logical), "package length differs from header")
    require(count == 2 and header[16:22] == b"AC791N", "wrong chip or file count")
    directory = cut(logical, 64, count * 80, "package directory")
    require(crc(directory) == struct.unpack_from("<H", header, 2)[0], "package directory CRC")
    files, intervals = {}, []
    for i in range(count):
        entry = decrypt(directory[i * 80:(i + 1) * 80])
        kind, index, checksum, reserved, offset, length, aligned = struct.unpack_from("<HHHHIII", entry)
        name = entry[64:80].split(b"\0")[0].decode("ascii")
        require((kind, index, reserved, name) == ((0, 0, 0, "flash.bin") if i == 0 else
                                                (100, 1, 0, "ota.bin")), "unexpected package file")
        require(offset >= 1024 and offset % 256 == 0 and length > 0, f"{name}: invalid extent")
        require(aligned == (length + 31) & ~31, f"{name}: invalid alignment length")
        payload = cut(logical, offset, length, name)
        require(crc(payload) == checksum, f"{name}: payload CRC")
        for start, stop in intervals:
            require(offset >= stop or offset + length <= start, "overlapping package files")
        intervals.append((offset, offset + length)); files[name] = payload
    flash, ota = files["flash.bin"], files["ota.bin"]
    require(len(flash) == FLASH_SIZE, "flash image has wrong length")
    plain_header = b"".join(decrypt(flash[i:i + 32]) for i in range(0, 160, 32))
    require(crc(plain_header[2:32]) == struct.unpack_from("<H", plain_header)[0], "flash header CRC")
    top = [jlfs(plain_header, i, "flash directory") for i in range(32, 160, 32)]
    require([e["name"] for e in top] == ["uboot.boot", "isd_config.ini", "app_dir_head", "key_mac"],
            "wrong flash directory")
    spl = file_data(flash, top[0], "SDK bootloader")
    isd = file_data(flash, top[1], "chip key blob")
    require(len(isd) >= 34 and crc(isd[:32]) == struct.unpack_from("<H", isd, 32)[0], "chip key CRC")
    threshold = sum(isd[:16]) & 255
    threshold = 0xAA if threshold >= 0xE0 else 0x55 if threshold <= 0x10 else threshold
    key = sum(1 << i for i in range(16) if (isd[16 + i] ^ isd[15 - i]) < threshold)
    require(key == 0x980F, "unexpected FM-1 chip key")
    area = bytearray()
    for i in range(0x4000, len(flash), 32):
        area += decrypt(flash[i:i + 32], key ^ ((i - 0x4000) >> 2))
    area_header = jlfs(area, 0, "application area")
    require(area_header["name"] == "app_area_head" and area_header["offset"] == 0x02000120,
            "wrong XIP application address")
    end = area_header["size"]
    require(end >= 0x120 and end <= len(area), "application area extent")
    require(crc(area[32:end]) == area_header["crc"], "application area body CRC")
    entries = [jlfs(area, 32 + i * 32, "application directory") for i in range(6)]
    require([e["name"] for e in entries] == ["app.bin", "cfg_tool.bin", "VM", "PRCT", "BTIF", "USR"],
            "wrong application directory")
    application = file_data(area, entries[0], "application")
    require(entries[0]["offset"] == 0x120 and len(application) == APP_SLOT, "application slot extent")
    require(application[:4] == bytes.fromhex("04818000"), "wrong FM-1 entry stub")
    if app is not None:
        require(len(app) <= APP_SLOT and application[:len(app)] == app, "packaged application differs from ELF image")
        require(application[len(app):] == b"\xff" * (APP_SLOT - len(app)), "non-padding data after application")
    cfg_tool = file_data(area, entries[1], "SDK config tool")
    cfg_header = jlfs(area, end, "SDK config directory")
    require(cfg_header["name"] == "cfg" and cfg_header["offset"] == 32, "wrong SDK config directory")
    cfg_body = cut(area, end + 32, cfg_header["size"] - 32, "SDK config body")
    require(crc(cfg_body) == cfg_header["crc"], "SDK config directory body CRC")
    eq_entry = jlfs(area, end + 32, "EQ configuration")
    require(eq_entry["name"] == "eq_cfg_hw.bin" and eq_entry["offset"] == 64, "wrong EQ configuration")
    eq = cut(area, end + eq_entry["offset"], eq_entry["size"], "EQ configuration bytes")
    require(crc(eq) == eq_entry["crc"], "EQ configuration content CRC")
    for name, payload in (("uboot.boot", spl), ("cfg_tool.bin", cfg_tool), ("cfg/eq_cfg_hw.bin", eq)):
        require(hashlib.sha256(payload).hexdigest() == SDK_HASHES[name], f"SDK {name}: unrecognized bytes")
    outer, inner = jlfs(ota, 0, "OTA outer header"), jlfs(ota, 32, "OTA inner header")
    require(outer["name"] == inner["name"] == "usb_hid_ota.bin", "wrong OTA program name")
    require(outer["offset"] == 32 and outer["size"] == len(ota) - 32, "OTA body extent")
    require(outer["flags"] == 0x41 and outer["size"] <= 0x4E00, "OTA loader buffer/type limit")
    require(crc(ota[32:]) == outer["crc"], "OTA body CRC")
    # The inner loader header reuses JLFS fields as (program length, RAM address).
    require(inner["size"] == 0x01C0A800 and 0 < inner["offset"] <= 0x14000, "OTA load address/length")
    expanded, pos = bytearray(), 64
    while pos < len(ota):
        packed, unpacked = struct.unpack("<II", cut(ota, pos, 8, "OTA LZ4 block header")); pos += 8
        require(0 < packed <= 4096 and 0 < unpacked <= 4096, "OTA LZ4 block extent")
        expanded += lz4_block(cut(ota, pos, packed, "OTA LZ4 block"), unpacked); pos += packed
    require(len(expanded) == inner["offset"] and crc(expanded) == inner["crc"], "expanded OTA image CRC/length")
    require(b"FELUCCA-LOADER-1" in expanded, "unknown RAM update-loader protocol")
    if loader is not None: require(bytes(expanded) == loader, "packaged loader differs from built RAM program")
    return {"identity": identity, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "loader_identity": loader_identity(expanded),
            "app_slot": APP_SLOT, "app_bytes": len(app) if app is not None else None,
            "ota_bytes": len(expanded), "chip": "AC791N", "format_valid": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--loader", type=Path)
    parser.add_argument("--identity")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = validate(args.package.read_bytes(), args.app.read_bytes() if args.app else None,
                          args.loader.read_bytes() if args.loader else None, args.identity)
    except (ValueError, OSError, struct.error, UnicodeError) as exc:
        parser.exit(1, f"Package validation failed: {exc}\n")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
