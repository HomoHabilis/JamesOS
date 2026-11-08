/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded private MIDI test protocol; all work runs in the main loop.
 * Session upload changes RAM only. It never saves or writes flash. */
#include "instrument_math.h"
#include "midi_clock_diag.h"
#include "instrument_pipeline_policy.h"
#include "instrument_failure_job_profile.h"
#include "instrument_worker_diag87.h"
#ifndef FM1_DEVELOPMENT_DIAGNOSTICS
#define FM1_DEVELOPMENT_DIAGNOSTICS 1
#endif
#include "instrument_tail_diag88.h"
#include "../braids/braids_interp_math.h"
#include "../hal/fm1_cpu1_snapshot.h"
#include "../hal/fm1_cpu1_stack_march.h"
#if defined(DINGHY_HOST) || defined(DINGHY_WEB)
#if !defined(BR_DIAGNOSTIC_TEST)
static uint32_t br_diagnostic_host_ticks;
static uint32_t fm1_ticks(void) { return ++br_diagnostic_host_ticks; }
#endif
#else
#include "../hal/fm1_time.h"
#endif
#include "../hal/fm1_fault_diag.h"
#include "../hal/fm1_c1_stack_guard.h"
#include "../hal/fm1_ram_code_guard.h"
#include "../hal/fm1_cpu1_smoke.h"
#include "../hal/fm1_cpu1_autostart_policy.h"
#if defined(FM1_DEVICE)
#include "../hal/fm1_cpu1_worker.h"
#endif
#define BR_DIAGNOSTIC_SESSION_CHUNKS ((sizeof(br_session_t) + 31u) / 32u)
#define BR_DIAGNOSTIC_CHUNK_WORDS ((BR_DIAGNOSTIC_SESSION_CHUNKS + 31u) / 32u)
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static br_session_t br_diagnostic_session __attribute__((section(".pool"), aligned(4)));
#else
static br_session_t br_diagnostic_session;
#endif
static uint32_t br_diagnostic_chunks[BR_DIAGNOSTIC_CHUNK_WORDS];
static uint32_t br_diagnostic_upload_length;
static uint32_t br_diagnostic_snapshot_valid;
#if FM1_DEVELOPMENT_DIAGNOSTICS
static volatile uint32_t br_diagnostic_armed;
#define br_diagnostic_disarm() (br_diagnostic_armed = 0u)
static volatile uint32_t br_diagnostic_deadline;
static volatile uint32_t br_diagnostic_pass_limit_ticks;
static uint32_t br_diagnostic_overload_run;
#else
#define br_diagnostic_armed 0u
#define br_diagnostic_disarm() ((void)0)
#endif
static volatile uint32_t br_diagnostic_abort;
static volatile uint32_t br_diagnostic_seed = 0x13579bdfu;
#if FM1_DEVELOPMENT_DIAGNOSTICS
static volatile uint32_t br_diagnostic_profile[4];
static volatile uint32_t br_diagnostic_detail[4];
#endif
#if defined(BR_DIAGNOSTIC_TEST) && defined(BR_SPLIT_AUDIO)
static uint32_t br_diagnostic_test_inject_restore_fault;
extern void br_engine_pipeline_test_uncontained_fault_latch(void);
#endif

#if FM1_DEVELOPMENT_DIAGNOSTICS
static void br_diagnostic_audio_guard(void)
{
    if (!br_diagnostic_armed) return;
    if (fm1_audio_metrics.last_ticks > br_diagnostic_pass_limit_ticks)
        ++br_diagnostic_overload_run;
    else br_diagnostic_overload_run = 0;
    if ((int32_t)(fm1_ms - br_diagnostic_deadline) >= 0 || br_diagnostic_overload_run >= 8u) {
        br_diagnostic_abort = br_diagnostic_overload_run >= 8u ? 2u : 1u;
        br_diagnostic_muted = 1u;
        br_diagnostic_armed = 0;
    }
}
#else
#define br_diagnostic_audio_guard() ((void)0)
#endif

static uint32_t br_diagnostic_source_mode_request_valid(uint32_t cmd,
                                                         const uint8_t *body,
                                                         uint32_t n)
{
    return cmd == 0x70u && body && n == 4u && body[0] == 'B' &&
           body[1] == 'R' && body[2] == 1u && body[3] <= 1u;
}

static void br_diagnostic_word(uint8_t *out, uint32_t value)
{
    uint32_t i;
    for (i = 0; i < 4u; ++i) out[i] = (uint8_t)(value >> (8u * i));
}

