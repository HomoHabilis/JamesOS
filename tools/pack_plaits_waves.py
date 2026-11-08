#!/usr/bin/env python3
"""Generate or verify the lossless packed Plaits integrated wavetable."""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/plaits/upstream/plaits/resources.cc"
OUTPUT = ROOT / "firmware/plaits/upstream/plaits/wav_integrated_waves_packed.inc"
SAMPLES = 25344
CHUNK = 4096
MAX_DELTA_BYTES = 8192


def read_samples() -> list[int]:
    source = SOURCE.read_text()
    match = re.search(
        r"const int16_t wav_integrated_waves\[\] FM1_FAST_CONST = \{(.*?)\};",
        source,
        re.S,
    )
    if not match:
        raise SystemExit(f"cannot find canonical wav_integrated_waves initializer in {SOURCE}")
    values = [int(x) for x in re.findall(r"-?\d+", match.group(1))]
    if len(values) != SAMPLES or any(x < -32768 or x > 32767 for x in values):
        raise SystemExit(f"expected {SAMPLES} signed 16-bit samples, got {len(values)}")
    return values


def zigzag(value: int) -> int:
    return value * 2 if value >= 0 else -value * 2 - 1


def encode_delta(samples: list[int]) -> bytes:
    out = bytearray()
    previous = 0
    for i, sample in enumerate(samples):
        if i == 0:
            delta = sample
        else:
            bits = (sample - previous) & 0xFFFF
            delta = bits if bits < 0x8000 else bits - 0x10000
        value = zigzag(delta)
        while value >= 0x80:
            out.append((value & 0x7F) | 0x80)
            value >>= 7
        out.append(value)
        previous = sample
    return bytes(out)


def lz4_library():
    name = ctypes.util.find_library("lz4")
    if not name:
        raise SystemExit("generating requires a system liblz4; --verify has no such dependency")
    lib = ctypes.CDLL(name)
    lib.LZ4_compressBound.argtypes = [ctypes.c_int]
    lib.LZ4_compressBound.restype = ctypes.c_int
    lib.LZ4_compress_HC.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int
    ]
    lib.LZ4_compress_HC.restype = ctypes.c_int
    return lib


def compress_block(lib, raw: bytes) -> bytes:
    src = ctypes.create_string_buffer(raw)
    capacity = lib.LZ4_compressBound(len(raw))
    dst = ctypes.create_string_buffer(capacity)
    size = lib.LZ4_compress_HC(src, dst, len(raw), capacity, 12)
    if size <= 0:
        raise SystemExit("liblz4 failed to compress a block")
    return dst.raw[:size]


def decode_lz4(src: bytes, cap: int) -> bytes:
    ip = 0
    out = bytearray()
    while ip < len(src):
        token = src[ip]
        ip += 1
        literal = token >> 4
        if literal == 15:
            while True:
                if ip >= len(src):
                    raise ValueError("truncated literal length")
                n = src[ip]
                ip += 1
                literal += n
                if n != 255:
                    break
        if ip + literal > len(src) or len(out) + literal > cap:
            raise ValueError("literal overrun")
        out.extend(src[ip : ip + literal])
        ip += literal
        if ip == len(src):
            return bytes(out)
        if ip + 2 > len(src):
            raise ValueError("truncated match offset")
        offset = src[ip] | (src[ip + 1] << 8)
        ip += 2
        if offset == 0 or offset > len(out):
            raise ValueError("invalid match offset")
        match = token & 15
        if match == 15:
            while True:
                if ip >= len(src):
                    raise ValueError("truncated match length")
                n = src[ip]
                ip += 1
                match += n
                if n != 255:
                    break
        match += 4
        if len(out) + match > cap:
            raise ValueError("match overrun")
        for _ in range(match):
            out.append(out[-offset])
    raise ValueError("missing terminal literal sequence")


def decode_delta(src: bytes, count: int) -> list[int]:
    ip = 0
    previous = 0
    out = []
    for n in range(count):
        value = shift = 0
        for byte_index in range(3):
            if ip >= len(src):
                raise ValueError("truncated varint")
            byte = src[ip]
            ip += 1
            value |= (byte & 0x7F) << shift
            if not byte & 0x80:
                break
            shift += 7
        else:
            raise ValueError("varint too long")
        if value > 65535:
            raise ValueError("varint out of range")
        delta = -((value + 1) >> 1) if value & 1 else value >> 1
        if n == 0:
            if not -32768 <= delta <= 32767:
                raise ValueError("first sample out of range")
            previous = delta & 0xFFFF
        else:
            previous = (previous + delta) & 0xFFFF
        out.append(previous if previous < 0x8000 else previous - 0x10000)
    if ip != len(src):
        raise ValueError("extra delta bytes")
    return out


