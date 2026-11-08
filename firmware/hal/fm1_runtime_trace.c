/* SPDX-License-Identifier: GPL-3.0-only
 * Default-off bounded per-half source scheduler trace.
 *
 * This file is included by platform.c in diagnostic builds. Keeping the two
 * banks in .pool makes them part of the platform's zeroed RAM pool and avoids
 * consuming the small BSS budget. The API never reads worker-owned records
 * until the caller has independently established a safe join and calls
 * half_seal().
 */
#include "fm1_runtime_trace.h"

#if FM1_RUNTIME_CHUNK_PROFILE

#include <stddef.h>

#if defined(FM1_DEVICE)
#include "fm1_time.h"
#define FM1_TRACE_STORE_ATTR __attribute__((section(".pool"), aligned(4), used))
#define FM1_TRACE_BARRIER() __asm__ volatile("csync" ::: "memory")
#else
#define FM1_TRACE_STORE_ATTR
#define FM1_TRACE_BARRIER() __asm__ volatile("" ::: "memory")
#endif

#define FM1_TRACE_MAGIC 0x52544331u /* RTC1 */

enum {
    FM1_TRACE_CTL_ACTIVE_BANK = 1u << 0,
    FM1_TRACE_CTL_SEALED_BANK = 1u << 1,
    FM1_TRACE_CTL_SEALED_VALID = 1u << 2,
    FM1_TRACE_CTL_RECORDING = 1u << 4,
    FM1_TRACE_CTL_CURRENT_SHIFT = 0u,
    FM1_TRACE_CTL_CURRENT_MASK = 0x0fu,
    FM1_TRACE_CTL_ERR_JOB_OVERFLOW = 1u << 4,
    FM1_TRACE_CTL_ERR_MARKER = 1u << 5,
    FM1_TRACE_CTL_ERR_SEQUENCE = 1u << 6,
    FM1_TRACE_CTL_ERR_CHUNK = 1u << 7
};

enum {
    FM1_TRACE_CTL_MAGIC = 0,
    FM1_TRACE_CTL_STATE = 1,
    FM1_TRACE_CTL_GENERATION = 2,
    FM1_TRACE_CTL_CURRENT_AND_ERRORS = 3,
    FM1_TRACE_CTL_FROZEN = 4,
    FM1_TRACE_CTL_WORDS = 5
};

enum {
    FM1_TRACE_HEADER_WORDS = 8,
    FM1_TRACE_CHUNK_WORDS = 15,
    FM1_TRACE_JOB_WORDS = 4,
    FM1_TRACE_TIMER5_WORDS = 3,
    FM1_TRACE_CHUNK_BASE = FM1_TRACE_HEADER_WORDS,
    FM1_TRACE_JOB_BASE = FM1_TRACE_HEADER_WORDS +
                         FM1_RUNTIME_TRACE_CHUNKS * FM1_TRACE_CHUNK_WORDS,
    FM1_TRACE_TIMER5_BASE = FM1_TRACE_JOB_BASE +
                            FM1_RUNTIME_TRACE_CHUNKS *
                            FM1_RUNTIME_TRACE_JOBS_PER_CHUNK *
                            FM1_TRACE_JOB_WORDS
};

enum {
    FM1_TRACE_COUNTS_ATTEMPTED_MASK = 0xffu,
    FM1_TRACE_COUNTS_COMPLETED_SHIFT = 8u,
    FM1_TRACE_COUNTS_COMPLETED_MASK = 0xffu << 8u,
    FM1_TRACE_TIMER_COUNT_MASK = 0x7fu,
    FM1_TRACE_TIMER_OVERFLOW = 1u << 31,
    FM1_TRACE_MARKER_MASK = 0x0fffu,
    FM1_TRACE_MARKER_SHIFT = 8u,
    FM1_TRACE_JOB_PART_MASK = 0xffu,
    FM1_TRACE_JOB_LANE_SHIFT = 8u,
    FM1_TRACE_JOB_CORE_SHIFT = 16u,
    FM1_TRACE_JOB_BEGIN_CORE_SHIFT = 18u,
    FM1_TRACE_JOB_HAS_METADATA = 1u << 20,
    FM1_TRACE_JOB_BEGAN = 1u << 21,
    FM1_TRACE_JOB_ENDED = 1u << 22,
    FM1_TRACE_JOB_BAD_OWNER = 1u << 23,
    FM1_TRACE_JOB_BAD_SEQUENCE = 1u << 24,
    FM1_TRACE_API_ERROR_MASK = FM1_TRACE_CTL_ERR_JOB_OVERFLOW |
                               FM1_TRACE_CTL_ERR_MARKER |
                               FM1_TRACE_CTL_ERR_SEQUENCE |
                               FM1_TRACE_CTL_ERR_CHUNK
};

