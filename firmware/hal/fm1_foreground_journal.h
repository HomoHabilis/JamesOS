/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_FOREGROUND_JOURNAL_H
#define FM1_FOREGROUND_JOURNAL_H
#include <stdint.h>

#define FM1_FOREGROUND_JOURNAL_MAGIC 0x57444731u /* WDG1 */
#define FM1_FOREGROUND_JOURNAL_VERSION 1u
#define FM1_FOREGROUND_JOURNAL_UNKNOWN 0xffffffffu
#define FM1_FOREGROUND_JOURNAL_POLL_VALID 1u
#define FM1_FOREGROUND_JOURNAL_TIMER_VALID 2u
#define FM1_FOREGROUND_DIAG89_VERSION 1u
#define FM1_FOREGROUND_DIAG89_WORDS 14u
#define FM1_FOREGROUND_DIAG89_BYTES (FM1_FOREGROUND_DIAG89_WORDS * 4u)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t byte_size;
    uint32_t boot_id;
    uint32_t poll_seq;
    uint32_t poll_seq_inverse;
    uint32_t poll_t4;
    uint32_t timer_seq;
    uint32_t timer_seq_inverse;
    uint32_t timer_reti;
    uint32_t timer_t4;
} fm1_foreground_journal_t;

typedef char fm1_foreground_journal_size_is_44[
    (sizeof(fm1_foreground_journal_t) == 44u) ? 1 : -1];

static inline uint32_t fm1_foreground_journal_header_valid(
    const fm1_foreground_journal_t *record)
{
    return record && record->magic == FM1_FOREGROUND_JOURNAL_MAGIC &&
           record->version == FM1_FOREGROUND_JOURNAL_VERSION &&
           record->byte_size == sizeof(fm1_foreground_journal_t) &&
           record->boot_id != 0u && record->boot_id != FM1_FOREGROUND_JOURNAL_UNKNOWN;
}

static inline uint32_t fm1_foreground_journal_poll_valid(
    const fm1_foreground_journal_t *record)
{
    return fm1_foreground_journal_header_valid(record) &&
           !(record->poll_seq & 1u) && record->poll_seq_inverse == ~record->poll_seq &&
           record->poll_t4 != FM1_FOREGROUND_JOURNAL_UNKNOWN;
}

static inline uint32_t fm1_foreground_journal_timer_valid(
    const fm1_foreground_journal_t *record)
{
    return fm1_foreground_journal_header_valid(record) &&
           !(record->timer_seq & 1u) && record->timer_seq_inverse == ~record->timer_seq &&
           record->timer_reti != FM1_FOREGROUND_JOURNAL_UNKNOWN &&
           record->timer_t4 != FM1_FOREGROUND_JOURNAL_UNKNOWN;
}

static inline uint32_t fm1_foreground_diag89_request_valid(const uint8_t *body,
                                                            uint32_t size)
{
    (void)body;
    return size == 0u;
}

static inline uint32_t fm1_foreground_diag89_decode(
    const uint8_t *bytes, uint32_t size, uint32_t out[FM1_FOREGROUND_DIAG89_WORDS])
{
    uint32_t i;
    if (!bytes || !out || size != FM1_FOREGROUND_DIAG89_BYTES) return 0u;
    for (i = 0u; i < FM1_FOREGROUND_DIAG89_WORDS; ++i) {
        uint32_t at = i * 4u;
        out[i] = (uint32_t)bytes[at] |
                 ((uint32_t)bytes[at + 1u] << 8) |
                 ((uint32_t)bytes[at + 2u] << 16) |
                 ((uint32_t)bytes[at + 3u] << 24);
    }
    if (out[0] != FM1_FOREGROUND_DIAG89_VERSION || (out[1] & ~3u) ||
        (out[6] & ~3u)) return 0u;
    if ((out[1] && (out[2] == 0u || out[2] == FM1_FOREGROUND_JOURNAL_UNKNOWN)) ||
        (out[6] && (out[7] == 0u || out[7] == FM1_FOREGROUND_JOURNAL_UNKNOWN)))
        return 0u;
    if (!(out[1] & FM1_FOREGROUND_JOURNAL_POLL_VALID) &&
        out[3] != FM1_FOREGROUND_JOURNAL_UNKNOWN) return 0u;
    if (!(out[1] & FM1_FOREGROUND_JOURNAL_TIMER_VALID) &&
        (out[4] != FM1_FOREGROUND_JOURNAL_UNKNOWN ||
         out[5] != FM1_FOREGROUND_JOURNAL_UNKNOWN)) return 0u;
    if ((out[1] & FM1_FOREGROUND_JOURNAL_POLL_VALID) &&
        out[3] == FM1_FOREGROUND_JOURNAL_UNKNOWN) return 0u;
    if ((out[1] & FM1_FOREGROUND_JOURNAL_TIMER_VALID) &&
        (out[4] == FM1_FOREGROUND_JOURNAL_UNKNOWN ||
         out[5] == FM1_FOREGROUND_JOURNAL_UNKNOWN)) return 0u;
    if (!(out[6] & FM1_FOREGROUND_JOURNAL_POLL_VALID) &&
        out[8] != FM1_FOREGROUND_JOURNAL_UNKNOWN) return 0u;
    if (!(out[6] & FM1_FOREGROUND_JOURNAL_TIMER_VALID) &&
        (out[9] != FM1_FOREGROUND_JOURNAL_UNKNOWN ||
         out[10] != FM1_FOREGROUND_JOURNAL_UNKNOWN)) return 0u;
    if ((out[6] & FM1_FOREGROUND_JOURNAL_POLL_VALID) &&
        out[8] == FM1_FOREGROUND_JOURNAL_UNKNOWN) return 0u;
    if ((out[6] & FM1_FOREGROUND_JOURNAL_TIMER_VALID) &&
        (out[9] == FM1_FOREGROUND_JOURNAL_UNKNOWN ||
         out[10] == FM1_FOREGROUND_JOURNAL_UNKNOWN)) return 0u;
    return 1u;
}

#endif
