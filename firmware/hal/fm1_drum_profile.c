/* SPDX-License-Identifier: GPL-3.0-only */
/* Included once by platform.c so T4 and core identity stay in the HAL. */
#include "fm1_drum_profile.h"

#if FM1_DRUM_PROFILE

#include <string.h>

#define FM1_DRUM_PROFILE_CORES 2u
#define FM1_DRUM_PROFILE_LANES 4u
#define FM1_DRUM_PROFILE_VOICES 8u
#define FM1_DRUM_PROFILE_APPEND_WORDS 4u
#define FM1_DRUM_PROFILE_STATS_WORDS 4u
#define FM1_DRUM_PROFILE_DATA_WORDS 168u
#define FM1_DRUM_PROFILE_BANK_WORDS (8u + FM1_DRUM_PROFILE_DATA_WORDS)

typedef struct {
    uint32_t calls;
    uint32_t frames;
    uint32_t sum_ticks;
    uint32_t max_ticks;
} fm1_drum_profile_stat_t;

typedef struct {
    uint32_t valid;
    uint32_t generation;
    uint32_t full_ack_ticks;
    uint32_t callback_ticks;
    uint32_t kit_id;
    uint32_t flags;
    uint32_t reserved[2];
    fm1_drum_profile_stat_t stat[FM1_DRUM_PROFILE_CORES]
        [FM1_DRUM_PROFILE_LANES][FM1_DRUM_PROFILE_STAGE_COUNT];
    uint32_t append[FM1_DRUM_PROFILE_CORES][FM1_DRUM_PROFILE_LANES]
        [FM1_DRUM_PROFILE_APPEND_WORDS];
    uint32_t voice_mask[FM1_DRUM_PROFILE_CORES][FM1_DRUM_PROFILE_LANES];
} fm1_drum_profile_bank_t;

typedef struct {
    fm1_drum_profile_bank_t bank[2];
    uint32_t ready;
    uint32_t frozen;
    uint32_t active_bank;
    uint32_t sealed_bank;
    uint32_t next_generation;
    uint32_t active_generation;
    uint32_t context_lane[FM1_DRUM_PROFILE_CORES];
    uint32_t context_voice[FM1_DRUM_PROFILE_CORES];
    uint32_t context_valid[FM1_DRUM_PROFILE_CORES];
} fm1_drum_profile_store_t;

#if defined(FM1_DEVICE)
static fm1_drum_profile_store_t fm1_drum_profile_store
    __attribute__((section(".pool"), aligned(4)));
#else
static fm1_drum_profile_store_t fm1_drum_profile_store;
#endif

_Static_assert(sizeof(fm1_drum_profile_store_t) <= 5812u,
               "drum profile exceeds fixed .pool headroom");

/* These HAL primitives are visible because this file is included from
 * platform.c after fm1_time.h and the CPU1 identity HAL. */
static uint32_t fm1_drum_profile_core(void)
{
#if defined(FM1_DEVICE)
    return fm1_cpu1_worker_hw_id();
#else
#if defined(FM1_DRUM_PROFILE_HOST_TEST)
    extern _Thread_local uint32_t fm1_drum_profile_test_core;
    return fm1_drum_profile_test_core;
#else
    return 0u;
#endif
#endif
}

static uint32_t fm1_drum_profile_now(void)
{
#if defined(FM1_DRUM_PROFILE_HOST_TEST) && !defined(FM1_DEVICE)
    extern _Thread_local uint32_t fm1_drum_profile_test_tick;
    return ++fm1_drum_profile_test_tick;
#else
    return fm1_ticks();
#endif
}

void fm1_drum_profile_reset(void)
{
    memset(&fm1_drum_profile_store, 0, sizeof fm1_drum_profile_store);
    fm1_drum_profile_store.sealed_bank = 2u;
    fm1_drum_profile_store.ready = 1u;
}

void fm1_drum_profile_half_begin(uint32_t start_tick)
{
    uint32_t bank;
    (void)start_tick;
    if (!fm1_drum_profile_store.ready || fm1_drum_profile_store.frozen) return;
    bank = fm1_drum_profile_store.sealed_bank < 2u
        ? (fm1_drum_profile_store.sealed_bank ^ 1u) : 0u;
    fm1_drum_profile_store.active_bank = bank;
    memset(&fm1_drum_profile_store.bank[bank], 0,
           sizeof fm1_drum_profile_store.bank[bank]);
    ++fm1_drum_profile_store.next_generation;
    if (!fm1_drum_profile_store.next_generation)
        ++fm1_drum_profile_store.next_generation;
    fm1_drum_profile_store.active_generation =
        fm1_drum_profile_store.next_generation;
    fm1_drum_profile_store.context_valid[0] = 0u;
    fm1_drum_profile_store.context_valid[1] = 0u;
}

