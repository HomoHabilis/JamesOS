# Session format v18 byte map

Generated from `firmware/src/instrument_session_v17.h` by `python3 tests/run_session_v17.py --write-doc`;
the host test fails if this file and the table differ. Little-endian. Do not edit by hand.

Total **3584 B** (limit 3584 B, 0 B headroom). 529 settings (147 table rows without spares), 8 spare bytes.

Version 18 packs two track patches into the upper bits of each track's route-enable bytes; see
`SESSION-V18-TRACK-PATCHES.md` for the bit layout and v17 migration. The table below shows the
low-bit legacy fields; it does not describe the packed upper bits.

Version 19 changes MOD DEPTHS to a neutral-centred 0–200% multiplier and uses the first six KIT
spare bytes for PORTAMENTO and voice modes (two bytes per synth track). The first byte is 0–100;
the second holds GLIDE MODE in bits 0–1 and AMP MODE in bit 2. Older records default these
controls to zero; positive v17/v18 modulation multipliers are converted, while inverted values
cannot be represented by the new law and become zero modulation. Older firmware rejects v19.

Rules: magic/version/lengths are structural (record rejected). A reader skips unknown sections, bytes after the
last section and the tail of a section longer than it knows; fields beyond a shorter section take their
defaults; TRACKS is four equal track blocks (stride = len / 4). Out-of-range values are clamped and counted.
Spare bytes are written 0 and ignored on read. No checksum: the project store header and the FMP1 envelope
already carry CRCs over these bytes.

