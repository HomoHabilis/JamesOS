/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_AUDIO_FIXED8_PLAN_H_
#define BR_AUDIO_FIXED8_PLAN_H_

#include <stdint.h>

#ifndef BR_AUDIO_FIXED8_SOLVER_RAM
#define BR_AUDIO_FIXED8_SOLVER_RAM 1
#endif
#if BR_AUDIO_FIXED8_SOLVER_RAM != 0 && BR_AUDIO_FIXED8_SOLVER_RAM != 1
#error "BR_AUDIO_FIXED8_SOLVER_RAM must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_SOLVER_RAM && defined(FM1_DEVICE)
#define BR_AUDIO_FIXED8_SOLVER_CODE_ATTR \
    __attribute__((section(".ram_audio.fx"), noinline, used))
#define BR_AUDIO_FIXED8_SOLVER_INLINE static inline \
    __attribute__((always_inline))
#else
#define BR_AUDIO_FIXED8_SOLVER_CODE_ATTR
#define BR_AUDIO_FIXED8_SOLVER_INLINE static
#endif

#ifndef BR_AUDIO_FIXED8_SEPARATE_SOLVER
#define BR_AUDIO_FIXED8_SEPARATE_SOLVER 1
#endif

#ifndef BR_AUDIO_FIXED8_SOLVER_SHORTCUT
#define BR_AUDIO_FIXED8_SOLVER_SHORTCUT 1
#endif
#ifndef BR_AUDIO_FIXED8_TABLE_SOLVER
#define BR_AUDIO_FIXED8_TABLE_SOLVER 1
#endif
#ifndef BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH
#define BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH 1
#endif
#ifndef BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
#define BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST 0
#endif
#ifndef BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY
#define BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY 1
#endif
#ifndef BR_AUDIO_FIXED8_HISTORY_KEY_HOIST
#define BR_AUDIO_FIXED8_HISTORY_KEY_HOIST 1
#endif
#ifndef BR_AUDIO_FIXED8_PARETO_SEARCH
#define BR_AUDIO_FIXED8_PARETO_SEARCH 1
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH != 0 && BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH != 1
#error "BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST != 0 && BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST != 1
#error "BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY != 0 && BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY != 1
#error "BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_HISTORY_KEY_HOIST != 0 && BR_AUDIO_FIXED8_HISTORY_KEY_HOIST != 1
#error "BR_AUDIO_FIXED8_HISTORY_KEY_HOIST must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_PARETO_SEARCH != 0 && BR_AUDIO_FIXED8_PARETO_SEARCH != 1
#error "BR_AUDIO_FIXED8_PARETO_SEARCH must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_PARETO_SEARCH && \
    (!BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH || \
     !BR_AUDIO_FIXED8_TABLE_SOLVER || !BR_AUDIO_FIXED8_SOLVER_SHORTCUT)
#error "BR_AUDIO_FIXED8_PARETO_SEARCH requires the bounded table/shortcut solver"
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST && !BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH
#error "BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST requires the bounded fast path"
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY && !BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH
#error "BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY requires the bounded fast path"
#endif
#if BR_AUDIO_FIXED8_SOLVER_SHORTCUT != 0 && BR_AUDIO_FIXED8_SOLVER_SHORTCUT != 1
#error "BR_AUDIO_FIXED8_SOLVER_SHORTCUT must be 0 or 1"
#endif
#if BR_AUDIO_FIXED8_TABLE_SOLVER != 0 && BR_AUDIO_FIXED8_TABLE_SOLVER != 1
#error "BR_AUDIO_FIXED8_TABLE_SOLVER must be 0 or 1"
#endif

BR_AUDIO_FIXED8_SOLVER_INLINE uint32_t br_audio_fixed8_plan_add(uint32_t a, uint32_t b,
                                         uint32_t *sum)
{
    if (!sum || a > UINT32_MAX - b) return 0u;
    *sum = a + b;
    return 1u;
}

/* Solve the fixed eight-job fusion graph. Jobs 0..3 are independent kit
 * lanes, jobs 4..6 are independent fused tracks, and the finalizer depends on
 * all four lanes. Costs are per-job, per-core T4 estimates. Lanes run before
 * any merge; a subset of tracks can run before the merge to use time while
 * the other core finishes its lanes. */
#if BR_AUDIO_FIXED8_SEPARATE_SOLVER && !defined(BR_AUDIO_FIXED8_SOLVER_IMPLEMENTATION)
uint32_t br_audio_fixed8_plan_solve(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2],
    uint8_t core_for_job[8], uint8_t order_for_job[8],
    uint8_t count_for_core[2], uint32_t *makespan_out);
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
uint32_t br_audio_fixed8_plan_test_reference(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2],
    uint8_t core_for_job[8], uint8_t order_for_job[8],
    uint8_t count_for_core[2], uint32_t *makespan_out);