typedef struct {
    volatile uint32_t control[FM1_TRACE_CTL_WORDS];
    volatile uint32_t bank[2][FM1_RUNTIME_TRACE_TOTAL_WORDS];
} fm1_runtime_trace_store_t;

static fm1_runtime_trace_store_t fm1_runtime_trace_store FM1_TRACE_STORE_ATTR;
typedef char fm1_runtime_trace_store_fits_pool_budget[
    (sizeof(fm1_runtime_trace_store_t) == 4052u &&
     sizeof(fm1_runtime_trace_store_t) <= 4096u) ? 1 : -1];
typedef char fm1_runtime_trace_schema_has_expected_words[
    (FM1_TRACE_TIMER5_BASE - FM1_TRACE_HEADER_WORDS == 280u &&
     FM1_TRACE_TIMER5_BASE +
         FM1_RUNTIME_TRACE_TIMER5_EVENTS * FM1_TRACE_TIMER5_WORDS ==
         FM1_RUNTIME_TRACE_TOTAL_WORDS) ? 1 : -1];

#if defined(FM1_RUNTIME_TRACE_HOST_TEST)
static volatile uint32_t fm1_runtime_trace_test_tick;
void fm1_runtime_trace_test_set_tick(uint32_t tick)
{
    fm1_runtime_trace_test_tick = tick;
}
#endif

static uint32_t fm1_runtime_trace_now(void)
{
#if defined(FM1_RUNTIME_TRACE_HOST_TEST)
    return fm1_runtime_trace_test_tick;
#elif defined(FM1_DEVICE)
    return fm1_ticks();
#else
    return 0u;
#endif
}

static uint32_t fm1_runtime_trace_state(void)
{
    return fm1_runtime_trace_store.control[FM1_TRACE_CTL_STATE];
}

static uint32_t fm1_runtime_trace_ready(void)
{
    return fm1_runtime_trace_store.control[FM1_TRACE_CTL_MAGIC] ==
           FM1_TRACE_MAGIC;
}

static uint32_t fm1_runtime_trace_recording(void)
{
    return (fm1_runtime_trace_state() & FM1_TRACE_CTL_RECORDING) != 0u;
}

static uint32_t fm1_runtime_trace_active_bank(void)
{
    return fm1_runtime_trace_state() & FM1_TRACE_CTL_ACTIVE_BANK;
}

static uint32_t fm1_runtime_trace_current_chunk_plus_one(void)
{
    return fm1_runtime_trace_store.control[
        FM1_TRACE_CTL_CURRENT_AND_ERRORS] & FM1_TRACE_CTL_CURRENT_MASK;
}

static void fm1_runtime_trace_set_api_error(uint32_t error)
{
    volatile uint32_t *word = &fm1_runtime_trace_store.control[
        FM1_TRACE_CTL_CURRENT_AND_ERRORS];
    *word |= error;
    FM1_TRACE_BARRIER();
}

static volatile uint32_t *fm1_runtime_trace_active_words(void)
{
    return fm1_runtime_trace_store.bank[fm1_runtime_trace_active_bank()];
}

static uint32_t fm1_runtime_trace_chunk_index(void)
{
    uint32_t current = fm1_runtime_trace_current_chunk_plus_one();
    return current ? current - 1u : FM1_RUNTIME_TRACE_CHUNKS;
}

static volatile uint32_t *fm1_runtime_trace_chunk_record(
    volatile uint32_t *bank, uint32_t chunk)
{
    return &bank[FM1_TRACE_CHUNK_BASE + chunk * FM1_TRACE_CHUNK_WORDS];
}

static volatile uint32_t *fm1_runtime_trace_job_record(
    volatile uint32_t *bank, uint32_t chunk, uint32_t job)
{
    uint32_t index = chunk * FM1_RUNTIME_TRACE_JOBS_PER_CHUNK + job;
    return &bank[FM1_TRACE_JOB_BASE + index * FM1_TRACE_JOB_WORDS];
}

