"""Parser for private, read-only USB-audio diagnostic command 0x8A."""
from __future__ import annotations

import struct

SCHEMA_VERSION = 1
WORD_COUNT = 19
REPLY_BYTES = WORD_COUNT * 4
FLAG_NAMES = {
    0: "alt_active",
    1: "host_rate_48k",
    2: "ring_rate_48k",
    3: "ring_primed",
    4: "host_flowing",
    5: "packet_queued",
    6: "render_feed_enabled",
    7: "usb_up",
    8: "usb_configured",
    9: "rate_request_pending",
}


def parse_uac_diag8a(raw: bytes) -> dict[str, int | dict[str, bool] | dict[str, str]]:
    """Decode the fixed 76-byte schema-1 response, rejecting malformed replies."""
    if not isinstance(raw, (bytes, bytearray, memoryview)):
        raise TypeError("0x8A payload must be bytes-like")
    if len(raw) != REPLY_BYTES:
        raise ValueError(f"expected {REPLY_BYTES}-byte 0x8A reply, got {len(raw)}")
    words = struct.unpack("<19I", raw)
    if words[0] != SCHEMA_VERSION:
        raise ValueError(f"unsupported 0x8A schema {words[0]}")
    flags = words[1]
    known_mask = (1 << len(FLAG_NAMES)) - 1
    if flags & ~known_mask:
        raise ValueError("0x8A state flags contain unknown bits")

    names = (
        "schema_version", "state_flags", "wait_polls", "missed_packets",
        "stalls", "ring_packets", "ring_frames", "stream_starts",
        "rate_set_requests", "ring_underruns", "ring_overruns",
        "packets_plus_one", "packets_minus_one", "ring_read_index",
        "ring_write_index", "ring_fill_now", "fill_sample",
        "stream_fill_low", "stream_fill_high",
    )
    result: dict[str, int | dict[str, bool] | dict[str, str]] = dict(zip(names, words))
    if words[15] != ((words[14] - words[13]) & 0xFFFFFFFF):
        raise ValueError("0x8A ring fill does not match the reported indices")
    result["state"] = {name: bool(flags & (1 << bit)) for bit, name in FLAG_NAMES.items()}
    result["counter_lifecycle"] = {
        "boot_lifetime": "missed_packets, stalls, stream_starts, rate_set_requests, "
                         "ring_underruns, ring_overruns, packets_plus_one, packets_minus_one",
        "rate_epoch": "ring_packets and ring_frames reset on host sample-rate change",
        "stream_interval": "stream_fill_low/high reset when alternate setting starts a stream; "
                           "0xffffffff is the pre-sample low sentinel",
        "snapshot": "wait_polls and state flags are current values; ring indices are uint32 cursors",
    }
    return result
