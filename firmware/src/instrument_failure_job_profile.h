/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BRAIDS_INSTRUMENT_FAILURE_JOB_PROFILE_H
#define BRAIDS_INSTRUMENT_FAILURE_JOB_PROFILE_H

#include <stdint.h>

/* Private, failure-only job snapshot. The production default is deliberately
 * off; with it off, neither the source-job ABI nor the audio path changes. */
#ifndef BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_PROFILE_DIAG 0
#endif
#ifndef BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2
#define BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2 0
#endif
#if BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2 != 0 && \
    BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2 != 1
#error "BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2 must be 0 or 1"
#endif
#ifndef BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG 0
#endif
#ifndef BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG 0
#endif
#ifndef BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG 0
#endif
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG && !BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
#error "failure stage profile requires the base failure-job profile"
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG && \
    (!BR_AUDIO_FAILURE_JOB_PROFILE_DIAG || \
     !BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG)
#error "FX substage profile requires the base and stage failure-job profiles"
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG && \
    (!BR_AUDIO_FAILURE_JOB_PROFILE_DIAG || \
     !BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG)
#error "Model substage profile requires the base and stage failure-job profiles"
#endif

#define BR_AUDIO_FAILURE_JOB_PROFILE_SCHEMA 1u
#define BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_SCHEMA 2u
#define BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_SCHEMA 3u
#define BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_SCHEMA 4u
#define BR_AUDIO_FAILURE_JOB_PROFILE_CAP 8u
#define BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS 32u
#define BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS 3u
#define BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT \
    ((BR_AUDIO_FAILURE_JOB_PROFILE_CAP + BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS - 1u) / \
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS)
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT \
    BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT
#else
#define BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT 0u
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_RECORDS 2u
#define BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_RECORD_WORDS 11u
#define BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_HEADER_WORDS 10u
#define BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_COUNT \
    ((BR_AUDIO_FAILURE_JOB_PROFILE_CAP + \
      BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_RECORDS - 1u) / \
     BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_RECORDS)
#else
#define BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_COUNT 0u
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_PAGE_COUNT \
    BR_AUDIO_FAILURE_JOB_PROFILE_CAP
#else
#define BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_PAGE_COUNT 0u
#endif
#define BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_COUNT \
    (BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT + \
     BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT + \
     BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_COUNT + \
     BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_PAGE_COUNT)
#define BR_AUDIO_FAILURE_JOB_PROFILE_RECORD_WORDS 7u
#define BR_AUDIO_FAILURE_JOB_PROFILE_HEADER_WORDS 10u
#define BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_RECORD_WORDS 7u
#define BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_HEADER_WORDS 10u
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_HEADER_WORDS 10u
#define BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_RECORD_WORDS 22u
#endif
typedef char br_audio_failure_profile_page_fits[
    (BR_AUDIO_FAILURE_JOB_PROFILE_HEADER_WORDS +
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS *
     BR_AUDIO_FAILURE_JOB_PROFILE_RECORD_WORDS <=
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS) ? 1 : -1];
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
typedef char br_audio_failure_stage_profile_page_fits[
    (BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_HEADER_WORDS +
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS *
     BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_RECORD_WORDS <=
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS) ? 1 : -1];
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
typedef char br_audio_failure_fx_substage_profile_page_fits[
    (BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_HEADER_WORDS +
     BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_RECORDS *
     BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_RECORD_WORDS <=
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS) ? 1 : -1];
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
typedef char br_audio_failure_model_substage_profile_page_fits[
    (BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_HEADER_WORDS +
     BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_RECORD_WORDS <=
     BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS) ? 1 : -1];
#endif

enum {
    BR_AUDIO_FAILURE_PROFILE_VALID = 1u << 0,
    BR_AUDIO_FAILURE_PROFILE_FAILED_JOIN = 1u << 1,
    BR_AUDIO_FAILURE_PROFILE_RESET_CONFIRMED = 1u << 2,
    BR_AUDIO_FAILURE_PROFILE_CUT_BEFORE_RESET = 1u << 3,
    BR_AUDIO_FAILURE_PROFILE_TRUNCATED = 1u << 4,
    BR_AUDIO_FAILURE_PROFILE_ESTIMATED_QUEUE_ORIGIN = 1u << 5,
    BR_AUDIO_FAILURE_PROFILE_ESTIMATED_SOURCE_START = 1u << 6,

