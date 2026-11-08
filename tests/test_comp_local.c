/* The block-local compressor step must match br_comp_next_mix_gain exactly:
 * same gain every frame and the same state after storing, for every source,
 * across randomized settings and detector sequences. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static uint32_t rng = 0x1234567u;
static uint32_t next_rand(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

typedef struct {
    int32_t envelope; uint32_t hold, gain, reduction, from, to, phase, valid, reseed;
} snap_t;
static snap_t snap(void)
{
    snap_t s = {br_comp_envelope, br_comp_hold_left, br_comp_gain_q15, br_comp_reduction_q15,
                br_comp_gain_target_from_q15, br_comp_gain_target_to_q15,
                br_comp_gain_target_phase, br_comp_gain_target_valid, br_comp_gain_target_reseed};
    return s;
}
static void put(const snap_t *s)
{
    br_comp_envelope = s->envelope; br_comp_hold_left = s->hold; br_comp_gain_q15 = s->gain;
    br_comp_reduction_q15 = s->reduction; br_comp_gain_target_from_q15 = s->from;
    br_comp_gain_target_to_q15 = s->to; br_comp_gain_target_phase = s->phase;
    br_comp_gain_target_valid = s->valid; br_comp_gain_target_reseed = s->reseed;
}

int main(void)
{
    uint64_t steps = 0;
    for (uint32_t config = 0u; config < 400u; ++config) {
        br_compressor_config_t c = BR_COMP_DEFAULTS;
        c.source = 1u + next_rand() % 8u;
        c.threshold = next_rand() % 128u; c.ratio = next_rand() % 128u;
        c.attack = next_rand() % 128u; c.release = next_rand() % 128u;
        c.hold = next_rand() % 128u; c.makeup = next_rand() % 128u; c.mix = next_rand() % 128u;
        br_comp_snapshot(&c, config + 1u);
        br_comp_reset_detector();
        for (uint32_t block = 0u; block < 6u; ++block) {
            int32_t detectors[BR_BLOCK];
            uint32_t shape = next_rand() % 4u;
            for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
                int32_t d = shape == 0 ? 0 : shape == 1 ? (int32_t)(next_rand() % 140000u) :
                            shape == 2 ? (int32_t)(next_rand() % 40000u) : (i < 64u ? 120000 : 0);
                detectors[i] = d;
            }
            const uint32_t source = br_comp_cached.source;
            const snap_t start = snap();
            uint32_t global_gain[BR_BLOCK];
            for (uint32_t i = 0u; i < BR_BLOCK; ++i)
                global_gain[i] = br_comp_next_mix_gain(detectors[i], source);
            const snap_t after_global = snap();
            put(&start);
            br_comp_local_t local;
            br_comp_local_load(&local);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i, ++steps)
                assert(br_comp_local_next(&local, detectors[i], source) == global_gain[i]);
            br_comp_local_store(&local);
            const snap_t after_local = snap();
            assert(!memcmp(&after_global, &after_local, sizeof after_local));
        }
    }
    /* The rest condition used by the silent finish path is a fixed point. */
    for (uint32_t config = 0u; config < 2000u; ++config) {
        br_compressor_config_t c = BR_COMP_DEFAULTS;
        c.source = 1u + next_rand() % 8u;
        c.threshold = next_rand() % 128u; c.ratio = next_rand() % 128u;
        c.attack = next_rand() % 128u; c.release = next_rand() % 128u;
        c.hold = next_rand() % 128u; c.makeup = next_rand() % 128u; c.mix = next_rand() % 128u;
        br_comp_snapshot(&c, 5000u + config);
        br_comp_envelope = 0; br_comp_hold_left = 0u; br_comp_gain_q15 = 32768u;
        br_comp_reduction_q15 = 0u; br_comp_gain_target_reseed = 0u; br_comp_gain_target_valid = 1u;
        br_comp_gain_target_from_q15 = br_comp_gain_target_to_q15 = 32768u;
        br_comp_gain_target_phase = next_rand() % BR_FX_COMP_IDLE_STEPS;
        assert(br_fx_comp_at_rest(BR_BLOCK));
        const snap_t rest = snap();
        for (uint32_t k = 0u; k < BR_FX_COMP_IDLE_STEPS; ++k)
            (void)br_comp_next_mix_gain(0, br_comp_cached.source);
        const snap_t after = snap();
        assert(!memcmp(&rest, &after, sizeof after));
    }
    printf("compressor block-local step matches the global step over %llu frames\n",
           (unsigned long long)steps);
    return 0;
}
