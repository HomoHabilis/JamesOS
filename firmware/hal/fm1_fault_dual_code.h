/* Diagnostic-only retained RAM-code windows for both published fault slots. */
#pragma once
#include "fm1_fault_diag.h"

#define FM1_DUAL_CODE_MAGIC 0x46444331u /* FDC1 */
#define FM1_DUAL_CODE_VERSION 1u

typedef struct {
    uint32_t magic, version, byte_size, epoch, valid_mask, owner_cpu;
    /* Snapshot from the boot which faulted, not the subsequent rescue boot. */
    uint32_t early_status, early_timeouts, early_con0, early_con5, early_con6, early_con9;
    fm1_fault_ram_code_record_t cpu[2];
} fm1_fault_dual_code_record_t;
typedef char fm1_fault_dual_code_is_240[(sizeof(fm1_fault_dual_code_record_t) == 240u) ? 1 : -1];
extern volatile fm1_fault_dual_code_record_t fm1_fault_dual_code_record;
extern volatile uint32_t fm1_early_p33_snapshot[6];
void fm1_fault_dual_code_capture_owned(uint32_t epoch, uint32_t cpu);

/* Accept a slot only if its primary bundle and private publication agree. */
static inline uint32_t fm1_fault_dual_slot_identity(
    const volatile fm1_fault_diag_bundle_t *b, uint32_t cpu,
    uint32_t epoch, uint32_t *pc)
{
    const volatile fm1_fault_diag_t *s;
    uint32_t phase;
    if (!b || !pc || cpu > 1u || !epoch ||
        b->magic != FM1_FAULT_BUNDLE_MAGIC ||
        b->version != FM1_FAULT_BUNDLE_VERSION || b->byte_size != sizeof(*b) ||
        b->epoch != epoch || b->owner_cpu > 1u || (b->valid_mask & ~3u) ||
        !(b->valid_mask & (1u << b->owner_cpu)) || !(b->valid_mask & (1u << cpu)))
        return 0u;
    phase = cpu ? b->cpu1_phase : b->cpu0_phase;
    s = &b->cpu[cpu];
    if (phase != 2u || s->magic != FM1_FAULT_DIAG_MAGIC ||
        s->version != FM1_FAULT_DIAG_VERSION || s->byte_size != sizeof(*s) ||
        s->fault_count != epoch || s->current_cpu_id != cpu) return 0u;
    *pc = s->fault_frame[16];
    return 1u;
}

/* Sealing is testable without hardware: callers supply only the two bounded
 * fixed code-window sources derived from their respective published PCs. */
static inline uint32_t fm1_fault_dual_code_capture(
    volatile fm1_fault_dual_code_record_t *r, const volatile fm1_fault_diag_bundle_t *b,
    uint32_t epoch, uint32_t cpu, const volatile uint32_t early[6],
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high,
    const uint32_t source_address[2], const volatile uint32_t *const source[2])
{
    uint32_t e, owner, owner_pc, c, pc, start, i, valid = 0u;
    volatile uint32_t *words = (volatile uint32_t *)r;
    if (!r) return 0u;
    r->magic = 0u;
    fm1_fault_ram_code_barrier();
    if (!early || !source_address || !source ||
        !fm1_fault_ram_code_bundle_identity(b, &e, &owner, &owner_pc) ||
        e != epoch || owner != cpu) return 0u;
    for (i = 1u; i < sizeof(*r) / 4u; ++i) words[i] = 0u;
    r->version = FM1_DUAL_CODE_VERSION; r->byte_size = sizeof(*r);
    r->epoch = epoch; r->owner_cpu = cpu;
    r->early_status = early[0]; r->early_timeouts = early[1];
    r->early_con0 = early[2]; r->early_con5 = early[3];
    r->early_con6 = early[4]; r->early_con9 = early[5];
    for (c = 0u; c < 2u; ++c) {
        if (!fm1_fault_dual_slot_identity(b, c, epoch, &pc) ||
            !fm1_fault_ram_code_window(pc, rt_low, rt_high, ra_low, ra_high, &start) ||
            source_address[c] != start || !source[c]) continue;
        if (fm1_fault_ram_code_capture_words(&r->cpu[c], c, epoch, pc,
                rt_low, rt_high, ra_low, ra_high, start, source[c])) valid |= 1u << c;
    }
    r->valid_mask = valid;
    fm1_fault_ram_code_barrier();
    r->magic = FM1_DUAL_CODE_MAGIC;
    fm1_fault_ram_code_barrier();
    return 1u;
}

static inline uint32_t fm1_fault_dual_code_record_valid(
    const volatile fm1_fault_dual_code_record_t *r,
    const volatile fm1_fault_diag_bundle_t *b,
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high)
{
    uint32_t c, pc, owner_epoch, owner, owner_pc;
    if (!r || !b || r->magic != FM1_DUAL_CODE_MAGIC) return 0u;
    fm1_fault_ram_code_barrier();
    if (r->version != FM1_DUAL_CODE_VERSION || r->byte_size != sizeof(*r) ||
        (r->valid_mask & ~3u) ||
        !fm1_fault_ram_code_bundle_identity(b, &owner_epoch, &owner, &owner_pc) ||
        r->epoch != owner_epoch || r->owner_cpu != owner) return 0u;
    for (c = 0u; c < 2u; ++c) {
        if (r->valid_mask & (1u << c)) {
            if (!fm1_fault_dual_slot_identity(b, c, owner_epoch, &pc) ||
                !fm1_fault_ram_code_record_valid(&r->cpu[c], owner_epoch, c, pc,
                                                rt_low, rt_high, ra_low, ra_high)) return 0u;
        } else if (r->cpu[c].magic != 0u) return 0u;
    }
    return 1u;
}

static inline uint32_t fm1_fault_dual_code_chunk(
    const volatile fm1_fault_dual_code_record_t *r,
    const volatile fm1_fault_diag_bundle_t *b,
    uint32_t rt_low, uint32_t rt_high, uint32_t ra_low, uint32_t ra_high,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)r;
    uint32_t i;
    if (!out || !requested || requested > 125u || offset > sizeof(*r) ||
        requested > sizeof(*r) - offset ||
        !fm1_fault_dual_code_record_valid(r, b, rt_low, rt_high, ra_low, ra_high)) return 0u;
    fm1_fault_ram_code_barrier();
    out[0] = (uint8_t)offset; out[1] = (uint8_t)(offset >> 8); out[2] = (uint8_t)requested;
    for (i = 0u; i < requested; ++i) out[3u + i] = bytes[offset + i];
    return requested + 3u;
}
