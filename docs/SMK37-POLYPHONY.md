# Stopped-transport polyphony

> **Status, 16 November 2026:** `tools/build.py --target smk37` builds from this tree and the polyphony sources are in it. This page was imported from the standalone JamesOS-K repository and has not been fully revised. The voice count below (six) is the count without `BR_ADAPTIVE_POLY`; the current candidate build command sets `BR_ADAPTIVE_POLY=1`, which allows up to eight voices with a per-model limit (see [SMK37-PORT.md](SMK37-PORT.md)). Other statements here were not rechecked.

Polyphony began as a JamesOS-K (SMK-37 Pro) feature. FM-1 device candidates now include it too
(`tools/build.py --target fm1`; `--no-polyphony` leaves it out, outputs in `-nopoly` directories); it
has not been tested on FM-1 hardware.

With the sequencer stopped, the selected synth patch can play up to six notes.
Each synth track has a MONO/POLY voice mode; selecting a factory sound applies
its preferred mode, and the mode is saved with the project. On HOME, hold OCT-
and press PLAY to switch the selected track's live polyphony. Older sessions without a
track-specific value inherit their saved global POLY setting.
Other synth tracks and drums are unavailable while the voices are in use; their
saved settings and sequences are preserved. Starting the sequencer restores the
normal track engines. The transition back to polyphony waits for held synth
notes and the selected track's audible release to finish.

Six voices use independent sound sources and envelopes, with tagged keyboard
and MIDI note events. Voice stealing uses the oldest voice, and generation tags
prevent an old note-off from releasing a newer strike. The voices share the
selected track's filter and effects; filter key tracking follows the newest
voice. Levels are managed after summing to reduce overload.

Particle, speech and string use memory borrowed from inactive tracks' delay
buffers. Punch echo is unavailable while playing these models polyphonically;
the other punch effects remain available. The selected track's delay is
preserved, and inactive delay tails are cleared when normal playback resumes.

Device candidate builds enable the `BR_POLY_SCHEDULER_EXPERIMENT` build flag by
default; pass `--no-polyphony` to omit it. Host
tests exercise all 24 Plaits models, compare two-worker and single-worker
output, and cover transport, note release and memory restoration. Hardware
playback, release and transport have been exercised on the connected SMK-37;
worst-case timing across every model and effect still needs hardware testing.

Relevant host checks:

```sh
python3 tests/run_poly_render.py
python3 tests/run_poly_scheduler.py
python3 tests/run_poly_two_cores.py --reuse-dsp
```
