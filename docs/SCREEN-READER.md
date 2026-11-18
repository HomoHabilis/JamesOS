# Screen reader

JamesOS can tell a screen reader program on the computer what its screen shows, so the reader can
speak it. It uses the FM-1 community screen reader protocol, version 1 (charlesvestal/fm1-reader,
`PROTOCOL.md`), over USB-MIDI SysEx. Any reader for that protocol works, for example the
fm1-reader web page.

The adapter is `firmware/src/instrument_reader.h`, switched by `BR_CAP_READER` in
`firmware/product.h` (on by default for both FM-1 and SMK-37 Pro). It was written for JamesOS from
the protocol's description and is GPL-3.0-only like the rest of the firmware. The adapter is
called from the UI pass (`br_reader_tick`, in `dinghy.c`) and receives SysEx frames before the
updater does (`platform_poll`, in `platform.c`).

Updated 16 November 2026 against the source for candidate build 3078. This page was checked
against the source and `tests/test_reader.c`. The test was not run for this update, and nothing was
checked with a physical instrument or a real reader program.

## The protocol, as implemented

Every message is `F0 7D 53 52 <cmd> <args> F7`, all bytes in between 7-bit.

| From | cmd | Args | JamesOS |
|---|---|---|---|
| reader | 1 HELLO | version | Answers every HELLO. The first one also starts listening and says the whole screen. |
| reader | 2 BYE | | Stops. |
| reader | 3 WHERE | | Says the whole screen again, at once. |
| device | 1 HELLO | 1, name, 0, link | Name is the app name, the version string and the package identity, joined by spaces, e.g. `JAMESOS 1.0b-device-test FM-1_9673078`. On the SMK-37 Pro the app name is `JAMESOS-K` and the version string is the one `tools/build.py` sets for that target (`1.0b-bring-up` unless a release version is given). Link is empty: no words file is published. |
| device | 16 SAY | flags, text | Flag 1 (NOW) cuts off what is being said. KEEP (2) is not used. |

- **Silent until asked.** Nothing is sent until a HELLO arrives; while nobody listens the UI pays
  one flag test per pass. Five seconds without a HELLO stops it. The test reader sends one every
  2 s, which is what the protocol reader is expected to do.
- **Not ours.** A frame that does not start `7D 53 52` is left for the updater. A SysEx that does
  start with it is consumed by the adapter and never reaches the updater.
- **Never waits.** A SAY is queued only when the USB SysEx queue has room for all of it;
  otherwise it stays pending and is tried on a later pass. SysEx packets leave before any note or
  clock packet and notes are held while a SysEx is being queued, so nothing lands inside a SAY.

## What is said

All text is the screen's own strings: `br_header_title()`, `br_control()` with the card value
formatter `br_control_text()` (dB on the mixer and kit level strips, `br_level_db_text()`), the
message line `br_ui.message`, the OCT-/+ hint `br_oct_hint()`, the list rows `br_sel_row_text()`
and the SAVE menu's titles, choices and slot names. The adapter only reads these strings. It does
not describe what is on screen beyond them: it does not describe graphics, the playhead, pad or
key state, or meters.

| When | What | Example |
|---|---|---|
| A new screen or page (after 200 ms still) | title, selection, the four knobs, hint | `ENV / AMP. ATTACK 1.04MS, DECAY 191MS, SUSTAIN 87%, RELEASE 37.5MS` |
| A knob turns (after 120 ms still, at least every 500 ms) | label and value | `RELEASE 298MS` |
| List row (SOUNDS, DRUM KITS) | row and its place | `002 PURE SUB, 2 OF 128` |
| Mixer / kit level | the selected strip in dB, then the others | `MIXER. TRACK 1 0.0 dB. TRACK 2 0.0 dB, TRACK 3 0.0 dB, DRUM KIT 0.0 dB` |
| Sequencer step | step number, then note, length, velocity, condition, then the sequencer cue | `SEQUENCER. STEP 1. NOTE C4, LENGTH 1 STEP, VELOCITY 100, CONDITION ALWAYS. ` followed by the cue |
| SAVE menu | title and choice; slot rows with their place; the name being edited | `CHOOSE SLOT. 01 EMPTY, 1 OF 12` |
| The message line | the message, at once (NOW); what follows within 1 s queues behind it | `HOLD ON` |

The examples come from the expected strings in `tests/test_reader.c`, which uses the host build's
default sound and settings. Values on a real instrument differ. The sequencer cue is spoken as one
line. Its wording changes with the firmware, so it is not repeated here.

On a kit track the step announcement also names the drum voice after the step number
(`STEP 3 <voice>`). While a step is held for editing, the step announced is the held step.

## What is spoken per page

Every new page or screen says its title first (`br_header_title()`), then the selection if the
page has one, then the four knobs that have a label, then the hint if the page has one. The titles
below are those the source returns. A page with no row in the "selection" column says no
selection.

