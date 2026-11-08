/* SPDX-License-Identifier: GPL-3.0-only */
/* Opt-in, bounded Plaits drum timing profile. */
#ifndef FM1_DRUM_PROFILE_H_
#define FM1_DRUM_PROFILE_H_

#include <stdint.h>

#ifndef FM1_DRUM_PROFILE
#define FM1_DRUM_PROFILE 0
#endif

#define FM1_DRUM_PROFILE_SCHEMA 1u
#define FM1_DRUM_PROFILE_PAGE_WORDS 8u
#define FM1_DRUM_PROFILE_PAGE_COUNT 22u
#define FM1_DRUM_PROFILE_KIT_PLAITS 76u

/* 0x8F takes one page byte and returns eight little-endian u32s. There are
 * 22 total pages indexed 0..21. Page 0 is
 * [schema, flags, generation, full-ACK ticks, callback ticks, kit id,
 * valid, page count]. Pages 1..21 contain 168 data words: for core 0..1,
 * lane 0..3, stage 0..3, each [calls, frames, sum ticks, maximum ticks];
 * then per core/lane [append calls, appended 24-frame units, source frames,
 * output frames]; finally per core/lane voice-id bitmasks. Stage totals are
 * inclusive and must not be added together. `MAIN`/`AUX` bracket the engine
 * generators, while `OUTER_RENDER` includes setup, both generators, FIFO,
 * adapter and lane mixing. All timing values are 24 MHz T4 ticks. Page zero
 * freezes the last safely joined worst full-half record until quiescent reset.
 * Flags bits 0 and 1 are valid and complete; both are set only when platform
 * confirms the CPU1 join-valid marker and all expected output-block commits.
 */

typedef enum {
    FM1_DRUM_PROFILE_TRIGGER_SETUP = 0u,
    FM1_DRUM_PROFILE_MAIN = 1u,
    FM1_DRUM_PROFILE_AUX = 2u,
    FM1_DRUM_PROFILE_OUTER_RENDER = 3u,
    FM1_DRUM_PROFILE_STAGE_COUNT = 4u
} fm1_drum_profile_stage_t;

#ifdef __cplusplus
extern "C" {
#endif

#if FM1_DRUM_PROFILE
typedef struct {
    uint32_t start_tick;
    uint32_t core;
    uint32_t lane;
    uint32_t voice;
    uint32_t stage;
    uint32_t frames;
    uint32_t valid;
} fm1_drum_profile_scope_t;

void fm1_drum_profile_reset(void);
void fm1_drum_profile_half_begin(uint32_t start_tick);
void fm1_drum_profile_half_seal(uint32_t full_ack_ticks,
                                uint32_t callback_ticks);
void fm1_drum_profile_set_context(uint32_t lane, uint32_t voice);
fm1_drum_profile_scope_t fm1_drum_profile_begin(uint32_t stage,
                                                 uint32_t frames);
void fm1_drum_profile_end(fm1_drum_profile_scope_t scope);
void fm1_drum_profile_append(uint32_t units, uint32_t output_frames);
uint32_t fm1_drum_profile_read_page(uint32_t page, uint32_t out[8]);

#if defined(FM1_DRUM_PROFILE_HOST_TEST)
void fm1_drum_profile_test_set_core(uint32_t core);
void fm1_drum_profile_test_set_tick(uint32_t tick);
#endif
#else
typedef struct {
    uint32_t unused;
} fm1_drum_profile_scope_t;
static inline void fm1_drum_profile_reset(void) {}
static inline void fm1_drum_profile_half_begin(uint32_t start_tick)
{ (void)start_tick; }
static inline void fm1_drum_profile_half_seal(uint32_t ack, uint32_t callback)
{ (void)ack; (void)callback; }
static inline void fm1_drum_profile_set_context(uint32_t lane, uint32_t voice)
{ (void)lane; (void)voice; }
static inline fm1_drum_profile_scope_t fm1_drum_profile_begin(uint32_t stage,
                                                               uint32_t frames)
{
    (void)stage; (void)frames;
    fm1_drum_profile_scope_t scope = {0u};
    return scope;
}
static inline void fm1_drum_profile_end(fm1_drum_profile_scope_t scope)
{ (void)scope; }
static inline void fm1_drum_profile_append(uint32_t units, uint32_t frames)
{ (void)units; (void)frames; }
static inline uint32_t fm1_drum_profile_read_page(uint32_t page, uint32_t out[8])
{ (void)page; (void)out; return 0u; }
#endif

#ifdef __cplusplus
}
#endif

#endif
