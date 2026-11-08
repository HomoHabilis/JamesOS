/* MIDI routing helpers. Channels are 1..16, with 0 meaning OFF; destination bits 0..2 are synths,
 * bits 3..6 are the four kit lanes. The bounded note ledger ensures an old
 * note-off follows its original destinations across a routing change. */
#ifndef BR_INSTRUMENT_MIDI_ROUTES_H
#define BR_INSTRUMENT_MIDI_ROUTES_H
#include <stdint.h>
#include <string.h>
#define BR_MIDI_DESTINATIONS 7u
#define BR_MIDI_ROUTE_NOTES 64u
#define BR_MIDI_DRUM_BITS 0x78u

typedef struct { uint8_t channel, note, targets, stale; } br_midi_route_note_t;
typedef struct {
    br_midi_route_note_t note[BR_MIDI_ROUTE_NOTES];
    uint8_t drum_channel[4], drum_note[4], drum_active;
} br_midi_routes_t;

static inline uint8_t br_midi_route_targets(const volatile uint8_t channels[7], uint8_t channel)
{
    uint8_t bits = 0u; uint32_t i;
    if (!channels || channel < 1u || channel > 16u) return 0u;
    for (i = 0; i < BR_MIDI_DESTINATIONS; ++i)
        if (channels[i] == channel) bits |= (uint8_t)(1u << i);
    return bits;
}
static inline br_midi_route_note_t *br_midi_route_find(br_midi_routes_t *s, uint8_t ch, uint8_t note)
{
    uint32_t i;
    for (i = 0; i < BR_MIDI_ROUTE_NOTES; ++i)
        if (s->note[i].channel == ch && s->note[i].note == note) return &s->note[i];
    return 0;
}
/* False/zero means no routing or ledger full; callers must not emit a note on
 * when the ledger cannot remember how to release it. */
static inline uint8_t br_midi_route_on(br_midi_routes_t *s, const volatile uint8_t channels[7], uint8_t ch, uint8_t note)
{
    uint32_t i; uint8_t targets = br_midi_route_targets(channels, ch);
    br_midi_route_note_t *e;
    if (!s || !targets || note > 127u) return 0u;
    e = br_midi_route_find(s, ch, note);
    if (!e) {
        for (i = 0; i < BR_MIDI_ROUTE_NOTES; ++i) if (!s->note[i].channel) { e = &s->note[i]; break; }
        if (!e) return 0u;
        e->channel = ch; e->note = note; e->targets = 0u; e->stale = 0u;
    } else {
        /* MIDI has no generation ID; assume offs arrive in on order. At
         * saturation reject a retrigger rather than wrap and release it early. */
        if (e->stale == 255u) return 0u;
        ++e->stale;
    }
    e->targets = targets;
    for (i = 0; i < 4u; ++i) if (targets & (1u << (i + 3u))) {
        s->drum_channel[i] = ch; s->drum_note[i] = note;
        s->drum_active |= (uint8_t)(1u << i);
    }
    return targets;
}
static inline uint8_t br_midi_route_off(br_midi_routes_t *s, uint8_t ch, uint8_t note)
{
    br_midi_route_note_t *e; uint8_t targets; uint32_t i;
    if (!s || ch < 1u || ch > 16u || note > 127u) return 0u;
    e = br_midi_route_find(s, ch, note);
    if (!e) return 0u;
    if (e->stale) { --e->stale; return 0u; }
    targets = e->targets;
    for (i = 0; i < 4u; ++i) if (targets & (1u << (i + 3u))) {
        if (!(s->drum_active & (1u << i)) || s->drum_channel[i] != ch || s->drum_note[i] != note)
            targets &= (uint8_t)~(1u << (i + 3u));
        else s->drum_active &= (uint8_t)~(1u << i);
    }
    memset(e, 0, sizeof *e);
    return targets;
}
/* Invalidate all outstanding routes for stopped parts before a session load,
 * panic or other bulk reassignment. Keep note tombstones for later offs. */
static inline void br_midi_route_invalidate(br_midi_routes_t *s, uint8_t parts)
{
    uint8_t purge = (uint8_t)(parts & 7u); uint32_t i;
    if (!s) return;
    if (parts & 8u) { purge |= BR_MIDI_DRUM_BITS; s->drum_active = 0u; }
    for (i = 0; i < BR_MIDI_ROUTE_NOTES; ++i) s->note[i].targets &= (uint8_t)~purge;
}
/* Channel-mode all-notes/all-sound-off terminates that channel's held-note
 * ownership; no individual note-offs are required after the panic. */
static inline void br_midi_route_forget_channel(br_midi_routes_t *s, uint8_t channel)
{
    if (!s || channel < 1u || channel > 16u) return;
    for (uint32_t i = 0u; i < BR_MIDI_ROUTE_NOTES; ++i)
        if (s->note[i].channel == channel) memset(&s->note[i], 0, sizeof s->note[i]);
}
/* Change one destination. Return affected part-mask: bit 0..2 synth, bit 3
 * kit. Caller must immediately stop/cancel those parts and their record holds.
 * The kit engine currently only has a whole-part panic, so all kit ownership
 * is invalidated. Outstanding input keys remain tombstones until their offs. */
static inline uint8_t br_midi_route_set(br_midi_routes_t *s, volatile uint8_t channels[7], uint32_t dest, uint8_t channel)
{
    uint8_t parts;
    if (!s || !channels || dest >= 7u || channel > 16u || channels[dest] == channel) return 0u;
    parts = dest < 3u ? (uint8_t)(1u << dest) : 8u;
    br_midi_route_invalidate(s, parts);
    channels[dest] = channel;
    return parts;
}
#endif
