/* SPDX-License-Identifier: GPL-3.0-only
 * Real source/FX follow-up to a validated, owner-joined partial drop.
 * Two processes repeat the same DSP trajectory, one poisoning abandoned
 * scratch before two complete frames. The runner compares their outputs.
 * This is a single-core host reconstruction test, not a DMA/IRQ model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DINGHY_HOST 1
#define BR_ENABLE_PLAITS 1
#define BR_SOURCE_QUEUE_TEST 1
#define BR_LATE_COMPLETION_TEST 1
#define BR_C1_LATE_COMPLETION_TEST 1
uint32_t fm1_cpu1_worker_submit_fx_batch(void *, uint32_t *);
#include "../firmware/src/instrument_audio_unit.c"

volatile uint32_t br_ready;
volatile uint32_t fm1_cpu1_worker_uncontained_fault;
__thread uint32_t test_core_id;
uint32_t br_audio_source_test_parallel;
static fm1_cpu1_worker_mailbox_t test_mailbox;
uint32_t fm1_cpu1_worker_available(void) { return 0u; }
uint32_t fm1_cpu1_worker_submit_fx_batch(void *context, uint32_t *sequence)
{ (void)context; (void)sequence; abort(); }
uint32_t fm1_cpu1_worker_wait(uint32_t sequence, uint32_t ticks, uint32_t *result)
{ (void)sequence; (void)ticks; (void)result; abort(); }
uint32_t fm1_cpu1_worker_abort_hold_reset(void) { abort(); }
uint32_t br_audio_source_test_ticks(void) { return 0u; }
void br_audio_source_test_begin(uint32_t job) { (void)job; }
void br_audio_source_test_end(uint32_t job) { (void)job; }
void br_audio_source_test_reset(uint32_t count) { (void)count; }
uint32_t br_audio_source_test_start(void *queue) { (void)queue; abort(); }
uint32_t br_audio_source_test_join_ticks(uint32_t ticks) { (void)ticks; abort(); }
uint32_t br_audio_source_test_join(void) { abort(); }
uint32_t br_audio_source_test_done(uint32_t count)
{
    uint32_t i;
    for (i = 0u; i < count; ++i)
        if (br_audio_source_jobs[i].state != 2u) return 0u;
    return 1u;
}
const fm1_cpu1_worker_mailbox_t *br_audio_source_test_c1_mailbox(void)
{ return &test_mailbox; }
uint32_t br_audio_source_test_late_sequence(void) { return 0u; }

/* Host-only hooks at timing/validation boundaries; no simulated deadline
 * movement is needed in the reconstruction frame. */
void br_audio_source_test_validation_boundary(void) {}
void br_audio_source_test_join_boundary(void) {}
void br_audio_source_test_tail_boundary(void) {}
void br_audio_source_test_fixed_snapshot_boundary(void) {}
const fm1_cpu1_worker_mailbox_t *br_audio_source_test_late_mailbox(void) { return &test_mailbox; }

static int32_t bus[BR_PARTS][BR_BLOCK];
static int32_t output[BR_BLOCK * 2u];

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void put(const void *data, size_t bytes)
{
    if (fwrite(data, 1u, bytes, stdout) != bytes) fail("trace write failed");
}

static void configure(uint32_t kit, uint32_t key_lane)
{
    uint32_t p, lane;
    memset(br_part, 0, sizeof br_part);
    for (p = 0u; p < BR_PARTS; ++p) {
        br_sound_t sound;
        br_mod_config_t mod;
        br_fx_config_t fx;
        br_preset_sound(p == BR_DRUM_PART ? kit : BR_FIRST_PLAITS + p, &sound);
        br_module_defaults(&mod, &fx);
        sound.level = 100u;
        sound.filter = 127u;
        sound.delay = 43u;
        sound.reverb = 33u;
        sound.pan = 64u;
        fx.delay_time = 45u;
        fx.delay_feedback = 35u;
        fx.reverb_tone = 64u;
        br_part[p].sound = sound;
        br_part[p].mod = mod;
        br_part[p].fx = fx;
        br_part[p].revision = 2u;
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            br_part[p].lane_level[lane] = 127u;
            br_part[p].lane_gate[lane] = 128u;
        }
    }
    br_master.revision = 2u;
    br_master.comp.source = BR_COMP_DRUM_1 + key_lane;
    br_master.comp.threshold = 18u;
    br_master.comp.ratio = 3u;
    br_master.comp.mix = 127u;
    br_playing = 0u;
    br_engine_init();
    br_fx_init();
    br_engine_source_fx_fusion_set(1u);
}