    /* fixed_plan_mode: the active bit describes the plan at failure time;
     * the remaining bits describe which plan variants this image enables. */
    BR_AUDIO_FAILURE_PLAN_ACTIVE = 1u << 0,
    BR_AUDIO_FAILURE_PLAN_FIXED_ENABLED = 1u << 1,
    BR_AUDIO_FAILURE_PLAN_GENERAL_EIGHT = 1u << 2,
    BR_AUDIO_FAILURE_PLAN_PART0_CPU0 = 1u << 3,
    BR_AUDIO_FAILURE_PLAN_FORCE_VALID_SIX = 1u << 4,
    BR_AUDIO_FAILURE_PLAN_BALANCED_STARTUP = 1u << 5,
    BR_AUDIO_FAILURE_PLAN_FAST_CUTOFF = 1u << 6,

    BR_AUDIO_FAILURE_JOB_START_VALID = 1u << 0,
    BR_AUDIO_FAILURE_JOB_CALLBACK_RETURNED = 1u << 1,
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    BR_AUDIO_FAILURE_STAGE_SOURCE_DONE = 1u << 0,
    BR_AUDIO_FAILURE_STAGE_MERGE_DONE = 1u << 1,
    BR_AUDIO_FAILURE_STAGE_POST_DONE = 1u << 2,
    BR_AUDIO_FAILURE_STAGE_FX_DONE = 1u << 3,
    BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID = 1u << 4,
    BR_AUDIO_FAILURE_STAGE_ACTIVE_SHIFT = 8u,
    BR_AUDIO_FAILURE_STAGE_ACTIVE_MASK = 0x7u << BR_AUDIO_FAILURE_STAGE_ACTIVE_SHIFT,
    BR_AUDIO_FAILURE_STAGE_ID_SOURCE = 1u,
    BR_AUDIO_FAILURE_STAGE_ID_MERGE = 2u,
    BR_AUDIO_FAILURE_STAGE_ID_POST = 3u,
    BR_AUDIO_FAILURE_STAGE_ID_FX = 4u,
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
    BR_AUDIO_FX_SUBSTAGE_DONE_DRIVE = 1u << 0,
    BR_AUDIO_FX_SUBSTAGE_DONE_MOD = 1u << 1,
    BR_AUDIO_FX_SUBSTAGE_DONE_DELAY = 1u << 2,
    BR_AUDIO_FX_SUBSTAGE_DONE_REVERB = 1u << 3,
    BR_AUDIO_FX_SUBSTAGE_ACTIVE_VALID = 1u << 4,
    BR_AUDIO_FX_SUBSTAGE_EDGE_PATH = 1u << 5,
    BR_AUDIO_FX_SUBSTAGE_RESIDUAL_VALID = 1u << 6,
    BR_AUDIO_FX_SUBSTAGE_ACTIVE_SHIFT = 8u,
    BR_AUDIO_FX_SUBSTAGE_ACTIVE_MASK = 0x7u << BR_AUDIO_FX_SUBSTAGE_ACTIVE_SHIFT,
    BR_AUDIO_FX_SUBSTAGE_ID_DRIVE = 1u,
    BR_AUDIO_FX_SUBSTAGE_ID_MOD = 2u,
    BR_AUDIO_FX_SUBSTAGE_ID_DELAY = 3u,
    BR_AUDIO_FX_SUBSTAGE_ID_REVERB = 4u,
    BR_AUDIO_FX_SUBSTAGE_DONE_MASK = 0x0fu,
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
    BR_AUDIO_MODEL_SUBSTAGE_DONE_PREP = 1u << 0,
    BR_AUDIO_MODEL_SUBSTAGE_DONE_EXCITER = 1u << 1,
    BR_AUDIO_MODEL_SUBSTAGE_DONE_AUX_COPY = 1u << 2,
    BR_AUDIO_MODEL_SUBSTAGE_DONE_RESONATOR = 1u << 3,
    BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_VALID = 1u << 4,
    BR_AUDIO_MODEL_SUBSTAGE_RESIDUAL_VALID = 1u << 5,
    BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_SHIFT = 8u,
    BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_MASK = 0x7u << BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_SHIFT,
    BR_AUDIO_MODEL_SUBSTAGE_ID_PREP = 1u,
    BR_AUDIO_MODEL_SUBSTAGE_ID_EXCITER = 2u,
    BR_AUDIO_MODEL_SUBSTAGE_ID_AUX_COPY = 3u,
    BR_AUDIO_MODEL_SUBSTAGE_ID_RESONATOR = 4u,
    BR_AUDIO_MODEL_SUBSTAGE_DONE_MASK = 0x0fu,
#endif

