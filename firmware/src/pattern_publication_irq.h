/* Single foreground producer / audio ISR ownership protocol experiment.
 * Requires aligned indivisible 32-bit loads/stores and the platform's ordered
 * shared-memory barrier. Do not ship without target disassembly, architecture
 * review, queue tests and real-time validation. No C atomic operations. */
#ifndef FM1_PATTERN_PUBLICATION_IRQ_H
#define FM1_PATTERN_PUBLICATION_IRQ_H
#include <stdint.h>
#include "pattern_snapshot.h"
#include "../hal/fm1_memory_sync.h"
#define BR_PATTERN_IRQ_NO_SLOT UINT32_MAX
#define BR_PATTERN_IRQ_SLOTS 1u

typedef struct {
    br_pattern_snapshot *slot[BR_PATTERN_IRQ_SLOTS];
    /* Each mailbox word has one writer. Request data precedes request_seq;
     * ack_seq is written only when the staged slot is reader-free. */
    volatile uint32_t request_slot, request_seq, ack_seq;
    uint32_t free_mask, writer_slot, producer_active, producer_pending;
    uint32_t producer_seq, outstanding; /* foreground-only */
    uint32_t audio_active; /* audio-owner-only */
    volatile uint32_t audio_seen, audio_retired; /* audio-written, stopped cancellation reads */
} br_pattern_irq_publication;

/* b and c are reserved for compatibility; only a is used. */
static inline int br_pattern_irq_init(br_pattern_irq_publication *p,
    br_pattern_snapshot *a, br_pattern_snapshot *b, br_pattern_snapshot *c)
{
    (void)b; (void)c;
    if (!p || !a) return 0;
    p->slot[0]=a;
    p->request_slot=0u; p->request_seq=p->ack_seq=0u;
    p->free_mask=1u; p->writer_slot=BR_PATTERN_IRQ_NO_SLOT;
    p->producer_active=p->producer_seq=p->outstanding=0u;
    p->producer_pending=BR_PATTERN_IRQ_NO_SLOT;
    p->audio_active=p->audio_seen=0u; p->audio_retired=BR_PATTERN_IRQ_NO_SLOT;
    return 1;
}
/* Only foreground changes free_mask and producer bookkeeping. */
static inline void br_pattern_irq_poll(br_pattern_irq_publication *p)
{
    uint32_t ack = p->ack_seq;
    fm1_memory_publish_sync();
    if (p->outstanding && ack == p->producer_seq) {
        p->free_mask |= 1u << p->producer_pending;
        p->producer_pending = BR_PATTERN_IRQ_NO_SLOT;
        p->outstanding = 0u;
    }
}
static inline uint32_t br_pattern_irq_claim(br_pattern_irq_publication *p)
{
    uint32_t i;
    br_pattern_irq_poll(p);
    if (p->writer_slot != BR_PATTERN_IRQ_NO_SLOT) return BR_PATTERN_IRQ_NO_SLOT;
    for (i=0; i<BR_PATTERN_IRQ_SLOTS; ++i) if (p->free_mask & (1u << i)) {
        p->free_mask &= ~(1u << i); p->writer_slot=i; return i;
    }
    return BR_PATTERN_IRQ_NO_SLOT;
}
static inline int br_pattern_irq_abandon(br_pattern_irq_publication *p, uint32_t slot)
{
    if (slot >= BR_PATTERN_IRQ_SLOTS || p->writer_slot != slot) return 0;
    p->writer_slot=BR_PATTERN_IRQ_NO_SLOT; p->free_mask |= 1u << slot; return 1;
}
static inline int br_pattern_irq_submit(br_pattern_irq_publication *p, uint32_t slot)
{
    br_pattern_irq_poll(p);
    if (slot >= BR_PATTERN_IRQ_SLOTS || p->writer_slot != slot || p->outstanding) return 0;
    p->producer_pending=slot; p->producer_seq += 1u;
    p->outstanding=1u; p->writer_slot=BR_PATTERN_IRQ_NO_SLOT;
    p->request_slot=slot;
    fm1_memory_publish_sync();
    p->request_seq=p->producer_seq; /* release publication, written last */
    return 1;
}
/* Foreground only, while stopped with audio IRQ excluded. Cancels only an
 * unconsumed request; caller must cancel its bank reservation separately. */
static inline int br_pattern_irq_cancel_unseen(br_pattern_irq_publication *p)
{
    if (!p->outstanding || p->audio_seen == p->producer_seq ||
        p->request_seq != p->producer_seq || p->audio_retired != BR_PATTERN_IRQ_NO_SLOT)
        return 0;
    p->request_seq = p->audio_seen;
    fm1_memory_publish_sync();
    p->free_mask |= 1u << p->producer_pending;
    p->producer_pending = BR_PATTERN_IRQ_NO_SLOT;
    p->outstanding = 0u;
    return 1;
}
/* Audio owner only. An unretired staged slot applies backpressure. */
static inline const br_pattern_snapshot *br_pattern_irq_boundary(
    br_pattern_irq_publication *p, uint32_t *old_slot)
{
    uint32_t seq=p->request_seq;
    fm1_memory_publish_sync();
    *old_slot=BR_PATTERN_IRQ_NO_SLOT;
    if (seq != p->audio_seen && p->audio_retired == BR_PATTERN_IRQ_NO_SLOT) {
        uint32_t next=p->request_slot;
        if (next >= BR_PATTERN_IRQ_SLOTS) return 0;
        /* Audio previously read globals. Protect this staged slot until it
         * returns to globals and explicitly retires the staged reader. */
        *old_slot=next; p->audio_retired=next;
        p->audio_active=next; p->audio_seen=seq;
    }
    return p->slot[p->audio_active];
}
/* Audio owner after all staged-slot readers have finished. */
static inline int br_pattern_irq_retire(br_pattern_irq_publication *p, uint32_t slot)
{
    if (slot >= BR_PATTERN_IRQ_SLOTS || p->audio_retired != slot) return 0;
    p->audio_retired=BR_PATTERN_IRQ_NO_SLOT;
    fm1_memory_publish_sync();
    p->ack_seq=p->audio_seen;
    return 1;
}
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
int br_pattern_irq_audio_bind(br_pattern_irq_publication *p);
int br_pattern_irq_audio_reset(br_pattern_irq_publication *p);
uint32_t br_pattern_irq_edits_blocked(void);
uint32_t br_pattern_irq_bar_count(void);
uint32_t br_pattern_irq_late_count(void);
uint32_t br_pattern_irq_error(void);
int br_pattern_irq_foreground_claim(void);
void br_pattern_irq_foreground_release(void);
int br_pattern_irq_globals_ready(void);
#endif
#endif