static uint32_t fm1_runtime_trace_expected_jobs(
    volatile uint32_t *chunk_record)
{
    return chunk_record[0] & 0xffu;
}

static uint32_t fm1_runtime_trace_required_marker_mask(void)
{
    return FM1_TRACE_MARKER_MASK;
}

static uint32_t fm1_runtime_trace_job_records_valid(
    volatile uint32_t *bank, uint32_t chunk, uint32_t jobs,
    uint32_t *out_flags)
{
    uint32_t i;
    for (i = 0u; i < jobs; ++i) {
        volatile uint32_t *record =
            fm1_runtime_trace_job_record(bank, chunk, i);
        uint32_t meta = record[0];
        uint32_t expected_core = (meta >> FM1_TRACE_JOB_CORE_SHIFT) & 3u;
        uint32_t begin_core =
            (meta >> FM1_TRACE_JOB_BEGIN_CORE_SHIFT) & 3u;
        if (!(meta & FM1_TRACE_JOB_HAS_METADATA)) {
            *out_flags |= FM1_RUNTIME_TRACE_FLAG_MISSING_JOB_END;
            return 0u;
        }
        if (!(meta & FM1_TRACE_JOB_BEGAN) ||
            !(meta & FM1_TRACE_JOB_ENDED) ||
            (meta & FM1_TRACE_JOB_BAD_SEQUENCE)) {
            *out_flags |= FM1_RUNTIME_TRACE_FLAG_MISSING_JOB_END;
            return 0u;
        }
        if ((meta & FM1_TRACE_JOB_BAD_OWNER) ||
            begin_core != expected_core) {
            *out_flags |= FM1_RUNTIME_TRACE_FLAG_BAD_OWNER;
            return 0u;
        }
    }
    return 1u;
}

static uint32_t fm1_runtime_trace_candidate_score(
    volatile uint32_t *bank)
{
    uint32_t ack = bank[3];
    uint32_t callback = bank[4];
    return ack > callback ? ack : callback;
}

