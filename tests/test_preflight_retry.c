/* Exercises the actual extracted preflight loop with deterministic lock/T4. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "fm1_audio_rate.h"

#define BR_PARTS 4u
#define BR_BLOCK 256u
#define BR_AUDIO_C1_PREFLIGHT_SPINS 8u
enum { BR_AUDIO_C1_PREFLIGHT_NONE = 0u, BR_AUDIO_C1_PREFLIGHT_CLAIMED = 1u,
       BR_AUDIO_C1_PREFLIGHT_COMPLETE = 2u, BR_AUDIO_C1_PREFLIGHT_TERMINAL = 3u,
       BR_AUDIO_C1_PREFLIGHT_INVALID = 4u };
typedef struct { volatile uint8_t lock; } queue_t;
typedef struct { uint32_t executor_core; } job_t;
static queue_t br_audio_source_queue;
static job_t br_audio_source_jobs[8];
static uint32_t samples[16], sample_count, sample_index;
static uint32_t attempts, release_at, unlocks, shape_calls, shape_claimed, shape_complete, fusion_complete;
static uint32_t br_audio_source_now(void)
{
    uint32_t index = sample_index < sample_count ? sample_index++ : sample_count - 1u;
    return samples[index];
}
static void br_audio_source_publish(void) { __sync_synchronize(); }
static uint32_t fm1_fault_owner_try_acquire(volatile uint8_t *lock)
{
    ++attempts;
    if (release_at && attempts == release_at) *lock = 0u;
    return __sync_bool_compare_and_swap(lock, 0u, 1u);
}
static void br_audio_source_queue_unlock(void)
{ ++unlocks; __sync_lock_release(&br_audio_source_queue.lock); }
static uint32_t br_audio_source_late_shape(int32_t bus[BR_PARTS][BR_BLOCK],
    uint32_t n, uint32_t count, uint32_t deadline, uint32_t all_complete)
{
    (void)bus; (void)n; (void)count; (void)deadline; ++shape_calls;
    return all_complete ? shape_complete : shape_claimed;
}
static uint32_t br_audio_call_fx_fusion_complete(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{ (void)bus; (void)n; return fusion_complete; }

#include "preflight_under_test.inc"

static int32_t bus[BR_PARTS][BR_BLOCK];
static volatile uint32_t done;
static void setup(uint32_t soft, uint32_t hard)
{
    (void)hard;
    sample_count = 2u; sample_index = 0u;
    samples[0] = samples[1] = soft;
    br_audio_source_queue.lock = 1u;
    br_audio_source_jobs[7].executor_core = 1u;
    attempts = release_at = unlocks = shape_calls = 0u;
    shape_claimed = fusion_complete = 1u; shape_complete = 0u;
    done = 11u;
}
static uint32_t run(uint32_t soft, uint32_t hard)
{ return br_audio_source_c1_late_preflight(bus, BR_BLOCK, 8u, soft, hard, &done, 12u); }

int main(void)
{
    uint32_t soft = 1000u, hard = soft + FM1_AUDIO_HALF_TICKS;
    setup(soft, hard); release_at = 2u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_CLAIMED);
    assert(attempts == 2u && unlocks == 1u && shape_calls == 1u && !br_audio_source_queue.lock);

    setup(soft, hard); release_at = 2u;
    shape_claimed = 0u; shape_complete = 1u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_COMPLETE);
    assert(attempts == 2u && unlocks == 1u && shape_calls == 2u && !br_audio_source_queue.lock);

    setup(soft, hard); release_at = 2u;
    shape_claimed = 0u; shape_complete = 1u; fusion_complete = 0u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_INVALID);
    assert(attempts == 2u && unlocks == 1u && shape_calls == 2u && !br_audio_source_queue.lock);

    setup(soft, hard); br_audio_source_queue.lock = 0u;
    shape_claimed = shape_complete = 0u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_INVALID);
    assert(attempts == 1u && unlocks == 1u && shape_calls == 2u && !br_audio_source_queue.lock);

    setup(soft, hard); samples[1] = hard - 1u; samples[2] = hard; sample_count = 3u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_TERMINAL);
    assert(attempts == 1u && !unlocks && !shape_calls);

    setup(soft, hard); samples[1] = hard; done = 12u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_TERMINAL);
    assert(attempts == 1u && !unlocks); /* completed, but lock still busy */

    setup(soft, hard); samples[1] = hard; done = 12u; release_at = 1u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_CLAIMED);
    assert(attempts == 1u && unlocks == 1u); /* one final observation */

    setup(soft, hard); /* stopped T4 and permanently busy lock */
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_TERMINAL);
    assert(attempts == BR_AUDIO_C1_PREFLIGHT_SPINS && !unlocks);

    soft = UINT32_MAX - FM1_AUDIO_HALF_TICKS / 2u;
    hard = soft + FM1_AUDIO_HALF_TICKS;
    setup(soft, hard); samples[1] = soft + 1u; samples[2] = soft + 2u;
    sample_count = 3u; release_at = 2u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_CLAIMED && attempts == 2u);
    setup(soft, hard); samples[1] = hard; done = 11u;
    assert(run(soft, hard) == BR_AUDIO_C1_PREFLIGHT_TERMINAL && !attempts);
    setup(soft, hard); assert(run(soft, hard + 1u) == BR_AUDIO_C1_PREFLIGHT_INVALID && !attempts);
    puts("PASS: actual retry loop: busy release, invalid graph, hard cutoff, stalled T4 and wrap");
    return 0;
}
