# Offline 1.0a project migration prototype

This is a development workflow, not a qualified public upgrade path. It
operates only on files and never reads or writes an instrument. Keep the
original backup and the original firmware available.

For a verified `.josbank` archive containing 1.0a projects, extract a named
slot to a new file and convert that payload to another new file:

```sh
python3 tools/project_backup_extract.py old.josbank 1 old-slot.psc
python3 tools/project_v4_to_v5.py old-slot.psc converted-slot.psc
```

The extractor validates the backup's overall and per-pair hashes, sector
headers, generation ordering and payload checksum. It refuses ambiguous
copies and never overwrites its output. The converter accepts PSC3 or raw FMP1
version-4 projects with a v16 session. It checks the envelope, runs the
existing v16 session converter, preserves the compressed pattern records,
then moves each track's former visible routes 0 and 1 into its v18 PATCH 1
and PATCH 2 slots, clearing the original copies so they are not applied
twice. Other existing track routes remain intact. It asks the current
project decoder to validate the resulting version-5 envelope, containing
the current v20 session. Its added LPG gate lengths default to OFF for
legacy input; older firmware that cannot decode v20 cannot import this output.
The eleven 1.0a delay-time buckets are mapped to equivalent
musical durations in the current fourteen-entry saved range; all 128 source
values are covered by `tests/test_v4_delay_map.c`. The GLITCH engine's
character has also changed, so preserving the time division does not promise
identical glitch behavior. Both LFO rates are mapped from 1.0a's linear law
to the nearest available current speed. All 128 inputs are checked by
`tests/test_v4_lfo_rate_map.c`; modulation of an LFO's rate may still sound
different because the laws differ. The converter warns when an enabled route,
LFO, or modulation envelope targets an LFO rate at a nonzero depth. Pitch LFO and patch depth amounts use the
closest current peak to compensate for the new quadratic control law; the
quantization and intermediate waveform can differ. The full signed range is
checked by `tests/test_v4_pitch_depth_map.c`. Non-neutral 1.0a HOME LFO depth
controls were additive LFO 1 routes, whereas current MOD DEPTHS attenuates all
modulation. The converter restores the former as additional track routes and
sets the current attenuverters to unity. If no empty route is available, it
refuses the conversion rather than discard another assignment. The old drum
HOME MORPH modulation was inactive and is reported if set. It compresses the
envelope to a slot-sized PSC3 payload and
verifies decompression. It refuses existing output files. The session
converter reports fields that have no direct destination; a successful
conversion does not imply identical sound.

`python3 tests/run_v4_conversion_cli.py` exercises extraction and conversion
on an archived project fixture, per-track patch mapping, integrity failures,
copy selection and no-overwrite behavior. `python3 tests/run_v4_migration_proof.py` independently
checks that fixture's pattern records and current decoder. The same fixture's
converted v17 session also passed `tests/run_session_wire.py` through the
real application restore and v18 recapture on the host.
`python3 tests/run_v4_migration_import.py` additionally imports and loads the
converted project in the host application and checks its six patterns, tempo,
v18 session capture, and representative migrated modulation using the real
modulation arithmetic. These checks do not prove general migration, an
instrument backup path, audible fidelity or hardware import.

Outstanding release work includes a supported way to obtain complete backups
from stock 1.0a installations, broader representative project fixtures and
field-by-field loss review, capacity testing, and a device-qualified
import/save/load workflow. Old project slots remain
unchanged until a user explicitly replaces them; this prototype never writes
to a slot.

The archived build-2108 distribution settings have `pattern_sector_test:
false`, so the current sector-backup tool cannot assume that command exists
on the public build. The archived 1.0a source includes a bounded sector-read
handler behind that build flag; its host gate/bounds checks pass when enabled.
An isolated bridge build can be investigated, but the command is not a
qualified extraction path until its actual installed behavior and complete
backup are verified. A development build may also contain other diagnostic
commands; do not describe the entire bridge firmware as read-only.

An isolated build from the archived 2108 source, with `--pattern-sector-test
--diagnostics development`, passed build and placement checks as
`FM-1_9673046`; package SHA-256
`32c1fb8fea18df5b97b6c10e8b0ec400124a7e13ab6d402f7a6f9a80c28eeba5`.
The source archive's sector diagnostic host test passed with the command
enabled and disabled. The bridge was installed locally for a development test
and has since been replaced by the current candidate. With both instruments
connected, the backup path read the FM-1 partition twice and converted a mixed
archive containing an archived v4 fixture and newer projects. The converted v4
project was imported to an empty slot on build 3065 and verified by device
readback. The owner confirmed that this converted fixture loads and plays.
Broader audible fidelity, the upgrade from an actual 1.0a installation, and
a supported public workflow remain to be qualified. This
development bridge is not published and is separate from the 1.0b candidate;
other diagnostic commands in it are not read-only.
