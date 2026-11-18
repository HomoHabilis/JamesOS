# Sound record v1 (standalone prototype)

This is a proposed interchange payload and has not been deployed. It does not
reserve flash or define a library, slot number, name, transport, publication
protocol, or project migration. Do not assume this format is frozen until the
website and device integration are reviewed together.

The record is 336 bytes: ASCII `FMSD` (4 bytes), version `1` (32-bit unsigned
little endian), 81 ordered 32-bit little-endian words, and a 32-bit little-endian
CRC-32/ISO-HDLC of all preceding 332 bytes (polynomial 0xedb88320 in reflected
form, initial and final XOR 0xffffffff). Signed words use two's complement. All
values are integers; signed ranges below are inclusive. Incompatible versions,
lengths, CRCs and out-of-range values are rejected without applying anything.

The word order is the `ALL_FIELDS` expansion in `sound_record.c`, specifically:

1. Sound: preset, timbre, color, tune, attack, decay, sustain, release, drive,
   delay, reverb, filter, filter resonance, filter keytrack, filter envelope
   depth, timbre modulation, FM modulation, morph modulation, drum snap, Plaits
   harmonics, Plaits aux, gate, drum voice mask.
2. Mod envelope: attack, decay, sustain, release, amount, target, loop,
   retrigger.
3. Track effects: drive mode, drive gain, drive tone, delay engine, delay
   time, delay feedback, modulation engine, modulation rate, modulation
   depth, modulation mix.
4. Two LFOs, each as wave, rate, depth, target.
5. Eight routes, each as source, target, amount, enabled.

Values follow the ranges in `sound_record.c`. Presets are current selectable
synths 52–75 or kits 48 and 76; destination track role is checked when
applying. Synths require enabled Plaits support. Tune is -24..24, modulation
amount and filter envelope depth are -127..127, gate is 0..128, boolean values
are 0..1, and the four 2-bit drum voice choices must each be 0..2. Other
continuous controls are 0..127; enumerated controls have the limits in the C
implementation. The record preserves even currently inactive modulation targets
and sources when their numeric IDs are in range.

Capture and apply cover fields currently present in `br_part_t`; level, pan,
mute, lane levels, notes and patterns are deliberately untouched. Global
reverb/compressor settings, mixer state, sequencer and arpeggiator settings are
not part of the sound. Some v17 project schema fields are reserved or have no
current `br_part_t` equivalent, so this is not a complete v17 track export.
Callers must capture a coherent state and surround apply with the existing
track revision/publication protocol. The codec performs no flash operations.
