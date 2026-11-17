# JamesOS 1.0b

**Build 3083, 17 November 2026.** This is the source at the `v1.0b` tag. The packages were built from it: its `firmware/` folder and `tools/build.py` are the ones used for the build.

JamesOS is an independent firmware project by Aaron Grando (a.k.a. James Os) and Gjelly for the M-VAVE FM-1. It builds a second product, JamesOS-K, for the M-VAVE SMK-37 Pro, from the same source tree. This page lists what changed since 1.0a build 2108 (9 November 2026), what changes for existing users, what is known to be wrong, and what has and has not been checked.

Firmware switching can overwrite another firmware's saved data. Back up saved work before installing. Read [Projects from 1.0a](#projects-from-1.0a) first if you have projects made with 1.0a.

## Builds and packages

| Product | Device | Identity | Update loader | App package SHA-256 | Size |
| --- | --- | --- | --- | --- | --- |
| JamesOS 1.0b | M-VAVE FM-1 | `FM-1_9673083` | `ota-FM-1_904` | `4ffe02d6cdceed126a5e2c3457df46105306976eafbb82e148e5a3252e5fa5e7` | 610,554 B |
| JamesOS-K 1.0b | M-VAVE SMK-37 Pro | `SMK-37 Pro_9653083` | `ota-SMK-37 Pro_9260001` | `fd31046bcdd28634916e9e07a0ee1ed9c07a0a6d28f17edef32bc2381f08c120` | 651,472 B |
| FM-1 package for a stock V15 unit | M-VAVE FM-1 | `FM-1_9673083` | `ota-FM-1_015` | `1d7ad1331f9e2b1020d402c749c73ee6e92e9ce50e4966f8063277f837f039aa` | 623,125 B |
| Source archive | | | | published beside the archive | |

The loader identifiers are the ones each package reports while it updates.

Each package is built with `tools/build.py` and the flags in the README. The exact command for the final build is recorded in `BUILD-REPRODUCIBILITY.txt` in its source archive. Both update packages rebuild byte for byte from that source archive with the same toolchain and SDK.

## What is new since 1.0a

### Sounds and engines

- The synth-track sound list grows from 64 to 128 starting sounds. Slots 1 to 32 are favourites. The rest are grouped by kind, such as bass, leads, keys, pads and arps. See [SOUND-BANK.md](SOUND-BANK.md).
- The 80S and 90S drum kits, including their DEEP and BRIGHT variants, use synthesized Plaits percussion instead of the earlier sample kits. They do not sound identical to the old kits. A project that used an old 80S or 90S kit loads with the new synthesized kit and shows the message `80S KIT NOW SYNTH` or `90S KIT NOW SYNTH`.
- Polyphony on the FM-1 and the SMK-37 Pro. With playback stopped, OCT- with PLAY on HOME turns polyphonic play on or off for the selected synth track. The number of voices depends on the engine, from 3 to 8, and it is lower while a MODEL modulation is active. Other tracks are not available while polyphony is on. Starting the sequencer returns to normal multitrack playback. Punch echo is unavailable while an engine has borrowed the delay memory. See [SMK37-POLYPHONY.md](SMK37-POLYPHONY.md).
- Voice modes on the HOME deep menu: portamento time, glide mode, amplitude mode and, in LPG mode, a timed gate length (OFF, or one of 31 timed settings). OCT- with HOME opens the deep menu. Kit tracks have no voice controls.
- The DX7 banks used by the six-operator FM engine are read from flash instead of being unpacked into data memory at start. About 12 KB of data memory (12,288 bytes) is free again on both products. The change moves where the banks are stored. Owner listening check of DX7-style sounds: see the verification section.

### Effects

