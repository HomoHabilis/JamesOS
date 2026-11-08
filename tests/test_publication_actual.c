/* Exercise the real 1494 queue and post-join validator with deterministic host
 * time and mailbox interleavings. Previously completed jobs in these focused
 * fixtures are seeded; the two explicitly exercised callbacks are real. */
#define main inherited_1493_queue_regression_main
#include "test_preflight_retry_engine.c"
#undef main

static uint32_t retire_tries, retire_release_on;
void br_audio_source_test_retire_probe(uint32_t expired)
{
    (void)expired;
    ++retire_tries;
    if (retire_release_on && retire_tries == retire_release_on)
        br_audio_source_queue.lock = 0u;
}

static void prepare_real_graph(void)
{
    setup();
    inject_engine_run = 0u;
    engine_case = ENGINE_ONTIME;
    quiet_plaits = 1u;
    expire_on_return = enter_worker = worker_sees_expired = 0u;
    expire_core = expire_after_reads = 0u;
    retire_tries = retire_release_on = 0u;
    assert(br_audio_build_fusion_jobs(bus, BR_BLOCK) == 8u);
    br_audio_source_queue.count = 8u;
    br_audio_source_queue.completed = 0u;
    br_audio_source_queue.lock = 0u;
    br_audio_source_publication_enabled = 1u;
    for (uint32_t i = 0; i < 8u; ++i) {
        br_audio_source_jobs[i].state = 0u;
        br_audio_source_jobs[i].duration_ticks = 0u;
        br_audio_source_jobs[i].executor_core = UINT32_MAX;
    }
    for (uint32_t i = 0; i < BR_PARTS; ++i) br_fx_fusion_done[i] = 0u;
    br_audio_late_drop_reset();
}

static void seed_job(uint32_t index, uint32_t core)
{
    br_audio_source_jobs[index].state = 2u;
    br_audio_source_jobs[index].executor_core = core;
    ++br_audio_source_queue.completed;
    if (index < 3u) br_fx_fusion_done[index] = 1u;
    if (index == 7u) br_fx_fusion_done[3] = 1u;
}

static uint32_t joined_valid(uint32_t result, uint32_t *completed)
{
    mailbox.done_seq = mailbox.request_seq;
    mailbox.result = result;
    return br_audio_source_joined_graph_valid(bus, BR_BLOCK, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, result, 0u, completed);
}

static void test_late_independent(uint32_t core)
{
    br_audio_source_late_t ignored = {UINT32_MAX, 0u, 0u};
    uint32_t result, completed;
    prepare_real_graph();
    for (uint32_t i = 1u; i <= 5u; ++i) seed_job(i, 1u - core);
    /* Finalizer remains unready because its last kit lane is unclaimed. */
    br_audio_source_jobs[0].cost_hint = UINT32_MAX - 1u;
    br_audio_source_jobs[6].cost_hint = 1u;
    test_core_id = expire_core = core;
    expire_job = 0u; expire_on_return = 1u;
    if (core == 0u) result = br_audio_source_queue_run_internal(0u, &ignored);
    else {
        fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
        result = mailbox.result;
    }
    assert(result == BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_jobs[0].state == 2u &&
           br_audio_source_jobs[0].executor_core == core);
    assert(br_audio_source_jobs[6].state == 0u &&
           br_audio_source_jobs[7].state == 0u);
    assert(joined_valid(result, &completed) && completed == 6u);
    br_audio_source_drop_partial(8u, br_audio_source_queue.deadline_tick,
                                  12u, br_audio_source_now());
    assert(!br_fx_fusion_pending && br_engine_late_drop_get());
    assert(br_audio_late_drop_count == 1u && br_audio_partial_drop_count == 1u);
    assert(br_audio_late_last_index == UINT32_MAX &&
           br_audio_late_last_core == UINT32_MAX &&
           br_audio_late_last_return == UINT32_MAX);
    for (uint32_t p = 0; p < BR_PARTS; ++p) assert(!br_quiet_part[p]);
}

