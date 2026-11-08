/* Focused 1487 queue regression. This includes the real audio translation
 * unit and executes its queue, finalizer/FX callback, and mailbox step. The
 * interleaving and timer are deterministic host simulations, not hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#define DINGHY_HOST 1
#define BR_ENABLE_PLAITS 1
#define BR_SOURCE_QUEUE_TEST 1
#define BR_LATE_COMPLETION_TEST 1
#define BR_C1_LATE_COMPLETION_TEST 1
uint32_t fm1_cpu1_worker_submit_fx_batch(void *, uint32_t *);
#include "../firmware/src/instrument_audio_unit.c"
#if defined(BR_RELEASE_PROFILE_ASSERT)
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG || BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
#error "Release queue test must disable the failure-job profiles"
#endif
#if !BR_AUDIO_FUSION_EWMA_SCHEDULER || !BR_AUDIO_C1_LATE_OWNER_KNOWN
#error "Release queue test must retain EWMA and known worker ownership"
#endif
#endif
#if defined(BR_TEST_PROFILE_ON_ASSERT)
#if !BR_AUDIO_FAILURE_JOB_PROFILE_DIAG || !BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
#error "Recorder-on contrast test must enable both failure-job profiles"
#endif
#endif

__thread uint32_t test_core_id;
uint32_t br_audio_source_test_parallel;
volatile uint32_t br_ready;
volatile uint32_t fm1_cpu1_worker_uncontained_fault;
uint32_t fm1_cpu1_worker_available(void) { return 0u; }
uint32_t fm1_cpu1_worker_submit_fx_batch(void *context, uint32_t *sequence)
{ (void)context; (void)sequence; abort(); }
uint32_t fm1_cpu1_worker_wait(uint32_t sequence, uint32_t ticks, uint32_t *result)
{ (void)sequence; (void)ticks; (void)result; abort(); }
uint32_t fm1_cpu1_worker_abort_hold_reset(void) { abort(); }
static _Atomic uint32_t ticks;
static uint32_t expire_on_return, enter_worker, worker_joined;
static uint32_t expire_job = 7u, quiet_plaits, pause_job = 7u;
static _Atomic uint32_t order_seq, order_of[BR_AUDIO_MAX_SOURCE_JOBS];
static _Atomic uint32_t c1_pause, c1_started, c1_release;
static uint32_t expire_after_reads, expire_core;
static uint32_t worker_loops;
static uint32_t inject_engine_run;
static uint32_t fake_fixed_plan;
static uint32_t worker_sees_expired;
static uint32_t fixed_snapshot_advance;
static uint32_t expire_after_claim_before_callback;
static uint32_t corrupt_c1_after_claim;
static uint32_t boundary_during_join, boundary_during_validation, boundary_during_tail;
enum { ENGINE_ONTIME, ENGINE_LATE, ENGINE_BAD_GRAPH, ENGINE_BAD_WORKER,
       ENGINE_BAD_CALLBACK, ENGINE_BAD_RESULT, ENGINE_BAD_CONTEXT,
       ENGINE_BAD_PENDING_VALUE, ENGINE_BAD_DONE_VALUE, ENGINE_UNCONTAINED,
       ENGINE_BAD_TASK, ENGINE_BAD_SEQUENCE, ENGINE_HELD_LOCK,
       ENGINE_INCOMPLETE, ENGINE_BAD_FUSION, ENGINE_BAD_KIND,
       ENGINE_BAD_OUTPUT, ENGINE_BAD_FRAME, ENGINE_BAD_EXECUTOR };
static uint32_t engine_case;
static uint32_t finalizer_calls;
static fm1_cpu1_worker_mailbox_t mailbox;
const fm1_cpu1_worker_mailbox_t *br_audio_source_test_c1_mailbox(void) { return &mailbox; }
void br_audio_source_test_join_boundary(void)
{ if (boundary_during_join) ticks = br_audio_source_queue.deadline_tick; }
void br_audio_source_test_validation_boundary(void)
{ if (boundary_during_validation) ticks = br_audio_source_queue.deadline_tick; }
void br_audio_source_test_tail_boundary(void)
{ if (boundary_during_tail) ticks = br_audio_source_queue.deadline_tick; }
void br_audio_source_test_fixed_snapshot_boundary(void)
{
    if (fixed_snapshot_advance) {
        br_audio_source_queue.fixed_plan_completed[1] =
            br_audio_source_queue.fixed_plan_count[1];
        br_audio_source_queue.fixed_plan_cursor[1] =
            br_audio_source_queue.fixed_plan_count[1];
        fixed_snapshot_advance = 0u;
    }
}
static int32_t bus[BR_PARTS][BR_BLOCK];
static int32_t on_time_bus[BR_BLOCK], on_time_key[BR_BLOCK];
static br_mod_state_t on_time_mod[BR_PARTS];
static unsigned char on_time_engine_bus[sizeof bus], on_time_fx[sizeof br_fx_track];
static unsigned char on_time_env[sizeof br_env], on_time_seq[sizeof br_seq_clock];

uint32_t br_audio_source_test_ticks(void)
{
    uint32_t now = ticks;
    if (expire_after_reads && !--expire_after_reads) {
        ticks = br_audio_source_queue.deadline_tick;
    }
    return now;
}
static uint32_t benign_kit_render; /* the split two-core run executes kit lanes */
#include <execinfo.h>
static void unexpected(void) { void *f[16]; fputs("unexpected generator call\n", stderr); backtrace_symbols_fd(f, backtrace(f, 16), 2); abort(); }
void braids_perc_note_on_seed(void *s,uint32_t a,uint32_t b,int32_t c,uint16_t d,uint16_t e,uint16_t f)
{ (void)s;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;unexpected(); }
void braids_perc_controls(void *s,uint16_t a,uint16_t b)
{ (void)s;(void)a;(void)b;unexpected(); }
void braids_perc_render(void *s,int16_t *a,uint32_t b)
{ (void)s;(void)a;(void)b;unexpected(); }
void braids_perc_choke(void *s) { (void)s; unexpected(); }
uint8_t braids_perc_active(const void *s) { (void)s; return 0u; }
void braids_perc_init(void *s,uint32_t seed) { (void)s;(void)seed;unexpected(); }
uint32_t plaits_quiet(const void *s) { (void)s; return quiet_plaits; }
uint32_t plaits_kit_quiet_mask(const void *s,uint32_t m)
{ (void)s;(void)m; return 0u; }
void plaits_render(void *s,uint32_t m,int32_t p,uint16_t t,uint16_t a,uint16_t h,uint16_t x,int16_t *o,uint32_t n)
{ (void)s;(void)m;(void)p;(void)t;(void)a;(void)h;(void)x;(void)o;(void)n;unexpected(); }
void plaits_kit_render_keyed_mask(void *s,const br_sound_t *v,const uint32_t l[BR_DRUM_LANES],
                                 const uint32_t g[BR_DRUM_LANES],uint32_t m,int32_t *o,int32_t *k,uint32_t n)
{ (void)s;(void)v;(void)l;(void)g;(void)m;(void)k;
  if (benign_kit_render) { if (o) memset(o, 0, n * sizeof *o); return; } unexpected(); }
void plaits_state_init(void *s,uint32_t seed) { (void)s;(void)seed;unexpected(); }
void plaits_prepare_model(void *s,uint32_t model) { (void)s;(void)model;unexpected(); }
void plaits_note_on(void *s,uint32_t model,int32_t pitch,uint16_t t,uint16_t m,uint16_t h,uint32_t v)
{ (void)s;(void)model;(void)pitch;(void)t;(void)m;(void)h;(void)v;unexpected(); }
void plaits_note_off(void *s,uint32_t note) { (void)s;(void)note;unexpected(); }
void plaits_all_off(void *s) { (void)s;unexpected(); }
int32_t plaits_active_engine(const void *s) { (void)s;return 0; }
void plaits_kit_init_quad(void *states[4],uint32_t seed) { (void)states;(void)seed;unexpected(); }
void plaits_kit_note_on_mask(void *s,uint32_t n,uint32_t v,uint32_t m)
{ (void)s;(void)n;(void)v;(void)m;unexpected(); }
void plaits_kit_note_on_voice_pitch_mask(void *s,uint32_t n,uint32_t v,uint32_t m,uint32_t voice,int32_t pitch)
{ (void)s;(void)n;(void)v;(void)m;(void)voice;(void)pitch;unexpected(); }
void plaits_kit_note_off_mask(void *s,uint32_t n,uint32_t m)
{ (void)s;(void)n;(void)m;unexpected(); }
void plaits_kit_all_off_mask(void *s,uint32_t m) { (void)s;(void)m;unexpected(); }
uint32_t plaits_kit_lane_scheduler_state(const void *s,const br_sound_t *v,uint32_t lane,
                                         uint32_t *work,uint32_t *voice)
{ (void)s;(void)v;(void)lane;if(work)*work=0u;if(voice)*voice=0u;return 0u; }