void fm1_drum_profile_set_context(uint32_t lane, uint32_t voice)
{
    uint32_t core = fm1_drum_profile_core();
    if (core >= FM1_DRUM_PROFILE_CORES || lane >= FM1_DRUM_PROFILE_LANES ||
        voice >= FM1_DRUM_PROFILE_VOICES || !fm1_drum_profile_store.ready ||
        fm1_drum_profile_store.frozen) return;
    fm1_drum_profile_store.context_lane[core] = lane;
    fm1_drum_profile_store.context_voice[core] = voice;
    fm1_drum_profile_store.context_valid[core] = 1u;
    fm1_drum_profile_store.bank[fm1_drum_profile_store.active_bank]
        .voice_mask[core][lane] |= 1u << voice;
}

fm1_drum_profile_scope_t fm1_drum_profile_begin(uint32_t stage,
                                                 uint32_t frames)
{
    fm1_drum_profile_scope_t scope = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
    uint32_t core = fm1_drum_profile_core();
    if (stage >= FM1_DRUM_PROFILE_STAGE_COUNT || core >= FM1_DRUM_PROFILE_CORES ||
        !fm1_drum_profile_store.ready || fm1_drum_profile_store.frozen ||
        !fm1_drum_profile_store.context_valid[core]) return scope;
    scope.start_tick = fm1_drum_profile_now();
    scope.core = core;
    scope.lane = fm1_drum_profile_store.context_lane[core];
    scope.voice = fm1_drum_profile_store.context_voice[core];
    scope.stage = stage;
    scope.frames = frames;
    scope.valid = 1u;
    return scope;
}

void fm1_drum_profile_end(fm1_drum_profile_scope_t scope)
{
    uint32_t elapsed, bank, core, lane, stage;
    fm1_drum_profile_stat_t *stat;
    if (!scope.valid || !fm1_drum_profile_store.ready ||
        fm1_drum_profile_store.frozen) return;
    core = scope.core;
    lane = scope.lane;
    stage = scope.stage;
    if (core >= FM1_DRUM_PROFILE_CORES || lane >= FM1_DRUM_PROFILE_LANES ||
        stage >= FM1_DRUM_PROFILE_STAGE_COUNT) return;
    elapsed = (uint32_t)(fm1_drum_profile_now() - scope.start_tick);
    bank = fm1_drum_profile_store.active_bank;
    stat = &fm1_drum_profile_store.bank[bank].stat[core][lane][stage];
    ++stat->calls;
    stat->frames += scope.frames;
    stat->sum_ticks += elapsed;
    if (elapsed > stat->max_ticks) stat->max_ticks = elapsed;
    if (scope.stage == FM1_DRUM_PROFILE_OUTER_RENDER)
        fm1_drum_profile_store.context_valid[core] = 0u;
}

void fm1_drum_profile_append(uint32_t units, uint32_t output_frames)
{
    uint32_t core = fm1_drum_profile_core();
    uint32_t lane, voice;
    uint32_t *append;
    if (core >= FM1_DRUM_PROFILE_CORES || !fm1_drum_profile_store.ready ||
        fm1_drum_profile_store.frozen ||
        !fm1_drum_profile_store.context_valid[core]) return;
    lane = fm1_drum_profile_store.context_lane[core];
    voice = fm1_drum_profile_store.context_voice[core];
    append = fm1_drum_profile_store.bank[fm1_drum_profile_store.active_bank]
        .append[core][lane];
    ++append[0];
    append[1] += units;
    append[2] += units * 24u;
    append[3] += output_frames;
    fm1_drum_profile_store.bank[fm1_drum_profile_store.active_bank]
        .voice_mask[core][lane] |= 1u << voice;
}