Firmware: instrument_session.c binds every table row to its runtime home (br_v17_binds) and walks the
table for capture, restore and validation; a record that fails validation (structure, or a preset that does
not suit its track) changes nothing. Projects carry it inside the FMP1 envelope version 6 (24-byte header;
bytes 20..21 hold the session length; the 80-byte song arrangement follows the session). Envelope version 5
(no arrangement) loads with an empty arrangement and saves back as 6. Version 4 and older (session v16,
magic 0x42524431) are never read: a working project in the old format boots from defaults with the store usable ("OLD PROJECT NOT
LOADED"), a named slot lists as OLD FORMAT, refuses LOAD and is replaced by SAVE after the usual confirm.
Host conversion of old captures: tools/session_v16_to_v17.py.

## Header (0..11)

| Off | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | "JSES" (0x5345534A) |
| 4 | 2 | version | 19 |
| 6 | 1 | header_len | 12 |
| 7 | 1 | section_count | 5 |
| 8 | 2 | total_len | 3584 |
| 10 | 2 | flags | 0 |

## Sections

| Id | Name | Header at | Payload at | Payload len | Spare bytes |
|---|---|---|---|---|---|
| 1 | GLOBAL | 12 | 16 | 20 | 0 |
| 2 | MASTER | 36 | 40 | 32 | 0 |
| 3 | TRACKS | 72 | 76 | 448 (4 x 112) | 0 (4 per track) |
| 4 | KIT | 524 | 528 | 44 | 8 |
| 5 | PATTERN | 572 | 576 | 3008 | 0 |

## Section 1 GLOBAL

| +Off | Abs | Size | Type | Field | Range | Min | Max | Default |
|---|---|---|---|---|---|---|---|---|
| 0 | 16 | 1 | u8 | bpm | BPM | 60 | 240 | 120 |
| 1 | 17 | 1 | u8 | swing | SWING | 0 | 77 | 0 |
| 2 | 18 | 1 | u8 | selected_track | TRACK | 0 | 3 | 0 |
| 3 | 19 | 1 | i8 | octave | OCTAVE | -2 | 2 | 0 |
| 4 | 20 | 1 | u8 | arp_on | BOOL | 0 | 1 | 0 |
| 5 | 21 | 1 | u8 | poly_on | BOOL | 0 | 1 | 1 |
| 6 | 22 | 1 | u8 | monitor | BOOL | 0 | 1 | 0 |
| 7 | 23 | 1 | u8 | midi_clock_in | BOOL | 0 | 1 | 0 |
| 8 | 24 | 1 | u8 | midi_ch_t1 | MIDI_CH | 0 | 16 | 1 |
| 9 | 25 | 1 | u8 | midi_ch_t2 | MIDI_CH | 0 | 16 | 2 |
| 10 | 26 | 1 | u8 | midi_ch_t3 | MIDI_CH | 0 | 16 | 3 |
| 11 | 27 | 1 | u8 | midi_ch_d1 | MIDI_CH | 0 | 16 | 4 |
| 12 | 28 | 1 | u8 | midi_ch_d2 | MIDI_CH | 0 | 16 | 5 |
| 13 | 29 | 1 | u8 | midi_ch_d3 | MIDI_CH | 0 | 16 | 6 |
| 14 | 30 | 1 | u8 | midi_ch_d4 | MIDI_CH | 0 | 16 | 7 |
| 15 | 31 | 1 | u8 | smk_fader_link | BOOL | 0 | 1 | 0 |
| 16 | 32 | 1 | u8 | global_lfo_wave | WAVE | 0 | 4 | 0 |
| 17 | 33 | 1 | u8 | global_lfo_rate | U7 | 0 | 127 | 102 |
| 18 | 34 | 1 | u8 | global_lfo_depth | U7 | 0 | 127 | 0 |
| 19 | 35 | 1 | u8 | global_lfo_target | GLOBAL_TARGET | 0 | 75 | 0 |

`smk_fader_link` (SMK-37 Pro "fader link", NOTE REPEAT + SCALE held 0.8 s): 1 = in the track-level fader
bank fader 1 sets T1 and T2 and fader 2 is ignored. The FM-1 saves and restores it and never acts on it.

## Section 2 MASTER

| +Off | Abs | Size | Type | Field | Range | Min | Max | Default |
|---|---|---|---|---|---|---|---|---|
| 0 | 40 | 1 | u8 | filter | U7 | 0 | 127 | 64 |
| 1 | 41 | 1 | u8 | resonance | U7 | 0 | 127 | 0 |
| 2 | 42 | 1 | i8 | env_depth | S7 | -127 | 127 | 0 |
| 3 | 43 | 1 | u8 | smooth | SMOOTH | 0 | 100 | 0 |
| 4 | 44 | 1 | u8 | heat_type | HEAT | 0 | 2 | 0 |
| 5 | 45 | 1 | u8 | heat_amount | U7 | 0 | 127 | 0 |
| 6 | 46 | 1 | u8 | comp_source | COMP_SRC | 0 | 8 | 0 |
| 7 | 47 | 1 | u8 | comp_threshold | COMP_THR | 0 | 48 | 18 |
| 8 | 48 | 1 | u8 | comp_ratio | COMP_RATIO | 0 | 7 | 3 |
| 9 | 49 | 1 | u8 | comp_makeup | COMP_MAKEUP | 0 | 24 | 0 |
| 10 | 50 | 1 | u8 | comp_attack | U7 | 0 | 127 | 12 |
| 11 | 51 | 1 | u8 | comp_release | U7 | 0 | 127 | 40 |
| 12 | 52 | 1 | u8 | comp_hold | U7 | 0 | 127 | 0 |
| 13 | 53 | 1 | u8 | comp_mix | U7 | 0 | 127 | 127 |
| 14 | 54 | 1 | u8 | leveler_on | BOOL | 0 | 1 | 0 |
| 15 | 55 | 1 | u8 | reverb_type | REVERB_TYPE | 0 | 2 | 0 |
| 16 | 56 | 1 | u8 | reverb_size | U7 | 0 | 127 | 0 |
| 17 | 57 | 1 | u8 | reverb_decay | U7 | 0 | 127 | 64 |
| 18 | 58 | 1 | u8 | reverb_predelay | U7 | 0 | 127 | 32 |
| 19 | 59 | 1 | u8 | reverb_tone | U7 | 0 | 127 | 64 |
| 20 | 60 | 1 | u8 | reverb_mod_depth | U7 | 0 | 127 | 64 |
| 21 | 61 | 1 | u8 | reverb_low_cut | U7 | 0 | 127 | 0 |
| 22 | 62 | 1 | u8 | reverb_width | U7 | 0 | 127 | 127 |
| 23 | 63 | 1 | u8 | reverb_manual | MANUAL5 | 0 | 63 | 0 |
| 24 | 64 | 2 | u8 | global_route_source[2] | SOURCE | 0 | 17 | 0 |
| 26 | 66 | 2 | u8 | global_route_target[2] | TARGET | 0 | 27 | 0 |
| 28 | 68 | 2 | i8 | global_route_amount[2] | S7 | -127 | 127 | 0 |
| 30 | 70 | 2 | u8 | global_route_enabled[2] | BOOL | 0 | 1 | 0 |

Reverb "derive": the reverb derives each optional control (pre-delay, tone, mod depth, low cut, width) from
TYPE/SIZE/DECAY unless its bit in `reverb_manual` is set (bit 0 predelay, 1 tone, 2 mod_depth, 3 low_cut,
4 width). Bit 5 selects tempo-synced pre-delay; its stored value selects a note division. A clear bit means derive
whatever the stored 0..127 value is; the value is kept so switching the control
to manual later starts from it. Default: all derive. `leveler_on` is saved and restored but not yet acted on by
the engine (the leveler is behind the compile switch BR_MASTER_LEVELER).

## Section 3 TRACKS

Track t block at absolute 76 + 112*t. Defaults are synth-track values; kit track (t = 3) overrides: preset = 48, osc_mix = 64, amp_release = 64, seq_def_length = 0.

| +Off | Abs | Size | Type | Field | Range | Min | Max | Default |
|---|---|---|---|---|---|---|---|---|
| 0 | 76 | 1 | u8 | preset | PRESET | 48 | 76 | 52 |
| 1 | 77 | 1 | u8 | timbre | U7 | 0 | 127 | 64 |
| 2 | 78 | 1 | u8 | morph | U7 | 0 | 127 | 64 |
| 3 | 79 | 1 | u8 | harmonics | U7 | 0 | 127 | 64 |
| 4 | 80 | 1 | u8 | osc_mix | U7 | 0 | 127 | 0 |
| 5 | 81 | 1 | i8 | tune | TUNE | -24 | 24 | 0 |
| 6 | 82 | 1 | u8 | level | U7 | 0 | 127 | 127 |
| 7 | 83 | 1 | u8 | pan | U7 | 0 | 127 | 64 |
| 8 | 84 | 1 | u8 | mute | BOOL | 0 | 1 | 0 |
| 9 | 85 | 1 | u8 | amp_attack | U7 | 0 | 127 | 0 |
| 10 | 86 | 1 | u8 | amp_decay | U7 | 0 | 127 | 64 |
| 11 | 87 | 1 | u8 | amp_sustain | U7 | 0 | 127 | 127 |
| 12 | 88 | 1 | u8 | amp_release | U7 | 0 | 127 | 40 |
| 13 | 89 | 1 | u8 | filter | U7 | 0 | 127 | 64 |
| 14 | 90 | 1 | u8 | resonance | U7 | 0 | 127 | 0 |
| 15 | 91 | 1 | u8 | keytrack | U7 | 0 | 127 | 0 |
| 16 | 92 | 1 | i8 | filter_env_depth | S7 | -127 | 127 | 0 |
| 17 | 93 | 1 | u8 | timbre_mod | U7 | 0 | 127 | 64 |
| 18 | 94 | 1 | u8 | pitch_mod | U7 | 0 | 127 | 64 |
| 19 | 95 | 1 | u8 | morph_mod | U7 | 0 | 127 | 64 |
| 20 | 96 | 1 | u8 | menv_attack | U7 | 0 | 127 | 0 |
| 21 | 97 | 1 | u8 | menv_decay | U7 | 0 | 127 | 64 |
| 22 | 98 | 1 | u8 | menv_sustain | U7 | 0 | 127 | 0 |
| 23 | 99 | 1 | u8 | menv_release | U7 | 0 | 127 | 24 |
| 24 | 100 | 1 | i8 | menv_amount | S7 | -127 | 127 | 0 |
| 25 | 101 | 1 | u8 | menv_target | TARGET | 0 | 27 | 4 |
| 26 | 102 | 1 | u8 | menv_loop | BOOL | 0 | 1 | 0 |
| 27 | 103 | 1 | u8 | menv_retrigger | BOOL | 0 | 1 | 1 |
| 28 | 104 | 1 | u8 | lfo1_wave | WAVE | 0 | 4 | 0 |
| 29 | 105 | 1 | u8 | lfo1_rate | U7 | 0 | 127 | 32 |
| 30 | 106 | 1 | u8 | lfo1_depth | U7 | 0 | 127 | 0 |
| 31 | 107 | 1 | u8 | lfo1_target | TARGET | 0 | 27 | 0 |
| 32 | 108 | 1 | u8 | lfo1_sync | BOOL | 0 | 1 | 0 |
| 33 | 109 | 1 | u8 | lfo1_division | LFO_DIV | 0 | 15 | 0 |
| 34 | 110 | 1 | u8 | lfo1_key_reset | BOOL | 0 | 1 | 0 |
| 35 | 111 | 1 | u8 | lfo1_fade_in | U7 | 0 | 127 | 0 |
| 36 | 112 | 1 | u8 | lfo2_wave | WAVE | 0 | 4 | 0 |
| 37 | 113 | 1 | u8 | lfo2_rate | U7 | 0 | 127 | 48 |
| 38 | 114 | 1 | u8 | lfo2_depth | U7 | 0 | 127 | 0 |
| 39 | 115 | 1 | u8 | lfo2_target | TARGET | 0 | 27 | 0 |
| 40 | 116 | 1 | u8 | lfo2_sync | BOOL | 0 | 1 | 0 |
| 41 | 117 | 1 | u8 | lfo2_division | LFO_DIV | 0 | 15 | 0 |
| 42 | 118 | 1 | u8 | lfo2_key_reset | BOOL | 0 | 1 | 0 |
| 43 | 119 | 1 | u8 | lfo2_fade_in | U7 | 0 | 127 | 0 |
| 44 | 120 | 8 | u8 | route_source[8] | SOURCE | 0 | 17 | 0 |
| 52 | 128 | 8 | u8 | route_target[8] | TARGET | 0 | 27 | 0 |
| 60 | 136 | 8 | i8 | route_amount[8] | S7 | -127 | 127 | 0 |
| 68 | 144 | 8 | u8 | route_enabled[8] | BOOL | 0 | 1 | 0 |
| 76 | 152 | 1 | u8 | drive_send | U7 | 0 | 127 | 0 |
| 77 | 153 | 1 | u8 | drive_mode | ENGINE4 | 0 | 3 | 0 |
| 78 | 154 | 1 | u8 | drive_gain | U7 | 0 | 127 | 64 |
| 79 | 155 | 1 | u8 | drive_tone | U7 | 0 | 127 | 127 |
| 80 | 156 | 1 | u8 | delay_send | U7 | 0 | 127 | 0 |
| 81 | 157 | 1 | u8 | delay_engine | ENGINE4 | 0 | 3 | 0 |
| 82 | 158 | 1 | u8 | delay_time | U7 | 0 | 127 | 64 |
| 83 | 159 | 1 | u8 | delay_feedback | U7 | 0 | 127 | 57 |
| 84 | 160 | 1 | u8 | delay_width | U7 | 0 | 127 | 0 |
| 85 | 161 | 1 | u8 | delay_tone | U7 | 0 | 127 | 127 |
| 86 | 162 | 1 | u8 | delay_wow_rate | U7 | 0 | 127 | 64 |
| 87 | 163 | 1 | u8 | reverb_send | U7 | 0 | 127 | 0 |
| 88 | 164 | 1 | u8 | mod_send | U7 | 0 | 127 | 0 |
| 89 | 165 | 1 | u8 | mod_engine | ENGINE4 | 0 | 3 | 0 |
| 90 | 166 | 1 | u8 | mod_rate | U7 | 0 | 127 | 40 |
| 91 | 167 | 1 | u8 | mod_depth | U7 | 0 | 127 | 48 |
| 92 | 168 | 1 | u8 | mod_feedback | U7 | 0 | 127 | 0 |
| 93 | 169 | 1 | u8 | seq_length | SEQ_LEN | 1 | 64 | 16 |
| 94 | 170 | 1 | u8 | seq_speed | SPEED | 0 | 2 | 1 |
| 95 | 171 | 1 | u8 | rec_quantize | QUANT | 0 | 6 | 0 |
| 96 | 172 | 1 | u8 | arp_mode | ARP_MODE | 0 | 4 | 0 |
| 97 | 173 | 1 | u8 | arp_rate | ARP_RATE | 0 | 4 | 2 |
| 98 | 174 | 1 | u8 | arp_octaves | ONE_TO_4 | 1 | 4 | 1 |
| 99 | 175 | 1 | u8 | arp_gate | ARP_GATE | 1 | 127 | 64 |
| 100 | 176 | 1 | u8 | arp_latch | BOOL | 0 | 1 | 0 |
| 101 | 177 | 1 | u8 | arp_follow_swing | BOOL | 0 | 1 | 1 |
| 102 | 178 | 1 | u8 | arp_repeat | ONE_TO_4 | 1 | 4 | 1 |
| 103 | 179 | 1 | u8 | seq_def_note | U7 | 0 | 127 | 60 |
| 104 | 180 | 2 | u16 | seq_def_length | STEP_LEN | 0 | 1024 | 16 |
| 106 | 182 | 1 | u8 | seq_def_velocity | U7 | 0 | 127 | 100 |
| 107 | 183 | 1 | u8 | seq_def_condition | COND | 0 | 144 | 0 |
| 108 | 184 | 1 | u8 | voice_mode | VOICE_MODE | 0 | 2 | 0 |
| 109 | 185 | 1 | u8 | harmonics_mod | GATE | 0 | 128 | 0 |
| 110 | 186 | 1 | u8 | delay_short | DELAY_SHORT | 0 | 2 | 0 |
| 111 | 187 | 1 | u8 | drive_bias | U7 | 0 | 127 | 0 |

Saved and restored but not yet acted on by the engine: `lfo*_sync`, `lfo*_division`, `lfo*_key_reset`,
`lfo*_fade_in`, `delay_width`, `delay_tone`, `delay_wow_rate`, `mod_feedback` (runtime home `br_part_pending`).
`keytrack` is an amount (127 = 1 octave per octave); the HOME KEY TRACK knob switches 0/127.
`filter_env_depth` is HOME ENV DEPTH: the mod envelope to the cutoff, besides that envelope's own TARGET/DEPTH.

## Section 4 KIT

| +Off | Abs | Size | Type | Field | Range | Min | Max | Default |
|---|---|---|---|---|---|---|---|---|
| 0 | 528 | 4 | u8 | voice_choice[4] | VOICE | 0 | 2 | 0 |
| 4 | 532 | 4 | u8 | lane_level[4] | U7 | 0 | 127 | 113 |
| 8 | 536 | 4 | u8 | lane_mute[4] | BOOL | 0 | 1 | 0 |
| 12 | 540 | 1 | u8 | kit_gate | GATE | 0 | 128 | 128 |
| 13 | 541 | 1 | u8 | drum_snap | U7 | 0 | 127 | 64 |
| 14 | 542 | 1 | u8 | punch_loop_len | CHOICE4 | 0 | 3 | 2 |
| 15 | 543 | 1 | u8 | punch_echo_time | CHOICE4 | 0 | 3 | 2 |
| 16 | 544 | 1 | u8 | punch_filter_shape | CHOICE4 | 0 | 3 | 0 |
| 17 | 545 | 1 | u8 | punch_trem_rate | CHOICE4 | 0 | 3 | 2 |
| 18 | 546 | 1 | u8 | punch_echo_feedback | LEVEL32 | 0 | 31 | 12 |
| 19 | 547 | 1 | u8 | punch_filter_amount | LEVEL32 | 0 | 31 | 20 |
| 20 | 548 | 1 | u8 | punch_trem_depth | LEVEL32 | 0 | 31 | 24 |
| 21 | 549 | 3 | u8 | def_note_l123[3] | KIT_NOTE | 36 | 84 | 60 |
| 24 | 552 | 6 | u16 | def_length_l123[3] | STEP_LEN | 0 | 1024 | 0 |
| 30 | 558 | 3 | u8 | def_velocity_l123[3] | U7 | 0 | 127 | 100 |
| 33 | 561 | 3 | u8 | def_condition_l123[3] | COND | 0 | 144 | 0 |
| 36 | 564 | 8 | u8 | spare_kit[8] | SPARE | 0 | 0 | 0 |

## Section 5 PATTERN

The live pattern. Out-of-range elements are replaced (clamped) and counted.

| +Off | Abs | Size | Block | Element | Default byte |
|---|---|---|---|---|---|
| 0 | 576 | 384 | step_synth | u16 | 0 |
| 384 | 960 | 128 | step_kit | u16 | 0 |
| 512 | 1088 | 384 | synth_len_micro | u16 | 0 |
| 896 | 1472 | 192 | synth_condition | u8 | 0 |
| 1088 | 1664 | 192 | synth_note | u8 | 255 |
| 1280 | 1856 | 192 | synth_velocity | u8 | 255 |
| 1472 | 2048 | 512 | kit_len_micro | u16 | 0 |
| 1984 | 2560 | 256 | kit_condition | u8 | 0 |
| 2240 | 2816 | 256 | kit_velocity | u8 | 100 |
| 2496 | 3072 | 256 | kit_pitch | u8 | 255 |
| 2752 | 3328 | 256 | kit_retrig | u8 | 0 |

Elements: step_synth [3][64] lo = note+1 (0 off, max 128), hi = velocity | tie<<7; step_kit [64] lo = hit
mask, hi = accent mask (bits 0-3); *_len_micro bits 0-10 length 0..1024, bits 11-15 micro -16..15;
synth [3][64], kit [64][4]; conditions 0..144; synth note/velocity 0..127 or 255 none; kit velocity 0..127;
kit_pitch 36..84 or 255 (C4); kit_retrig bits 0-2 count, 3-5 spacing, 6-7 zero.