void fm1_runtime_trace_reset(void)
{
    volatile uint32_t *control = fm1_runtime_trace_store.control;
    /* Quiescent only. The payload banks need not be cleared: without a valid
     * sealed-bank bit, stale words are inaccessible to the page reader. */
    control[FM1_TRACE_CTL_MAGIC] = 0u;
    FM1_TRACE_BARRIER();
    control[FM1_TRACE_CTL_STATE] = 0u;
    control[FM1_TRACE_CTL_GENERATION] = 0u;
    control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] = 0u;
    control[FM1_TRACE_CTL_FROZEN] = 0u;
    FM1_TRACE_BARRIER();
    control[FM1_TRACE_CTL_MAGIC] = FM1_TRACE_MAGIC;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_half_begin(uint32_t start_tick)
{
    volatile uint32_t *control = fm1_runtime_trace_store.control;
    volatile uint32_t *bank;
    uint32_t state, active, generation, i;
    if (!fm1_runtime_trace_ready()) return;
    state = fm1_runtime_trace_state();
    if (control[FM1_TRACE_CTL_FROZEN]) return;
    if (state & FM1_TRACE_CTL_RECORDING) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_SEQUENCE);
        return;
    }
    active = state & FM1_TRACE_CTL_ACTIVE_BANK;
    bank = fm1_runtime_trace_store.bank[active];
    generation = control[FM1_TRACE_CTL_GENERATION] + 1u;
    control[FM1_TRACE_CTL_GENERATION] = generation;
    for (i = 0u; i < FM1_TRACE_HEADER_WORDS; ++i) bank[i] = 0u;
    bank[0] = FM1_RUNTIME_TRACE_SCHEMA;
    bank[2] = generation;
    bank[7] = start_tick;
    control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] = 0u;
    FM1_TRACE_BARRIER();
    control[FM1_TRACE_CTL_STATE] = state | FM1_TRACE_CTL_RECORDING;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_chunk_begin(uint32_t job_count)
{
    volatile uint32_t *bank;
    volatile uint32_t *chunk_record;
    volatile uint32_t *control;
    uint32_t chunk, attempted, jobs, i;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    control = fm1_runtime_trace_store.control;
    if (control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &
        FM1_TRACE_CTL_CURRENT_MASK) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_SEQUENCE);
        return;
    }
    bank = fm1_runtime_trace_active_words();
    attempted = bank[5] & FM1_TRACE_COUNTS_ATTEMPTED_MASK;
    if (attempted >= FM1_RUNTIME_TRACE_CHUNKS) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_CHUNK);
        return;
    }
    chunk = attempted;
    jobs = job_count;
    if (jobs > FM1_RUNTIME_TRACE_JOBS_PER_CHUNK) {
        jobs = FM1_RUNTIME_TRACE_JOBS_PER_CHUNK;
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_JOB_OVERFLOW);
    }
    chunk_record = fm1_runtime_trace_chunk_record(bank, chunk);
    for (i = 0u; i < FM1_TRACE_CHUNK_WORDS; ++i) chunk_record[i] = 0u;
    chunk_record[0] = jobs;
    chunk_record[1] = fm1_runtime_trace_now();
    for (i = 0u; i < FM1_RUNTIME_TRACE_JOBS_PER_CHUNK; ++i)
        fm1_runtime_trace_job_record(bank, chunk, i)[0] = 0u;
    bank[5] = (bank[5] & ~FM1_TRACE_COUNTS_ATTEMPTED_MASK) | (attempted + 1u);
    control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] =
        (control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] & ~FM1_TRACE_CTL_CURRENT_MASK) |
        (chunk + 1u);
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_job_metadata(uint32_t index, uint32_t part,
                                    uint32_t lane_mask, uint32_t core,
                                    uint32_t predicted_ticks)
{
    volatile uint32_t *bank, *chunk_record, *record;
    uint32_t chunk, jobs, meta;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    chunk = fm1_runtime_trace_chunk_index();
    if (chunk >= FM1_RUNTIME_TRACE_CHUNKS) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_SEQUENCE);
        return;
    }
    bank = fm1_runtime_trace_active_words();
    chunk_record = fm1_runtime_trace_chunk_record(bank, chunk);
    jobs = fm1_runtime_trace_expected_jobs(chunk_record);
    if (index >= jobs || index >= FM1_RUNTIME_TRACE_JOBS_PER_CHUNK ||
        part > 3u || lane_mask > 0xffu || core > 1u) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_JOB_OVERFLOW);
        return;
    }
    record = fm1_runtime_trace_job_record(bank, chunk, index);
    meta = (part & 0xffu) | ((lane_mask & 0xffu) << FM1_TRACE_JOB_LANE_SHIFT) |
           ((core & 3u) << FM1_TRACE_JOB_CORE_SHIFT) |
           FM1_TRACE_JOB_HAS_METADATA;
    if (record[0] & FM1_TRACE_JOB_HAS_METADATA)
        meta |= FM1_TRACE_JOB_BAD_SEQUENCE;
    record[1] = predicted_ticks;
    record[2] = 0u;
    record[3] = 0u;
    FM1_TRACE_BARRIER();
    record[0] = meta;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_job_begin(uint32_t index, uint32_t core)
{
    volatile uint32_t *bank, *record;
    uint32_t chunk, jobs, meta, expected;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    chunk = fm1_runtime_trace_chunk_index();
    if (chunk >= FM1_RUNTIME_TRACE_CHUNKS || index >= FM1_RUNTIME_TRACE_JOBS_PER_CHUNK)
        return;
    bank = fm1_runtime_trace_active_words();
    jobs = fm1_runtime_trace_expected_jobs(fm1_runtime_trace_chunk_record(bank, chunk));
    if (index >= jobs) return;
    record = fm1_runtime_trace_job_record(bank, chunk, index);
    meta = record[0];
    if (!(meta & FM1_TRACE_JOB_HAS_METADATA) || core > 1u ||
        (meta & (FM1_TRACE_JOB_BEGAN | FM1_TRACE_JOB_ENDED))) {
        record[0] = meta | FM1_TRACE_JOB_BAD_SEQUENCE;
        FM1_TRACE_BARRIER();
        return;
    }
    expected = (meta >> FM1_TRACE_JOB_CORE_SHIFT) & 3u;
    meta &= ~(3u << FM1_TRACE_JOB_BEGIN_CORE_SHIFT);
    meta |= (core & 3u) << FM1_TRACE_JOB_BEGIN_CORE_SHIFT;
    if (core != expected) meta |= FM1_TRACE_JOB_BAD_OWNER;
    record[2] = fm1_runtime_trace_now();
    FM1_TRACE_BARRIER();
    record[0] = meta | FM1_TRACE_JOB_BEGAN;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_job_end(uint32_t index, uint32_t core)
{
    volatile uint32_t *bank, *record;
    uint32_t chunk, jobs, meta, expected, begin_core;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    chunk = fm1_runtime_trace_chunk_index();
    if (chunk >= FM1_RUNTIME_TRACE_CHUNKS || index >= FM1_RUNTIME_TRACE_JOBS_PER_CHUNK)
        return;
    bank = fm1_runtime_trace_active_words();
    jobs = fm1_runtime_trace_expected_jobs(fm1_runtime_trace_chunk_record(bank, chunk));
    if (index >= jobs) return;
    record = fm1_runtime_trace_job_record(bank, chunk, index);
    meta = record[0];
    if (!(meta & FM1_TRACE_JOB_BEGAN) ||
        (meta & (FM1_TRACE_JOB_ENDED | FM1_TRACE_JOB_BAD_SEQUENCE)) || core > 1u) {
        record[0] = meta | FM1_TRACE_JOB_BAD_SEQUENCE;
        FM1_TRACE_BARRIER();
        return;
    }
    expected = (meta >> FM1_TRACE_JOB_CORE_SHIFT) & 3u;
    begin_core = (meta >> FM1_TRACE_JOB_BEGIN_CORE_SHIFT) & 3u;
    if (core != expected || begin_core != expected)
        meta |= FM1_TRACE_JOB_BAD_OWNER;
    record[3] = fm1_runtime_trace_now();
    FM1_TRACE_BARRIER();
    record[0] = meta | FM1_TRACE_JOB_ENDED;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_mark(uint32_t marker)
{
    volatile uint32_t *bank, *record;
    uint32_t chunk, marker_bit;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    chunk = fm1_runtime_trace_chunk_index();
    if (chunk >= FM1_RUNTIME_TRACE_CHUNKS || marker < FM1_RUNTIME_TRACE_MARK_ASSIGN_END ||
        marker > FM1_RUNTIME_TRACE_MARK_COMMIT_BEGIN) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_MARKER);
        return;
    }
    bank = fm1_runtime_trace_active_words();
    record = fm1_runtime_trace_chunk_record(bank, chunk);
    marker_bit = 1u << (marker - 1u);
    if (record[0] & (marker_bit << FM1_TRACE_MARKER_SHIFT)) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_MARKER);
        return;
    }
    record[marker + 1u] = fm1_runtime_trace_now();
    FM1_TRACE_BARRIER();
    record[0] |= marker_bit << FM1_TRACE_MARKER_SHIFT;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_timer5(uint32_t entry_tick,
                              uint32_t postscan_tick,
                              uint32_t end_tick)
{
    volatile uint32_t *bank;
    volatile uint32_t *count_word;
    uint32_t count, offset;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    bank = fm1_runtime_trace_active_words();
    count_word = &bank[6];
    count = *count_word;
    if (count & FM1_TRACE_TIMER_OVERFLOW) return;
    count &= FM1_TRACE_TIMER_COUNT_MASK;
    if (count >= FM1_RUNTIME_TRACE_TIMER5_EVENTS) {
        *count_word = FM1_TRACE_TIMER_OVERFLOW | count;
        FM1_TRACE_BARRIER();
        return;
    }
    offset = FM1_TRACE_TIMER5_BASE + count * FM1_TRACE_TIMER5_WORDS;
    bank[offset] = entry_tick;
    bank[offset + 1u] = postscan_tick;
    bank[offset + 2u] = end_tick;
    FM1_TRACE_BARRIER();
    *count_word = count + 1u;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_commit(uint32_t end_tick)
{
    volatile uint32_t *bank, *chunk_record, *control;
    uint32_t chunk, jobs, completed, attempted, flags = 0u, valid;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    chunk = fm1_runtime_trace_chunk_index();
    if (chunk >= FM1_RUNTIME_TRACE_CHUNKS) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_CHUNK);
        return;
    }
    bank = fm1_runtime_trace_active_words();
    control = fm1_runtime_trace_store.control;
    chunk_record = fm1_runtime_trace_chunk_record(bank, chunk);
    if (!(chunk_record[0] &
          ((1u << (FM1_RUNTIME_TRACE_MARK_COMMIT_BEGIN - 1u)) <<
           FM1_TRACE_MARKER_SHIFT))) {
        fm1_runtime_trace_set_api_error(FM1_TRACE_CTL_ERR_MARKER);
        return;
    }
    chunk_record[14] = end_tick;
    jobs = fm1_runtime_trace_expected_jobs(chunk_record);
    valid = fm1_runtime_trace_job_records_valid(bank, chunk, jobs, &flags);
    if ((chunk_record[0] & (FM1_TRACE_MARKER_MASK << FM1_TRACE_MARKER_SHIFT)) !=
        (fm1_runtime_trace_required_marker_mask() << FM1_TRACE_MARKER_SHIFT))
        flags |= FM1_RUNTIME_TRACE_FLAG_MISSING_MARKER;
    if (control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] & FM1_TRACE_API_ERROR_MASK)
        flags |= FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (!valid) flags |= FM1_RUNTIME_TRACE_FLAG_MISSING_JOB_END;
    attempted = bank[5] & FM1_TRACE_COUNTS_ATTEMPTED_MASK;
    completed = (bank[5] & FM1_TRACE_COUNTS_COMPLETED_MASK) >>
                FM1_TRACE_COUNTS_COMPLETED_SHIFT;
    if (attempted != completed + 1u) flags |= FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (!flags) {
        bank[5] = (bank[5] & ~FM1_TRACE_COUNTS_COMPLETED_MASK) |
                  ((completed + 1u) << FM1_TRACE_COUNTS_COMPLETED_SHIFT);
    } else {
        bank[1] |= flags;
    }
    FM1_TRACE_BARRIER();
    control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &= ~FM1_TRACE_CTL_CURRENT_MASK;
    FM1_TRACE_BARRIER();
}