def get_blocks(include: str | None = None):
    text = include if include is not None else OUTPUT.read_text()
    sizes_match = re.search(r"fm1_waves_block_sizes\[[^]]*\].*?=\s*\{(.*?)\};", text, re.S)
    packed_match = re.search(r"fm1_waves_packed\[[^]]*\].*?=\s*\{(.*?)\};", text, re.S)
    crc_match = re.search(r"FM1_PLAITS_WAVES_PACKED_CRC32\s+UINT32_C\(0x([0-9a-fA-F]+)\)", text)
    max_match = re.search(r"FM1_PLAITS_WAVES_MAX_DELTA_BYTES\s+(\d+)u", text)
    if not (sizes_match and packed_match and crc_match and max_match):
        raise ValueError("packed include is missing generated metadata")
    sizes = [int(x) for x in re.findall(r"\d+", sizes_match.group(1))]
    packed = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", packed_match.group(1)))
    return sizes, packed, int(crc_match.group(1), 16), int(max_match.group(1))


def verify() -> tuple[int, int, int]:
    samples = read_samples()
    sizes, packed, expected_crc, declared_max = get_blocks()
    offset = 0
    reconstructed = []
    max_delta_size = 0
    for block_index, compressed_size in enumerate(sizes):
        compressed = packed[offset : offset + compressed_size]
        if len(compressed) != compressed_size:
            raise ValueError(f"block {block_index}: truncated packed source")
        offset += compressed_size
        expected_samples = min(CHUNK, SAMPLES - len(reconstructed))
        delta_bytes = zlib.decompress(compressed, -15)
        if len(delta_bytes) > declared_max:
            raise ValueError("delta exceeds declared maximum")
        max_delta_size = max(max_delta_size, len(delta_bytes))
        reconstructed.extend(decode_delta(delta_bytes, expected_samples))
    if offset != len(packed) or len(reconstructed) != SAMPLES:
        raise ValueError("packed stream has extra bytes or wrong sample count")
    source_crc = zlib.crc32(b"".join(struct.pack("<h", x) for x in samples)) & 0xFFFFFFFF
    packed_crc = zlib.crc32(b"".join(struct.pack("<h", x) for x in reconstructed)) & 0xFFFFFFFF
    if reconstructed != samples:
        mismatch = next(i for i, (a, b) in enumerate(zip(samples, reconstructed)) if a != b)
        raise ValueError(f"sample mismatch at {mismatch}: source={samples[mismatch]}, packed={reconstructed[mismatch]}")
    if packed_crc != expected_crc or source_crc != expected_crc:
        raise ValueError(f"CRC mismatch: source={source_crc:08x} packed={packed_crc:08x} expected={expected_crc:08x}")
    if max_delta_size != declared_max:
        raise ValueError(f"declared max delta size {declared_max} != measured {max_delta_size}")
    return len(packed), max_delta_size, packed_crc


def generate() -> tuple[int, int, int]:
    samples = read_samples()
    blocks: list[bytes] = []
    max_delta_size = 0
    for start in range(0, SAMPLES, CHUNK):
        raw = encode_delta(samples[start : start + CHUNK])
        max_delta_size = max(max_delta_size, len(raw))
        if len(raw) > MAX_DELTA_BYTES:
            raise SystemExit(f"delta block exceeds bounded decoder buffer: {len(raw)}")
        compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
        blocks.append(compressor.compress(raw) + compressor.flush())
    crc = zlib.crc32(b"".join(struct.pack("<h", x) for x in samples)) & 0xFFFFFFFF
    lines = [
        "/* Generated by tools/pack_plaits_waves.py; do not edit by hand. */",
        f"#define FM1_PLAITS_WAVES_PACKED_CRC32 UINT32_C(0x{crc:08x})",
        f"#define FM1_PLAITS_WAVES_PACKED_SIZE {sum(map(len, blocks))}u",
        f"#define FM1_PLAITS_WAVES_MAX_DELTA_BYTES {max_delta_size}u",
        "static const uint16_t fm1_waves_block_sizes[FM1_PLAITS_WAVE_BLOCK_COUNT] = {",
        "  " + ", ".join(str(len(b)) for b in blocks),
        "};",
        "static const uint8_t fm1_waves_packed[FM1_PLAITS_WAVES_PACKED_SIZE] = {",
    ]
    for block in blocks:
        lines.append("  /* block */")
        for i in range(0, len(block), 16):
            lines.append("  " + ", ".join(f"0x{x:02x}" for x in block[i : i + 16]) + ",")
    lines.append("};")
    OUTPUT.write_text("\n".join(lines) + "\n")
    return verify()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", action="store_true", help="regenerate packed include using raw DEFLATE")
    args = parser.parse_args()
    try:
        size, scratch, crc = generate() if args.generate else verify()
    except (OSError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    print(f"verified {SAMPLES} samples, packed={size} bytes, max_delta={scratch}, crc32={crc:08x}")


if __name__ == "__main__":
    main()
