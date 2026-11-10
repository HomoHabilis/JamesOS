/* SPDX-License-Identifier: GPL-3.0-only */
/* Timed MIDI output from the audio renderer: sequencer and arp notes, MIDI
 * clock and transport. The renderer is the only producer and stamps each
 * USB-MIDI packet with the timer tick at which it is due; the USB service is
 * the only consumer and sends a packet once its tick has passed. Packets must
 * be pushed in non-decreasing due order. This keeps the clock's timing finer
 * than one audio block and leaves the main loop's own MIDI ring with a single
 * producer. */
#ifndef FM1_MIDI_OUT_SCHEDULE_H
#define FM1_MIDI_OUT_SCHEDULE_H

#include <stdint.h>

#define FM1_MIDI_OUT_SLOTS 64u
#ifndef FM1_MIDI_OUT_PUBLISH
#define FM1_MIDI_OUT_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

typedef struct { uint32_t packet, due; } fm1_midi_out_event_t;
typedef struct {
    fm1_midi_out_event_t event[FM1_MIDI_OUT_SLOTS];
    volatile uint32_t write, read;
    /* Timer tick at which the block being rendered started, and the frames
     * rendered since that tick was taken. */
    volatile uint32_t block_tick, block_frames;
} fm1_midi_out_schedule_t;

/* USB-MIDI event packets on cable 0. */
static inline uint32_t fm1_midi_out_note_packet(uint32_t channel, uint32_t note, uint32_t velocity)
{
    channel = (channel - 1u) & 15u;
    return velocity ? 0x09u | (0x90u | channel) << 8 | (note & 127u) << 16 | (velocity & 127u) << 24
                    : 0x08u | (0x80u | channel) << 8 | (note & 127u) << 16;
}
static inline uint32_t fm1_midi_out_realtime_packet(uint32_t status)
{
    return 0x0Fu | (status & 255u) << 8;
}

/* Timer ticks per audio frame in Q8, as a constant expression. */
#define FM1_MIDI_OUT_TICKS_PER_FRAME_Q8(ticks_per_second, frames_per_second) \
    ((uint32_t)((((uint64_t)(ticks_per_second)) << 8) / (frames_per_second)))

/* Timer ticks spanned by `frames` audio frames, for at most 4096 frames. */
static inline uint32_t fm1_midi_out_frame_ticks(uint32_t frames, uint32_t ticks_per_frame_q8)
{
    if (frames > 4096u) frames = 4096u;
    return (frames * ticks_per_frame_q8) >> 8;
}

/* The platform calls this as a render begins. */
static inline void fm1_midi_out_block_start(fm1_midi_out_schedule_t *s, uint32_t tick)
{
    s->block_tick = tick;
    s->block_frames = 0u;
}

/* Producer. Returns 0 and drops the packet when the ring is full. */
static inline int fm1_midi_out_push(fm1_midi_out_schedule_t *s, uint32_t packet, uint32_t due)
{
    uint32_t w = s->write;
    if (w - s->read >= FM1_MIDI_OUT_SLOTS) return 0;
    s->event[w % FM1_MIDI_OUT_SLOTS].packet = packet;
    s->event[w % FM1_MIDI_OUT_SLOTS].due = due;
    FM1_MIDI_OUT_PUBLISH();
    s->write = w + 1u;
    return 1;
}

static inline int fm1_midi_out_pending(const fm1_midi_out_schedule_t *s)
{
    return s->read != s->write;
}

/* Consumer. Returns 1 with the oldest packet when it is due at `now`. */
static inline int fm1_midi_out_pop(fm1_midi_out_schedule_t *s, uint32_t now, uint32_t *packet)
{
    uint32_t r = s->read;
    FM1_MIDI_OUT_PUBLISH();
    if (r == s->write || (int32_t)(now - s->event[r % FM1_MIDI_OUT_SLOTS].due) < 0) return 0;
    *packet = s->event[r % FM1_MIDI_OUT_SLOTS].packet;
    FM1_MIDI_OUT_PUBLISH();
    s->read = r + 1u;
    return 1;
}

/* Consumer, while nothing can be sent: forget what is waiting. */
static inline void fm1_midi_out_drop(fm1_midi_out_schedule_t *s)
{
    s->read = s->write;
}

#endif