void fm1_runtime_trace_half_seal(uint32_t full_ack_ticks,
                                 uint32_t callback_ticks)
{
    volatile uint32_t *control = fm1_runtime_trace_store.control;
    volatile uint32_t *bank;
    uint32_t state, active, sealed, counts, attempted, completed;
    uint32_t timer_count, timer_overflow, flags, valid, replace;
    uint32_t sealed_valid;
    if (!fm1_runtime_trace_ready() || !fm1_runtime_trace_recording()) return;
    state = fm1_runtime_trace_state();
    active = state & FM1_TRACE_CTL_ACTIVE_BANK;
    sealed = (state & FM1_TRACE_CTL_SEALED_BANK) >> 1u;
    sealed_valid = (state & FM1_TRACE_CTL_SEALED_VALID) != 0u;
    if (control[FM1_TRACE_CTL_FROZEN]) {
        /* A reader may freeze the previous sealed bank while this scratch
         * bank is still active. The caller only reaches this point after its
         * safe-join gate; discard scratch without changing the frozen bank. */
        control[FM1_TRACE_CTL_STATE] = state & ~FM1_TRACE_CTL_RECORDING;
        control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &= ~FM1_TRACE_CTL_CURRENT_MASK;
        FM1_TRACE_BARRIER();
        return;
    }
    bank = fm1_runtime_trace_store.bank[active];
    FM1_TRACE_BARRIER(); /* caller has proved worker join before calling */
    counts = bank[5];
    attempted = counts & FM1_TRACE_COUNTS_ATTEMPTED_MASK;
    completed = (counts & FM1_TRACE_COUNTS_COMPLETED_MASK) >>
                FM1_TRACE_COUNTS_COMPLETED_SHIFT;
    timer_count = bank[6];
    timer_overflow = (timer_count & FM1_TRACE_TIMER_OVERFLOW) != 0u;
    timer_count &= FM1_TRACE_TIMER_COUNT_MASK;
    flags = bank[1] & (FM1_RUNTIME_TRACE_FLAG_TRUNCATED |
                       FM1_RUNTIME_TRACE_FLAG_TIMER5_OVERFLOW |
                       FM1_RUNTIME_TRACE_FLAG_JOB_OVERFLOW |
                       FM1_RUNTIME_TRACE_FLAG_MISSING_MARKER |
                       FM1_RUNTIME_TRACE_FLAG_MISSING_JOB_END |
                       FM1_RUNTIME_TRACE_FLAG_BAD_OWNER);
    if (control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &
        (FM1_TRACE_CTL_ERR_JOB_OVERFLOW))
        flags |= FM1_RUNTIME_TRACE_FLAG_JOB_OVERFLOW | FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &
        (FM1_TRACE_CTL_ERR_MARKER))
        flags |= FM1_RUNTIME_TRACE_FLAG_MISSING_MARKER | FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &
        (FM1_TRACE_CTL_ERR_SEQUENCE | FM1_TRACE_CTL_ERR_CHUNK))
        flags |= FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (timer_overflow) flags |= FM1_RUNTIME_TRACE_FLAG_TIMER5_OVERFLOW |
                                 FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (attempted != FM1_RUNTIME_TRACE_CHUNKS ||
        completed != FM1_RUNTIME_TRACE_CHUNKS ||
        fm1_runtime_trace_current_chunk_plus_one() != 0u)
        flags |= FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    if (timer_count > FM1_RUNTIME_TRACE_TIMER5_EVENTS)
        flags |= FM1_RUNTIME_TRACE_FLAG_TIMER5_OVERFLOW | FM1_RUNTIME_TRACE_FLAG_TRUNCATED;
    valid = (flags == 0u && attempted == FM1_RUNTIME_TRACE_CHUNKS &&
             completed == FM1_RUNTIME_TRACE_CHUNKS && !timer_overflow);
    bank[3] = full_ack_ticks;
    bank[4] = callback_ticks;
    bank[5] = (attempted & 0xffu) | ((completed & 0xffu) << 8u);
    bank[6] = timer_count | (timer_overflow ? FM1_TRACE_TIMER_OVERFLOW : 0u);
    bank[1] = flags | FM1_RUNTIME_TRACE_FLAG_SAFE_JOINED;
    if (valid) bank[1] |= FM1_RUNTIME_TRACE_FLAG_VALID |
                          FM1_RUNTIME_TRACE_FLAG_COMPLETE;
    FM1_TRACE_BARRIER();
    replace = valid && (!sealed_valid ||
        fm1_runtime_trace_candidate_score(bank) >
        fm1_runtime_trace_candidate_score(fm1_runtime_trace_store.bank[sealed]));
    if (replace) {
        state &= ~(FM1_TRACE_CTL_ACTIVE_BANK | FM1_TRACE_CTL_SEALED_BANK);
        state |= ((active & 1u) << 1u) | (active ^ 1u);
        state |= FM1_TRACE_CTL_SEALED_VALID;
        control[FM1_TRACE_CTL_STATE] = state;
    }
    state = control[FM1_TRACE_CTL_STATE] & ~FM1_TRACE_CTL_RECORDING;
    control[FM1_TRACE_CTL_STATE] = state;
    control[FM1_TRACE_CTL_CURRENT_AND_ERRORS] &= ~FM1_TRACE_CTL_CURRENT_MASK;
    FM1_TRACE_BARRIER();
}