    BR_AUDIO_FAILURE_META_STATE_MASK = 0x3u,
    BR_AUDIO_FAILURE_META_KIND_SHIFT = 2u,
    BR_AUDIO_FAILURE_META_PART_SHIFT = 4u,
    BR_AUDIO_FAILURE_META_CORE_SHIFT = 8u,
    BR_AUDIO_FAILURE_META_STARTED = 1u << 10,
    BR_AUDIO_FAILURE_META_CALLBACK_RETURNED_BY_RESET = 1u << 11,
    BR_AUDIO_FAILURE_META_QUEUE_DONE_AT_RESET = 1u << 12,
    BR_AUDIO_FAILURE_META_CALLBACK_RETURNED_BY_CUT = 1u << 13,
    BR_AUDIO_FAILURE_META_UNFINISHED_AT_CUT = 1u << 14,
    BR_AUDIO_FAILURE_META_STARTED_AFTER_CUT = 1u << 15,
    BR_AUDIO_FAILURE_META_DURATION_VALID = 1u << 16,
    BR_AUDIO_FAILURE_META_START_AFTER_DEADLINE = 1u << 17
};

#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_FX_SUBSTAGE_UNKNOWN_TICKS UINT32_MAX
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
#define BR_AUDIO_MODEL_SUBSTAGE_UNKNOWN_TICKS UINT32_MAX
#endif

#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
/* Written only by the owner of this part's FX job, then copied after worker
 * reset/containment. timer_reads counts added coarse boundary reads; no timer
 * is read inside a sample loop. */
typedef struct {
    uint32_t flags;
    uint32_t drive_ticks;
    uint32_t mod_ticks;
    uint32_t delay_ticks;
    uint32_t reverb_ticks;
    uint32_t active_start_tick;
    uint32_t timer_reads;
} br_audio_fx_substage_trace_t;
#endif

#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
/* Written by the source-job owner and copied only after containment/reset. */
typedef struct {
    uint32_t flags;
    uint32_t calls[4];
    uint32_t frames[4];
    uint32_t ticks[4];
    uint32_t max_ticks[4];
    uint32_t active_start_tick;
    uint32_t timer_reads;
} br_audio_model_substage_trace_t;

/* The current trace is selected once per model Render. begin/end are coarse
 * boundaries; a null trace performs no timer reads. */
#ifdef __cplusplus
extern "C" {
#endif
br_audio_model_substage_trace_t *br_audio_model_substage_current_trace(void);
uint32_t br_audio_model_substage_begin(
    br_audio_model_substage_trace_t *trace, uint32_t stage);
void br_audio_model_substage_end(
    br_audio_model_substage_trace_t *trace, uint32_t stage, uint32_t frames);
#ifdef __cplusplus
}
#endif
#endif

typedef struct {
    uint32_t state;
    uint32_t kind;
    uint32_t part;
    uint32_t executor_core;
    uint32_t frames;
    uint32_t cost_hint;
    uint32_t duration_ticks;
    uint32_t start_tick;
    uint32_t profile_flags;
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    uint32_t stage_flags;
    uint32_t stage_start_tick;
    uint32_t source_ticks;
    uint32_t merge_ticks;
    uint32_t post_ticks;
    uint32_t fx_ticks;
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
    uint32_t fx_substage_flags;
    uint32_t fx_substage_drive_ticks;
    uint32_t fx_substage_mod_ticks;
    uint32_t fx_substage_delay_ticks;
    uint32_t fx_substage_reverb_ticks;
    uint32_t fx_substage_active_start_tick;
    uint32_t fx_substage_timer_reads;
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
    uint32_t model_substage_flags;
    uint32_t model_substage_calls[4];
    uint32_t model_substage_frames[4];
    uint32_t model_substage_ticks[4];
    uint32_t model_substage_max_ticks[4];
    uint32_t model_substage_active_start_tick;
    uint32_t model_substage_timer_reads;
#endif
} br_audio_failure_job_sample_t;

typedef struct {
    uint32_t metadata;
    uint32_t frames;
    uint32_t cost_hint;
    uint32_t callback_duration_ticks;
    uint32_t start_offset_ticks;
    uint32_t deadline_minus_start_ticks;
    uint32_t elapsed_lower_bound_ticks;
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    uint32_t stage_flags;
    uint32_t source_ticks;
    uint32_t merge_ticks;
    uint32_t post_ticks;
    uint32_t fx_ticks;
    uint32_t active_stage_lower_bound_ticks;
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
    uint32_t fx_substage_flags;
    uint32_t fx_substage_drive_ticks;
    uint32_t fx_substage_mod_ticks;
    uint32_t fx_substage_delay_ticks;
    uint32_t fx_substage_reverb_ticks;
    uint32_t fx_substage_unattributed_ticks;
    uint32_t fx_substage_active_id;
    uint32_t fx_substage_active_lower_bound_ticks;
    uint32_t fx_substage_timer_reads;
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
    uint32_t model_substage_flags;
    uint32_t model_substage_calls[4];
    uint32_t model_substage_frames[4];
    uint32_t model_substage_ticks[4];
    uint32_t model_substage_max_ticks[4];
    uint32_t model_substage_unattributed_ticks;
    uint32_t model_substage_active_id;
    uint32_t model_substage_active_lower_bound_ticks;
    uint32_t model_substage_timer_reads;
#endif
} br_audio_failure_job_entry_t;

