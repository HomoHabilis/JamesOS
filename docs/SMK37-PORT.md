# SMK-37 Pro port (JamesOS-K)

Updated 17 November 2026 against the source tree for candidate build 3083. Statements below were checked against the source unless the text says otherwise. Nothing here was measured or run on hardware by the author of this update.

> **Status:** `--target smk37` builds the JamesOS-K package from the same source tree as JamesOS for the FM-1. Build 3081 (`SMK-37 Pro_9653081`) ran on an SMK-37 Pro on 17 November 2026 and the owner checked its button layout. Candidate 3083 adds the GLOBE ring without the mixer, the ARP cue page on the drum track and the fader link in the drum bank; those are host-tested and not yet checked on hardware.

## One tree, one product switch

`firmware/product.h` selects the product with one compiler define, `-DBR_PRODUCT=BR_PRODUCT_ID_FM1` (the default) or `-DBR_PRODUCT=BR_PRODUCT_ID_SMK37`. `tools/build.py --target fm1|smk37` passes it. Hardware capabilities follow from the product and cannot be set on the command line:

| Flag | FM-1 | SMK-37 Pro |
| --- | --- | --- |
| `BR_CAP_PADS` (16 RGB pads) | 0 | 1 |
| `BR_CAP_FADERS` (4 faders) | 0 | 1 |
| `BR_CAP_WHEELS` (pitch bend, mod wheel) | 0 | 1 |
| `BR_CAP_VELOCITY` (velocity keys) | 0 | 1 |
| `BR_CAP_DIN_MIDI` (DIN MIDI) | 1 | 0 |
| `BR_CAP_SUPPLY396` (396 MHz supply step at boot) | 1 | 0 |
| `BR_CAP_READER` (screen reader, see [SCREEN-READER.md](SCREEN-READER.md)) | 1 | 1 |

`BR_CAP_POLY` (stopped-transport polyphony) is the one capability a build may choose. `tools/build.py` turns it on for device candidates of both products; `--no-polyphony` turns it off. Builds that are not device candidates never include it. See [SMK37-POLYPHONY.md](SMK37-POLYPHONY.md). The comment in `firmware/product.h` that says FM-1 polyphony is off by default describes the header default, not what `tools/build.py` passes for FM-1 device candidates.

HD is the only interface on both products. The standard (pixel) interface was removed; `tools/build.py` accepts `--ui-hd` and rejects `--ui-standard`. The SMK-37 Pro HD screens draw the step grid and pattern slots as two rows of eight, matching the 16 pads, and the build refuses to compile without that layout.

## Firmware identity

Paired candidates use the same four-digit build number. The FM-1 package is `FM-1_967NNNN` and the SMK-37 Pro package is `SMK-37 Pro_965NNNN`. Build 3083 gives `FM-1_9673083` and `SMK-37 Pro_9653083`. Build with:

```sh
python3 tools/build.py --target smk37 --pattern-device-candidate --app-opt Oz --build-number NNNN --diagnostics distribution \
  --define RV2_LINE_STRUCT=1 --define BR_MODEL_MOD_EXPERIMENT=1 --define FM1_PATTERN_DELETE_EXPERIMENT=1 \
  --define FM1_PATTERN_DELETE_PROTOCOL_EXPERIMENT=1 --define BR_ADAPTIVE_POLY=1
```

This is the SMK-37 Pro command from the top-level README. Use a new number for every new paired candidate and never reuse a full identity for different firmware. Identity ownership of the 965 and 967 prefixes in the Firmware Hub was not checked in this update; check each full identity before a public release.

The app name shown to a screen reader is `JAMESOS-K`, the USB product string is `SMK-37` and the settings magic is `JOSK` (`"JOSK"` in `firmware/product.h`). The loader identity in `product.h` is `ota-SMK-37 Pro_9260001`.

JamesOS-K began from the JamesOS 1.0a build 2108 source (commit `21c4fcb8ebc10065abea90cf0926549f6e0d3062`, which exists in this repository). The board layer draws on M. Geisler's (genkigenki) [Felucca SMK-37 port](https://github.com/hugelton/Felucca/pull/237); see [third-party notices](legal/THIRD-PARTY-NOTICES.md).

