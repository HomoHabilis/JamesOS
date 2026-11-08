/* Private read-only 0xB3 clock diagnostic schema 2. */
#ifndef FM1_MIDI_CLOCK_DIAG_H
#define FM1_MIDI_CLOCK_DIAG_H
#define BR_CLOCK_DIAG_WORDS 23u
#define BR_CLOCK_FAIL_INVALID  (1u << 0) /* follower rejected an event */
#define BR_CLOCK_FAIL_LOST     (1u << 1) /* follower clock timeout */
#define BR_CLOCK_FAIL_ANCHOR   (1u << 2) /* DMA anchor, timestamp map or render */
#define BR_CLOCK_FAIL_OVERFLOW (1u << 3) /* ordered transport queue */
#define BR_CLOCK_FAIL_SPP      (1u << 4) /* unsupported song position */
/* 0 schema=2, 1 supported, 2..6 cumulative counts in flag order,
 * 7 last failure mask, 8 follower locked, 9 follower enabled for sequencing,
 * 10 measured BPM, 11 playback, 12 USB MIDI clock selected. Counts wrap at
 * 2^32 and survive Start/Stop/project changes until reboot; 0x61 does not reset.
 * 13 F8 processed; 14 rejected (follower rejected/invalid/lost/anchor); 15 suspect;
 * 16 interval count; 17 interval sum in frames modulo 2^32; 18 last,
 * 19 minimum, 20 maximum interval frames (all since boot); 21/22 follower
 * period Q16 frames per MIDI clock (low/high). Intervals are between mapped
 * F8 events, including rejected and suspect events. Anchor resets discard
 * the prior endpoint. They measure receipt timing, not sender generation. */
#endif
