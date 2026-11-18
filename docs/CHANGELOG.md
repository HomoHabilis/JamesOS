# Changelog: 1.0a (build 2108) to 1.0b (build 3083)

Terse and dated. Sources: `git log 21c4fcb..HEAD` (about 600 commits), the build scripts and candidate folders of 16 and 17 November 2026 and the release records. Times are local to the build machine.

Reading guide:

- Commit hashes in this file refer to the development repository. The public repository carries 1.0b as one commit, tagged `v1.0b`.
- Git history is grouped by day because most builds were not committed one by one. The 16 November source was committed in three integration commits and, after build 3077, in one commit that holds the whole working tree.
- **NOT RECONSTRUCTED** marks a range where the builds exist or are numbered but their individual contents could not be recovered from the records available.
- Items are firmware changes unless they say test, tool or documentation.
- Build 3083 is the release build, built from commit `c7c010b`. Its package hashes are in `docs/RELEASE-1.0b.md`.

## Summary table

| Builds | Date | Source state | Main content | Status of the record |
| --- | --- | --- | --- | --- |
| 2108 | 9 Nov | `21c4fcb` | 1.0a alpha, last 1.0a update. Session v16, envelope v4, 64 sounds. | Complete. See [RELEASE-1.0a.md](RELEASE-1.0a.md). |
| 2109 to 3005 | 9 to 15 Nov | `21c4fcb` to `c41376b` | The 11 to 15 November integration work, listed by day below. | **NOT RECONSTRUCTED** per build. Builds `2301` and `9877` have folders but their notes were not found. Daily commit lists only. |
| 3006 | 15 Nov, about 23:00 | `c41376b` | First 1.0b candidate folder. 128 sounds, synthesized 80S and 90S kits, project export and import. | Source commit known. |
| 3007 to 3019 | 15 Nov, to about 23:50 | between `c41376b` and `df6c292` | Candidates 3007 and 3015 to 3019 have build logs only. | **NOT RECONSTRUCTED.** |
| 3020 to 3021 | 16 Nov, 00:10 to 00:22 | 3021 source `df6c292` | Arranger, modulation, polyphony, tempo-synced effects and screen updates integrated. | Source commit known for 3021. 3020 package only. |
| 3022 to 3027 | 16 Nov, to about 02:40 | `06e54f6` plus uncommitted work | 3023: installer-tested base (global LFO routed to master effects, `06e54f6`). 3025: REC pause for SAVE. 3026: global LFO picture follows the oscillator. 3027: mixer meter decay fix, frozen, never installed. | Partial. 3022 and 3024 not found. |
| 3028 | 16 Nov, about 07:00 | 3023 base plus uncommitted | First HALL tail fix after changing from ROOM. Installed and confirmed by the owner. | Complete. |
| 3029 | 16 Nov, about 08:10 | same | INIT works with REC armed and clears tails. MODEL as an LFO target (stepped, Plaits synth tracks). Installed. | Complete. |
| 3030 | 16 Nov, about 11:00 | same | Automatic CPU monitor notice, COMP TIME graph, OCT- + GLO shortcut to TEMPO, PRESETS-only engine change. Installed and confirmed by the owner. | Complete. |
| 3031 to 3036 | 16 Nov, 11:40 to 12:30 | same | Polyphony voice-limit measurements and limits, polyphony UI cue. 3034 and 3036 installed. | Partial. Per-build changes not separated. |
| 3037 to 3048 | 16 Nov, 12:50 to 14:50 | same | 3042: HOME dot flicker found (cards redrew over the dots). 3044: session v18, per-track PATCH 1 and PATCH 2. 3045: HOME hint and PLA/STP colours. 3048: polyphony ON/OFF cue. 3048 was installed from 3047. | Partial. 3046 not found. |
| 3049 to 3068 | 16 Nov, 14:50 to 18:20 | same | Session v19 (voice modes, new modulation depth law) is in place by 3067 (Editor allow-list). Per-build changes not separated. | **NOT RECONSTRUCTED.** |
| 3069 to 3070 | 16 Nov, about 18:20 | same | Session v19. 3069 was installed from 1.0a build 2108 as the direct-upgrade test. | Complete for 3069. |
| 3071 to 3074 | 16 Nov, 19:00 to 20:30 | same | Session v20 (LPG timed gate length). Per-build changes not separated. | **NOT RECONSTRUCTED.** |
| 3075 | 16 Nov, about 20:35 | same | Badge position. Sequencer hint change. | From the handoff note. |
| 3076 | 16 Nov, about 20:45 | same | HOME dots steady, combined OCT cue, arranger rail. Confirmed by the owner. | From the handoff note. |
| 3077 | 16 Nov, about 20:56 | `e8ba317` (whole working tree) | Fresh OCT- hold on GLOBAL FILTER with knob 1 snaps between low-pass, OPEN and high-pass. Installed on one FM-1. | Complete. |
| 3078 | 16 Nov, about 21:55 | `1239ce4` | FX knob 4 pictures. DELAY TONE shares the drive TONE picture. Unreachable second reverb page removed. | Complete. |
| 3079 | 16 Nov, about 22:40 | `60cdf71` | DX7 banks read from flash (12,288 B of data memory back on both products). Test build. | Complete. |
| 3080 | 16 Nov, about 23:35 | `c84371d` | Sequencer cue alternates SHIFT NOTES and STEP BANK. HOME page 2 header. Single floor on large charts. PATCH AMOUNT slider. FX and MOD RATE ring. KEY TRACK on HOME shows a switch. Arranger knob icons show state. JamesOS-K new button layout. | Complete. |
| 3081 | 16 Nov, about 23:41 | `567b79e` | JamesOS-K fader link covers the drum bank. Owner checked on hardware: SMK-37 Pro button layout, DX7-style sounds, arranger loop-off icon. | Complete. |
| 3082 | 17 Nov, about 00:06 | `5133a60` | JamesOS-K GLOBAL starts on PAN and the mixer is not in the GLOBE ring. Budgets recorded for this build. | Complete. |
| 3083 | 17 Nov | `c7c010b` | Final candidate. Adds the KEY TRACK switch in the track colour (`47a91c3`) and the JamesOS-K drum ARP cue `SWITCH TO T1 T2 T3` (`170a90b`), regenerated test baselines and the release documents. | Built and installed on both products. |

