# Project deletion experiment

This is an isolated experiment, enabled on host tests and in explicitly flagged
target builds only. No deletion command is exposed on the instrument or in
the Editor, and it is not qualified for device writes.

The pair store can hold two generations of a project. Erasing the newest copy
first can expose the older version again after a power failure. The experiment
instead pins the selected project's name and payload SHA-256 under the existing
pair owner and fence, erases and fully checks the older sector first, checks the
selected generation and identity again, and only then erases the newer sector.
It checks ownership, parked state and the external guard before erases. The
host tests exercise both erase phases at every prefix boundary, selected
non-prefix bit patterns, identity mismatches, guard and lease revocations, and
preservation of other slots.

An interrupted first erase can leave the latest project readable alongside a
damaged older sector. The ordinary delete refuses to erase that ambiguous
sibling. An experimental recovery path accepts a fresh, explicit authorization to
discard the incompatible sibling and verifies the surviving name and digest
before any erase. Tests cover interrupted retries and ensure unrelated slots
and the live project remain unchanged. This consent must be surfaced explicitly
in the Editor; a previous delete confirmation is insufficient.

If the final erase is interrupted, both halves can instead be unreadable. An
experimental separate operation handles this only with a fresh explicit
permission to discard all unavailable data in that exact slot, and after
checking a SHA-256 fingerprint of both full sectors. It refuses to clear a slot
with a readable survivor. This is only pair-owner code and host testing: the
target checks, and rollback behavior are not qualified. The raw-pair review
and clear have an experimental runtime path tested against the host project
fixture. The Editor does not yet provide the separate confirmations. A damaged sibling cannot be
assumed to be our own data.

Before exposing the command, establish the target partition and continuous
fence guarantees, design and test the recovery record and rollback behavior,
measure peak target stack and erase latency, test interruptions throughout the
real storage/transport path, and verify that the selected working project and
all other slots remain intact. The simulated cuts are not proof of arbitrary
physical flash failures. Do not perform destructive tests against the user's
saved projects.

A separate reviewed-delete gate pins a slot, name or raw-pair fingerprint,
operation type, and random nonce to one confirmation. The gate expires pending
reviews, disarms cancellations, and latches the result so a repeated request
does not run the erase twice. An explicitly flagged candidate connects the gate to experimental transport
commands. Each review is read from the device and commits recheck the pinned
identity. This candidate is not qualified for installation.

A guarded sizing build retained the deletion and review-gate code without
exposing commands: versus build 2325 it used 3,328 more image bytes, 32 more
bytes of data/BSS, and 16 more pool bytes. That was a sizing build rather than
a complete feature and does not qualify stack use or timing.

Run `python3 tests/project_delete_safety.py` for the host experiment. Run
`python3 tests/run_pattern_delete_protocol_runtime.py --reuse-dsp` for the
review-to-runtime integration when host DSP objects are available.