Back up a unit's flash before the first installation. Such backups are private and are not part of this repository.

## What differs on the SMK-37 Pro

The display is the same 240 by 240 LCD as the FM-1 and the same HD interface runs on it. The differences are in the panel.

| Area | SMK-37 Pro | Source |
| --- | --- | --- |
| Keys | A 37-key velocity keybed. Keybed keys 5 to 31 are the 27 note keys the FM-1 has. The five lowest and five highest keys are outer keys that also play notes (octave follows OCT-/+). | `hal/smk37/smk37_input.h`, `src/smk37/smk37_panel.c` |
| Velocity | Measured from two contacts per key. Scanning is once per 1.6 ms, so very fast strikes quantise coarsely (a code comment says so). | `smk37_input.h` |
| Pads | 16 RGB pads. On the sequencer grid they edit steps. On the pattern and arranger page the first 11 pads select patterns. On pages where the keys play notes (not the sound browser), the pads trigger four punch effects, repeated across each row: LOOP, ECHO, FILTER, TREM. Only a pad struck in this mode counts. | `smk37_panel.c`, `smk37_punch_pads.h` |
| Faders | 4 faders. See below. | `smk37_fader_logic.h` |
| Encoders | 8 encoders. The panel labels in the HD screens are K1 to K4 for the four knobs and K5 for SELECT. K8 acts as the master volume: a virtual pot from 0 to 1023 that moves 24 per detent and starts at 724. The code comment says it stands in for the FM-1's MASTER pot. Which encoder carries ALGORITHM and PRESETS was not checked. | `instrument_ui.c`, `smk37_input.h` |
| Buttons | The logical JamesOS buttons map to printed labels: PATCH is SEL (the sound and kit browser), SCALE is LFO, KNOB BANK is HOME, CHORD is EDIT, PARA is ENV, GLOBE is GLO, BT is SAVE. FX, ARP, SEQ, SEQ PLAY and SEQ REC keep their names. GLOBE cycles the five GLOBAL screens: PAN, FILTER, HEAT, COMPRESSOR, COMP TIME; the mixers are on FADER BANK, not in the GLOBE ring. HOLD is a 0.8 s press of PATCH (SEL), and KNOB BANK (HOME) on the SOUNDS list steps to the next category. Extra buttons: left and right arrows, FADER BANK and NOTE REPEAT. The printed names come from a mapping table in the input code whose comment says it was confirmed on one unit. Changing which printed button is which is a change to that table. | `smk37_input.h` |
| Arrows | The left and right arrows select the previous or next track, including from the patch and kit pages. They are ignored while a step is held for editing. | `instrument_ui.c` (`br_smk_track_turn`) |
| KNOB BANK | It is HOME. HOME cycles its screens when pressed again, and OCT- with KNOB BANK opens the HOME deep menu, as OCT- with HOME does on the FM-1. On the sound list of a synth track a tap advances to the next category and shows its name; any other page button leaves the list. | `instrument_ui.c` |
| PATCH | It is SEL. A tap, acted on at release, opens the SOUNDS list on a synth track or DRUM KITS on the drum track; a second tap loads the highlighted entry. Holding it for 0.8 seconds toggles HOLD, with the cue "HOLD 0.8S: HOLD ON" or "HOLD OFF" after 0.4 seconds. The LED blinks while HOLD is on. | `instrument_ui.c`, `dinghy.c` |
| SCALE | It is the LFO button: LFO 1, LFO 2 and the global LFO. | `instrument_ui.c` |
| CHORD | It is EDIT: PATCH 1 and PATCH 2. It does not include KIT LEVEL; FADER BANK opens that page. | `instrument_ui.c` |
| ARP | On a synth track it opens the ARP page. The drum track has no arpeggiator and ARP never shows RETRIGGER there: the page is titled ARP and shows one centred line, SWITCH TO and the badges T1, T2 and T3. Knobs do nothing on it, and the ARP LED is lit. A 0.8 s hold of ARP on the drum track just opens that page. A track arrow onto the drum track while the ARP page is open gives the same cue. The screen reader says ARP. SWITCH TO T1 T2 T3. | `instrument_ui.c` (`br_arp_cue_page`, `br_draw_arp_cue`) |
| NOTE REPEAT | On the drum track a tap opens the RETRIGGER page and a second tap returns to the page it came from. Any page button, including ARP, or a track arrow also leaves it. Its LED is lit only while RETRIGGER shows (the LED has not been checked on a unit). Pressed on the ARP cue page of the drum track it opens RETRIGGER. Nothing happens while a step is held. On synth tracks, while held on a key-playing page, it is the punch shift key and opens the PUNCH page. | `smk37_panel.c`, `smk37_panel_logic.h` |
| Pedal | Sustains local keybed voices. The pedal state is read from the ADC with a fixed threshold. Pedal, pitch bend and mod wheel are also sent as MIDI when the track's MIDI channel is on. | `smk37_panel.c`, `instrument_ui.c` |
| Wheels | Pitch bend moves the pitch of the selected synth track by up to two semitones. The mod wheel raises TIMBRE toward its maximum. The firmware learns the pitch wheel centre after the ADC has started and keeps a dead band. | `smk37_panel.c`, `instrument_audio.c` |
| Button LEDs | 19 LED positions. The FADER BANK LED is lit in the drum bank. | `smk37_input.h`, `smk37_panel.c` |
| Sequencer hint | The sequencer shows "KEYS SET PITCH   PADS SET STEPS" under the status chips. | `instrument_ui.c` |
| DIN MIDI | None. USB MIDI only. The GLOBAL MIDI screens are titled USB MIDI 1 and USB MIDI 2 instead of MIDI 1 and MIDI 2. | `product.h`, `instrument_ui.c` |
| Battery | Four levels from the ADC, not the FM-1 battery model. | `platform.c` |
| Boot | No 396 MHz supply step. | `platform.c`, `product.h` |