typedef struct {
    uint32_t schema;
    uint32_t generation;
    uint32_t flags;
    uint32_t frames;
    uint32_t job_count;
    uint32_t completed;
    uint32_t recorded_count;
    uint32_t fixed_plan_mode;
    uint32_t queue_origin_tick;
    uint32_t queue_source_start_tick;
    uint32_t deadline_tick;
    uint32_t reset_request_tick;
    br_audio_failure_job_entry_t jobs[BR_AUDIO_FAILURE_JOB_PROFILE_CAP];
} br_audio_failure_job_profile_t;

typedef uint32_t (*br_audio_failure_job_reader_t)(
    void *context, uint32_t index, br_audio_failure_job_sample_t *out);

/* This shared gate is also host-tested with a guarded reader. In particular,
 * no reader call is made unless reset is confirmed and ownership is contained. */
static inline uint32_t br_audio_failure_job_profile_capture(
    br_audio_failure_job_profile_t *out,
    uint32_t reset_confirmed, uint32_t uncontained,
    uint32_t frames, uint32_t job_count, uint32_t completed,
    uint32_t fixed_plan_mode, uint32_t queue_origin_tick,
    uint32_t queue_source_start_tick, uint32_t deadline_tick,
    uint32_t reset_request_tick, uint32_t queue_origin_estimated,
    uint32_t source_start_estimated,
    br_audio_failure_job_reader_t reader, void *context)
{
    uint32_t i, count, generation;
    if (!out) return 0u;
    generation = out->generation + 1u;
    if (!generation) generation = 1u;
    out->flags = 0u;
    if (!reset_confirmed || uncontained || !reader) return 0u;

    out->schema = BR_AUDIO_FAILURE_JOB_PROFILE_SCHEMA;
    out->generation = generation;
    out->frames = frames;
    out->job_count = job_count;
    out->completed = completed;
    out->recorded_count = job_count < BR_AUDIO_FAILURE_JOB_PROFILE_CAP ?
                          job_count : BR_AUDIO_FAILURE_JOB_PROFILE_CAP;
    out->fixed_plan_mode = fixed_plan_mode;
    out->queue_origin_tick = queue_origin_tick;
    out->queue_source_start_tick = queue_source_start_tick;
    out->deadline_tick = deadline_tick;
    out->reset_request_tick = reset_request_tick;
    out->flags = BR_AUDIO_FAILURE_PROFILE_FAILED_JOIN |
                 BR_AUDIO_FAILURE_PROFILE_RESET_CONFIRMED |
                 BR_AUDIO_FAILURE_PROFILE_CUT_BEFORE_RESET |
                 (queue_origin_estimated ?
                    BR_AUDIO_FAILURE_PROFILE_ESTIMATED_QUEUE_ORIGIN : 0u) |
                 (source_start_estimated ?
                    BR_AUDIO_FAILURE_PROFILE_ESTIMATED_SOURCE_START : 0u) |
                 (job_count > BR_AUDIO_FAILURE_JOB_PROFILE_CAP ?
                    BR_AUDIO_FAILURE_PROFILE_TRUNCATED : 0u);

    count = out->recorded_count;
    for (i = 0u; i < count; ++i) {
        br_audio_failure_job_sample_t sample;
        br_audio_failure_job_entry_t *entry = &out->jobs[i];
        uint32_t started, returned, returned_by_cut, started_after_cut;
        int32_t deadline_remaining;
        uint32_t elapsed_to_cut = 0u;
        uint32_t metadata;
        if (!reader(context, i, &sample)) {
            out->flags = 0u;
            return 0u;
        }

        started = (sample.profile_flags & BR_AUDIO_FAILURE_JOB_START_VALID) != 0u;
        returned = (sample.profile_flags & BR_AUDIO_FAILURE_JOB_CALLBACK_RETURNED) != 0u;
        started_after_cut = started &&
            (int32_t)(sample.start_tick - reset_request_tick) > 0;
        deadline_remaining = (int32_t)(deadline_tick - sample.start_tick);
        if (started && !started_after_cut &&
            (int32_t)(reset_request_tick - sample.start_tick) >= 0)
            elapsed_to_cut = reset_request_tick - sample.start_tick;
        returned_by_cut = started && returned && !started_after_cut &&
            (int32_t)(reset_request_tick - sample.start_tick) >= 0 &&
            sample.duration_ticks <= elapsed_to_cut;

        metadata = (sample.state & BR_AUDIO_FAILURE_META_STATE_MASK) |
                   ((sample.kind & 0x3u) << BR_AUDIO_FAILURE_META_KIND_SHIFT) |
                   ((sample.part & 0x0fu) << BR_AUDIO_FAILURE_META_PART_SHIFT) |
                   (((sample.executor_core < 2u ? sample.executor_core : 3u) & 0x3u)
                     << BR_AUDIO_FAILURE_META_CORE_SHIFT);
        if (started) metadata |= BR_AUDIO_FAILURE_META_STARTED;
        if (returned) metadata |= BR_AUDIO_FAILURE_META_CALLBACK_RETURNED_BY_RESET;
        if (sample.state == 2u) metadata |= BR_AUDIO_FAILURE_META_QUEUE_DONE_AT_RESET;
        if (returned_by_cut) metadata |= BR_AUDIO_FAILURE_META_CALLBACK_RETURNED_BY_CUT;
        if (started && !started_after_cut && !returned_by_cut)
            metadata |= BR_AUDIO_FAILURE_META_UNFINISHED_AT_CUT;
        if (started_after_cut) metadata |= BR_AUDIO_FAILURE_META_STARTED_AFTER_CUT;
        if (started && returned) metadata |= BR_AUDIO_FAILURE_META_DURATION_VALID;
        if (started && deadline_remaining <= 0)
            metadata |= BR_AUDIO_FAILURE_META_START_AFTER_DEADLINE;

        entry->metadata = metadata;
        entry->frames = sample.frames;
        entry->cost_hint = sample.cost_hint;
        entry->callback_duration_ticks = started && returned ?
                                         sample.duration_ticks : 0u;
        entry->start_offset_ticks = started ?
                                    sample.start_tick - queue_source_start_tick : 0u;
        entry->deadline_minus_start_ticks = started ?
                                            (uint32_t)deadline_remaining : 0u;
        entry->elapsed_lower_bound_ticks =
            (metadata & BR_AUDIO_FAILURE_META_UNFINISHED_AT_CUT) ?
            elapsed_to_cut : 0u;
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        entry->stage_flags = sample.stage_flags;
        entry->source_ticks = sample.source_ticks;
        entry->merge_ticks = sample.merge_ticks;
        entry->post_ticks = sample.post_ticks;
        entry->fx_ticks = sample.fx_ticks;
        entry->active_stage_lower_bound_ticks = 0u;
        if ((sample.stage_flags & BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) &&
            started && !started_after_cut &&
            (int32_t)(reset_request_tick - sample.stage_start_tick) >= 0)
            entry->active_stage_lower_bound_ticks =
                reset_request_tick - sample.stage_start_tick;
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
        {
            uint32_t stage_sum = 0u;
            uint32_t sum_ok = 1u;
            uint32_t active_id =
                (sample.fx_substage_flags & BR_AUDIO_FX_SUBSTAGE_ACTIVE_MASK) >>
                BR_AUDIO_FX_SUBSTAGE_ACTIVE_SHIFT;
            const uint32_t values[4] = {
                sample.fx_substage_drive_ticks,
                sample.fx_substage_mod_ticks,
                sample.fx_substage_delay_ticks,
                sample.fx_substage_reverb_ticks
            };
            entry->fx_substage_flags = sample.fx_substage_flags;
            entry->fx_substage_drive_ticks = sample.fx_substage_drive_ticks;
            entry->fx_substage_mod_ticks = sample.fx_substage_mod_ticks;
            entry->fx_substage_delay_ticks = sample.fx_substage_delay_ticks;
            entry->fx_substage_reverb_ticks = sample.fx_substage_reverb_ticks;
            entry->fx_substage_active_id = active_id;
            entry->fx_substage_active_lower_bound_ticks = 0u;
            entry->fx_substage_timer_reads = sample.fx_substage_timer_reads;
            entry->fx_substage_unattributed_ticks =
                BR_AUDIO_FX_SUBSTAGE_UNKNOWN_TICKS;
            if ((sample.fx_substage_flags & BR_AUDIO_FX_SUBSTAGE_ACTIVE_VALID) &&
                active_id && started && !started_after_cut &&
                (int32_t)(reset_request_tick -
                          sample.fx_substage_active_start_tick) >= 0)
                entry->fx_substage_active_lower_bound_ticks =
                    reset_request_tick - sample.fx_substage_active_start_tick;
            if ((sample.fx_substage_flags & BR_AUDIO_FX_SUBSTAGE_DONE_MASK) ==
                BR_AUDIO_FX_SUBSTAGE_DONE_MASK) {
                uint32_t k;
                for (k = 0u; k < 4u; ++k) {
                    uint32_t next = stage_sum + values[k];
                    if (next < stage_sum) sum_ok = 0u;
                    stage_sum = next;
                }
                if (sum_ok && stage_sum <= entry->fx_ticks) {
                    entry->fx_substage_unattributed_ticks =
                        entry->fx_ticks - stage_sum;
                    entry->fx_substage_flags |=
                        BR_AUDIO_FX_SUBSTAGE_RESIDUAL_VALID;
                }
            }
        }
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
        {
            uint32_t stage_sum = 0u;
            uint32_t sum_ok = 1u;
            uint32_t has_data = sample.model_substage_flags ||
                                sample.model_substage_timer_reads;
            uint32_t active_id =
                (sample.model_substage_flags &
                 BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_MASK) >>
                BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_SHIFT;
            uint32_t k;
            entry->model_substage_flags = sample.model_substage_flags;
            entry->model_substage_active_id = active_id;
            entry->model_substage_active_lower_bound_ticks = 0u;
            entry->model_substage_timer_reads = sample.model_substage_timer_reads;
            entry->model_substage_unattributed_ticks =
                has_data ? BR_AUDIO_MODEL_SUBSTAGE_UNKNOWN_TICKS : 0u;
            for (k = 0u; k < 4u; ++k) {
                uint32_t next;
                entry->model_substage_calls[k] = sample.model_substage_calls[k];
                entry->model_substage_frames[k] = sample.model_substage_frames[k];
                entry->model_substage_ticks[k] = sample.model_substage_ticks[k];
                entry->model_substage_max_ticks[k] =
                    sample.model_substage_max_ticks[k];
                if (sample.model_substage_calls[k] ||
                    sample.model_substage_frames[k] ||
                    sample.model_substage_ticks[k] ||
                    sample.model_substage_max_ticks[k]) has_data = 1u;
                next = stage_sum + sample.model_substage_ticks[k];
                if (next < stage_sum) sum_ok = 0u;
                stage_sum = next;
            }
            if (has_data && !sample.model_substage_flags &&
                !sample.model_substage_timer_reads)
                entry->model_substage_unattributed_ticks =
                    BR_AUDIO_MODEL_SUBSTAGE_UNKNOWN_TICKS;
            if ((sample.model_substage_flags &
                 BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_VALID) && active_id && started &&
                !started_after_cut &&
                (int32_t)(reset_request_tick -
                          sample.model_substage_active_start_tick) >= 0)
                entry->model_substage_active_lower_bound_ticks =
                    reset_request_tick - sample.model_substage_active_start_tick;
            if ((sample.model_substage_flags &
                 BR_AUDIO_MODEL_SUBSTAGE_DONE_MASK) ==
                    BR_AUDIO_MODEL_SUBSTAGE_DONE_MASK &&
                !(sample.model_substage_flags &
                  BR_AUDIO_MODEL_SUBSTAGE_ACTIVE_VALID) &&
                sum_ok && stage_sum <= entry->source_ticks) {
                entry->model_substage_unattributed_ticks =
                    entry->source_ticks - stage_sum;
                entry->model_substage_flags |=
                    BR_AUDIO_MODEL_SUBSTAGE_RESIDUAL_VALID;
            }
        }
#endif
    }
    for (; i < BR_AUDIO_FAILURE_JOB_PROFILE_CAP; ++i) {
        out->jobs[i].metadata = 0u;
        out->jobs[i].frames = 0u;
        out->jobs[i].cost_hint = 0u;
        out->jobs[i].callback_duration_ticks = 0u;
        out->jobs[i].start_offset_ticks = 0u;
        out->jobs[i].deadline_minus_start_ticks = 0u;
        out->jobs[i].elapsed_lower_bound_ticks = 0u;
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        out->jobs[i].stage_flags = 0u;
        out->jobs[i].source_ticks = 0u;
        out->jobs[i].merge_ticks = 0u;
        out->jobs[i].post_ticks = 0u;
        out->jobs[i].fx_ticks = 0u;
        out->jobs[i].active_stage_lower_bound_ticks = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
        out->jobs[i].fx_substage_flags = 0u;
        out->jobs[i].fx_substage_drive_ticks = 0u;
        out->jobs[i].fx_substage_mod_ticks = 0u;
        out->jobs[i].fx_substage_delay_ticks = 0u;
        out->jobs[i].fx_substage_reverb_ticks = 0u;
        out->jobs[i].fx_substage_unattributed_ticks = 0u;
        out->jobs[i].fx_substage_active_id = 0u;
        out->jobs[i].fx_substage_active_lower_bound_ticks = 0u;
        out->jobs[i].fx_substage_timer_reads = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
        out->jobs[i].model_substage_flags = 0u;
        for (uint32_t k = 0u; k < 4u; ++k) {
            out->jobs[i].model_substage_calls[k] = 0u;
            out->jobs[i].model_substage_frames[k] = 0u;
            out->jobs[i].model_substage_ticks[k] = 0u;
            out->jobs[i].model_substage_max_ticks[k] = 0u;
        }
        out->jobs[i].model_substage_unattributed_ticks = 0u;
        out->jobs[i].model_substage_active_id = 0u;
        out->jobs[i].model_substage_active_lower_bound_ticks = 0u;
        out->jobs[i].model_substage_timer_reads = 0u;
#endif
    }
    out->flags |= BR_AUDIO_FAILURE_PROFILE_VALID;
    return 1u;
}