#endif
#else
#if BR_AUDIO_FIXED8_SEPARATE_SOLVER
#define BR_AUDIO_FIXED8_LINKAGE
#else
#define BR_AUDIO_FIXED8_LINKAGE static
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH
#define BR_AUDIO_FIXED8_BASE_LINKAGE static
#define BR_AUDIO_FIXED8_BASE_NAME br_audio_fixed8_plan_solve_checked
#else
#define BR_AUDIO_FIXED8_BASE_LINKAGE BR_AUDIO_FIXED8_LINKAGE
#define BR_AUDIO_FIXED8_BASE_NAME br_audio_fixed8_plan_solve
#endif
#if !BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY || BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
BR_AUDIO_FIXED8_BASE_LINKAGE uint32_t BR_AUDIO_FIXED8_BASE_NAME(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2],
    uint8_t core_for_job[8], uint8_t order_for_job[8],
    uint8_t count_for_core[2], uint32_t *makespan_out)
{
    uint32_t subset_cost[2][8] = {{0u}};
#if BR_AUDIO_FIXED8_TABLE_SOLVER && BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    uint32_t lane_subset_cost[16][2] = {{0u}};
#endif
    uint32_t best = UINT32_MAX;
    uint32_t best_mask = 0u, best_finalizer_core = 0u, best_pre_mask = 0u;
    uint32_t mask, job, core, subset, found = 0u;
    uint32_t symmetric = 0u;

    if (!independent_cost || !finalizer_cost || !core_for_job ||
        !order_for_job || !count_for_core || !makespan_out) return 0u;
    /* Equal core costs make each mask and switched finalizer core mirror its
     * complement. The lower half contains the earlier representative, so the
     * original ascending mask/core tie order is preserved. */
    symmetric = finalizer_cost[0] == finalizer_cost[1];
    for (job = 0u; job < 7u; ++job) {
        if (independent_cost[job][0] != independent_cost[job][1])
            symmetric = 0u;
        if (!independent_cost[job][0] ||
            independent_cost[job][0] == UINT32_MAX ||
            !independent_cost[job][1] ||
            independent_cost[job][1] == UINT32_MAX) return 0u;
    }
    if (!finalizer_cost[0] || finalizer_cost[0] == UINT32_MAX ||
        !finalizer_cost[1] || finalizer_cost[1] == UINT32_MAX) return 0u;

    /* Precompute all three-track subset costs once per core. */
    for (core = 0u; core < 2u; ++core) {
        for (subset = 1u; subset < 8u; ++subset) {
            uint32_t bit = subset & (0u - subset);
            uint32_t track = bit == 1u ? 0u : bit == 2u ? 1u : 2u;
            uint32_t prior = subset ^ bit;
            if (!br_audio_fixed8_plan_add(subset_cost[core][prior],
                    independent_cost[4u + track][core],
                    &subset_cost[core][subset]))
                subset_cost[core][subset] = UINT32_MAX;
        }
    }

    /* Each bit selects the core-0 owner of one independent job. Enumerate all
     * 128 partitions, then reject any finalizer placement that leaves a core
     * with no assigned job. */
#if BR_AUDIO_FIXED8_TABLE_SOLVER && BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    /* Precompute each checked four-lane subset once. The 128 fixed owner
     * masks below can then load lane and track totals directly instead of
     * walking all seven jobs for every candidate. */
    for (subset = 1u; subset < 16u; ++subset) {
        const uint32_t bit = subset & (0u - subset);
        const uint32_t lane = bit == 1u ? 0u : bit == 2u ? 1u :
            bit == 4u ? 2u : 3u;
        const uint32_t prior = subset ^ bit;
        for (core = 0u; core < 2u; ++core) {
            if (!br_audio_fixed8_plan_add(lane_subset_cost[prior][core],
                    independent_cost[lane][core],
                    &lane_subset_cost[subset][core]))
                lane_subset_cost[subset][core] = UINT32_MAX;
        }
    }
    for (mask = 0u; mask < (symmetric ? 64u : 128u); ++mask) {
        const uint32_t lane_owner_mask = mask & 0x0fu;
        const uint32_t track_owner_mask = (mask >> 4) & 0x07u;
        uint32_t lane_load[2], track_load[2];
        uint32_t lane_ready;

        lane_load[0] = lane_subset_cost[lane_owner_mask][0];
        lane_load[1] = lane_subset_cost[(~lane_owner_mask) & 0x0fu][1];
        track_load[0] = subset_cost[0][track_owner_mask];
        track_load[1] = subset_cost[1][(~track_owner_mask) & 0x07u];
        if (lane_load[0] == UINT32_MAX || lane_load[1] == UINT32_MAX ||
            track_load[0] == UINT32_MAX || track_load[1] == UINT32_MAX)
            continue;
        lane_ready = lane_load[0] > lane_load[1] ?
            lane_load[0] : lane_load[1];

        for (core = 0u; core < 2u; ++core) {
            const uint32_t other = core ^ 1u;
            uint32_t remote_finish, owner_tracks, owner_start, owner_finish;
            uint32_t makespan;

            /* The non-finalizer core must own at least one independent job.
             * These are exactly the two all-jobs-on-one-core masks. */
            if ((other == 0u && mask == 0u) ||
                (other == 1u && mask == 127u)) continue;
            if (!br_audio_fixed8_plan_add(lane_load[other],
                    track_load[other], &remote_finish)) continue;
            if (!br_audio_fixed8_plan_add(lane_load[core],
                    track_load[core], &owner_tracks)) continue;
            owner_start = owner_tracks > lane_ready ?
                owner_tracks : lane_ready;
            if (!br_audio_fixed8_plan_add(owner_start,
                    finalizer_cost[core], &owner_finish)) continue;
            makespan = owner_finish > remote_finish ?
                owner_finish : remote_finish;
            if (makespan < best) {
                best = makespan;
                best_mask = mask;
                best_finalizer_core = core;
                found = 1u;
            }
        }
    }
#else
    for (mask = 0u; mask < (symmetric ? 64u : 128u); ++mask) {
        uint32_t lane_load[2] = {0u, 0u};
        uint32_t track_load[2] = {0u, 0u};
        uint32_t track_mask[2] = {0u, 0u};
        uint32_t independent_count[2] = {0u, 0u};
        uint32_t valid = 1u;

        for (job = 0u; job < 7u; ++job) {
            const uint32_t owner = (mask & (1u << job)) ? 0u : 1u;
            uint32_t *load = job < 4u ? &lane_load[owner] :
                &track_load[owner];
            if (!br_audio_fixed8_plan_add(*load,
                    independent_cost[job][owner], load)) {
                valid = 0u;
                break;
            }
            ++independent_count[owner];
            if (job >= 4u) track_mask[owner] |= 1u << (job - 4u);
        }
        if (!valid) continue;

        for (core = 0u; core < 2u; ++core) {
            const uint32_t other = core ^ 1u;
            uint32_t remote_finish, lane_ready;
            if (!independent_count[other]) continue;
            if (!br_audio_fixed8_plan_add(lane_load[other],
                    track_load[other], &remote_finish)) continue;
            lane_ready = lane_load[0] > lane_load[1] ?
                lane_load[0] : lane_load[1];
#if BR_AUDIO_FIXED8_SOLVER_SHORTCUT
            {
                uint32_t owner_tracks, owner_start, owner_finish, makespan;
                /* For pre-track cost P, the owner finishes at
                 * max(lane_load[core] + P, lane_ready) + F + (T - P).
                 * This decreases until the lanes are ready, then is constant;
                 * assigning all owned tracks before the finalizer reaches that
                 * minimum, max(lane_load + T, lane_ready) + F. If lane_load+T
                 * overflows, no subset can be valid: below the readiness point
                 * lane_ready + T - P is at least lane_load + T; above it the
                 * owner finishes at lane_load + T + F. All costs are positive.
                 */
                if (!br_audio_fixed8_plan_add(lane_load[core],
                        track_load[core], &owner_tracks)) continue;
                owner_start = owner_tracks > lane_ready ?
                    owner_tracks : lane_ready;
                if (!br_audio_fixed8_plan_add(owner_start,
                        finalizer_cost[core], &owner_finish)) continue;
                makespan = owner_finish > remote_finish ?
                    owner_finish : remote_finish;
                if (makespan < best) {
                    best = makespan;
                    best_mask = mask;
                    best_finalizer_core = core;
                    found = 1u;
                }
            }
#else
            for (subset = 0u; subset < 8u; ++subset) {
                uint32_t owner_start, owner_finish, remaining_tracks;
                uint32_t makespan, pre_track_cost;
                if (subset & ~track_mask[core]) continue;
                pre_track_cost = subset_cost[core][subset];
                if (pre_track_cost == UINT32_MAX ||
                    pre_track_cost > track_load[core]) continue;
                if (!br_audio_fixed8_plan_add(lane_load[core],
                        pre_track_cost, &owner_start)) continue;
                if (owner_start < lane_ready) owner_start = lane_ready;
                if (!br_audio_fixed8_plan_add(owner_start,
                        finalizer_cost[core], &owner_finish)) continue;
                remaining_tracks = track_load[core] - pre_track_cost;
                if (!br_audio_fixed8_plan_add(owner_finish, remaining_tracks,
                                              &owner_finish)) continue;
                makespan = owner_finish > remote_finish ?
                    owner_finish : remote_finish;
                if (makespan < best) {
                    best = makespan;
                    best_mask = mask;
                    best_finalizer_core = core;
                    best_pre_mask = subset;
                    found = 1u;
                }
            }
#endif
        }
    }
#endif
    if (!found || best == UINT32_MAX) return 0u;
#if BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    {
        uint32_t lane_load[2] = {0u, 0u};
        uint32_t track_load[2] = {0u, 0u};
        uint32_t track_mask[2] = {0u, 0u};
        uint32_t remote_finish, lane_ready, found_pre_mask = 0u;
        /* Rebuild the winning partition once, then preserve the old subset
         * enumeration/tie-break with the original checked objective. */
        for (job = 0u; job < 7u; ++job) {
            const uint32_t owner = (best_mask & (1u << job)) ? 0u : 1u;
            uint32_t *load = job < 4u ? &lane_load[owner] :
                &track_load[owner];
            if (!br_audio_fixed8_plan_add(*load,
                    independent_cost[job][owner], load)) return 0u;
            if (job >= 4u) track_mask[owner] |= 1u << (job - 4u);
        }
        if (!br_audio_fixed8_plan_add(lane_load[best_finalizer_core ^ 1u],
                track_load[best_finalizer_core ^ 1u], &remote_finish))
            return 0u;
        lane_ready = lane_load[0] > lane_load[1] ?
            lane_load[0] : lane_load[1];
        for (subset = 0u; subset < 8u; ++subset) {
            uint32_t pre_track_cost, owner_start, owner_finish;
            uint32_t remaining_tracks, makespan;
            if (subset & ~track_mask[best_finalizer_core]) continue;
            pre_track_cost = subset_cost[best_finalizer_core][subset];
            if (pre_track_cost == UINT32_MAX ||
                pre_track_cost > track_load[best_finalizer_core]) continue;
            if (!br_audio_fixed8_plan_add(lane_load[best_finalizer_core],
                    pre_track_cost, &owner_start)) continue;
            if (owner_start < lane_ready) owner_start = lane_ready;
            if (!br_audio_fixed8_plan_add(owner_start,
                    finalizer_cost[best_finalizer_core], &owner_finish)) continue;
            remaining_tracks = track_load[best_finalizer_core] - pre_track_cost;
            if (!br_audio_fixed8_plan_add(owner_finish, remaining_tracks,
                                          &owner_finish)) continue;
            makespan = owner_finish > remote_finish ?
                owner_finish : remote_finish;
            if (makespan == best) {
                best_pre_mask = subset;
                found_pre_mask = 1u;
                break;
            }
        }
        if (!found_pre_mask) return 0u;
    }
