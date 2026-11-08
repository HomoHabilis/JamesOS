/* SPDX-License-Identifier: GPL-3.0-only
 * Default-off, bounded per-half source-scheduler trace.
 *
 * FM1_RUNTIME_CHUNK_PROFILE=1 is diagnostic-build-only. The platform owner
 * must call reset() during startup before audio hooks can run, and from the
 * existing quiescent/IRQ-safe 0x61 counter-reset path. With the macro unset or
 * zero, these are empty inline stubs and the trace pool is not allocated.
 * Only the legacy source schedule with pipeline mode 0 is eligible for a
 * complete record. Queue, reverse, and hybrid dispatches are unsupported, and
 * diagnostic trace builds cannot qualify callback timing.
 */
#pragma once
#include <stdint.h>

#ifndef FM1_RUNTIME_CHUNK_PROFILE
#define FM1_RUNTIME_CHUNK_PROFILE 0
#endif

#define FM1_RUNTIME_TRACE_SCHEMA 1u
#define FM1_RUNTIME_TRACE_PAGE_WORDS 8u
#define FM1_RUNTIME_TRACE_CHUNKS 8u
#define FM1_RUNTIME_TRACE_JOBS_PER_CHUNK 5u
#define FM1_RUNTIME_TRACE_TIMER5_EVENTS 72u
#define FM1_RUNTIME_TRACE_TOTAL_WORDS 504u
#define FM1_RUNTIME_TRACE_PAGE_COUNT 63u

/* Markers are timestamps, not durations. Chunk begin is recorded by
 * chunk_begin(), and commit end is recorded by commit() after each 32-frame
 * block-done call. All timestamps use the shared wrapping 24 MHz T4 clock.
 * Unsigned T4 timestamps wrap naturally; consumers must subtract modulo 2^32.
 */
typedef enum {
    FM1_RUNTIME_TRACE_MARK_ASSIGN_END = 1u,
    FM1_RUNTIME_TRACE_MARK_SUBMIT_BEGIN = 2u,
    FM1_RUNTIME_TRACE_MARK_SUBMIT_END = 3u,
    FM1_RUNTIME_TRACE_MARK_CPU0_BATCH_BEGIN = 4u,
    FM1_RUNTIME_TRACE_MARK_CPU0_BATCH_END = 5u,
    FM1_RUNTIME_TRACE_MARK_WAIT_BEGIN = 6u,
    FM1_RUNTIME_TRACE_MARK_WAIT_END = 7u,
    FM1_RUNTIME_TRACE_MARK_MERGE_BEGIN = 8u,
    FM1_RUNTIME_TRACE_MARK_MERGE_END = 9u,
    FM1_RUNTIME_TRACE_MARK_POST_BEGIN = 10u,
    FM1_RUNTIME_TRACE_MARK_POST_END = 11u,
    FM1_RUNTIME_TRACE_MARK_COMMIT_BEGIN = 12u
} fm1_runtime_trace_marker_t;

enum {
    FM1_RUNTIME_TRACE_FLAG_VALID = 1u << 0,
    FM1_RUNTIME_TRACE_FLAG_COMPLETE = 1u << 1,
    FM1_RUNTIME_TRACE_FLAG_SAFE_JOINED = 1u << 2,
    FM1_RUNTIME_TRACE_FLAG_TRUNCATED = 1u << 3,
    FM1_RUNTIME_TRACE_FLAG_TIMER5_OVERFLOW = 1u << 4,
    FM1_RUNTIME_TRACE_FLAG_JOB_OVERFLOW = 1u << 5,
    FM1_RUNTIME_TRACE_FLAG_MISSING_MARKER = 1u << 6,
    FM1_RUNTIME_TRACE_FLAG_MISSING_JOB_END = 1u << 7,
    FM1_RUNTIME_TRACE_FLAG_BAD_OWNER = 1u << 8
};

#if FM1_RUNTIME_CHUNK_PROFILE
/* Caller hooks are deliberately explicit. The source owner records job
 * metadata before dispatch, then brackets the source call on its owning core.
 * half_seal() may only be called after the caller has independently proved a
 * safe worker join; this API never substitutes for worker ownership checks.
 * The platform calls half_seal() and page-0 reads with IRQs masked. reset() is
 * startup-only or quiescent/IRQ-safe. The module does not lock arbitrary
 * concurrent callers. */
void fm1_runtime_trace_reset(void);
void fm1_runtime_trace_half_begin(uint32_t start_tick);
void fm1_runtime_trace_chunk_begin(uint32_t job_count);
void fm1_runtime_trace_job_metadata(uint32_t index, uint32_t part,
                                    uint32_t lane_mask, uint32_t core,
                                    uint32_t predicted_ticks);
void fm1_runtime_trace_job_begin(uint32_t index, uint32_t core);
void fm1_runtime_trace_job_end(uint32_t index, uint32_t core);
void fm1_runtime_trace_mark(uint32_t marker);
void fm1_runtime_trace_timer5(uint32_t entry_tick,
                              uint32_t postscan_tick,
                              uint32_t end_tick);