static inline uint32_t br_audio_failure_job_profile_encode_page(
    const br_audio_failure_job_profile_t *profile, uint32_t page,
    uint32_t out[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS])
{
    uint32_t i, first;
    if (!profile || !out || page >= BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_VALID) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_RESET_CONFIRMED) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_FAILED_JOIN)) return 0u;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS; ++i) out[i] = 0u;
    out[0] = profile->schema;
    out[1] = profile->generation;
    out[2] = profile->flags;
    out[3] = profile->frames;
    out[4] = (profile->job_count & 0xffu) |
             ((profile->completed & 0xffu) << 8) |
             ((profile->recorded_count & 0xffu) << 16);
    out[5] = profile->fixed_plan_mode;
    out[6] = profile->queue_origin_tick;
    out[7] = profile->queue_source_start_tick;
    out[8] = profile->deadline_tick;
    out[9] = profile->reset_request_tick;
    first = page * BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS; ++i) {
        uint32_t index = first + i;
        uint32_t base = BR_AUDIO_FAILURE_JOB_PROFILE_HEADER_WORDS +
                        i * BR_AUDIO_FAILURE_JOB_PROFILE_RECORD_WORDS;
        if (index < profile->recorded_count) {
            const br_audio_failure_job_entry_t *entry = &profile->jobs[index];
            out[base] = entry->metadata;
            out[base + 1u] = entry->frames;
            out[base + 2u] = entry->cost_hint;
            out[base + 3u] = entry->callback_duration_ticks;
            out[base + 4u] = entry->start_offset_ticks;
            out[base + 5u] = entry->deadline_minus_start_ticks;
            out[base + 6u] = entry->elapsed_lower_bound_ticks;
        }
    }
    return 1u;
}