static void test_two_late_owners(void)
{
    pthread_t worker;
    br_audio_source_late_t ignored = {UINT32_MAX, 0u, 0u};
    uint32_t completed;
    prepare_real_graph();
    for (uint32_t i = 1u; i <= 6u; ++i) seed_job(i, 1u);
    atomic_store(&c1_pause, 1u); atomic_store(&c1_started, 0u);
    atomic_store(&c1_release, 0u);
    assert(!pthread_create(&worker, 0, run_c1_worker, 0));
    while (!atomic_load(&c1_started)) sched_yield();
    assert(br_audio_source_jobs[7].state == 1u);
    test_core_id = expire_core = 0u;
    expire_job = 0u; expire_on_return = 1u;
    assert(br_audio_source_queue_run_internal(0u, &ignored) ==
           BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_queue.completed == 7u);
    atomic_store(&c1_release, 1u);
    assert(!pthread_join(worker, 0));
    atomic_store(&c1_pause, 0u);
    assert(mailbox.done_seq == 12u &&
           mailbox.result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE);
    assert(br_audio_source_jobs[0].executor_core == 0u &&
           br_audio_source_jobs[7].executor_core == 1u);
    assert(joined_valid(mailbox.result, &completed) && completed == 8u);
}

static void test_fixed_late(void)
{
    br_audio_source_late_t ignored = {UINT32_MAX, 0u, 0u};
    uint32_t completed;
    prepare_real_graph();
    br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_queue.fixed_plan_count[0] = 1u;
    br_audio_source_queue.fixed_plan_count[1] = 7u;
    br_audio_source_queue.fixed_plan_completed[1] = 7u;
    br_audio_source_queue.fixed_plan_cursor[1] = 7u;
    for (uint32_t i = 0; i < 8u; ++i) {
        br_audio_source_jobs[i].fixed_plan_core = i ? 1u : 0u;
        br_audio_source_jobs[i].fixed_plan_order = i ? i - 1u : 0u;
        if (i) seed_job(i, 1u);
    }
    br_audio_source_queue.completed = 0u; /* fixed counters own publication */
    test_core_id = expire_core = 0u;
    expire_job = 0u; expire_on_return = 1u;
    assert(br_audio_source_queue_run_internal(0u, &ignored) ==
           BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_jobs[0].state == 2u &&
           br_audio_source_queue.fixed_plan_completed[0] == 1u &&
           br_audio_source_queue.fixed_plan_cursor[0] == 1u);
    assert(br_audio_fixed_plan_seal_completed());
    assert(joined_valid(BR_AUDIO_QUEUE_OK, &completed) && completed == 8u);
}

static void test_partial_validator(void)
{
    br_audio_source_late_t ignored = {UINT32_MAX, 0u, 0u};
    uint32_t completed;
    prepare_real_graph();
    for (uint32_t i = 0; i < 6u; ++i) seed_job(i, i & 1u);
    ticks = br_audio_source_queue.deadline_tick;
    /* Both real queue actors decline all new claims at the observed cutoff. */
    test_core_id = 0u;
    assert(br_audio_source_queue_run_internal(0u, &ignored) ==
           BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    test_core_id = 1u;
    assert(br_audio_source_queue_run_internal(0u, &ignored) ==
           BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed) && completed == 6u);
    br_audio_source_jobs[6].state = 1u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[6].state = 0u;
    br_audio_source_jobs[6].executor_core = 0u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[6].executor_core = UINT32_MAX;
    br_audio_source_jobs[6].duration_ticks = 1u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[6].duration_ticks = 0u;
    --br_audio_source_queue.completed;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    ++br_audio_source_queue.completed;
    br_fx_fusion_done[2] = 0u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_fx_fusion_done[2] = 1u;
    br_fx_fusion_done[3] = 1u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_fx_fusion_done[3] = 0u;
    br_fx_fusion_pending = 2u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_fx_fusion_pending = 1u;
    br_audio_source_jobs[7].state = 2u;
    br_audio_source_jobs[7].executor_core = 1u;
    ++br_audio_source_queue.completed;
    br_fx_fusion_done[3] = 1u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed)); /* unmet lane */
    br_audio_source_jobs[7].state = 0u;
    br_audio_source_jobs[7].executor_core = UINT32_MAX;
    --br_audio_source_queue.completed;
    br_fx_fusion_done[3] = 0u;
    br_audio_source_jobs[0].output = bus[1];
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[0].output = bus[0];
    assert(!joined_valid(BR_AUDIO_QUEUE_FAILED, &completed));
    assert(!br_audio_source_joined_graph_valid(bus, BR_BLOCK / 2u, 8u,
        br_audio_source_queue.deadline_tick, &mailbox, 12u,
        FM1_CPU1_WORKER_OK, BR_AUDIO_QUEUE_CUTOFF_DRAINED, 0u, &completed));
    assert(joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
}