#endif

    {
        uint8_t next[2] = {0u, 0u};
        uint32_t index;
        for (index = 0u; index < 8u; ++index) {
            core_for_job[index] = UINT8_MAX;
            order_for_job[index] = UINT8_MAX;
        }
        for (core = 0u; core < 2u; ++core) {
            for (job = 0u; job < 4u; ++job) {
                const uint32_t owner = (best_mask & (1u << job)) ? 0u : 1u;
                if (owner == core) {
                    core_for_job[job] = (uint8_t)core;
                    order_for_job[job] = next[core]++;
                }
            }
            for (job = 0u; job < 3u; ++job) {
                const uint32_t owner = (best_mask & (1u << (4u + job))) ?
                    0u : 1u;
                if (core == best_finalizer_core && owner == core &&
                    (best_pre_mask & (1u << job))) {
                    core_for_job[4u + job] = (uint8_t)core;
                    order_for_job[4u + job] = next[core]++;
                }
            }
            if (core == best_finalizer_core) {
                core_for_job[7] = (uint8_t)core;
                order_for_job[7] = next[core]++;
            }
            for (job = 0u; job < 3u; ++job) {
                const uint32_t owner = (best_mask & (1u << (4u + job))) ?
                    0u : 1u;
                if (owner == core && (core != best_finalizer_core ||
                    !(best_pre_mask & (1u << job)))) {
                    core_for_job[4u + job] = (uint8_t)core;
                    order_for_job[4u + job] = next[core]++;
                }
            }
        }
        count_for_core[0] = next[0];
        count_for_core[1] = next[1];
        if (next[0] + next[1] != 8u) return 0u;
        *makespan_out = best;
    }
    return 1u;
}
#endif /* checked reference/fallback solver */