#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
static inline uint32_t br_audio_failure_job_profile_encode_stage_page(
    const br_audio_failure_job_profile_t *profile, uint32_t page,
    uint32_t out[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS])
{
    uint32_t i, first;
    if (!profile || !out ||
        page >= BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_VALID) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_RESET_CONFIRMED) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_FAILED_JOIN)) return 0u;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS; ++i) out[i] = 0u;
    out[0] = BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_SCHEMA;
    out[1] = profile->generation;
    out[2] = profile->flags;
    out[3] = profile->frames;
    out[4] = (profile->job_count & 0xffu) |
             ((profile->completed & 0xffu) << 8) |
             ((profile->recorded_count & 0xffu) << 16);
    out[5] = profile->fixed_plan_mode;
    out[6] = profile->queue_origin_tick;
    out[7] = profile->queue_source_start_tick;
    out[8] = profile->deadline_tick;
    out[9] = profile->reset_request_tick;
    first = page * BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_RECORDS; ++i) {
        uint32_t index = first + i;
        uint32_t base = BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_HEADER_WORDS +
                        i * BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_RECORD_WORDS;
        if (index < profile->recorded_count) {
            const br_audio_failure_job_entry_t *entry = &profile->jobs[index];
            out[base] = index;
            out[base + 1u] = entry->stage_flags;
            out[base + 2u] = entry->source_ticks;
            out[base + 3u] = entry->merge_ticks;
            out[base + 4u] = entry->post_ticks;
            out[base + 5u] = entry->fx_ticks;
            out[base + 6u] = entry->active_stage_lower_bound_ticks;
        }
    }
    return 1u;
}
#endif

