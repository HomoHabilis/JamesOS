# Development diagnostics and operational safeguards

`tools/build.py --diagnostics development` includes the private development commands, capture buffers and profiling code. `--diagnostics distribution` selects the public device configuration and rejects runtime profiling, panel and sector tests, and provisioning or namespace experiments. The pattern and project options selected by `--pattern-device-candidate` implement the shipped sequencer and save format.

Distribution excludes synthetic workloads and UI controls; private CPU, audio, USB, MIDI clock, worker, FX, register and power snapshots; audio timing and drop counters; late and tail traces; the worker smoke job; foreground timing samples; the reporting-only startup RAM checksum; and the optional crash sidecar capture/read paths. The development build retains these facilities. A few configuration-dependent declarations may remain in source, but the build inspection checks the generated code and symbols, not just the command dispatcher.

Operational work is retained in both configurations:

- The on-screen CPU meter uses its own audio-duration averaging window. The audio deadline, worker timeout, phase guard, mute and buffer ownership checks remain active.
- Scheduler source-cost estimates and FX-job cost estimates remain because they determine the running audio schedule. MIDI clock timing and its fail-closed transport behavior remain.
- Startup still validates the worker, memory guards, watchdog, power transitions and prior boot state. The boot journal retains its header and boot identifier even when development timing samples are absent.
- The primary crash record and fault bundle, guarded reboot, rescue mode and loader remain. Distribution fault records mark development-only worker and FX detail as unavailable; this does not change their format. Diagnostic-only crash sidecars are reserved as empty address ranges to preserve the established retained-memory layout.
- Installer identity and transfer use the OTA protocol. Session status, guarded upload and restore, protection status and their buffers remain available; these are operational recovery paths, despite historical `diagnostic` names in their implementation.

`build-settings.json` records the selected configuration. The source and build checks do not by themselves qualify a release: installer, upgrade, recovery, saves and hardware behavior must be checked against the exact candidate package. See the project release tracker for current evidence and outstanding hardware coverage.

Build 2108 retains small physical-MIDI receive activity counters in the distribution image. They are not exposed by a distribution diagnostic command.