#undef BR_AUDIO_FIXED8_BASE_NAME
#undef BR_AUDIO_FIXED8_BASE_LINKAGE

#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
static uint32_t br_audio_fixed8_plan_test_fast_calls;
static uint32_t br_audio_fixed8_plan_test_fallback_calls;
#if !BR_AUDIO_FIXED8_SEPARATE_SOLVER
static void br_audio_fixed8_plan_test_reset_stats(void)
{
    br_audio_fixed8_plan_test_fast_calls = 0u;
    br_audio_fixed8_plan_test_fallback_calls = 0u;
}
#endif
#endif

/* Checked additions inside each of the 252 candidate placements are
 * unnecessary when these input bounds prove every subset and critical-path
 * sum representable. Cross-core lane_ready needs its own check: it can exceed
 * the finalizer owner's lane total. */
BR_AUDIO_FIXED8_SOLVER_INLINE uint32_t br_audio_fixed8_plan_bounded_inputs_safe(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2])
{
    uint32_t total[2] = {0u, 0u};
    uint32_t lane_total[2] = {0u, 0u};
    uint32_t i, core, lane_ready, sum;
    if (!independent_cost || !finalizer_cost) return 0u;
    for (i = 0u; i < 7u; ++i) {
        for (core = 0u; core < 2u; ++core) {
            const uint32_t cost = independent_cost[i][core];
            if (!cost || cost == UINT32_MAX ||
                !br_audio_fixed8_plan_add(total[core], cost,
                                          &total[core])) return 0u;
            if (i < 4u &&
                !br_audio_fixed8_plan_add(lane_total[core], cost,
                                          &lane_total[core])) return 0u;
        }
    }
    for (core = 0u; core < 2u; ++core) {
        if (!finalizer_cost[core] || finalizer_cost[core] == UINT32_MAX ||
            !br_audio_fixed8_plan_add(total[core], finalizer_cost[core],
                                      &sum)) return 0u;
    }
    lane_ready = lane_total[0] > lane_total[1] ?
        lane_total[0] : lane_total[1];
    for (core = 0u; core < 2u; ++core)
        if (!br_audio_fixed8_plan_add(lane_ready, finalizer_cost[core],
                                      &sum)) return 0u;
    return 1u;
}