## 9 to 15 November: daily detail (builds 2109 to 3006, not mapped to builds)

Counts are commits in `21c4fcb..c41376b`.

### 11 November (56 commits)

- HD interface: parameter graphics batches A to D (track sound, modulation, blend, send, drive, echo, reverb tail, loop length, sequencer tiles, steps, comp curve, MIDI ring, clock source), polish, smooth FX tiles, globe, Plaits icons.
- Drawing cost cut to under twice the old standard interface.
- Working tree as of this date: level smoothing, Plaits defaults, icons.

### 12 November (158 commits)

- One source tree for two products: `firmware/product.h`, `tools/build.py --target fm1|smk37`, JamesOS-K panel, loader, flash map and package tools.
- Polyphony on the FM-1 and SMK-37 Pro: scheduler, delay-line loan, UI, size work. FM-1 device candidates include it by default.
- The HD interface became the default. The standard interface was removed on 12 and 13 November.
- Four-pole ladder low-pass and geometric high-pass filter.
- Reverb: held-note balance, SPACE soft knee, longer ROOM and HALL ceiling.
- Delay: linear send law, feedback curve to 0.97, line saturator, feedback high-pass.
- Master peak limiter with soft knee.
- Sequencer fixes: step recording, held-step velocity, PLAY as transport while a step is held, OCT+ stops at the last used bank, hold a step and press a key to set its pitch, black keys inert on the grid.

### 13 November (102 commits)

- Session v17: table-driven codec, v16 converter, generated byte map. Per-setting runtime homes.
- DRIVE: parallel level-matched law, fuzz gain, asymmetric distortion, open tone.
- Envelope times: exponential law. LFO rate: exponential law. MOD RATE: exponential 0.05 to 10 Hz.
- LFO DEPTHS act as true attenuverters. Modulation summed once in Q8. Pitch depth law.
- Filter: direct geometric low-pass table, resonance rising evenly in dB, high-pass cutoff and resonance law. Global high-pass and low-pass follow the track filters.
- Delay: TIME has 14 note values (16 by 3083), TAPE wow and flutter, darker repeats, WIDTH engine, half-band wet path.
- Reverb: the shared reverb engine was replaced by the `reverb2` FDN.
- LEVEL follows a squared curve. PAN is constant power.
- GLOBAL / PAN: OCT- with a knob snaps 100% L, CENTER, 100% R. SNAP on the drum track's HOME.
- Screen reader adapter, protocol v1.
- Version text reads V1.0B on both products. HD font changed to Chakra Petch SemiBold.
- Audio overrun and load counters kept in distribution builds.

### 14 November (179 commits)