/* Target mailboxes hold 32-bit addresses. Check the truncated host token and
 * then use the actual host pointer; the worker registry and callback are real. */
static uint32_t dispatch(void *unused, uint32_t id, void *token)
{
    (void)unused;
    if ((uintptr_t)token != (uint32_t)(uintptr_t)&br_audio_source_queue)
        return fm1_cpu1_worker_dispatch_audio_task(id, 0);
    return fm1_cpu1_worker_dispatch_audio_task(id, &br_audio_source_queue);
}
void br_audio_source_test_begin(uint32_t i)
{
    if (expire_after_claim_before_callback) {
        ticks = br_audio_source_queue.deadline_tick;
        expire_after_claim_before_callback = 0u;
    }
    if (test_core_id == 1u && i == 7u && corrupt_c1_after_claim)
        br_audio_source_jobs[i].output = 0;
    if (i < BR_AUDIO_MAX_SOURCE_JOBS) atomic_store(&order_of[i], atomic_fetch_add(&order_seq, 1u) + 1u);
    if (test_core_id == 1u && i == pause_job && atomic_load(&c1_pause)) {
        atomic_store(&c1_started, 1u);
        while (!atomic_load(&c1_release)) sched_yield();
    }
    if (test_core_id == 0u && i == 7u) ++finalizer_calls;
    if (test_core_id != 0u || i != 7u || !enter_worker) return;
    enter_worker = 0u;
    if (worker_sees_expired)
        ticks = br_audio_source_queue.deadline_tick;
    test_core_id = 1u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    test_core_id = 0u;
    worker_joined = mailbox.done_seq == mailbox.request_seq;
}
void br_audio_source_test_end(uint32_t i)
{
    if (inject_engine_run && test_core_id == 0u && i == 7u)
        br_audio_source_jobs[i].duration_ticks = 12345u;
    if (test_core_id == expire_core && i == expire_job && expire_on_return) {
        ticks = br_audio_source_queue.deadline_tick - (expire_on_return > 1u);
        expire_after_reads = expire_on_return > 1u ? expire_on_return - 1u : 0u;
        if (engine_case == ENGINE_BAD_TASK) mailbox.task_id ^= 1u;
        if (engine_case == ENGINE_BAD_SEQUENCE) ++mailbox.done_seq;
        if (engine_case == ENGINE_BAD_RESULT) mailbox.result = 0u;
        if (engine_case == ENGINE_BAD_CONTEXT) mailbox.task_context ^= 1u;
        if (engine_case == ENGINE_BAD_PENDING_VALUE) br_fx_fusion_pending = 2u;
        if (engine_case == ENGINE_BAD_DONE_VALUE) br_fx_fusion_done[2] = 0xffu;
        if (engine_case == ENGINE_UNCONTAINED) br_pipeline_uncontained_fault = 1u;
        if (engine_case == ENGINE_HELD_LOCK) br_audio_source_queue.lock = 1u;
        if (engine_case == ENGINE_INCOMPLETE) br_audio_source_jobs[6].state = 0u;
        if (engine_case == ENGINE_BAD_FUSION) br_fx_fusion_done[2] = 0u;
        if (engine_case == ENGINE_BAD_KIND) br_audio_source_jobs[0].kind = 99u;
        if (engine_case == ENGINE_BAD_OUTPUT) br_audio_source_jobs[0].output = 0;
        if (engine_case == ENGINE_BAD_FRAME) --br_audio_source_jobs[0].frames;
        if (engine_case == ENGINE_BAD_EXECUTOR)
            br_audio_source_jobs[7].executor_core = 2u;
    }
}
void br_audio_source_test_reset(uint32_t count)
{
    uint32_t i, lane;
    if (!inject_engine_run) return;
    assert(count == 8u);
    /* Establish the same legitimate shared state immediately before the
     * final job: seven prior jobs have completed, and the kit merge is ready. */
    br_audio_fixed_plan_disable();
    for (i = 0u; i < 7u; ++i) {
        br_audio_source_jobs[i].executor_core = 1u;
        br_audio_source_jobs[i].state = 2u;
    }
    br_audio_source_queue.completed = 7u;
    if (engine_case == ENGINE_BAD_GRAPH) {
        br_audio_source_jobs[7].predecessor4 = 8u;
        assert(!br_audio_worker_source_queue_context_valid(&br_audio_source_queue));
    }
    if (engine_case == ENGINE_BAD_CALLBACK) br_audio_source_jobs[7].output = 0;
    for (i = 0u; i < 3u; ++i) br_fx_fusion_done[i] = 1u;
    for (i = 0u; i < BR_PARTS; ++i) br_audio_fused_osc_block[i] = (int32_t)i;
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
        for (i = 0u; i < BR_BLOCK; ++i)
            br_audio_kit_partial[0][lane][i] = (int32_t)(lane + 1u);
    if (fake_fixed_plan) {
        br_audio_source_queue.fixed_plan_active = 1u;
        br_audio_source_queue.fixed_plan_count[0] = 1u;
        br_audio_source_queue.fixed_plan_count[1] = 7u;
        br_audio_source_queue.fixed_plan_completed[1] = 7u;
        br_audio_source_queue.fixed_plan_cursor[1] = 7u;
        br_audio_source_queue.completed = 0u;
        for (i = 0u; i < 8u; ++i) {
            br_audio_source_jobs[i].fixed_plan_core = i == 7u ? 0u : 1u;
            br_audio_source_jobs[i].fixed_plan_order = i == 7u ? 0u : i;
        }
    }
}
uint32_t br_audio_source_test_start(void *queue)
{
    assert(queue == &br_audio_source_queue);
    memset(&mailbox, 0, sizeof mailbox);
    mailbox.cpu_id = 1u; mailbox.state = FM1_CPU1_WORKER_READY;
    mailbox.request_seq = 12u; mailbox.done_seq = 11u;
    mailbox.task_id = FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH;
    mailbox.task_context = (uint32_t)(uintptr_t)queue;
    worker_loops = worker_joined = 0u; enter_worker = 1u;
    if (engine_case == ENGINE_BAD_GRAPH || engine_case == ENGINE_BAD_WORKER) {
        if (engine_case == ENGINE_BAD_WORKER) mailbox.task_context ^= 1u;
        test_core_id = 1u;
        fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
        test_core_id = 0u;
        worker_joined = mailbox.done_seq == mailbox.request_seq;
        enter_worker = 0u;
    }
    return 1u;
}
uint32_t br_audio_source_test_join_ticks(uint32_t timeout)
{ (void)timeout; return worker_joined &&
    (mailbox.result == BR_AUDIO_QUEUE_OK ||
     mailbox.result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
     mailbox.result == BR_AUDIO_QUEUE_CUTOFF_DRAINED); }
uint32_t br_audio_source_test_join(void)
{ return br_audio_source_test_join_ticks(1u); }
const fm1_cpu1_worker_mailbox_t *br_audio_source_test_late_mailbox(void)
{ return worker_joined ? &mailbox : 0; }
uint32_t br_audio_source_test_late_sequence(void) { return 12u; }
uint32_t br_audio_source_test_done(uint32_t count)
{
    uint32_t i;
    if (count != br_audio_source_queue.count ||
        br_audio_source_queue.completed != count) return 0u;
    for (i = 0u; i < count; ++i)
        if (br_audio_source_jobs[i].state != 2u) return 0u;
    return 1u;
}