#define BR_AUDIO_FIXED8_ADD_UNCHECKED(a, b, sum) \
    ((*(sum) = (a) + (b)), 1u)
BR_AUDIO_FIXED8_SOLVER_INLINE uint32_t br_audio_fixed8_plan_solve_bounded_impl(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2],
    uint8_t core_for_job[8], uint8_t order_for_job[8],
    uint8_t count_for_core[2], uint32_t *makespan_out)
{
    uint32_t subset_cost[2][8];
#if BR_AUDIO_FIXED8_TABLE_SOLVER && BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    uint32_t lane_subset_cost[16][2];
#endif
    uint32_t best = UINT32_MAX;
    uint32_t best_mask = 0u, best_finalizer_core = 0u, best_pre_mask = 0u;
    uint32_t mask, job, core, subset, found = 0u;
    uint32_t symmetric = 0u;

    /* The public dispatcher has already validated all costs and proven the
     * addition bounds. Keep the output pointer checks local for safe linkage. */
    if (!independent_cost || !finalizer_cost || !core_for_job ||
        !order_for_job || !count_for_core || !makespan_out) return 0u;
    /* Equal core costs make each mask and switched finalizer core mirror its
     * complement. The lower half contains the earlier representative, so the
     * original ascending mask/core tie order is preserved. */
    symmetric = finalizer_cost[0] == finalizer_cost[1];
    for (job = 0u; job < 7u; ++job)
        if (independent_cost[job][0] != independent_cost[job][1])
            symmetric = 0u;

    /* Only the zero-subset bases are read before their descendants are
     * filled below. Initialize them explicitly to keep this bounded solver
     * free of compiler-emitted memset calls when placed in audio RAM. */
    subset_cost[0][0] = 0u;
    subset_cost[1][0] = 0u;
#if BR_AUDIO_FIXED8_TABLE_SOLVER && BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    lane_subset_cost[0][0] = 0u;
    lane_subset_cost[0][1] = 0u;
#endif

    /* Precompute all three-track subset costs once per core. */
    for (core = 0u; core < 2u; ++core) {
        for (subset = 1u; subset < 8u; ++subset) {
            uint32_t bit = subset & (0u - subset);
            uint32_t track = bit == 1u ? 0u : bit == 2u ? 1u : 2u;
            uint32_t prior = subset ^ bit;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(subset_cost[core][prior],
                    independent_cost[4u + track][core],
                    &subset_cost[core][subset]))
                subset_cost[core][subset] = UINT32_MAX;
        }
    }

    /* Each bit selects the core-0 owner of one independent job. Enumerate all
     * 128 partitions, then reject any finalizer placement that leaves a core
     * with no assigned job. */
#if BR_AUDIO_FIXED8_TABLE_SOLVER && BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    /* Precompute each checked four-lane subset once. The 128 fixed owner
     * masks below can then load lane and track totals directly instead of
     * walking all seven jobs for every candidate. */
    for (subset = 1u; subset < 16u; ++subset) {
        const uint32_t bit = subset & (0u - subset);
        const uint32_t lane = bit == 1u ? 0u : bit == 2u ? 1u :
            bit == 4u ? 2u : 3u;
        const uint32_t prior = subset ^ bit;
        for (core = 0u; core < 2u; ++core) {
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_subset_cost[prior][core],
                    independent_cost[lane][core],
                    &lane_subset_cost[subset][core]))
                lane_subset_cost[subset][core] = UINT32_MAX;
        }
    }
