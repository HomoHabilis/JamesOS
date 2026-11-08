/* Read-only, fixed P33 register survey and separately retained fault snapshot. */
#pragma once
#include <stdint.h>
#include "fm1_fault_diag.h"

#define FM1_POWER_READ_COUNT 11u
#define FM1_POWER_READ_UNKNOWN 0xFFFFFFFFu
#define FM1_POWER_READ_READY 0x50524431u
#define FM1_POWER_FAULT_MAGIC 0x46505731u /* FPW1 */
#define FM1_POWER_FAULT_VERSION 1u

typedef struct { uint32_t timeouts, raw[FM1_POWER_READ_COUNT]; } fm1_power_read_sample_t;
typedef int (*fm1_power_read_fn)(void *, uint32_t, uint32_t *);
typedef struct {
    uint32_t magic, version, byte_size, epoch, owner_cpu;
    fm1_power_read_sample_t entry;
    uint32_t checksum;
} fm1_power_fault_record_t;
typedef char fm1_power_fault_record_is_72[(sizeof(fm1_power_fault_record_t)==72u)?1:-1];
extern volatile fm1_power_read_sample_t fm1_power_read_entry;
extern volatile fm1_power_fault_record_t fm1_power_fault_record;
extern volatile uint32_t fm1_power_read_entry_ready;
void fm1_power_fault_capture_owned(uint32_t epoch, uint32_t cpu);

/* Fixed whitelist, in response order: P33 00,01,02,05,06,07,08,09,11,16,17. */
static inline uint32_t fm1_power_read_reg(uint32_t i)
{
    if (i < 3u) return i;
    if (i < 8u) return i + 2u;
    return i == 8u ? 0x11u : i == 9u ? 0x16u : 0x17u;
}
/* A survey without proven C1 hold is explicitly unavailable. */
static inline void fm1_power_read_invalidate(fm1_power_read_sample_t *out)
{
    uint32_t i;
    out->timeouts = FM1_POWER_READ_UNKNOWN;
    for (i=0u; i<FM1_POWER_READ_COUNT; ++i) out->raw[i] = FM1_POWER_READ_UNKNOWN;
}
static inline void fm1_power_read_collect(fm1_power_read_sample_t *out,
    fm1_power_read_fn read, void *context, uint32_t *timeouts)
{
    uint32_t i, before, value, failed = 0u;
    for (i=0u; i<FM1_POWER_READ_COUNT; ++i) {
        before = *timeouts; value = FM1_POWER_READ_UNKNOWN;
        if (!failed && (!read(context, fm1_power_read_reg(i), &value) || *timeouts != before))
            failed = 1u;
        out->raw[i] = failed ? FM1_POWER_READ_UNKNOWN : value;
    }
    out->timeouts = *timeouts;
}

/* FNV-1a over word values detects changes after capture; it cannot validate
 * the original P33 transfer or protect against all possible collisions. */
static inline uint32_t fm1_power_fault_checksum(const volatile fm1_power_fault_record_t *r)
{
    uint32_t i, h=2166136261u;
#define FM1_POWER_HASH_WORD(v) do { h=(h^(v))*16777619u; } while (0)
    FM1_POWER_HASH_WORD(r->version); FM1_POWER_HASH_WORD(r->byte_size);
    FM1_POWER_HASH_WORD(r->epoch); FM1_POWER_HASH_WORD(r->owner_cpu);
    FM1_POWER_HASH_WORD(r->entry.timeouts);
    for (i=0u; i<FM1_POWER_READ_COUNT; ++i) FM1_POWER_HASH_WORD(r->entry.raw[i]);
#undef FM1_POWER_HASH_WORD
    return h;
}
static inline uint32_t fm1_power_fault_publish(
    volatile fm1_power_fault_record_t *out, const volatile fm1_fault_diag_bundle_t *bundle,
    uint32_t epoch, uint32_t cpu, const volatile fm1_power_read_sample_t *entry)
{
    uint32_t e, owner, pc, i;
    if (!out) return 0u;
    out->magic = 0u;
    fm1_fault_ram_code_barrier();
    if (!entry || !fm1_fault_ram_code_bundle_identity(bundle,&e,&owner,&pc) ||
        e != epoch || owner != cpu) return 0u;
    out->version = FM1_POWER_FAULT_VERSION; out->byte_size = sizeof(*out);
    out->epoch = epoch; out->owner_cpu = cpu;
    out->entry.timeouts = entry->timeouts;
    for (i=0u; i<FM1_POWER_READ_COUNT; ++i) out->entry.raw[i] = entry->raw[i];
    out->checksum = fm1_power_fault_checksum(out);
    fm1_fault_ram_code_barrier();
    out->magic = FM1_POWER_FAULT_MAGIC;
    fm1_fault_ram_code_barrier();
    return 1u;
}
static inline uint32_t fm1_power_fault_chunk(
    const volatile fm1_power_fault_record_t *r, const volatile fm1_fault_diag_bundle_t *b,
    uint32_t offset, uint32_t requested, uint8_t out[128])
{
    uint32_t e, owner, pc, i;
    const volatile uint8_t *bytes=(const volatile uint8_t *)r;
    if (!r || !out || !requested || requested > 125u || offset > sizeof(*r) ||
        requested > sizeof(*r)-offset || r->magic != FM1_POWER_FAULT_MAGIC) return 0u;
    fm1_fault_ram_code_barrier();
    if (r->version != FM1_POWER_FAULT_VERSION || r->byte_size != sizeof(*r) ||
        !fm1_fault_ram_code_bundle_identity(b,&e,&owner,&pc) || r->epoch != e || r->owner_cpu != owner ||
        r->checksum != fm1_power_fault_checksum(r))
        return 0u;
    out[0]=(uint8_t)offset; out[1]=(uint8_t)(offset>>8); out[2]=(uint8_t)requested;
    for (i=0u; i<requested; ++i) out[3u+i]=bytes[offset+i];
    return requested+3u;
}