void fm1_drum_profile_half_seal(uint32_t full_ack_ticks,
                                uint32_t callback_ticks)
{
    uint32_t active, sealed;
    fm1_drum_profile_bank_t *bank;
    if (!fm1_drum_profile_store.ready || fm1_drum_profile_store.frozen) return;
    active = fm1_drum_profile_store.active_bank;
    sealed = fm1_drum_profile_store.sealed_bank;
    if (sealed < 2u &&
        full_ack_ticks <= fm1_drum_profile_store.bank[sealed].full_ack_ticks)
        return;
    bank = &fm1_drum_profile_store.bank[active];
    bank->generation = fm1_drum_profile_store.active_generation;
    bank->full_ack_ticks = full_ack_ticks;
    bank->callback_ticks = callback_ticks;
    bank->kit_id = FM1_DRUM_PROFILE_KIT_PLAITS;
    bank->flags = 3u; /* valid + complete, published only after safe join */
    bank->valid = 1u;
    fm1_drum_profile_store.sealed_bank = active;
}

static void fm1_drum_profile_bank_word(const fm1_drum_profile_bank_t *bank,
                                       uint32_t index, uint32_t *out)
{
    uint32_t core, lane, stage, word = 0u;
    if (index < 8u) {
        const uint32_t header[8] = {
            FM1_DRUM_PROFILE_SCHEMA, bank->flags, bank->generation,
            bank->full_ack_ticks, bank->callback_ticks, bank->kit_id,
            bank->valid, FM1_DRUM_PROFILE_PAGE_COUNT
        };
        *out = header[index];
        return;
    }
    index -= 8u;
    if (index < 128u) {
        uint32_t record = index / FM1_DRUM_PROFILE_STATS_WORDS;
        uint32_t field = index % FM1_DRUM_PROFILE_STATS_WORDS;
        stage = record % FM1_DRUM_PROFILE_STAGE_COUNT;
        lane = (record / FM1_DRUM_PROFILE_STAGE_COUNT) % FM1_DRUM_PROFILE_LANES;
        core = record / (FM1_DRUM_PROFILE_STAGE_COUNT * FM1_DRUM_PROFILE_LANES);
        const fm1_drum_profile_stat_t *stat = &bank->stat[core][lane][stage];
        switch (field) {
        case 0u: word = stat->calls; break;
        case 1u: word = stat->frames; break;
        case 2u: word = stat->sum_ticks; break;
        default: word = stat->max_ticks; break;
        }
    } else if (index < 160u) {
        index -= 128u;
        uint32_t record = index / FM1_DRUM_PROFILE_APPEND_WORDS;
        uint32_t field = index % FM1_DRUM_PROFILE_APPEND_WORDS;
        core = record / FM1_DRUM_PROFILE_LANES;
        lane = record % FM1_DRUM_PROFILE_LANES;
        word = bank->append[core][lane][field];
    } else {
        index -= 160u;
        core = index / FM1_DRUM_PROFILE_LANES;
        lane = index % FM1_DRUM_PROFILE_LANES;
        word = bank->voice_mask[core][lane];
    }
    *out = word;
}

uint32_t fm1_drum_profile_read_page(uint32_t page, uint32_t out[8])
{
    uint32_t i, sealed;
    if (!out || !fm1_drum_profile_store.ready ||
        page >= FM1_DRUM_PROFILE_PAGE_COUNT) return 0u;
    /* Payload pages are only coherent after page 0 freezes the selected
     * sealed bank. A rejected out-of-order read must not set that freeze. */
    if (page != 0u && !fm1_drum_profile_store.frozen) return 0u;
    sealed = fm1_drum_profile_store.sealed_bank;
    if (sealed >= 2u || !fm1_drum_profile_store.bank[sealed].valid) return 0u;
    if (page == 0u) fm1_drum_profile_store.frozen = 1u;
    for (i = 0u; i < 8u; ++i) {
        uint32_t index = page == 0u ? i : 8u + (page - 1u) * 8u + i;
        if (index < FM1_DRUM_PROFILE_BANK_WORDS)
            fm1_drum_profile_bank_word(&fm1_drum_profile_store.bank[sealed],
                                       index, &out[i]);
        else out[i] = 0u;
    }
    return 1u;
}

#if defined(FM1_DRUM_PROFILE_HOST_TEST)
/* Let a pthread host test model each firmware core's identity and clock
 * without racing a shared test-only global. Target code does not use TLS. */
_Thread_local uint32_t fm1_drum_profile_test_core;
_Thread_local uint32_t fm1_drum_profile_test_tick;
void fm1_drum_profile_test_set_core(uint32_t core)
{
    fm1_drum_profile_test_core = core;
}
void fm1_drum_profile_test_set_tick(uint32_t tick)
{
    fm1_drum_profile_test_tick = tick;
}
#endif

#endif /* FM1_DRUM_PROFILE */