#if BR_AUDIO_FIXED8_PARETO_SEARCH
    {
        uint8_t frontier[8];
        uint32_t frontier0[8], frontier1[8];
        uint32_t frontier_count = 0u;
        uint32_t minimum_track1 = UINT32_MAX;

        /* Sort track assignments by their core-0 load, then core-1 load.
         * Keep only points with a strictly decreasing core-1 load. */
        for (subset = 0u; subset < 8u; ++subset)
            frontier[subset] = (uint8_t)subset;
        for (subset = 1u; subset < 8u; ++subset) {
            const uint32_t key = frontier[subset];
            const uint32_t key_other = (~key) & 0x07u;
            uint32_t position = subset;
            while (position > 0u) {
                const uint32_t prior = frontier[position - 1u];
                const uint32_t prior_other = (~prior) & 0x07u;
                const uint32_t key_x = subset_cost[0][key];
                const uint32_t prior_x = subset_cost[0][prior];
                const uint32_t key_y = subset_cost[1][key_other];
                const uint32_t prior_y = subset_cost[1][prior_other];
                if (prior_x < key_x ||
                    (prior_x == key_x && prior_y <= key_y)) break;
                frontier[position] = (uint8_t)prior;
                --position;
            }
            frontier[position] = (uint8_t)key;
        }
        for (subset = 0u; subset < 8u; ++subset) {
            const uint32_t track0_mask = frontier[subset];
            const uint32_t track1_mask = (~track0_mask) & 0x07u;
            const uint32_t track1 = subset_cost[1][track1_mask];
            if (track1 < minimum_track1) {
                frontier[frontier_count] = (uint8_t)track0_mask;
                frontier0[frontier_count] = subset_cost[0][track0_mask];
                frontier1[frontier_count] = track1;
                ++frontier_count;
                minimum_track1 = track1;
            }
        }

        /* For each lane ownership and finalizer core, the score is
         * max(C, A+T0, B+T1). Along the frontier T0 rises and T1 falls,
         * so the optimum is at the first crossing or its predecessor.
         * Neighbor checks also skip the sole illegal all-jobs-on-finalizer
         * endpoint when that endpoint straddles the crossing. */
        (void)symmetric;
        for (mask = 0u; mask < 16u; ++mask) {
            const uint32_t lane0_mask = mask;
            const uint32_t lane1_mask = (~mask) & 0x0fu;
            uint32_t lane_load[2];
            uint32_t lane_ready;
            lane_load[0] = lane_subset_cost[lane0_mask][0];
            lane_load[1] = lane_subset_cost[lane1_mask][1];
            lane_ready = lane_load[0] > lane_load[1] ?
                lane_load[0] : lane_load[1];

            for (core = 0u; core < 2u; ++core) {
                const uint32_t other = core ^ 1u;
                const uint32_t a_base = lane_load[0] +
                    (core == 0u ? finalizer_cost[0] : 0u);
                const uint32_t b_base = lane_load[1] +
                    (core == 1u ? finalizer_cost[1] : 0u);
                const uint32_t floor = lane_ready + finalizer_cost[core];
                uint32_t low = 0u, high = frontier_count;
                uint32_t candidate0, candidate1, pair;

                while (low < high) {
                    const uint32_t middle = low + (high - low) / 2u;
                    const uint32_t left = a_base + frontier0[middle];
                    const uint32_t right = b_base + frontier1[middle];
                    if (left >= right) high = middle;
                    else low = middle + 1u;
                }

                if (low == 0u) {
                    candidate0 = 0u;
                    candidate1 = 1u;
                } else if (low == frontier_count) {
                    candidate0 = frontier_count - 1u;
                    candidate1 = frontier_count - 2u;
                } else {
                    candidate0 = low;
                    candidate1 = low - 1u;
                }
                for (pair = 0u; pair < 2u; ++pair) {
                    const uint32_t point = pair == 0u ? candidate0 : candidate1;
                    const uint32_t track0_mask = frontier[point];
                    const uint32_t track1_mask = (~track0_mask) & 0x07u;
                    const uint32_t other_has_lane = other == 0u ?
                        lane0_mask != 0u : lane1_mask != 0u;
                    const uint32_t other_has_track = other == 0u ?
                        track0_mask != 0u : track1_mask != 0u;
                    uint32_t owner_finish, remote_finish, makespan;

                    if (!other_has_lane && !other_has_track) continue;
                    if (core == 0u) {
                        owner_finish = a_base + frontier0[point];
                        remote_finish = b_base + frontier1[point];
                    } else {
                        owner_finish = b_base + frontier1[point];
                        remote_finish = a_base + frontier0[point];
                    }
                    makespan = owner_finish > remote_finish ?
                        owner_finish : remote_finish;
                    if (floor > makespan) makespan = floor;
                    if (makespan < best) {
                        best = makespan;
                        best_mask = lane0_mask | (track0_mask << 4);
                        best_finalizer_core = core;
                        found = 1u;
                    }
                }
            }
        }
    }
#else
    for (mask = 0u; mask < (symmetric ? 64u : 128u); ++mask) {
        const uint32_t lane_owner_mask = mask & 0x0fu;
        const uint32_t track_owner_mask = (mask >> 4) & 0x07u;
        uint32_t lane_load[2], track_load[2];
        uint32_t lane_ready;

        lane_load[0] = lane_subset_cost[lane_owner_mask][0];
        lane_load[1] = lane_subset_cost[(~lane_owner_mask) & 0x0fu][1];
        track_load[0] = subset_cost[0][track_owner_mask];
        track_load[1] = subset_cost[1][(~track_owner_mask) & 0x07u];
        if (lane_load[0] == UINT32_MAX || lane_load[1] == UINT32_MAX ||
            track_load[0] == UINT32_MAX || track_load[1] == UINT32_MAX)
            continue;
        lane_ready = lane_load[0] > lane_load[1] ?
            lane_load[0] : lane_load[1];

        for (core = 0u; core < 2u; ++core) {
            const uint32_t other = core ^ 1u;
            uint32_t remote_finish, owner_tracks, owner_start, owner_finish;
            uint32_t makespan;

            /* The non-finalizer core must own at least one independent job.
             * These are exactly the two all-jobs-on-one-core masks. */
            if ((other == 0u && mask == 0u) ||
                (other == 1u && mask == 127u)) continue;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[other],
                    track_load[other], &remote_finish)) continue;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[core],
                    track_load[core], &owner_tracks)) continue;
            owner_start = owner_tracks > lane_ready ?
                owner_tracks : lane_ready;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(owner_start,
                    finalizer_cost[core], &owner_finish)) continue;
            makespan = owner_finish > remote_finish ?
                owner_finish : remote_finish;
            if (makespan < best) {
                best = makespan;
                best_mask = mask;
                best_finalizer_core = core;
                found = 1u;
            }
        }
    }