#if BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG && defined(BR_SPLIT_AUDIO) && \
    (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
static uint32_t br_diagnostic_job_row_hash_word(uint32_t hash, uint32_t value)
{
    uint32_t byte;
    for (byte = 0u; byte < 4u; ++byte) {
        hash ^= (value >> (8u * byte)) & 0xffu;
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t br_diagnostic_job_rows_hash(
    const br_audio_source_job_timing_t *sample)
{
    uint32_t hash = 2166136261u;
    uint32_t row;
    hash = br_diagnostic_job_row_hash_word(hash, sample->generation);
    hash = br_diagnostic_job_row_hash_word(hash, sample->queue_wall_ticks);
    hash = br_diagnostic_job_row_hash_word(hash, sample->job_count);
    hash = br_diagnostic_job_row_hash_word(hash, sample->job_row_count);
    for (row = 0u; row < sample->job_row_count; ++row) {
        hash = br_diagnostic_job_row_hash_word(hash,
                                               sample->job_rows[row].meta);
        hash = br_diagnostic_job_row_hash_word(
            hash, sample->job_rows[row].cost_hint_core0);
        hash = br_diagnostic_job_row_hash_word(
            hash, sample->job_rows[row].cost_hint_core1);
        hash = br_diagnostic_job_row_hash_word(
            hash, sample->job_rows[row].duration_ticks);
    }
    return hash;
}
#endif

#if FM1_RUNTIME_IRQ_PROFILE
static void fm1_runtime_irq_profile_reset(void);
static void fm1_runtime_irq_profile_snapshot(uint32_t out[7]);
#endif

#if FM1_DEVELOPMENT_DIAGNOSTICS
/* Same-half timing is a read-only snapshot. The working record and both
 * sealed records are CPU0-owned; no address to this object is ever submitted
 * to the CPU1 worker. `.pool` is not startup-zeroed, so the BSS readiness word
 * prevents any pre-init read until a quiescent reset has explicitly cleared
 * the pool-backed object. */
enum {
    BR_HALF_TIMING_VALID = 1u << 0,
    BR_HALF_TIMING_COMPLETE = 1u << 1,
    BR_HALF_TIMING_FAILED = 1u << 2,
    BR_HALF_TIMING_INCOMPLETE = 1u << 3,
    BR_HALF_TIMING_C1_UNCONFIRMED = 1u << 4,
    BR_HALF_TIMING_LATE_DROP = 1u << 5,
    BR_HALF_TIMING_SCHEMA = 1u,
    BR_HALF_TIMING_WORDS = 19u,
    BR_HALF_TIMING_EXPECTED_CHUNKS = HALF_FRAMES / BR_BLOCK
};

typedef struct {
    uint32_t generation;
    uint32_t flags;
    uint32_t chunks_attempted;
    uint32_t chunks_completed;
    uint32_t callback_elapsed_ticks;
} br_half_timing_record_t;

typedef struct {
    uint32_t next_generation;
    uint32_t active;
    uint32_t callback_start_tick;
    uint32_t block_done_ticks;
    br_half_timing_record_t current;
    br_half_timing_record_t worst_complete;
    br_half_timing_record_t first_failed;
} br_half_timing_store_t;

#if defined(FM1_DEVICE)
static br_half_timing_store_t br_half_timing_store
    __attribute__((section(".pool"), aligned(4)));
#else
static br_half_timing_store_t br_half_timing_store;
#endif
static uint32_t br_half_timing_pool_ready;

/* Called only from a quiescent transaction or the masked 0x61 reset. */
static void br_half_timing_reset(void)
{
    uint32_t irq = br_foreground_irq_save();
    memset(&br_half_timing_store, 0, sizeof br_half_timing_store);
#if FM1_RUNTIME_IRQ_PROFILE
    fm1_runtime_irq_profile_reset();
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
    fm1_runtime_trace_reset();
#endif
#if FM1_DRUM_PROFILE
    fm1_drum_profile_reset();
#endif
    br_half_timing_pool_ready = 1u;
    br_foreground_irq_restore(irq);
}

static void br_half_timing_begin(void)
{
    uint32_t generation;
    if (!br_half_timing_pool_ready) return;
    generation = br_half_timing_store.next_generation + 1u;
    if (!generation) generation = 1u;
    br_half_timing_store.next_generation = generation;
    memset(&br_half_timing_store.current, 0,
           sizeof br_half_timing_store.current);
    br_half_timing_store.current.generation = generation;
    br_half_timing_store.callback_start_tick = fm1_ticks();
    br_half_timing_store.block_done_ticks = 0u;
    br_half_timing_store.active = 1u;
}

static void br_half_timing_attempt_chunk(void)
{
    if (br_half_timing_pool_ready && br_half_timing_store.active)
        ++br_half_timing_store.current.chunks_attempted;
}

static void br_half_timing_complete_chunk(void)
{
    if (br_half_timing_pool_ready && br_half_timing_store.active)
        ++br_half_timing_store.current.chunks_completed;
}

static void br_half_timing_mark_failed(uint32_t c1_unconfirmed)
{
    if (!br_half_timing_pool_ready || !br_half_timing_store.active) return;
    br_half_timing_store.current.flags |= BR_HALF_TIMING_FAILED;
    if (c1_unconfirmed)
        br_half_timing_store.current.flags |= BR_HALF_TIMING_C1_UNCONFIRMED;
}

static void br_half_timing_mark_late_drop(void)
{
    if (!br_half_timing_pool_ready || !br_half_timing_store.active) return;
    br_half_timing_store.current.flags |=
        BR_HALF_TIMING_FAILED | BR_HALF_TIMING_LATE_DROP;
}

static void br_half_timing_commit_block(int32_t *out, uint32_t frames)
{
    if (!br_half_timing_pool_ready || !br_half_timing_store.active) {
        platform_audio_block_done(out, frames);
        return;
    }
    uint32_t start = fm1_ticks();
    platform_audio_block_done(out, frames);
    br_half_timing_store.block_done_ticks += (uint32_t)(fm1_ticks() - start);
}

static void br_half_timing_finish(void)
{
    br_half_timing_record_t sealed;
    uint32_t flags, elapsed;
    if (!br_half_timing_pool_ready || !br_half_timing_store.active) return;
    sealed = br_half_timing_store.current;
    elapsed = (uint32_t)(fm1_ticks() - br_half_timing_store.callback_start_tick);
    sealed.callback_elapsed_ticks = elapsed >= br_half_timing_store.block_done_ticks ?
        elapsed - br_half_timing_store.block_done_ticks : 0u;
    flags = sealed.flags | BR_HALF_TIMING_VALID;
    if (sealed.chunks_attempted == BR_HALF_TIMING_EXPECTED_CHUNKS &&
        sealed.chunks_completed == BR_HALF_TIMING_EXPECTED_CHUNKS)
        flags |= BR_HALF_TIMING_COMPLETE;
    else
        flags |= BR_HALF_TIMING_INCOMPLETE;
    sealed.flags = flags;
    br_half_timing_store.active = 0u;

    if (flags & BR_HALF_TIMING_FAILED) {
        if (!(br_half_timing_store.first_failed.flags & BR_HALF_TIMING_VALID))
            br_half_timing_store.first_failed = sealed;
    } else if ((flags & BR_HALF_TIMING_COMPLETE) &&
        (!(br_half_timing_store.worst_complete.flags & BR_HALF_TIMING_VALID) ||
         sealed.callback_elapsed_ticks >
            br_half_timing_store.worst_complete.callback_elapsed_ticks)) {
        br_half_timing_store.worst_complete = sealed;
    }
}

/* Copy only sealed CPU0-owned records while audio IRQ mutation is excluded.
 * Before the first safe init, the pool may contain arbitrary data; return a
 * schema header and zero records without touching it. */
static void br_half_timing_snapshot(uint32_t words[BR_HALF_TIMING_WORDS])
{
    uint32_t irq, i;
    const br_half_timing_record_t *record;
    memset(words, 0, BR_HALF_TIMING_WORDS * sizeof words[0]);
    words[0] = BR_HALF_TIMING_SCHEMA;
    if (!br_half_timing_pool_ready) return;
    irq = br_foreground_irq_save();
    if (br_half_timing_pool_ready) {
        record = &br_half_timing_store.worst_complete;
        words[1] = record->generation;
        words[2] = record->flags;
        words[3] = record->chunks_attempted;
        words[4] = record->chunks_completed;
        words[5] = record->callback_elapsed_ticks;
        /* words 6..9 are reserved (zero): the reply keeps its 19-word shape. */
        record = &br_half_timing_store.first_failed;
        words[10] = record->generation;
        words[11] = record->flags;
        words[12] = record->chunks_attempted;
        words[13] = record->chunks_completed;
        words[14] = record->callback_elapsed_ticks;
        /* words 15..18 are reserved (zero). */
        /* Invalid records are never exposed, even if a future writer changes
         * its seal ordering. */
        if (!(words[2] & BR_HALF_TIMING_VALID))
            for (i = 1u; i < 10u; ++i) words[i] = 0u;
        if (!(words[11] & BR_HALF_TIMING_VALID))
            for (i = 10u; i < 19u; ++i) words[i] = 0u;
    }
    br_foreground_irq_restore(irq);
}
#else
/* This instrumentation does not participate in audio completion. */
#define br_half_timing_reset() ((void)0)
#define br_half_timing_begin() ((void)0)
#define br_half_timing_attempt_chunk() ((void)0)
#define br_half_timing_complete_chunk() ((void)0)
#define br_half_timing_mark_failed(v) ((void)(v))
#define br_half_timing_mark_late_drop() ((void)0)
#define br_half_timing_commit_block(out, frames) platform_audio_block_done(out, frames)
#define br_half_timing_finish() ((void)0)
#endif

/* Fixed workload and no address parameters: compare the CPU's divider with
 * exact constant-division candidates while synthesis is stopped. */
/* Retain legacy synthetic math tests for optional diagnostic builds. */
#ifndef FM1_ENABLE_ARITHMETIC_BENCHMARK
#define FM1_ENABLE_ARITHMETIC_BENCHMARK 0
#endif
#ifndef FM1_ENABLE_CLOCK_COUNTER_PROBE
#define FM1_ENABLE_CLOCK_COUNTER_PROBE 0
#endif
#ifndef FM1_ENABLE_TIMER4_REGISTER_SNAPSHOT
#define FM1_ENABLE_TIMER4_REGISTER_SNAPSHOT 0
#endif
#if !FM1_DEVELOPMENT_DIAGNOSTICS && \
    (FM1_ENABLE_ARITHMETIC_BENCHMARK || FM1_ENABLE_CLOCK_COUNTER_PROBE || \
     FM1_ENABLE_TIMER4_REGISTER_SNAPSHOT || FM1_RUNTIME_IRQ_PROFILE || \
     FM1_RUNTIME_CHUNK_PROFILE || FM1_DRUM_PROFILE || \
     (defined(FM1_ENABLE_P33_REGISTER_SNAPSHOT) && FM1_ENABLE_P33_REGISTER_SNAPSHOT) || \
     (defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE) || \
     (defined(FM1_PATTERN_PANEL_TEST) && FM1_PATTERN_PANEL_TEST) || \
     (defined(FM1_PATTERN_SECTOR_TEST) && FM1_PATTERN_SECTOR_TEST))
#error "Distribution firmware cannot include diagnostic profiling or test controls"
#endif
#if FM1_ENABLE_ARITHMETIC_BENCHMARK
static uint32_t __attribute__((noinline)) br_diagnostic_divide(uint32_t mode)
{
    /* Volatile input keeps this fixed workload between the two timer reads;
     * otherwise a pure calculation can be moved out of the timed interval. */
    uint32_t state = br_diagnostic_seed, sum = 0, i;
    for (i = 0; i < 50000u; ++i) {
        int32_t value;
        state = state * 1664525u + 1013904223u;
        value = (int32_t)state;
        if (mode == 0u) sum += (uint32_t)(value / 127);
        else if (mode == 1u) sum += (uint32_t)br_div127_exact(value);
        else if (mode == 2u) sum += (uint32_t)(value / 16129);
        else if (mode == 3u) sum += (uint32_t)br_div16129_exact(value);
        else if (mode == 4u) sum += (uint32_t)(value / 4096);
        else if (mode == 5u) sum += (uint32_t)((value + (value < 0 ? 4095 : 0)) >> 12);
        else if (mode == 6u) sum += (uint32_t)(value / 65536);
        else sum += (uint32_t)((value + (value < 0 ? 65535 : 0)) >> 16);
    }
    return sum;
}

/* Fixed bounded input stream for a direct target comparison of the expensive
 * signed divider and the exact BRAIDS reciprocal. Both modes consume the same
 * LCG stream and produce the same signed-result checksum. */
static uint32_t __attribute__((noinline)) br_diagnostic_divide147(uint32_t mode)
{
    uint32_t state = br_diagnostic_seed, sum = 0, i;
    for (i = 0; i < 50000u; ++i) {
        int32_t value;
        state = state * 1664525u + 1013904223u;
        value = (int32_t)((state >> 8) & 0x00ffffffu) - 8388608;
        if (value < -4816896) value = -4816896;
        if (value > 4816749) value = 4816749;
        if (mode == 8u) sum += (uint32_t)(value / 147);
        else sum += (uint32_t)braids_interp_div147(value);
    }
    return sum;
}

#endif

#if defined(FM1_DEVICE) && FM1_FAULT_C1_TIMEOUT_SIDECAR
extern char _ustack_lo[], _ustack_top[], _sstack_lo[], _sstack_top[];
static uint32_t __attribute__((noinline)) br_fault_c1_sidecar_read_allowed(void)
{
    uint32_t state = fm1_cpu1_worker_mailbox.state;
    uint32_t contained = !fm1_cpu1_worker_uncontained_fault_get();
    uint32_t worker_safe =
        (state == FM1_CPU1_WORKER_READY && !fm1_cpu1_worker_fault) ||
        (state == FM1_CPU1_WORKER_ABORTED && fm1_cpu1_worker_fault &&
         fm1_cpu1_worker_reset_held());
    return !br_playing && !br_diagnostic_armed && !br_diagnostic_muted &&
        !br_diagnostic_abort && br_engine_quiet() && br_fx_quiet() &&
        contained && worker_safe;
}
#endif

#if defined(FM1_DEVICE) && FM1_FAULT_RAM_CODE_SIDECAR
extern uint32_t _rt_start[], _rt_end[], _ra_start[], _ra_end[];
#endif
#if defined(FM1_DEVICE) && FM1_FAULT_C1_CONTEXT_SIDECAR
extern uint8_t fm1_cpu1_worker_ustack[0x2000];
extern uint32_t _fm1_cpu1_worker_ustack_top[];
#endif

#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
#include "pattern_provision_protocol.h"
#endif
#if defined(FM1_PATTERN_SECTOR_TEST) && FM1_PATTERN_SECTOR_TEST && defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
#error "Sector-test project-sector read conflicts with provisioning command D2"
#endif
static int fm1_app_command(uint32_t cmd, const uint8_t *body, uint32_t n,
                           uint8_t reply[128], uint32_t *reply_n)
{
    uint32_t i;
    *reply_n = 0;
    /* Before successful app initialization, expose only the same read-only
     * fault status and guarded reboot request available after a sticky fault. */
    if (!br_ready && !br_pipeline_fault_command_allowed(cmd)) return 0;
    if (br_diagnostic_restore_in_progress &&
        !br_pipeline_fault_command_allowed(cmd)) return 0;
    /* A failed CPU1 reset confirmation is sticky until cold reboot. Keep
     * project restore, workload starts, and scheduler mutations away from
     * state that the still-running worker may own. */
    if (br_engine_pipeline_uncontained_fault_get() &&
        !br_pipeline_fault_command_allowed(cmd)) return 0;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    if (cmd==0xd0u || cmd==0xd1u || cmd==0xd2u || cmd==0xd3u || cmd==0xd4u) {
        size_t provision_reply=0u;
        if(fm1_provision_runtime_dispatch((uint8_t)cmd,body,n,reply,128u,&provision_reply)!=FM1_PROVISION_OK)return 0;
        *reply_n=(uint32_t)provision_reply;return 1;
    }
#endif
#if defined(FM1_PATTERN_SECTOR_TEST) && FM1_PATTERN_SECTOR_TEST && (defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST))
    if (cmd == 0xd2u && n == 4u && body) {
        uint32_t sector=body[0], offset=(uint32_t)body[1]|((uint32_t)body[2]<<8), count=body[3];
        int rc;
        if(sector>=PS_TOTAL_SLOTS*2u || !count || count>32u || offset>=PS_SECTOR || count>PS_SECTOR-offset ||
           br_playing || br_recording || !br_engine_quiet() || !br_fx_quiet() || !br_quiescent_transaction_begin()) return 0;
        rc=br_project_read(0,BR_PROJECT_BASE+sector*PS_SECTOR+offset,reply,count);
        if(br_engine_pipeline_uncontained_fault_get()) return 0; /* retain quarantine */
        br_quiescent_transaction_end();if(rc) return 0;
        *reply_n=count;return 1;
    }
#endif
#if defined(FM1_DEVICE) && defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE
    if (cmd == 0xd5u && !n) {
        for (i = 0u; i < 4u; ++i) br_diagnostic_word(reply + 4u*i, fm1_wave_boot_timing[i]);
        *reply_n = 16u;
        return 1;
    }
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    if (cmd == 0x60u && !n) {
        uint32_t metrics[8];
        fm1_audio_metrics_snapshot(metrics);
        for (i = 0; i < 8u; ++i) br_diagnostic_word(reply + 4u * i, metrics[i]);
        *reply_n = 32u;
    } else if (cmd == 0x61u && n == 4u &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u && !body[3]) {
        fm1_audio_metrics_reset();
        br_half_timing_reset();
#if FM1_DEVELOPMENT_DIAGNOSTICS
        for (i = 0; i < 4u; ++i) br_diagnostic_profile[i] = br_diagnostic_detail[i] = 0;
#endif
        reply[0] = 1u; *reply_n = 1u;
    } else
#endif
    if (cmd == 0x62u && !n) {
        reply[0] = 'B'; reply[1] = 'R'; reply[2] = 1u; reply[3] = 0;
        br_diagnostic_word(reply + 4u, sizeof(br_session_t));
        br_diagnostic_word(reply + 8u, FS);
        br_diagnostic_word(reply + 12u, HALF_FRAMES);
        br_diagnostic_word(reply + 16u, br_diagnostic_abort);
#if defined(FM1_PLAITS_PHASE_LUT) && FM1_PLAITS_PHASE_LUT
        br_diagnostic_word(reply + 20u, 1u);
#else
        br_diagnostic_word(reply + 20u, 0u);
#endif
        *reply_n = 24u;
    } else if (cmd == 0xA0u && !n) {
        /* Versioned read-only SAVE/restore protection status. Unknown flags
         * must be treated as protected by clients. */
        reply[0] = 'B'; reply[1] = 'R'; reply[2] = 1u;
        reply[3] = (br_legacy_migration_pending ? 1u : 0u) |
                   (br_invalid_session_pending ? 2u : 0u);
        *reply_n = 4u;
    } else if (cmd == 0x68u && !n) {
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
        /* This build never creates a legacy migration record. */
        reply[0] = 'B'; reply[1] = 'R'; reply[2] = 1u; reply[3] = 0u;
        br_diagnostic_word(reply + 4u, 0u); br_diagnostic_word(reply + 8u, 0u);
        *reply_n = 12u;
#else
        uint32_t length = 0u, version = 0u;
        uint32_t available = br_session_legacy_raw_info(&length, &version);
        reply[0] = 'B'; reply[1] = 'R'; reply[2] = 1u; reply[3] = (uint8_t)available;
        br_diagnostic_word(reply + 4u, available ? length : 0u);
        br_diagnostic_word(reply + 8u, available ? version : 0u);
        *reply_n = 12u;
#endif
    } else if (cmd == 0x68u && n == 3u) {
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
        return 0;
#else
        uint32_t offset = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count = body[2], length = 0u, version = 0u;
        uint32_t expected;
        if (!br_session_legacy_raw_info(&length, &version) ||
            offset % 32u || offset >= length) return 0;
        (void)version;
        expected = length - offset < 32u ? length - offset : 32u;
        if (count != expected || !br_session_legacy_raw_chunk(offset, count,
                                                               reply, 32u)) return 0;
        *reply_n = count;
#endif
    } else if (cmd == 0x63u && n == 3u) {
        uint32_t offset = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count = body[2];
        if (!count || count > 32u || offset > sizeof(br_session_t) ||
            count > sizeof(br_session_t) - offset) return 0;
        if (!offset) {
            if (!br_capture(&br_diagnostic_session)) return 0;
            br_diagnostic_snapshot_valid = 1u;
            memset(br_diagnostic_chunks, 0, sizeof br_diagnostic_chunks);
        }
        if (!br_diagnostic_snapshot_valid) return 0;
        memcpy(reply, (const uint8_t *)&br_diagnostic_session + offset, count);
        *reply_n = count;
    } else if (cmd == 0x7du && n == 3u) {
#if defined(FM1_DEVICE)
        uint32_t offset = (uint32_t)body[0] | ((uint32_t)body[1] << 8);
        uint32_t count = fm1_fault_bundle_chunk(&fm1_fault_bundle, offset, body[2], reply);
        if (!count) return 0;
        *reply_n = count;
#else
        return 0;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS && defined(FM1_DEVICE) && FM1_FAULT_C0_POPFRAME_SIDECAR
    } else if (cmd == 0x94u && n == 3u) {
        uint32_t offset = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t owner = fm1_fault_bundle.owner_cpu;
        uint32_t valid = fm1_fault_bundle.valid_mask;
        uint32_t epoch = fm1_fault_bundle.epoch;
        uint32_t count;
        if (fm1_fault_bundle.magic != FM1_FAULT_BUNDLE_MAGIC ||
            fm1_fault_bundle.version != FM1_FAULT_BUNDLE_VERSION ||
            fm1_fault_bundle.byte_size != sizeof(fm1_fault_diag_bundle_t) ||
            !epoch || owner > 1u || (valid & ~3u) || !(valid & (1u << owner)) ||
            !(valid & 1u) || fm1_fault_bundle.cpu0_phase != 2u ||
            fm1_fault_bundle.cpu[0].magic != FM1_FAULT_DIAG_MAGIC ||
            fm1_fault_bundle.cpu[0].version != FM1_FAULT_DIAG_VERSION ||
            fm1_fault_bundle.cpu[0].byte_size != sizeof(fm1_fault_diag_t) ||
            fm1_fault_bundle.cpu[0].fault_count != epoch ||
            fm1_fault_bundle.cpu[0].current_cpu_id != 0u)
            return 0;
        count = fm1_fault_popframe_chunk(&fm1_fault_popframe_diag, epoch,
                                         offset, body[2], reply);
        if (!count) return 0;
        *reply_n = count;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS && defined(FM1_DEVICE) && FM1_FAULT_C1_TIMEOUT_SIDECAR && (!defined(FM1_PATTERN_PANEL_TEST) || !FM1_PATTERN_PANEL_TEST)
    } else if (cmd == 0x96u && n == 4u && body) {
        uint32_t record_kind = body[0];
        uint32_t offset = (uint32_t)body[1] | (uint32_t)body[2] << 8;
        uint32_t count = 0u;
        if ((record_kind > (FM1_FAULT_C1_CONTEXT_SIDECAR ? 3u :
                            (FM1_FAULT_RAM_CODE_SIDECAR ? 2u : 1u)) &&
             record_kind != FM1_FAULT_C0_SYSTEM_STACK_READ_KIND) ||
            !br_fault_c1_sidecar_read_allowed() ||
            !br_quiescent_transaction_begin()) return 0;
        /* Recheck after the bounded IRQ-masked claim; the page read never
         * touches the live C1 stack or worker-owned buffers. */
        if (!br_fault_c1_sidecar_read_allowed()) {
            br_quiescent_transaction_end();
            return 0;
        }
        if (record_kind == 0u)
            count = fm1_fault_c1_timeout_chunk(
                &fm1_fault_c1_timeout_record, offset, body[3], reply);
        else if (record_kind == 1u ||
                 record_kind == FM1_FAULT_C0_SYSTEM_STACK_READ_KIND) {
            uint32_t system = record_kind == FM1_FAULT_C0_SYSTEM_STACK_READ_KIND;
            count = fm1_fault_c0_stack_window_chunk_abi(
                &fm1_fault_c0_stack_window_record,
                system ? FM1_FAULT_C0_SYSTEM_STACK_VERSION :
                         FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION,
                (uint32_t)(uintptr_t)(system ? _sstack_lo : _ustack_lo),
                (uint32_t)(uintptr_t)(system ? _sstack_top : _ustack_top),
                offset, body[3], reply);
        }
#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ
        else if (record_kind == 2u) {
            uint32_t owner, epoch, pc;
            if (fm1_fault_ram_code_bundle_identity(
                    &fm1_fault_bundle, &epoch, &owner, &pc)) {
                count = fm1_fault_ram_code_chunk(
                    &fm1_fault_ram_code_record, epoch, owner, pc,
                    (uint32_t)(uintptr_t)_rt_start,
                    (uint32_t)(uintptr_t)_rt_end,
                    (uint32_t)(uintptr_t)_ra_start,
                    (uint32_t)(uintptr_t)_ra_end,
                    offset, body[3], reply);
            }
        }
#endif
        else if (record_kind == 3u) {
            uint32_t owner, epoch, pc;
            if (fm1_fault_ram_code_bundle_identity(
                    &fm1_fault_bundle, &epoch, &owner, &pc) && owner == 1u) {
                count = fm1_fault_c1_context_chunk_shallow(
                    &fm1_fault_c1_context_record, epoch, offset, body[3], reply);
            }
            (void)pc;
        }
        br_quiescent_transaction_end();
        if (!count) return 0;
        *reply_n = count;
#endif
#if defined(FM1_DEVICE) && FM1_FAULT_C1_TIMEOUT_SIDECAR && FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ
    } else if (cmd == 0xacu && n == 3u && body) {
        uint32_t offset=(uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count;
        /* Match the existing owner-sidecar guard: the brief transaction may
         * emit silence, and no live worker stack or arbitrary address is read. */
        if (!br_fault_c1_sidecar_read_allowed() || !br_quiescent_transaction_begin()) return 0;
        if (!br_fault_c1_sidecar_read_allowed()) {
            br_quiescent_transaction_end();
            return 0;
        }
        count=fm1_fault_dual_code_chunk(&fm1_fault_dual_code_record,&fm1_fault_bundle,
              (uint32_t)(uintptr_t)_rt_start,(uint32_t)(uintptr_t)_rt_end,
              (uint32_t)(uintptr_t)_ra_start,(uint32_t)(uintptr_t)_ra_end,
              offset,body[2],reply);
        br_quiescent_transaction_end();
        if (!count) return 0;
        *reply_n=count;
#endif
    } else if (cmd == 0x64u && n >= 3u && n <= 34u) {
        uint32_t offset = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count = n - 2u;
        if (!offset) {
            uint32_t version;
            if (count < 8u) return 0;
            memset(&br_diagnostic_session, 0, sizeof br_diagnostic_session);
            memset(br_diagnostic_chunks, 0, sizeof br_diagnostic_chunks);
            br_diagnostic_snapshot_valid = 0u;
            br_diagnostic_upload_length = 0u;
            memcpy((uint8_t *)&br_diagnostic_session, body + 2u, count);
            if (br_diagnostic_session.v3.magic != BR_SESSION_MAGIC) return 0;
            version = br_diagnostic_session.v3.version;
            if (version == 10u) br_diagnostic_upload_length = sizeof(br_session_v10_t);
            else if (version == 11u || version == 12u || version == 13u) br_diagnostic_upload_length = BR_SESSION_V13_SIZE;
            else if (version == 14u || version == 15u || version == 16u) br_diagnostic_upload_length = sizeof(br_session_t);
            else return 0;
        }
        if (!br_diagnostic_upload_length || offset % 32u ||
            offset >= br_diagnostic_upload_length ||
            count != (br_diagnostic_upload_length - offset < 32u ?
                      br_diagnostic_upload_length - offset : 32u)) return 0;
        br_diagnostic_snapshot_valid = 0;
        if (offset) memcpy((uint8_t *)&br_diagnostic_session + offset, body + 2u, count);
        br_diagnostic_chunks[(offset / 32u) / 32u] |= 1u << ((offset / 32u) % 32u);
        reply[0] = body[0]; reply[1] = body[1]; *reply_n = 2u;
    } else if (cmd == 0x65u && n == 4u &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u && !body[3]) {
        uint32_t chunk, chunks, version;
        if (!br_diagnostic_upload_length) return 0;
        chunks = (br_diagnostic_upload_length + 31u) / 32u;
        for (chunk = 0u; chunk < chunks; ++chunk)
            if (!(br_diagnostic_chunks[chunk / 32u] & (1u << (chunk % 32u)))) return 0;
        version = br_diagnostic_session.v3.version;
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
        if (version != 16u || !br_session_v16_valid(&br_diagnostic_session)) return 0;
#else
        if ((version == 10u && !br_session_v10_valid(&br_diagnostic_session.v10)) ||
            (version == 11u && !br_session_v11_valid(&br_diagnostic_session)) ||
            (version == 12u && !br_session_v12_valid(&br_diagnostic_session)) ||
            (version == 13u && !br_session_v13_valid(&br_diagnostic_session)) ||
            (version == 14u && !br_session_v14_valid(&br_diagnostic_session)) ||
            (version == 15u && !br_session_v15_valid(&br_diagnostic_session)) ||
            (version == 16u && !br_session_v16_valid(&br_diagnostic_session)) ||
            (version != 10u && version != 11u && version != 12u && version != 13u && version != 14u && version != 15u && version != 16u)) return 0;
#endif
#if defined(BR_DIAGNOSTIC_TEST) && defined(BR_SPLIT_AUDIO)
        /* Inject the ALNK fault after the command-entry check but before the
         * masked transaction claim. The claim must observe and reject it. */
        if (br_diagnostic_test_inject_restore_fault) {
            br_diagnostic_test_inject_restore_fault = 0u;
            br_engine_pipeline_test_uncontained_fault_latch();
        }
#endif
        if (!br_quiescent_transaction_begin()) return 0;
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST) && BR_PATTERN_OWNER_HOST
        /* A session-only upload replaces the live take, so it must not retain
         * patterns or queued transitions from a different session. */
        if(!br_ps_host_can_reset()) { br_quiescent_transaction_end(); return 0; }
#endif
        uint32_t old_muted = br_diagnostic_muted;
        br_diagnostic_muted = 1u;
        RING_PUBLISH();
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
        if (!br_restore(&br_diagnostic_session)) {
#else
        if (!(version == 10u ? br_restore_v10(&br_diagnostic_session.v10) :
                               br_restore(&br_diagnostic_session))) {
#endif
            br_diagnostic_muted = old_muted;
            br_quiescent_transaction_end();
            return 0;
        }
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST) && BR_PATTERN_OWNER_HOST
        /* Under the same stopped transaction the preflight makes reset
         * deterministic: Pattern 1 becomes the restored four-track take. */
        if(!br_ps_host_reset()) return 0; /* keep quarantine/mute on invariant failure */
        br_ps_external_restore_completed=1u;
#if (defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)) && !defined(DINGHY_EMULATOR_SAVE)
        es.current=-1;
#if defined(FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY) && FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY
        es_project_boot_state=1u;
#endif
#endif
#endif
        br_engine_init();
        br_fx_init();
        /* Restore rebuilt effective controls before playback. */
        if (!br_playing && br_engine_quiet() && br_fx_quiet())
            br_fx_warm_controls_quiescent();
        /* Accepted uploads can complete legacy migration, but a generic
         * upload cannot authorize overwriting an invalid settings record. */
        if (!br_invalid_session_pending) {
#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
            if (version == 10u)
                br_session_explicit_v10_upload_accepted(&br_diagnostic_session.v10);
            else
#endif
            {
                br_legacy_migration_pending = 0u;
                br_legacy_raw_length = br_legacy_raw_version = 0u;
            }
        }
        br_half_timing_reset();
        br_diagnostic_disarm();
        br_diagnostic_abort = 0;
#if FM1_DEVELOPMENT_DIAGNOSTICS
        br_diagnostic_overload_run = 0;
#endif
        br_recording = br_save_pending = 0;
        br_ui.redraw = 1;
        RING_PUBLISH();
        br_diagnostic_muted = 0;
        memset(br_diagnostic_chunks, 0, sizeof br_diagnostic_chunks);
        br_diagnostic_upload_length = 0u;
        br_quiescent_transaction_end();
        reply[0] = 1u; *reply_n = 1u;
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x66u && (n == 4u || n == 8u)) {
        uint32_t ms = (uint32_t)body[0] | (uint32_t)body[1] << 8 |
            (uint32_t)body[2] << 16 | (uint32_t)body[3] << 24;
        uint32_t pass_limit = n == 8u ?
            ((uint32_t)body[4] | (uint32_t)body[5] << 8 |
             (uint32_t)body[6] << 16 | (uint32_t)body[7] << 24) :
            FM1_AUDIO_HALF_TICKS;
        if (ms < 500u || ms > 600000u) return 0;
        if (!pass_limit || pass_limit > FM1_AUDIO_HALF_TICKS) return 0;
        uint32_t irq;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        br_diagnostic_deadline = fm1_ms + ms;
        br_diagnostic_abort = br_diagnostic_overload_run = 0;
        br_diagnostic_pass_limit_ticks = pass_limit;
        RING_PUBLISH(); br_diagnostic_armed = 1u;
        br_foreground_mutation_end(irq);
        reply[0] = 1u; *reply_n = 1u;
#endif
#if FM1_ENABLE_ARITHMETIC_BENCHMARK
    } else if (cmd == 0x67u && n == 1u && body[0] < 12u &&
               !br_playing && !br_diagnostic_armed) {
        uint32_t start, elapsed, sum, muted = br_diagnostic_muted;
        br_diagnostic_muted = 1u; RING_PUBLISH();
        start = fm1_ticks();
        sum = body[0] < 8u ? br_diagnostic_divide(body[0]) :
                              (body[0] < 10u ? br_diagnostic_divide147(body[0]) :
                               br_fx_bounded_pole_benchmark(body[0], br_diagnostic_seed));
        elapsed = fm1_ticks() - start;
        RING_PUBLISH(); br_diagnostic_muted = muted;
        br_diagnostic_word(reply, elapsed);
        br_diagnostic_word(reply + 4u, sum);
        br_diagnostic_word(reply + 8u, 50000u);
        *reply_n = 12u;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x6Au && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[8], irq = irq_save();
        fm1_clock_snapshot(words);
        irq_restore(irq);
        for (i = 0; i < 8u; ++i) br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 32u;
#else
        return 0;
#endif
#if FM1_ENABLE_CLOCK_COUNTER_PROBE
    } else if (cmd == 0x6Bu && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        uint32_t old_muted = br_diagnostic_muted;
        uint32_t result, lsb = 0u, sys = 0u, raw = 0u;
        if (br_playing || br_diagnostic_armed || old_muted ||
            !br_engine_quiet() || !br_fx_quiet()) {
            irq_restore(irq);
            return 0;
        }
        br_diagnostic_muted = 1u;
        RING_PUBLISH();
        irq_restore(irq);
        result = (uint32_t)fm1_clock_measure_once_target(&lsb, &sys, &raw);
        irq = irq_save();
        RING_PUBLISH(); br_diagnostic_muted = old_muted;
        irq_restore(irq);
        br_diagnostic_word(reply, result);
        br_diagnostic_word(reply + 4u, lsb);
        br_diagnostic_word(reply + 8u, sys);
        br_diagnostic_word(reply + 12u, raw);
        *reply_n = 16u;
#else
        return 0;
#endif
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x6Cu && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        for (i = 0; i < 4u; ++i) br_diagnostic_word(reply + 4u * i, br_diagnostic_detail[i]);
        irq_restore(irq);
        *reply_n = 16u;
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x6Du && !n) {
#if defined(FM1_DEVICE)
        fm1_cpu1_snapshot_t snapshot;
        fm1_cpu1_snapshot_target(&snapshot);
        fm1_cpu1_snapshot_encode(&snapshot, reply);
        *reply_n = 8u;
#else
        return 0;
#endif
#endif
    } else if (cmd == 0x6Fu && !n) {
#if defined(FM1_DEVICE)
        /* Read the existing no-init crash record only; this adds no MMIO. */
        uint32_t irq = irq_save();
        br_diagnostic_word(reply, fm1_crash.magic);
        br_diagnostic_word(reply + 4u, fm1_crash.count);
        br_diagnostic_word(reply + 8u, fm1_crash.vec);
        br_diagnostic_word(reply + 12u, fm1_crash.pc);
        br_diagnostic_word(reply + 16u, fm1_crash.dbg);
        br_diagnostic_word(reply + 20u, fm1_crash.uptime_ms);
        irq_restore(irq);
        *reply_n = 24u;
#else
        return 0;
#endif
    } else if (cmd == 0x6Eu && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        uint32_t old_muted = br_diagnostic_muted;
        uint32_t flags = (br_playing ? 1u : 0u) |
                         (br_diagnostic_armed ? 2u : 0u) |
                         (old_muted ? 4u : 0u) |
                         (br_engine_quiet() ? 8u : 0u) |
                         (br_fx_quiet() ? 16u : 0u);
        irq_restore(irq);
        br_diagnostic_word(reply, FM1_CPU1_SMOKE_UNSUPPORTED);
        br_diagnostic_word(reply + 4u, flags);
        br_diagnostic_word(reply + 8u, old_muted);
        br_diagnostic_word(reply + 12u, 0u);
        br_diagnostic_word(reply + 16u, 0u);
        br_diagnostic_word(reply + 20u, 0u);
        br_diagnostic_word(reply + 24u, 0u);
        br_diagnostic_word(reply + 28u, 0u);
        *reply_n = 32u;
#else
        return 0;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x74u && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        uint32_t armed = 0u;
        if (!br_playing && !br_diagnostic_armed && !br_diagnostic_muted &&
            br_engine_quiet() && br_fx_quiet())
            armed = fm1_cpu1_early_cookie_arm();
        br_foreground_mutation_end(irq);
        br_diagnostic_word(reply, armed ? 1u : 0u);
        br_diagnostic_word(reply + 4u, 100u);
        *reply_n = 8u;
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x75u && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        if (fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC) {
            irq_restore(irq);
            return 0;
        }
        br_diagnostic_word(reply, fm1_cpu1_early_record.status);
        br_diagnostic_word(reply + 4u, fm1_cpu1_early_record.heartbeat);
        br_diagnostic_word(reply + 8u, fm1_cpu1_early_record.cpu_id);
        br_diagnostic_word(reply + 12u, fm1_cpu1_early_record.restored);
        br_diagnostic_word(reply + 16u, fm1_cpu1_early_snapshot.c1_con);
        br_diagnostic_word(reply + 20u, fm1_cpu1_early_snapshot.entry);
        br_diagnostic_word(reply + 24u, fm1_cpu1_early_snapshot.dbg_en);
        br_diagnostic_word(reply + 28u, fm1_cpu1_early_snapshot.c0_wr_limit_en);
        irq_restore(irq);
        *reply_n = 32u;
#else
        return 0;
#endif
    } else if (cmd == 0x76u && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        if (fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC) {
            irq_restore(irq);
            return 0;
        }
        br_diagnostic_word(reply, fm1_cpu1_early_snapshot.pc0_l);
        br_diagnostic_word(reply + 4u, fm1_cpu1_early_snapshot.pc0_h);
        br_diagnostic_word(reply + 8u, fm1_cpu1_early_snapshot.pc1_l);
        br_diagnostic_word(reply + 12u, fm1_cpu1_early_snapshot.pc1_h);
        irq_restore(irq);
        *reply_n = 16u;
#else
        return 0;
#endif
    } else if (cmd == 0x7Bu && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        br_diagnostic_word(reply, fm1_cpu1_early_trace.launch_stage);
        br_diagnostic_word(reply + 4u, fm1_cpu1_early_trace.post_control);
        br_diagnostic_word(reply + 8u, fm1_cpu1_early_trace.wait_rc);
        br_diagnostic_word(reply + 12u, fm1_cpu1_early_trace.mailbox_id);
        br_diagnostic_word(reply + 16u, fm1_cpu1_early_trace.mailbox_state);
        br_diagnostic_word(reply + 20u, fm1_cpu1_early_trace.worker_heartbeat);
        irq_restore(irq);
        *reply_n = 24u;
#else
        return 0;
#endif
#endif
    } else if (cmd == 0x7Cu && n == 4u &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u) {
#if defined(FM1_DEVICE)
        uint32_t scheduled = 0u;
        uint32_t fault = fm1_cpu1_worker_fault != 0u;
        uint32_t reset_held = fault && fm1_cpu1_worker_reset_held();
        uint32_t engine_quiet = 1u, fx_quiet = 1u;
        if (!br_ready && (!fault || !reset_held)) return 0;
        if (br_ready && !fault) {
            engine_quiet = br_engine_quiet();
            fx_quiet = br_fx_quiet();
        }
        if (fm1_cpu1_reboot_request_allowed(body[3], fm1_cpu1_early_reboot_pending,
                br_playing, br_diagnostic_armed, fault, reset_held,
                br_diagnostic_muted, engine_quiet, fx_quiet))
            scheduled = fm1_cpu1_early_reboot_cold(body[3]);
        reply[0] = scheduled ? 1u : 0u;
        *reply_n = 1u;
#else
        return 0;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x70u && br_diagnostic_source_mode_request_valid(cmd, body, n)) {
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t irq;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        uint32_t accepted = 0u;
        if (!br_playing && !br_diagnostic_armed && !br_diagnostic_muted &&
            !br_diagnostic_abort && br_engine_quiet() && br_fx_quiet()) {
            br_engine_source_mode_set(body[3]);
            accepted = 1u;
        }
        br_diagnostic_word(reply, accepted);
        br_diagnostic_word(reply + 4u, br_engine_source_mode_get());
        *reply_n = 8u;
        br_foreground_mutation_end(irq);
#else
        return 0;
#endif
    } else if (cmd == 0x7Eu &&
               br_pipeline_mode_command_valid(cmd, body, n)) {
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t accepted = 0u, irq;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        if (!br_playing && !br_diagnostic_armed && !br_diagnostic_muted &&
            !br_diagnostic_abort && br_engine_quiet() && br_fx_quiet()) {
            br_fx_set_serial_mode(body[3]);
            accepted = 1u;
        }
        br_diagnostic_word(reply, accepted);
        br_diagnostic_word(reply + 4u, br_fx_serial_mode_get());
        *reply_n = 8u;
        br_foreground_mutation_end(irq);
#else
        return 0;
#endif
    } else if (cmd == 0x86u && br_pipeline_mode_command_valid(cmd, body, n)) {
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t accepted = 0u;
        uint32_t irq;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        if (br_pipeline_mode_change_allowed(br_playing, br_diagnostic_armed,
                br_diagnostic_muted, br_diagnostic_abort,
                br_engine_quiet(), br_fx_quiet()))
            accepted = br_engine_pipeline_mode_set(body[3]);
        /* Snapshot the result under the same interrupt mask as the mode and
         * generation update, so audio cannot observe a half-transition. */
        uint32_t mode = br_engine_pipeline_mode_get();
        br_foreground_mutation_end(irq);
        br_diagnostic_word(reply, accepted);
        br_diagnostic_word(reply + 4u, mode);
        *reply_n = 8u;
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x81u && !n) {
        /* Read the last computed gain; do not sample ADC or mutate volume. */
        br_diagnostic_word(reply, 1u);
        br_diagnostic_word(reply + 4u, (uint32_t)master_q12);
        *reply_n = 8u;
    } else if (cmd == 0x82u && n == 4u &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u) {
#if defined(FM1_DEVICE)
        uint32_t irq;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        uint32_t safe = fm1_master_gain_test_allowed(br_playing,
            br_diagnostic_armed, br_diagnostic_muted, br_diagnostic_abort,
            br_engine_quiet(), br_fx_quiet());
        uint32_t accepted = fm1_master_gain_test_set(body[3], safe);
        br_diagnostic_word(reply, accepted);
        br_diagnostic_word(reply + 4u, fm1_master_gain_test_mode());
        br_diagnostic_word(reply + 8u, (uint32_t)fm1_master_gain_effective());
        *reply_n = 12u;
        br_foreground_mutation_end(irq);
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x7Fu && !n) {
#if defined(FM1_DEVICE)
        uint32_t values[8];
        fm1_fault_diag_live(fm1_cpu1_worker_mailbox.cpu_id,
                            fm1_cpu1_worker_mailbox.state, values);
        for (i = 0; i < 8u; ++i) br_diagnostic_word(reply + 4u * i, values[i]);
        br_diagnostic_word(reply + 32u,
            fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.emu : 0xffffffffu);
        *reply_n = 36u;
#else
        return 0;
#endif
    } else if (cmd == 0x77u && !n) {
#if defined(FM1_DEVICE)
        if (!br_quiescent_transaction_begin()) return 0;
        uint32_t sequence = 0u, result = 0u, rc;
        fm1_cpu1_worker_smoke_result_t *work = fm1_cpu1_worker_smoke_result();
        /* Starting work on CPU1 is opt-in and only safe while synthesis and
         * effects are quiet, playback is stopped, and diagnostics are unarmed. */
        if (br_playing || br_diagnostic_armed || br_diagnostic_muted ||
            !br_engine_quiet() || !br_fx_quiet())
            rc = FM1_CPU1_WORKER_BUSY;
        else
            rc = fm1_cpu1_worker_submit_smoke(&sequence);
        if (rc == FM1_CPU1_WORKER_OK)
            rc = fm1_cpu1_worker_wait(sequence, FM1_CPU1_WORKER_TIMEOUT_TICKS, &result);
        if (rc == FM1_CPU1_WORKER_OK)
            rc = fm1_cpu1_worker_park(FM1_CPU1_WORKER_TIMEOUT_TICKS);
        if (rc == FM1_CPU1_WORKER_TIMEOUT) {
            (void)fm1_cpu1_worker_abort_hold_reset();
            br_diagnostic_muted = 1u;
            br_diagnostic_abort = 3u;
        }
        br_diagnostic_word(reply, rc);
        br_diagnostic_word(reply + 4u, sequence);
        br_diagnostic_word(reply + 8u, result);
        if (br_engine_pipeline_uncontained_fault_get()) {
            /* C1 may still own the result record after failed reset proof. */
            br_diagnostic_word(reply + 12u, 0xffffffffu);
            br_diagnostic_word(reply + 16u, 0xffffffffu);
        } else {
            br_diagnostic_word(reply + 12u, work->integer_result);
            br_diagnostic_word(reply + 16u, work->float_bits);
        }
        *reply_n = 20u;
        br_quiescent_transaction_end();
#else
        return 0;
#endif
    } else if (cmd == 0x79u && !n) {
        uint32_t progress[5];
        br_fx_progress_read(progress);
        br_diagnostic_word(reply, BR_FX_PROGRESS_RETAINED_VERSION);
        for (i = 0; i < 5u; ++i) br_diagnostic_word(reply + 4u * (i + 1u), progress[i]);
#if defined(FM1_DEVICE)
        br_diagnostic_word(reply + 24u, fm1_boot.p3_rst);
#else
        br_diagnostic_word(reply + 24u, 0u);
#endif
        *reply_n = 28u;
    } else if (cmd == 0x7Au && !n) {
        uint32_t p3_rst = 0u, rst_src = 0u, wdt_con = 0u;
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        p3_rst = fm1_boot.p3_rst;
        rst_src = fm1_boot.rst_src;
        wdt_con = fm1_boot.wdt_con;
        irq_restore(irq);
#endif
        br_diagnostic_word(reply, 1u);
        br_diagnostic_word(reply + 4u, p3_rst);
        br_diagnostic_word(reply + 8u, rst_src);
        br_diagnostic_word(reply + 12u, wdt_con);
        *reply_n = 16u;
    } else if (cmd == 0x78u && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[8];
        fm1_cpu1_worker_status_snapshot(words);
        for (i = 0; i < 8u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 32u;
#else
        return 0;
#endif
    } else if (cmd == 0x87u && br_worker_diag87_request_valid(body, n)) {
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO) && defined(BR_SPLIT_AUDIO)
        uint32_t words[BR_WORKER_DIAG87_WORDS];
        uint32_t queue[4], irq;
        irq = irq_save();
        words[0] = BR_WORKER_DIAG87_VERSION;
        words[1] = fm1_cpu1_worker_diag.wait_timeout_count;
        words[2] = fm1_cpu1_worker_diag.last_job_status;
        words[3] = fm1_cpu1_worker_diag.last_job_task;
        words[4] = fm1_cpu1_worker_diag.last_job_context;
        words[5] = fm1_cpu1_worker_diag.last_job_elapsed_ticks;
        words[6] = fm1_cpu1_worker_diag.last_job_start_ticks;
        words[7] = fm1_cpu1_worker_diag.last_job_sequence;
        words[8] = fm1_cpu1_worker_diag.last_job_result;
        words[9] = fm1_cpu1_worker_diag.last_wait_status;
        words[10] = fm1_cpu1_worker_diag.last_wait_task;
        words[11] = fm1_cpu1_worker_diag.last_wait_sequence;
        words[12] = fm1_cpu1_worker_diag.last_wait_context;
        words[13] = fm1_cpu1_worker_diag.last_wait_elapsed_ticks;
        br_audio_source_diagnostic_snapshot(queue);
        words[14] = queue[0];
        words[15] = queue[1];
        words[16] = queue[2];
        words[17] = queue[3];
        irq_restore(irq);
        for (i = 0u; i < BR_WORKER_DIAG87_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = BR_WORKER_DIAG87_BYTES;
#else
        return 0;
#endif
    } else if (cmd == 0xC8u && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        fm1_tail_detail[0] = 1u;
        for (i = 0u; i < 16u; ++i) br_diagnostic_word(reply + 4u * i, fm1_tail_detail[i]);
        irq_restore(irq);
        *reply_n = 64u;
#else
        return 0;
#endif
    } else if (cmd == 0xCCu && n == 4u && body[0] == 'B' && body[1] == 'R' && body[2] == 1u) {
        /* Hold a punch-in effect (0 = none, 1..4) without the keys, for timing runs. */
        br_punch_loop_len = br_punch_cfg.loop_len & 3u;
        br_punch_effect = body[3] <= BR_PUNCH_TREM ? body[3] : 0u;
        reply[0] = 1u; *reply_n = 1u;
    } else if (cmd == 0xCAu && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        fm1_gap_detail[0] = 1u;
        for (i = 0u; i < 14u; ++i) br_diagnostic_word(reply + 4u * i, fm1_gap_detail[i]);
        irq_restore(irq);
        *reply_n = 56u;
#else
        return 0;
#endif
    } else if (cmd == 0xCDu && (!n || n == 4u)) {  /* a 4-byte body also clears the counters */
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        br_diagnostic_word(reply, br_gr_stats[0]);
        br_diagnostic_word(reply + 4u, br_gr_stats[1]);
        br_diagnostic_word(reply + 8u, br_gr_stats[2]);
        br_diagnostic_word(reply + 12u, br_gr_stats[3]);
        for (i = 4u; i < 8u; ++i) br_diagnostic_word(reply + 4u * i, br_gr_stats[i]);
        irq_restore(irq);
        *reply_n = 32u;
        if (n == 4u) memset((void *)br_gr_stats, 0, sizeof br_gr_stats);
#else
        return 0;
#endif
    } else if (cmd == 0xC9u && n == 4u &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u && !body[3]) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
        for (i = 0u; i < 16u; ++i) fm1_tail_detail[i] = 0u;
        for (i = 0u; i < 14u; ++i) fm1_gap_detail[i] = 0u;
        irq_restore(irq);
        reply[0] = 1u; *reply_n = 1u;
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x88u && br_audio_tail_diag88_request_valid(body, n)) {
#if defined(FM1_DEVICE)
        uint32_t words[BR_AUDIO_TAIL_DIAG88_WORDS];
        fm1_audio_tail_metrics_snapshot(words);
        for (i = 0u; i < BR_AUDIO_TAIL_DIAG88_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = BR_AUDIO_TAIL_DIAG88_BYTES;
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x89u && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[FM1_FOREGROUND_DIAG89_WORDS];
        fm1_foreground_journal_snapshot_reply(words);
        for (i = 0u; i < FM1_FOREGROUND_DIAG89_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = FM1_FOREGROUND_DIAG89_BYTES;
#else
        return 0;
#endif
    } else if (cmd == 0x8Au && !n) {
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t words[19];
        fm1_uac_diagnostic_snapshot(words);
        for (i = 0u; i < 19u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 76u;
#else
        return 0;
#endif
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x95u && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[4];
        fm1_c1_stack_guard_snapshot(words);
        for (i = 0u; i < 4u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 16u;
#else
        return 0;
#endif
    } else if (cmd == 0x9Au && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[4];
        fm1_ram_code_guard_snapshot(words);
        for (i = 0u; i < 4u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 16u;
#else
        return 0;
#endif
    } else if (cmd == 0x8Bu && !n) {
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t words[9];
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
#endif
        words[0] = 1u;
        br_audio_source_cost_diagnostic_snapshot(&words[1]);
#if defined(FM1_DEVICE)
        irq_restore(irq);
#endif
        for (i = 0u; i < 9u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 36u;
#else
        return 0;
#endif
    } else if (cmd == 0x8Cu && !n) {
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t words[BR_HALF_TIMING_WORDS];
        br_half_timing_snapshot(words);
        for (i = 0u; i < BR_HALF_TIMING_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = BR_HALF_TIMING_WORDS * 4u;
#else
        return 0;
#endif
#endif
    }
#if FM1_ENABLE_TIMER4_REGISTER_SNAPSHOT
    else if (cmd == 0x98u && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[3];
        fm1_timer4_read_snapshot(words);
        for (i = 0u; i < 3u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 12u;
#else
        return 0;
#endif
    }
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    else if (cmd == 0x90u && n == 8u && body) {
#if BR_AUDIO_FUSION_JOB_ROW_PAGE_DIAG && defined(BR_SPLIT_AUDIO) && \
    (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
        br_audio_source_job_timing_t sample;
        uint32_t words[32] = {0};
        uint32_t requested_generation, page, page_count, base, end, row;
        if (body[0] != 'B' || body[1] != 'R' || body[2] != 1u)
            return 0; /* exact BR versioned request only */
        page = body[3];
        requested_generation = (uint32_t)body[4] |
                               ((uint32_t)body[5] << 8) |
                               ((uint32_t)body[6] << 16) |
                               ((uint32_t)body[7] << 24);
        if (!br_audio_source_job_timing_snapshot_bounded(&sample, 8u))
            return 0; /* bounded retry: never spin on a changing seal */
        if (sample.version != 1u && sample.version != 2u) return 0;
        if (!sample.valid || sample.completed != sample.job_count ||
            sample.generation != requested_generation ||
            !sample.job_row_count || sample.job_row_count > 8u ||
            sample.job_row_count > sample.job_count) return 0;
        page_count = (sample.job_row_count + 5u) / 6u;
        if (page >= page_count) return 0;
        base = page * 6u;
        end = base + 6u;
        if (end > sample.job_row_count) end = sample.job_row_count;
        words[0] = 1u; /* job-row page schema */
        words[1] = sample.generation;
        words[2] = sample.queue_wall_ticks;
        words[3] = br_diagnostic_job_rows_hash(&sample);
        words[4] = page;
        words[5] = page_count;
        words[6] = sample.job_row_count;
        words[7] = end - base;
        for (row = base; row < end; ++row) {
            uint32_t out = 8u + (row - base) * 4u;
            words[out] = sample.job_rows[row].meta;
            words[out + 1u] = sample.job_rows[row].cost_hint_core0;
            words[out + 2u] = sample.job_rows[row].cost_hint_core1;
            words[out + 3u] = sample.job_rows[row].duration_ticks;
        }
        for (i = 0u; i < 32u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = sizeof words;
#else
        return 0;
#endif
    } else if (cmd == 0x90u && !n) {
#if defined(BR_SPLIT_AUDIO) && (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
        /* A single sealed, fully joined queue. Counts use five bits each;
         * timing fields remain uncompressed T4 ticks. This fits the existing
         * 128-byte reply buffer without changing any transport limit. */
        br_audio_source_job_timing_t sample;
        uint32_t words[27] = {0};
        uint32_t word_count;
        br_audio_source_job_timing_snapshot(&sample);
        if (sample.version != 1u) return 0; /* diagnostic not compiled */
        word_count = 27u;
        words[0] = sample.version; words[1] = sample.generation;
        words[2] = sample.valid; words[3] = sample.frames;
        words[4] = sample.job_count; words[5] = sample.completed;
        words[6] = sample.queue_wall_ticks;
        for (uint32_t core = 0u; core < 2u; ++core) {
            words[7u + core] = sample.core_ticks[core];
            if (sample.core_jobs[core] > 16u) return 0;
            words[23] |= sample.core_jobs[core] << (5u * core);
            for (uint32_t kind = 0u; kind < 3u; ++kind) {
                uint32_t slot = 3u * core + kind;
                words[9u + slot] = sample.kind_ticks[core][kind];
                if (sample.kind_jobs[core][kind] > 16u) return 0;
                words[24] |= sample.kind_jobs[core][kind] << (5u * slot);
            }
            for (uint32_t part = 0u; part < 4u; ++part) {
                words[15u + 4u * core + part] = sample.part_ticks[core][part];
                if (sample.part_jobs[core][part] > 16u) return 0;
                words[25u + core] |= sample.part_jobs[core][part] << (5u * part);
            }
        }
        for (i = 0u; i < word_count; ++i) br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = word_count * 4u;
#else
        return 0;
#endif
    } else if (cmd == 0x91u && !n) {
#if defined(BR_SPLIT_AUDIO) && (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
        /* Same peak queue as 0x90: source/post and FX are disjoint intervals
         * within each job. Keep the entire reply within the existing buffer. */
        br_audio_source_stage_timing_t sample;
        uint32_t words[BR_AUDIO_SOURCE_STAGE_TIMING_WORDS];
        br_audio_source_stage_timing_snapshot(&sample);
        if (sample.version != 1u) return 0;
        memcpy(words, &sample, sizeof words);
        for (i = 0u; i < BR_AUDIO_SOURCE_STAGE_TIMING_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = sizeof words;
#else
        return 0;
#endif
#if defined(FM1_DEVICE) && FM1_RAM_IMAGE_CRC_DIAG
    } else if (cmd == 0x93u && !n) {
        uint32_t words[FM1_RAM_IMAGE_CRC_DIAG_WORDS];
        fm1_ram_image_crc_diag_snapshot(words);
        for (i = 0u; i < FM1_RAM_IMAGE_CRC_DIAG_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = FM1_RAM_IMAGE_CRC_DIAG_BYTES;
#endif
    } else if (cmd == 0x92u && n == 1u && body) {
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG && BR_SPLIT_AUDIO && \
    (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
        uint32_t words[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS];
        uint32_t available;
        if (body[0] >= BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_COUNT) return 0;
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
#endif
        available = br_audio_failure_job_profile_read_page(body[0], words);
#if defined(FM1_DEVICE)
        irq_restore(irq);
#endif
        if (!available) return 0;
        for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS * 4u;
#else
        return 0;
#endif
    }
#endif
    else if (cmd == 0x8Du && !n) {
#if FM1_RUNTIME_IRQ_PROFILE && (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
        /* Eight little-endian u32s: schema, valid, generation,
         * full-half ACK ticks, callback ticks, summed Timer5 body ticks,
         * summed entry-to-after-scan prefix ticks, and Timer5 IRQ count.
         * The two sums overlap; prefix is not scanner-only time. */
        uint32_t words[8] = {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
        /* The pool-backed record is uninitialized before the quiescent reset.
         * Keep this pre-ready command reply schema-valid without reading it. */
        if (br_half_timing_pool_ready)
            fm1_runtime_irq_profile_snapshot(&words[1]);
        for (i = 0u; i < 8u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 32u;
#else
        return 0;
#endif
#if FM1_RUNTIME_CHUNK_PROFILE && (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
    } else if (cmd == 0x8Eu && n == 1u && body) {
        uint32_t words[FM1_RUNTIME_TRACE_PAGE_WORDS];
        uint32_t available;
        /* The trace store lives in .pool and is not implicitly initialized.
         * 0x8E is permitted through the sticky-fault read-only gate, so avoid
         * touching it until the safe startup/0x61 reset path has initialized
         * its control words. */
        if (!br_half_timing_pool_ready) return 0;
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
#endif
        available = fm1_runtime_trace_read_page(body[0], words);
#if defined(FM1_DEVICE)
        irq_restore(irq);
#endif
        if (!available) return 0;
        for (i = 0u; i < FM1_RUNTIME_TRACE_PAGE_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = FM1_RUNTIME_TRACE_PAGE_WORDS * 4u;
#endif
#if FM1_DRUM_PROFILE && (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST))
    } else if (cmd == 0x8fu && n == 1u && body) {
        uint32_t words[FM1_DRUM_PROFILE_PAGE_WORDS];
        uint32_t available;
        /* The profile pool is not startup-zeroed. This read-only command is
         * also allowed through sticky fault handling, so do not touch it
         * before the safe quiescent reset has initialized the store. */
        if (!br_half_timing_pool_ready) return 0;
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
#endif
        available = fm1_drum_profile_read_page(body[0], words);
#if defined(FM1_DEVICE)
        irq_restore(irq);
#endif
        if (!available) return 0;
        for (i = 0u; i < FM1_DRUM_PROFILE_PAGE_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = FM1_DRUM_PROFILE_PAGE_WORDS * 4u;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0x69u && !n) {
#if defined(FM1_DEVICE)
        uint32_t irq = irq_save();
#endif
        for (i = 0; i < 4u; ++i) br_diagnostic_word(reply + 4u * i, br_diagnostic_profile[i]);
#if defined(FM1_DEVICE)
        irq_restore(irq);
#endif
        *reply_n = 16u;
    } else if (cmd == 0x9Du && (!n || (n == 6u && body &&
               body[0]=='B' && body[1]=='R' && body[2]==1u))) {
#if defined(FM1_DEVICE)
        /* Guarded synthetic panel edges for end-to-end device validation.
         * They run in the normal foreground input path, never in this handler. */
        if(n) {
            uint32_t kind=body[3],id=body[4],value=body[5];
            if(fm1_in.buttons || fm1_in.notes || br_project_diag_down || br_project_diag_up ||
               br_project_diag_knob1 || br_project_diag_knob2) return 0;
#if defined(FM1_PATTERN_PANEL_TEST) && FM1_PATTERN_PANEL_TEST
            if(kind==6u && id==0u && value==0u) { /* appended status below */ }
            else if(kind==4u && id<27u && value==0u && br_black_key(id) &&
               br_ui.page==BR_PAGE_PS && !es.mode && !br_pattern_diag_key &&
               !br_ui.previous_keys) br_pattern_diag_key=1u<<id;
            else if(kind==5u && id<27u && value==0u && br_pattern_diag_key==(1u<<id) &&
                    (br_ui.previous_keys & br_pattern_diag_key)) br_pattern_diag_key=0u;
            else
#endif
            if(kind==1u && id<14u && value==0u)br_project_diag_down=1u<<id;
            else if(kind==2u && id<14u && value==0u)br_project_diag_up=1u<<id;
            else if(kind==3u && id<2u && value<=32u && es.mode) {
                if(id==0u)br_project_diag_knob1=(int32_t)value-16;
                else br_project_diag_knob2=(int32_t)value-16;
            } else return 0;
        }
        reply[0]=1u;reply[1]=(uint8_t)es.mode;reply[2]=(uint8_t)es.action;
        reply[3]=(uint8_t)(es.slot+1);reply[4]=(uint8_t)(es.current+1);
        reply[5]=(uint8_t)br_recording;reply[6]=(uint8_t)br_playing;
        reply[7]=(uint8_t)es.pending;reply[8]=es_directory[es.slot].state;
        reply[9]=(uint8_t)(es.error[0]!=0);
        reply[10]=(uint8_t)(es.mode==4?es.cursor:0xff);
        reply[11]=(uint8_t)(es.mode==4?es.name[es.cursor]:0);
        *reply_n=12u;
#if defined(FM1_PATTERN_PANEL_TEST) && FM1_PATTERN_PANEL_TEST
        if(n && body[3]==6u) {
            memcpy(reply+12,&br_ps_state.used,sizeof br_ps_state.used);
            reply[14]=br_ps_state.active;reply[15]=br_ps_state.queued;
            reply[16]=(uint8_t)((br_pattern_diag_key!=0u) | (((br_ui.previous_keys & br_pattern_diag_key)!=0u)<<1));
            *reply_n=17u;
        }
#endif
#else
        return 0;
#endif
    } else if (cmd == 0x9Eu && (!n || (n == 6u && body &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u))) {
#if defined(FM1_DEVICE)
        /* Exercise the real foreground drawing path in hardware benchmarks.
         * This changes UI navigation only; it never saves or changes sound. */
        if (n) {
            uint32_t irq;
            if (body[3] >= BR_PARTS || body[4] >= BR_PAGE_COUNT ||
                br_step_press_pending() || br_seq_step_edit() ||
                !br_foreground_mutation_begin(&irq)) return 0;
            br_ui.part = body[3]; br_ui.page = body[4];
            br_ui.screen[br_ui.page] = body[5] % br_screen_count();
            br_ui.redraw = 1u;
            br_foreground_mutation_end(irq);
        }
        reply[0] = 1u; reply[1] = br_ui.part; reply[2] = br_ui.page;
        reply[3] = br_ui.screen[br_ui.page]; *reply_n = 4u;
#else
        return 0;
#endif
#endif
    } else if (cmd == 0xb4u && n == 4u && body &&
               body[0] == 'B' && body[1] == 'R' && body[2] == 1u && body[3] <= 1u) {
#if (defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)) && \
    defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        uint32_t irq, accepted = 0u, held = 0u;
        if (!br_foreground_mutation_begin(&irq)) return 0;
        /* Check the scanner and the foreground's held-key/route state under
         * the same mutation guard. A released-but-not-yet-processed note is
         * conservatively treated as held. No SAVE or project restore. */
        held = fm1_in.notes || fm1_in.buttons || br_step_press_pending() || br_seq_step_edit();
#if defined(FM1_DEVICE)
        held |= fm1_in.notes_pressed || fm1_in.pressed;
#endif
        for (i = 0u; i < 27u; ++i) if (br_ui.held_note[i]) held = 1u;
        for (i = 0u; i < BR_MIDI_ROUTE_NOTES; ++i) if (br_midi_routes.note[i].channel) held = 1u;
        for (i = 0u; i < BR_SYNTH_PARTS; ++i) if (br_arp_keys[i].count) held = 1u;
        if (!held && !br_playing && !br_diagnostic_armed && !br_diagnostic_muted &&
            !br_diagnostic_abort && br_engine_quiet() && br_fx_quiet()) {
            if (br_midi_clock_enabled != body[3]) {
                br_engine_clock_source_changed(fm1_ticks());
                br_midi_clock_enabled = body[3];
                br_ui.redraw = 1u;
            }
            accepted = 1u;
        }
        br_diagnostic_word(reply, accepted);
        br_diagnostic_word(reply + 4u, br_midi_clock_enabled);
        *reply_n = 8u;
        br_foreground_mutation_end(irq);
#else
        return 0;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0xb3u && !n) {
        uint32_t words[BR_CLOCK_DIAG_WORDS] = {2u, 0u};
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        uint32_t irq = br_foreground_irq_save();
        br_engine_clock_diag_snapshot(words);
        br_foreground_irq_restore(irq);
#endif
        for (i = 0u; i < BR_CLOCK_DIAG_WORDS; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = BR_CLOCK_DIAG_WORDS * 4u;
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    } else if (cmd == 0xb2u && !n) {
        /* Schema 3 preserves the schema-2 words and appends contained-fault
         * and partial-drop counts. Exclude audio while reading both owners. */
        uint32_t words[17] = {3u}, engine[9];
        uint32_t irq = br_foreground_irq_save();
        br_audio_late_drop_snapshot(engine);
        for (i = 0u; i < 6u; ++i) words[1u + i] = engine[i];
        words[11] = engine[6];
        words[12] = engine[7];
#if defined(FM1_DEVICE)
        words[7] = fm1_audio_late_drops;
        words[8] = fm1_audio_late_resync_skips;
        words[9] = fm1_audio_late_resync_active;
        words[10] = fm1_audio_late_resync_baseline;
        words[13] = fm1_audio_late_over_one_half;
        words[14] = fm1_audio_late_over_two_halves;
        words[15] = fm1_audio_late_contained_faults;
#endif
        words[16] = engine[8];
        br_foreground_irq_restore(irq);
        for (i = 0u; i < 17u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 68u;
    } else if (cmd == 0xafu || cmd == 0xb0u) {
#if defined(FM1_DEVICE)
        return fm1_vendor360_diag_command(cmd,body,n,reply,reply_n);
#else
        return 0;
#endif
#endif
#if defined(FM1_ENABLE_P33_REGISTER_SNAPSHOT) && FM1_ENABLE_P33_REGISTER_SNAPSHOT
    /* Optional voltage-research probe; keep release space for playing/UI.
     * Fault, clock, worker and timing diagnostics remain available. */
    } else if (cmd == 0xaeu) {
#if defined(FM1_DEVICE)
        return fm1_power_read_command(body, n, reply, reply_n);
#else
        return 0;
#endif
    } else if (cmd == 0x9bu && !n) {
#if defined(FM1_DEVICE)
        /* Work counters: ticks[part][core] then jobs[part][core], free-running. They live in
         * the separately compiled audio unit. */
        extern uint32_t br_audio_work_ticks[BR_PARTS][2], br_audio_work_jobs[BR_PARTS][2];
        extern uint32_t br_fx_master_work_ticks, br_fx_gr_work_ticks;
        for (i = 0u; i < 8u; ++i) {
            br_diagnostic_word(reply + 4u * i, br_audio_work_ticks[i >> 1][i & 1u]);
            br_diagnostic_word(reply + 32u + 4u * i, br_audio_work_jobs[i >> 1][i & 1u]);
        }
        br_diagnostic_word(reply + 64u, br_fx_master_work_ticks);
        br_diagnostic_word(reply + 68u, br_fx_gr_work_ticks);
        {
            extern uint32_t br_audio_split_blocks, br_audio_split_tracks;
            br_diagnostic_word(reply + 72u, br_audio_split_blocks);
            br_diagnostic_word(reply + 76u, br_audio_split_tracks);
        }
        *reply_n = 80u;
#else
        return 0;
#endif
    } else if (cmd == 0x99u && !n) {
#if defined(FM1_DEVICE)
        uint32_t words[14];
        fm1_p33_snapshot_words(words);
        for (i = 0u; i < 14u; ++i)
            br_diagnostic_word(reply + 4u * i, words[i]);
        *reply_n = 56u;
#else
        return 0;
#endif
#endif
    } else return 0;
    return 1;
}