### Faders and FADER BANK

Bank 0 sets the levels of tracks T1, T2, T3 and the drum kit (DK). Bank 1 sets the four drum-lane levels. A FADER BANK tap switches banks and opens the real mixer page of the bank now selected: bank 0 opens MIXER and bank 1 opens KIT LEVEL. The page stays until you press a page button, and a second tap switches to the other bank and its page. KIT LEVEL has the lane mute and solo on OCT- and OCT+. Moving a fader on any other page shows the bank's mixer for 1.6 seconds, restarting while the fader keeps moving. On MIXER or KIT LEVEL the move only updates the page. Fader readings are 0 to 127 with a dead band of one count, and both ends of travel are reachable.

Faders use pickup. A fader changes a level only after it has crossed the stored value, so switching banks or loading a project does not make the level jump. While a step is held for editing, a bank switch opens no page and no brief mixer, and only shows "TRACK FADERS" or "DRUM FADERS".

### Fader link

With the link on, fader 1 sets the first two levels of the selected bank: T1 and T2 in the track bank, kit lanes 1 and 2 in the drum bank. Fader 2 is ignored entirely in both banks (no value, no mixer peek, no pickup). The mixer cue then shows "T1+T2", and the KIT LEVEL cue shows "D1+D2". Switching the link on copies T1's level to T2 and kit lane 1's level to lane 2 at once.

Toggle it by holding NOTE REPEAT and SCALE together for 0.8 seconds. The message line shows FADER LINK ON or FADER LINK OFF. Whichever button goes down first acts as it normally would; the second press is held back. If you let go of either before 0.8 seconds, the held-back press is delivered then. At 0.8 seconds the toggle happens, the held-back press is dropped and a punch overlay or RETRIGGER page opened by NOTE REPEAT is closed again. SCALE is the LFO button here, so the chord uses the same two printed buttons as before.

The setting is the BOOL field `smk_fader_link` in the session (see [SESSION-V20.md](SESSION-V20.md)). It is saved with the project. The FM-1 stores and restores it and never reads it. Host tests: `tests/run_smk_fader_link.py`, `tests/run_smk_faders.py`.

## Memory budgets

Limits come from `tools/build.py` and `firmware/product.h`. Measured values below are from candidate 3082's build log (distribution configuration; 3083 differs by a few hundred bytes of image) and were not remeasured here, because this update did no builds.