#endif /* BR_AUDIO_FIXED8_PARETO_SEARCH */
#else
    for (mask = 0u; mask < (symmetric ? 64u : 128u); ++mask) {
        uint32_t lane_load[2] = {0u, 0u};
        uint32_t track_load[2] = {0u, 0u};
        uint32_t track_mask[2] = {0u, 0u};
        uint32_t independent_count[2] = {0u, 0u};
        uint32_t valid = 1u;

        for (job = 0u; job < 7u; ++job) {
            const uint32_t owner = (mask & (1u << job)) ? 0u : 1u;
            uint32_t *load = job < 4u ? &lane_load[owner] :
                &track_load[owner];
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(*load,
                    independent_cost[job][owner], load)) {
                valid = 0u;
                break;
            }
            ++independent_count[owner];
            if (job >= 4u) track_mask[owner] |= 1u << (job - 4u);
        }
        if (!valid) continue;

        for (core = 0u; core < 2u; ++core) {
            const uint32_t other = core ^ 1u;
            uint32_t remote_finish, lane_ready;
            if (!independent_count[other]) continue;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[other],
                    track_load[other], &remote_finish)) continue;
            lane_ready = lane_load[0] > lane_load[1] ?
                lane_load[0] : lane_load[1];
#if BR_AUDIO_FIXED8_SOLVER_SHORTCUT
            {
                uint32_t owner_tracks, owner_start, owner_finish, makespan;
                /* For pre-track cost P, the owner finishes at
                 * max(lane_load[core] + P, lane_ready) + F + (T - P).
                 * This decreases until the lanes are ready, then is constant;
                 * assigning all owned tracks before the finalizer reaches that
                 * minimum, max(lane_load + T, lane_ready) + F. If lane_load+T
                 * overflows, no subset can be valid: below the readiness point
                 * lane_ready + T - P is at least lane_load + T; above it the
                 * owner finishes at lane_load + T + F. All costs are positive.
                 */
                if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[core],
                        track_load[core], &owner_tracks)) continue;
                owner_start = owner_tracks > lane_ready ?
                    owner_tracks : lane_ready;
                if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(owner_start,
                        finalizer_cost[core], &owner_finish)) continue;
                makespan = owner_finish > remote_finish ?
                    owner_finish : remote_finish;
                if (makespan < best) {
                    best = makespan;
                    best_mask = mask;
                    best_finalizer_core = core;
                    found = 1u;
                }
            }
#else
            for (subset = 0u; subset < 8u; ++subset) {
                uint32_t owner_start, owner_finish, remaining_tracks;
                uint32_t makespan, pre_track_cost;
                if (subset & ~track_mask[core]) continue;
                pre_track_cost = subset_cost[core][subset];
                if (pre_track_cost == UINT32_MAX ||
                    pre_track_cost > track_load[core]) continue;
                if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[core],
                        pre_track_cost, &owner_start)) continue;
                if (owner_start < lane_ready) owner_start = lane_ready;
                if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(owner_start,
                        finalizer_cost[core], &owner_finish)) continue;
                remaining_tracks = track_load[core] - pre_track_cost;
                if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(owner_finish, remaining_tracks,
                                              &owner_finish)) continue;
                makespan = owner_finish > remote_finish ?
                    owner_finish : remote_finish;
                if (makespan < best) {
                    best = makespan;
                    best_mask = mask;
                    best_finalizer_core = core;
                    best_pre_mask = subset;
                    found = 1u;
                }
            }
#endif
        }
    }
#endif
    if (!found || best == UINT32_MAX) return 0u;
#if BR_AUDIO_FIXED8_SOLVER_SHORTCUT
    {
        uint32_t lane_load[2] = {0u, 0u};
        uint32_t track_load[2] = {0u, 0u};
        uint32_t track_mask[2] = {0u, 0u};
        uint32_t remote_finish, lane_ready, found_pre_mask = 0u;
        /* Rebuild the winning partition once, then preserve the old subset
         * enumeration/tie-break with the original checked objective. */
        for (job = 0u; job < 7u; ++job) {
            const uint32_t owner = (best_mask & (1u << job)) ? 0u : 1u;
            uint32_t *load = job < 4u ? &lane_load[owner] :
                &track_load[owner];
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(*load,
                    independent_cost[job][owner], load)) return 0u;
            if (job >= 4u) track_mask[owner] |= 1u << (job - 4u);
        }
        if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[best_finalizer_core ^ 1u],
                track_load[best_finalizer_core ^ 1u], &remote_finish))
            return 0u;
        lane_ready = lane_load[0] > lane_load[1] ?
            lane_load[0] : lane_load[1];
        for (subset = 0u; subset < 8u; ++subset) {
            uint32_t pre_track_cost, owner_start, owner_finish;
            uint32_t remaining_tracks, makespan;
            if (subset & ~track_mask[best_finalizer_core]) continue;
            pre_track_cost = subset_cost[best_finalizer_core][subset];
            if (pre_track_cost == UINT32_MAX ||
                pre_track_cost > track_load[best_finalizer_core]) continue;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(lane_load[best_finalizer_core],
                    pre_track_cost, &owner_start)) continue;
            if (owner_start < lane_ready) owner_start = lane_ready;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(owner_start,
                    finalizer_cost[best_finalizer_core], &owner_finish)) continue;
            remaining_tracks = track_load[best_finalizer_core] - pre_track_cost;
            if (!BR_AUDIO_FIXED8_ADD_UNCHECKED(owner_finish, remaining_tracks,
                                          &owner_finish)) continue;
            makespan = owner_finish > remote_finish ?
                owner_finish : remote_finish;
            if (makespan == best) {
                best_pre_mask = subset;
                found_pre_mask = 1u;
                break;
            }
        }
        if (!found_pre_mask) return 0u;
    }