uint32_t fm1_runtime_trace_read_page(uint32_t page, uint32_t out[8])
{
    volatile uint32_t *bank;
    uint32_t state, sealed, start, i;
    if (!out || page >= FM1_RUNTIME_TRACE_PAGE_COUNT ||
        !fm1_runtime_trace_ready()) return 0u;
    state = fm1_runtime_trace_state();
    if (!(state & FM1_TRACE_CTL_SEALED_VALID)) return 0u;
    if (page == 0u) {
        if (!fm1_runtime_trace_store.control[FM1_TRACE_CTL_FROZEN]) {
            FM1_TRACE_BARRIER();
            /* Freeze only future writes. The live scratch bank can still be
             * written by a worker, but the sealed bank is distinct and is
             * never swapped after this point. This separate aligned word
             * also avoids a read-modify-write race with the audio ISR. */
            fm1_runtime_trace_store.control[FM1_TRACE_CTL_FROZEN] = 1u;
            FM1_TRACE_BARRIER();
        }
    } else if (!fm1_runtime_trace_store.control[FM1_TRACE_CTL_FROZEN]) {
        return 0u;
    }
    sealed = (state & FM1_TRACE_CTL_SEALED_BANK) >> 1u;
    bank = fm1_runtime_trace_store.bank[sealed];
    start = page * FM1_RUNTIME_TRACE_PAGE_WORDS;
    for (i = 0u; i < FM1_RUNTIME_TRACE_PAGE_WORDS; ++i)
        out[i] = bank[start + i];
    FM1_TRACE_BARRIER();
    return 1u;
}

#endif /* FM1_RUNTIME_CHUNK_PROFILE */