| Budget | Used | Limit | Free |
| --- | --- | --- | --- |
| Application image | 509,228 B | 619,520 B (app slot `0x97400`) | 110,292 B |
| `.data` + `.bss` | 85,776 B | 98,304 B | 12,528 B |
| Designated pool | 343,344 B | 344,064 B | 720 B |
| RAM code | 32,076 B | 32,512 B | 436 B |

The pool and RAM-code margins are small (data memory was recovered at 3079 by reading the DX7 banks from flash). Do not add SMK state casually. Recheck all four after any material change. For comparison the FM-1 application slot is 581,564 B (`0x8DFBC`). A comment in `tools/build.py` that says the SMK-37 Pro has "36 KB free" is out of date against the figures above.

## Polyphony

Polyphony is on by default for SMK-37 Pro device candidates (`BR_CAP_POLY` defaults to the product). With `BR_ADAPTIVE_POLY=1`, which the build command above sets, up to 8 voices are available and the number of voices admitted for each Plaits model is limited by a table in `firmware/src/instrument_poly_limits.h` (3 to 8; 3 while a MODEL modulation sweep is active). Without `BR_ADAPTIVE_POLY` the count is 6. On HOME, hold OCT- and press PLAY to switch the selected track's polyphony. See [SMK37-POLYPHONY.md](SMK37-POLYPHONY.md), whose voice-count and build-status wording is older than this page. Punch echo is refused in poly while a model has borrowed the delay memory (message ECHO UNAVAILABLE IN POLY).

## Flash layout

All ranges have exclusive ends (`firmware/hal/smk37/smk37_flash_map.h`). The loader may write only the app window and update staging. The app may write only project storage, settings, the owner tag and staging.

| Physical range | Use |
| --- | --- |
| `0x00000-0x04000` | Boot header and loader, preserved |
| `0x04000-0x9D000` | Application window. The linked image limit is 619,520 B (see budgets) |
| `0x9D000-0xAB000`, `0xAC000-0xAF000`, `0xB0000-0xB9000` | 26 project sectors: 13 slots (12 named plus the working record) of two sectors each |
| `0xAB000-0xAC000`, `0xAF000-0xB0000` | Existing data on the developer's unit, preserved |
| `0xB9000-0xBA000` | JamesOS-K settings |
| `0xBA000-0xBB000` | JamesOS-K store owner tag |
| `0xBB000-0xC0000` | Update staging (20 KiB) |
| `0xC0000-0x100000` | Bluetooth data, factory calibration and other stock data, preserved (never written by the app) |

Project offsets are logical and are translated around the two occupied sectors in the flash layer. The firmware claims the project and settings sectors only when every sector in them is erased or the owner tag `JamesOS-K store 1` already exists. A different unit with data in those sectors keeps its data and reports storage unavailable. A general storage allocator has not been written. The updater accepts only a loader that carries both the Felucca loader mark and the JamesOS-K SMK-37 mark.

Projects use session format v20 inside project envelope v6, the same as the FM-1. The browser Editor transfers projects for the FM-1 only; SMK-37 Pro transfer has not been qualified.

## What is unverified on hardware

- Build 3081 ran on an SMK-37 Pro on 17 November 2026: the owner checked the button layout and DX7-style sounds. Later builds, and everything else in this list, were not checked.
- All 37 keys and their velocity, the 16 pads and their LEDs, fader pickup in both banks, the fader link chord, the wheels, the pedal, K8 master volume, every screen, project save and reload, and the sequencer during playback.
- Audio and sustained workloads, including polyphony at the per-model voice limits. Polyphony playback, release and transport were exercised on an SMK-37 in an earlier build; worst-case timing across every model and effect was not.
- A second update, interrupted update recovery and return to stock before the update path is treated as qualified.
- The SMK host sequencer audit (`BR_HOST_TARGET=smk37 python3 tests/run_seq_behaviour.py --config clock`) was reported in the 16 November handoff as 93 passes, 2 failures and 10 documented defects at build 3075, the same as an untouched 3075 baseline. It was not re-run for this update, and the findings are host results, not observed hardware failures.
- An older note said the SMK-37 Pro development diagnostics configuration needed a RAM-size reduction before it could be built. Not rechecked here; the distribution configuration is the one with recorded budgets.
- The unit-specific mapping in the input code (button names, LED order, encoder order) was confirmed on one unit only, per its comments.