static void test_fixed_partial_validator(void)
{
    uint32_t completed;
    prepare_real_graph();
    br_audio_source_queue.fixed_plan_active = 1u;
    br_audio_source_queue.fixed_plan_count[0] = 4u;
    br_audio_source_queue.fixed_plan_count[1] = 4u;
    for (uint32_t i = 0; i < 8u; ++i) {
        br_audio_source_jobs[i].fixed_plan_core = i / 4u;
        br_audio_source_jobs[i].fixed_plan_order = i % 4u;
        if (i < 3u || (i >= 4u && i < 6u)) seed_job(i, i / 4u);
    }
    br_audio_source_queue.completed = 0u;
    br_audio_source_queue.fixed_plan_completed[0] = 3u;
    br_audio_source_queue.fixed_plan_cursor[0] = 3u;
    br_audio_source_queue.fixed_plan_completed[1] = 2u;
    br_audio_source_queue.fixed_plan_cursor[1] = 2u;
    assert(joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed) && completed == 5u);
    ++br_audio_source_queue.fixed_plan_completed[1];
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    --br_audio_source_queue.fixed_plan_completed[1];
    br_audio_source_jobs[5].state = 0u;
    br_audio_source_jobs[5].executor_core = UINT32_MAX;
    br_audio_source_jobs[6].state = 2u;
    br_audio_source_jobs[6].executor_core = 1u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[5].state = 2u;
    br_audio_source_jobs[5].executor_core = 1u;
    br_audio_source_jobs[6].state = 0u;
    br_audio_source_jobs[6].executor_core = UINT32_MAX;
    br_audio_source_jobs[5].executor_core = 0u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[5].executor_core = 1u;
    br_audio_source_jobs[6].fixed_plan_order = 0u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_jobs[6].fixed_plan_order = 2u;
    br_audio_source_queue.fixed_plan_active = 2u;
    assert(!joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
    br_audio_source_queue.fixed_plan_active = 1u;
    assert(joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed));
}

static void test_partial_extremes(void)
{
    uint32_t completed;
    prepare_real_graph();
    assert(joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed) && completed == 0u);
    for (uint32_t i = 0u; i < 7u; ++i) seed_job(i, i & 1u);
    assert(joined_valid(BR_AUDIO_QUEUE_CUTOFF_DRAINED, &completed) && completed == 7u);
}