#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
static inline uint32_t br_audio_failure_job_profile_encode_model_substage_page(
    const br_audio_failure_job_profile_t *profile, uint32_t page,
    uint32_t out[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS])
{
    uint32_t i, base;
    const br_audio_failure_job_entry_t *entry;
    if (!profile || !out || page >= BR_AUDIO_FAILURE_JOB_PROFILE_CAP ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_VALID) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_RESET_CONFIRMED) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_FAILED_JOIN)) return 0u;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS; ++i) out[i] = 0u;
    out[0] = BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_SCHEMA;
    out[1] = profile->generation;
    out[2] = profile->flags;
    out[3] = profile->frames;
    out[4] = (profile->job_count & 0xffu) |
             ((profile->completed & 0xffu) << 8) |
             ((profile->recorded_count & 0xffu) << 16);
    out[5] = profile->fixed_plan_mode;
    out[6] = profile->queue_origin_tick;
    out[7] = profile->queue_source_start_tick;
    out[8] = profile->deadline_tick;
    out[9] = profile->reset_request_tick;
    if (page >= profile->recorded_count) return 1u;
    entry = &profile->jobs[page];
    base = BR_AUDIO_FAILURE_JOB_PROFILE_MODEL_SUBSTAGE_HEADER_WORDS;
    out[base] = page;
    out[base + 1u] = entry->model_substage_flags;
    for (i = 0u; i < 4u; ++i) {
        uint32_t stage_base = base + 2u + i * 4u;
        out[stage_base] = entry->model_substage_calls[i];
        out[stage_base + 1u] = entry->model_substage_frames[i];
        out[stage_base + 2u] = entry->model_substage_ticks[i];
        out[stage_base + 3u] = entry->model_substage_max_ticks[i];
    }
    out[base + 18u] = entry->model_substage_unattributed_ticks;
    out[base + 19u] = entry->model_substage_active_id;
    out[base + 20u] = entry->model_substage_active_lower_bound_ticks;
    out[base + 21u] = entry->model_substage_timer_reads;
    return 1u;
}
#endif