void fm1_runtime_trace_commit(uint32_t end_tick);
void fm1_runtime_trace_half_seal(uint32_t full_ack_ticks,
                                 uint32_t callback_ticks);

/* Page zero freezes the current sealed bank using one control-word update.
 * Each call returns one fixed page of eight raw little-endian u32 words. Page
 * zero is the header; pages 1..62 are the remaining words in schema order.
 * Return 1 when a page is available, 0 for an invalid page or before reset. */
uint32_t fm1_runtime_trace_read_page(uint32_t page, uint32_t out[8]);

#if defined(FM1_RUNTIME_TRACE_HOST_TEST)
/* Deterministic clock injection for host-only ownership tests. */
void fm1_runtime_trace_test_set_tick(uint32_t tick);
#endif
#else
static inline void fm1_runtime_trace_reset(void) {}
static inline void fm1_runtime_trace_half_begin(uint32_t start_tick)
{ (void)start_tick; }
static inline void fm1_runtime_trace_chunk_begin(uint32_t job_count)
{ (void)job_count; }
static inline void fm1_runtime_trace_job_metadata(uint32_t index, uint32_t part,
    uint32_t lane_mask, uint32_t core, uint32_t predicted_ticks)
{
    (void)index; (void)part; (void)lane_mask; (void)core; (void)predicted_ticks;
}
static inline void fm1_runtime_trace_job_begin(uint32_t index, uint32_t core)
{ (void)index; (void)core; }
static inline void fm1_runtime_trace_job_end(uint32_t index, uint32_t core)
{ (void)index; (void)core; }
static inline void fm1_runtime_trace_mark(uint32_t marker) { (void)marker; }
static inline void fm1_runtime_trace_timer5(uint32_t entry_tick,
    uint32_t postscan_tick, uint32_t end_tick)
{
    (void)entry_tick; (void)postscan_tick; (void)end_tick;
}
static inline void fm1_runtime_trace_commit(uint32_t end_tick) { (void)end_tick; }
static inline void fm1_runtime_trace_half_seal(uint32_t full_ack_ticks,
    uint32_t callback_ticks)
{
    (void)full_ack_ticks; (void)callback_ticks;
}
static inline uint32_t fm1_runtime_trace_read_page(uint32_t page, uint32_t out[8])
{ (void)page; (void)out; return 0u; }
#endif

/* Frozen raw 0x8E page schema (all values are little-endian u32 on wire).
 * Page 0 words: 0 schema=1; 1 record flags; 2 generation; 3 full-half ACK
 * ticks; 4 callback ticks; 5 counts (attempted in bits 0..7, completed in
 * bits 8..15); 6 Timer5 count (bits 0..6, bit 31 means overflow); 7 half
 * start T4 tick. A valid page-0 record is always a complete, safely joined
 * half; incomplete attempts are never published.
 *
 * Pages 1..62 concatenate a 496-word payload. First are eight 15-word chunk
 * records, then forty 4-word job records, then seventy-two 3-word Timer5
 * records. Offsets below are payload-relative:
 *   chunks: 0 + chunk*15. Word 0 packs job_count in bits 0..7 and marker
 *     presence bits in bits 8..19 (marker N uses bit 7+N). Words 1..14 are
 *     T4 timestamps: chunk_begin, assign_end, submit_begin, submit_end,
 *     CPU0_batch_begin, CPU0_batch_end, wait_begin, wait_end, merge_begin,
 *     merge_end, post_begin, post_end, commit_begin, commit_end.
 *   jobs: 120 + (chunk*5 + job)*4. Word 0 packs part (bits 0..7), lane mask
 *     (8..15), expected core (16..17), observed begin core (18..19), and
 *     metadata/began/ended/bad-owner/bad-sequence flags (bits 20..24).
 *     Words 1..3 are predicted ticks, start T4 tick, end T4 tick.
 *   Timer5: 280 + event*3. Words are IRQ entry, after-scan, and IRQ end T4
 *     ticks. `timer5_count` is at most 72; on overflow bit 31 is set and only
 *     the first 72 entries are retained.
 *
 * `half_begin()` increments a wrapping u32 generation (first after reset is
 * 1). `chunk_begin()` and `mark()` timestamp at call entry; `commit(tick)` is
 * called once after each chunk's platform_audio_block_done returns, with its
 * end tick. `half_seal()` is called only after the caller has independently
 * proved all worker jobs safely joined; it publishes only a full eight-chunk
 * record with every required marker and every declared job begun/ended by its
 * expected owner. A page-0 read is available only after a sealed record exists;
 * it freezes that selected worst-complete record using a separate control
 * word. Thus the previous sealed bank remains readable even while an unsafe
 * later attempt is still writing its distinct scratch bank. `half_begin()` is
 * a no-op while frozen, so no later attempt can overwrite the snapshot. If
 * that in-flight attempt later reaches a safe join and calls half_seal(), the
 * scratch attempt is discarded without replacing the frozen record.
 * `read_page()` returns 0 for an invalid/unavailable page, before reset, or
 * before a sealed record exists.
 * Consumers must fetch page 0 first; pages 1..62 are readable only after that
 * freeze.
 */
