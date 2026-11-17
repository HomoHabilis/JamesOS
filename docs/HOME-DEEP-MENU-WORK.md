> **Note, 16 November 2026: partly out of date.** This page was written for session format v19. The current candidate is build 3078, session format v20 in project envelope v6. Checked against the source: OCT- with HOME (either press order) opens or leaves the deep menu; holding HOME for 0.8 s toggles HOLD; the two deep screens are MOD DEPTHS and the voice screen; the voice screen has PORTA TIME (0 to 100), GLIDE MODE (OFF, LEGATO, ALWAYS) and AMP MODE (ENVELOPE, LPG); glide time is at most two seconds per octave. Not current: the fourth control is no longer inactive. With AMP MODE set to LPG on a synth track it is GATE LENGTH (OFF, or 1 to 31), added in session v20. On kit tracks the voice controls are inactive. "Older firmware rejects v19" now applies to v20. The "in progress" title is out of date. The MOD DEPTHS law and the migration of old negative multipliers are described in [SESSION-V20.md](SESSION-V20.md) and were not re-verified here.

# HOME deep menu — implementation in progress

Owner decision: OCT− + HOME opens a two-screen deep menu and returns to
HOME on the next chord; holding HOME for 0.8 seconds toggles HOLD.
The first deep page contains the current MOD DEPTHS controls. The second
contains PORTA TIME 0–100% (portamento), GLIDE MODE (OFF, LEGATO, ALWAYS), and AMP MODE
(ENVELOPE, LPG), with the fourth control inactive. Normal HOME cycles its
sound and filter pages. The same design applies to both instruments.

The HOLD handler fires after 800 ms. The deep-menu chord must not deliver a
stray HOME tap or OCT− octave change. Tests must exercise
both button press orders, release orders, cancellation, page rings and LEDs.

Glide follows actual pitch through mono and independently allocated poly
voices; LEGATO tests held notes rather than lingering release tails. At a
fresh note with no prior pitch there is no glide. The `instrument_glide.h`
ramp uses up to two seconds per octave and retains fractional pitch across
blocks. Host audio-path tests cover both mono and poly; device timing and
listening checks are still required.

LPG couples a low-pass response to the amplitude envelope, in addition to
amplitude. It has independent per-voice state in mono and poly; ENVELOPE is
the default. The drum-kit voice controls are shown inactive. Its sound and
processor cost still need device qualification.

The v19 session uses six previously spare KIT bytes for three synth tracks and
keeps the 3,584-byte record size. v17/v18 are migrated on load. The revised
MOD DEPTHS law is unity at 0%, half at −50%, and double at +100%. Old positive
multipliers migrate to the nearest equivalent. Old negative multipliers were
inversions and cannot be represented by the new non-inverting law; they become
zero modulation. Preserve original backups and disclose this limitation before
release. Standalone codec, app save/load and v4 prototype conversion have host
coverage; the public 1.0a upgrade path remains unqualified. The browser project
transfer treats the project payload as opaque. Older firmware rejects v19.
Recheck final-candidate size, flash placement and hardware timing.