static void setup(void)
{
    uint32_t i, lane;
    /* Direct queue cases below exercise the inherited, non-opted-in path. */
    br_audio_source_publication_enabled = 0u;
    memset(&br_audio_source_queue, 0, sizeof br_audio_source_queue);
    memset(br_audio_source_jobs, 0, sizeof br_audio_source_jobs);
    memset(&mailbox, 0, sizeof mailbox);
    memset(&br_audio_c1_late_record, 0, sizeof br_audio_c1_late_record);
    memset(bus, 0, sizeof bus);
    memset(br_audio_kit_partial, 0, sizeof br_audio_kit_partial);
    memset(br_audio_kit_key_partial, 0, sizeof br_audio_kit_key_partial);
    memset(br_comp_key, 0, sizeof br_comp_key);
    memset(br_env, 0, sizeof br_env);
    memset(br_mode, 0, sizeof br_mode);
    memset(br_effective_sound, 0, sizeof br_effective_sound);
    memset(br_cached_fx, 0, sizeof br_cached_fx);
    memset(br_effective_fx, 0, sizeof br_effective_fx);
    br_mode[3] = 4u;
    br_effective_sound[3].preset = BR_KIT_PLAITS;
    br_effective_sound[3].level = 127u;
    br_effective_sound[3].filter = 127u;
    br_env[3].stage = BR_ENV_SUSTAIN;
    br_env[3].amp = 32767u;
    br_source_compressor.source = UINT32_MAX;
    for (i = 0u; i < BR_PARTS; ++i) br_filter_init(&br_filter[i]);
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
        for (i = 0u; i < BR_BLOCK; ++i)
            br_audio_kit_partial[0][lane][i] = (int32_t)(lane + 1u);
    br_fx_init();
    assert(br_fx_fusion_prepare(bus, BR_BLOCK));
    br_audio_source_fx_fusion_active = 1u;
    br_audio_source_queue.count = 8u;
    br_audio_source_queue.completed = 7u;
    br_audio_source_queue.deadline_tick = 1000u;
    for (i = 0u; i < 8u; ++i) {
        br_audio_source_job_t *j = &br_audio_source_jobs[i];
        j->part = i < 4u ? 3u : i < 7u ? i - 4u : 3u;
        j->frames = BR_BLOCK;
        j->output = i < 4u ? br_audio_kit_partial[0][i] : bus[j->part];
        j->kind = i < 4u ? BR_AUDIO_JOB_SOURCE :
                  i < 7u ? BR_AUDIO_JOB_FUSED_TRACK : BR_AUDIO_JOB_KIT_FINALIZE;
        j->lane_mask = i < 4u ? 1u << i : BR_DRUM_ALL_MASK;
        j->predecessor = j->predecessor2 = j->predecessor3 = j->predecessor4 = UINT32_MAX;
        j->state = i < 7u ? 2u : 0u;
    }
    br_audio_source_jobs[7].predecessor = 0u;
    br_audio_source_jobs[7].predecessor2 = 1u;
    br_audio_source_jobs[7].predecessor3 = 2u;
    br_audio_source_jobs[7].predecessor4 = 3u;
    mailbox.cpu_id = 1u;
    mailbox.state = FM1_CPU1_WORKER_READY;
    mailbox.request_seq = 12u;
    mailbox.done_seq = 11u;
    mailbox.task_id = FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH;
    mailbox.task_context = (uint32_t)(uintptr_t)&br_audio_source_queue;
    worker_loops = worker_joined = 0u;
    enter_worker = 1u;
    ticks = 100u;
    test_core_id = 0u;
    expire_core = expire_after_reads = 0u;
    expire_job = 7u; quiet_plaits = 0u; pause_job = 7u;
    br_audio_split_test_force = 0u;
    memset(br_audio_split_state, 0, sizeof br_audio_split_state);
    fixed_snapshot_advance = 0u;
    expire_after_claim_before_callback = 0u;
    corrupt_c1_after_claim = 0u;
}

static void *run_c1_worker(void *unused)
{
    (void)unused;
    test_core_id = 1u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    return 0;
}

static void test_real_c1_finalizer_late(void)
{
    pthread_t worker;
    br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
    uint32_t i;
    setup();
    inject_engine_run = 0u;
    engine_case = ENGINE_LATE;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (i = 0u; i < 7u; ++i) br_audio_source_jobs[i].state = 2u;
    br_audio_source_queue.completed = 7u;
    br_audio_source_jobs[7].executor_core = UINT32_MAX;
    for (i = 0u; i < 3u; ++i) br_fx_fusion_done[i] = 1u;
    atomic_store(&c1_pause, 1u); atomic_store(&c1_started, 0u);
    atomic_store(&c1_release, 0u);
    enter_worker = 0u; expire_core = 1u; expire_on_return = 1u;
    assert(!pthread_create(&worker, 0, run_c1_worker, 0));
    while (!atomic_load(&c1_started)) sched_yield();
    /* C1 has claimed the real finalizer; C0 reaches the soft cutoff. */
    atomic_store(&ticks, br_audio_source_queue.deadline_tick);
    test_core_id = 0u;
    assert(br_audio_source_queue_run_internal(0u, &late) == BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS, &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_CLAIMED);
    br_audio_source_queue.lock = 1u;
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS,
        &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_TERMINAL);
    br_audio_source_queue.lock = 0u;
    br_audio_source_jobs[6].state = 0u;
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS,
        &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_INVALID);
    br_audio_source_jobs[6].state = 2u;
    br_audio_source_jobs[7].predecessor4 = 8u;
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS,
        &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_INVALID);
    br_audio_source_jobs[7].predecessor4 = 6u;
    atomic_store(&c1_release, 1u);
    assert(!pthread_join(worker, 0));
    atomic_store(&c1_pause, 0u);
    assert(mailbox.done_seq == 12u && mailbox.result == BR_AUDIO_QUEUE_COMPLETE_LATE);
    assert(br_audio_source_jobs[7].executor_core == 1u);
    assert(br_audio_source_late_join_valid(&mailbox, 12u, FM1_CPU1_WORKER_OK,
        mailbox.result, BR_AUDIO_QUEUE_COMPLETE_LATE, 0u));
    assert(!br_audio_source_late_join_valid(&mailbox, 13u, FM1_CPU1_WORKER_OK,
        mailbox.result, BR_AUDIO_QUEUE_COMPLETE_LATE, 0u));
    assert(!br_audio_source_late_join_valid(&mailbox, 12u, FM1_CPU1_WORKER_TIMEOUT,
        mailbox.result, BR_AUDIO_QUEUE_COMPLETE_LATE, 0u));
    assert(!br_audio_source_late_join_valid(&mailbox, 12u, FM1_CPU1_WORKER_OK,
        mailbox.result, BR_AUDIO_QUEUE_COMPLETE_LATE, 1u));
    br_audio_c1_late_record.sequence = 13u;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    br_audio_c1_late_record.sequence = 12u;
    br_audio_c1_late_record.valid ^= 1u;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    br_audio_c1_late_record.valid = BR_AUDIO_C1_LATE_VALID;
    ++br_audio_c1_late_record.deadline;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    --br_audio_c1_late_record.deadline;
    br_audio_c1_late_record.returned_tick =
        br_audio_source_queue.deadline_tick - FM1_AUDIO_HALF_TICKS - 1u;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    br_audio_c1_late_record.returned_tick = br_audio_source_queue.deadline_tick;
    br_audio_c1_late_record.cutoff_tick =
        br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    assert(br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick,
        br_audio_c1_late_record.cutoff_tick, &late));
    br_audio_c1_late_record.cutoff_tick = br_audio_source_queue.deadline_tick;
    br_audio_c1_late_record.index = 6u;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    br_audio_c1_late_record.index = 7u;
    br_audio_c1_late_record.returned_tick = br_audio_source_queue.deadline_tick + 1u;
    assert(!br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    br_audio_c1_late_record.returned_tick = br_audio_source_queue.deadline_tick;
    assert(br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick, ticks, &late));
    if (!br_audio_source_accept_complete_late(bus, BR_BLOCK, 8u,
                                               br_audio_source_queue.deadline_tick, &late)) {
        fprintf(stderr, "late reject: shape=%u fx=%u pending=%u done=%u,%u,%u,%u lock=%u ret=%u cut=%u deadline=%u\n",
            br_audio_source_late_shape(bus, BR_BLOCK, 8u, br_audio_source_queue.deadline_tick, 0u),
            br_fx_fusion_complete(bus, BR_BLOCK), br_fx_fusion_pending,
            br_fx_fusion_done[0],br_fx_fusion_done[1],br_fx_fusion_done[2],br_fx_fusion_done[3],
            br_audio_source_queue.lock, late.returned_tick, late.cutoff_tick,
            br_audio_source_queue.deadline_tick);
        abort();
    }
    assert(br_audio_source_queue.completed == 8u && br_audio_source_jobs[7].state == 2u);
    assert(br_fx_fusion_done[3] == 1u && late.index == 7u);
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS, &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_COMPLETE);
    br_audio_source_fusion_cancel();
    assert(!br_fx_fusion_pending);
}