#endif

    {
        uint8_t next[2] = {0u, 0u};
        uint32_t index;
        for (index = 0u; index < 8u; ++index) {
            core_for_job[index] = UINT8_MAX;
            order_for_job[index] = UINT8_MAX;
        }
        for (core = 0u; core < 2u; ++core) {
            for (job = 0u; job < 4u; ++job) {
                const uint32_t owner = (best_mask & (1u << job)) ? 0u : 1u;
                if (owner == core) {
                    core_for_job[job] = (uint8_t)core;
                    order_for_job[job] = next[core]++;
                }
            }
            for (job = 0u; job < 3u; ++job) {
                const uint32_t owner = (best_mask & (1u << (4u + job))) ?
                    0u : 1u;
                if (core == best_finalizer_core && owner == core &&
                    (best_pre_mask & (1u << job))) {
                    core_for_job[4u + job] = (uint8_t)core;
                    order_for_job[4u + job] = next[core]++;
                }
            }
            if (core == best_finalizer_core) {
                core_for_job[7] = (uint8_t)core;
                order_for_job[7] = next[core]++;
            }
            for (job = 0u; job < 3u; ++job) {
                const uint32_t owner = (best_mask & (1u << (4u + job))) ?
                    0u : 1u;
                if (owner == core && (core != best_finalizer_core ||
                    !(best_pre_mask & (1u << job)))) {
                    core_for_job[4u + job] = (uint8_t)core;
                    order_for_job[4u + job] = next[core]++;
                }
            }
        }
        count_for_core[0] = next[0];
        count_for_core[1] = next[1];
        if (next[0] + next[1] != 8u) return 0u;
        *makespan_out = best;
    }
    return 1u;
}

#undef BR_AUDIO_FIXED8_ADD_UNCHECKED

BR_AUDIO_FIXED8_LINKAGE BR_AUDIO_FIXED8_SOLVER_CODE_ATTR uint32_t br_audio_fixed8_plan_solve(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2],
    uint8_t core_for_job[8], uint8_t order_for_job[8],
    uint8_t count_for_core[2], uint32_t *makespan_out)
{
    if (!independent_cost || !finalizer_cost || !core_for_job ||
        !order_for_job || !count_for_core || !makespan_out)
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY && !BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
        return 0u;
#else
        return br_audio_fixed8_plan_solve_checked(independent_cost,
            finalizer_cost, core_for_job, order_for_job, count_for_core,
            makespan_out);
#endif
    if (br_audio_fixed8_plan_bounded_inputs_safe(independent_cost,
                                                 finalizer_cost)) {
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
        ++br_audio_fixed8_plan_test_fast_calls;
#endif
        return br_audio_fixed8_plan_solve_bounded_impl(independent_cost,
            finalizer_cost, core_for_job, order_for_job, count_for_core,
            makespan_out);
    }
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
    ++br_audio_fixed8_plan_test_fallback_calls;
#endif
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_ONLY
    /* Estimates outside the proven arithmetic domain simply decline to add a
     * fixed plan. The caller safely keeps the ordinary ready-queue schedule. */
    return 0u;
#else
    return br_audio_fixed8_plan_solve_checked(independent_cost,
        finalizer_cost, core_for_job, order_for_job, count_for_core,
        makespan_out);
#endif
}
#if BR_AUDIO_FIXED8_SOLVER_BOUNDED_TEST
BR_AUDIO_FIXED8_LINKAGE uint32_t br_audio_fixed8_plan_test_reference(
    const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2],
    uint8_t core_for_job[8], uint8_t order_for_job[8],
    uint8_t count_for_core[2], uint32_t *makespan_out)
{
    return br_audio_fixed8_plan_solve_checked(independent_cost,
        finalizer_cost, core_for_job, order_for_job, count_for_core,
        makespan_out);
}
#endif
#endif /* BR_AUDIO_FIXED8_SOLVER_BOUNDED_FASTPATH */

#undef BR_AUDIO_FIXED8_LINKAGE
#undef BR_AUDIO_FIXED8_SOLVER_CODE_ATTR
#undef BR_AUDIO_FIXED8_SOLVER_INLINE
#endif /* implementation body */

#endif /* BR_AUDIO_FIXED8_PLAN_H_ */