#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
static inline uint32_t br_audio_failure_job_profile_encode_fx_substage_page(
    const br_audio_failure_job_profile_t *profile, uint32_t page,
    uint32_t out[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS])
{
    uint32_t i, first;
    if (!profile || !out ||
        page >= BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_COUNT ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_VALID) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_RESET_CONFIRMED) ||
        !(profile->flags & BR_AUDIO_FAILURE_PROFILE_FAILED_JOIN)) return 0u;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS; ++i) out[i] = 0u;
    out[0] = BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_SCHEMA;
    out[1] = profile->generation;
    out[2] = profile->flags;
    out[3] = profile->frames;
    out[4] = (profile->job_count & 0xffu) |
             ((profile->completed & 0xffu) << 8) |
             ((profile->recorded_count & 0xffu) << 16);
    out[5] = profile->fixed_plan_mode;
    out[6] = profile->queue_origin_tick;
    out[7] = profile->queue_source_start_tick;
    out[8] = profile->deadline_tick;
    out[9] = profile->reset_request_tick;
    first = page * BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_RECORDS;
    for (i = 0u; i < BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_RECORDS; ++i) {
        uint32_t index = first + i;
        uint32_t base = BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_HEADER_WORDS +
                        i * BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_RECORD_WORDS;
        if (index < profile->recorded_count) {
            const br_audio_failure_job_entry_t *entry = &profile->jobs[index];
            out[base] = index;
            out[base + 1u] = entry->fx_substage_flags;
            out[base + 2u] = entry->fx_substage_drive_ticks;
            out[base + 3u] = entry->fx_substage_mod_ticks;
            out[base + 4u] = entry->fx_substage_delay_ticks;
            out[base + 5u] = entry->fx_substage_reverb_ticks;
            out[base + 6u] = entry->fx_ticks;
            out[base + 7u] = entry->fx_substage_unattributed_ticks;
            out[base + 8u] = entry->fx_substage_active_id;
            out[base + 9u] = entry->fx_substage_active_lower_bound_ticks;
            out[base + 10u] = entry->fx_substage_timer_reads;
        }
    }
    return 1u;
}
#endif

static inline uint32_t br_audio_failure_job_profile_encode_requested_page(
    const br_audio_failure_job_profile_t *profile, uint32_t page,
    uint32_t out[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS])
{
    if (page < BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT)
        return br_audio_failure_job_profile_encode_page(profile, page, out);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    if (page < BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT +
               BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT)
        return br_audio_failure_job_profile_encode_stage_page(
            profile, page - BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT, out);
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG
    if (page < BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT +
               BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT +
               BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_COUNT)
        return br_audio_failure_job_profile_encode_fx_substage_page(
            profile, page - BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT -
                     BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT, out);
#endif
#if BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG
    if (page < BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_COUNT)
        return br_audio_failure_job_profile_encode_model_substage_page(
            profile, page - BR_AUDIO_FAILURE_JOB_PROFILE_BASE_PAGE_COUNT -
                     BR_AUDIO_FAILURE_JOB_PROFILE_STAGE_PAGE_COUNT -
                     BR_AUDIO_FAILURE_JOB_PROFILE_FX_SUBSTAGE_PAGE_COUNT, out);
#endif
    return 0u;
}

#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
uint32_t br_audio_failure_job_profile_read_page(
    uint32_t page, uint32_t words[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS]);
#endif

#endif
