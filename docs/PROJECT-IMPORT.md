# Project import

The pattern-device-candidate distribution firmware supports importing one complete
PSC3 project to one selected named slot. This is the device-side foundation for
future website project loading; it is not yet a website feature or an export tool.
The live project is not switched automatically. Loading through the normal panel
updates the working project in the usual way.

The computer sends D6 (begin with a nonzero nonce, zero-based slot, byte length,
and a padded name), D7 (32-byte aligned data chunks), and D8 (nonce plus SHA-256).
The device checks the digest, validates the complete decoded session, patterns,
and arrangement, commits through the selected slot's existing two-copy store,
and verifies readback. D9 reports the latched result; DA releases the staging
buffer. Duplicate chunks must match; a commit is not repeated after a reply is
lost. Staging expires after 120 seconds of inactivity. While staged, diagnostic
session capture/upload commands are refused. This protocol is available only in
builds with the device-candidate project runtime; clients must verify firmware
identity or use a future explicit capability negotiation before sending it.

`tools/fm1_project_import.py` is the current computer-side client. It checks the
exact identity, transmits one PSC3 file, and writes the requested slot. The
separate `fm1_project_json.py` and host helper are an experimental converter for
the particular score format used by Sunday Morning House; they are not a general
project editor. The saved payload limit is 3,584 bytes, and the decoded pattern
bank must fit the 4,096-byte loading arena.

JOSBANK1 is a different whole-bank archive format. The experimental website code
can inspect it locally; it cannot yet export projects from an ordinary distribution
firmware or safely load a whole archive. A future website must extract and validate
logical projects, use the device's project API instead of writing raw sectors,
handle incompatible versions and interrupted transfers, and present clear
per-project destination choices. The SMK-37 path has not been qualified on device.

## Exporting saved projects

The matching candidate firmware exposes `DB` capability `PEX` version 1.
`DC` snapshots one named slot under the normal project-store fence and returns
its name, length, and SHA-256; `DD` reads that immutable snapshot in bounded
chunks; `DE` releases it. The snapshot expires after 120 seconds of inactivity.
No raw flash addresses are accepted, and no live or saved project is modified.
`tools/fm1_project_export.py` checks the device identity and payload digest and
creates a new file without overwriting an existing file. The website uses the
same commands. A collection contains separate per-slot snapshots, not an atomic
whole-bank snapshot. Save any unsaved edits on the instrument first.

## Listing project slots

Candidate firmware with listing support answers `DF` with `PLS` version 1.
A `DF` request for slot 0 or 6 returns six slot states and names under one
read-only parked transaction. Empty means both sectors were fully erased;
present means the selected pair's record has valid framing and payload CRC,
not that the complete project has been semantically validated. Ambiguous,
foreign, corrupt or torn copies are reported unavailable. The Editor can fall
back to full project exports on earlier project-transfer firmware. Export and
load retain their full validation. Real-device timing has not yet been measured.
