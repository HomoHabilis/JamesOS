# FM-1 boot and update resource provenance

Evidence recorded 16 November 2026 for the local FM-1 build 3029 from-V15 package
(SHA-256 `39e7b4c72affcba8d1c343c58c12cf51e350ca7338c806063c8423b076998fd7`)
and the archived build 2108 from-V15 package. Both have the same first 0x4000-byte
boot area, SHA-256 `2c0d29a6993955a1877fffa5e6bb3e14846f3aba241e71288ded21ef24e918e5`.

M-VAVE's [FM1 Open third-party notice, pinned revision](https://github.com/m-vave/fm1_open/blob/4c7496daac04a95ff10d1afdcc8f9ead2dbabc41/THIRD_PARTY.md)
identifies `sdk/tools/pack/uboot.boot`, `ota.bin`, `cfg_tool.bin`, `boot-config.bin`,
and `sdk/firmware/FM-1_015.fwsc` as Apache-2.0 resources. It requires the Apache
licence, identification of modifications, and retention of applicable notices.
A copy is [MVAVE-FM1-OPEN-THIRD-PARTY.md](MVAVE-FM1-OPEN-THIRD-PARTY.md).
Resources with their own terms retain those terms; the full notice should accompany
a distribution containing the M-VAVE material.

## Byte-level trace

- The manufacturer's pinned `uboot.boot` SHA-256 is
  `b88747cb72afc554d6ca078594a89f4fdcea5a74d26345b633f8e2524e7e3415`.
  Its 16-byte bank headers and bank payloads are encoded separately for flash.
  Encoding that file produces the package bootloader byte-for-byte. Each decoded
  bank header and payload passes its CRC, and the decoded file's CRC (`dcc8`)
  matches the flash-directory entry. The raw encoded CRC (`cba6`) is not the
  format's content checksum.
- The pinned `boot-config.bin` SHA-256 is
  `9c2f90573c2490991fcf68f5e3d9c3d713deed5d114934477c36303d4e67df3c`.
  The package's settings resource consists of a reproducible 34-byte public
  chip-key descriptor followed by that 721-byte configuration with five changed
  bytes at configuration offsets `0x4d`, `0x4e`, `0x4f`, `0x9c` and `0xa8`.
  Their original-to-modified values are `d0->7b`, `12->9a`, `13->17`,
  `0b->0e`, and `03->04` respectively. The build's documented purpose is the
  396 MHz clock profile. The settings entry CRC is recomputed; no per-device
  flash data are incorporated.
- The package OTA resource SHA-256 is
  `ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541`.
  It is byte-identical to the OTA resource extracted from the official V15
  recovery package SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`.
  Its LZ4 blocks use preceding decoded output as a dictionary; the expanded
  size and CRC pass. The `ota-FM-1_015` loader identity was observed on the FM-1,
  not extracted as a literal string from the binary.
- These checks establish the identified boot/update component provenance and
  modification record, not complete corresponding-source or all-component
  licence compliance for a new release. Archive notices and source for the
  actual release candidate, and check its exact hashes before distribution.