static void full_frame(void)
{
    uint32_t p;
    memset(output, 0, sizeof output);
    br_engine_render(bus, BR_BLOCK);
    if (br_audio_source_worker_fault || br_engine_late_drop_get())
        fail("unexpected fault/drop in ordinary reconstruction frame");
    if (br_audio_source_queue.completed != 8u || br_audio_source_queue.lock ||
        !br_fx_fusion_complete(bus, BR_BLOCK))
        fail("next frame did not complete its real source/FX graph");
    for (p = 0u; p < BR_PARTS; ++p)
        if (br_fx_fusion_done[p] != 1u || !br_fx_job_enabled[p])
            fail("missing fusion part or disabled wet effects");
    br_fx_process(bus, output, BR_BLOCK);
    if (br_fx_fusion_pending) fail("completed fusion was not consumed");
}

static void joined_partial(uint32_t key_lane, uint32_t pattern)
{
    static const uint32_t unfinished_kit[] = {0u, 2u, 3u, 5u};
    static const uint32_t finished_kit[] = {2u, 3u, 4u, 5u, 6u, 7u};
    const uint32_t *finished = pattern ? finished_kit : unfinished_kit;
    const uint32_t finished_count = pattern ?
        sizeof finished_kit / sizeof finished_kit[0] :
        sizeof unfinished_kit / sizeof unfinished_kit[0];
    fm1_cpu1_worker_mailbox_t mailbox = {0};
    uint32_t count, i, completed = UINT32_MAX;
    for (i = 0u; i < BR_PARTS; ++i)
        memset(br_comp_key[i], 0, sizeof br_comp_key[i]);
    if (!br_fx_fusion_prepare(bus, BR_BLOCK)) fail("partial prepare failed");
    br_audio_source_fx_fusion_active = 1u;
    br_audio_fixed_plan_disable();
    count = br_audio_build_fusion_jobs(bus, BR_BLOCK);
    if (count != 8u) fail("unexpected graph");
    br_audio_source_queue.count = count;
    br_audio_source_queue.completed = 0u;
    br_audio_source_queue.lock = 0u;
    br_audio_source_queue.deadline_tick = 1000u;
    br_audio_source_publication_enabled = 1u;
    for (i = 0u; i < count; ++i) {
        br_audio_source_jobs[i].state = 0u;
        br_audio_source_jobs[i].duration_ticks = 0u;
        br_audio_source_jobs[i].executor_core = UINT32_MAX;
    }
    for (i = 0u; i < finished_count; ++i) {
        br_audio_source_job_t *job = &br_audio_source_jobs[finished[i]];
        job->state = 1u;
        if (!br_audio_worker_sources(job)) fail("partial callback failed");
        job->state = 2u;
        ++br_audio_source_queue.completed;
    }
    if (!br_audio_kit_key_output(BR_DRUM_PART, key_lane, 1u << key_lane))
        fail("selected sidechain lane did not have a key buffer");
    mailbox.cpu_id = 1u;
    mailbox.request_seq = mailbox.done_seq = 17u;
    mailbox.task_id = FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH;
    mailbox.task_context = (uint32_t)(uintptr_t)&br_audio_source_queue;
    mailbox.result = BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    if (!br_audio_source_joined_graph_valid(bus, BR_BLOCK, count, 1000u,
            &mailbox, 17u, FM1_CPU1_WORKER_OK, mailbox.result, 0u, &completed) ||
        completed != finished_count)
        fail("joined partial graph rejected");
    br_audio_source_drop_partial(count, 1000u, 17u, 1001u);
    if (!br_engine_late_drop_get() || br_audio_partial_drop_count != 1u ||
        br_fx_fusion_pending || br_engine_quiet())
        fail("partial drop did not cancel fusion or block quiet");
    for (i = 0u; i < BR_PARTS; ++i)
        if (br_quiet_part[i]) fail("partial drop left stale quiet flag");
}