- Each effect page now has its own fourth control: BIAS on DRIVE, TONE on DELAY, FEEDBACK on MODULATION and PRE DELAY on REVERB. Each has its own picture. DELAY TONE uses the same picture as the drive TONE control. Wet amount stays on FX SENDS.
- Delay time has 16 note values, from 1/32T to 1 BAR, including triplets and dotted notes.
- Reverb pre-delay can follow the tempo: AUTO, or a note value from 1/64T to 1/8.
- The shared reverb has a new engine. The choices are ROOM, HALL and SPACE. A first-tail error after changing from ROOM to HALL was fixed in build 3028 and confirmed by the owner on the instrument at that build.
- FX and MOD RATE show a rotating ring, like the LFO rate control.
- The FX menu has five screens: SENDS, DRIVE, DELAY, MODUL8N and REVERB. A second reverb page that could not be reached was removed from the code. It was not visible, so nothing changes for the player.
- Modulation can use the global LFO as a source in track patches, and MODEL is a stepped LFO target on Plaits synth tracks. Model changes are discrete steps and can click at fast rates. Random per Note is a patch source that picks one value for each note on a track.
- The filter control runs from low-pass through open to high-pass, and modulation can sweep through the open centre.
- On GLOBAL, FILTER, holding OCT- while turning knob 1 snaps between low-pass, OPEN and high-pass. OCT- with a knob on GLOBAL, PAN snaps to 100% left, centre and 100% right.
- When playback causes repeated audio overruns, the CPU monitor turns on and shows `CPU USAGE HIGH. MONITOR ENABLED.` once per session.

### Sequencer and arranger

- A pattern arranger: a timeline of up to 64 blocks, each 1 to 16 bars. Blocks can loop, and the song can stop or loop at its end. The arrangement is saved with the project.
- The arranger knob icons show their state: BARS lights 1 to 4 slots and adds a plus above 4, LOOP MODE shows a closed loop when on and a struck-through loop when off, and PERFORM shows a single repeating block for PATTERN or a timeline for ARRANGE.
- The bottom cue of the sequencer alternates every 3 seconds between SHIFT NOTES and STEP BANK when the pattern is longer than 16 steps. The screen reader speaks both.
- The song position shows above the sequencer strip.
- Step recording and step editing fixes: a note recorded in a pass is not replayed in the same pass, step recording while stopped advances the cursor, and PLAY remains the transport while a step is held.

### Projects

- Session format v20, saved in project envelope FMP1 v6. Envelope v5 projects load with an empty arrangement and save back as v6. Older firmware rejects v20 rather than dropping what it cannot store. Details: [SESSION-V20.md](SESSION-V20.md).
- Saving works with REC armed. REC is paused for the save and restored afterwards. INIT also works with REC armed.
- Projects in an older format are not loaded. A saved slot in the old format is listed as OLD FORMAT, and loading is refused. SAVE replaces it after the usual confirmation.
- The browser Editor can export saved projects and import a project into a chosen slot. It works on saved slots, not on the unsaved live session. It supports the FM-1 only. See [PROJECT-IMPORT.md](PROJECT-IMPORT.md).

### Interface

- The HD interface is the only interface on both products. The earlier standard interface is removed. The version badge reads `V1.0B` with the build number.
- HOME page 2 shows the same header as page 1, with the engine icon before the sound name.
- Large charts (filter, compressor time, large envelope) draw a single floor line.
- PATCH AMOUNT uses the same slider as LFO DEPTH: a bar from the centre mark to the signed amount.
- The KEY TRACK switch on HOME is drawn in the track colour when on.
- The pictures on the HOME cards stay still whether or not a track sounds. The header metronome and the step playhead still move.
- A screen-reader adapter sends page names, selected items, values and messages over USB MIDI. It needs a compatible reader. See [SCREEN-READER.md](SCREEN-READER.md).

### JamesOS-K (SMK-37 Pro)

Build 3083 changes the SMK-37 Pro button layout. The full table is in [SMK37-PORT.md](SMK37-PORT.md).

- KNOB BANK is HOME. On the sound list, KNOB BANK moves to the next category.
- PATCH is the sound and kit list (printed SEL). A 0.8 s hold of PATCH turns HOLD on or off.
- SCALE is the LFO button. CHORD is EDIT, with PATCH 1 and PATCH 2.
- NOTE REPEAT on the drum track opens RETRIGGER. On synth tracks it is the punch shift key.
- A 0.8 s hold of ARP turns the arpeggiator on or off. On the drum track, the ARP page shows `SWITCH TO T1 T2 T3`, because the drum track has no arpeggiator.
- FADER BANK opens the real mixer page for its bank: MIXER for the track bank, KIT LEVEL for the drum bank. The short mixer view appears only when a fader moves on a page that is not a mixer page.
- GLOBE cycles PAN, FILTER, HEAT, COMPRESSOR and COMP TIME. The mixers are not in that ring.
- Fader link also covers the drum bank. Fader 1 then sets kit lanes 1 and 2.