static void test_c1_publication_cutoff(void)
{
    br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
    setup(); engine_case = ENGINE_LATE; inject_engine_run = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (uint32_t i = 0u; i < 7u; ++i) br_audio_source_jobs[i].state = 2u;
    br_audio_source_queue.completed = 7u;
    for (uint32_t i = 0u; i < 3u; ++i) br_fx_fusion_done[i] = 1u;
    test_core_id = expire_core = 1u; enter_worker = 0u; expire_on_return = 2u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    assert(mailbox.done_seq == 12u && mailbox.result == BR_AUDIO_QUEUE_COMPLETE_LATE);
    assert(br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick,
                                        ticks, &late));
    assert(late.returned_tick + 1u == br_audio_source_queue.deadline_tick);
    assert(late.cutoff_tick == br_audio_source_queue.deadline_tick);
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS, &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_CLAIMED);
    assert(br_audio_source_accept_complete_late(bus, BR_BLOCK, 8u,
                                                br_audio_source_queue.deadline_tick, &late));
    assert(br_audio_source_queue.completed == 8u);
}

static void test_c1_postpublication_cutoff_success(void)
{
    setup(); engine_case = ENGINE_LATE; inject_engine_run = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (uint32_t i = 0u; i < 7u; ++i) br_audio_source_jobs[i].state = 2u;
    br_audio_source_queue.completed = 7u;
    for (uint32_t i = 0u; i < 3u; ++i) br_fx_fusion_done[i] = 1u;
    test_core_id = expire_core = 1u;
    expire_on_return = 4u; enter_worker = 0u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    /* The real C1 callback and queue publication succeeded. */
    assert(mailbox.done_seq == 12u &&
           mailbox.result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    assert(br_audio_source_queue.completed == 8u);
    assert(br_audio_source_jobs[7].state == 2u);
    assert(br_audio_source_jobs[7].executor_core == 1u);
    assert(br_fx_fusion_done[3] == 1u && !br_audio_c1_late_record.valid);
    assert(br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, 0u));
    br_audio_source_jobs[7].predecessor4 = 8u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, BR_AUDIO_QUEUE_PUBLISHED_COMPLETE, 0u));
}