static void test_failure_and_full_graph_controls(void)
{
    uint32_t completed;
    prepare_real_graph();
    for (uint32_t i = 0; i < 7u; ++i) seed_job(i, 0u);
    corrupt_c1_after_claim = 1u; test_core_id = 1u;
    fm1_cpu1_worker_step(&mailbox, dispatch, 0, &worker_loops);
    assert(mailbox.result == BR_AUDIO_QUEUE_FAILED &&
           br_audio_source_jobs[7].state == 1u);
    assert(!joined_valid(BR_AUDIO_QUEUE_FAILED, &completed));
    corrupt_c1_after_claim = 0u;
    prepare_real_graph();
    for (uint32_t i = 0; i < 8u; ++i) seed_job(i, i & 1u);
    assert(joined_valid(BR_AUDIO_QUEUE_OK, &completed) && completed == 8u);
    br_audio_source_jobs[0].output = bus[1];
    assert(!joined_valid(BR_AUDIO_QUEUE_OK, &completed));
    br_audio_source_jobs[0].output = bus[0];
    br_audio_source_jobs[0].executor_core = 2u;
    assert(!joined_valid(BR_AUDIO_QUEUE_OK, &completed));
    br_audio_source_jobs[0].executor_core = 0u;
    br_audio_source_publication_enabled = 2u;
    assert(!joined_valid(BR_AUDIO_QUEUE_OK, &completed));
}

static void test_preclaim_observation(void)
{
    br_audio_source_late_t ignored = {UINT32_MAX, 0u, 0u};
    prepare_real_graph();
    for (uint32_t i = 1u; i <= 5u; ++i) seed_job(i, 1u);
    br_audio_source_jobs[0].cost_hint = UINT32_MAX - 1u;
    br_audio_source_jobs[6].cost_hint = 1u;
    test_core_id = 0u;
    /* A clock crossing after the preclaim read is not an atomic cutoff. */
    expire_after_claim_before_callback = 1u;
    assert(br_audio_source_queue_run_internal(0u, &ignored) ==
           BR_AUDIO_QUEUE_CUTOFF_DRAINED);
    assert(br_audio_source_jobs[0].state == 2u &&
           br_audio_source_jobs[6].state == 0u &&
           br_audio_source_jobs[7].state == 0u);
    assert(!br_audio_source_can_publish_late(BR_BLOCK / 2u, 8u));
    assert(!br_audio_source_can_publish_late(BR_BLOCK, 7u));
    br_audio_source_fx_fusion_active = 0u;
    assert(!br_audio_source_can_publish_late(BR_BLOCK, 8u));
}

static void test_retire_boundaries(void)
{
    const uint32_t hard = 2000u;
    prepare_real_graph();
    ticks = hard - 1u; br_audio_source_queue.lock = 1u;
    retire_release_on = 3u;
    assert(br_audio_source_retire_lock(hard) && retire_tries == 3u);
    br_audio_source_queue_unlock();
    retire_tries = 0u; retire_release_on = 0u;
    ticks = hard; br_audio_source_queue.lock = 0u;
    assert(br_audio_source_retire_lock(hard) && retire_tries == 1u);
    br_audio_source_queue_unlock();
    retire_tries = 0u; br_audio_source_queue.lock = 1u;
    assert(!br_audio_source_retire_lock(hard) && retire_tries == 1u);
    retire_tries = 0u; ticks = hard - 1u;
    assert(!br_audio_source_retire_lock(hard) &&
           retire_tries == BR_AUDIO_RETIRE_LOCK_SPINS);
    /* Wrapped hard deadline: the same signed comparison preserves the bound. */
    retire_tries = 0u; retire_release_on = 2u; ticks = UINT32_MAX;
    assert(br_audio_source_retire_lock(1u) && retire_tries == 2u);
    br_audio_source_queue_unlock();
    retire_tries = 0u; retire_release_on = 0u;
    br_audio_source_queue.lock = 1u; ticks = 1u;
    assert(!br_audio_source_retire_lock(1u) && retire_tries == 1u);
    br_audio_source_queue.lock = 0u;
}

int main(void)
{
    test_late_independent(0u);
    test_late_independent(1u);
    test_two_late_owners();
    test_fixed_late();
    test_partial_validator();
    test_fixed_partial_validator();
    test_partial_extremes();
    test_failure_and_full_graph_controls();
    test_preclaim_observation();
    test_retire_boundaries();
    puts("PASS: actual generalized publication, two-owner/fixed/partial graphs and bounded retirement");
    return 0;
}