## What changes for existing users

### Projects from 1.0a

Projects saved by 1.0a (session v16 in envelope v4) are not loaded directly. 1.0b lists them as OLD FORMAT. This is a change of format, not a data wipe, but a confirmed save over such a slot replaces it.

What to do:

- Save any work you want to keep into a named slot before updating. The migration tools read the twelve named slots, not the separate working record.
- Keep an independent backup. Stock 1.0a cannot make a full backup of the project partition.
- After updating, select the old project in the Editor, download its original and keep the file. Use the Editor's upgrade action, read its notes and import the result into a slot you have chosen that is empty. Listen to it. Keep the original until you are happy.
- Conversion can approximate settings or refuse a project. It does not promise the same sound. Inverted modulation amounts from older formats cannot be stored in the current format and become zero modulation.

Projects saved by 1.0b cannot be opened by 1.0a or by other older firmware.

### Sound can differ

Mix and effect laws changed after 1.0a. Track and kit LEVEL follow a squared curve, PAN is constant power, envelope and LFO rate laws are exponential, the filters and the shared reverb have new engines, and the drive and delay were reworked. A project that loads can still sound different from the same project on 1.0a. See [SESSION-V20.md](SESSION-V20.md) for the format side of these changes.

## Known issues

These come from a host audit of the sequencer and clock code. None is a regression from 1.0a.

- External clock: when a master starts the clock and playback together, JamesOS can join 2 to 3 beats late.
- Tempo drift between tracks with different step rates, at some tempos. About 35 ms over 64 bars at 133 BPM. None was seen at 60, 120 and 240 BPM.
- Drum retrigger repeats can land up to 4.9 ms early.
- Simultaneous cold synth starts are staggered by 5.8 ms and 11.6 ms. This is by design.
- MODEL as an LFO target changes the engine in steps. Fast settings can click.
- To return to M-VAVE firmware, install the official V15 package. Returning through the M-VAVE Workbench is not supported. A return to stock firmware empties the JamesOS project slots, so export projects first.

## Memory budgets

Measured on build 3082. Build 3083 is expected to be within a few hundred bytes. Limits come from `tools/build.py` and `firmware/product.h`.

| Budget | FM-1 used | FM-1 limit | SMK-37 Pro used | SMK-37 Pro limit |
| --- | --- | --- | --- | --- |
| Application image | 497,072 B | 581,564 B | 509,228 B | 619,520 B |
| `.data` + `.bss` | 84,848 B | 98,304 B | 85,776 B | 98,304 B |
| Designated pool | 342,960 B | 344,064 B | 343,344 B | 344,064 B |
| RAM code | 31,328 B | 32,512 B | 32,076 B | 32,512 B |

The pool and RAM-code margins are small on both products (1,104 B and 1,184 B on the FM-1, 720 B and 436 B on the SMK-37 Pro). Final 3083 figures: FM-1 image 497,104 B of 581,564, data and BSS 84,848 of 98,304, pool 342,960 of 344,064, RAM code 31,328 of 32,512. SMK-37 Pro image 509,772 B of 619,520, data and BSS 85,776 of 98,304, pool 343,344 of 344,064, RAM code 32,076 of 32,512.

## Where the claims were checked

Behaviour statements were checked against the source in this tree or against a document in `docs/`. The main places are `firmware/src/instrument_ui.c` (effect labels and note tables, sequencer cue, arranger icons, SMK-37 Pro controls, CPU message), `firmware/src/pattern_song.h` (64 blocks, 1 to 16 bars), `firmware/src/instrument_poly_limits.h` (3 to 8 voices), `firmware/src/instrument_modulation.c` (filter sweep), `firmware/src/instrument_bank.h` and `assets/presets/bank.json` (128 sounds, synthesized kits), `firmware/plaits/upstream/plaits/wavetable_pack.h` (DX7 banks), `firmware/hal/fm1_flash_ids.h` (flash IDs), and the documents linked above. The KEY TRACK colour and the SMK-37 Pro drum ARP cue were checked in their own branch commits (`47a91c3`, `170a90b`).

Component notices are in `legal/THIRD-PARTY-NOTICES.md`. Build configuration and toolchain requirements are in the project README. A [changelog](CHANGELOG.md) lists changes by build range.