- Session v17 on the device: capture, restore and validation. A project in the old format boots from defaults and shows OLD PROJECT NOT LOADED.
- Pattern arranger: song engine and storage, PATTERNS and ARRANGE views, ruler, loops, hints, overview playhead.
- Project envelope v5 is read, with an empty arrangement.
- UI draw load: sliced and chunked drawing, redraw only what changed, 24 frames per second, CPU monitor refresh every 200 ms, HOME pictures hold still.
- Played notes sound at arrival plus 6 ms, independent of the screen.
- Flash: the part is never left in execute-in-place mode while busy. Late and hard-fault codes, part reset and counters.
- A contained CPU1 fault no longer silences the unit or locks SAVE.
- Plaits code size work: unreachable branches removed, kick and 808 loops in RAM code.
- JamesOS-K: fader link, first version.
- Tests and tools: parallel runs, optimisation classes, quick mode.

### 15 November (33 commits)

- 128 starting sounds (`97e8746`), descriptive names (`c41376b`), five sounds rebalanced.
- 80S and 90S kits are synthesized from Plaits percussion. Old sample-kit projects migrate with a notice.
- Project export and import: validated snapshot export, guarded import with SHA-256 check, import tooling and tests.
- Compact saved project listing. Song position above the sequencer strip.
- Project deletion: prototype and host tests only, listed under `docs/PROJECT-DELETE-EXPERIMENT.md`. Not described as a feature in the release notes.
- Sound list accessibility expectations updated.

## Detail for 16 and 17 November

### 3078 (`1239ce4`)

- FX knob 4 has its own picture on each effect page: BIAS (drive), TONE, FEEDBACK (modulation), PRE DELAY (reverb).
- DELAY TONE uses the drive TONE picture. The separate delay tone drawing is removed.
- The unreachable FX reverb-time page and the pictures of the retired Spring and Ethereal reverb types were removed. The player-visible FX screens are SENDS, DRIVE, DELAY, MODUL8N and REVERB.

### 3079 (`60cdf71`)

- The three DX7 SysEx banks are read from flash as constant data. 12,288 B of `.bss` is free on both products, for about 3 KB more flash. The SMK-37 Pro had 240 B of data memory left before this change.

### 3080 (`c84371d`)

- Sequencer bottom cue alternates SHIFT NOTES and STEP BANK every 3 s on patterns longer than 16 steps. The screen reader speaks both.
- HOME page 2 keeps page 1's header, with the engine icon before the sound name.
- Large charts have one floor line.
- PATCH AMOUNT uses the LFO DEPTH slider.
- FX and MOD RATE use the LFO rate ring.
- HOME KEY TRACK shows a switch.
- Arranger knob icons: BARS (1 to 4 slots lit, plus above 4), LOOP MODE (on and off), PERFORM (pattern or arrange).
- JamesOS-K: KNOB BANK is HOME, PATCH is SEL with a 0.8 s HOLD, SCALE is LFO, CHORD is EDIT, NOTE REPEAT on the drum track toggles RETRIGGER, FADER BANK opens the real mixer page for its bank.

### 3081 (`567b79e`)

- JamesOS-K fader link covers the drum bank: fader 1 sets kit lanes 1 and 2, fader 2 is ignored. Switching the link on copies lane 1's level to lane 2.

### 3082 (`5133a60`)

- JamesOS-K: GLOBAL starts on PAN. The mixer is not in the GLOBE ring (PAN, FILTER, HEAT, COMPRESSOR, COMP TIME). FADER BANK opens the mixers.
- Tests only: fixes found by the full pass on the candidate.

### 3083 (`c7c010b`)

- KEY TRACK switch takes the track colour when on (`47a91c3`, branch `keytrack`, 17 Nov 00:14).
- JamesOS-K drum track ARP page shows `SWITCH TO T1 T2 T3`. RETRIGGER only through NOTE REPEAT (`170a90b`, branch `smk-arp`, 17 Nov 00:20).
- Test baselines regenerated from a full pass on all four configurations. Release documents added.
- Package hashes and budgets: `docs/RELEASE-1.0b.md`.

## Non-firmware changes in the same period (not part of any package)

- Test suite: parallel runs, targeted runs (`--changed`), unity builds for host tests, optimisation classes, shorter fuzz runs. 16 November.
- Documentation: README for candidate 3077, SMK-37 Pro notes for 3078, historical banners on the qualification and HOME deep-menu notes. 16 November.
- Credit to the upstream origin added to four platform files (`ec0059f`).