static void poison_abandoned_scratch(void)
{
    memset(bus, 0x5a, sizeof bus);
    memset(br_comp_key, 0x5a, sizeof br_comp_key);
    memset(br_audio_kit_partial, 0x5a, sizeof br_audio_kit_partial);
    memset(br_audio_kit_key_partial, 0x5a, sizeof br_audio_kit_key_partial);
    memset(br_audio_fused_osc_block, 0x5a, sizeof br_audio_fused_osc_block);
    memset(br_fx_track_stereo, 0x5a, sizeof br_fx_track_stereo);
    memset((void *)br_fx_fusion_done, 0x5a, sizeof br_fx_fusion_done);
}

int main(int argc, char **argv)
{
    uint32_t kit, key_lane, poison, pattern, i, nonzero = 0u, key_nonzero = 0u;
    if (argc != 5) fail("expected kit, key lane, partial pattern, and poison flag");
    kit = (uint32_t)strtoul(argv[1], 0, 10);
    key_lane = (uint32_t)strtoul(argv[2], 0, 10);
    pattern = (uint32_t)strtoul(argv[3], 0, 10);
    poison = (uint32_t)strtoul(argv[4], 0, 10);
    if (key_lane >= BR_DRUM_LANES || poison > 1u || pattern > 1u)
        fail("invalid case");
    configure(kit, key_lane);
    /* The kit goes first: cold starts are admitted one per half, and this fixture needs the kit (and its
     * sidechain key) sounding in the first frames. */
    br_engine_note_on(BR_DRUM_PART, 36u, 115u);
    br_engine_note_on(BR_DRUM_PART, 38u, 110u);
    br_engine_note_on(BR_DRUM_PART, 42u, 105u);
    br_engine_note_on(BR_DRUM_PART, 49u, 100u);
    for (i = 0u; i < BR_SYNTH_PARTS; ++i)
        br_engine_note_on(i, 60u + i * 3u, 110u);
    full_frame();
    joined_partial(key_lane, pattern);
    if (poison) poison_abandoned_scratch();
    full_frame();
    for (i = 0u; i < BR_BLOCK * 2u; ++i) nonzero |= output[i] != 0;
    if (!nonzero) fail("silent fixture cannot establish output parity");
    for (i = 0u; i < BR_BLOCK; ++i)
        key_nonzero |= br_comp_key[BR_DRUM_PART][i] != 0;
    if (!br_fx_comp_active || !key_nonzero)
        fail("selected kit sidechain was not active and nonzero");
    put(output, sizeof output);
    put(bus, sizeof bus);
    put(br_comp_key, sizeof br_comp_key);
    put(br_audio_kit_partial, sizeof br_audio_kit_partial);
    put(br_audio_kit_key_partial[0][key_lane],
        sizeof br_audio_kit_key_partial[0][key_lane]);
    put(br_audio_fused_osc_block, sizeof br_audio_fused_osc_block);
    put(br_fx_track_stereo, sizeof br_fx_track_stereo);

    /* A poisoned abandoned buffer must not leak into the following frame's
     * persistent voice or effect state either. Observe a second real frame. */
    full_frame();
    put(output, sizeof output);
    put(bus, sizeof bus);
    put(br_comp_key, sizeof br_comp_key);
    put(br_audio_fused_osc_block, sizeof br_audio_fused_osc_block);
    put(br_fx_track_stereo, sizeof br_fx_track_stereo);

    /* Quiet flags set false by partial cancellation recover after a complete
     * silent frame. Reset the actual engine/FX to avoid live voice tails. */
    configure(kit, key_lane);
    for (i = 0u; i < BR_PARTS; ++i) br_quiet_part[i] = 1u;
    if (!br_fx_fusion_prepare(bus, BR_BLOCK)) fail("quiet prepare failed");
    br_audio_source_fx_fusion_active = 1u;
    br_audio_source_drop_partial(8u, 1000u, 19u, 1001u);
    if (br_engine_quiet()) fail("partial quiet guard was not conservative");
    full_frame();
    if (!br_engine_quiet()) fail("quiet state did not recover after a complete frame");
    fprintf(stderr, "PASS: kit %u key lane %u partial pattern %u poison %u, next frame and quiet recovery\n",
            kit, key_lane, pattern, poison);
    return 0;
}
