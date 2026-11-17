# Sound saving: foundation and integration plan

The standalone sound record codec is a prototype; it is not yet connected to
sound saving, loading, the device UI, flash, or a website. It performs no
physical writes. Its versioned 336-byte record, field order and CRC are
specified in [sound_record_format.md](../firmware/src/sound_record_format.md).
The format remains subject to review before publishing an interchange contract.

The model captures the current `br_part_t` controls for synths and kits:
oscillator, filter and envelope controls, gate, per-track effects, all LFOs and
modulation routes, and kit voice choices. Loading preserves track level, pan,
mute, lane levels, patterns and other sequencing data. It does not capture
global effects, compressor, or arpeggiator controls; some session-v17 fields
are reserved or do not have a current live `br_part_t` equivalent. Codec
callers must provide coherent capture and use the existing track revision
publication protocol for apply. Decode validates before changing output and
apply rejects the wrong track role.

No flash region is allocated. FM-1 project records occupy 0x97000–0xB1000;
the broader flash data window also contains other users and ownership must be
proven before any sector is claimed. On SMK-37, logical projects occupy
0x9D000–0xB7000; physical translation skips two occupied sectors and extends
through 0xB9000, followed by settings at 0xB9000, owner at 0xBA000, and OTA at
0xBB000–0xC0000. Neither apparent gaps nor updater behavior establish free,
safe storage. Existing projects and foreign data require explicit protection.

Before wiring persistence, audit both product flash maps, updater recovery and
all current owners; choose a capacity, durable two-copy/atomic publication
scheme, naming and version migration policy. Validate power-loss, corruption,
foreign-data and upgrade behavior in a flash model, then add UI and a transport
for reviewed website import/export. Confirm user-visible choices and audio
publication behavior before any on-device test. Host tests run with
`python3 tests/run_sound_record.py` for both product configurations.