| Page | Title spoken | Selection spoken | Hint spoken (when not empty) |
|---|---|---|---|
| HOME | the sound's name; `MOD DEPTHS` or `VOICE` on the deep screens | none | on the first HOME screen: the polyphony shortcut text, a playback warning, or (SMK-37 Pro) a model-limit note; on MOD DEPTHS: `SCALES ALL MODULATION` |
| ENV | `ENV / AMP`, `ENV / MOD`, `ENV / MOD SET` | none | none |
| FX | `FX / SENDS`, `FX / DRIVE`, `FX / DELAY`, `FX / MOD`, `FX / REVERB`, `REV / TIME` | none | none |
| GLOBAL | `MIXER`, `GLOBAL / FILTER`, `TEMPO`, `GLOBAL / PAN`, `COMPRESSOR`, `COMP TIME`, `GLOBAL / HEAT`, then `MIDI 1`, `MIDI 2` (FM-1) or `USB MIDI 1`, `USB MIDI 2` (SMK-37 Pro) | on MIXER, the selected strip in dB; the other strips follow | on TEMPO, the build text (`BUILD` and the digits after the family prefix, e.g. `BUILD 3078`) |
| SEQUENCER | `SEQUENCER` | the step number (and drum voice on a kit track) | the sequencer cue |
| PATTERNS or ARRANGER | `PATTERNS` or `ARRANGER` | none | the arranger hint (brackets such as `[PATTERN KEY]` are spoken as written; the hint alternates every few seconds, so a screen change says whichever is showing) |
| ARP or RETRIGGER | `ARP` or `RETRIGGER` | none | `HOLD STEP: EDIT` on RETRIGGER |
| SOUNDS or DRUM KITS | `SOUNDS` (synth tracks) or `DRUM KITS` | the row and its place, no knobs | none |
| PUNCH FX | `PUNCH FX` | none | `OCT-/+: PAGES` |
| LFO | `LFO 1`, `LFO 2`, `GLOBAL LFO` | none | none |
| PATCH or kit mixer | `PATCH 1`, `PATCH 2`, or `KIT LEVEL` | on KIT LEVEL, the selected lane in dB | none |
| SAVE menu | `SAVE`, `SAVE FIRST?`, `CHOOSE SLOT`, `SAVE AS`, `OVERWRITE`, `CONFIRM` | the action, choice, slot row or name being edited; no knobs | none |

The hint column lists what `br_oct_hint()` returns for the page at the time of writing. A hint
speaks only when the screen is announced, not when it changes later. The test exercises the home, ENV, SOUNDS, MIXER, SEQUENCER and SAVE screens only. The other rows were
read from the source.

The list places ("2 OF 128") are the numbers the screen shows as "2/128", in words, so a reader
does not say a fraction. The sounds list has 128 entries. The drum kit list has 12 entries in
builds with the Plaits kits (3 otherwise). Sound and kit rows are numbered with three digits, SAVE slots with two.

## Limits

- A SAY carries at most 170 characters (the protocol allows 240): the whole frame, 177 bytes,
  then fits the 64-packet USB SysEx queue with room to spare. Longer text is cut.
- Sends are at least 50 ms apart. Worst case 20 SAYs a second of 177 bytes, about 3.5 KB/s;
  normal use is a few SAYs a second. A full-speed USB-MIDI endpoint moves 64 bytes per frame
  (64 KB/s). While a long SAY drains (59 packets, a few USB frames) MIDI clock and notes out
  wait behind it: a few milliseconds of jitter, only while a reader listens. These rates and the jitter
  figure are design estimates from the queue sizes. They were not measured on an instrument.

## Size (distribution builds against b5e0b45)

These figures were measured when the adapter was added, against commit b5e0b45. They have not been
remeasured on the current tree. For the current linked budgets see [SMK37-PORT.md](SMK37-PORT.md) (SMK-37 Pro).

| | FM-1 | SMK-37 Pro |
|---|---|---|
| Flash (image) | +2016 B | +1936 B |
| .data + .bss, RAM code | +0 B | +0 B |
| Spare pool (the adapter's state) | +40 B | +40 B |

The state lives in the zeroed spare pool, as `br_sel` does, so the layout of `.data`/`.bss` and
the code of the audio and USB interrupts are unchanged. A frame is built on the main-loop stack
(about 190 B) only while a reader listens. At the time, with `-DBR_CAP_READER=0` the FM-1 ELF was
byte-identical to b5e0b45's. That was not rechecked.

## Tests

`tests/test_reader.c` (`tests/run_reader.py`, in all four host baselines) checks silence before
HELLO, the HELLO answer, the exact bytes of fourteen announcements (home, a page, a knob, the
sounds list and a row, the mixer and a strip, the sequencer, a held step and its velocity, the
SAVE menu, a choice, its slot list and a row), a knob turned continuously (a SAY about every 500 ms),
a message and what follows it, the rate limit, a full queue, WHERE, BYE and the five-second timeout, and
that every frame is well-formed SysEx within the limits. `tests/run_reader.py` runs on the host and
never touches hardware. No test drives a real reader program, so interoperability with other
readers of the protocol is unverified.