static void test_other_core_published_completion(void)
{
    br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
    setup(); engine_case = ENGINE_ONTIME; enter_worker = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (uint32_t i = 0u; i < 8u; ++i) br_audio_source_jobs[i].state = 2u;
    for (uint32_t i = 0u; i < 4u; ++i) br_fx_fusion_done[i] = 1u;
    br_audio_source_jobs[7].executor_core = 1u;
    br_audio_source_queue.completed = 8u;
    atomic_store(&ticks, br_audio_source_queue.deadline_tick);
    /* Another core's count can be visible just before it releases the lock.
     * The local queue can finish, but no bus is accepted until the matching
     * worker has joined and the lock and full shape validate. */
    br_audio_source_queue.lock = 1u;
    assert(br_audio_source_queue_run_internal(0u, &late) == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    test_core_id = 0u;
    assert(br_audio_source_queue_run_internal(0u, &late) == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    test_core_id = 1u;
    assert(br_audio_source_queue_run_internal(0u, &late) == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    mailbox.done_seq = 12u; mailbox.result = 1u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    br_audio_source_queue.lock = 0u;
    assert(br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    mailbox.done_seq = 11u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    mailbox.done_seq = 12u; mailbox.result = 0u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 0u, 0u));
    mailbox.result = 1u;
    br_audio_source_jobs[6].state = 1u;
    assert(!br_audio_source_queue_run_internal(0u, &late));
    br_audio_source_jobs[6].state = 2u;
    br_audio_source_queue.completed = 9u;
    assert(!br_audio_source_queue_run_internal(0u, &late));
    br_audio_source_queue.completed = 8u;
    br_audio_source_jobs[7].predecessor4 = 8u;
    assert(!br_audio_source_queue_run_internal(0u, &late));
    br_audio_source_jobs[7].predecessor4 = 6u;
    br_audio_source_jobs[0].kind = 99u;
    assert(!br_audio_source_queue_run_internal(0u, &late));
    br_audio_source_jobs[0].kind = BR_AUDIO_JOB_FUSED_TRACK;
    /* A structurally plausible wrong bus is caught by the post-join watch. */
    br_audio_source_jobs[0].output = bus[1];
    assert(br_audio_source_queue_run_internal(0u, &late) == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
}

static void test_c0_nonfinal_then_c1_late(void)
{
    pthread_t worker;
    br_audio_source_late_t cpu0_late = {UINT32_MAX, 0u, 0u};
    br_audio_source_late_t c1_late = {UINT32_MAX, 0u, 0u};
    setup(); engine_case = ENGINE_LATE; inject_engine_run = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (uint32_t i = 0u; i < 8u; ++i) br_audio_source_jobs[i].state = 2u;
    br_audio_source_jobs[0].state = 0u;
    br_audio_source_jobs[7].state = 0u;
    br_audio_source_queue.completed = 6u;
    br_fx_fusion_done[1] = br_fx_fusion_done[2] = 1u;
    atomic_store(&c1_pause, 1u); atomic_store(&c1_started, 0u);
    atomic_store(&c1_release, 0u);
    enter_worker = 0u;
    assert(!pthread_create(&worker, 0, run_c1_worker, 0));
    while (!atomic_load(&c1_started)) sched_yield();
    assert(br_audio_source_jobs[7].state == 1u);
    test_core_id = expire_core = 0u;
    expire_job = 0u; expire_on_return = 4u; quiet_plaits = 1u;
    assert(br_audio_source_queue_run_internal(0u, &cpu0_late) ==
           BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_jobs[0].state == 2u &&
           br_audio_source_queue.completed == 7u);
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick,
        br_audio_source_queue.deadline_tick + FM1_AUDIO_HALF_TICKS,
        &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_CLAIMED);
    atomic_store(&c1_release, 1u);
    assert(!pthread_join(worker, 0));
    atomic_store(&c1_pause, 0u);
    assert(mailbox.done_seq == 12u && mailbox.result == BR_AUDIO_QUEUE_COMPLETE_LATE);
    assert(br_audio_source_late_join_valid(&mailbox, 12u,
        FM1_CPU1_WORKER_OK, mailbox.result, BR_AUDIO_QUEUE_COMPLETE_LATE, 0u));
    assert(br_audio_source_jobs[7].executor_core == 1u);
    assert(br_audio_source_c1_late_read(12u, br_audio_source_queue.deadline_tick,
                                       ticks, &c1_late));
    assert(br_audio_source_accept_complete_late(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &c1_late));
    assert(br_audio_source_queue.completed == 8u && br_fx_fusion_done[3] == 1u);
    quiet_plaits = 0u;
}

static void test_partial_corrupt_descriptor_rejected(void)
{
    for (uint32_t core = 0u; core < 2u; ++core) {
        br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
        uint32_t result;
        setup(); inject_engine_run = 0u; engine_case = ENGINE_ONTIME;
        br_fx_fusion_cancel();
        assert(br_fx_fusion_prepare(bus, BR_BLOCK / 2u));
        assert(br_audio_build_fusion_jobs(bus, BR_BLOCK / 2u) == 8u);
        for (uint32_t i = 0u; i < 7u; ++i) br_audio_source_jobs[i].state = 2u;
        for (uint32_t i = 0u; i < 3u; ++i) br_fx_fusion_done[i] = 1u;
        br_audio_source_queue.completed = 7u;
        br_audio_source_jobs[7].frames = BR_BLOCK; /* malformed finalizer */
        if (core == 1u)
            for (uint32_t i = 0u; i < 7u; ++i)
                br_audio_source_jobs[i].frames = BR_BLOCK;
        enter_worker = expire_on_return = 0u; test_core_id = core;
        if (core == 0u) {
            result = br_audio_source_queue_run_internal(0u, &late);
            mailbox.done_seq = 12u; mailbox.result = 1u;
        } else {
            fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
            result = mailbox.result;
        }
        assert(result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
        assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK / 2u, 8u,
            br_audio_source_queue.deadline_tick, &mailbox, 12u,
            FM1_CPU1_WORKER_OK, mailbox.result, 0u));
        assert(br_fx_fusion_done[3] == 0u);
    }
}

static void test_failed_c1_callback_remains_failure(void)
{
    setup(); inject_engine_run = 0u; engine_case = ENGINE_BAD_CALLBACK;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (uint32_t i = 0u; i < 7u; ++i) br_audio_source_jobs[i].state = 2u;
    br_audio_source_queue.completed = 7u;
    corrupt_c1_after_claim = 1u;
    enter_worker = expire_on_return = 0u; test_core_id = 1u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    assert(mailbox.done_seq == 12u && mailbox.result == BR_AUDIO_QUEUE_FAILED);
    assert(br_audio_source_queue.completed == 7u &&
           br_audio_source_jobs[7].state == 1u && !br_audio_c1_late_record.valid);
    corrupt_c1_after_claim = 0u;
}

static void test_fixed_plan_complete_watch(void)
{
    setup(); engine_case = ENGINE_ONTIME; enter_worker = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_queue.fixed_plan_count[0] = 1u;
    br_audio_source_queue.fixed_plan_count[1] = 7u;
    br_audio_source_queue.fixed_plan_completed[0] = 1u;
    br_audio_source_queue.fixed_plan_completed[1] = 7u;
    br_audio_source_queue.fixed_plan_cursor[0] = 1u;
    br_audio_source_queue.fixed_plan_cursor[1] = 7u;
    br_audio_source_queue.completed = 0u;
    for (uint32_t i = 0u; i < 8u; ++i) {
        br_audio_source_jobs[i].state = 2u;
        br_audio_source_jobs[i].fixed_plan_core = i == 7u ? 0u : 1u;
        br_audio_source_jobs[i].fixed_plan_order = i == 7u ? 0u : i;
    }
    for (uint32_t i = 0u; i < 4u; ++i) br_fx_fusion_done[i] = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    assert(br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    --br_audio_source_queue.fixed_plan_cursor[1];
    /* C1 has stored its completed count but not its cursor. */
    assert(br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    --br_audio_source_queue.fixed_plan_completed[1];
    /* C1 has published state 2 but neither aggregate field yet. */
    assert(br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    fixed_snapshot_advance = 1u;
    assert(br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    assert(!fixed_snapshot_advance &&
           br_audio_source_queue.fixed_plan_completed[1] == 7u &&
           br_audio_source_queue.fixed_plan_cursor[1] == 7u);
    --br_audio_source_queue.fixed_plan_completed[1];
    --br_audio_source_queue.fixed_plan_cursor[1];
    --br_audio_source_queue.fixed_plan_cursor[1];
    assert(!br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    ++br_audio_source_queue.fixed_plan_cursor[1];
    ++br_audio_source_queue.fixed_plan_completed[1];
    ++br_audio_source_queue.fixed_plan_cursor[1];
    ++br_audio_source_queue.fixed_plan_completed[1];
    assert(!br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    --br_audio_source_queue.fixed_plan_completed[1];
    --br_audio_source_queue.fixed_plan_completed[0];
    assert(!br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    ++br_audio_source_queue.fixed_plan_completed[0];
    br_audio_source_jobs[6].fixed_plan_order = 0u;
    assert(!br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    br_audio_source_jobs[6].fixed_plan_order = 6u;
    br_audio_source_queue.fixed_plan_active = 2u;
    assert(!br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_jobs[6].state = 1u;
    assert(!br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    br_audio_source_jobs[6].state = 2u;
    /* Generic prejoin graph validation may permit a plausible wrong bus;
     * only the post-join exact graph watch can accept the output. */
    br_audio_source_jobs[0].output = bus[1];
    assert(br_audio_source_fixed_published_ready(BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u));
    assert(br_audio_fixed_plan_seal_completed());
    assert(br_audio_source_queue.completed == 8u);
    mailbox.done_seq = 12u; mailbox.result = 1u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    br_audio_source_jobs[0].output = bus[0];
    assert(br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    assert(!br_audio_source_late_shape(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, 0u)); /* no fixed 7/8 repair */
    br_audio_source_queue.fixed_plan_active = 2u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    br_audio_source_queue.fixed_plan_active = 1u;
    --br_audio_source_queue.fixed_plan_cursor[1];
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    ++br_audio_source_queue.fixed_plan_cursor[1];
    --br_audio_source_queue.fixed_plan_completed[1];
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    ++br_audio_source_queue.fixed_plan_completed[1];
    br_audio_source_jobs[6].fixed_plan_order = 0u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 1u, 0u));
    br_audio_source_jobs[6].fixed_plan_order = 6u;
    mailbox.result = 0u;
    assert(!br_audio_source_complete_watch_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, 0u, 0u));
}

static void test_c0_late_ack_preflight(void)
{
    br_audio_source_late_t late = {7u, 999u, 1000u};
    setup(); engine_case = ENGINE_ONTIME; enter_worker = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    for (uint32_t i = 0u; i < 7u; ++i) br_audio_source_jobs[i].state = 2u;
    for (uint32_t i = 0u; i < 4u; ++i) br_fx_fusion_done[i] = 1u;
    br_audio_source_jobs[7].state = 1u;
    br_audio_source_jobs[7].executor_core = 0u;
    br_audio_source_queue.completed = 7u;
    br_audio_source_queue.lock = 1u; /* C1 may still be leaving the queue */
    ticks = 1000u;
    assert(br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                             &mailbox, 12u, &late));
    late.index = 6u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    late.index = 7u; late.cutoff_tick = 1001u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    late.cutoff_tick = 1000u; late.returned_tick = 1001u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    late.returned_tick = 1000u - FM1_AUDIO_HALF_TICKS - 1u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    late.returned_tick = 999u;
    br_audio_source_jobs[7].executor_core = 1u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    br_audio_source_jobs[7].executor_core = 0u;
    br_fx_fusion_done[3] = 0u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    br_fx_fusion_done[3] = 1u;
    mailbox.request_seq = 11u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    mailbox.request_seq = 12u;
    mailbox.task_id = FM1_CPU1_WORKER_TASK_SMOKE;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    mailbox.task_id = FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH;
    br_audio_source_jobs[7].predecessor4 = 8u;
    assert(!br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u, 1000u,
                                              &mailbox, 12u, &late));
    br_audio_source_jobs[7].predecessor4 = 6u;
    br_audio_source_queue.deadline_tick = UINT32_MAX - 200u;
    late.returned_tick = br_audio_source_queue.deadline_tick - 1u;
    late.cutoff_tick = br_audio_source_queue.deadline_tick;
    ticks = late.cutoff_tick;
    assert(br_audio_source_c0_late_ack_ready(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u, &late));
}

static void engine_setup(void)
{
    uint32_t p;
    memset(br_part, 0, sizeof br_part);
    memset(br_env, 0, sizeof br_env);
    memset(br_mode, 0, sizeof br_mode);
    memset(br_cached_sound, 0, sizeof br_cached_sound);
    memset(br_effective_sound, 0, sizeof br_effective_sound);
    memset(br_cached_mod, 0, sizeof br_cached_mod);
    memset(br_cached_fx, 0, sizeof br_cached_fx);
    memset(br_effective_fx, 0, sizeof br_effective_fx);
    memset(br_effective_valid, 0, sizeof br_effective_valid);
    memset(br_mod_state, 0, sizeof br_mod_state);
    memset((void *)br_seq_clock, 0, sizeof br_seq_clock);
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
    memset(br_audio_fusion_job_history, 0, sizeof br_audio_fusion_job_history);
#endif
    memset(bus, 0, sizeof bus);
    for (p = 0u; p < BR_PARTS; ++p) {
        uint32_t preset = p == 3u ? BR_KIT_PLAITS : BR_FIRST_PLAITS;
        br_part[p].revision = 2u;
        br_part[p].sound.preset = preset;
        br_part[p].sound.level = 127u;
        br_part[p].sound.filter = 127u;
        br_cached_revision[p] = UINT32_MAX;
        br_active_preset[p] = (uint8_t)preset;
        br_mode[p] = p == 3u ? 4u : 3u;
        br_seq_note[p] = 255u;
        br_filter_init(&br_filter[p]);
        br_mod_init(&br_mod_state[p], 0x4d4f4400u + p);
    }
    br_ready = 1u;
    br_playing = br_transport_prev = 0u;
    br_event_read = br_event_write = br_panic_parts = 0u;
    br_audio_source_worker_fault = 0u;
    br_pipeline_uncontained_fault = 0u; /* independent host test case only */
    br_audio_source_fx_fusion = 1u;
    br_audio_source_test_parallel = 1u;
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_FREE;
#endif
    br_source_compressor.source = UINT32_MAX;
    br_fx_init();
    ticks = 100u;
    test_core_id = 0u;
    expire_core = expire_after_reads = 0u;
    inject_engine_run = 1u;
    finalizer_calls = 0u;
    br_audio_late_drop_reset();
    boundary_during_join = boundary_during_validation = boundary_during_tail = 0u;
}

/* Split graph: three fused synth tracks become source jobs at their own index
 * and effects jobs appended after the kit finalizer. */
static uint32_t split_reverb;
static void split_build(uint32_t force, uint32_t expect)
{
    setup();
    if (split_reverb) br_gr_run = br_gr_key_valid = 1u;
    br_audio_split_test_force = force;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == expect);
    br_audio_source_queue.count = expect;
    br_audio_source_queue.completed = 0u;
    for (uint32_t i = 0u; i < expect; ++i) {
        br_audio_source_jobs[i].state = 0u;
        br_audio_source_jobs[i].executor_core = UINT32_MAX;
        br_audio_source_jobs[i].duration_ticks = 0u;
    }
}

static void test_split_layout(void)
{
    uint32_t dl;
    split_build(1u << 2, 9u);
    dl = br_audio_source_queue.deadline_tick;
    assert(br_audio_source_jobs[2].kind == BR_AUDIO_JOB_TRACK_SOURCE);
    assert(br_audio_source_jobs[0].kind == BR_AUDIO_JOB_FUSED_TRACK);
    assert(br_audio_source_jobs[7].kind == BR_AUDIO_JOB_KIT_FINALIZE);
    assert(br_audio_source_jobs[8].kind == BR_AUDIO_JOB_TRACK_FX &&
           br_audio_source_jobs[8].part == 2u && br_audio_source_jobs[8].predecessor == 2u &&
           br_audio_source_jobs[8].output == bus[2]);
    assert(br_audio_worker_source_queue_context_valid(&br_audio_source_queue));
    assert(br_audio_source_graph_layout(bus, BR_BLOCK, 9u, dl));
    assert(br_audio_late_index_ok(8u, 9u) && br_audio_late_index_ok(7u, 9u));
    assert(!br_audio_late_index_ok(2u, 9u) && !br_audio_late_index_ok(9u, 9u));
    assert(br_audio_late_index_ok(7u, 8u) && !br_audio_late_index_ok(8u, 8u));
    br_audio_source_jobs[8].predecessor = 1u;          /* effects of the wrong source */
    assert(!br_audio_source_graph_layout(bus, BR_BLOCK, 9u, dl));
    br_audio_source_jobs[8].predecessor = 2u;
    br_audio_source_jobs[8].part = 1u;                 /* a fused track cannot gain a tail */
    assert(!br_audio_source_graph_layout(bus, BR_BLOCK, 9u, dl));
    br_audio_source_jobs[8].part = 2u;
    br_audio_source_queue.count = 8u;                  /* a split source without its tail */
    assert(!br_audio_source_graph_layout(bus, BR_BLOCK, 8u, dl));

    split_build(7u, 11u);
    for (uint32_t p = 0u; p < 3u; ++p) {
        assert(br_audio_source_jobs[p].kind == BR_AUDIO_JOB_TRACK_SOURCE);
        assert(br_audio_source_jobs[8u + p].part == p && br_audio_source_jobs[8u + p].predecessor == p);
    }
    assert(br_audio_source_graph_layout(bus, BR_BLOCK, 11u, br_audio_source_queue.deadline_tick));
    { br_audio_source_job_t t = br_audio_source_jobs[8];   /* tails must be in track order */
      br_audio_source_jobs[8] = br_audio_source_jobs[9]; br_audio_source_jobs[9] = t;
      assert(!br_audio_source_graph_layout(bus, BR_BLOCK, 11u, br_audio_source_queue.deadline_tick)); }

    /* With the shared reverb running, a split graph gives it its own last job. */
    split_reverb = 1u;
    split_build(1u << 0, 10u);
    assert(br_audio_source_jobs[8].kind == BR_AUDIO_JOB_TRACK_FX && br_audio_source_jobs[8].part == 0u);
    assert(br_audio_source_jobs[9].kind == BR_AUDIO_JOB_SHARED_REVERB &&
           br_audio_source_jobs[9].predecessor == UINT32_MAX);
    assert(br_gr_separate_seq == br_fx_block_seq);   /* track 3 will not run it too */
    assert(br_audio_source_graph_layout(bus, BR_BLOCK, 10u, br_audio_source_queue.deadline_tick));
    assert(br_audio_late_index_ok(9u, 10u) && br_audio_late_index_ok(8u, 10u));
    br_audio_source_jobs[9].predecessor = 0u;        /* the reverb depends on nothing */
    assert(!br_audio_source_graph_layout(bus, BR_BLOCK, 10u, br_audio_source_queue.deadline_tick));
    /* The fused graph never gains a reverb job. */
    split_build(0u, 8u);
    assert(br_audio_source_jobs[7].kind == BR_AUDIO_JOB_KIT_FINALIZE);
    split_reverb = 0u;
    br_gr_run = br_gr_key_valid = 0u;
    br_gr_separate_seq = 0xffffffffu;

    /* Not the standard graph (the drum track is not a kit): nothing splits. */
    setup(); br_mode[3] = 0u; br_audio_split_test_force = 7u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 4u);
    for (uint32_t i = 0u; i < 4u; ++i) assert(br_audio_source_jobs[i].kind == BR_AUDIO_JOB_FUSED_TRACK);
    br_mode[3] = 4u;
}

static void *run_c1_queue(void *unused)
{
    (void)unused;
    test_core_id = 1u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    return 0;
}

static void test_split_runs_on_two_cores(void)
{
    pthread_t worker;
    br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
    uint32_t r;
    split_build(7u, 11u);
    /* The kit lanes are already rendered (their real renderer needs a live kit),
     * the synth voices are quiet; every other job runs for real on both cores. */
    for (uint32_t i = 3u; i < 7u; ++i) { br_audio_source_jobs[i].state = 2u; br_audio_source_jobs[i].executor_core = 0u; }
    br_audio_source_queue.completed = 4u;
    quiet_plaits = 1u;
    enter_worker = 0u; expire_on_return = 0u;
    atomic_store(&order_seq, 0u);
    for (uint32_t i = 0u; i < BR_AUDIO_MAX_SOURCE_JOBS; ++i) atomic_store(&order_of[i], 0u);
    br_audio_source_test_parallel = 1u; benign_kit_render = 1u;
    assert(!pthread_create(&worker, 0, run_c1_queue, 0));
    test_core_id = 0u;
    r = br_audio_source_queue_run_internal(1u, &late);
    assert(!pthread_join(worker, 0));
    br_audio_source_test_parallel = 0u; benign_kit_render = 0u;
    assert(r == BR_AUDIO_QUEUE_OK || r == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    assert(mailbox.done_seq == 12u);
    assert(br_audio_source_queue.completed == 11u);
    for (uint32_t i = 0u; i < 11u; ++i) assert(br_audio_source_jobs[i].state == 2u);
    for (uint32_t p = 0u; p < 3u; ++p)          /* effects only after their source */
        assert(atomic_load(&order_of[8u + p]) > atomic_load(&order_of[p]));
    assert(br_fx_fusion_complete(bus, BR_BLOCK));
    br_audio_source_fusion_cancel();
}

static void test_split_fx_late_on_c1(void)
{
    pthread_t worker;
    br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
    uint32_t i, dl;
    split_build(1u << 2, 9u);
    dl = br_audio_source_queue.deadline_tick;
    for (i = 0u; i < 8u; ++i) { br_audio_source_jobs[i].state = 2u; br_audio_source_jobs[i].executor_core = 0u; }
    br_audio_source_queue.completed = 8u;
    for (i = 0u; i < BR_PARTS; ++i) br_fx_fusion_done[i] = i != 2u;
    /* A source job is never an acceptable late job. */
    assert(br_audio_source_record_late(&late, 2u, 9u, dl, dl) == BR_AUDIO_QUEUE_FAILED);
    atomic_store(&c1_pause, 1u); atomic_store(&c1_started, 0u); atomic_store(&c1_release, 0u);
    pause_job = 8u; enter_worker = 0u; expire_core = 1u; expire_job = 8u; expire_on_return = 1u;
    assert(!pthread_create(&worker, 0, run_c1_worker, 0));
    while (!atomic_load(&c1_started)) sched_yield();
    /* C1 has claimed the effects job; C0 reaches the soft cutoff. */
    atomic_store(&ticks, dl);
    test_core_id = 0u;
    assert(br_audio_source_queue_run_internal(0u, &late) == BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 9u, dl, dl + FM1_AUDIO_HALF_TICKS,
        &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_CLAIMED);
    br_audio_source_jobs[2].state = 1u;              /* two jobs out: refused */
    assert(br_audio_source_c1_late_preflight(bus, BR_BLOCK, 9u, dl, dl + FM1_AUDIO_HALF_TICKS,
        &mailbox.done_seq, 12u) == BR_AUDIO_C1_PREFLIGHT_INVALID);
    br_audio_source_jobs[2].state = 2u;
    atomic_store(&c1_release, 1u);
    assert(!pthread_join(worker, 0));
    atomic_store(&c1_pause, 0u);
    assert(mailbox.done_seq == 12u && mailbox.result == BR_AUDIO_QUEUE_COMPLETE_LATE);
    assert(br_audio_source_jobs[8].executor_core == 1u);
    assert(br_audio_source_c1_late_read(12u, dl, br_audio_c1_late_record.cutoff_tick, &late));
    assert(late.index == 8u);
    assert(br_audio_source_accept_complete_late(bus, BR_BLOCK, 9u, dl, &late));
    assert(br_audio_source_queue.completed == 9u && br_audio_source_jobs[8].state == 2u);
    assert(br_fx_fusion_done[2] == 1u);
    br_audio_source_fusion_cancel();
    pause_job = 7u; expire_job = 7u; expire_on_return = 0u; expire_core = 0u;
}

int main(void)
{
    uint32_t ok, uncontained;
    test_split_layout();
    test_split_runs_on_two_cores();
    test_split_fx_late_on_c1();
    setup();
    assert(br_audio_worker_source_queue_context_valid(&br_audio_source_queue));
    expire_on_return = 0u;
    ok = br_audio_source_queue_run(0u);
    assert(ok && worker_joined && mailbox.result == 1u);
    assert(br_audio_source_queue.completed == 8u && br_audio_source_jobs[7].state == 2u);
    assert(br_fx_fusion_done[3]);
    assert(bus[3][0] != 0); /* actual finalizer and post-processing ran */
    memcpy(on_time_bus, bus[3], sizeof on_time_bus);
    memcpy(on_time_key, br_comp_key[3], sizeof on_time_key);

    setup();
    expire_on_return = 1u;
    ok = br_audio_source_queue_run(0u);
    assert(!ok && worker_joined && mailbox.result == 1u);
    assert(br_audio_source_queue.completed == 7u && br_audio_source_jobs[7].state == 1u);
    assert(br_fx_fusion_done[3] && bus[3][0] != 0); /* useful callback already ran */
    assert(!memcmp(on_time_bus, bus[3], sizeof on_time_bus));
    assert(!memcmp(on_time_key, br_comp_key[3], sizeof on_time_key));

    setup();
    br_audio_source_jobs[7].predecessor4 = 8u; /* malformed dependency */
    assert(!br_audio_worker_source_queue_context_valid(&br_audio_source_queue));
    enter_worker = 0u;
    expire_on_return = 0u;
    assert(br_audio_source_queue_run(0u)); /* nonblocking, but incomplete */
    assert(br_audio_source_queue.completed == 7u && br_audio_source_jobs[7].state == 0u);
    assert(!br_fx_fusion_done[3]);

    setup();
    mailbox.task_context ^= 1u; /* genuinely bad worker context */
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    assert(mailbox.done_seq == mailbox.request_seq && mailbox.result == 0u);
    uncontained = 0u;
    assert(!fm1_cpu1_worker_note_reset_confirmation(&uncontained, 0u));
    assert(uncontained == 1u); /* unconfirmed ownership remains sticky */
    assert(fm1_cpu1_worker_note_reset_confirmation(&uncontained, 1u));
    assert(uncontained == 1u);
    engine_setup(); engine_case = ENGINE_ONTIME;
    expire_on_return = 0u;
    br_engine_render(bus, BR_BLOCK);
    br_engine_post_join_finish();   /* the app runs deferred next-block work after FX */
    assert(worker_joined && mailbox.result == 1u);
    assert(br_audio_source_queue.completed == 8u);
    assert(br_audio_source_worker_fault == 0u);
    memcpy(on_time_mod, br_mod_state, sizeof on_time_mod);
    memcpy(on_time_engine_bus, bus, sizeof on_time_engine_bus);
    memcpy(on_time_fx, br_fx_track, sizeof on_time_fx);
    memcpy(on_time_env, br_env, sizeof on_time_env);
    memcpy(on_time_seq, (const void *)br_seq_clock, sizeof on_time_seq);

    engine_setup(); engine_case = ENGINE_LATE;
    expire_on_return = 1u;
    br_engine_render(bus, BR_BLOCK);
    assert(worker_joined && mailbox.result == 1u);
    assert(br_audio_source_queue.count == 8u && br_audio_source_queue.completed == 8u);
    assert(br_audio_source_worker_fault == 0u && br_engine_late_drop_get());
    assert(finalizer_calls == 1u && !br_fx_fusion_pending);
    assert(br_audio_late_drop_count == 1u);
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
    /* Whole-graph recovery does not pretend to know which callback was late. */
    assert(br_audio_late_last_index == UINT32_MAX);
    assert(br_audio_late_last_return == UINT32_MAX);
#else
    assert(br_audio_late_last_index == 7u);
    assert(br_audio_late_last_deadline == br_audio_late_last_return);
#endif
    assert(br_audio_late_last_sequence == 12u);
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
    assert(br_audio_fusion_job_history[3][4][1].samples[0] == 1u);
    assert(br_audio_fusion_job_history[3][4][1].average_ticks[0] == 12345u);
#endif
#endif
    assert(!memcmp(on_time_mod, br_mod_state, sizeof on_time_mod));
    assert(!memcmp(on_time_engine_bus, bus, sizeof on_time_engine_bus));
    assert(!memcmp(on_time_fx, br_fx_track, sizeof on_time_fx));
    assert(!memcmp(on_time_env, br_env, sizeof on_time_env));
    assert(!memcmp(on_time_seq, (const void *)br_seq_clock, sizeof on_time_seq));
    /* A fully joined 8/8 batch remains recoverable if the soft cutoff passes
     * during validation or its C0 source tail. No worker reset is implied. */
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
    for (uint32_t where = 0u; where < 3u; ++where) {
        engine_setup(); engine_case = ENGINE_ONTIME; expire_on_return = 0u;
        boundary_during_join = where == 0u;
        boundary_during_validation = where == 1u;
        boundary_during_tail = where == 2u;
        br_engine_render(bus, BR_BLOCK);
        assert(!br_audio_source_worker_fault && br_engine_late_drop_get());
        assert(br_audio_source_queue.completed == 8u && finalizer_calls == 1u);
        assert(br_audio_late_last_return == UINT32_MAX);
        assert(br_audio_late_last_cutoff == br_audio_late_last_deadline);
        assert(!br_fx_fusion_pending);
    }
#endif
    /* A subsequent half can render without resetting the engine or fault
     * state; the next finalizer is new work, not a replay of the dropped job. */
    boundary_during_join = boundary_during_validation = boundary_during_tail = 0u;
    expire_on_return = 0u;
    br_engine_render(bus, BR_BLOCK);
    assert(!br_audio_source_worker_fault && !br_engine_late_drop_get());
    assert(br_audio_late_drop_count == 1u && finalizer_calls == 2u);
    assert(br_fx_fusion_pending == 1u);
    {
        int32_t output[2u * BR_BLOCK] = {0};
        br_fx_process(bus, output, BR_BLOCK);
        assert(!br_fx_fusion_pending);
    }
    engine_setup(); engine_case = ENGINE_ONTIME; expire_on_return = 0u;
    br_engine_render(bus, BR_BLOCK / 2u);
    assert(!br_audio_source_worker_fault && !br_engine_late_drop_get());
    assert(br_audio_source_queue.completed == 8u && finalizer_calls == 1u);

    /* The timestamp comparison remains valid across the 32-bit T4 wrap. */
    engine_setup(); engine_case = ENGINE_LATE; expire_on_return = 1u;
    ticks = UINT32_MAX - 70000u;
    br_engine_render(bus, BR_BLOCK);
    assert(!br_audio_source_worker_fault && br_engine_late_drop_get());
    assert(br_audio_source_queue.completed == 8u && finalizer_calls == 1u);

    /* The callback can return on time and cross the cutoff while acquiring
     * the lock or at the second, locked publication check. */
    for (uint32_t edge = 2u; edge <= 3u; ++edge) {
        engine_setup(); engine_case = ENGINE_LATE; expire_on_return = edge;
        br_engine_render(bus, BR_BLOCK);
        assert(!br_audio_source_worker_fault && br_engine_late_drop_get());
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
        assert(br_audio_late_last_return == UINT32_MAX);
#else
        assert(br_audio_late_last_return == br_audio_late_last_deadline - 1u);
#endif
        assert(br_audio_late_last_cutoff == br_audio_late_last_deadline);
        assert(br_audio_source_queue.lock == 0u && finalizer_calls == 1u);
    }
    /* Return, lock acquisition and the locked cutoff check are on time, but
     * the cutoff passes before the next loop iteration after publication. */
    engine_setup(); engine_case = ENGINE_LATE; expire_on_return = 4u;
    br_engine_render(bus, BR_BLOCK);
    assert(worker_joined && mailbox.result == 1u);
    assert(br_audio_source_queue.completed == 8u &&
           br_audio_source_jobs[7].state == 2u && !br_audio_source_queue.lock);
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
    assert(!br_audio_source_worker_fault && br_engine_late_drop_get());
#else
    assert(br_audio_source_worker_fault && !br_engine_late_drop_get());
#endif
    assert(!br_fx_fusion_pending);
    assert(finalizer_calls == 1u);
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
    /* Both fixed-plan cursors finish, the worker joins, and the exact graph
     * seals. Crossing the cutoff still drops before master FX/output. */
    engine_setup(); fake_fixed_plan = 1u;
    engine_case = ENGINE_LATE; expire_on_return = 2u;
    br_engine_render(bus, BR_BLOCK);
    assert(worker_joined && mailbox.result == 1u);
    assert(!br_audio_source_worker_fault && br_engine_late_drop_get());
    assert(br_audio_source_queue.fixed_plan_active &&
           br_audio_source_queue.completed == 8u);
    assert(!br_fx_fusion_pending && finalizer_calls == 1u);
    fake_fixed_plan = 0u;
#endif
    /* C1 can report that its own drain stopped at cutoff while C0 already
     * owns the finalizer. The joined C1 result and C0 late receipt must both
     * validate before the block is dropped. */
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
    engine_setup(); worker_sees_expired = 1u;
    engine_case = ENGINE_LATE; expire_on_return = 1u;
    br_engine_render(bus, BR_BLOCK);
    assert(worker_joined && mailbox.result == BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(!br_audio_source_worker_fault && br_engine_late_drop_get());
    assert(br_audio_source_queue.completed == 8u && !br_fx_fusion_pending);
    assert(finalizer_calls == 1u);
    worker_sees_expired = 0u;
#endif
    {
        br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
        assert(!br_audio_source_record_late(&late, 7u, 8u, 99u,
                                            br_audio_source_queue.deadline_tick - 1u));
        assert(late.index == UINT32_MAX);
    }

    /* The selected fast-cutoff fixed plan checks its final cursor before
     * checking time again. A completed final callback succeeds even when the
     * cutoff passes immediately after its last pre-publication time check. */
    for (uint32_t core = 0u; core < 2u; ++core) {
        setup(); engine_case = ENGINE_LATE; expire_on_return = 2u; enter_worker = 0u;
        br_audio_source_queue.fixed_plan_active = 1u;
        br_audio_source_queue.fixed_plan_count[core] = 1u;
        for (uint32_t i = 0u; i < 7u; ++i)
            br_audio_source_jobs[i].fixed_plan_core = 1u - core;
        br_audio_source_jobs[7].fixed_plan_core = core;
        br_audio_source_jobs[7].fixed_plan_order = 0u;
        test_core_id = expire_core = core;
        assert(br_audio_source_queue_run_internal(0u, 0) == 1u);
        assert(ticks == br_audio_source_queue.deadline_tick);
        assert(br_audio_source_jobs[7].state == 2u);
        assert(br_audio_source_queue.fixed_plan_completed[core] == 1u);
        assert(br_audio_source_queue.fixed_plan_cursor[core] == 1u);
    }

    /* Fixed-plan and C1 late callbacks still take the legacy failure path. */
    setup(); engine_case = ENGINE_LATE; expire_on_return = 1u; enter_worker = 0u;
    br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_queue.fixed_plan_count[0] = 1u;
    for (uint32_t i = 0u; i < 7u; ++i) br_audio_source_jobs[i].fixed_plan_core = 1u;
    br_audio_source_jobs[7].fixed_plan_core = 0u;
    br_audio_source_jobs[7].fixed_plan_order = 0u;
    {
        br_audio_source_late_t late = {UINT32_MAX, 0u};
        assert(!br_audio_source_queue_run_internal(0u, &late));
        assert(late.index == UINT32_MAX && br_audio_source_jobs[7].state == 1u);
    }
    setup(); engine_case = ENGINE_LATE; expire_on_return = 1u; enter_worker = 0u;
    test_core_id = expire_core = 1u;
    assert(!br_audio_source_queue_run(0u));
    assert(br_audio_source_jobs[7].state == 1u && br_audio_source_queue.completed == 7u);
    for (uint32_t core = 0u; core < 2u; ++core) {
        setup(); engine_case = ENGINE_LATE; expire_on_return = 0u; enter_worker = 0u;
        test_core_id = core; ticks = br_audio_source_queue.deadline_tick;
        assert(!br_audio_source_queue_run(0u));
        assert(br_audio_source_jobs[7].state == 0u && br_audio_source_queue.completed == 7u);
    }

    engine_setup(); engine_case = ENGINE_BAD_GRAPH;
    expire_on_return = 0u;
    br_engine_render(bus, BR_BLOCK);
    assert(worker_joined && mailbox.result == 1u);
    assert(br_audio_source_queue.completed == 7u && br_audio_source_worker_fault == 1u);

    engine_setup(); engine_case = ENGINE_BAD_WORKER;
    expire_on_return = 0u;
    br_engine_render(bus, BR_BLOCK);
    assert(worker_joined && mailbox.result == 0u);
    assert(br_audio_source_queue.completed == 8u && br_audio_source_worker_fault == 1u);
    {
        const uint32_t failures[] = {
            ENGINE_BAD_TASK, ENGINE_BAD_SEQUENCE, ENGINE_BAD_RESULT,
            ENGINE_BAD_CONTEXT, ENGINE_BAD_CALLBACK, ENGINE_HELD_LOCK,
            ENGINE_BAD_PENDING_VALUE, ENGINE_BAD_DONE_VALUE, ENGINE_UNCONTAINED,
            ENGINE_INCOMPLETE, ENGINE_BAD_FUSION, ENGINE_BAD_KIND,
            ENGINE_BAD_OUTPUT, ENGINE_BAD_FRAME
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
            , ENGINE_BAD_EXECUTOR
#endif
        };
        for (uint32_t i = 0u; i < sizeof failures / sizeof failures[0]; ++i) {
            engine_setup(); engine_case = failures[i]; expire_on_return = 1u;
            br_engine_render(bus, BR_BLOCK);
            assert(br_audio_source_worker_fault == 1u);
            assert(!br_engine_late_drop_get() && !br_audio_late_drop_count);
            assert(finalizer_calls == 1u);
        }
    }
#if BR_AUDIO_C1_LATE_OWNER_KNOWN
    test_real_c1_finalizer_late();
    test_c1_publication_cutoff();
    test_c1_postpublication_cutoff_success();
    test_other_core_published_completion();
    test_c0_nonfinal_then_c1_late();
    test_partial_corrupt_descriptor_rejected();
    test_failed_c1_callback_remains_failure();
    test_fixed_plan_complete_watch();
    test_c0_late_ack_preflight();
    puts("PASS: actual C0/C1 source callbacks, queue/mailbox, preflight, late publication and fault controls");
#else
    puts("PASS: owner-unavailable configuration preserves ordinary/C0 queue behavior");
#endif
    return 0;
}
