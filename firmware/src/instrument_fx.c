/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-track fixed-point effects: modulation -> delay -> reverb. */
#ifndef BR_FX_JOB_PROGRESS_ONLY
#if defined(FM1_DEVICE) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
#define BR_FX_JOB_PROGRESS_ONLY 1
#endif
#endif
#include "instrument_fx_progress.h"
#include "instrument_fx_div127.h"
#include "instrument_failure_job_profile.h"
#include "../hal/fm1_memory_sync.h"
#include "audio_level.h"
#include "../product.h"
#ifndef BR_FX_NATIVE_RATE_CALIBRATION
#if defined(FM1_NATIVE_OUTPUT_32K) && FM1_NATIVE_OUTPUT_32K
#define BR_FX_NATIVE_RATE_CALIBRATION 1
#else
#define BR_FX_NATIVE_RATE_CALIBRATION 0
#endif
#endif
#ifndef BR_FX_NATIVE_RATE_48K
#if defined(FM1_NATIVE_OUTPUT_48K) && FM1_NATIVE_OUTPUT_48K
#define BR_FX_NATIVE_RATE_48K 1
#else
#define BR_FX_NATIVE_RATE_48K 0
#endif
#endif
#if BR_FX_NATIVE_RATE_CALIBRATION != 0 && BR_FX_NATIVE_RATE_CALIBRATION != 1
#error "BR_FX_NATIVE_RATE_CALIBRATION must be 0 or 1"
#endif
#if BR_FX_NATIVE_RATE_48K != 0 && BR_FX_NATIVE_RATE_48K != 1
#error "BR_FX_NATIVE_RATE_48K must be 0 or 1"
#endif
#if BR_FX_NATIVE_RATE_CALIBRATION && BR_FX_NATIVE_RATE_48K
#error "Select only one FX native-rate calibration"
#endif
#if BR_FX_NATIVE_RATE_CALIBRATION && \
    (!defined(FS) || FS != 32000u || !FM1_NATIVE_OUTPUT_32K)
#error "Native FX rate calibration is only valid for the 32 kHz output profile"
#endif
#if BR_FX_NATIVE_RATE_48K && \
    (!defined(FS) || FS != 48000u || !FM1_NATIVE_OUTPUT_48K)
#error "Native FX rate calibration is only valid for the 48 kHz output profile"
#endif

/* Targeted native-32 kHz arithmetic experiments. Both preserve the exact
 * integer result and remain off unless a measured profile selects them. */
#ifndef BR_FX_RATE_SIGNED_441_RECIP
#define BR_FX_RATE_SIGNED_441_RECIP 1
#endif
#if BR_FX_RATE_SIGNED_441_RECIP != 0 && BR_FX_RATE_SIGNED_441_RECIP != 1
#error "BR_FX_RATE_SIGNED_441_RECIP must be 0 or 1"
#endif
#if BR_FX_RATE_SIGNED_441_RECIP && \
    !(BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K)
#error "BR_FX_RATE_SIGNED_441_RECIP requires a native calibrated profile"
#endif
#ifndef BR_FX_PHASER_MUL_Q15_APPROX32
#define BR_FX_PHASER_MUL_Q15_APPROX32 1
#endif
#if BR_FX_PHASER_MUL_Q15_APPROX32 != 0 && BR_FX_PHASER_MUL_Q15_APPROX32 != 1
#error "BR_FX_PHASER_MUL_Q15_APPROX32 must be 0 or 1"
#endif
#ifndef BR_FX_FEEDFORWARD_Q15_ASR64
#define BR_FX_FEEDFORWARD_Q15_ASR64 1
#endif
#if BR_FX_FEEDFORWARD_Q15_ASR64 != 0 && BR_FX_FEEDFORWARD_Q15_ASR64 != 1
#error "BR_FX_FEEDFORWARD_Q15_ASR64 must be 0 or 1"
#endif
#ifndef BR_FX_PHASER_MUL_Q15_ASR64
#define BR_FX_PHASER_MUL_Q15_ASR64 0
#endif
#if BR_FX_PHASER_MUL_Q15_ASR64 != 0 && BR_FX_PHASER_MUL_Q15_ASR64 != 1
#error "BR_FX_PHASER_MUL_Q15_ASR64 must be 0 or 1"
#endif
#ifndef BR_FX_MASTER_WOW_PHASE_CACHE
#define BR_FX_MASTER_WOW_PHASE_CACHE 1
#endif
#if BR_FX_MASTER_WOW_PHASE_CACHE != 0 && BR_FX_MASTER_WOW_PHASE_CACHE != 1
#error "BR_FX_MASTER_WOW_PHASE_CACHE must be 0 or 1"
#endif

/* Skip the work of a track's modulation stage at MIX 0 and of its delay stage at send 0.
 * Neither is heard then; only the state a later enable reads is kept. */
#ifndef BR_FX_STAGE_SKIP_IDLE
#define BR_FX_STAGE_SKIP_IDLE 1
#endif
/* The shared-reverb emit step reads block constants once and writes track state once. */
#ifndef BR_FX_SEND_BLOCK_HOIST
#define BR_FX_SEND_BLOCK_HOIST 1
#endif
/* Read MULTITAP and GLITCH at the nearest whole sample of the half-rate line, not between
 * two: one decode instead of two and no interpolation per tap (at most half a line sample
 * of timing, and repeats keep their brightness). DIGITAL keeps its fractional read, whose
 * gentle per-repeat darkening is part of its feedback character; TAPE needs it for wow. */
#ifndef BR_FX_DELAY_INTEGER_READS
#define BR_FX_DELAY_INTEGER_READS 1
#endif
#if BR_FX_DELAY_INTEGER_READS
#define BR_FX_DELAY_GRID_Q8(q8) (((q8) + 128u) & ~255u)
#else
#define BR_FX_DELAY_GRID_Q8(q8) (q8)
#endif
/* Run every delay engine, not only TAPE, through the block loop when its controls are stable. */
#ifndef BR_FX_DELAY_BLOCK_ALL_ENGINES
#define BR_FX_DELAY_BLOCK_ALL_ENGINES 1
#endif
/* One-pole high-pass (about 60 Hz) on the delay feedback path only, so bass
 * does not build up at high feedback. The first echo is not filtered. */
#ifndef BR_FX_DELAY_LOOP_HPF
#define BR_FX_DELAY_LOOP_HPF 1
#endif
#ifndef BR_FX_STAGE_FULL_GAIN_UNITY
#define BR_FX_STAGE_FULL_GAIN_UNITY 1
#endif
#if BR_FX_STAGE_FULL_GAIN_UNITY != 0 && BR_FX_STAGE_FULL_GAIN_UNITY != 1
#error "BR_FX_STAGE_FULL_GAIN_UNITY must be 0 or 1"
#endif
#ifndef BR_FX_STAGE_EMIT_FAST_SHIFTS
#define BR_FX_STAGE_EMIT_FAST_SHIFTS 1
#endif
#ifndef BR_FX_STAGE_EMIT_FAST_DIV3
#define BR_FX_STAGE_EMIT_FAST_DIV3 0
#endif
#ifndef BR_FX_STAGE_EMIT_TAIL_BOUNDARY
#define BR_FX_STAGE_EMIT_TAIL_BOUNDARY 1
#endif
#ifndef BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB
#define BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB 1
#endif
#ifndef BR_FX_STAGE_EMIT_PAN_DIRECT
#define BR_FX_STAGE_EMIT_PAN_DIRECT 1
#endif
#ifndef BR_FX_STAGE_EMIT_ROUTE_COEFFS
#define BR_FX_STAGE_EMIT_ROUTE_COEFFS 0
#endif
#if BR_FX_STAGE_EMIT_FAST_SHIFTS != 0 && BR_FX_STAGE_EMIT_FAST_SHIFTS != 1
#error "BR_FX_STAGE_EMIT_FAST_SHIFTS must be 0 or 1"
#endif
#if BR_FX_STAGE_EMIT_FAST_DIV3 != 0 && BR_FX_STAGE_EMIT_FAST_DIV3 != 1
#error "BR_FX_STAGE_EMIT_FAST_DIV3 must be 0 or 1"
#endif
#if BR_FX_STAGE_EMIT_TAIL_BOUNDARY != 0 && BR_FX_STAGE_EMIT_TAIL_BOUNDARY != 1
#error "BR_FX_STAGE_EMIT_TAIL_BOUNDARY must be 0 or 1"
#endif
#if BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB != 0 && BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB != 1
#error "BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB must be 0 or 1"
#endif
#if BR_FX_STAGE_EMIT_PAN_DIRECT != 0 && BR_FX_STAGE_EMIT_PAN_DIRECT != 1
#error "BR_FX_STAGE_EMIT_PAN_DIRECT must be 0 or 1"
#endif
#if BR_FX_STAGE_EMIT_ROUTE_COEFFS != 0 && BR_FX_STAGE_EMIT_ROUTE_COEFFS != 1
#error "BR_FX_STAGE_EMIT_ROUTE_COEFFS must be 0 or 1"
#endif
#if BR_FX_STAGE_EMIT_TAIL_BOUNDARY && !BR_FX_STAGE_FULL_GAIN_UNITY
#error "the tail-boundary fast path requires the unity endpoint shortcut"
#endif
#ifndef BR_FX_DELAY_RATE2
#define BR_FX_DELAY_RATE2 1
#endif
#ifndef BR_FX_MOD_RATE2
#define BR_FX_MOD_RATE2 1
#endif
#ifndef BR_FX_BLOCK_STAGE_PIPELINE
#define BR_FX_BLOCK_STAGE_PIPELINE 1
#endif
#ifndef BR_FX_STAGED_COMPACT_CALL_ABI
#define BR_FX_STAGED_COMPACT_CALL_ABI 1
#endif
#ifndef BR_FX_SILENT_FINISH
#define BR_FX_SILENT_FINISH 1
#endif
#ifndef BR_FX_STREAM_OUTPUT_SCALE
#define BR_FX_STREAM_OUTPUT_SCALE 1
#endif
#ifndef BR_FX_SKIP_SILENT_TRACK
#define BR_FX_SKIP_SILENT_TRACK 1
#endif
#ifndef BR_FX_BLOCK_STAGE_ONLY_BUILD
#define BR_FX_BLOCK_STAGE_ONLY_BUILD 1
#endif
#ifndef BR_FX_BLOCK_STAGE_SPECIALIZE_MOD
#define BR_FX_BLOCK_STAGE_SPECIALIZE_MOD 1
#endif
#ifndef BR_FX_MOD_STAGE_NOINLINE
#define BR_FX_MOD_STAGE_NOINLINE 1
#endif
#if BR_FX_MOD_STAGE_NOINLINE != 0 && BR_FX_MOD_STAGE_NOINLINE != 1
#error "BR_FX_MOD_STAGE_NOINLINE must be 0 or 1"
#endif
#if BR_FX_MOD_STAGE_NOINLINE
#define BR_FX_MOD_STAGE_NOINLINE_ATTR __attribute__((noinline))
#else
#define BR_FX_MOD_STAGE_NOINLINE_ATTR
#endif
#ifndef BR_FX_BLOCK_STAGE_SPECIALIZE_DELAY
#define BR_FX_BLOCK_STAGE_SPECIALIZE_DELAY 1
#endif
#ifndef BR_FX_TAPE_WOW_DELTA_LUT
#define BR_FX_TAPE_WOW_DELTA_LUT 1
#endif
#if BR_FX_TAPE_WOW_DELTA_LUT != 0 && BR_FX_TAPE_WOW_DELTA_LUT != 1
#error "BR_FX_TAPE_WOW_DELTA_LUT must be 0 or 1"
#endif
#if BR_FX_TAPE_WOW_DELTA_LUT && \
    (!(BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K) || !BR_FX_DELAY_RATE2 || \
     !BR_FX_BLOCK_STAGE_SPECIALIZE_DELAY)
#error "Tape wow delta lookup requires native calibration and the rate-2 Tape stage"
#endif
#ifndef BR_FX_INTERP_DELTA_1MUL
#define BR_FX_INTERP_DELTA_1MUL 1
#endif
#ifndef BR_FX_MASTER_INTERP_DELTA_1MUL
#define BR_FX_MASTER_INTERP_DELTA_1MUL 1
#endif
#ifndef BR_FX_MASTER_COEFF_CACHE
#define BR_FX_MASTER_COEFF_CACHE 1
#endif
/* Exact active-master fast path when all slewed controls match this block. */
/* Skip repeated coefficient writes only after a mid-block control slew has
 * reached its target. Keep controls_stable and Q15 mix gains unchanged: those
 * deliberately remain block-stable, including their original fallback path. */
#ifndef BR_FX_MASTER_MIDBLOCK_SETTLED
#define BR_FX_MASTER_MIDBLOCK_SETTLED 1
#endif
#if BR_FX_MASTER_MIDBLOCK_SETTLED != 0 && BR_FX_MASTER_MIDBLOCK_SETTLED != 1
#error "BR_FX_MASTER_MIDBLOCK_SETTLED must be 0 or 1"
#endif
#ifndef BR_FX_MASTER_STABLE_FASTPATH
#define BR_FX_MASTER_STABLE_FASTPATH 1
#endif
#ifndef BR_FX_MASTER_Q15_MIX_GAIN
#define BR_FX_MASTER_Q15_MIX_GAIN 1
#endif
#ifndef BR_FX_MASTER_Q15_MIX_GAIN_TEST_COUNTERS
#define BR_FX_MASTER_Q15_MIX_GAIN_TEST_COUNTERS 0
#endif
#if BR_FX_MASTER_Q15_MIX_GAIN != 0 && BR_FX_MASTER_Q15_MIX_GAIN != 1
#error "BR_FX_MASTER_Q15_MIX_GAIN must be 0 or 1"
#endif
#if BR_FX_MASTER_Q15_MIX_GAIN && !BR_FX_MASTER_STABLE_FASTPATH
#error "master Q15 mix gain requires the stable-control fast path"
#endif
#if BR_FX_MASTER_STABLE_FASTPATH != 0 && BR_FX_MASTER_STABLE_FASTPATH != 1
#error "BR_FX_MASTER_STABLE_FASTPATH must be 0 or 1"
#endif
#if BR_FX_MASTER_WOW_PHASE_CACHE && !BR_FX_MASTER_STABLE_FASTPATH
#error "BR_FX_MASTER_WOW_PHASE_CACHE requires BR_FX_MASTER_STABLE_FASTPATH"
#endif
#ifndef BR_FX_FINISH_DIRECT_SUM
#define BR_FX_FINISH_DIRECT_SUM 1
#endif
#ifndef BR_FX_FINISH_MASTER_FUSE
#define BR_FX_FINISH_MASTER_FUSE 1
#endif
#ifndef BR_FX_MASTER_FRAME_INLINE_FINISH
#define BR_FX_MASTER_FRAME_INLINE_FINISH 1
#endif
#ifndef BR_FX_FINISH_MASTER_STREAM
#define BR_FX_FINISH_MASTER_STREAM 1
#endif
#if BR_FX_FINISH_MASTER_STREAM != 0 && BR_FX_FINISH_MASTER_STREAM != 1
#error "BR_FX_FINISH_MASTER_STREAM must be 0 or 1"
#endif
#if BR_FX_FINISH_MASTER_STREAM && \
    (!BR_FX_FINISH_MASTER_FUSE || !BR_FX_MASTER_FRAME_INLINE_FINISH || \
     !BR_AUDIO_FUSION_ONLY_BUILD)
#error "finish streaming requires the fused master frame and fusion-only build"
#endif
#if BR_FX_MASTER_FRAME_INLINE_FINISH != 0 && BR_FX_MASTER_FRAME_INLINE_FINISH != 1
#error "BR_FX_MASTER_FRAME_INLINE_FINISH must be 0 or 1"
#endif
#if BR_FX_MASTER_FRAME_INLINE_FINISH && \
    (!BR_FX_FINISH_MASTER_FUSE || !BR_AUDIO_FUSION_ONLY_BUILD)
#error "inline master finish requires fused master processing and fusion-only build"
#endif
#ifndef BR_COMP_FINISH_SUM_FOLD
#define BR_COMP_FINISH_SUM_FOLD 1
#endif
#ifndef BR_FX_PAN_COEFF_CACHE
#define BR_FX_PAN_COEFF_CACHE 1
#endif
#ifndef BR_FX_GLITCH_POWER2_QUANTIZE
#define BR_FX_GLITCH_POWER2_QUANTIZE 1
#endif
#ifndef BR_FX_DIV_POW2_BRANCHLESS
#define BR_FX_DIV_POW2_BRANCHLESS 1
#endif
#ifndef BR_FX_MULTITAP_COMBINED_DIV
#define BR_FX_MULTITAP_COMBINED_DIV 1
#endif
#ifndef BR_FX_DIV3_RECIP
#define BR_FX_DIV3_RECIP 1
#endif
#ifndef BR_FX_DIV5_RECIP
#define BR_FX_DIV5_RECIP 1
#endif
#ifndef BR_FX_MASTER_WOW_DIV_RECIP
#define BR_FX_MASTER_WOW_DIV_RECIP 1
#endif
#ifndef BR_FX_MASTER_WOW_BOUNDED_RECIP
#if defined(FS) && FS == 32000u && BR_FX_NATIVE_RATE_CALIBRATION
#define BR_FX_MASTER_WOW_BOUNDED_RECIP 1
#else
#define BR_FX_MASTER_WOW_BOUNDED_RECIP 0
#endif
#endif
#if BR_FX_MASTER_WOW_BOUNDED_RECIP != 0 && BR_FX_MASTER_WOW_BOUNDED_RECIP != 1
#error "BR_FX_MASTER_WOW_BOUNDED_RECIP must be 0 or 1"
#endif
#if BR_FX_MASTER_WOW_BOUNDED_RECIP && \
    (!BR_FX_NATIVE_RATE_CALIBRATION || !defined(FS) || FS != 32000u || \
     !BR_FX_MASTER_WOW_DIV_RECIP || !BR_FX_RATE_SIGNED_441_RECIP)
#error "bounded wow reciprocal requires native 32 kHz and both exact reciprocal helpers"
#endif
#ifndef BR_FX_TAIL_GAIN_RECIP
#define BR_FX_TAIL_GAIN_RECIP 1
#endif
#ifndef BR_FX_TAIL_GAIN_RECIP_32K
#define BR_FX_TAIL_GAIN_RECIP_32K BR_FX_NATIVE_RATE_CALIBRATION
#endif
#if BR_FX_TAIL_GAIN_RECIP_32K != 0 && BR_FX_TAIL_GAIN_RECIP_32K != 1
#error "BR_FX_TAIL_GAIN_RECIP_32K must be 0 or 1"
#endif
#if BR_FX_TAIL_GAIN_RECIP_32K && !BR_FX_NATIVE_RATE_CALIBRATION
#error "BR_FX_TAIL_GAIN_RECIP_32K requires native 32 kHz rate calibration"
#endif
#if BR_FX_TAIL_GAIN_RECIP_32K && (!defined(FS) || FS != 32000u)
#error "BR_FX_TAIL_GAIN_RECIP_32K is only valid at 32 kHz"
#endif
#ifndef BR_FX_MASTER_TAIL_GAIN_HOIST
#define BR_FX_MASTER_TAIL_GAIN_HOIST 1
#endif
#if BR_FX_MASTER_TAIL_GAIN_HOIST != 0 && BR_FX_MASTER_TAIL_GAIN_HOIST != 1
#error "BR_FX_MASTER_TAIL_GAIN_HOIST must be 0 or 1"
#endif
#ifndef BR_FX_MASTER_WOW_FADE_RECIP_32K
#define BR_FX_MASTER_WOW_FADE_RECIP_32K BR_FX_NATIVE_RATE_CALIBRATION
#endif
#if BR_FX_MASTER_WOW_FADE_RECIP_32K != 0 && BR_FX_MASTER_WOW_FADE_RECIP_32K != 1
#error "BR_FX_MASTER_WOW_FADE_RECIP_32K must be 0 or 1"
#endif
#if BR_FX_MASTER_WOW_FADE_RECIP_32K && \
    (!BR_FX_NATIVE_RATE_CALIBRATION || !defined(FS) || FS != 32000u)
#error "the bounded wow fade reciprocal requires native 32 kHz calibration"
#endif
#ifndef BR_FX_FLOAT_MIX_GAIN
#define BR_FX_FLOAT_MIX_GAIN 1
#endif
#ifndef BR_FX_Q15_MIX_GAIN
#define BR_FX_Q15_MIX_GAIN 1
#endif
#if BR_FX_Q15_MIX_GAIN != 0 && BR_FX_Q15_MIX_GAIN != 1
#error "BR_FX_Q15_MIX_GAIN must be 0 or 1"
#endif
#ifndef BR_FX_DIV10_RECIP
#define BR_FX_DIV10_RECIP 1
#endif
#if BR_FX_BLOCK_STAGE_ONLY_BUILD && !BR_FX_BLOCK_STAGE_PIPELINE
#error "stage-only FX builds require the block-stage pipeline"
#endif
#if BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG && \
    (!BR_FX_BLOCK_STAGE_PIPELINE || !BR_FX_BLOCK_STAGE_ONLY_BUILD)
#error "FX substage timing requires the block-staged, stage-only FX path"
#endif
#if BR_FX_BLOCK_STAGE_PIPELINE
#define BR_FX_TAIL_EVENT_PARAM , int32_t *tail_event
#define BR_FX_TAIL_EVENT_ARG(value) , (value)
#else
#define BR_FX_TAIL_EVENT_PARAM
#define BR_FX_TAIL_EVENT_ARG(value)
#endif

#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
#include "../hal/fm1_cpu1_worker.h"
#endif
#ifndef FM1_AUDIO_FORCE_INLINE
#define FM1_AUDIO_FORCE_INLINE 0
#endif
#if FM1_AUDIO_FORCE_INLINE
#define BR_FX_LEAF static inline __attribute__((always_inline))
#else
#define BR_FX_LEAF static
#endif
#if !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
#define BR_POOL __attribute__((section(".pool")))
#else
#define BR_POOL
#endif

/* Normal rendering may execute from RAM while XIP is available. Flash
 * critical routines remain in their separate, call-free .ram_text section. */
#if defined(FM1_DEVICE) && !defined(FX_FAULT_TEST)
#define BR_FX_RAM_AUDIO __attribute__((section(".ram_audio.fx")))
#else
#define BR_FX_RAM_AUDIO
#endif

#ifndef BR_FX_PREPARE_FLASH
#define BR_FX_PREPARE_FLASH 1
#endif
#if BR_FX_PREPARE_FLASH != 0 && BR_FX_PREPARE_FLASH != 1
#error "BR_FX_PREPARE_FLASH must be 0 or 1"
#endif
#if BR_FX_PREPARE_FLASH
#define BR_FX_PREPARE_TARGET(fn) \
    ({ __typeof__(&(fn)) volatile prepare_target_ = &(fn); prepare_target_; })
/* br_fx_prepare_block runs from XIP flash in this mode, while these cold
 * helpers remain in RAM. Force absolute indirect calls for the cross-region
 * edges; pi32v2 cannot encode them as relative long jumps. */
#define BR_FX_PREPARE_INVOKE(fn, ...) \
    ({ __typeof__(&(fn)) volatile prepare_helper_ = &(fn); prepare_helper_(__VA_ARGS__); })
#define BR_FX_PREPARE_CODE
#else
#define BR_FX_PREPARE_TARGET(fn) fn
#define BR_FX_PREPARE_INVOKE(fn, ...) fn(__VA_ARGS__)
#define BR_FX_PREPARE_CODE BR_FX_RAM_AUDIO
#endif

/* The master setup runs once per audio block. Keep it in XIP flash by
 * default; its per-frame DSP caller remains in RAM. Calls crossing this
 * boundary use absolute indirection because pi32v2 cannot encode a direct
 * relative call between the XIP and RAM regions. Set this to zero to retain
 * the legacy all-RAM placement. */
#ifndef BR_FX_MASTER_PREPARE_FLASH
#define BR_FX_MASTER_PREPARE_FLASH 1
#endif
#if BR_FX_MASTER_PREPARE_FLASH != 0 && BR_FX_MASTER_PREPARE_FLASH != 1
#error "BR_FX_MASTER_PREPARE_FLASH must be 0 or 1"
#endif
#if BR_FX_MASTER_PREPARE_FLASH
#define BR_FX_MASTER_PREPARE_CODE __attribute__((minsize, noinline))
#define BR_FX_MASTER_PREPARE_TARGET(fn) \
    ({ __typeof__(&(fn)) volatile master_prepare_target_ = &(fn); master_prepare_target_; })
#define BR_FX_MASTER_PREPARE_INVOKE(fn, ...) \
    ({ __typeof__(&(fn)) volatile master_prepare_target_ = &(fn); \
       master_prepare_target_(__VA_ARGS__); })
#else
#define BR_FX_MASTER_PREPARE_CODE BR_FX_RAM_AUDIO
#define BR_FX_MASTER_PREPARE_TARGET(fn) fn
#define BR_FX_MASTER_PREPARE_INVOKE(fn, ...) fn(__VA_ARGS__)
#endif /* BR_FX_MASTER_PREPARE_FLASH */

/* The shared reverb (reverb2). Only its per-sample core, rv2_core_run, is RAM code;
 * design, apply and the block driver run from XIP flash and reach the core through
 * an absolute indirect call (pi32v2 cannot encode a relative call between the two
 * regions). The core's small index tables stay ordinary constants: at -O3 the four
 * allpass tables fold, but the line loop keeps rv2_off/rv2_blen (16 B each) as XIP
 * .rodata loads. They cannot move to a named .data section: the vendor compiler marks
 * a 16-byte constant there mergeable and its assembler then fails ("expected the entry
 * size"). RV2_LINE_STRUCT=1 removes those loads (the offsets live in the state,
 * same output). Build options, each overridable with build.py --define:
 * RV2_UPSAMPLE_TAPS (6 or 4), RV2_INT_READS (0 or 1), RV2_LINE_STRUCT (0 or 1). */
#define RV2_API static
#define RV2_RAM BR_FX_RAM_AUDIO __attribute__((noinline))
#define RV2_CORE_CALL(fn, ...) \
    ({ __typeof__(&(fn)) volatile rv2_core_fn_ = &(fn); rv2_core_fn_(__VA_ARGS__); })
#include "instrument_reverb2.c"

typedef uint32_t br_fx_clear_word_t __attribute__((may_alias));
static inline __attribute__((always_inline)) void br_fx_clear32(br_fx_clear_word_t *p, uint32_t words)
{
    while (words--) *p++ = 0u;
}
static inline __attribute__((always_inline)) void br_fx_clear16(int16_t *p, uint32_t samples)
{
    while (samples--) *p++ = 0;
}
static inline __attribute__((always_inline)) int br_fx_is_kit(uint32_t preset)
{
    return (preset >= BR_KIT_BRAIDS && preset <= BR_KIT_909) ||
        (BR_ENABLE_PLAITS && preset == BR_KIT_PLAITS);
}
static inline __attribute__((always_inline)) uint32_t br_fx_bound_ram(uint32_t x, uint32_t hi)
{ return x > hi ? hi : x; }
/* Spring and Ethereal were retired; sessions load them as Room and Space. Any
 * stale id reaching the effects maps the same way, so only three kernels exist. */
static inline __attribute__((always_inline)) uint32_t br_fx_reverb_engine_of(uint32_t e)
{ return e == BR_REVERB_SPRING ? BR_REVERB_ROOM : e >= BR_REVERB_ETHEREAL ? BR_REVERB_SPACE : e; }
static inline __attribute__((always_inline)) int32_t br_fx_clip_ram(int32_t x, int32_t lo, int32_t hi)
{ return x < lo ? lo : x > hi ? hi : x; }
static inline __attribute__((always_inline)) int16_t br_fx_clip16_ram(int32_t x)
{ return (int16_t)br_fx_clip_ram(x, -32767, 32767); }

/* Glitch quantization always uses a power-of-two step derived from delay_time.
 * The mask form is exactly truncation toward zero, while avoiding a variable
 * signed divide on the occasional sample where a new hold value is captured. */
static inline __attribute__((always_inline)) int32_t
br_fx_glitch_quantize(int32_t sample, uint32_t step)
{
#if BR_FX_GLITCH_POWER2_QUANTIZE
    uint32_t magnitude = (uint32_t)sample;
    if (sample < 0) magnitude = 0u - magnitude;
    magnitude &= ~(step - 1u);
    return sample < 0 ? -(int32_t)magnitude : (int32_t)magnitude;
#else
    return (sample / (int32_t)step) * (int32_t)step;
#endif
}

#define BR_FX_DELAY_FULL_SAMPLES 16384u
#if BR_FX_DELAY_RATE2
#define BR_FX_DELAY_SAMPLES (BR_FX_DELAY_FULL_SAMPLES / 2u)
#define BR_FX_DELAY_MAX_SHIFT 5u   /* at most 8: the per-level ring indices are bytes */
#else
#define BR_FX_DELAY_SAMPLES BR_FX_DELAY_FULL_SAMPLES
#define BR_FX_DELAY_MAX_SHIFT 0u
#endif
#define BR_FX_DELAY_XFADE_FRAMES 64u
#if BR_FX_MOD_RATE2
#define BR_FX_MOD_SAMPLES 1024u
#else
#define BR_FX_MOD_SAMPLES 2048u
#endif
/* The deepest MOD read (and its interpolation partner) in 44.1 kHz samples. */
#define BR_FX_MOD_REACH (BR_FX_MOD_RATE2 ? 2u * (BR_FX_MOD_SAMPLES - 2u) : BR_FX_MOD_SAMPLES - 2u)
#define BR_FX_TAIL_SAMPLES (FS * 3u)

#ifndef BR_FX_GLOBAL_REVERB
#define BR_FX_GLOBAL_REVERB 1
#endif
/* Only the shared reverb (instrument_reverb2.c) exists: the per-track reverb and the
 * per-frame FX path that ran it were removed. */
#if !BR_FX_GLOBAL_REVERB || !BR_FX_BLOCK_STAGE_ONLY_BUILD || !BR_FX_BLOCK_STAGE_PIPELINE
#error "the per-track reverb was removed: BR_FX_GLOBAL_REVERB and the stage-only FX build are required"
#endif
/* The shared reverb runs inside the render job of this track, one block behind the
 * tracks that feed it, so any track can host it. Measured on build 1578, hosting it
 * in the kit finalizer lowered the mean finish time with stacked effects but raised
 * the worst half: the finalizer starts after the four lane jobs, so the reverb
 * became a late tail. Track 3 stays the host. */
#ifndef BR_GR_HOST
#define BR_GR_HOST 2u
#endif

/* Four independent mono effect memories. Their combined data reservation is
 * exactly 215,984 bytes: 131,072 delay + 68,528 reverb + 16,384 modulation. */
#if BR_POLY_SUPPORT
static int16_t br_delay_line[BR_PARTS][BR_FX_DELAY_SAMPLES] BR_POOL __attribute__((aligned(8)));
#else
static int16_t br_delay_line[BR_PARTS][BR_FX_DELAY_SAMPLES] BR_POOL __attribute__((aligned(4)));
#endif
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
/* Only selected-track effects may use a delay line while the other lines are
 * loaned. Aliases are changed while both workers are parked. */
static int16_t *br_fx_delay_alias[BR_PARTS] = {
    br_delay_line[0], br_delay_line[1], br_delay_line[2], br_delay_line[3]
};
static uint8_t br_fx_poly_loaned, br_fx_poly_part, br_fx_poly_mode;
#define BR_POLY_DELAY_LINE(p) br_fx_delay_alias[(p)]
#elif BR_POLY_SUPPORT
#define BR_POLY_DELAY_LINE(p) br_delay_line[(p)]
#else
#define BR_POLY_DELAY_LINE(p) br_delay_line[p]
#endif
static int16_t br_mod_line[BR_PARTS][BR_FX_MOD_SAMPLES] BR_POOL __attribute__((aligned(4)));
/* Shared stereo master wow line: 2 KiB, linked modulation with separate channels. */
static int32_t br_master_wow_line[2][256] BR_POOL;

typedef struct {
    uint32_t delay_pos, mod_pos, lfo_phase;
    uint32_t delay_storage_shift, delay_valid_samples, delay_last_time;
    uint32_t delay_output_xfade_left;
    int32_t delay_output_last, delay_output_xfade_from;
#if BR_FX_MOD_RATE2
    uint32_t mod_rate2_phase, previous_mod_engine;
    int32_t mod_rate2_input_history[3], mod_rate2_previous_output;
#endif
    uint32_t glitch_count;
    int32_t delay_lp, phase_state[4], mod_feedback_last;
    int32_t delay_hp;   /* loop high-pass: low-passed echo, Q4 */
    int32_t drive_lp, drive_dc, drive_prev, drive_send;     /* drive_send: last block's send, 0..127 */
    int32_t glitch_hold;
    uint32_t tail_samples;
    uint32_t tail_storage;
    uint32_t previous_delay_wet, previous_mod_mix;
#if BR_FX_DELAY_RATE2
    uint32_t delay_rate2_phase, delay_rate2_history_pos;
    uint32_t glitch_half_error;
    /* Line writes left before the whole line is known to hold zeros: reset to the
     * line length by every nonzero write. Zero lets the MIX 0 stage be skipped. */
    uint32_t delay_ring;
    /* Send half-band ring (track bus units), the wet half-band's ring of line
     * echoes (one per storage sample) and the odd output still to be played. */
    int32_t delay_rate2_send_history[16], delay_rate2_previous_echo;
    int32_t delay_echo_history[8];
    uint32_t delay_echo_pos;
    uint32_t delay_wow_phase, delay_flutter_phase;   /* TAPE's own wow and flutter */
    int32_t delay_pan_sigma;   /* WIDTH: slewed side of the echo now playing, Q15, + = left */
    int32_t delay_side_q14;    /* WIDTH * sigma, Q14 */
#if BR_FX_DELAY_MAX_SHIFT
    uint8_t delay_long_pos[8], delay_long_phase[8];   /* per level: ring index, 0/1 */
    uint32_t delay_long_interp_phase;
    int16_t delay_long_history[BR_FX_DELAY_MAX_SHIFT][16];   /* half the line value */
    int32_t delay_long_previous_echo, delay_long_echo, delay_long_odd;
#endif
#endif
    float drive_wet, drive_m, drive_ed, drive_ew;   /* DRIVE: wet gain s*M*T, level match M, smoothed energies (last, so no other offset moves) */
} br_fx_track_t;
static br_fx_track_t br_fx_track[BR_PARTS] BR_POOL;

typedef struct {
    uint32_t wow_pos, wow_phase, filter_tail, wow_tail, wow_valid_samples;
    uint32_t filter, saturation, wow, resonance;
    uint32_t heat_type, heat_amount;
    uint32_t bit_hold_count;
    int32_t bit_hold[2];
    int32_t low[2];
    float svf_low[2], band[2], hp2_low[2], hp2_band[2], hp2_a, hp2_d;
#if BR_FX_MASTER_WOW_PHASE_CACHE
    uint32_t stable_wow_phase_inc;
#endif
#if BR_FX_MASTER_STABLE_FASTPATH
    uint32_t controls_stable;
#if BR_FX_MASTER_COEFF_CACHE
    uint32_t stable_filter_alpha, stable_saturation_gain;
    float stable_filter_damping, stable_resonance_alpha;
    float stable_resonance_mix;
#endif
#endif
    uint32_t limit_cut;     /* master limiter: 32768 - gain (Q15); 0 = unity */
#ifndef BR_MASTER_LEVELER
#define BR_MASTER_LEVELER 0 /* trial: always-on output leveler (br_master_limit_block) */
#endif
#if BR_MASTER_LEVELER
    /* Output leveler (all zero = idle, 0 dB): boost - 32768 (Q15), the
     * applied gain reached - 32768 (Q15, boost x limiter), its per-frame
     * step (Q30), the smoothed mean square (a^2 / 64) and blocks under the gate. */
    uint32_t lev_boost;
    int32_t lev_applied;
    uint32_t lev_step;
    uint32_t lev_ms;
    uint32_t lev_quiet;
#endif
#if BR_FILTER_LADDER
    /* Master low-pass: the track filter's four-pole ladder, one per channel.
     * Both channels see the same control sequence, so their mixes match. */
    br_filter_t ladder[2];
#endif
} br_master_state_t;

/* The serialized br_master_fx_t keeps its legacy distortion member. The
 * renderer caches only the controls that still have an audible path. */
typedef struct {
    uint32_t filter, saturation, wow, resonance;
    uint32_t heat_type, heat_amount, filter_snap_generation;
} br_master_controls_t;

/* Values derived from controls that are fixed for one audio block. The
 * per-sample effect routines reuse these instead of reclamping and rebuilding
 * the same parameters on every frame. */
typedef struct {
    uint32_t delay_engine, delay_amount, delay_time, delay_feedback;
    /* Keep the outer wet gate open through the first selected long echo and
     * its existing tail fade. Short divisions retain the legacy 3 s hold. */
    uint32_t delay_tail_hold;
    /* Full-rate sample lengths; four beats can reach six seconds. */
    uint32_t delay_tap[4];
    uint32_t delay_storage_shift;
#if BR_FX_DELAY_RATE2
    uint16_t delay_tape_alpha;   /* TAPE's per-pass low-pass, or TONE's for DIGITAL (0: open) */
    uint16_t delay_wow_depth_q8, delay_flutter_depth_q8;   /* storage samples, Q8 */
    uint32_t delay_wow_inc, delay_flutter_inc;             /* per storage sample */
#endif
    uint16_t delay_hp_alpha;   /* loop high-pass coefficient at the line rate, Q12 */
    uint16_t glitch_hold, glitch_step;
    uint16_t delay_width_q15;  /* WIDTH 0..127 as 0..32767; 0 = the mono line */
    uint32_t reverb_wet;       /* SEND to the shared reverb, 0..127 */
#if BR_FX_PAN_COEFF_CACHE
    uint16_t pan_left_q15, pan_right_q15;
#endif
    uint32_t mod_engine, mod_depth, mod_mix, mod_lfo_inc, mod_base_q8, mod_span, mod_feedback;
#if BR_FX_Q15_MIX_GAIN
    uint16_t delay_amount_gain, mod_mix_gain;
#elif BR_FX_FLOAT_MIX_GAIN
    float delay_amount_gain, mod_mix_gain;
#endif
} br_fx_controls_t;
static br_fx_controls_t br_fx_controls[BR_PARTS] BR_POOL;
#ifndef BR_FX_STABLE_CONTROL_CACHE
#define BR_FX_STABLE_CONTROL_CACHE 1
#endif
#if BR_FX_STABLE_CONTROL_CACHE != 0 && BR_FX_STABLE_CONTROL_CACHE != 1
#error "BR_FX_STABLE_CONTROL_CACHE must be 0 or 1"
#endif
#if BR_FX_STABLE_CONTROL_CACHE
/* Exact raw control key: derived values never depend on advancing DSP state.
 * Full words preserve out-of-range inputs and low-bit glitch controls too. */
static uint32_t br_fx_control_key[BR_PARTS][11];
static uint32_t br_fx_control_key_valid;
#endif
/* Shared call-free wide-delta helper; master paths use it in every mix mode. */
/* Keep the cold wide-delta work in one shared RAM helper instead of
 * duplicating its range checks and exact fallback into each hot FX caller. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) int32_t
br_fx_blend127_wide(int32_t delta, uint32_t mix)
{
    uint32_t raw = (uint32_t)delta;
    uint32_t magnitude = delta < 0 ? 0u - raw : raw;
    if (magnitude <= 16909004u)
        return br_fx_div127(delta * (int32_t)mix);

    /* Split delta into quotient/remainder first so every product remains in
     * int32 range for the clamped 0..127 mix, even at INT32_MIN. */
    int32_t quotient = delta / 127;
    int32_t remainder = delta - quotient * 127;
    return quotient * (int32_t)mix +
        (remainder * (int32_t)mix) / 127;
}
#if BR_FX_Q15_MIX_GAIN
/* Controls cache rounded Q15 /127 gains once per block; The bounded path
 * keeps the signed product below INT32_MAX and falls back for wider deltas. */
static inline __attribute__((always_inline)) int32_t
br_fx_blend127_q15(int32_t delta, uint32_t mix, uint32_t gain_q15)
{
    if (!mix) return 0;
    if (mix >= 127u) return delta;
    /* Inclusive delta interval [-65535, 65535], checked without abs(INT_MIN). */
    if ((uint32_t)delta + 65535u <= 131070u) {
        int32_t product = delta * (int32_t)gain_q15;
        return product / 32768;
    }
    return br_fx_blend127_wide(delta, mix);
}
#define BR_FX_BLEND127(delta, mix, gain_q15) \
    br_fx_blend127_q15((delta), (mix), (gain_q15))
#elif BR_FX_FLOAT_MIX_GAIN
/* The wet/dry control is fixed for a block. Cache gain so each sample needs
 * only a float conversion and multiply, with exact endpoints and safe fallback. */
static inline __attribute__((always_inline)) int32_t
br_fx_blend127_cached(int32_t delta, uint32_t mix, float gain)
{
    if (!mix) return 0;
    if (mix >= 127u) return delta;
    {
        uint32_t raw = (uint32_t)delta;
        uint32_t magnitude = delta < 0 ? 0u - raw : raw;
        if (magnitude <= 0xffffffu)
            return (int32_t)((float)delta * gain);
    }
    return br_fx_blend127_wide(delta, mix);
}
#define BR_FX_BLEND127(delta, mix, gain) \
    br_fx_blend127_cached((delta), (mix), (gain))
#else
#define BR_FX_BLEND127(delta, mix, gain) \
    br_fx_div127((delta) * (int32_t)(mix))
#endif
static br_master_state_t br_master_state BR_POOL;
static br_master_controls_t br_master_cached = {
    64u, 0u, 0u, 0u, BR_HEAT_BIT, 0u, 0u
};
#if BR_FX_MASTER_Q15_MIX_GAIN
static uint32_t br_master_saturation_mix_gain_q15;
static uint32_t br_master_wow_mix_gain_q15;
static uint32_t br_master_bit_mix_gain_q15;
#endif
/* Opt-in diagnostic schedule mode. Zero preserves the production concurrent path. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static volatile uint32_t br_fx_serial_mode;
#else
#define br_fx_serial_mode 0u
#endif
typedef struct {
    uint32_t magic, inverse_magic, version;
    volatile uint32_t words[2];
} br_fx_progress_retained_t;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
#if defined(FM1_DEVICE) && !defined(FX_FAULT_TEST)
static volatile br_fx_progress_retained_t br_fx_progress_retained
    __attribute__((section(".noinit.fx_progress"), used, aligned(4)));
#else
static volatile br_fx_progress_retained_t br_fx_progress_retained;
#endif
static uint32_t br_fx_progress_previous[2];
static uint32_t br_fx_progress_previous_valid;
static uint32_t br_fx_progress_bootstrapped;
#elif defined(FM1_DEVICE) && !defined(FX_FAULT_TEST)
static uint8_t br_fx_progress_reserved[sizeof(br_fx_progress_retained_t)]
    __attribute__((section(".noinit.fx_progress"), used, aligned(4)));
#endif
/* Block job context is published by br_fx_process_ram before invoking any
 * track jobs. Each job owns only its p-indexed FX state and output row. */
static int32_t (*br_fx_job_bus)[BR_BLOCK];
static uint32_t br_fx_job_n;
static uint32_t br_fx_job_enabled[BR_PARTS], br_fx_job_pan[BR_PARTS], br_fx_job_kit[BR_PARTS];
/* The PAN gains each track's last block ended on, packed and XOR 0x80008000 (0 = centre). */
static uint32_t br_fx_pan_prev[BR_PARTS];
static int32_t br_fx_track_stereo[BR_PARTS][BR_BLOCK][2] BR_POOL;
/* A track job that took the silent-skip path records the current block's
 * sequence, so the finish stage knows that row is all zero without a scan. */
static uint32_t br_fx_block_seq, br_fx_track_silent_seq[BR_PARTS];
static volatile uint32_t br_gr_separate_seq = 0xffffffffu; /* block whose reverb is its own job */
#if BR_FX_GLOBAL_REVERB
/* One reverb for all tracks (instrument_reverb2.c). Each track job leaves its send
 * (post-delay signal times its send level) in the buffer for the current block; the
 * shared reverb sums the previous block's sends, so its wet return lags the tracks
 * by one block (5.8 ms) and runs in the render phase instead of the serial tail.
 * Once the reverb has read them, the previous block's rows are free until the next
 * block's tracks write them: row 0 takes the summed send, rows 1 and 2 the stereo
 * return that br_gr_add_range adds to this block's mix (no separate wet buffer). */
static int32_t br_gr_send[2][BR_PARTS][BR_BLOCK] BR_POOL;
#define BR_GR_IN_ROW 0u
#define BR_GR_WET_L_ROW 1u
#define BR_GR_WET_R_ROW 2u
static uint8_t br_gr_have[2][BR_PARTS];
/* Line pools and state of the reverb (24,656 + 2,760 bytes of lines). */
static int16_t br_gr_pool16[RV2_POOL16_LEN] BR_POOL __attribute__((aligned(4)));
static int32_t br_gr_pool32[RV2_POOL32_LEN] BR_POOL;
static rv2_state_t br_gr_rv BR_POOL;
static int16_t br_gr_sync_pre[RV2_SYNC_PRE_LEN];
/* The optional user controls (pre-delay, tone, mod, low cut, width), each 0..127 or
 * RV2_DERIVE (255: derived from TYPE/SIZE/DECAY). br_gr_prepare refreshes them each
 * block from br_master.reverb_extra / reverb_manual (the saved homes). */
static rv2_overrides_t br_gr_overrides = {RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE};
static rv2_overrides_t br_gr_key_overrides;
static rv2_coeffs_t br_gr_design_cache;
/* br_gr_key: TYPE, SIZE, DECAY the coefficients were designed for. br_gr_out_seq:
 * the block whose return rows hold this block's reverb output. */
static uint32_t br_gr_key[3], br_gr_key_valid, br_gr_wanted, br_gr_run, br_gr_cur, br_gr_out_seq;
/* TYPE change de-click: 1 = this block's return fades out (the new TYPE is applied
 * next block), 2 = this block's return fades in after it was applied. */
static uint32_t br_gr_duck;
/* Bound expensive reverb redesigns by elapsed samples, even in 32-frame pipelines. */
static uint32_t br_gr_lfo_frames;
static uint32_t br_gr_sync_key = 0xffffffffu;
static void br_gr_prepare(void);
/* Output of the DRIVE stage, one row per track; the rest of the track chain reads it as its input. */
static int32_t br_fx_drive_buf[BR_PARTS][BR_BLOCK] BR_POOL;
static uint32_t br_fx_drive_block(uint32_t p, const int32_t *in, int32_t *out, uint32_t n);
static void br_gr_plan(void);
/* Blocks the shared reverb ran, guard hits, last and worst job time in ticks. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
#if defined(FM1_DEVICE)
volatile uint32_t br_gr_stats[8];
#else
static volatile uint32_t br_gr_stats[8];
#endif
#else
/* The scheduler consumes the last measured job time; keep it independent of
 * the diagnostic counters and peak tracking. */
static volatile uint32_t br_gr_cost_ticks;
#endif
#endif
#if defined(FM1_DEVICE)
volatile int32_t br_fx_output_gain_q12 = 4096;
volatile uint32_t br_fx_output_scale_request, br_fx_output_prescaled;
#endif
#if BR_FX_FINISH_MASTER_STREAM
/* C0 owns compressor state and publishes finished stereo slices. C1 owns
 * master state and consumes each published slice in sample order. */
typedef struct {
    int32_t *out;
    uint32_t n;
    volatile uint32_t produced_frames;
    uint32_t scale;              /* C1 also applies the final Q12 output gain */
    int32_t gain_q12;
    void (*pre)(void);           /* next-block bookkeeping, run before slice 0 */
} br_fx_finish_stream_job_t;
static br_fx_finish_stream_job_t br_fx_finish_stream_job BR_POOL;
/* Work counters for diagnostic 0x9b: master section time on CPU1 and shared-reverb time. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
uint32_t br_fx_master_work_ticks, br_fx_gr_work_ticks;
#endif
#if defined(FM1_DEVICE)
#define BR_FX_STREAM_LOAD_PRODUCED(job) ((job)->produced_frames)
#define BR_FX_STREAM_PUBLISH_PRODUCED(job, frames) ((job)->produced_frames = (frames))
#else
/* Host concurrency tests use C atomics; the target relies on aligned words
 * and the platform's `csync` publication barrier. */
#define BR_FX_STREAM_LOAD_PRODUCED(job) \
    __atomic_load_n(&(job)->produced_frames, __ATOMIC_ACQUIRE)
#define BR_FX_STREAM_PUBLISH_PRODUCED(job, frames) \
    __atomic_store_n(&(job)->produced_frames, (frames), __ATOMIC_RELEASE)
#endif
#endif
#if BR_FX_BLOCK_STAGE_PIPELINE
/* Each worker owns one part row while the independent stages reuse this
 * block-sized modulation buffer. Stereo output storage is temporary
 * delay/event space until the final reverb/mix stage. */
static int32_t br_fx_stage_mod[BR_PARTS][BR_BLOCK] BR_POOL;
#endif
typedef struct { uint32_t count, part[BR_PARTS]; } br_fx_job_batch_t;
static br_fx_job_batch_t br_fx_worker_batches[2] BR_POOL;
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
/* Test-only fake clock and rendezvous hooks exercise the per-slice deadline
 * independently of host scheduling. These declarations compile out on target. */
static uint32_t br_fx_stream_test_ticks(void);
static void br_fx_stream_test_waiting(uint32_t end);
static void br_fx_stream_test_after_range(uint32_t end);
static void br_fx_stream_test_before_source_next(uint32_t end);
static void br_fx_stream_test_before_publish(uint32_t end);
static void br_fx_stream_test_done(void);
#endif
#if !BR_AUDIO_FUSION_ONLY_BUILD
typedef struct {
    int32_t (*bus)[BR_BLOCK];
    int32_t (*keys)[BR_BLOCK];
    const br_pipeline_controls_t *controls;
    int32_t *out;
    uint32_t n, sequence;
} br_fx_reverse_job_t;
static br_fx_reverse_job_t br_fx_reverse_job BR_POOL;
typedef struct {
    void *fx_context;
    void *source_context;
    uint32_t fx_valid;
} br_fx_hybrid_job_t;
static br_fx_hybrid_job_t br_fx_hybrid_job BR_POOL;
#endif
static uint32_t br_fx_job_cost[BR_PARTS];
#if !BR_AUDIO_FUSION_ONLY_BUILD
static const br_pipeline_controls_t *br_fx_pipeline_controls;
static int32_t (*br_fx_pipeline_keys)[BR_BLOCK];
static uint32_t br_fx_pipeline_cpu0_only;
#endif
static uint32_t br_fx_comp_active, br_fx_comp_key_part;
static uint32_t br_fx_dry_center;
static volatile uint32_t br_fx_fusion_pending;
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
/* Caller owns a parked worker and muted audio. Inactive delay tails are
 * discarded when the loan ends; project settings are never changed. */
BR_AUDIO_API int br_fx_poly_can_loan(uint32_t part, uint32_t required)
{
    return part < BR_SYNTH_PARTS && !br_fx_poly_loaned && !br_fx_fusion_pending &&
        !br_punch_effect && required <= sizeof br_delay_line - sizeof br_delay_line[0];
}
BR_AUDIO_API uint32_t br_fx_poly_loan_active(void) { return br_fx_poly_loaned; }
BR_AUDIO_API int br_fx_poly_mode_set(uint32_t part, uint32_t enabled)
{
    if (br_fx_fusion_pending || part >= BR_SYNTH_PARTS) return 0;
    if (!enabled && br_fx_poly_loaned) return 0;
    if (enabled && br_fx_poly_mode) return 0;
    br_fx_poly_part = (uint8_t)part;
    br_fx_poly_mode = !!enabled;
    return 1;
}
BR_AUDIO_API int br_fx_poly_loan_acquire(uint32_t part, uint32_t required, void **buffer, uint32_t *bytes)
{
    if (!buffer || !bytes || !br_fx_poly_can_loan(part, required)) return 0;
    if (part != 0u) memmove(br_delay_line[0], br_delay_line[part], sizeof br_delay_line[0]);
    br_fx_delay_alias[part] = br_delay_line[0];
    br_fx_poly_part = (uint8_t)part;
    br_fx_poly_loaned = 1u;
    *buffer = br_delay_line[1];
    *bytes = (uint32_t)(sizeof br_delay_line - sizeof br_delay_line[0]);
    return 1;
}
BR_AUDIO_API int br_fx_poly_loan_release(void)
{
    if (!br_fx_poly_loaned) return 1;
    if (br_fx_fusion_pending) return 0;
    uint32_t selected = br_fx_poly_part;
    if (selected != 0u) memmove(br_delay_line[selected], br_delay_line[0], sizeof br_delay_line[0]);
    for (uint32_t p = 0; p < BR_PARTS; ++p) {
        if (p != selected) {
            memset(br_delay_line[p], 0, sizeof br_delay_line[p]);
            memset(&br_fx_track[p], 0, sizeof br_fx_track[p]);
        }
        br_fx_delay_alias[p] = br_delay_line[p];
    }
    br_fx_poly_loaned = 0u;
    return 1;
}
#endif
static volatile uint8_t br_fx_fusion_done[BR_PARTS];
#if !BR_AUDIO_FUSION_ONLY_BUILD
static br_master_fx_t br_pipeline_last_master = {64u, 0u, 0u, 0u, 0, 0u, 0u};
#endif
static BR_FX_RAM_AUDIO int32_t br_fx_pole(int32_t current, int32_t target, uint32_t alpha);
static BR_FX_RAM_AUDIO int32_t br_fx_softclip(int32_t x);
static BR_FX_RAM_AUDIO int32_t br_fx_tri(uint32_t phase);
static BR_FX_RAM_AUDIO int32_t br_fx_div_pow2(int32_t value, uint32_t shift);
static BR_FX_RAM_AUDIO int32_t br_fx_mul_q15(int32_t x, uint32_t q);

#if BR_FX_FEEDFORWARD_Q15_ASR64 || BR_FX_PHASER_MUL_Q15_ASR64
/* Signed wide multiply followed by arithmetic shift. Callers below constrain
 * q and x so the shifted result remains in int32 range. Unlike br_fx_mul_q15,
 * this rounds negative fractional results down; the difference is <= 1 LSB. */
static inline __attribute__((always_inline)) int32_t
br_fx_mul_q15_asr64_bounded(int32_t x, uint32_t q)
{
    return (int32_t)(((int64_t)x * (int64_t)q) >> 15);
}
#endif

/* PAN: constant power, sqrt2*cos / sqrt2*sin of theta = pi/4 * (1 -/+ amount),
 * amount = (64 - v) / 64 to the left, (v - 64) / 63 to the right. Centre is
 * unity on both sides (and bypassed exactly), hard pan +3.0 dB on one side and
 * silence on the other, total power flat within 0.02 dB. Table: sqrt2*cos(pi*k/32)
 * in Q15, k = 0..16, linearly interpolated; the near side reads 8 - 8*amount,
 * the far side 8 + 8*amount. Returns the gains packed (left << 16 | right). */
static const uint16_t br_fx_pan_cos_q15[17] = {
    46341u, 46118u, 45451u, 44346u, 42813u, 40869u, 38531u, 35822u, 32768u,
    29398u, 25746u, 21845u, 17734u, 13452u, 9041u, 4542u, 0u
};
static inline __attribute__((always_inline)) uint32_t br_fx_pan_table_gain(uint32_t index_q8)
{
    uint32_t i = index_q8 >> 8;
    int32_t a = br_fx_pan_cos_q15[i], b = i < 16u ? br_fx_pan_cos_q15[i + 1u] : a;
    return (uint32_t)(a + (((b - a) * (int32_t)(index_q8 & 255u)) >> 8));
}
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) uint32_t br_fx_pan_gains(uint32_t pan)
{
    uint32_t amount_q8 = pan < 64u ? (64u - pan) * 32u : ((pan - 64u) * 2048u + 31u) / 63u;
    uint32_t near = br_fx_pan_table_gain(2048u - amount_q8);
    uint32_t far = br_fx_pan_table_gain(2048u + amount_q8);
    return pan < 64u ? near << 16 | far : far << 16 | near;
}
/* Gains above unity act as crossfeed so a stereo input keeps its image:
 * the near side is its own channel plus (g - 1) of the other. For the mono
 * track signal this is exactly out = x * g on each side. */
static inline __attribute__((always_inline)) int32_t br_fx_pan_mul(int32_t x, int32_t g)
{
    return (int32_t)(((int64_t)x * g) >> 15);
}
static inline __attribute__((always_inline)) void br_fx_pan_stereo_cached(
    int32_t left, int32_t right, uint32_t pan, uint32_t left_q15,
    uint32_t right_q15, int32_t *out_left, int32_t *out_right)
{
    (void)pan;
    if (left_q15 >= 32768u) {
        *out_left = left + br_fx_pan_mul(right, (int32_t)left_q15 - 32768);
        *out_right = br_fx_pan_mul(right, (int32_t)right_q15);
    } else {   /* also mid-ramp, where both gains can sit below unity */
        *out_left = br_fx_pan_mul(left, (int32_t)left_q15);
        *out_right = right + br_fx_pan_mul(left, (int32_t)right_q15 - 32768);
    }
}
#define br_fx_pan_stereo_cached_noncenter br_fx_pan_stereo_cached
#if !BR_FX_PAN_COEFF_CACHE
#error "the constant-power PAN law needs its block gains (BR_FX_PAN_COEFF_CACHE)"
#endif

/* A PAN change ramps both gains linearly across one block. The track job runs
 * that block at centre (identity), then this cold pass applies the ramp; a
 * static PAN costs nothing extra per sample. `from`/`to` are packed gains
 * XOR 0x80008000, so 0 is centre (the zeroed state). */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void
br_fx_pan_ramp(int32_t (*stereo)[2], uint32_t n, uint32_t from, uint32_t to)
{
    uint32_t i;
    int32_t gl, gr, dl, dr;
    if (!n) return;
    from ^= 0x80008000u; to ^= 0x80008000u;
    gl = (int32_t)(from >> 16) << 8; gr = (int32_t)(from & 0xffffu) << 8;   /* Q23 */
    dl = (((int32_t)(to >> 16) << 8) - gl) / (int32_t)n;
    dr = (((int32_t)(to & 0xffffu) << 8) - gr) / (int32_t)n;
    /* Each side times its own gain: exact for the mono track signal (the device's
     * shared-reverb build); a stereo input skips the crossfeed for this one block. */
    for (i = 0u; i < n; ++i) {
        gl += dl; gr += dr;
        stereo[i][0] = br_fx_pan_mul(stereo[i][0], gl >> 8);
        stereo[i][1] = br_fx_pan_mul(stereo[i][1], gr >> 8);
    }
}

#if defined(FM1_DEVICE)
#define BR_FX_TABLE_DATA __attribute__((section(".data.fm1_fx_tables"), used))
#else
#define BR_FX_TABLE_DATA
#endif
#include "instrument_softclip_table.h"

#if BR_FX_TAPE_WOW_DELTA_LUT
/* One complete, continuous triangular wow cycle. Interpolation below uses
 * the full oscillator phase, rather than repeating an audio-rate nibble.
 * Keep 64 bytes: smaller first tables trigger a vendor merge-section bug. */
static const int32_t br_fx_tape_wow_delta[16] BR_FX_TABLE_DATA = {
    -5, -4, -2, -1, 1, 2, 4, 5,
     5,  4,  2,  1,-1,-2,-4,-5
};
#endif
/* TIME entries in 24ths of a quarter note, ordered by duration: 1/32, 1/16T,
 * 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/4D, 1/2, 1/2D, 1 BAR.
 * Stored 0..127 maps to the 14 entries in even buckets. */
#define BR_FX_DELAY_NOTE_COUNT 14u
static const uint8_t br_fx_delay_note_24ths[BR_FX_DELAY_NOTE_COUNT] BR_FX_TABLE_DATA =
    {3u, 4u, 6u, 8u, 9u, 12u, 16u, 18u, 24u, 32u, 36u, 48u, 72u, 96u};
#undef BR_FX_TABLE_DATA

/* Convert 44.1 kHz sample counts to the selected native profile. The FX
 * half-rate paths first convert to the native output rate, then halve their
 * loop lengths, preserving the original physical delay. Phase increments
 * use the inverse ratio so oscillators keep the same cycles per second. */
static inline __attribute__((always_inline)) uint32_t br_fx_rate_samples(uint32_t samples)
{
#if BR_FX_NATIVE_RATE_CALIBRATION
    uint32_t scaled = (samples * 320u + 220u) / 441u;
    return samples && !scaled ? 1u : scaled;
#elif BR_FX_NATIVE_RATE_48K
    uint32_t scaled = (samples * 480u + 220u) / 441u;
    return samples && !scaled ? 1u : scaled;
#else
    return samples;
#endif
}

static inline __attribute__((always_inline)) uint32_t br_fx_delay_note_of(uint32_t stored)
{
    stored = stored > 127u ? 127u : stored;
    return stored * BR_FX_DELAY_NOTE_COUNT / 128u;
}
BR_AUDIO_API uint32_t br_fx_delay_note_index(uint32_t stored) { return br_fx_delay_note_of(stored); }
/* Keep every legacy 0..127 TIME value intact; the two new short divisions use
 * a separate saved field. The UI list remains ordered by actual duration. */
BR_AUDIO_API uint32_t br_fx_delay_choice(uint32_t stored, uint32_t short_note)
{
    uint32_t old = br_fx_delay_note_of(stored);
    if (short_note == 1u) return 0u;   /* 1/32 triplet */
    if (short_note == 2u) return 3u;   /* dotted 1/32 */
    return old < 2u ? old + 1u : old + 2u;
}
BR_AUDIO_API void br_fx_delay_choice_set(uint32_t choice, uint32_t *stored, uint8_t *short_note)
{
    if (choice > 15u) choice = 15u;
    *short_note = choice == 0u ? 1u : choice == 3u ? 2u : 0u;
    uint32_t old = *short_note || choice == 1u ? 0u : choice == 2u ? 1u : choice - 2u;
    *stored = (old * 128u + 64u) / BR_FX_DELAY_NOTE_COUNT;
}


/* The middle of the entry's bucket. */
BR_AUDIO_API uint32_t br_fx_delay_note_store(uint32_t index)
{
    if (index >= BR_FX_DELAY_NOTE_COUNT) index = BR_FX_DELAY_NOTE_COUNT - 1u;
    return (index * 128u + 64u) / BR_FX_DELAY_NOTE_COUNT;
}

/* Full-rate samples: FS * 60 / bpm * 24ths / 24. The line holds every entry
 * exactly at every tempo (1 BAR at 60 BPM is 4 s; the longest storage holds 16 s). */
static BR_FX_PREPARE_CODE uint32_t br_fx_delay_note_samples(uint32_t stored,
                                                          uint32_t bpm)
{
    uint32_t n = br_fx_delay_note_24ths[br_fx_delay_note_of(stored)];
    bpm = bpm < BR_BPM_MIN ? BR_BPM_MIN : bpm > BR_BPM_MAX ? BR_BPM_MAX : bpm;
    return (FS * 5u * n + bpm) / (2u * bpm);
}
static inline __attribute__((always_inline)) uint32_t br_fx_rate_q8(uint32_t q8)
{
#if BR_FX_NATIVE_RATE_CALIBRATION
    return (q8 * 320u + 220u) / 441u;
#elif BR_FX_NATIVE_RATE_48K
    return (q8 * 480u + 220u) / 441u;
#else
    return q8;
#endif
}
static inline __attribute__((always_inline)) int32_t br_fx_rate_signed(int32_t value)
{
#if BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K
    uint32_t mag = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
#if BR_FX_NATIVE_RATE_CALIBRATION
    uint32_t numerator = mag * 320u + 220u;
#else
    uint32_t numerator = mag * 480u + 220u;
#endif
    uint32_t scaled;
#if BR_FX_RATE_SIGNED_441_RECIP
    /* The production callers currently use [-4096, 4095]. The reciprocal
     * is exact over that documented domain; retain division outside it so
     * this helper does not silently narrow its general input contract. */
    if (mag <= 4096u)
        scaled = (uint32_t)(((uint64_t)numerator * 9739155u) >> 32);
    else
        scaled = numerator / 441u;
#else
    scaled = numerator / 441u;
#endif
    return value < 0 ? -(int32_t)scaled : (int32_t)scaled;
#else
    return value;
#endif
}
#if BR_FX_NATIVE_RATE_48K
/* WOW's /1016 stage bounds this input to [-4095,4095]. On that domain,
 * floor((480*magnitude + 220) / 441) is exactly
 * floor((160*magnitude + 73) / 147), with a maximum numerator of 655433. */
static inline __attribute__((always_inline)) int32_t
br_fx_master_wow_rate_48_bounded(int32_t value)
{
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    uint32_t scaled = (magnitude * 160u + 73u) / 147u;
    return value < 0 ? -(int32_t)scaled : (int32_t)scaled;
}
#endif
static inline __attribute__((always_inline)) uint32_t br_fx_rate_phase(uint32_t phase)
{
#if BR_FX_NATIVE_RATE_CALIBRATION
    return (phase * 441u + 160u) / 320u;
#elif BR_FX_NATIVE_RATE_48K
    return (phase * 441u + 240u) / 480u;
#else
    return phase;
#endif
}
/* Global high-pass coefficients (filter 65..127): the track high-pass's law
 * (instrument_filter.c), in float for the master frame's TPT state-variable
 * filter: g = tan(pi*fc/fs), a = q + g, d = 1/(1 + q*g + g*g), q = 1/Q from
 * the same resonance nodes (Butterworth at 0, +16 dB peak at 127). Runs from
 * flash only when the filter or resonance moves. */
static __attribute__((noinline)) void
br_master_hp_coeffs(uint32_t filter, uint32_t resonance, float *g, float *a, float *d)
{
    float gf, qf;
    if (filter < 65u) filter = 65u;
    if (resonance > 127u) resonance = 127u;
    gf = (float)br_filter_hp_target(filter - 65u) * (1.0f / 536870912.0f);
    qf = (float)br_filter_res_node(br_filter_hp_q_nodes, resonance) * (1.0f / 32768.0f);
    *g = gf;
    *a = qf + gf;
    *d = 1.0f / (1.0f + qf * gf + gf * gf);
    { float g2 = gf * 0.5f;
      br_master_state.hp2_a = 1.41421356f + g2;
      br_master_state.hp2_d = 1.0f / (1.0f + 1.41421356f * g2 + g2 * g2); }
#if BR_FX_MASTER_STABLE_FASTPATH && BR_FX_MASTER_COEFF_CACHE
    br_master_state.stable_filter_alpha = 1u + filter + 128u * resonance;
#endif
}
#if defined(FM1_DEVICE) && !defined(FX_FAULT_TEST)
/* RAM -> XIP flash: pi32v2 needs an absolute indirect call. */
#define BR_MASTER_HP_COEFFS_INVOKE(...) \
    ({ __typeof__(&br_master_hp_coeffs) volatile hp_coeffs_fn_ = &br_master_hp_coeffs; \
       hp_coeffs_fn_(__VA_ARGS__); })
#else
#define BR_MASTER_HP_COEFFS_INVOKE(...) br_master_hp_coeffs(__VA_ARGS__)
#endif
static inline __attribute__((always_inline)) float
br_fx_master_q15_float(uint32_t value)
{
    return (float)value * (1.0f / 32768.0f);
}
static inline __attribute__((always_inline)) uint32_t br_fx_tape_alpha_full(void)
{
#if BR_FX_NATIVE_RATE_CALIBRATION
    return 13526u;
#elif BR_FX_NATIVE_RATE_48K
    return 9790u;
#else
    return 10500u;
#endif
}
static inline __attribute__((always_inline)) uint32_t br_fx_tape_alpha_rate2(void)
{
#if BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K
    uint32_t a = br_fx_tape_alpha_full();
    return 2u * a - ((a * a) >> 15);
#else
    return 17636u;
#endif
}


BR_AUDIO_API int32_t br_clip(int32_t x, int32_t low, int32_t high)
{ return x < low ? low : x > high ? high : x; }
static int16_t br_clip16(int32_t x) { return (int16_t)br_clip(x, -32767, 32767); }
static BR_FX_RAM_AUDIO int32_t br_fx_abs(int32_t x) { return x < 0 ? -x : x; }

static uint32_t br_fx_bound(uint32_t x, uint32_t hi)
{ return x > hi ? hi : x; }

/* Exact signed division by 2^shift with C truncation toward zero. */
static BR_FX_RAM_AUDIO int32_t br_fx_div_pow2(int32_t value, uint32_t shift)
{
#if BR_FX_DIV_POW2_BRANCHLESS
    uint32_t sign_mask = 0u - ((uint32_t)value >> 31);
    uint32_t bias = sign_mask & ((1u << shift) - 1u);
    return (value + (int32_t)bias) >> shift;
#else
    int32_t bias = value < 0 ? (int32_t)((1u << shift) - 1u) : 0;
    return (value + bias) >> shift;
#endif
}

static inline __attribute__((always_inline)) int32_t
br_fx_delay_feedback_q15(int32_t sample, uint32_t feedback)
{
    if (sample >= -32767 && sample <= 32767)
        return br_fx_div_pow2(sample * (int32_t)feedback, 15u);
    int32_t whole = sample / 32768;
    int32_t remainder = sample - whole * 32768;
    return whole * (int32_t)feedback +
        br_fx_div_pow2(remainder * (int32_t)feedback, 15u);
}

/* Loop protection on every value written to a delay line (send + feedback).
 * Exactly linear for |x| <= 16384, then a quadratic knee whose slope falls
 * from 1 to 0, held at +/-32752 from about |x| = 48300 (the line store rounds
 * 32760..32767 up to 32768, so this keeps every decoded value in int16). With
 * loop gain near 1 a sustained input cannot grow the line past full scale. */
static inline __attribute__((always_inline)) int32_t br_fx_delay_write_sat(int32_t x)
{
    uint32_t m = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    if (m <= 16384u) return x;
    m -= 16384u;
    m = m >= 32767u ? 32767u : 16383u + m - ((m * m) >> 16);
    /* 32752 is the largest magnitude the compact line store keeps below 32768
     * (it rounds 32760..32767 up to 32768), so a decoded line stays in int16. */
    if (m > 32752u) m = 32752u;
    return x < 0 ? -(int32_t)m : (int32_t)m;
}

/* Loop high-pass corner: 2*pi*60/line_rate in Q12 at storage shift 0 (the line
 * runs at FS/2 with the rate-2 delay). Longer delays store at FS/2^(1+shift);
 * the prepare step squares the complement once per shift to keep the corner. */
#if BR_FX_DELAY_RATE2
#define BR_FX_DELAY_HP_ALPHA_Q12 ((60u * 2u * 25736u + FS / 2u) / FS)
#else
#define BR_FX_DELAY_HP_ALPHA_Q12 ((60u * 25736u + FS / 2u) / FS)
#endif

/* echo - LP(echo): a first-order high-pass with its zero at DC and one state
 * word. |echo| <= 32767 (line writes are saturated, TAPE is clipped) and
 * alpha <= 2047 keep (16 * echo - lp) * alpha inside int32. */
static inline __attribute__((always_inline)) int32_t
br_fx_delay_loop_hp(int32_t *state, int32_t echo, uint32_t alpha_q12)
{
#if BR_FX_DELAY_LOOP_HPF
    /* Subtract the previous low-pass value: (1 - z^-1) / (1 - (1 - a) z^-1),
     * whose gain rises to 1/(1 - a/2) at Nyquist. The prepare step folds
     * (1 - a/2) into the feedback gain, so the loop gain above the corner is
     * the feedback law at every storage rate. Subtracting the updated value
     * instead lost a further (1 - a) per pass: 0.1 dB at full storage rate,
     * about 1 dB at the longest. */
    int32_t lp = *state;
    *state = lp + (((echo * 16 - lp) * (int32_t)alpha_q12) >> 12);
    return echo - (lp >> 4);
#else
    (void)state; (void)alpha_q12;
    return echo;
#endif
}

/* Exact signed /3 via a 32x32->64 reciprocal multiply. The quotient formula
 * is exact for every uint32 magnitude; restoring the sign matches C's
 * truncation toward zero, including INT32_MIN. Keep it opt-in until target
 * timing confirms the wide multiply beats the core's signed divide. */
static inline __attribute__((always_inline)) int32_t br_fx_div3(int32_t value)
{
#if BR_FX_DIV3_RECIP
    uint32_t magnitude = (uint32_t)value;
    uint32_t quotient;
    if (value < 0) magnitude = 0u - magnitude;
    quotient = (uint32_t)(((uint64_t)magnitude * 0xaaaaaaabu) >> 33);
    return value < 0 ? -(int32_t)quotient : (int32_t)quotient;
#else
    return value / 3;
#endif
}

/* Exact unsigned /5 through a reciprocal multiply; the formula is exact for
 * all uint32 inputs. Fuzz only calls it after proving its input nonnegative. */
static inline __attribute__((always_inline)) uint32_t br_fx_div5_u32(uint32_t value)
{
#if BR_FX_DIV5_RECIP
    return (uint32_t)(((uint64_t)value * 0xcccccccdu) >> 34);
#else
    return value / 5u;
#endif
}

/* Exact signed /10 for every int32 value, including INT32_MIN. The wide
 * multiply reciprocal avoids a runtime divider when the target benefits. */
static inline __attribute__((always_inline)) int32_t br_fx_div10(int32_t value)
{
#if BR_FX_DIV10_RECIP
    uint32_t magnitude = (uint32_t)value;
    uint32_t quotient;
    if (value < 0) magnitude = 0u - magnitude;
    quotient = (uint32_t)(((uint64_t)magnitude * 0xcccccccdu) >> 35);
    return value < 0 ? -(int32_t)quotient : (int32_t)quotient;
#else
    return value / 10;
#endif
}

/* The wow path divides tri*depth by 1016 after cancelling the original
 * numerator/denominator factor of 16. br_fx_tri() returns [-32767,32767]
 * and the caller clamps depth to [0,127], so its product magnitude is at most
 * 4161409. Keep the helper safe for other callers with a conservative bound. */
static inline __attribute__((always_inline)) int32_t br_fx_wow_div1016(int32_t value)
{
#if BR_FX_MASTER_WOW_DIV_RECIP
    uint32_t magnitude = (uint32_t)value;
    uint32_t quotient;
    if (value < 0) magnitude = 0u - magnitude;
    if (magnitude > 4161663u) return value / 1016;
    /* 1016 is exactly 8*127. The bounded /127 series is exact through
     * 0xffffff; this helper's caller domain after the shift is below that.
     * Retain the full helper contract with the original divider fallback. */
    {
        uint32_t divided = magnitude >> 3;
        uint32_t remainder;
        quotient = (divided >> 7) + (divided >> 14) + (divided >> 21);
        remainder = divided - quotient * 127u;
        quotient += 3u - ((remainder - 127u) >> 31) -
                    ((remainder - 254u) >> 31) -
                    ((remainder - 381u) >> 31);
    }
    return value < 0 ? -(int32_t)quotient : (int32_t)quotient;
#else
    return value / 1016;
#endif
}

/* The master-wow caller supplies tri*depth with tri in [-32767,32767] and
 * depth in [0,127]. That gives |product| <= 4161409; the /1016 reciprocal
 * therefore produces |delta| <= 4095. Compose the same exact reciprocals
 * directly so this callsite does not retain the general helper fallbacks. */
#if BR_FX_MASTER_WOW_BOUNDED_RECIP
static inline __attribute__((always_inline)) int32_t
br_fx_master_wow_rate_bounded(int32_t product)
{
    uint32_t magnitude = product < 0 ? 0u - (uint32_t)product : (uint32_t)product;
    uint32_t delta = (uint32_t)(((uint64_t)magnitude * 1082196485u) >> 40);
    uint32_t numerator = delta * 320u + 220u;
    uint32_t scaled = (uint32_t)(((uint64_t)numerator * 9739155u) >> 32);
    return product < 0 ? -(int32_t)scaled : (int32_t)scaled;
}
#endif

/* The tail fade is a bounded gain ramp: remaining is below FS/4 and the
 * numerator is at most (FS/4-1)*127. A Q35 reciprocal is exact for the
 * supported 44.1 and 48 kHz rates over that complete domain, while replacing
 * the division in each active tail frame. */
static inline __attribute__((always_inline)) uint32_t br_fx_tail_gain(uint32_t remaining)
{
    const uint32_t fade_samples = FS / 4u;
    if (remaining >= fade_samples) return 127u;
#if BR_FX_TAIL_GAIN_RECIP_32K && FS == 32000u
    const uint32_t numerator = remaining * 127u;
    /* ceil(2^32 / 8000) is exact for every legal numerator (0..1,015,873). */
    return (uint32_t)(((uint64_t)numerator * 536871u) >> 32);
#elif BR_FX_TAIL_GAIN_RECIP && (FS == 44100u || FS == 48000u)
    const uint64_t reciprocal = (((uint64_t)1u << 35) + (FS / 4u) - 1u) /
                                (FS / 4u);
    const uint32_t numerator = remaining * 127u;
    return (uint32_t)(((uint64_t)numerator * reciprocal) >> 35);
#else
    return (remaining * 127u) / fade_samples;
#endif
}

#if BR_FX_STAGE_EMIT_TAIL_BOUNDARY
/* Caller proves remaining < FS/4, so avoid repeating br_fx_tail_gain's
 * unity-boundary comparison in the StageEmit fade branch. */
static inline __attribute__((always_inline)) uint32_t
br_fx_tail_gain_below_fade(uint32_t remaining)
{
#if BR_FX_TAIL_GAIN_RECIP_32K && FS == 32000u
    const uint32_t numerator = remaining * 127u;
    /* This helper's caller guarantees remaining < FS/4 (8,000 at 32 kHz). */
    return (uint32_t)(((uint64_t)numerator * 536871u) >> 32);
#elif BR_FX_TAIL_GAIN_RECIP && (FS == 44100u || FS == 48000u)
    const uint64_t reciprocal = (((uint64_t)1u << 35) + (FS / 4u) - 1u) /
                                (FS / 4u);
    const uint32_t numerator = remaining * 127u;
    return (uint32_t)(((uint64_t)numerator * reciprocal) >> 35);
#else
    return (remaining * 127u) / (FS / 4u);
#endif
}
#endif

/* Bounded interpolation for the int16 delay/modulation memories. The delta
 * form uses one product instead of two; its truncation can differ from the
 * weighted form by at most one audio LSB, so it stays opt-in. */
static inline __attribute__((always_inline)) int32_t
br_fx_interp_q8_i16(int32_t a, int32_t b, uint32_t fraction)
{
#if BR_FX_INTERP_DELTA_1MUL
    return a + br_fx_div_pow2((b - a) * (int32_t)fraction, 8u);
#else
    return br_fx_div_pow2(a * (int32_t)(256u - fraction) +
                          b * (int32_t)fraction, 8u);
#endif
}

/* The master wow delay stores shaped full-range samples. When both samples
 * fit in this bound, the difference product fits signed 32-bit and the delta
 * interpolation removes one multiply. Its rounding differs from the
 * weighted form by at most one LSB. Keep the original exact path as fallback
 * and leave this approximation opt-in. */
static inline __attribute__((always_inline)) int32_t
br_fx_interp_q8_master(int32_t a, int32_t b, uint32_t fraction)
{
#if BR_FX_MASTER_INTERP_DELTA_1MUL
    if (fraction <= 255u && a >= -4000000 && a <= 4000000 &&
        b >= -4000000 && b <= 4000000) {
        int32_t delta = b - a;
        return a + br_fx_div_pow2(delta * (int32_t)fraction, 8u);
    }
#endif
    return br_fx_div_pow2(a * (int32_t)(256u - fraction) +
                          b * (int32_t)fraction, 8u);
}

static BR_FX_RAM_AUDIO int32_t br_fx_mul_q15(int32_t x, uint32_t q)
{
    /* Split the unsigned magnitude at bit 15 so even INT32_MIN can be
     * multiplied exactly without a 64-bit product. Phaser's phase endpoint
     * can produce q > 32768; its bounded audio values use this exact fallback.
     * This matches truncation toward zero for the signed Q15 product. */
    uint32_t magnitude = (uint32_t)x;
    uint32_t result;
    if (x < 0) magnitude = 0u - magnitude;
    /* Phaser audio and the wet filters normally remain in this bounded range.
     * The full product still fits uint32_t at both inclusive limits, reducing
     * the hot common case to one multiply while retaining the exact fallback. */
    if (magnitude <= 131071u && q <= 32768u) {
        result = (magnitude * q) >> 15;
        return x < 0 ? -(int32_t)result : (int32_t)result;
    }
    result = (magnitude >> 15) * q + (((magnitude & 32767u) * q) >> 15);
    return x < 0 ? -(int32_t)result : (int32_t)result;
}

#define BR_FX_REVERB_COMPACT_MAX 8386560u

/* Signed compact ring storage: |x| < 4096 stays exact, while the
 * upper range uses an 11-bit rounded mantissa and shared exponent. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) int16_t
br_fx_reverb_compress16(int32_t x)
{
    union { float f; uint32_t u; } bits;
    uint32_t magnitude = (uint32_t)x;
    uint32_t code_mag;
    if (x < 0) magnitude = 0u - magnitude;
    if (magnitude > BR_FX_REVERB_COMPACT_MAX)
        magnitude = BR_FX_REVERB_COMPACT_MAX;
    if (magnitude < 4096u) code_mag = magnitude;
    else {
        bits.f = (float)magnitude;
        code_mag = ((bits.u + 2048u) >> 12u) - (137u << 11u);
    }
    /* The signed compact code leaves the exact low range as ordinary int16. */
    return (int16_t)(x < 0 ? -(int32_t)code_mag : (int32_t)code_mag);
}

/* The overwhelmingly common small sample is stored bit-exactly. Let hot
 * writers bypass the out-of-line floating-point compander in that domain. */
static inline __attribute__((always_inline)) int16_t
br_fx_reverb_compress16_fast(int32_t x)
{
    if (x > -4096 && x < 4096) return (int16_t)x;
    return br_fx_reverb_compress16(x);
}

/* Always inline the decoder in the bounded fractional tap leaf. Its call-free
 * code size is checked by the production RAM-symbol guard. */
static inline __attribute__((always_inline)) int32_t
br_fx_reverb_decompress16_inline(int16_t stored)
{
    union { float f; uint32_t u; } bits;
    int32_t signed_code = (int32_t)stored;
    uint32_t magnitude = signed_code < 0 ? (uint32_t)(-signed_code) :
        (uint32_t)signed_code;
    int32_t decoded;
    if (magnitude < 4096u) return signed_code;
    bits.u = (magnitude + (137u << 11u)) << 12u;
    decoded = (int32_t)bits.f;
    /* Branch-free sign restore keeps the bounded SIZE tap leaf call-free. */
    int32_t sign = signed_code < 0 ? -1 : 0;
    return (decoded ^ sign) - sign;
}

static BR_FX_RAM_AUDIO __attribute__((noinline)) int32_t
br_fx_reverb_decompress16(int16_t stored)
{
    return br_fx_reverb_decompress16_inline(stored);
}

static inline __attribute__((always_inline)) int32_t
br_fx_reverb_decompress16_fast(int16_t stored)
{
    if (stored > -4096 && stored < 4096) return (int32_t)stored;
    return br_fx_reverb_decompress16(stored);
}

static inline __attribute__((always_inline)) int32_t
br_fx_reverb_lerp_q8_companded(int32_t a, int32_t b, uint32_t fraction)
{
    int32_t weighted = a * (int32_t)(256u - fraction) +
                       b * (int32_t)fraction;
    return br_fx_div_pow2(weighted, 8u);
}


#if BR_FX_PHASER_MUL_Q15_APPROX32
/* Phaser-only fast path. br_fx_tri() stays in the symmetric range
 * [-32767,32767], so its unsigned coefficient map cannot wrap at the LFO
 * endpoint. Restrict the signed product to q<=32768 as well as |x|<=65535 so
 * it is guaranteed to fit int32. Other coefficients use the exact helper.
 * Arithmetic shift rounds negative fractional results down instead of toward
 * zero, so the bounded fast path differs by at most one output LSB. */
static inline __attribute__((always_inline)) int32_t
br_fx_mul_q15_phaser_approx32(int32_t x, uint32_t q)
{
    if ((uint32_t)x + 65535u <= 131070u && q <= 32768u)
        return (x * (int32_t)q) >> 15;
    return br_fx_mul_q15(x, q);
}
#endif

#if BR_FX_PHASER_MUL_Q15_ASR64 && BR_FX_PHASER_MUL_Q15_APPROX32
/* Alternative phaser code-generation experiment. Match the existing
 * approx32 fast-path domain exactly; outside it, use the exact prior helper.
 * The wide multiply therefore makes no phaser-state or PCM changes. */
static inline __attribute__((always_inline)) int32_t
br_fx_mul_q15_phaser_asr64(int32_t x, uint32_t q)
{
    if ((uint32_t)x + 65535u <= 131070u && q <= 32768u)
        return br_fx_mul_q15_asr64_bounded(x, q);
    return br_fx_mul_q15(x, q);
}
#define br_fx_mul_q15_phaser_selected br_fx_mul_q15_phaser_asr64
#elif BR_FX_PHASER_MUL_Q15_APPROX32
#define br_fx_mul_q15_phaser_selected br_fx_mul_q15_phaser_approx32
#else
#define br_fx_mul_q15_phaser_selected br_fx_mul_q15
#endif


static BR_FX_RAM_AUDIO int32_t br_fx_wet_gain(int32_t x, uint32_t gain)
{
    if (gain == 0u) return 0;
    if (gain == 127u) {
        /* q=32766=32768-2. For m=abs(x), trunc0(x*q/32768) is
         * sign(x) * (m-ceil(m/16384)). Unsigned magnitude handles INT32_MIN. */
        uint32_t magnitude = (uint32_t)x;
        uint32_t loss;
        int32_t result;
        if (x < 0) magnitude = 0u - magnitude;
        loss = (magnitude + 16383u) >> 14;
        result = (int32_t)(magnitude - loss);
        return x < 0 ? -result : result;
    }
    return br_fx_mul_q15(x, gain * 258u);
}


static BR_FX_RAM_AUDIO int br_fx_any_enabled(const br_sound_t *s, const br_fx_config_t *cfg)
{
    return s->delay || s->reverb || cfg->mod_mix || s->drive;
}

/* Inline into the selected snapshot region; never emit a cross-region call. */
static inline __attribute__((always_inline)) void br_master_cache_heat(uint32_t heat_type,
                                                 uint32_t heat_amount)
{
    if (heat_type >= BR_HEAT_COUNT) {
        heat_type = BR_HEAT_BIT;
        heat_amount = 0u;
    }
    if (heat_amount > 127u) heat_amount = 127u;
    br_master_cached.heat_type = heat_type;
    br_master_cached.heat_amount = heat_amount;
    br_master_cached.saturation = (heat_type == BR_HEAT_SAT || heat_type == BR_HEAT_WOW) ? heat_amount : 0u;
    br_master_cached.wow = heat_type == BR_HEAT_WOW ? heat_amount : 0u;
}

/* Shared snapshot: initialization clears the pipeline pointer before using it. */
static BR_FX_MASTER_PREPARE_CODE void br_master_snapshot_ram(void)
{
#if !BR_AUDIO_FUSION_ONLY_BUILD
    if (br_fx_pipeline_controls) {
        const br_master_fx_t *s = &br_fx_pipeline_controls->master_fx;
        br_master_cache_heat(s->heat_type, s->heat_amount);
        br_master_cached.filter = s->filter > 127u ? 127u : s->filter;
        br_master_cached.resonance = s->resonance > 127u ? 127u : s->resonance;
        br_master_cached.filter_snap_generation = s->filter_snap_generation;
        return;
    }
#endif
    uint32_t before = br_master.revision;
    uint32_t filter, heat_type, heat_amount, resonance, snap_generation, after;
    if (before & 1u) return;
    filter = br_master.fx.filter;
    heat_type = br_master.fx.heat_type;
    heat_amount = br_master.fx.heat_amount;
    resonance = br_master.fx.resonance;
    snap_generation = br_master.fx.filter_snap_generation;
    after = br_master.revision;
    if (before != after || (after & 1u)) return;
    br_master_cached.filter = filter > 127u ? 127u : filter;
    br_master_cache_heat(heat_type, heat_amount);
    br_master_cached.resonance = resonance > 127u ? 127u : resonance;
    br_master_cached.filter_snap_generation = snap_generation;
}

/* Pipeline snapshots already contain their own block's modulation. */
static BR_FX_MASTER_PREPARE_CODE __attribute__((noinline)) void br_master_apply_lfo(void)
{
#if !BR_AUDIO_FUSION_ONLY_BUILD
    if (br_fx_pipeline_controls) return;
#endif
    uint32_t type = br_global_lfo_value(BR_GLOBAL_TARGET_HEAT_TYPE, br_master_cached.heat_type, BR_HEAT_COUNT - 1u);
    uint32_t amount = br_global_lfo_value(BR_GLOBAL_TARGET_HEAT_AMOUNT, br_master_cached.heat_amount, 127u);
    br_master_cached.filter = br_global_lfo_value(BR_GLOBAL_TARGET_FILTER, br_master_cached.filter, 127u);
    br_master_cached.resonance = br_global_lfo_value(BR_GLOBAL_TARGET_RESONANCE, br_master_cached.resonance, 127u);
    br_master_cache_heat(type, amount);
}

static inline __attribute__((always_inline)) const br_sound_t *br_fx_sound_at(uint32_t p)
#if BR_AUDIO_FUSION_ONLY_BUILD
{ return &br_effective_sound[p]; }
#else
{ return br_fx_pipeline_controls ? &br_fx_pipeline_controls->sound[p] : &br_effective_sound[p]; }
#endif
static inline __attribute__((always_inline)) const br_fx_config_t *br_fx_config_at(uint32_t p)
#if BR_AUDIO_FUSION_ONLY_BUILD
{ return &br_effective_fx[p]; }
#else
{ return br_fx_pipeline_controls ? &br_fx_pipeline_controls->fx[p] : &br_effective_fx[p]; }
#endif

#if !BR_AUDIO_FUSION_ONLY_BUILD
void br_fx_capture_pipeline_controls(br_pipeline_controls_t *controls)
{
    uint32_t p, before, after;
    if (!controls) return;
    for (p = 0; p < BR_PARTS; ++p) {
        controls->sound[p] = br_effective_sound[p];
        controls->fx[p] = br_effective_fx[p];
    }
    before = br_master.revision;
    if (!(before & 1u)) {
        br_master_fx_t master = br_master.fx;
        after = br_master.revision;
        if (before == after && !(after & 1u)) {
            br_pipeline_last_master = master;
        }
    }
    controls->master_fx = br_pipeline_last_master;
    controls->master_fx.filter = br_global_lfo_value(BR_GLOBAL_TARGET_FILTER, controls->master_fx.filter, 127u);
    controls->master_fx.resonance = br_global_lfo_value(BR_GLOBAL_TARGET_RESONANCE, controls->master_fx.resonance, 127u);
    controls->master_fx.heat_amount = br_global_lfo_value(BR_GLOBAL_TARGET_HEAT_AMOUNT, controls->master_fx.heat_amount, 127u);
    controls->master_fx.heat_type = br_global_lfo_value(BR_GLOBAL_TARGET_HEAT_TYPE, controls->master_fx.heat_type, BR_HEAT_COUNT - 1u);
    controls->reverb_size = (uint8_t)br_global_lfo_value(BR_GLOBAL_TARGET_REVERB_SIZE, br_master.reverb_size, 127u);
    controls->reverb_decay = (uint8_t)br_global_lfo_value(BR_GLOBAL_TARGET_REVERB_DECAY, br_master.reverb_decay, 127u);
    controls->reverb_lfo_active = br_master.global_lfo.depth &&
        (br_master.global_lfo.target == BR_GLOBAL_TARGET_REVERB_SIZE || br_master.global_lfo.target == BR_GLOBAL_TARGET_REVERB_DECAY);
    controls->compressor = br_source_compressor;
    controls->revision = br_source_compressor_revision;
}
#endif

static BR_FX_RAM_AUDIO uint32_t br_master_slew(uint32_t current, uint32_t target)
{
    if (current < target) return current + 1u;
    if (current > target) return current - 1u;
    return current;
}

#if BR_FX_MASTER_Q15_MIX_GAIN
#if BR_FX_MASTER_Q15_MIX_GAIN_TEST_COUNTERS
static uint32_t br_master_q15_fast_calls;
static uint32_t br_master_q15_fallback_calls;
#endif

/* Master controls are block-stable on this path. Round their linear 0..127
 * mix to Q15 once before the frame loop; exact endpoints remain exact. */
static inline __attribute__((always_inline)) uint32_t
br_master_q15_mix_gain(uint32_t mix)
{
    return (mix * 32768u + 63u) / 127u;
}

/* The bounded product avoids the wide /127 helper for typical audio deltas.
 * Large values retain the existing exact implementation. */
static inline __attribute__((always_inline)) int32_t
br_master_q15_mix(int32_t delta, uint32_t mix, uint32_t gain_q15)
{
    if (!mix) return 0;
    if (mix >= 127u) return delta;
    if ((uint32_t)delta + 65535u <= 131070u) {
        int32_t product = delta * (int32_t)gain_q15;
#if BR_FX_MASTER_Q15_MIX_GAIN_TEST_COUNTERS
        ++br_master_q15_fast_calls;
#endif
        return (product + ((int32_t)(0u - ((uint32_t)product >> 31)) & 32767)) >> 15;
    }
#if BR_FX_MASTER_Q15_MIX_GAIN_TEST_COUNTERS
    ++br_master_q15_fallback_calls;
#endif
    return br_fx_blend127_wide(delta, mix);
}
#endif

#if BR_FX_FEEDFORWARD_Q15_ASR64
static inline __attribute__((always_inline)) int32_t
br_master_q15_mix_feedforward_asr64(int32_t delta, uint32_t gain_q15)
{
    /* This helper is used only for the post-delay wow mix. Its cached Q15
     * coefficient is in [0, 32768], and this result is written to output only. */
    if ((uint32_t)delta + 65535u <= 131070u)
        return (delta * (int32_t)gain_q15) >> 15;
    return br_fx_mul_q15_asr64_bounded(delta, gain_q15);
}

static inline __attribute__((always_inline)) int32_t
br_master_wet_gain_feedforward_asr64(int32_t x, uint32_t gain)
{
    /* Wow-tail gain is in [0,127], so gain*258 is in [0,32766]. */
    if (gain == 127u) {
        uint32_t magnitude = (uint32_t)x;
        uint32_t loss;
        int32_t result;
        if (x < 0) magnitude = 0u - magnitude;
        loss = x < 0 ? magnitude >> 14 : (magnitude + 16383u) >> 14;
        result = (int32_t)(magnitude - loss);
        return x < 0 ? -result : result;
    }
    return br_fx_mul_q15_asr64_bounded(x, gain * 258u);
}
#endif

#include "instrument_compressor.c"

static BR_FX_RAM_AUDIO int32_t br_master_shape(int32_t input, uint32_t saturation
#if BR_FX_MASTER_COEFF_CACHE
    , uint32_t saturation_gain
#endif
    )
{
    int32_t x = input;
    if (saturation) {
#if BR_FX_MASTER_COEFF_CACHE
        uint32_t gain = saturation_gain;
#else
        uint32_t gain = 128u + saturation * 4u;
#endif
        int32_t dry = x;
        x = x < -65535 ? -65535 : x > 65535 ? 65535 : x;
        x = br_fx_div_pow2(x * (int32_t)gain, 7u);
        x = x < -65535 ? -65535 : x > 65535 ? 65535 : x;
        x = br_fx_softclip(x);
#if BR_FX_MASTER_Q15_MIX_GAIN
        if (br_master_saturation_mix_gain_q15)
            x = dry + br_master_q15_mix(x - dry, saturation,
                                        br_master_saturation_mix_gain_q15);
        else
#endif
        x = dry + br_fx_blend127_wide(x - dry, saturation);
    }
    return x;
}

static BR_FX_RAM_AUDIO int32_t br_master_bit_quantize(int32_t sample,
                                                       uint32_t shift)
{
    uint32_t magnitude = sample < 0 ? 0u - (uint32_t)sample : (uint32_t)sample;
    uint32_t quantized;
    if (!shift) return sample;
    if (shift > 30u) shift = 30u;
    quantized = ((magnitude + (1u << (shift - 1u))) >> shift) << shift;
    if (sample < 0) {
        if (quantized >= 0x80000000u) return INT32_MIN;
        return -(int32_t)quantized;
    }
    return quantized > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)quantized;
}

static BR_FX_RAM_AUDIO int32_t br_master_bit_blend(int32_t dry, int32_t wet,
                                                    uint32_t amount)
{
    int64_t delta = (int64_t)wet - (int64_t)dry;
    int64_t mixed;
    if (!amount) return dry;
    if (amount >= 127u) return wet;
#if BR_FX_MASTER_Q15_MIX_GAIN
    if (br_master_bit_mix_gain_q15 &&
        delta >= INT32_MIN && delta <= INT32_MAX)
        mixed = br_master_q15_mix((int32_t)delta, amount,
                                  br_master_bit_mix_gain_q15);
    else
#endif
    if (delta >= INT32_MIN && delta <= INT32_MAX)
        mixed = br_fx_blend127_wide((int32_t)delta, amount);
    else
        mixed = (delta * (int64_t)amount) / 127;
    mixed += dry;
    if (mixed > INT32_MAX) return INT32_MAX;
    if (mixed < INT32_MIN) return INT32_MIN;
    return (int32_t)mixed;
}

/* Measured on the unit (builds 1577 and 1579): caching the two divides alone saves
 * nothing; the 10 % BITCRUSH gain came from a 32-bit blend that cost about 300 B of
 * RAM code, which was given back. Off: the original code. */
#ifndef BR_FX_MASTER_BIT_CACHE
#define BR_FX_MASTER_BIT_CACHE 0
#endif
#if BR_FX_MASTER_BIT_CACHE
static uint32_t br_master_bit_amount_cached = 0xffffffffu;
static uint32_t br_master_bit_shift_cached, br_master_bit_period_cached;
#endif
/* One shared hold clock keeps the stereo sample-and-hold phase aligned.
 * Magnitude rounding makes the quantizer sign-symmetric, so silence has no
 * injected DC and identical L/R input remains identical. */
/* Keep the stereo BIT kernel compact while retaining RAM execution. */
static BR_FX_RAM_AUDIO __attribute__((noinline, minsize))
void br_master_bit_process(int32_t stereo[2], uint32_t amount)
{
    uint32_t period, shift, ch;
    int32_t dry[2] = {stereo[0], stereo[1]};
    if (!amount) {
        br_master_state.bit_hold_count = 0u;
        br_master_state.bit_hold[0] = br_master_state.bit_hold[1] = 0;
        return;
    }
#if BR_FX_MASTER_BIT_CACHE
    /* The two divides only change with the knob: cache them on the amount. */
    if (amount != br_master_bit_amount_cached) {
        br_master_bit_amount_cached = amount;
        br_master_bit_shift_cached = 1u + (amount * 7u + 63u) / 127u;
        br_master_bit_period_cached = 1u + (amount * 31u + 63u) / 127u;
    }
    shift = br_master_bit_shift_cached;
    period = br_master_bit_period_cached;
#else
    shift = 1u + (amount * 7u + 63u) / 127u;
    period = 1u + (amount * 31u + 63u) / 127u;
#endif
    if (br_master_state.bit_hold_count >= period)
        br_master_state.bit_hold_count = 0u;
    if (!br_master_state.bit_hold_count) {
        br_master_state.bit_hold[0] = br_master_bit_quantize(dry[0], shift);
        br_master_state.bit_hold[1] = br_master_bit_quantize(dry[1], shift);
    }
    for (ch = 0u; ch < 2u; ++ch)
        stereo[ch] = br_master_bit_blend(dry[ch],
                                         br_master_state.bit_hold[ch], amount);
    if (++br_master_state.bit_hold_count >= period)
        br_master_state.bit_hold_count = 0u;
}

static BR_FX_RAM_AUDIO void br_master_heat_switch(uint32_t heat_type)
{
    if (br_master_state.heat_type == BR_HEAT_WOW) {
        /* A later return to WOW starts with fresh history without the
         * 512-word clear spike in the audio loop. */
        br_master_state.wow_tail = 0u;
        br_master_state.wow_valid_samples = 0u;
    }
    if (br_master_state.heat_type == BR_HEAT_BIT || heat_type == BR_HEAT_BIT) {
        br_master_state.bit_hold_count = 0u;
        br_master_state.bit_hold[0] = br_master_state.bit_hold[1] = 0;
    }
    br_master_state.heat_type = heat_type;
}

static BR_FX_RAM_AUDIO void br_master_heat_advance(void)
{
    if (br_master_state.heat_type != br_master_cached.heat_type) {
        if (br_master_state.heat_amount) {
            --br_master_state.heat_amount;
        } else {
            br_master_heat_switch(br_master_cached.heat_type);
        }
    } else {
        br_master_state.heat_amount = br_master_slew(
            br_master_state.heat_amount, br_master_cached.heat_amount);
    }
    br_master_state.saturation =
        (br_master_state.heat_type == BR_HEAT_SAT || br_master_state.heat_type == BR_HEAT_WOW) ?
            br_master_state.heat_amount : 0u;
    br_master_state.wow =
        br_master_state.heat_type == BR_HEAT_WOW ? br_master_state.heat_amount : 0u;
}

#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
static uint32_t br_paced_clear_active;
static void br_paced_clear_buffer(void *ptr, uint32_t size);
#endif

static void br_master_fx_init(void)
{
#if defined(FM1_DEVICE)
    /* XIP cannot encode a direct long branch to the RAM audio region. */
    typedef void (*snapshot_fn)(void);
    void *volatile snapshot_target = (void *)&br_master_snapshot_ram;
    ((snapshot_fn)snapshot_target)();
#else
    br_master_snapshot_ram();
#endif
#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
    if (br_paced_clear_active) br_paced_clear_buffer(br_master_wow_line, sizeof br_master_wow_line);
    else
#endif
        memset(br_master_wow_line, 0, sizeof br_master_wow_line);
    memset(&br_master_state, 0, sizeof br_master_state);
    br_master_state.filter = br_master_cached.filter;
    br_master_state.heat_type = br_master_cached.heat_type;
    br_master_state.heat_amount = br_master_cached.heat_amount;
    br_master_state.saturation = br_master_cached.saturation;
    br_master_state.wow = br_master_cached.wow;
    br_master_state.resonance = br_master_cached.resonance;
}

/* A full-range move takes about 250 ms; smaller moves take proportionally less.
 * A snap generation travels with the filter snapshot so the shortcut lands immediately. */
static struct {
    uint32_t knob_q8, target, remaining, env, snap_generation;
    int32_t step_q8;
    uint32_t valid;
} br_gf;
static BR_FX_MASTER_PREPARE_CODE void br_gf_apply(uint32_t n)
{
    uint32_t target = br_master_cached.filter, snap = br_master_cached.filter_snap_generation;
    int32_t depth = br_master.fx.env_depth, value, delta;
    if (target > 127u) target = 127u;
    if (depth < -127) depth = -127;
    if (depth > 127) depth = 127;
    if (!br_gf.valid || snap != br_gf.snap_generation) {
        br_gf.knob_q8 = target << 8; br_gf.target = target; br_gf.remaining = 0u;
        br_gf.snap_generation = snap; br_gf.valid = 1u;
    } else if (target != br_gf.target) {
        uint32_t magnitude, blocks, denominator;
        delta = (int32_t)(target << 8) - (int32_t)br_gf.knob_q8;
        magnitude = (uint32_t)(delta < 0 ? -delta : delta) >> 8;
        denominator = 127u * (n ? n : 1u);
        blocks = ((FS / 4u) * magnitude + denominator / 2u) / denominator;
        if (blocks < 1u) blocks = 1u;
        br_gf.target = target; br_gf.remaining = blocks;
        br_gf.step_q8 = delta / (int32_t)blocks;
    }
    if (br_gf.remaining) {
        br_gf.knob_q8 = (uint32_t)((int32_t)br_gf.knob_q8 + br_gf.step_q8);
        if (--br_gf.remaining == 0u) br_gf.knob_q8 = br_gf.target << 8;
    }
    if (br_gf_kick) { br_gf_kick = 0u; br_gf.env = 32767u; }
    else if (br_gf.env) { br_gf.env -= br_gf.env >> 5; if (br_gf.env < 64u) br_gf.env = 0u; }
    value = (int32_t)((br_gf.knob_q8 + 128u) >> 8) + (depth * (int32_t)br_gf.env / 32767);
    br_master_cached.filter = (uint32_t)(value < 0 ? 0 : value > 127 ? 127 : value);
}

static BR_FX_MASTER_PREPARE_CODE uint32_t br_master_fx_begin_block(uint32_t n)
{
    BR_FX_MASTER_PREPARE_INVOKE(br_master_snapshot_ram);
    BR_FX_MASTER_PREPARE_INVOKE(br_master_apply_lfo);
    br_gf_apply(n);
#if BR_FX_MASTER_STABLE_FASTPATH
    br_master_state.controls_stable =
        br_master_state.filter == br_master_cached.filter &&
        br_master_state.heat_type == br_master_cached.heat_type &&
        br_master_state.heat_amount == br_master_cached.heat_amount &&
        br_master_state.saturation == br_master_cached.saturation &&
        br_master_state.wow == br_master_cached.wow &&
        br_master_state.resonance == br_master_cached.resonance;
#if BR_FX_MASTER_Q15_MIX_GAIN
    br_master_saturation_mix_gain_q15 = 0u;
    br_master_wow_mix_gain_q15 = 0u;
    br_master_bit_mix_gain_q15 = 0u;
    if (br_master_state.controls_stable) {
        if (br_master_state.heat_type == BR_HEAT_SAT || br_master_state.heat_type == BR_HEAT_WOW)
            br_master_saturation_mix_gain_q15 =
                br_master_q15_mix_gain(br_master_state.heat_amount);
        if (br_master_state.heat_type == BR_HEAT_WOW)
            br_master_wow_mix_gain_q15 =
                br_master_q15_mix_gain(br_master_state.heat_amount);
        else if (br_master_state.heat_type == BR_HEAT_BIT)
            br_master_bit_mix_gain_q15 =
                br_master_q15_mix_gain(br_master_state.heat_amount);
    }
#endif
    if (br_master_state.controls_stable) {
#if BR_FX_MASTER_WOW_PHASE_CACHE
        br_master_state.stable_wow_phase_inc =
            br_fx_rate_phase(18000u + br_master_state.wow * 1200u);
#endif
#if BR_FX_MASTER_COEFF_CACHE
        uint32_t saturation = br_master_state.saturation;
        /* High-pass cache: resonance_alpha = g, resonance_mix = q + g,
         * filter_damping = d; stable_filter_alpha = the (filter, resonance) key. */
        if (br_master_state.filter > 64u && br_master_state.stable_filter_alpha !=
            1u + br_master_state.filter + 128u * br_master_state.resonance)
            br_master_hp_coeffs(br_master_state.filter, br_master_state.resonance,
                                &br_master_state.stable_resonance_alpha,
                                &br_master_state.stable_resonance_mix,
                                &br_master_state.stable_filter_damping);
        br_master_state.stable_saturation_gain = 128u + saturation * 4u;
#endif
    }
#endif
    /* Once neutral, the master path has no per-sample state to advance.
     * A 63 <-> 64 filter change settles on the first sample in either path. */
    if ((br_master_cached.filter == 63u || br_master_cached.filter == 64u) &&
        (br_master_state.filter == 63u || br_master_state.filter == 64u) &&
        !br_master_cached.saturation && !br_master_state.saturation &&
        !br_master_cached.wow && !br_master_state.wow && !br_master_state.wow_tail &&
        !br_master_cached.heat_amount && !br_master_state.heat_amount
#if BR_FILTER_LADDER
        && !br_master_state.ladder[0].mix && !br_master_state.ladder[1].mix
#endif
        ) {
        if (n) {
            br_master_state.filter = br_master_cached.filter;
            br_master_state.resonance = br_master_cached.resonance;
            br_master_state.filter_tail = 0;
            br_master_state.low[0] = br_master_state.low[1] = 0;
            br_master_state.svf_low[0] = br_master_state.svf_low[1] = 0;
            br_master_state.band[0] = br_master_state.band[1] = 0;
            br_master_state.hp2_low[0] = br_master_state.hp2_low[1] = 0;
            br_master_state.hp2_band[0] = br_master_state.hp2_band[1] = 0;
#if BR_FILTER_LADDER
            memset(br_master_state.ladder, 0, sizeof br_master_state.ladder);
#endif
        }
        return 0u;
    }
    return 1u;
}

#if BR_FILTER_LADDER
#ifndef BR_FX_MASTER_LADDER_HEADROOM_SHIFT
#define BR_FX_MASTER_LADDER_HEADROOM_SHIFT 1 /* the summed bus is hotter than one track: saturate 6 dB later */
#endif
/* Master low-pass (filter < 63), and its fade back to dry once the control
 * leaves the low-pass side. Runs from flash, like br_filter_ladder itself;
 * the RAM frame only tests and calls. Only the wet difference is scaled back,
 * so mix 0 returns the input bit-exactly. The one-pole/SVF state is held at
 * zero meanwhile, so a following high-pass starts from dry. Same 2 s tail as
 * the old low-pass: fade over the last FS/4, then zero the ladder. */
static __attribute__((noinline)) void
br_master_ladder_frame(const int32_t *input, int32_t *filtered, int32_t peak)
{
    uint32_t ch, filter = br_master_state.filter, fade, gain;
    /* Hold a high-pass target at 64 until the fade to dry is done, so the
     * high-pass then opens one step per sample from its widest setting, as
     * it does when entered from bypass. */
    if (filter > 64u) br_master_state.filter = 64u;
    br_master_state.low[0] = br_master_state.low[1] = 0;
    br_master_state.svf_low[0] = br_master_state.svf_low[1] = 0;
    br_master_state.band[0] = br_master_state.band[1] = 0;
            br_master_state.hp2_low[0] = br_master_state.hp2_low[1] = 0;
            br_master_state.hp2_band[0] = br_master_state.hp2_band[1] = 0;
    if (peak > 2) br_master_state.filter_tail = FS * 2u;
    else if (br_master_state.filter_tail) --br_master_state.filter_tail;
    fade = peak <= 2 && br_master_state.filter_tail < FS / 4u;
    /* Q15 copy of br_fx_wet_gain(x, tail gain); that one lives in RAM. */
    gain = fade ? br_fx_tail_gain(br_master_state.filter_tail) * 258u : 0u;
    for (ch = 0; ch < 2u; ++ch) {
        br_filter_t *f = &br_master_state.ladder[ch];
        int32_t x = input[ch] >> BR_FX_MASTER_LADDER_HEADROOM_SHIFT, y;
        if (x > 262144) x = 262144;
        if (x < -262144) x = -262144;
        if (!f->mix) f->low = x;
        y = br_filter_ladder(f, x, filter < 63u ? filter : 63u,
                             br_master_state.resonance);
        y = input[ch] + (int32_t)((uint32_t)(y - x) << BR_FX_MASTER_LADDER_HEADROOM_SHIFT);
        if (fade) {
            if (!br_master_state.filter_tail) {
                f->low = f->band = 0;
                f->ladder_s[0] = f->ladder_s[1] = f->ladder_s[2] = f->ladder_s[3] = 0;
            }
            y = (int32_t)(((int64_t)y * (int32_t)gain) >> 15);
        }
        filtered[ch] = y;
    }
}
#if defined(FM1_DEVICE) && !defined(FX_FAULT_TEST)
/* RAM -> XIP flash: pi32v2 needs an absolute indirect call. */
#define BR_MASTER_LADDER_INVOKE(...) \
    ({ __typeof__(&br_master_ladder_frame) volatile ladder_fn_ = &br_master_ladder_frame; \
       ladder_fn_(__VA_ARGS__); })
#else
#define BR_MASTER_LADDER_INVOKE(...) br_master_ladder_frame(__VA_ARGS__)
#endif
#endif

#if BR_FX_MASTER_FRAME_INLINE_FINISH
static inline __attribute__((always_inline)) void br_master_fx_process_frame(int32_t *stereo)
#else
static BR_FX_RAM_AUDIO void br_master_fx_process_frame(int32_t *stereo)
#endif
{
    uint32_t ch;
#if BR_FX_MASTER_STABLE_FASTPATH
    uint32_t filter, saturation, wow, resonance;
    if (!br_master_state.controls_stable
#if BR_FX_MASTER_MIDBLOCK_SETTLED
        && (br_master_state.filter != br_master_cached.filter ||
            br_master_state.heat_type != br_master_cached.heat_type ||
            br_master_state.heat_amount != br_master_cached.heat_amount ||
            br_master_state.saturation != br_master_cached.saturation ||
            br_master_state.wow != br_master_cached.wow ||
            br_master_state.resonance != br_master_cached.resonance)
#endif
        ) {
        br_master_state.filter = br_master_slew(
            br_master_state.filter, br_master_cached.filter);
        br_master_heat_advance();
        br_master_state.resonance = br_master_slew(
            br_master_state.resonance, br_master_cached.resonance);
#if BR_FX_MASTER_COEFF_CACHE
        br_master_state.stable_saturation_gain =
            128u + br_master_state.saturation * 4u;
        if (br_master_state.filter > 64u && br_master_state.stable_filter_alpha !=
            1u + br_master_state.filter + 128u * br_master_state.resonance)
            BR_MASTER_HP_COEFFS_INVOKE(br_master_state.filter, br_master_state.resonance,
                                       &br_master_state.stable_resonance_alpha,
                                       &br_master_state.stable_resonance_mix,
                                       &br_master_state.stable_filter_damping);
#endif
    }
    filter = br_master_state.filter;
    saturation = br_master_state.saturation;
    wow = br_master_state.wow;
    resonance = br_master_state.resonance;
#else
    uint32_t filter = br_master_state.filter =
        br_master_slew(br_master_state.filter, br_master_cached.filter);
    br_master_heat_advance();
    uint32_t saturation = br_master_state.saturation;
    uint32_t wow = br_master_state.wow;
    uint32_t resonance = br_master_state.resonance =
        br_master_slew(br_master_state.resonance, br_master_cached.resonance);
#endif
#if BR_FX_MASTER_COEFF_CACHE
#if BR_FX_MASTER_STABLE_FASTPATH
    uint32_t saturation_gain = br_master_state.stable_saturation_gain;
#else
    uint32_t saturation_gain = 128u + saturation * 4u;
#endif
#endif
#if BR_FX_MASTER_STABLE_FASTPATH && BR_FX_MASTER_COEFF_CACHE
    float hp_d = br_master_state.stable_filter_damping;
    float hp_g = br_master_state.stable_resonance_alpha;
    float hp_a = br_master_state.stable_resonance_mix;
#else
    float hp_g = 0.0f, hp_a = 0.0f, hp_d = 0.0f;
    if (filter > 64u) BR_MASTER_HP_COEFFS_INVOKE(filter, resonance, &hp_g, &hp_a, &hp_d);
#endif
    int32_t input[2], filtered[2], shaped[2], result[2];
    int32_t peak;
    input[0] = stereo[0]; input[1] = stereo[1];
    peak = br_fx_abs(input[0]);
    if (br_fx_abs(input[1]) > peak) peak = br_fx_abs(input[1]);
#if BR_FILTER_LADDER
    if (filter < 63u || br_master_state.ladder[0].mix)
        BR_MASTER_LADDER_INVOKE(input, filtered, peak);
    else
#endif
    if (filter == 63u || filter == 64u) {
        br_master_state.filter_tail = 0;
        br_master_state.low[0] = br_master_state.low[1] = 0;
        br_master_state.svf_low[0] = br_master_state.svf_low[1] = 0;
        br_master_state.band[0] = br_master_state.band[1] = 0;
            br_master_state.hp2_low[0] = br_master_state.hp2_low[1] = 0;
            br_master_state.hp2_band[0] = br_master_state.hp2_band[1] = 0;
        filtered[0] = input[0]; filtered[1] = input[1];
    } else {
        if (peak > 2) br_master_state.filter_tail = FS * 2u;
        else if (br_master_state.filter_tail) --br_master_state.filter_tail;
        /* High-pass (filter 65..127; the low-pass is the ladder above): a TPT
         * state-variable filter, band = s1 (band[]), low = s2 (svf_low[]).
         * Unconditionally stable, so the float states need no clamp. Keep one
         * stereo loop rather than duplicating it in RAM. */
#if defined(__clang__)
#pragma clang loop unroll(disable)
#endif
        for (ch = 0; ch < 2u; ++ch) {
            float s1 = br_master_state.band[ch], s2 = br_master_state.svf_low[ch];
            float hp = ((float)input[ch] - hp_a * s1 - s2) * hp_d;
            float v = hp_g * hp, band = v + s1;
            br_master_state.band[ch] = band + v;
            v = hp_g * band;
            br_master_state.svf_low[ch] = s2 + v + v;
            s1 = br_master_state.hp2_band[ch]; s2 = br_master_state.hp2_low[ch];
            hp = (hp - br_master_state.hp2_a * s1 - s2) * br_master_state.hp2_d;
            v = (hp_g * 0.5f) * hp; band = v + s1;
            br_master_state.hp2_band[ch] = band + v;
            v = (hp_g * 0.5f) * band; br_master_state.hp2_low[ch] = s2 + v + v;
            filtered[ch] = (int32_t)hp;
        }
        /* The 2 s tail: fade over its last FS/4, then zero. Outside the loop,
         * so the compiler does not version the loop on it. */
        if (peak <= 2 && br_master_state.filter_tail < FS / 4u) {
            if (!br_master_state.filter_tail) {
                br_master_state.svf_low[0] = br_master_state.svf_low[1] = 0;
                br_master_state.band[0] = br_master_state.band[1] = 0;
            br_master_state.hp2_low[0] = br_master_state.hp2_low[1] = 0;
            br_master_state.hp2_band[0] = br_master_state.hp2_band[1] = 0;
                filtered[0] = filtered[1] = 0;
            } else {
                uint32_t g = br_fx_tail_gain(br_master_state.filter_tail);
                filtered[0] = br_fx_wet_gain(filtered[0], g);
                filtered[1] = br_fx_wet_gain(filtered[1], g);
            }
        }
    }
    for (ch = 0; ch < 2u; ++ch)
        shaped[ch] = br_master_shape(filtered[ch], saturation
#if BR_FX_MASTER_COEFF_CACHE
            , saturation_gain
#endif
            );

    if (br_master_state.heat_type == BR_HEAT_BIT)
        br_master_bit_process(shaped, br_master_state.heat_amount);

    if (wow || br_master_state.wow_tail) {
        int32_t wow_peak = br_fx_abs(shaped[0]);
        if (br_fx_abs(shaped[1]) > wow_peak) wow_peak = br_fx_abs(shaped[1]);
        if (wow && wow_peak > 2) br_master_state.wow_tail = br_fx_rate_samples(256u);
        else if (br_master_state.wow_tail) --br_master_state.wow_tail;
#if BR_FX_MASTER_WOW_PHASE_CACHE
        br_master_state.wow_phase += br_master_state.controls_stable ?
            br_master_state.stable_wow_phase_inc :
            br_fx_rate_phase(18000u + wow * 1200u);
#else
        br_master_state.wow_phase += br_fx_rate_phase(18000u + wow * 1200u);
#endif
        {
            int32_t tri = br_fx_tri(br_master_state.wow_phase);
#if BR_FX_MASTER_WOW_BOUNDED_RECIP
            int32_t delta_q8 = br_fx_master_wow_rate_bounded(
                tri * (int32_t)wow);
#elif BR_FX_NATIVE_RATE_48K && BR_FX_MASTER_WOW_DIV_RECIP
            int32_t delta_q8 = br_fx_master_wow_rate_48_bounded(
                br_fx_wow_div1016(tri * (int32_t)wow));
#elif BR_FX_MASTER_WOW_DIV_RECIP
            int32_t delta_q8 = br_fx_rate_signed(
                br_fx_wow_div1016(tri * (int32_t)wow));
#else
            int32_t delta_q8 = br_fx_rate_signed(
                tri * (int32_t)wow * 16 / 16256);
#endif
            int32_t delay_q8 = (int32_t)br_fx_rate_q8(64u * 256u) + delta_q8;
            uint32_t delay = (uint32_t)delay_q8 >> 8;
            uint32_t frac = (uint32_t)delay_q8 & 255u;
            uint32_t read_pos = (br_master_state.wow_pos - delay) & 255u;
            uint32_t wow_fade = br_fx_rate_samples(64u);
#if BR_FX_MASTER_WOW_FADE_RECIP_32K && FS == 32000u
            uint32_t gain;
            if (br_master_state.wow_tail >= wow_fade) {
                gain = 127u;
            } else {
                if (wow_fade == 46u) {
                    /* For 0 <= n <= 45*127, ceil(2^24/46)=364723 gives
                     * floor(n/46) exactly. The largest product is
                     * 5715*364723=2,084,391,945, which fits uint32_t. */
                    uint32_t numerator = br_master_state.wow_tail * 127u;
                    gain = (numerator * 364723u) >> 24;
                } else gain = (br_master_state.wow_tail * 127u) / wow_fade;
            }
#else
            uint32_t gain = br_master_state.wow_tail >= wow_fade ? 127u :
                (br_master_state.wow_tail * 127u) / wow_fade;
#endif
            for (ch = 0; ch < 2u; ++ch) {
                int32_t a, b, delayed, wet;
                br_master_wow_line[ch][br_master_state.wow_pos] = shaped[ch];
                a = br_master_state.wow_valid_samples >= delay ?
                    br_master_wow_line[ch][read_pos] : 0;
                b = br_master_state.wow_valid_samples > delay ?
                    br_master_wow_line[ch][(read_pos - 1u) & 255u] : 0;
                delayed = br_fx_interp_q8_master(a, b, frac);
#if BR_FX_MASTER_Q15_MIX_GAIN
                if (br_master_wow_mix_gain_q15)
#if BR_FX_FEEDFORWARD_Q15_ASR64
                    wet = br_master_q15_mix_feedforward_asr64(
                        delayed - shaped[ch], br_master_wow_mix_gain_q15);
#else
                    wet = br_master_q15_mix(delayed - shaped[ch], wow,
                                            br_master_wow_mix_gain_q15);
#endif
                else
#endif
                wet = br_fx_blend127_wide(delayed - shaped[ch], wow);
#if BR_FX_FEEDFORWARD_Q15_ASR64
                result[ch] = shaped[ch] +
                    br_master_wet_gain_feedforward_asr64(wet, gain);
#else
                result[ch] = shaped[ch] + br_fx_wet_gain(wet, gain);
#endif
            }
            br_master_state.wow_pos = (br_master_state.wow_pos + 1u) & 255u;
            if (br_master_state.wow_valid_samples < 256u)
                ++br_master_state.wow_valid_samples;
        }
        if (!br_master_state.wow_tail && !wow) {
            br_master_state.wow_valid_samples = 0u;
            br_fx_clear32((br_fx_clear_word_t *)br_master_wow_line,
                          sizeof br_master_wow_line / 4u);
        }
    } else {
        result[0] = shaped[0]; result[1] = shaped[1];
    }
    stereo[0] = result[0];
    stereo[1] = result[1];
}

#if BR_FX_FINISH_MASTER_STREAM
/* Keep one copy of the sizeable always-inline per-frame kernel. The serial
 * fallback calls this once for the block; the stream worker calls it once per
 * published slice instead of making a function call for every sample. */
/* Settled silence: once the master filter, HEAT hold and WOW tail have all come to rest at zero, a slice of
 * zero input produces exactly zero output, so it is skipped. Measured on the unit with an off-centre filter
 * and HEAT on, the master section was most of the idle load. Only the WOW and bit-hold phases stop advancing
 * while skipped, which changes nothing audible. */
#ifndef BR_FX_MASTER_SILENT_SKIP
#define BR_FX_MASTER_SILENT_SKIP 1
#endif
static BR_FX_RAM_AUDIO __attribute__((noinline)) void
br_master_fx_process_range(int32_t *stereo, uint32_t first, uint32_t end)
{
    uint32_t i;
#if BR_FX_MASTER_SILENT_SKIP
    /* The ladder needs no test here: it only holds state while filter_tail
     * runs, and br_master_ladder_frame zeroes it when the tail expires. */
    if (!br_master_state.filter_tail && !br_master_state.wow_tail &&
        !(br_master_state.low[0] | br_master_state.low[1] |
          br_master_state.bit_hold[0] | br_master_state.bit_hold[1]) &&
        br_master_state.svf_low[0] == 0.0f && br_master_state.svf_low[1] == 0.0f &&
        br_master_state.band[0] == 0.0f && br_master_state.band[1] == 0.0f &&
        br_master_state.hp2_low[0] == 0.0f && br_master_state.hp2_low[1] == 0.0f &&
        br_master_state.hp2_band[0] == 0.0f && br_master_state.hp2_band[1] == 0.0f) {
        int32_t any = 0;
        for (i = 2u * first; i < 2u * end; ++i) any |= stereo[i];
        if (!any) return;
    }
#endif
    for (i = first; i < end; ++i)
        br_master_fx_process_frame(&stereo[2u * i]);
}
#endif

/* Master output limiter: the last thing done to the summed stereo mix before
 * master volume (the hard clip there stays as the backstop). Once per block:
 * peak over both channels, target gain T/peak (one divide), attack within the
 * block by a linear per-sample ramp from the previous gain, release about
 * 150 ms (5/128 of the cut per 256-frame block, plus 8 so it ends at exactly
 * unity). One gain for both channels keeps the stereo image. A soft knee then
 * catches the first block's overshoot: linear up to 24576, quadratic above,
 * reaching 32767 at 40960 and never more. A block whose peak is <= 24576 at
 * unity gain is left untouched (bit-identical), after one peak scan. */
#define BR_MASTER_LIMIT_KNEE 24576u
#define BR_MASTER_LIMIT_THRESHOLD 26000u
#if BR_MASTER_LEVELER
/* Output LEVELER (trial, -DBR_MASTER_LEVELER=1): a slow gain rider, boost
 * only, folded into the limiter's gain. Per block: the mean square of the
 * louder channel (the limiter's own scan, samples clamped to full scale),
 * smoothed over ~400 ms; the boost steps toward TARGET / level, clamped to
 * 0 .. MAX_BOOST, up at UP dB/s (not while a block is 3 dB louder than that),
 * down at DOWN dB/s (it holds within one step of the wanted value). Below GATE (smoothed level or this block) the boost
 * holds; after GATE_HOLD under it, it relaxes toward 0 dB at RELAX dB/s. The
 * limiter then sees peak x boost, so its ceiling is unchanged, and the one
 * combined gain (boost x limiter) ramps linearly across the block. Levels
 * are RMS on the full scale 32768. Tune by ear here: */
#define BR_MASTER_LEV_TARGET_RMS 6528u       /* -14 dBFS RMS (32768 x 10^(-14/20)) */
#define BR_MASTER_LEV_GATE_RMS 104u          /* -50 dBFS RMS */
#define BR_MASTER_LEV_MAX_BOOST 65380u       /* +6 dB (Q15, rounded down; <= 65535) */
#define BR_MASTER_LEV_UP_MDB_S 2000u         /* boost rises 2 dB/s */
#define BR_MASTER_LEV_DOWN_MDB_S 12000u      /* boost falls 12 dB/s */
#define BR_MASTER_LEV_RELAX_MDB_S 2000u      /* gated: back to 0 dB at 2 dB/s */
#define BR_MASTER_LEV_GATE_HOLD_MS 2000u     /* gated this long before relaxing */
#define BR_MASTER_LEV_WINDOW_MS 400u         /* level smoothing time constant */
/* Derived, per block. A step of r mdB/s is a Q16 factor 1 + r ln10/20000 x
 * BR_BLOCK/FS (7545 = 65536 ln10 / 20). */
#define BR_MASTER_LEV_STEP_Q16(mdb) \
    ((uint32_t)(((uint64_t)(mdb) * 7545u * BR_BLOCK) / ((uint64_t)FS * 1000u)))
#define BR_MASTER_LEV_WINDOW_Q13 \
    ((uint32_t)((8192ull * BR_BLOCK * 1000u) / ((uint64_t)FS * BR_MASTER_LEV_WINDOW_MS)))
#define BR_MASTER_LEV_HOLD_BLOCKS \
    ((uint32_t)(((uint64_t)BR_MASTER_LEV_GATE_HOLD_MS * FS) / (1000u * BR_BLOCK)))
/* Levels are mean squares in units of a^2 / 64 (the mean of (a >> 3)^2). */
#define BR_MASTER_LEV_GATE_MS (BR_MASTER_LEV_GATE_RMS * BR_MASTER_LEV_GATE_RMS / 64u)
/* boost^2 (Q14) x level / 256 against target^2 / 64 / 256 (Q14): 32-bit. */
#define BR_MASTER_LEV_WANT ((BR_MASTER_LEV_TARGET_RMS * BR_MASTER_LEV_TARGET_RMS / 64u >> 8) << 14)
#define BR_MASTER_LEV_OVER(b, ms) ((((b) * (b)) >> 16) * ((ms) >> 8))
_Static_assert(BR_BLOCK <= 256u, "leveler: 32-bit sums of (a >> 3)^2 hold 256 frames");
_Static_assert(BR_MASTER_LEV_MAX_BOOST <= 65535u, "leveler: Q15 product range");
_Static_assert(BR_MASTER_LEV_WINDOW_Q13 * 4095ull * 4095u < 0x80000000ull, "leveler: smoothing range");

/* Block-rate part (flash): leveler, then the limiter's target on the boosted
 * peak. Returns the ramp's start (Q30, biased to round up) and leaves the
 * per-frame step in lev_step; 0 = block untouched (all at unity). */
static __attribute__((noinline)) uint32_t
br_master_leveler_block(uint32_t peak, uint32_t sumsq, uint32_t n)
{
    br_master_state_t *s = &br_master_state;
    uint32_t boost = 32768u + s->lev_boost, old = s->limit_cut, cut, target = 0u, gain, peak_b;
    uint32_t blk = n ? sumsq / n : 0u, ms = s->lev_ms;
    uint32_t old_gain = (uint32_t)(32768 + s->lev_applied);
    int32_t d = (int32_t)((blk - ms) * BR_MASTER_LEV_WINDOW_Q13);
    /* One-pole, rounded away from the level so it always arrives. */
    ms += (uint32_t)(d > 0 ? (d + 8191) >> 13 : d >> 13);
    s->lev_ms = ms;
    if (ms < BR_MASTER_LEV_GATE_MS || blk < BR_MASTER_LEV_GATE_MS) {
        if (s->lev_quiet < BR_MASTER_LEV_HOLD_BLOCKS) {
            ++s->lev_quiet;
        } else {
            boost -= (boost * BR_MASTER_LEV_STEP_Q16(BR_MASTER_LEV_RELAX_MDB_S)) >> 16;
            if (boost < 32768u) boost = 32768u;
        }
    } else {
        uint32_t up = boost + ((boost * BR_MASTER_LEV_STEP_Q16(BR_MASTER_LEV_UP_MDB_S)) >> 16);
        uint32_t down = boost - ((boost * BR_MASTER_LEV_STEP_Q16(BR_MASTER_LEV_DOWN_MDB_S)) >> 16);
        s->lev_quiet = 0u;
        if (up > BR_MASTER_LEV_MAX_BOOST) up = BR_MASTER_LEV_MAX_BOOST;
        if (down < 32768u) down = 32768u;
        /* (boost x level)^2 against target^2: no divide, no square root.
         * Up only if this block, 3 dB down, agrees too (an onset after quiet
         * is not lifted while the smoothed level still lags). */
        if (up != boost && BR_MASTER_LEV_OVER(up, (blk >> 1) > ms ? blk >> 1 : ms) <= BR_MASTER_LEV_WANT)
            boost = up;
        else if (down != boost && BR_MASTER_LEV_OVER(down, ms) >= BR_MASTER_LEV_WANT) boost = down;
    }
    s->lev_boost = boost - 32768u;
    peak_b = (peak >> 15) * boost + (((peak & 32767u) * boost) >> 15);   /* exact, as the RAM loop */
    if (peak_b <= BR_MASTER_LIMIT_KNEE && !old && boost == 32768u && old_gain == 32768u) return 0u;
    if (peak_b > BR_MASTER_LIMIT_THRESHOLD)
        target = 32768u - (BR_MASTER_LIMIT_THRESHOLD << 15) / peak_b;
    cut = old - (old >> 5) - (old >> 7) - 8u;    /* release, as without the leveler */
    if ((int32_t)cut < (int32_t)target) cut = target;
    s->limit_cut = cut;
    gain = (boost * (32768u - cut)) >> 15;
    /* Q30 ramp: lands exactly on gain after BR_BLOCK frames (power of two). */
    s->lev_step = (gain - old_gain) * (32768u / BR_BLOCK);
    return (old_gain << 15) | 0x7fffu;
}
#if defined(FM1_DEVICE) && !defined(FX_FAULT_TEST)
/* RAM -> XIP flash: pi32v2 needs an absolute indirect call. */
#define BR_MASTER_LEVELER_INVOKE(...) \
    ({ __typeof__(&br_master_leveler_block) volatile leveler_fn_ = &br_master_leveler_block; \
       leveler_fn_(__VA_ARGS__); })
#else
#define BR_MASTER_LEVELER_INVOKE(...) br_master_leveler_block(__VA_ARGS__)
#endif

static BR_FX_RAM_AUDIO __attribute__((noinline)) void
br_master_limit_block(int32_t *stereo, uint32_t n)
{
    uint32_t i, a, peak = 0u, sl = 0u, sr = 0u, gain, step;
    n *= 2u;
    for (i = 0u; i < n; i += 2u) {
        a = stereo[i] < 0 ? 0u - (uint32_t)stereo[i] : (uint32_t)stereo[i];
        if (a > peak) peak = a;
        if (a > 32767u) a = 32767u;
        a >>= 3;
        sl += a * a;
        a = stereo[i + 1u] < 0 ? 0u - (uint32_t)stereo[i + 1u] : (uint32_t)stereo[i + 1u];
        if (a > peak) peak = a;
        if (a > 32767u) a = 32767u;
        a >>= 3;
        sr += a * a;
    }
    gain = BR_MASTER_LEVELER_INVOKE(peak, sl > sr ? sl : sr, n >> 1);
    if (!gain) return;
    step = br_master_state.lev_step;
    for (i = 0u; i < n; ++i) {
        int32_t x = stereo[i];
        uint32_t g;
        if (!(i & 1u)) gain += step;
        g = gain >> 15;
        a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
        /* Exact truncated Q15 product for any int32 magnitude (g <= 65535). */
        a = (a >> 15) * g + (((a & 32767u) * g) >> 15);
        if (a > BR_MASTER_LIMIT_KNEE + 16384u) a = BR_MASTER_LIMIT_KNEE + 16384u;
        if (a > BR_MASTER_LIMIT_KNEE) {
            uint32_t u = a - BR_MASTER_LIMIT_KNEE;
            a -= (u * u) >> 15;
            if (a > 32767u) a = 32767u;
        }
        stereo[i] = x < 0 ? -(int32_t)a : (int32_t)a;
    }
    br_master_state.lev_applied = (int32_t)(gain >> 15) - 32768;
}
#else
static BR_FX_RAM_AUDIO __attribute__((noinline)) void
br_master_limit_block(int32_t *stereo, uint32_t n)
{
    uint32_t i, a, peak = 0u, old = br_master_state.limit_cut, cut, gain, step, target = 0u;
    n *= 2u;
    for (i = 0u; i < n; ++i) {
        a = stereo[i] < 0 ? 0u - (uint32_t)stereo[i] : (uint32_t)stereo[i];
        if (a > peak) peak = a;
    }
    if (peak <= BR_MASTER_LIMIT_KNEE && !old) return;
    if (peak > BR_MASTER_LIMIT_THRESHOLD)
        target = 32768u - (BR_MASTER_LIMIT_THRESHOLD << 15) / peak;
    /* Release (old <= 32768, so a release past unity is at worst -8 signed). */
    cut = old - (old >> 5) - (old >> 7) - 8u;
    if ((int32_t)cut < (int32_t)target) cut = target;
    /* Gain in Q15 << 16, biased by 0xFFFF so it rounds up: the step lands
     * exactly on the target after BR_BLOCK frames when BR_BLOCK is a power of
     * two and within one unit otherwise, and a release always reaches unity. */
    gain = ((32768u - old) << 16) | 0xFFFFu;
    step = (old - cut) * (65536u / BR_BLOCK);
    for (i = 0u; i < n; ++i) {
        int32_t x = stereo[i];
        uint32_t g;
        if (!(i & 1u)) gain += step;
        g = gain >> 16;
        a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
        /* Exact truncated Q15 product for any int32 magnitude. */
        a = (a >> 15) * g + (((a & 32767u) * g) >> 15);
        if (a > BR_MASTER_LIMIT_KNEE + 16384u) a = BR_MASTER_LIMIT_KNEE + 16384u;
        if (a > BR_MASTER_LIMIT_KNEE) {
            uint32_t u = a - BR_MASTER_LIMIT_KNEE;
            a -= (u * u) >> 15;
            if (a > 32767u) a = 32767u;
        }
        stereo[i] = x < 0 ? -(int32_t)a : (int32_t)a;
    }
    br_master_state.limit_cut = 32768u - (gain >> 16);
}
#endif /* BR_MASTER_LEVELER */

#if !BR_FX_MASTER_FRAME_INLINE_FINISH || !defined(FM1_DEVICE)
static BR_FX_RAM_AUDIO void br_master_fx_process(int32_t *stereo, uint32_t n)
{
    uint32_t i;
    if (!BR_FX_MASTER_PREPARE_TARGET(br_master_fx_begin_block)(n)) return;
    for (i = 0; i < n; ++i) br_master_fx_process_frame(&stereo[2u * i]);
}
#endif

static BR_FX_RAM_AUDIO int32_t br_fx_pole(int32_t current, int32_t target, uint32_t alpha)
{
    int32_t delta = target - current;
    int32_t step = br_fx_mul_q15(delta, alpha);
    if (!step && delta) step = delta > 0 ? 1 : -1;
    return current + step;
}

/* Only tape prefilters use this narrower pole. Their state starts at zero and
 * stays within the input range, |echo| <= 32767. With alpha <= 31980 < 32768,
 * the update, including its minimum one-unit step, stays between its old
 * state and target. Thus |delta| <= 131070 and its unsigned product with
 * alpha is < 2^32. Restoring the sign after >>15 gives exactly trunc0 of
 * the original signed 64-bit multiply; no float or precision change. */
static BR_FX_RAM_AUDIO int32_t br_fx_pole_bounded(int32_t current, int32_t target, uint32_t alpha)
{
    int32_t delta = target - current;
    uint32_t magnitude = delta < 0 ? 0u - (uint32_t)delta : (uint32_t)delta;
    int32_t step = (int32_t)((magnitude * alpha) >> 15);
    if (delta < 0) step = -step;
    if (!step && delta) step = delta > 0 ? 1 : -1;
    return current + step;
}

#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
/* Fixed-data comparison of the two equivalent bounded audio poles. */
BR_AUDIO_API uint32_t __attribute__((noinline)) br_fx_bounded_pole_benchmark(uint32_t mode, uint32_t seed)
{
    uint32_t state = seed, sum = 0u, i;
    int32_t (* volatile pole)(int32_t, int32_t, uint32_t) =
        mode == 10u ? br_fx_pole : br_fx_pole_bounded;
    for (i = 0u; i < 50000u; ++i) {
        int32_t current, target;
        uint32_t alpha;
        state = state * 1664525u + 1013904223u;
        current = (int32_t)(state & 131071u) - 65535;
        if (current > 65535) current = 65535;
        state = state * 1664525u + 1013904223u;
        target = (int32_t)(state & 131071u) - 65535;
        if (target > 65535) target = 65535;
        state = state * 1664525u + 1013904223u;
        alpha = state & 32767u;
        if (alpha > 31980u) alpha = 31980u;
        sum += (uint32_t)pole(current, target, alpha);
    }
    return sum;
}

#endif

static BR_FX_RAM_AUDIO int32_t br_fx_softclip(int32_t x)
{
    return br_fx_softclip_table_bounded(x);
}

#if !BR_FX_DELAY_RATE2   /* the rate-2 TAPE saturates on the write instead */
/* Same rational curve as br_fx_softclip, with an algebraic wide-input path.
 * Tape can inherit expanded state when the user switches delay engines. */
static BR_FX_RAM_AUDIO int32_t br_fx_delay_softclip(int32_t x)
{
    uint32_t magnitude = x < 0 ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
    if (magnitude <= 65535u) return br_fx_softclip(x);
    uint32_t denominator = magnitude + 32767u;
    uint32_t correction = (1073676289u + denominator - 1u) / denominator;
    int32_t result = 32767 - (int32_t)correction;
    return x < 0 ? -result : result;
}
#endif

/* Symmetric triangle keeps both endpoints inside [-32767,32767]. */
static BR_FX_RAM_AUDIO int32_t br_fx_tri(uint32_t phase)
{
    uint32_t x = phase >> 16;
    if (x < 32768u) return (int32_t)(x * 2u) - 32767;
    return 98303 - (int32_t)(x * 2u);
}

/* MOD RATE: exponential in two segments pinned at the default 40, which keeps
 * the old law's 2.5655 Hz (phase step 249856 per 44.1 kHz sample) bit-exactly:
 * 0.05 Hz at 0 rising x1.10346 per step to 40, then x1.01576 per step to 10 Hz
 * at 127. Returns the LFO phase step per sample at 44.1 kHz (Q0.32 of a cycle);
 * the read-out shows it in Hz through the LFO page's formatter. */
BR_AUDIO_API __attribute__((noinline)) uint32_t br_mod_rate_inc(uint32_t rate)
{
    float inc = 249856.0f;
    if (rate > 127u) rate = 127u;
    for (; rate < 40u; ++rate) inc *= 0.906243777f;        /* 1 / 1.1034559 */
    for (; rate > 40u; --rate) inc *= 1.01576014f;
    return (uint32_t)(inc + 0.5f);
}

/* MOD DEPTH. The delay sweeps base + span (44.1 kHz samples) with a triangle,
 * so the pitch moves by +-x, x = 2 * span * f / FS = span * step / 2^31. For
 * CHORUS and VIBRATO the knob means that deviation: the span is set at the
 * default rate (CHORUS 1 + 4d, VIBRATO 8 + 7d, as before) and scaled by
 * f(40) / f(rate), so the cents hold as RATE moves, until the span reaches the
 * line (2044 samples less the base: CHORUS 1724, VIBRATO 1996, about 39 / 45 ms).
 * FLANGER keeps a fixed sweep (it is a comb sweep, not a pitch), PHASER has no
 * delay line; both stay in %. */
static uint32_t br_mod_base(uint32_t engine)
{
    return engine == BR_MOD_FLANGE ? 24u : engine == BR_MOD_VIBRATO ? 48u : 320u;
}
BR_AUDIO_API __attribute__((noinline)) uint32_t br_mod_depth_span(uint32_t engine, uint32_t depth, uint32_t rate)
{
    uint32_t span, inc, room = BR_FX_MOD_REACH - br_mod_base(engine);
    if (depth > 127u) depth = 127u;
    if (engine == BR_MOD_FLANGE) return 1u + depth * 4u;
    span = engine == BR_MOD_VIBRATO ? 8u + depth * 7u : 1u + depth * 4u;
    inc = br_mod_rate_inc(rate);
    span = (span * 249856u + inc / 2u) / inc;          /* <= 897 * 249856 */
    return span < room ? span : room;
}
/* Peak pitch deviation in cents (half the up/down span) for the read-out:
 * 600 log2((1 + x) / (1 - x)) = 1731.234 * (x + x^3/3 + ...), x in Q16 (x <= 0.11,
 * so the x^5 term is under 0.05 cent). span * step <= 1996 * 973919 < 2^32. */
BR_AUDIO_API uint32_t br_mod_depth_cents(uint32_t engine, uint32_t depth, uint32_t rate)
{
    uint32_t x = br_mod_depth_span(engine, depth, rate) * br_mod_rate_inc(rate) >> 15;
    uint32_t t = x * 1731u >> 8;                        /* cents, Q8 */
    return (t + ((t >> 4) * ((x * x >> 16) / 3u) >> 12) + 128u) >> 8;
}

static BR_FX_RAM_AUDIO uint32_t br_fx_mod_delay_q8(uint32_t base_q8, uint32_t span, int32_t tri)
{
    /* Algebraically (tri+32767)/65536 * span * 256, with 32-bit-safe order. */
    return base_q8 + (((uint32_t)(tri + 32767) * span) >> 8);
}

static BR_FX_RAM_AUDIO int32_t br_fx_mod(br_fx_track_t *st, uint32_t p, int32_t input,
                         const br_fx_controls_t *c)
{
#if BR_FX_MOD_RATE2
    uint32_t engine = c->mod_engine;
    uint32_t depth = c->mod_depth;
    uint32_t mix = c->mod_mix;
    uint32_t phase = st->mod_rate2_phase;
    int32_t bounded_input = br_fx_clip_ram(input, -65535, 65535);
    uint32_t lfo_phase = st->lfo_phase;
    int32_t reconstructed, wet = input;
    int32_t history0 = st->mod_rate2_input_history[0];
    int32_t history1 = st->mod_rate2_input_history[1];
    int32_t history2 = st->mod_rate2_input_history[2];

    /* Keep the phaser's four recursive allpass stages at native rate. */
    if (engine == BR_MOD_PHASER) {
        uint32_t i;
        int32_t tri = br_fx_tri(lfo_phase);
        int32_t a = 3500 + (int32_t)((((uint32_t)(tri + 32767)) *
            (5000u + depth * 110u)) >> 16);
        st->lfo_phase += c->mod_lfo_inc;
        wet = br_fx_clip_ram(wet + ((st->mod_feedback_last * (int32_t)c->mod_feedback) >> 8), -65535, 65535);
        for (i = 0u; i < 4u; ++i) {
            int32_t y = st->phase_state[i] -
                br_fx_mul_q15_phaser_selected(wet, (uint32_t)a);
            st->phase_state[i] = br_fx_clip_ram(
                wet + br_fx_mul_q15_phaser_selected(y, (uint32_t)a), -65535, 65535);
            wet = y;
        }
        st->mod_feedback_last = br_fx_clip_ram(wet, -65535, 65535);
        return mix ? input + BR_FX_BLEND127((wet - input), mix, c->mod_mix_gain)
                   : input;
    }

    /* The LFO stays at native rate for tape wow and other effects. */
    st->lfo_phase += c->mod_lfo_inc;
    st->mod_rate2_input_history[2] = history1;
    st->mod_rate2_input_history[1] = history0;
    st->mod_rate2_input_history[0] = bounded_input;
    if (phase == 2u) {
        /* Seed the interpolated wet output after reset or an engine switch. */
        st->mod_rate2_previous_output = input;
        st->mod_rate2_phase = 1u;
        return input;
    }
    if (phase == 0u) {
        reconstructed = st->mod_rate2_previous_output;
        st->mod_rate2_phase = 1u;
        return mix ? input + BR_FX_BLEND127((reconstructed - input), mix, c->mod_mix_gain)
                   : input;
    }

    /* Anti-alias only the wet modulation send before decimating by two. */
    {
        int32_t filtered_input = br_fx_div_pow2(bounded_input +
            3 * history0 + 3 * history1 + history2, 3u);
        int32_t tri = br_fx_tri(st->lfo_phase);
        uint32_t delay_q8, read_pos, frac;
        int32_t sample_a, sample_b;
        br_mod_line[p][st->mod_pos] = br_fx_clip16_ram(filtered_input + ((st->mod_feedback_last * (int32_t)c->mod_feedback) >> 8));
        delay_q8 = br_fx_mod_delay_q8(c->mod_base_q8, c->mod_span, tri) >> 1;
        if (delay_q8 >= BR_FX_MOD_SAMPLES * 256u)
            delay_q8 = (BR_FX_MOD_SAMPLES - 1u) * 256u;
        read_pos = (st->mod_pos - (delay_q8 >> 8)) & (BR_FX_MOD_SAMPLES - 1u);
        frac = delay_q8 & 255u;
        sample_a = br_mod_line[p][read_pos];
        sample_b = br_mod_line[p][(read_pos - 1u) & (BR_FX_MOD_SAMPLES - 1u)];
        wet = br_fx_interp_q8_i16(sample_a, sample_b, frac);
        st->mod_feedback_last = wet;
        if (engine == BR_MOD_FLANGE)
            wet -= br_fx_div_pow2(filtered_input, 1u);
        st->mod_pos = (st->mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
    }
    reconstructed = br_fx_div_pow2(st->mod_rate2_previous_output + wet, 1u);
    st->mod_rate2_previous_output = wet;
    st->mod_rate2_phase = 0u;
    return mix ? input + BR_FX_BLEND127((reconstructed - input), mix, c->mod_mix_gain)
               : input;
#else
    uint32_t engine = c->mod_engine;
    uint32_t depth = c->mod_depth;
    uint32_t mix = c->mod_mix;
    uint32_t phase = st->lfo_phase;
    int32_t wet = input;
    uint32_t delay_q8, read_pos, frac;
    int32_t sample_a, sample_b;
    st->lfo_phase += c->mod_lfo_inc;
    if (engine == BR_MOD_PHASER) {
        uint32_t i;
        int32_t tri = br_fx_tri(phase);
        int32_t a = 3500 + (int32_t)((((uint32_t)(tri + 32767)) * (5000u + depth * 110u)) >> 16);
        wet = input;
        for (i = 0; i < 4u; ++i) {
            int32_t y = st->phase_state[i] -
                br_fx_mul_q15_phaser_selected(wet, (uint32_t)a);
            st->phase_state[i] = br_fx_clip_ram(
                wet + br_fx_mul_q15_phaser_selected(y, (uint32_t)a), -65535, 65535);
            wet = y;
        }
    } else {
        br_mod_line[p][st->mod_pos] = br_fx_clip16_ram(input);
        /* A zero mix still advances the ring so later live enable starts from
         * the same history, but it cannot affect the output. Avoid the two
         * interpolation reads and arithmetic in that case. */
        if (!mix) {
            st->mod_pos = (st->mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
            return input;
        }
        delay_q8 = br_fx_mod_delay_q8(c->mod_base_q8, c->mod_span,
                                      br_fx_tri(phase));
        if (delay_q8 >= BR_FX_MOD_SAMPLES * 256u) delay_q8 = (BR_FX_MOD_SAMPLES - 1u) * 256u;
        read_pos = (st->mod_pos - (delay_q8 >> 8)) & (BR_FX_MOD_SAMPLES - 1u);
        frac = delay_q8 & 255u;
        sample_a = br_mod_line[p][read_pos];
        sample_b = br_mod_line[p][(read_pos - 1u) & (BR_FX_MOD_SAMPLES - 1u)];
        wet = br_fx_interp_q8_i16(sample_a, sample_b, frac);
        if (engine == BR_MOD_FLANGE) wet -= br_fx_div_pow2(input, 1u);
        st->mod_pos = (st->mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
    }
    /* Phaser state above remains warm while bypassed; only its zero-weight
     * output mix is unnecessary. */
    if (!mix) return input;
    return input + BR_FX_BLEND127((wet - input), mix, c->mod_mix_gain);
#endif
}

/* FEEDBACK: Q15 loop gain with the repeats to -60 dB growing geometrically,
 * from 1 at 1 to 224 at 127 (gain 0.9696, the old safe maximum), 0 at 0:
 * 16 -> 1.9, 32 -> 3.8, 48 -> 7.5, 64 -> 15, 80 -> 30, 96 -> 59, 112 -> 117.
 * Points every 8 steps, linear between (repeats within 5 % of the law); the
 * last point is placed so that 127 lands exactly on 31771. */
static const uint16_t br_fx_delay_feedback_q15_law[17] = {
    0u, 197u, 871u, 2500u, 5282u, 8979u, 13081u, 17084u, 20645u,
    23612u, 25972u, 27787u, 29152u, 30160u, 30896u, 31429u, 31820u
};
static BR_FX_PREPARE_CODE uint32_t br_fx_delay_feedback_law(uint32_t fb)
{
    const uint16_t *t = br_fx_delay_feedback_q15_law;
    uint32_t i;
    fb = br_fx_bound_ram(fb, 127u);
    i = fb >> 3;
    return t[i] + (((t[i + 1u] - t[i]) * (fb & 7u)) >> 3);
}

/* Delay controls that have no saved field yet: one value per track with a
 * fixed default, read when the track's controls are prepared. The session
 * format work connects them (WIDTH 0 = the mono line, TONE 127 = open,
 * WOW RATE 64 = 0.6 Hz). */
#ifndef BR_FX_DELAY_TONE_DEFAULT
#define BR_FX_DELAY_TONE_DEFAULT 127u
#endif
#ifndef BR_FX_DELAY_WOW_RATE_DEFAULT
#define BR_FX_DELAY_WOW_RATE_DEFAULT 64u
#endif
/* Phase increment per sample for 1 mHz. */
#define BR_FX_DELAY_MHZ_INC ((uint32_t)(4294967.296 / (double)FS + 0.5))

/* The TIME entry's length at the current tempo (the value text shows it). */
BR_AUDIO_API uint32_t br_fx_delay_samples(uint32_t index)
{
    return BR_FX_PREPARE_INVOKE(br_fx_delay_note_samples, br_fx_delay_note_store(index), br_bpm);
}
BR_AUDIO_API uint32_t br_fx_delay_choice_samples(uint32_t choice)
{
    uint32_t stored, bpm = br_bpm < BR_BPM_MIN ? BR_BPM_MIN : br_bpm > BR_BPM_MAX ? BR_BPM_MAX : br_bpm;
    uint8_t short_note;
    br_fx_delay_choice_set(choice, &stored, &short_note);
    if (short_note) return (FS * 5u * (short_note == 1u ? 4u : 9u) + 2u * bpm) / (4u * bpm);
    return BR_FX_PREPARE_INVOKE(br_fx_delay_note_samples, stored, bpm);
}

static BR_FX_RAM_AUDIO uint32_t br_fx_delay_samples_ram(uint32_t value)
{
    uint32_t delay;
    value = br_fx_bound_ram(value, 127u);
    delay = br_fx_rate_samples(32u + value * value);
    return delay >= BR_FX_DELAY_FULL_SAMPLES ? BR_FX_DELAY_FULL_SAMPLES - 1u : delay;
}

#if BR_FX_DELAY_RATE2 && BR_FX_DELAY_MAX_SHIFT
/* A TIME or tempo change that needs another storage rate keeps a ringing
 * tail: the line is resampled in place (oldest at delay_pos) instead of
 * cleared. Slower storage: each 2^k old samples are averaged into the newest
 * 1/2^k of the line, and the older part, which the old rate never held, is
 * continued with the tail's own period (what a longer line would have held,
 * one pass newer). Faster storage: the newest 1/2^k is interpolated over the
 * whole line. The processing order never overwrites a sample before it is
 * read. Runs once per crossing, from the block prepare step (flash). */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void
br_fx_delay_storage_convert(br_fx_track_t *st, uint32_t p, uint32_t shift)
{
    int16_t *line = BR_POLY_DELAY_LINE(p);
    const uint32_t n = BR_FX_DELAY_SAMPLES, mask = BR_FX_DELAY_SAMPLES - 1u;
    uint32_t pos = st->delay_pos, valid = st->delay_valid_samples, k, keep, j, m;
    /* The compander's slow paths live in RAM: reach them through absolute
     * pointers (pi32v2 cannot encode a direct call from XIP flash). */
    int32_t (*volatile dec_slow)(int16_t) = br_fx_reverb_decompress16;
    int16_t (*volatile enc_slow)(int32_t) = br_fx_reverb_compress16;
#define BR_FX_CONVERT_DEC(v) ((v) > -4096 && (v) < 4096 ? (int32_t)(v) : dec_slow(v))
#define BR_FX_CONVERT_DIV(v) (((v) + ((v) < 0 ? (int32_t)(1u << k) - 1 : 0)) >> k)
#define BR_FX_CONVERT_ENC(v) ((v) > -4096 && (v) < 4096 ? (int16_t)(v) : enc_slow(v))
    if (shift > st->delay_storage_shift) {
        uint32_t period = (st->delay_last_time + (1u << shift)) >> (shift + 1u);
        k = shift - st->delay_storage_shift;
        keep = n >> k;
        for (j = keep; j--; ) {
            int32_t sum = 0;
            for (m = 0u; m < (1u << k); ++m)
                sum += BR_FX_CONVERT_DEC(line[(pos + (j << k) + m) & mask]);
            {
                int32_t avg = BR_FX_CONVERT_DIV(sum);
                line[(pos + n - keep + j) & mask] = BR_FX_CONVERT_ENC(avg);
            }
        }
        valid >>= k;
        if (period && period <= valid) {
            for (m = n - valid; m--; )
                line[(pos + m) & mask] = line[(pos + m + period) & mask];
            valid = n;
        }
    } else {
        k = st->delay_storage_shift - shift;
        keep = n >> k;
        for (m = 0u; m < n; ++m) {
            uint32_t a = (n - 1u - m) >> k, fraction = (n - 1u - m) & ((1u << k) - 1u);
            int32_t x = BR_FX_CONVERT_DEC(line[(pos + n - 1u - a) & mask]);
            if (fraction)
            {
                int32_t step = (BR_FX_CONVERT_DEC(line[(pos + n - 2u - a) & mask]) - x) *
                    (int32_t)fraction;
                x += BR_FX_CONVERT_DIV(step);
            }
            line[(pos + m) & mask] = BR_FX_CONVERT_ENC(x);
        }
        valid = valid > keep ? n : valid << k;
    }
#undef BR_FX_CONVERT_DEC
#undef BR_FX_CONVERT_ENC
#undef BR_FX_CONVERT_DIV
    st->delay_storage_shift = shift;
    st->delay_valid_samples = valid;
    /* The send decimators restart; the wet output continues from its last value. */
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_history,
                  sizeof st->delay_long_history / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_pos,
                  sizeof st->delay_long_pos / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_phase,
                  sizeof st->delay_long_phase / sizeof(uint32_t));
    {
        /* The wet half-band and its output ramp hold the last wet value (line units). */
        int32_t last = st->delay_output_last / 4;
        for (k = 0u; k < 8u; ++k) st->delay_echo_history[k] = last;
        st->delay_long_previous_echo = st->delay_long_echo = st->delay_long_odd =
            st->delay_rate2_previous_echo = last;
    }
    st->delay_long_interp_phase = 2u << shift;
}
#endif

/* A gentler send taper puts the midpoint at one quarter of full send while
 * keeping zero silent and retaining the full effect at the top of the range. */
static inline uint32_t br_fx_send_taper(uint32_t value)
{
    if (value > 127u) value = 127u;
    return (value * value + 63u) / 127u;
}

/* Derive controls only after a cache miss. Keep this cold setup compact;
 * the block dispatcher and per-sample kernels retain their O3 settings. */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void
br_fx_prepare_track_controls(uint32_t p, const br_sound_t *s,
                             const br_fx_config_t *cfg, br_fx_controls_t *c,
                             uint32_t bpm)
{
    uint32_t i;
        c->delay_engine = br_fx_bound_ram(cfg->delay_engine, BR_DELAY_COUNT - 1u);
        /* Delay send controls only the input to the line, so tails continue
         * naturally when the send is turned down. */
        c->delay_amount = br_fx_send_taper(s->delay);
        if (br_part_pending[p].delay_short == 1u || br_part_pending[p].delay_short == 2u) {
            uint32_t ticks = br_part_pending[p].delay_short == 1u ? 4u : 9u; /* 48ths of a beat */
            uint32_t beat = bpm < BR_BPM_MIN ? BR_BPM_MIN : bpm > BR_BPM_MAX ? BR_BPM_MAX : bpm;
            c->delay_time = (FS * 5u * ticks + 2u * beat) / (4u * beat);
        } else c->delay_time = BR_FX_PREPARE_INVOKE(br_fx_delay_note_samples, cfg->delay_time, bpm);
        c->delay_tail_hold = BR_FX_TAIL_SAMPLES;
        if (c->delay_time >= BR_FX_TAIL_SAMPLES - FS / 4u ||
            br_fx_bound_ram(cfg->delay_feedback, 127u))
            c->delay_tail_hold = c->delay_time + BR_FX_TAIL_SAMPLES;
        c->delay_storage_shift = 0u;
#if BR_FX_DELAY_RATE2
        while (c->delay_storage_shift < BR_FX_DELAY_MAX_SHIFT &&
               c->delay_time > (BR_FX_DELAY_SAMPLES << (c->delay_storage_shift + 1u)))
            ++c->delay_storage_shift;
        /* Whole storage samples: no fractional read, whose linear interpolation
         * took the wet bandwidth from about 3.0 to 2.4 kHz on some entries (and
         * likewise at every storage rate). The loop period moves by at most half
         * a storage sample (31 us at full storage rate). */
        c->delay_time = (c->delay_time + (1u << c->delay_storage_shift)) &
            ~((2u << c->delay_storage_shift) - 1u);
        {
            /* TAPE: one-pole low-pass per pass, 3.5 kHz at TONE open (darker as
             * TONE falls, 500 Hz at 0), at every storage rate. DIGITAL: none at
             * TONE open, else the same pole from 500 Hz to 8.5 kHz. Wow 0.15..1.9 Hz
             * (0.6 at WOW RATE 64) at +/-8 cents and 6 Hz flutter at +/-3 cents:
             * the pitch deviation depends on the depth in time and the rate, not
             * on TIME, so the depth is fixed in time (1.2 ms wow, 0.05 ms flutter
             * at the default rate) and only rescaled to the storage rate. */
            uint32_t tone = br_fx_bound_ram(br_part_pending[p].delay_tone, 127u);
            uint32_t wow = br_fx_bound_ram(br_part_pending[p].delay_wow_rate, 127u);
            uint32_t wow_mhz = 150u + wow * wow * 450u / 4096u, hz = 0u;
            uint32_t rate = FS >> (c->delay_storage_shift + 1u);
            if (c->delay_engine == BR_DELAY_TAPE) hz = 500u + tone * 3000u / 127u;
            else if (tone < 127u) hz = 500u + tone * 8000u / 127u;
            if (hz) {
                float w = 6.2831853f * (float)hz / (float)rate;
                float a = 1.0f - 1.0f / (1.0f + w * (1.0f + w * (0.5f + w * (1.0f / 6.0f))));
                c->delay_tape_alpha = (uint16_t)(a * 32767.0f);
            } else {
                c->delay_tape_alpha = 0u;
            }
            c->delay_wow_inc = (wow_mhz * BR_FX_DELAY_MHZ_INC) << (c->delay_storage_shift + 1u);
            c->delay_flutter_inc = (6000u * BR_FX_DELAY_MHZ_INC) << (c->delay_storage_shift + 1u);
            c->delay_wow_depth_q8 = (uint16_t)((FS * 148u / wow_mhz) >> (c->delay_storage_shift + 1u));
            c->delay_flutter_depth_q8 = (uint16_t)((FS * 37u / 4000u) >> (c->delay_storage_shift + 1u));
            c->delay_width_q15 = (uint16_t)(br_fx_bound_ram(br_part_pending[p].delay_width, 127u) * 258u);
        }
#if BR_FX_DELAY_MAX_SHIFT
        /* A ringing line is resampled rather than cleared (the kernel's reset
         * then sees no change); a silent one is simply reset by the kernel.
         * Lines lent to polyphonic voices are never touched. */
        if (c->delay_storage_shift != br_fx_track[p].delay_storage_shift &&
            br_fx_track[p].delay_ring
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
            && !(br_fx_poly_mode && p != br_fx_poly_part)
#endif
            )
            br_fx_delay_storage_convert(&br_fx_track[p], p, c->delay_storage_shift);
#endif
#endif
        {
            /* TAPE loses a little more low end per pass: 120 Hz instead of 60. */
            uint32_t complement = 4096u - (c->delay_engine == BR_DELAY_TAPE ?
                2u * BR_FX_DELAY_HP_ALPHA_Q12 : BR_FX_DELAY_HP_ALPHA_Q12), k;
            for (k = 0u; k < c->delay_storage_shift; ++k)
                complement = (complement * complement + 2048u) >> 12;
            c->delay_hp_alpha = (uint16_t)(complement < 2049u ? 2047u : 4096u - complement);
        }
        for (i = 1u; i <= 4u; ++i) {
            uint32_t d = (c->delay_time * i) >> 2;
            c->delay_tap[i - 1u] = d < 1u ? 1u : d;
        }
        c->delay_feedback = br_fx_delay_feedback_law(cfg->delay_feedback);
#if BR_FX_DELAY_LOOP_HPF
        c->delay_feedback = (c->delay_feedback * (8192u - c->delay_hp_alpha)) >> 13;
#endif
        {
            /* GLITCH hold and quantizer step rise with the TIME entry, never
             * jumping between neighbours: 1..8 samples (44.1 kHz units), 2..256. */
            uint32_t g = br_part_pending[p].delay_short ? 0u :
                br_fx_delay_note_of(cfg->delay_time) * 7u / (BR_FX_DELAY_NOTE_COUNT - 1u);
            c->glitch_hold = (uint16_t)br_fx_rate_samples(1u + g);
#if BR_FX_DELAY_RATE2
            /* The line holds a quarter of the track bus: the same steps in bus units. */
            c->glitch_step = (uint16_t)((2u << g) >> 2 ? (2u << g) >> 2 : 1u);
#else
            c->glitch_step = (uint16_t)(2u << g);
#endif
        }
        c->reverb_wet = br_fx_send_taper(s->reverb);   /* SEND to the shared reverb */
        {
            uint32_t gains = br_fx_pan_gains(br_fx_bound_ram(s->pan, 127u));
            c->pan_left_q15 = (uint16_t)(gains >> 16);
            c->pan_right_q15 = (uint16_t)gains;
        }
        c->mod_engine = br_fx_bound_ram(cfg->mod_engine, BR_MOD_COUNT - 1u);
        c->mod_feedback = br_fx_bound_ram(br_part_pending[p].mod_feedback, 127u);
        c->mod_depth = br_fx_bound_ram(cfg->mod_depth, 127u);
        c->mod_mix = br_fx_bound_ram(cfg->mod_mix, 127u);
#if BR_FX_Q15_MIX_GAIN
        c->mod_mix_gain = (uint16_t)((c->mod_mix * 32768u + 63u) / 127u);
#elif BR_FX_FLOAT_MIX_GAIN
        c->mod_mix_gain = c->mod_mix >= 127u ? 1.0f :
            (float)c->mod_mix * (1.0f / 127.0f);
#endif
        c->mod_lfo_inc = br_fx_rate_phase(
            BR_FX_PREPARE_INVOKE(br_mod_rate_inc, br_fx_bound_ram(cfg->mod_rate, 127u)));
        c->mod_base_q8 = br_fx_rate_q8(br_mod_base(c->mod_engine) * 256u);
        c->mod_span = br_fx_rate_samples(BR_FX_PREPARE_INVOKE(br_mod_depth_span,
            c->mod_engine, c->mod_depth, br_fx_bound_ram(cfg->mod_rate, 127u)));
        {   /* Keep the deepest read (and its interpolation partner) inside the line. */
            uint32_t room = BR_FX_MOD_REACH - (c->mod_base_q8 >> 8);
            if (c->mod_span > room) c->mod_span = room;
        }
}

static BR_FX_PREPARE_CODE void br_fx_prepare_block(uint32_t n)
{
    uint32_t p, i, bpm = br_bpm;
#if BR_AUDIO_FUSION_ONLY_BUILD
    BR_FX_PREPARE_INVOKE(br_comp_snapshot,
                         br_source_compressor_revision != 0xffffffffu ?
                             &br_source_compressor : 0,
                         br_source_compressor_revision);
#else
    BR_FX_PREPARE_INVOKE(br_comp_snapshot,
                         br_fx_pipeline_controls ? &br_fx_pipeline_controls->compressor :
                             br_source_compressor_revision != 0xffffffffu ? &br_source_compressor : 0,
                         br_fx_pipeline_controls ? br_fx_pipeline_controls->revision : br_source_compressor_revision);
#endif
    br_fx_comp_active = !BR_FX_PREPARE_INVOKE(br_comp_bypassed);
    br_fx_comp_key_part = br_comp_cached.source == BR_COMP_ALL ? BR_PARTS :
        br_comp_source_part(br_comp_cached.source);
    br_fx_dry_center = 1u;
    ++br_fx_block_seq;
#if BR_FX_GLOBAL_REVERB
    if (n) br_gr_plan();
#endif
    /* Effective controls are fixed for this audio block. Tail state still
     * advances per sample, including the moment a disabled effect settles. */
    for (p = 0; p < BR_PARTS; ++p) {
        const br_sound_t *s = br_fx_sound_at(p);
        br_fx_job_enabled[p] = BR_FX_PREPARE_INVOKE(br_fx_any_enabled, s, br_fx_config_at(p));
        br_fx_job_pan[p] = br_fx_bound_ram(s->pan, 127u);
        br_fx_job_kit[p] = br_fx_is_kit(s->preset);
        if (br_fx_job_enabled[p] || br_fx_track[p].tail_samples || br_fx_job_pan[p] != 64u ||
            br_fx_pan_prev[p] || br_fx_track[p].drive_send)
            br_fx_dry_center = 0u;
    }
#if BR_FX_GLOBAL_REVERB
    br_gr_wanted = br_gr_run;
    for (p = 0; p < BR_PARTS; ++p) if (br_fx_sound_at(p)->reverb) br_gr_wanted = 1u;
    if (br_gr_wanted) br_fx_dry_center = 0u;
#endif
    if (n && !br_fx_comp_active) BR_FX_PREPARE_INVOKE(br_comp_reset_detector);
    if (br_fx_dry_center && !br_fx_comp_active) return;

    /* Clamp and derive hot-path values once here; each track job owns only
     * its indexed FX state and output row. */
    for (p = 0; p < BR_PARTS; ++p) {
        const br_sound_t *s = br_fx_sound_at(p);
        const br_fx_config_t *cfg = br_fx_config_at(p);
        br_fx_controls_t *c = &br_fx_controls[p];
#if BR_FX_STABLE_CONTROL_CACHE
        uint32_t key[11] = {
            s->delay, s->reverb, s->pan,
            cfg->delay_engine | ((uint32_t)br_part_pending[p].delay_tone << 8), cfg->delay_time | ((uint32_t)br_part_pending[p].delay_short << 8), cfg->delay_feedback,
            cfg->mod_engine | ((uint32_t)br_part_pending[p].mod_feedback << 8), cfg->mod_depth, cfg->mod_mix, cfg->mod_rate, bpm
        };
        uint32_t key_index = 0u;
        if (br_fx_control_key_valid & (1u << p)) {
            for (; key_index < 11u; ++key_index)
                if (key[key_index] != br_fx_control_key[p][key_index]) break;
            if (key_index == 11u) continue;
        }
        for (key_index = 0u; key_index < 11u; ++key_index)
            br_fx_control_key[p][key_index] = key[key_index];
        br_fx_control_key_valid |= 1u << p;
#endif
        BR_FX_PREPARE_INVOKE(br_fx_prepare_track_controls, p, s, cfg, c, bpm);
    }
#if BR_FX_GLOBAL_REVERB
    if (br_gr_wanted) {
        if (br_gr_lfo_frames < FS / 20u) br_gr_lfo_frames += n;
        br_gr_prepare();
    }
#endif
}

/* Call only after session/effective snapshots are rebuilt, under the app's
 * quiescent transaction. The stopped+quiet check prevents state advancement.
 * Cache/control writes are retained; the shared reverb's coefficients are
 * designed here (no DSP state advances), not in the first audio block. */
BR_POLY_COLD BR_AUDIO_API void br_fx_warm_controls_quiescent(void)
{
#if BR_FX_STABLE_CONTROL_CACHE
    if (br_playing || !br_engine_quiet() || !br_fx_quiet()) return;
    br_fx_prepare_block(0u);
#endif
}

/* Full-rate Q8 read-head offset. Adjacent table points interpolate over
 * 2^28 phase units, so neither the table wrap nor a knob change introduces
 * the old ~1 kHz stepped sawtooth into the tape feedback loop. */
static inline __attribute__((always_inline)) int32_t br_fx_tape_wow_q8(uint32_t phase)
{
#if BR_FX_TAPE_WOW_DELTA_LUT
    uint32_t index = phase >> 28;
    int32_t a = br_fx_tape_wow_delta[index];
    int32_t b = br_fx_tape_wow_delta[(index + 1u) & 15u];
    return a * 256 + (b - a) * (int32_t)((phase >> 20) & 255u);
#else
    return br_fx_div_pow2(br_fx_tri(phase) *
        (int32_t)br_fx_rate_q8(7u * 256u), 15u);
#endif
}

#if !BR_FX_DELAY_RATE2
static BR_FX_RAM_AUDIO int32_t br_fx_delay(br_fx_track_t *st, uint32_t p, int32_t input,
                           const br_fx_controls_t *c BR_FX_TAIL_EVENT_PARAM)
{
    uint32_t engine = c->delay_engine;
    uint32_t amount = c->delay_amount;
    uint32_t time = c->delay_time;
    uint32_t feedback = c->delay_feedback;
    uint32_t tap, read_pos;
    int32_t echo, write, send;
    if (!amount) send = 0;
    else send = br_fx_div127(input * (int32_t)amount);
    if (engine == BR_DELAY_TAPE) {
        int32_t wow = br_fx_div_pow2(br_fx_tape_wow_q8(st->lfo_phase), 8u);
        uint32_t wow_time = (uint32_t)((int32_t)time + wow);
        if (wow_time < 1u) wow_time = 1u;
        read_pos = (st->delay_pos - wow_time) & (BR_FX_DELAY_SAMPLES - 1u);
        echo = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]);
        echo = br_fx_delay_softclip(echo);
        st->delay_lp = br_fx_pole_bounded(st->delay_lp, echo, br_fx_tape_alpha_full());
        echo = st->delay_lp;
    } else if (engine == BR_DELAY_MULTITAP) {
        int32_t sum = 0;
#if BR_FX_MULTITAP_COMBINED_DIV
        int32_t weighted_sum = 0;
#endif
        for (tap = 1u; tap <= 4u; ++tap) {
            uint32_t d = c->delay_tap[tap - 1u];
            read_pos = (st->delay_pos - d) & (BR_FX_DELAY_SAMPLES - 1u);
#if BR_FX_MULTITAP_COMBINED_DIV
            weighted_sum += (int32_t)br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]) * (int32_t)(5u - tap);
#else
            sum += br_fx_div10((int32_t)br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]) * (int32_t)(5u - tap));
#endif
        }
#if BR_FX_MULTITAP_COMBINED_DIV
        sum = br_fx_div10(weighted_sum);
#endif
        echo = sum;
    } else {
        read_pos = (st->delay_pos - time) & (BR_FX_DELAY_SAMPLES - 1u);
        echo = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]);
        if (engine == BR_DELAY_GLITCH) {
            uint32_t hold = c->glitch_hold;
            if (st->glitch_count == 0u) {
                int32_t step = (int32_t)c->glitch_step;
                st->glitch_hold = br_fx_glitch_quantize(echo, (uint32_t)step);
                st->glitch_count = hold;
            }
            echo = st->glitch_hold;
            --st->glitch_count;
        }
    }
    if (engine == BR_DELAY_TAPE) echo = br_fx_clip_ram(echo, -32767, 32767);
    if (engine == BR_DELAY_GLITCH) feedback = (feedback * 3u) >> 2;
    write = br_fx_delay_write_sat(send + br_fx_delay_feedback_q15(
        br_fx_delay_loop_hp(&st->delay_hp, echo, c->delay_hp_alpha), feedback));
    /* Drop quantization residue instead of letting feedback hold a DC tick forever. */
    if (br_fx_abs(write) <= 2) write = 0;
    BR_POLY_DELAY_LINE(p)[st->delay_pos] = br_fx_reverb_compress16_fast(write);
    st->delay_pos = (st->delay_pos + 1u) & (BR_FX_DELAY_SAMPLES - 1u);
    if (send > 2 || send < -2 ||
        ((feedback || c->delay_tail_hold > BR_FX_TAIL_SAMPLES) &&
         (echo > 2 || echo < -2))) {
#if BR_FX_BLOCK_STAGE_ONLY_BUILD
        /* Stage-only callers always provide an event slot; the staged emitter
         * owns the tail counter update after the delay pass. */
        *tail_event = 1;
#elif BR_FX_BLOCK_STAGE_PIPELINE
        if (tail_event) *tail_event = 1;
        else if (st->tail_samples < c->delay_tail_hold)
            st->tail_samples = c->delay_tail_hold;
#else
        if (st->tail_samples < c->delay_tail_hold)
            st->tail_samples = c->delay_tail_hold;
#endif
    }
    return input + echo;
}
#else
/* Keep the live input and wet-return mix at full rate. Only the delay loop
 * consumes the anti-aliased send at half rate; the feedback coefficient is
 * unchanged because each loop round trip still spans the same time. */
static inline __attribute__((always_inline)) int32_t
br_fx_delay_rate2_read(uint32_t p, uint32_t delay_pos, uint32_t delay_q8,
                       uint32_t valid_samples)
{
    if (delay_q8 > BR_FX_DELAY_SAMPLES * 256u)
        delay_q8 = BR_FX_DELAY_SAMPLES * 256u;
    uint32_t needed = (delay_q8 + 255u) >> 8;
    if (valid_samples < needed) return 0;
    uint32_t read_pos = (delay_pos - (delay_q8 >> 8)) &
        (BR_FX_DELAY_SAMPLES - 1u);
    uint32_t fraction = delay_q8 & 255u;
    int32_t a = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]);
    if (!fraction) return a;
    int32_t b = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[(read_pos - 1u) &
        (BR_FX_DELAY_SAMPLES - 1u)]);
    return br_fx_reverb_lerp_q8_companded(a, b, fraction);
}

/* A smooth LFO from a phase: 4x(1 - |x|), Q15. Its slope (the pitch it
 * gives a delay read) is a triangle, so the pitch glides without corners. */
static inline __attribute__((always_inline)) int32_t br_fx_delay_parabola(uint32_t phase)
{
    int32_t x = (int32_t)phase >> 16;
    int32_t y = (x * (32768 - (x < 0 ? -x : x))) >> 13;
    return y > 32767 ? 32767 : y;
}

/* One storage sample of the engine's loop: read, per-pass processing, write. */
static inline __attribute__((always_inline)) int32_t br_fx_delay_loop(br_fx_track_t *st,
    uint32_t p, int32_t send, const br_fx_controls_t *c)
{
    uint32_t engine = c->delay_engine;
    uint32_t shift = c->delay_storage_shift;
    uint32_t time_q8 = (c->delay_time * 128u) >> shift;
    uint32_t feedback = c->delay_feedback;
    uint32_t tap, wide_bit = 0u;
    int32_t echo, out, write;
    if (engine == BR_DELAY_TAPE) {
        /* Wow and flutter: two parabolic LFOs (smooth delay, triangle pitch)
         * move a Q12 read between storage samples, linearly interpolated. */
        uint32_t wow_phase = st->delay_wow_phase += c->delay_wow_inc;
        uint32_t flutter_phase = st->delay_flutter_phase += c->delay_flutter_inc;
        int32_t read_q12 = (int32_t)((c->delay_time << 11) >> shift) +
            ((br_fx_delay_parabola(wow_phase) * (int32_t)c->delay_wow_depth_q8 +
              br_fx_delay_parabola(flutter_phase) * (int32_t)c->delay_flutter_depth_q8) >> 11);
        uint32_t whole, fraction;
        if (read_q12 < 4096) read_q12 = 4096;
        whole = (uint32_t)read_q12 >> 12;
        fraction = (uint32_t)read_q12 & 4095u;
        if (whole >= BR_FX_DELAY_SAMPLES - 1u || st->delay_valid_samples <= whole) {
            echo = 0;
        } else {
            uint32_t at = (st->delay_pos - whole) & (BR_FX_DELAY_SAMPLES - 1u);
            int32_t a = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[at]);
            int32_t b = br_fx_reverb_decompress16_fast(
                BR_POLY_DELAY_LINE(p)[(at - 1u) & (BR_FX_DELAY_SAMPLES - 1u)]);
            echo = a + ((b - a) * (int32_t)fraction >> 12);
        }
        /* Each pass loses highs (3.5 kHz pole at TONE open); the loop's 120 Hz
         * high-pass takes a little low end from each repeat. No clip on the
         * read: the write's knee bounds the line (the rational curve that sat
         * here took 2.4 dB even from a -24 dBFS echo). */
        st->delay_lp = br_fx_pole_bounded(st->delay_lp, echo, c->delay_tape_alpha);
        echo = br_fx_clip_ram(st->delay_lp, -32767, 32767);
        out = echo;
    } else if (engine == BR_DELAY_MULTITAP) {
        int32_t weighted_sum = 0;
        for (tap = 1u; tap <= 4u; ++tap) {
            int32_t tap_echo = br_fx_delay_rate2_read(p, st->delay_pos,
                BR_FX_DELAY_GRID_Q8((c->delay_tap[tap - 1u] * 128u) >> shift),
                st->delay_valid_samples);
            weighted_sum += tap_echo * (int32_t)(5u - tap);
        }
        /* The loop hears the taps at 4:3:2:1 / 10 (unity at DC); the output
         * plays them at 1, 3/4, 1/2 and 1/4, so the first tap lands at the
         * send level instead of 8 dB under it. */
        echo = br_fx_div10(weighted_sum);
        out = br_fx_div_pow2(weighted_sum, 2u);
    } else if (engine == BR_DELAY_GLITCH) {
        /* Only the held value is heard: read the line just to capture a new one. */
        if (st->glitch_count == 0u) {
            int32_t step = (int32_t)c->glitch_step;
            uint32_t hold = ((uint32_t)c->glitch_hold +
                             st->glitch_half_error) >> 1;
            echo = br_fx_delay_rate2_read(p, st->delay_pos,
                BR_FX_DELAY_GRID_Q8(time_q8), st->delay_valid_samples);
            if (c->glitch_hold & 1u) st->glitch_half_error ^= 1u;
            if (hold < 1u) hold = 1u;
            st->glitch_hold = br_fx_glitch_quantize(echo, (uint32_t)step);
            st->glitch_count = hold;
        }
        out = echo = st->glitch_hold;
        --st->glitch_count;
        feedback = (feedback * 3u) >> 2;
    } else {
        echo = br_fx_delay_rate2_read(p, st->delay_pos, time_q8,
                                      st->delay_valid_samples);
        if (c->delay_tape_alpha)          /* TONE below open */
            echo = st->delay_lp = br_fx_pole_bounded(st->delay_lp, echo, c->delay_tape_alpha);
        out = echo;
    }
    if ((engine == BR_DELAY_MULTITAP || engine == BR_DELAY_GLITCH) && c->delay_tape_alpha) {
        echo = st->delay_lp = br_fx_pole_bounded(st->delay_lp, echo, c->delay_tape_alpha);
        out = engine == BR_DELAY_MULTITAP ? echo + echo + br_fx_div_pow2(echo, 1u) : echo;
    }
    {
        int32_t fed = br_fx_delay_feedback_q15(
            br_fx_delay_loop_hp(&st->delay_hp, echo, c->delay_hp_alpha), feedback);
        write = send + fed;
        if (c->delay_width_q15) {
            /* WIDTH: each line sample carries, in its lowest bit, which side its
             * echo plays on: what the send writes plays left on its first
             * repeat, and every pass flips it. Where send and feedback overlap
             * the louder one decides. Silence (a zero code) carries no side.
             * An echo that starts from near silence takes its side at once; a
             * flip in the middle of a sound pans over about 3 ms instead of
             * clicking. */
            int32_t code = BR_POLY_DELAY_LINE(p)[
                (st->delay_pos - (c->delay_time >> (shift + 1u))) & (BR_FX_DELAY_SAMPLES - 1u)];
            uint32_t read_bit = (uint32_t)code & 1u;
            wide_bit = br_fx_abs(send) >= br_fx_abs(fed) ? 0u : read_bit ^ 1u;
            if (code) {
                int32_t sigma = st->delay_pan_sigma, step = (int32_t)(1024u << shift);
                int32_t target = read_bit ? -32767 : 32767;
                if (br_fx_abs(echo) < 256) sigma = target;
                else sigma = sigma < target ? (sigma + step < target ? sigma + step : target) :
                                              (sigma - step > target ? sigma - step : target);
                st->delay_pan_sigma = sigma;
                st->delay_side_q14 = (sigma * (int32_t)c->delay_width_q15) >> 16;
            }
        }
    }
    if (engine == BR_DELAY_TAPE) {
        /* Tape compresses rather than clips: the same knee driven 6 dB hotter
         * (x 2 in, x 1/2 out), unity below -12 dBFS. */
        write = br_fx_delay_write_sat(br_fx_clip_ram(write, -65535, 65535) * 2);
        write = br_fx_div_pow2(write, 1u);
    } else {
        write = br_fx_delay_write_sat(write);
    }
    if (br_fx_abs(write) <= 2) write = 0;
    if (write) st->delay_ring = BR_FX_DELAY_SAMPLES;
    else if (st->delay_ring) --st->delay_ring;
    {
        int16_t code = br_fx_reverb_compress16_fast(write);
        if (c->delay_width_q15 && write) code = (int16_t)((code & ~1) | (int32_t)wide_bit);
        BR_POLY_DELAY_LINE(p)[st->delay_pos] = code;
    }
    st->delay_pos = (st->delay_pos + 1u) & (BR_FX_DELAY_SAMPLES - 1u);
    if (st->delay_valid_samples < BR_FX_DELAY_SAMPLES) ++st->delay_valid_samples;
    return out;
}

/* The wet path's half-band, used at every rate change of the send (decimate
 * by 2) and once on the way back (interpolate by 2). 11 taps in Q13: centre
 * 4096 and three odd taps (the even ones are zero). As a 44.1 kHz decimator it
 * is flat to 7 kHz (-0.08 dB) and at least 40.3 dB down from 15.05 kHz, so
 * nothing folds into the band below 7 kHz above -40 dB; the binomial filter
 * it replaces was -3 dB at 4.1 kHz with linear interpolation on top. */
#define BR_FX_DELAY_HB1 2498
#define BR_FX_DELAY_HB3 616
#define BR_FX_DELAY_HB5 166
#define BR_FX_DELAY_HB(h, pos, mask) \
    (4096 * (int32_t)(h)[((pos) - 5u) & (mask)] + \
     BR_FX_DELAY_HB1 * ((int32_t)(h)[((pos) - 4u) & (mask)] + (int32_t)(h)[((pos) - 6u) & (mask)]) - \
     BR_FX_DELAY_HB3 * ((int32_t)(h)[((pos) - 2u) & (mask)] + (int32_t)(h)[((pos) - 8u) & (mask)]) + \
     BR_FX_DELAY_HB5 * ((int32_t)(h)[(pos) & (mask)] + (int32_t)(h)[((pos) - 10u) & (mask)]))

#if BR_FX_DELAY_MAX_SHIFT
/* Further powers-of-two decimation for long wet delays: the same half-band at
 * every level. The int16 rings hold half the line value, which leaves room for
 * the filter's overshoot above the line's full scale. */
static BR_FX_RAM_AUDIO uint32_t br_fx_delay_long_decimate(br_fx_track_t *st,
    int32_t input, uint32_t levels, int32_t *output)
{
    uint32_t level;
    int32_t sample = input;
    for (level = 0u; level < levels; ++level) {
        uint32_t pos = (st->delay_long_pos[level] + 1u) & 15u;
        int16_t *history = st->delay_long_history[level];
        history[pos] = (int16_t)br_fx_clip_ram(sample >> 1, -32767, 32767);
        st->delay_long_pos[level] = (uint8_t)pos;
        if ((st->delay_long_phase[level] ^= 1u) != 0u) return 0u;
        sample = (BR_FX_DELAY_HB(history, pos, 15u) + 2048) >> 12;
    }
    *output = sample;
    return 1u;
}
#endif

/* One storage-rate tick, out of line so the per-sample loop stays small:
 * the send half-band (bus/4 in, line units out) and the send level, further
 * halvings for long times, the engine's loop, and the wet half-band's two
 * outputs. At FS/2 storage it returns the centre and leaves the odd output in
 * delay_rate2_previous_echo; below FS/2 it sets the output ramp's points. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) int32_t br_fx_delay_rate2_step(
    br_fx_track_t *st, uint32_t p, const br_fx_controls_t *c)
{
    const int32_t *history = st->delay_rate2_send_history;
    uint32_t pos = st->delay_rate2_history_pos, e;
    int32_t send = br_fx_clip_ram((BR_FX_DELAY_HB(history, pos, 15u) + 4096) >> 13, -65535, 65535);
    int32_t *echoes = st->delay_echo_history, centre, odd;
    send = (send * (int32_t)(c->delay_amount * 258u)) >> 15;     /* amount / 127, Q15 */
#if BR_FX_DELAY_MAX_SHIFT
    if (c->delay_storage_shift &&
        !br_fx_delay_long_decimate(st, send, c->delay_storage_shift, &send))
        return st->delay_long_echo;
#endif
    e = (st->delay_echo_pos + 1u) & 7u;
    st->delay_echo_pos = e;
    echoes[e] = br_fx_delay_loop(st, p, send, c);
    centre = echoes[(e - 3u) & 7u];
    odd = (BR_FX_DELAY_HB1 * (centre + echoes[(e - 2u) & 7u]) -
           BR_FX_DELAY_HB3 * (echoes[(e - 4u) & 7u] + echoes[(e - 1u) & 7u]) +
           BR_FX_DELAY_HB5 * (echoes[(e - 5u) & 7u] + echoes[e]) + 2048) >> 12;
#if BR_FX_DELAY_MAX_SHIFT
    if (c->delay_storage_shift) {
        st->delay_long_previous_echo = st->delay_long_odd;
        st->delay_long_echo = centre;
        st->delay_long_odd = odd;
        st->delay_long_interp_phase = 0u;
        return centre;
    }
#endif
    st->delay_rate2_previous_echo = odd;
    return centre;
}

/* Runs only when the delay storage length changes or a line is cleared:
 * cold, so it shares the prepare placement; RAM callers use BR_FX_PREPARE_INVOKE. */
static BR_FX_PREPARE_CODE __attribute__((noinline)) void br_fx_delay_storage_reset(br_fx_track_t *st,
                                                           uint32_t shift)
{
    st->delay_storage_shift = shift;
    st->delay_pos = 0u;
    st->delay_valid_samples = 0u;   /* reads return zero until rewritten */
    st->delay_ring = 0u;
    st->delay_rate2_phase = 0u;
    st->delay_rate2_history_pos = 0u;
    st->delay_rate2_previous_echo = 0;
    st->delay_echo_pos = 0u;
    st->delay_long_previous_echo = st->delay_long_echo = st->delay_long_odd = 0;
    st->delay_long_interp_phase = 0u;
    st->delay_lp = 0;
    st->delay_hp = 0;
    st->glitch_count = 0u;
    st->glitch_hold = 0;
    br_fx_clear32((br_fx_clear_word_t *)st->delay_rate2_send_history,
                  sizeof st->delay_rate2_send_history / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_echo_history,
                  sizeof st->delay_echo_history / sizeof(uint32_t));
#if BR_FX_DELAY_MAX_SHIFT
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_history,
                  sizeof st->delay_long_history / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_pos,
                  sizeof st->delay_long_pos / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_phase,
                  sizeof st->delay_long_phase / sizeof(uint32_t));
#endif
}

/* The per-sample loop, specialised for WIDTH 0 and on (a block-level branch,
 * so WIDTH 0 pays nothing for it). */
static inline __attribute__((always_inline)) void br_fx_delay_rate2_run(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core,
    const int32_t *input, uint32_t n, const br_fx_controls_t *c, int32_t (*stereo)[2],
    const uint32_t wide)
{
    uint32_t i;
    const uint32_t shift = c->delay_storage_shift;
    /* Tail event: the line has held something nonzero within one line length
     * (any nonzero write, from the send or the feedback, re-arms delay_ring).
     * Taken once per block: the tail hold only needs the last event of a block. */
    const int32_t event = st->delay_ring != 0u;
    int32_t *const history = st->delay_rate2_send_history;
    uint32_t pos = st->delay_rate2_history_pos;
    uint32_t phase = st->delay_rate2_phase;
    int32_t wet = st->delay_rate2_previous_echo, out = 0;
    (void)sample_base; (void)progress_core;
    for (i = 0u; i < n; ++i) {
        int32_t sample = input[i];
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_DELAY);
        pos = (pos + 1u) & 15u;
        history[pos] = sample >> 2;            /* line units; the level is applied per tick */
        if (phase) {
            st->delay_rate2_history_pos = pos;
            wet = br_fx_delay_rate2_step(st, p, c);
        } else {
            wet = st->delay_rate2_previous_echo;
        }
#if BR_FX_DELAY_MAX_SHIFT
        if (shift) {
            /* Two ramps per storage sample: odd -> centre -> odd. */
            uint32_t k = st->delay_long_interp_phase, half = 1u << shift;
            if (k < 2u * half) ++k;
            st->delay_long_interp_phase = k;
            wet = k <= half ?
                st->delay_long_previous_echo + ((st->delay_long_echo -
                    st->delay_long_previous_echo) * (int32_t)k >> shift) :
                st->delay_long_echo + ((st->delay_long_odd - st->delay_long_echo) *
                    (int32_t)(k - half) >> shift);
        }
#endif
        out = wet * 4;
        stereo[i][0] = sample + out;
        /* WIDTH: the side (L = mono + side, R = mono - side) rides above the
         * event bit; at 0 only the event is written. */
        if (wide)
            stereo[i][1] = (int32_t)((uint32_t)((wet * st->delay_side_q14) >> 12) << 1) | event;
        else
            stereo[i][1] = event;
        phase ^= 1u;
    }
    if (n) st->delay_output_last = out;
    st->delay_rate2_history_pos = pos;
    st->delay_rate2_phase = phase;
}

/* The rate-2 delay for a run of samples whose controls are fixed: the send
 * goes through a half-band to the line's storage rate (halved again for long
 * times), the engine's loop runs once per storage sample, and the wet comes
 * back through the same half-band to FS/2 (at FS/2 storage directly, below it
 * through a linear ramp between the interpolated points). The line holds a
 * quarter of the track bus (its soft knee starts at -6 dBFS and holds at
 * 0 dBFS), so the wet is scaled back up by 4. stereo[i][1] is the delay's
 * tail event. */
static BR_FX_RAM_AUDIO void br_fx_stage_delay_rate2_tape_block(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core, uint32_t lfo_start,
    uint32_t lfo_inc, uint32_t lfo_final, const int32_t *input, uint32_t n,
    const br_fx_controls_t *c, int32_t (*stereo)[2])
{
    if (c->delay_width_q15)
        br_fx_delay_rate2_run(st, p, sample_base, progress_core, input, n, c, stereo, 1u);
    else
        br_fx_delay_rate2_run(st, p, sample_base, progress_core, input, n, c, stereo, 0u);
    (void)lfo_start; (void)lfo_inc;
#if defined(BR_FX_TEST_LONG_TAPE_BLOCK)
    ++br_fx_test_long_tape_block_calls;
#endif
    st->lfo_phase = lfo_final;
}

/* A TIME or storage-rate change: the wet output fades over 64 frames from
 * its last value (and a new storage rate without a converted tail resets). */
static inline __attribute__((always_inline)) void
br_fx_delay_begin(br_fx_track_t *st, const br_fx_controls_t *c)
{
    if (c->delay_storage_shift != st->delay_storage_shift ||
        c->delay_time != st->delay_last_time) {
        st->delay_output_xfade_from = st->delay_output_last;
        st->delay_output_xfade_left = BR_FX_DELAY_XFADE_FRAMES;
        st->delay_last_time = c->delay_time;
        if (c->delay_storage_shift != st->delay_storage_shift)
            BR_FX_PREPARE_INVOKE(br_fx_delay_storage_reset, st, c->delay_storage_shift);
    }
}

static inline __attribute__((always_inline)) void
br_fx_delay_fade(br_fx_track_t *st, const int32_t *input, uint32_t n, int32_t (*stereo)[2])
{
    uint32_t i;
    for (i = 0u; i < n && st->delay_output_xfade_left; ++i) {
        uint32_t elapsed = BR_FX_DELAY_XFADE_FRAMES - st->delay_output_xfade_left + 1u;
        int32_t from = st->delay_output_xfade_from;
        int32_t wet = stereo[i][0] - input[i];
        stereo[i][0] = input[i] + from + br_fx_div_pow2((wet - from) * (int32_t)elapsed, 6u);
        --st->delay_output_xfade_left;
    }
    if (i && n) st->delay_output_last = stereo[n - 1u][0] - input[n - 1u];
}

static BR_FX_RAM_AUDIO int32_t br_fx_delay(br_fx_track_t *st, uint32_t p, int32_t input,
    const br_fx_controls_t *c BR_FX_TAIL_EVENT_PARAM)
{
    int32_t out[1][2];
    br_fx_delay_begin(st, c);
    br_fx_stage_delay_rate2_tape_block(st, p, 0u, 0u, st->lfo_phase, 0u, st->lfo_phase,
                                       &input, 1u, c, out);
    br_fx_delay_fade(st, &input, 1u, out);
    if (out[0][1]) {
#if BR_FX_BLOCK_STAGE_ONLY_BUILD
        *tail_event = 1;
#elif BR_FX_BLOCK_STAGE_PIPELINE
        if (tail_event) *tail_event = 1;
        else if (st->tail_samples < c->delay_tail_hold)
            st->tail_samples = c->delay_tail_hold;
#else
        if (st->tail_samples < c->delay_tail_hold)
            st->tail_samples = c->delay_tail_hold;
#endif
    }
    return out[0][0];
}
#endif

#if BR_FX_BLOCK_STAGE_PIPELINE
#if BR_FX_BLOCK_STAGE_SPECIALIZE_MOD && BR_FX_MOD_RATE2
/* Rate-2 block modulation keeps the original decimation/reconstruction phase,
 * FIR history, ring writes, and LFO advancement while hoisting the dispatch and
 * control loads out of the per-sample loop. Phaser remains on its native-rate
 * implementation below. */
static BR_FX_RAM_AUDIO BR_FX_MOD_STAGE_NOINLINE_ATTR void br_fx_stage_mod_rate2_block(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core,
    const int32_t *input, uint32_t n, const br_fx_controls_t *c,
    int32_t *output)
{
    uint32_t i;
    uint32_t engine = c->mod_engine;
    uint32_t mix = c->mod_mix;
    uint32_t inc = c->mod_lfo_inc;
    uint32_t base_q8 = c->mod_base_q8;
    uint32_t span = c->mod_span;
    uint32_t lfo_phase = st->lfo_phase;
    uint32_t mod_pos = st->mod_pos;
    uint32_t phase = st->mod_rate2_phase;
    int32_t history0 = st->mod_rate2_input_history[0];
    int32_t history1 = st->mod_rate2_input_history[1];
    int32_t history2 = st->mod_rate2_input_history[2];
    int32_t previous_output = st->mod_rate2_previous_output;

    for (i = 0u; i < n; ++i) {
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_BUS_READ);
        int32_t sample = input[i];
        int32_t bounded_input = br_fx_clip_ram(sample, -65535, 65535);
        int32_t old0 = history0, old1 = history1, old2 = history2;
        int32_t reconstructed;
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_MOD);
        lfo_phase += inc;
        history2 = old1;
        history1 = old0;
        history0 = bounded_input;

        if (phase == 2u) {
            previous_output = sample;
            phase = 1u;
            output[i] = sample;
            continue;
        }
        if (phase == 0u) {
            reconstructed = previous_output;
            phase = 1u;
            output[i] = mix ? sample + BR_FX_BLEND127((reconstructed - sample), mix, c->mod_mix_gain) : sample;
            continue;
        }

        /* Preserve the original 1:3:3:1 anti-alias filter and write cadence. */
        {
            int32_t filtered_input = br_fx_div_pow2(
                bounded_input + 3 * old0 + 3 * old1 + old2, 3u);
            int32_t tri = br_fx_tri(lfo_phase);
            uint32_t delay_q8 = br_fx_mod_delay_q8(base_q8, span, tri) >> 1;
            uint32_t read_pos, frac;
            int32_t sample_a, sample_b, wet;
            if (delay_q8 >= BR_FX_MOD_SAMPLES * 256u)
                delay_q8 = (BR_FX_MOD_SAMPLES - 1u) * 256u;
            read_pos = (mod_pos - (delay_q8 >> 8)) &
                (BR_FX_MOD_SAMPLES - 1u);
            frac = delay_q8 & 255u;
            br_mod_line[p][mod_pos] = br_fx_clip16_ram(filtered_input + ((st->mod_feedback_last * (int32_t)c->mod_feedback) >> 8));
            sample_a = br_mod_line[p][read_pos];
            sample_b = br_mod_line[p][(read_pos - 1u) &
                (BR_FX_MOD_SAMPLES - 1u)];
            wet = br_fx_interp_q8_i16(sample_a, sample_b, frac);
            st->mod_feedback_last = wet;
            if (engine == BR_MOD_FLANGE)
                wet -= br_fx_div_pow2(filtered_input, 1u);
            mod_pos = (mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
            reconstructed = br_fx_div_pow2(previous_output + wet, 1u);
            previous_output = wet;
            phase = 0u;
            output[i] = mix ? sample + BR_FX_BLEND127((reconstructed - sample), mix, c->mod_mix_gain) : sample;
        }
    }
    st->lfo_phase = lfo_phase;
    st->mod_pos = mod_pos;
    st->mod_rate2_phase = phase;
    st->mod_rate2_input_history[0] = history0;
    st->mod_rate2_input_history[1] = history1;
    st->mod_rate2_input_history[2] = history2;
    st->mod_rate2_previous_output = previous_output;
}
#endif

#if BR_FX_BLOCK_STAGE_SPECIALIZE_MOD
static BR_FX_RAM_AUDIO BR_FX_MOD_STAGE_NOINLINE_ATTR void br_fx_stage_mod_block(br_fx_track_t *st, uint32_t p,
    uint32_t sample_base, uint32_t progress_core, const int32_t *input, uint32_t n,
    const br_fx_controls_t *c, int32_t *output)
{
    uint32_t i;
    uint32_t engine = c->mod_engine;
    uint32_t depth = c->mod_depth;
    uint32_t mix = c->mod_mix;
    uint32_t inc = c->mod_lfo_inc;
#if BR_FX_STAGE_SKIP_IDLE && BR_FX_MOD_RATE2
    if (!mix) {
        /* MIX 0 is silent and the caller reads the input directly. The phaser keeps
         * only its LFO; the delay-line engines keep writing the anti-aliased line so
         * turning MIX up reads real history, as before. */
        if (engine != BR_MOD_PHASER) {
            uint32_t mod_pos = st->mod_pos, phase = st->mod_rate2_phase;
            int32_t h0 = st->mod_rate2_input_history[0];
            int32_t h1 = st->mod_rate2_input_history[1];
            int32_t h2 = st->mod_rate2_input_history[2];
            for (i = 0u; i < n; ++i) {
                int32_t bounded = br_fx_clip_ram(input[i], -65535, 65535);
                if (phase == 1u) {
                    br_mod_line[p][mod_pos] = br_fx_clip16_ram(
                        br_fx_div_pow2(bounded + 3 * h0 + 3 * h1 + h2, 3u));
                    mod_pos = (mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
                    phase = 0u;
                } else {
                    if (phase == 2u) st->mod_rate2_previous_output = input[i];
                    phase = 1u;
                }
                h2 = h1; h1 = h0; h0 = bounded;
            }
            st->mod_pos = mod_pos;
            st->mod_rate2_phase = phase;
            st->mod_rate2_input_history[0] = h0;
            st->mod_rate2_input_history[1] = h1;
            st->mod_rate2_input_history[2] = h2;
        }
        (void)sample_base; (void)progress_core; (void)depth; (void)output;
        st->lfo_phase += inc * n;
        return;
    }
#endif
#if BR_FX_MOD_RATE2
    if (engine != BR_MOD_PHASER) {
        br_fx_stage_mod_rate2_block(st, p, sample_base, progress_core, input, n, c, output);
        return;
    }
#endif
    if (engine == BR_MOD_PHASER) {
        for (i = 0u; i < n; ++i) {
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_BUS_READ);
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_MOD);
            uint32_t phase = st->lfo_phase;
            int32_t wet = input[i];
            int32_t tri = br_fx_tri(phase);
            int32_t a = 3500 + (int32_t)((((uint32_t)(tri + 32767)) *
                (5000u + depth * 110u)) >> 16);
            uint32_t j;
            st->lfo_phase += inc;
            wet = br_fx_clip_ram(wet + ((st->mod_feedback_last * (int32_t)c->mod_feedback) >> 8), -65535, 65535);
            for (j = 0u; j < 4u; ++j) {
                int32_t y = st->phase_state[j] -
                    br_fx_mul_q15_phaser_selected(wet, (uint32_t)a);
                st->phase_state[j] = br_fx_clip_ram(
                    wet + br_fx_mul_q15_phaser_selected(y, (uint32_t)a), -65535, 65535);
                wet = y;
            }
            st->mod_feedback_last = br_fx_clip_ram(wet, -65535, 65535);
            output[i] = mix ? input[i] + BR_FX_BLEND127((wet - input[i]), mix, c->mod_mix_gain) : input[i];
        }
    } else if (engine == BR_MOD_FLANGE && mix) {
        for (i = 0u; i < n; ++i) {
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_BUS_READ);
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_MOD);
            uint32_t phase = st->lfo_phase;
            uint32_t delay_q8, read_pos, frac;
            int32_t sample_a, sample_b, wet;
            st->lfo_phase += inc;
            br_mod_line[p][st->mod_pos] = br_fx_clip16_ram(input[i]);
            delay_q8 = br_fx_mod_delay_q8(c->mod_base_q8, c->mod_span,
                                           br_fx_tri(phase));
            if (delay_q8 >= BR_FX_MOD_SAMPLES * 256u)
                delay_q8 = (BR_FX_MOD_SAMPLES - 1u) * 256u;
            read_pos = (st->mod_pos - (delay_q8 >> 8)) & (BR_FX_MOD_SAMPLES - 1u);
            frac = delay_q8 & 255u;
            sample_a = br_mod_line[p][read_pos];
            sample_b = br_mod_line[p][(read_pos - 1u) & (BR_FX_MOD_SAMPLES - 1u)];
            wet = br_fx_interp_q8_i16(sample_a, sample_b, frac) -
                br_fx_div_pow2(input[i], 1u);
            st->mod_pos = (st->mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
            output[i] = input[i] + BR_FX_BLEND127((wet - input[i]), mix, c->mod_mix_gain);
        }
    } else if (mix) {
        for (i = 0u; i < n; ++i) {
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_BUS_READ);
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_MOD);
            uint32_t phase = st->lfo_phase;
            uint32_t delay_q8, read_pos, frac;
            int32_t sample_a, sample_b, wet;
            st->lfo_phase += inc;
            br_mod_line[p][st->mod_pos] = br_fx_clip16_ram(input[i]);
            delay_q8 = br_fx_mod_delay_q8(c->mod_base_q8, c->mod_span,
                                           br_fx_tri(phase));
            if (delay_q8 >= BR_FX_MOD_SAMPLES * 256u)
                delay_q8 = (BR_FX_MOD_SAMPLES - 1u) * 256u;
            read_pos = (st->mod_pos - (delay_q8 >> 8)) & (BR_FX_MOD_SAMPLES - 1u);
            frac = delay_q8 & 255u;
            sample_a = br_mod_line[p][read_pos];
            sample_b = br_mod_line[p][(read_pos - 1u) & (BR_FX_MOD_SAMPLES - 1u)];
            wet = br_fx_interp_q8_i16(sample_a, sample_b, frac);
            st->mod_pos = (st->mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
            output[i] = input[i] + BR_FX_BLEND127((wet - input[i]), mix, c->mod_mix_gain);
        }
    } else {
        for (i = 0u; i < n; ++i) {
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_BUS_READ);
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_MOD);
            st->lfo_phase += inc;
            br_mod_line[p][st->mod_pos] = br_fx_clip16_ram(input[i]);
            st->mod_pos = (st->mod_pos + 1u) & (BR_FX_MOD_SAMPLES - 1u);
            output[i] = input[i];
        }
    }
}
#endif

#if BR_FX_BLOCK_STAGE_SPECIALIZE_DELAY

static inline __attribute__((always_inline)) int32_t br_fx_stage_delay_finish(
    br_fx_track_t *st, uint32_t p, int32_t input, int32_t echo,
    int32_t send, const br_fx_controls_t *c, uint32_t feedback,
    int32_t *tail_event)
{
    int32_t write = br_fx_delay_write_sat(send + br_fx_delay_feedback_q15(
        br_fx_delay_loop_hp(&st->delay_hp, echo, c->delay_hp_alpha), feedback));
    if (br_fx_abs(write) <= 2) write = 0;
    BR_POLY_DELAY_LINE(p)[st->delay_pos] = br_fx_reverb_compress16_fast(write);
    st->delay_pos = (st->delay_pos + 1u) & (BR_FX_DELAY_SAMPLES - 1u);
    if (send > 2 || send < -2 ||
        ((feedback || c->delay_tail_hold > BR_FX_TAIL_SAMPLES) &&
         (echo > 2 || echo < -2))) *tail_event = 1;
    return input + echo;
}

static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_fx_stage_delay_block(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core, uint32_t lfo_start,
    uint32_t lfo_inc, uint32_t lfo_final, const int32_t *input, uint32_t n,
    const br_fx_controls_t *c, int32_t (*stereo)[2])
{
    uint32_t i;
    uint32_t engine = c->delay_engine;
    uint32_t amount = c->delay_amount;
    uint32_t time = c->delay_time;
    uint32_t feedback = c->delay_feedback;
#if BR_FX_STAGE_SKIP_IDLE && BR_FX_DELAY_RATE2
    if (!amount && !st->delay_ring && !st->delay_output_xfade_left) {
        /* Send 0 and a whole line of zero writes since the last nonzero one (or a
         * reset): the line holds only zeros, so the stage adds nothing. A tail
         * still ringing when MIX reaches 0 keeps running here until it has decayed.
         * Keep what a later enable reads. */
        if (c->delay_storage_shift != st->delay_storage_shift)
            BR_FX_PREPARE_INVOKE(br_fx_delay_storage_reset, st, c->delay_storage_shift);
        st->delay_last_time = c->delay_time;
        st->delay_rate2_history_pos = (st->delay_rate2_history_pos + n) & 15u;
        st->delay_rate2_phase ^= n & 1u;
        for (i = 0u; i < n; ++i) {
            stereo[i][0] = input[i];
            stereo[i][1] = 0;
        }
        st->lfo_phase = lfo_final;
        (void)sample_base; (void)progress_core; (void)lfo_start; (void)lfo_inc;
        return;
    }
#endif
#if BR_FX_DELAY_RATE2
    /* Every engine runs the block kernel; a TIME or rate change at the block
     * start fades the wet output over its first 64 frames. */
    (void)engine; (void)time; (void)feedback;
    br_fx_delay_begin(st, c);
    br_fx_stage_delay_rate2_tape_block(st, p, sample_base, progress_core, lfo_start,
        lfo_inc, lfo_final, input, n, c, stereo);
    if (st->delay_output_xfade_left) br_fx_delay_fade(st, input, n, stereo);
#else
    if (engine == BR_DELAY_TAPE) {
        uint32_t phase = lfo_start;
        for (i = 0u; i < n; ++i) {
            uint32_t wow_time;
            uint32_t read_pos;
            int32_t echo, send;
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_DELAY);
            send = amount ? br_fx_div127(input[i] * (int32_t)amount) : 0;
            wow_time = (uint32_t)((int32_t)time + br_fx_div_pow2(br_fx_tape_wow_q8(phase), 8u));
            if (wow_time < 1u) wow_time = 1u;
            read_pos = (st->delay_pos - wow_time) & (BR_FX_DELAY_SAMPLES - 1u);
            echo = br_fx_delay_softclip(br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]));
            st->delay_lp = br_fx_pole_bounded(st->delay_lp, echo, br_fx_tape_alpha_full());
            echo = st->delay_lp;
            stereo[i][1] = 0;
            stereo[i][0] = br_fx_stage_delay_finish(st, p, input[i], echo, send,
                c, feedback, &stereo[i][1]);
        }
    } else if (engine == BR_DELAY_MULTITAP) {
        for (i = 0u; i < n; ++i) {
            uint32_t tap;
            int32_t sum = 0, send;
#if BR_FX_MULTITAP_COMBINED_DIV
            int32_t weighted_sum = 0;
#endif
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_DELAY);
            send = amount ? br_fx_div127(input[i] * (int32_t)amount) : 0;
            for (tap = 1u; tap <= 4u; ++tap) {
                uint32_t read_pos = (st->delay_pos - c->delay_tap[tap - 1u]) &
                    (BR_FX_DELAY_SAMPLES - 1u);
#if BR_FX_MULTITAP_COMBINED_DIV
                weighted_sum += (int32_t)br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]) * (int32_t)(5u - tap);
#else
                sum += br_fx_div10((int32_t)br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]) * (int32_t)(5u - tap));
#endif
            }
#if BR_FX_MULTITAP_COMBINED_DIV
            sum = br_fx_div10(weighted_sum);
#endif
            stereo[i][1] = 0;
            stereo[i][0] = br_fx_stage_delay_finish(st, p, input[i], sum, send,
                c, feedback, &stereo[i][1]);
        }
    } else if (engine == BR_DELAY_GLITCH) {
        for (i = 0u; i < n; ++i) {
            uint32_t read_pos;
            int32_t echo, send;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_DELAY);
            send = amount ? br_fx_div127(input[i] * (int32_t)amount) : 0;
            read_pos = (st->delay_pos - time) & (BR_FX_DELAY_SAMPLES - 1u);
            echo = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]);
            if (st->glitch_count == 0u) {
                int32_t step = (int32_t)c->glitch_step;
                st->glitch_hold = br_fx_glitch_quantize(echo, (uint32_t)step);
                st->glitch_count = c->glitch_hold;
            }
            echo = st->glitch_hold;
            --st->glitch_count;
            stereo[i][1] = 0;
            stereo[i][0] = br_fx_stage_delay_finish(st, p, input[i], echo, send,
                c, feedback * 3u >> 2, &stereo[i][1]);
        }
    } else {
        for (i = 0u; i < n; ++i) {
            uint32_t read_pos;
            int32_t echo, send;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_DELAY);
            send = amount ? br_fx_div127(input[i] * (int32_t)amount) : 0;
            read_pos = (st->delay_pos - time) & (BR_FX_DELAY_SAMPLES - 1u);
            echo = br_fx_reverb_decompress16_fast(BR_POLY_DELAY_LINE(p)[read_pos]);
            stereo[i][1] = 0;
            stereo[i][0] = br_fx_stage_delay_finish(st, p, input[i], echo, send,
                c, feedback, &stereo[i][1]);
        }
    }
    st->lfo_phase = lfo_final;
#endif
}
#endif

/* StageEmit uses only constant /2 and /4 divisions. On the PI32 fast path,
 * arithmetic shifts avoid exact-C truncation-bias work; for negative
 * nonmultiples this intentionally differs by at most one LSB. */
static inline __attribute__((always_inline)) int32_t br_fx_stage_emit_div_pow2(
    int32_t value, uint32_t shift)
{
#if BR_FX_STAGE_EMIT_FAST_SHIFTS
    return value >> shift;
#else
    return br_fx_div_pow2(value, shift);
#endif
}

static inline __attribute__((always_inline)) void br_fx_stage_emit_frame(
    uint32_t p, uint32_t i, uint32_t sample_base, uint32_t progress_core, uint32_t pan,
    br_fx_track_t *st, const br_sound_t *s, const br_fx_config_t *cfg,
    const br_fx_controls_t *c, int32_t input, int32_t modded,
    int32_t delayed, int32_t rv_l, int32_t rv_r, int32_t delay_event,
    int32_t reverb_event, uint32_t *tail_samples)
{
    int32_t mod_direct, track_left, track_right;
    uint32_t wet_gain;
    (void)c;
    (void)reverb_event;   /* the shared reverb keeps its own tail */
    if (cfg->mod_mix && (input > 2 || input < -2)) {
        uint32_t short_tail = FS / 2u;
        if (*tail_samples < short_tail) *tail_samples = short_tail;
    }
    if (delay_event && *tail_samples < c->delay_tail_hold)
        *tail_samples = c->delay_tail_hold;
#if !BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB
    if (!s->reverb) rv_l = rv_r = 0;
#endif
    if (*tail_samples) st->tail_storage = 1u;
    st->tail_samples = *tail_samples;

    br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                         BR_FX_PROGRESS_WET_MIX);
#if BR_FX_STAGE_EMIT_TAIL_BOUNDARY
    if (*tail_samples >= FS / 4u) {
        /* `br_fx_tail_gain` is exactly 127 on this domain. Test the state
         * boundary once instead of calculating gain and comparing it again. */
        mod_direct = modded;
    } else {
        wet_gain = br_fx_tail_gain_below_fade(*tail_samples);
        mod_direct = input + br_fx_wet_gain(modded - input, wet_gain);
        rv_l = br_fx_wet_gain(rv_l, wet_gain);
        rv_r = br_fx_wet_gain(rv_r, wet_gain);
        delayed = mod_direct + br_fx_wet_gain(delayed - modded, wet_gain);
    }
#else
    wet_gain = br_fx_tail_gain(*tail_samples);
#if BR_FX_STAGE_FULL_GAIN_UNITY
    if (wet_gain == 127u) {
        /* The exact Q15 unity endpoint is 32766/32768 (-0.0061%). Bypass
         * four per-frame blend helpers during the long full-gain tail; the
         * final fade and gain-zero path stay exact. */
        mod_direct = modded;
    } else {
        mod_direct = input + br_fx_wet_gain(modded - input, wet_gain);
        rv_l = br_fx_wet_gain(rv_l, wet_gain);
        rv_r = br_fx_wet_gain(rv_r, wet_gain);
        delayed = mod_direct + br_fx_wet_gain(delayed - modded, wet_gain);
    }
#else
    mod_direct = input + br_fx_wet_gain(modded - input, wet_gain);
    rv_l = br_fx_wet_gain(rv_l, wet_gain);
    rv_r = br_fx_wet_gain(rv_r, wet_gain);
    delayed = mod_direct + br_fx_wet_gain(delayed - modded, wet_gain);
#endif
#endif
    if (*tail_samples) --*tail_samples;
    st->tail_samples = *tail_samples;
    st->previous_delay_wet = s->delay;
    st->previous_mod_mix = cfg->mod_mix;

    /* Delay stays centred. The shared wet sum reaches both output allpasses,
     * then user PAN follows the wet mix. */
    {
        int32_t centered = br_fx_stage_emit_div_pow2(mod_direct, 2u) +
            br_fx_stage_emit_div_pow2(delayed - mod_direct, 2u);
        track_left = centered + rv_l;
        track_right = centered + rv_r;
    }
    if (pan != 64u) {
#if BR_FX_PAN_COEFF_CACHE
#if BR_FX_STAGE_EMIT_PAN_DIRECT
        br_fx_pan_stereo_cached_noncenter(track_left, track_right, pan,
            c->pan_left_q15, c->pan_right_q15, &track_left, &track_right);
#else
        br_fx_pan_stereo_cached(track_left, track_right, pan,
            c->pan_left_q15, c->pan_right_q15, &track_left, &track_right);
#endif
#else
        br_fx_pan_stereo(track_left, track_right, pan, &track_left, &track_right);
#endif
    }
    br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                         BR_FX_PROGRESS_OUTPUT);
    br_fx_track_stereo[p][sample_base + i][0] = track_left;
    br_fx_track_stereo[p][sample_base + i][1] = track_right;
}

#if BR_FX_GLOBAL_REVERB
#if !BR_FX_STAGE_EMIT_TAIL_BOUNDARY
#error "the shared reverb send is written for the tail-boundary emit"
#endif
/* WIDTH: the side the delay stage returns above its tail-event bit. */
static inline __attribute__((always_inline)) int32_t br_fx_delay_side_of(int32_t event)
{
    return event >> 1;
}

/* A track's reverb send: the post-delay signal at the track's send level. The
 * track keeps its own delay and modulation; the reverb itself is shared. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_fx_stage_send_block(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core, uint32_t pan,
    uint32_t lfo_final, const int32_t *input, const int32_t *mod, uint32_t n,
    const br_sound_t *s, const br_fx_config_t *cfg, const br_fx_controls_t *c,
    uint32_t *tail_samples, int32_t (*stereo)[2])
{
    uint32_t i, wet = c->reverb_wet;
    int32_t *send = &br_gr_send[br_gr_cur][p][sample_base];
#if BR_FX_SEND_BLOCK_HOIST && BR_FX_STAGE_EMIT_TAIL_BOUNDARY && BR_FX_PAN_COEFF_CACHE && \
    BR_FX_STAGE_EMIT_PAN_DIRECT && BR_FX_STAGE_EMIT_CALLER_ZERO_REVERB
    /* The emit step with the shared reverb: no per-track reverb return, so only the
     * mod/delay mix, the tail and PAN remain. Block constants are read once and the
     * track state is written once at the end; per frame the result is the same as
     * br_fx_stage_emit_frame with zero reverb. */
    const uint32_t mod_on = cfg->mod_mix != 0u;
    const uint32_t delay_hold = c->delay_tail_hold;
    const uint32_t pan_l = c->pan_left_q15, pan_r = c->pan_right_q15;
    uint32_t tail = *tail_samples, touched = 0u;
    (void)progress_core;
    /* WIDTH 0 runs this loop; WIDTH on runs the one after it. */
    for (i = 0u; i < (c->delay_width_q15 ? 0u : n); ++i) {
        int32_t in = input[i], modded = mod[i], delayed = stereo[i][0];
        int32_t mod_direct, centered, left, right;
        send[i] = wet ? br_fx_div127(br_fx_clip_ram(delayed, -16000000, 16000000) *
                                     (int32_t)wet) : 0;
        if (mod_on && (in > 2 || in < -2) && tail < FS / 2u) tail = FS / 2u;
        if (stereo[i][1] && tail < delay_hold) tail = delay_hold;
        touched |= tail;
        if (tail >= FS / 4u) {
            mod_direct = modded;
        } else {
            uint32_t g = br_fx_tail_gain_below_fade(tail);
            mod_direct = in + br_fx_wet_gain(modded - in, g);
            delayed = mod_direct + br_fx_wet_gain(delayed - modded, g);
        }
        if (tail) --tail;
        centered = br_fx_stage_emit_div_pow2(mod_direct, 2u) +
            br_fx_stage_emit_div_pow2(delayed - mod_direct, 2u);
        left = right = centered;
        if (pan != 64u) {          /* mono here: out = x * gain on each side */
            left = br_fx_pan_mul(centered, (int32_t)pan_l);
            right = br_fx_pan_mul(centered, (int32_t)pan_r);
        }
        stereo[i][0] = left;
        stereo[i][1] = right;
    }
    if (c->delay_width_q15) {
        /* Delay WIDTH: the same frame as above, plus the echo's side (carried
         * above the event bit) added to the left and subtracted from the
         * right before PAN. A separate loop keeps WIDTH 0 at no cost. */
        for (i = 0u; i < n; ++i) {
            int32_t in = input[i], modded = mod[i], delayed = stereo[i][0];
            int32_t event = stereo[i][1], side = br_fx_delay_side_of(event);
            int32_t mod_direct, centered, left, right;
            send[i] = wet ? br_fx_div127(br_fx_clip_ram(delayed, -16000000, 16000000) *
                                         (int32_t)wet) : 0;
            if (mod_on && (in > 2 || in < -2) && tail < FS / 2u) tail = FS / 2u;
            if ((event & 1) && tail < delay_hold) tail = delay_hold;
            touched |= tail;
            if (tail >= FS / 4u) {
                mod_direct = modded;
            } else {
                uint32_t g = br_fx_tail_gain_below_fade(tail);
                mod_direct = in + br_fx_wet_gain(modded - in, g);
                delayed = mod_direct + br_fx_wet_gain(delayed - modded, g);
                side = br_fx_wet_gain(side, g);
            }
            if (tail) --tail;
            centered = br_fx_stage_emit_div_pow2(mod_direct, 2u) +
                br_fx_stage_emit_div_pow2(delayed - mod_direct, 2u);
            side = br_fx_stage_emit_div_pow2(side, 2u);
            left = centered + side;
            right = centered - side;
            if (pan != 64u)
                br_fx_pan_stereo_cached_noncenter(centered + side, centered - side, pan,
                                                  pan_l, pan_r, &left, &right);
            stereo[i][0] = left;
            stereo[i][1] = right;
        }
    }
    if (touched) st->tail_storage = 1u;
    st->tail_samples = tail;
    *tail_samples = tail;
    st->previous_delay_wet = s->delay;
    st->previous_mod_mix = cfg->mod_mix;
#else
    for (i = 0u; i < n; ++i) {
        int32_t delayed = stereo[i][0];
        send[i] = wet ? br_fx_div127(br_fx_clip_ram(delayed, -16000000, 16000000) *
                                     (int32_t)wet) : 0;
        br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
            input[i], mod[i], delayed, 0, 0, stereo[i][1], 0, tail_samples);
    }
#endif
    st->lfo_phase = lfo_final;
}

/* Cold, once per block: the shared reverb's TYPE, SIZE and DECAY are global
 * (br_master.reverb_*), read here as single words. The coefficients are
 * designed (rv2_design: about 5k float operations, from flash) only when one of the
 * three or an override changes, and applied here, at the block start. rv2_apply
 * slews the line lengths, so SIZE and DECAY changes do not click. A TYPE change
 * switches the output taps at once (SPACE reads only lines 4-7), which clicks; while
 * the reverb sounds, it is applied between a one-block fade-out and fade-in of the
 * return instead (br_gr_duck), outside the reverb itself. */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void br_gr_prepare(void)
{
    uint32_t type = br_fx_reverb_engine_of(br_master.reverb_engine);   /* retired 2 -> 0, 4 -> 3 */
    uint32_t decay = br_fx_bound_ram(br_master.reverb_decay, 127u);
    uint32_t size = br_fx_bound_ram(br_master.reverb_size, 127u);
    uint32_t lfo_active = br_master.global_lfo.depth &&
        (br_master.global_lfo.target == BR_GLOBAL_TARGET_REVERB_SIZE || br_master.global_lfo.target == BR_GLOBAL_TARGET_REVERB_DECAY);
#if !BR_AUDIO_FUSION_ONLY_BUILD
    if (br_fx_pipeline_controls) {
        size = br_fx_pipeline_controls->reverb_size;
        decay = br_fx_pipeline_controls->reverb_decay;
        lfo_active = br_fx_pipeline_controls->reverb_lfo_active;
    } else
#endif
    {
        size = br_global_lfo_value(BR_GLOBAL_TARGET_REVERB_SIZE, size, 127u);
        decay = br_global_lfo_value(BR_GLOBAL_TARGET_REVERB_DECAY, decay, 127u);
    }
    uint32_t manual = br_master.reverb_manual;
    rv2_coeffs_t co;
    uint32_t sync = (manual & 32u) ? br_master.reverb_extra[0] : 0u;
    uint32_t bpm = br_bpm < BR_BPM_MIN ? BR_BPM_MIN : br_bpm > BR_BPM_MAX ? BR_BPM_MAX : br_bpm;
    static const uint8_t sync_ticks[12] = {0u, 8u, 12u, 16u, 18u, 24u, 32u, 36u, 48u, 64u, 72u, 96u};
    uint32_t sync_key, total, base_pre, wanted_mode, changed_mode;
    if (sync > 11u) sync = 0u;
    total = sync ? (RV2_CORE_FS * 5u * sync_ticks[sync] + 8u * bpm) / (16u * bpm) : 0u;
    base_pre = (RV2_CORE_FS * 60u * 68u + 63500u) / 127000u;
    wanted_mode = total > base_pre + RV2_SYNC_PRE_LEN - 1u;
    changed_mode = wanted_mode != br_gr_rv.sync_mode;
    sync_key = sync ? (sync << 16) | bpm : 0u;
    br_gr_overrides.pre = manual & 1u ? br_master.reverb_extra[0] & 127u : RV2_DERIVE;
    if (sync) br_gr_overrides.pre = 68u; /* reserve about 32 ms for full-quality early taps */
    br_gr_overrides.tone = manual & 2u ? br_master.reverb_extra[1] & 127u : RV2_DERIVE;
    br_gr_overrides.mod = manual & 4u ? br_master.reverb_extra[2] & 127u : RV2_DERIVE;
    br_gr_overrides.lowcut = manual & 8u ? br_master.reverb_extra[3] & 127u : RV2_DERIVE;
    br_gr_overrides.width = manual & 16u ? br_master.reverb_extra[4] & 127u : RV2_DERIVE;
    if (br_gr_duck == 2u) br_gr_duck = 0u;                 /* the fade-in has run */
    if (br_gr_key_valid && br_gr_key[0] == type && br_gr_key[1] == decay &&
        br_gr_key[2] == size &&
        br_gr_sync_key == sync_key &&
        !memcmp(&br_gr_key_overrides, &br_gr_overrides, sizeof br_gr_overrides)) return;
    /* SIZE/DECAY redesign performs thousands of floating-point operations.
     * Limit LFO-driven redesigns to 20 Hz; engine, sync and override changes
     * still take effect immediately. rv2_apply slews the resulting line lengths. */
    if (lfo_active && br_gr_key_valid && br_gr_key[0] == type &&
        br_gr_sync_key == sync_key && !changed_mode &&
        !memcmp(&br_gr_key_overrides, &br_gr_overrides, sizeof br_gr_overrides) &&
        br_gr_lfo_frames < FS / 20u) return;
    if (br_gr_key_valid && (br_gr_key[0] != type || changed_mode) && !br_gr_rv.idle && !br_gr_duck) {
        br_gr_duck = 1u;                                    /* fade out first */
        return;
    }
    /* Tempo-only changes reuse the expensive room design; they adjust just the
     * pre-delay and early-tap offsets instead of recalculating the whole FDN. */
    if (br_gr_key_valid && br_gr_key[0] == type && br_gr_key[1] == decay &&
        br_gr_key[2] == size &&
        !memcmp(&br_gr_key_overrides, &br_gr_overrides, sizeof br_gr_overrides)) {
        co = br_gr_design_cache;
    } else {
        br_gr_key[0] = type; br_gr_key[1] = decay; br_gr_key[2] = size;
        br_gr_key_overrides = br_gr_overrides;
        br_gr_lfo_frames = 0u;
        rv2_design(type, size, decay, &br_gr_key_overrides, &co);
        br_gr_design_cache = co;
    }
    if (sync) {
        /* Divisions are measured in 192ths of a quarter note. */
        uint32_t initial = total < (uint32_t)co.pre_target ? total : (uint32_t)co.pre_target;
        int32_t adjust = (int32_t)initial - co.pre_target;
        unsigned i;
        co.pre_target = (int32_t)initial;
        for (i = 0; i < 4u; ++i) co.er_t[i] += adjust;
        br_gr_rv.sync_target = (uint16_t)(total - initial);
    } else br_gr_rv.sync_target = 0u;
    if (changed_mode) {
        memset(br_gr_sync_pre, 0, sizeof br_gr_sync_pre);
        memset(br_gr_rv.sync_hist, 0, sizeof br_gr_rv.sync_hist);
        br_gr_rv.sync_pos = br_gr_rv.sync_phase = br_gr_rv.sync_pending = 0u;
        br_gr_rv.sync_mode = (uint8_t)wanted_mode;
    }
    if (br_gr_rv.idle || changed_mode) br_gr_rv.sync_d = br_gr_rv.sync_target;
    br_gr_sync_key = sync_key;
    rv2_apply(&br_gr_rv, &co);
    if (br_gr_rv.idle || changed_mode) {
        unsigned i;
        /* An idle tank does not process samples, so its delay lengths cannot
         * slew to a new room until a note arrives. Start silent rooms at the
         * new design; keep the existing slew for audible tails. */
        if (br_gr_rv.idle) for (i = 0u; i < RV2_LINES; ++i)
            br_gr_rv.base[i] = RV2_LN(&br_gr_rv, i, d) = br_gr_rv.target[i];
        br_gr_rv.pre_d = co.pre_target;
        for (i = 0; i < 4u; ++i) br_gr_rv.er_d[i] = co.er_t[i];
    }
    br_gr_key_valid = 1u;
    if (br_gr_duck == 1u) br_gr_duck = 2u;                 /* then fade in */
}

/* Once per block, before any job runs: swap the send buffers and decide whether
 * the reverb has the previous block's sends, or a tail, to work on. The reverb
 * reports idle (rv2_idle) once its tail has ended and its lines are clear. */
static BR_FX_PREPARE_CODE void br_gr_plan(void)
{
    uint32_t p, prev = br_gr_cur, present = 0u;
    for (p = 0u; p < BR_PARTS; ++p) present |= br_gr_have[prev][p];
    br_gr_cur = prev ^ 1u;
    for (p = 0u; p < BR_PARTS; ++p) br_gr_have[br_gr_cur][p] = 0u;
    br_gr_run = present || !br_gr_rv.idle;
}

/* ---- DRIVE: a guitar-pedal style stage ahead of the track's modulation and delay.
 * Three shapers (overdrive: smooth symmetric cubic, odd harmonics; distortion: a hard knee driven
 * off centre, so it adds even harmonics as well; fuzz: high gain into an asymmetric squarish clip),
 * a DC blocker after the two asymmetric ones and a tone low-pass that is fully open at 127.
 * The SEND adds the driven signal to the dry one, which stays at unity: out = dry + s*M*T*wet.
 * M matches the wet's RMS to the dry's (about 30 ms smoothing, within +-6 dB, held while the dry is
 * near silence) and T is -3 dB, so a full send adds about +4.5 dB and never makes a track quieter.
 * Turned to zero, the wet part fades out over one more block before the stage stops running.
 * Stored in the existing fields: mode = drive_mode, gain = drive_gain, tone = drive_tone,
 * send = sound.drive. The block setup runs from flash, the per-sample loop from RAM. */
#define BR_DRIVE_FS 32768
#define BR_DRIVE_DIST_BIAS 0.9f
#define BR_DRIVE_DIST_CENTRE 0.375f       /* br_adaa_dist_f(BR_DRIVE_DIST_BIAS) */
static const uint32_t br_drive_gain_q8[3][17] = {
    {256, 296, 341, 394, 455, 526, 607, 701, 810, 935, 1080, 1247, 1440, 1662, 1920, 2217, 2560},
    {1024, 1213, 1437, 1701, 2015, 2387, 2827, 3348, 3966, 4697, 5564, 6590, 7805, 9244, 10949, 12968, 15360},
    {512, 654, 835, 1066, 1361, 1739, 2220, 2835, 3620, 4623, 5904, 7539, 9627, 12294, 15699, 20047, 25600}
};
/* tone: one-pole coefficient, Q15, cutoffs log-spaced 400 Hz .. 16 kHz (TONE 0..120), then 0.987 at 127 (open) */
static const uint32_t br_drive_gain_q15[17] = {
    1815, 2303, 2916, 3682, 4632, 5803, 7229, 8943, 10969, 13311, 15943, 18796, 21751, 24639, 27256, 29415, 32768
};
static uint32_t br_drive_lerp17(const uint32_t *t, uint32_t v)   /* v: 0..127 over 16 steps */
{
    uint32_t i = v >> 3, f = v & 7u;
    if (i >= 16u) return t[16];
    return t[i] + (uint32_t)(((int32_t)t[i + 1u] - (int32_t)t[i]) * (int32_t)f / 8);
}
/* First-order antiderivative anti-aliasing (ADAA). A shaper f is replaced by the mean of f over the
 * step between two samples, (F(b) - F(a)) / (b - a), with F the antiderivative of f. That removes most of
 * the harmonics that would fold back over Nyquist. Where both samples sit on one polynomial piece the
 * mean has a closed form with no division; a step across a corner uses F and one float division; a tiny
 * step falls back to f at the midpoint. All values are in units of full scale. */
#define BR_ADAA_EPS (1.0f / 1024.0f)

/* The shapers inline into the per-sample loop, which runs from RAM (br_fx_drive_run). */
#define BR_ADAA_FN static inline __attribute__((always_inline))
BR_ADAA_FN float br_adaa_od_f(float x)
{
    return x >= 1.0f ? 2.0f / 3.0f : x <= -1.0f ? -2.0f / 3.0f : x - x * x * x * (1.0f / 3.0f);
}
BR_ADAA_FN float br_adaa_od_F(float x)       /* even */
{
    float ax = x < 0.0f ? -x : x;
    return ax <= 1.0f ? x * x * (0.5f - x * x * (1.0f / 12.0f)) : 5.0f / 12.0f + (2.0f / 3.0f) * (ax - 1.0f);
}
BR_ADAA_FN float br_adaa_dist_f(float x)
{
    float ax = x < 0.0f ? -x : x;
    float m = ax <= 0.3f ? ax : ax < 4.0f ? 0.3f + (ax - 0.3f) * 0.125f : 0.7625f;
    return x < 0.0f ? -m : m;
}
BR_ADAA_FN float br_adaa_dist_F(float x)     /* even */
{
    float ax = x < 0.0f ? -x : x, t;
    if (ax <= 0.3f) return ax * ax * 0.5f;
    if (ax < 4.0f) { t = ax - 0.3f; return 0.045f + 0.3f * t + t * t * (1.0f / 16.0f); }
    return 2.010625f + 0.7625f * (ax - 4.0f);
}
BR_ADAA_FN float br_adaa_fuzz_f(float x)
{
    return x > 0.7f ? 0.7f : x < -0.45f ? -0.45f : x;
}
BR_ADAA_FN float br_adaa_fuzz_F(float x)
{
    if (x > 0.7f) return 0.7f * x - 0.245f;
    if (x < -0.45f) return -0.45f * x - 0.10125f;
    return x * x * 0.5f;
}
/* The polynomial piece a sample sits on (distortion: 0..4, fuzz: 0..2); overdrive does not use it. */
BR_ADAA_FN uint32_t br_adaa_piece(uint32_t mode, float x)
{
    if (mode == 1u)
        return (uint32_t)(x > -4.0f) + (uint32_t)(x > -0.3f) + (uint32_t)(x > 0.3f) + (uint32_t)(x > 4.0f);
    if (mode == 2u) return (uint32_t)(x > -0.45f) + (uint32_t)(x > 0.7f);
    return 0u;
}
/* F(b) - F(a) for a step across a corner, with one copy of F for both ends: the loop is kept rolled,
 * which keeps the distortion's long F out of RAM twice (corner samples only; same operations). */
BR_ADAA_FN float br_adaa_F_diff(uint32_t mode, float a, float b)
{
    float fb = 0.0f, v = b, f;
    uint32_t k;
#pragma clang loop unroll(disable)
    for (k = 0u; k < 2u; ++k) {
        f = mode == 0u ? br_adaa_od_F(v) : mode == 1u ? br_adaa_dist_F(v) : br_adaa_fuzz_F(v);
        if (k) break;
        fb = f; v = a;
    }
    return fb - f;
}
/* ia, ib: the pieces of a and b; the loop carries b's piece on as the next sample's a. */
BR_ADAA_FN float br_adaa_step(uint32_t mode, float a, float b, uint32_t ia, uint32_t ib)
{
    float d = b - a;
    if (mode == 0u) {
        if (a >= -1.0f && a <= 1.0f && b >= -1.0f && b <= 1.0f) {
            float s = a + b;
            return s * (0.5f - (a * a + b * b) * (1.0f / 12.0f));
        }
        if ((a >= 1.0f && b >= 1.0f) || (a <= -1.0f && b <= -1.0f) ||   /* both clipped: f(b); */
            (d < BR_ADAA_EPS && d > -BR_ADAA_EPS))                      /* a tiny step: f at the midpoint */
            return br_adaa_od_f((a >= 1.0f && b >= 1.0f) || (a <= -1.0f && b <= -1.0f) ? b : 0.5f * (a + b));
        return (br_adaa_od_F(b) - br_adaa_od_F(a)) / d;
    }
    if (mode == 1u) {
        if (ia == ib || (d < BR_ADAA_EPS && d > -BR_ADAA_EPS)) return br_adaa_dist_f(0.5f * (a + b));
        return br_adaa_F_diff(1u, a, b) / d;
    }
    if (ia == ib || (d < BR_ADAA_EPS && d > -BR_ADAA_EPS)) return br_adaa_fuzz_f(0.5f * (a + b));
    return (br_adaa_fuzz_F(b) - br_adaa_fuzz_F(a)) / d;
}
/* floor(d * a / 2^15), low 32 bits, for 0 <= a <= 2^15: one 32x32->64 product and a pair shift.
 * The same bits as the former two-product split (hi*a + (lo*a >> 15), d = hi*2^15 + lo), which wraps
 * modulo 2^32 exactly as this truncation does. */
static inline __attribute__((always_inline)) int32_t br_fx_mul_q15_split(int32_t d, int32_t a)
{
    return (int32_t)(((int64_t)d * a) >> 15);
}
/* What the block setup (flash) hands the per-sample loop (RAM) and what comes back. */
typedef struct {
    float gx, prev, g, step, bias, centre;       /* in: input gain, last input (shaper units), wet gain and its ramp step */
    float sd, sw;                  /* out: dry and wet energies of the block */
    int32_t alpha, lp, dc, dc_enabled;         /* tone coefficient; tone and DC-blocker state (Q12), in and out */
} br_drive_run_t;
/* The wet path for one shaper (one copy per mode, so the choice is not tested per sample). */
static inline __attribute__((always_inline)) void br_fx_drive_loop(uint32_t mode,
    const int32_t *in, int32_t *out, uint32_t n, br_drive_run_t *r, float bias, float centre, float out_scale)
{
    const float gx = r->gx;
    float prev = r->prev;
    uint32_t i, ia = br_adaa_piece(mode, prev);
    for (i = 0u; i < n; ++i) {
        const float x = (float)in[i] * gx + bias;
        const uint32_t ib = br_adaa_piece(mode, x);
        int32_t y = (int32_t)((br_adaa_step(mode, prev, x, ia, ib) - centre) * out_scale);
        prev = x; ia = ib;
        out[i] = y;
    }
}
/* The per-sample work, from RAM and call-free. Returns 0 (out and r untouched) for a silent block. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) uint32_t br_fx_drive_run(uint32_t mode,
    const int32_t *in, int32_t *out, uint32_t n, br_drive_run_t *r)
{
    uint32_t i;
    for (i = 0u; i < n && !in[i]; ++i) {}
    if (i == n) return 0u;
    if (mode == 0u) br_fx_drive_loop(0u, in, out, n, r, r->bias, r->centre, (float)BR_DRIVE_FS * 0.5f);
    else if (mode == 1u) br_fx_drive_loop(1u, in, out, n, r, r->bias, r->centre,
                                          (float)BR_DRIVE_FS * 0.4f);
    else br_fx_drive_loop(2u, in, out, n, r, r->bias, r->centre, (float)BR_DRIVE_FS * 0.5f);
    {                                                   /* DC blocker and tone, shared by all shapers */
        int32_t lp = r->lp, dc = r->dc, alpha = r->alpha;
        uint32_t dc_enabled = mode != 0u || r->dc_enabled;
        for (i = 0u; i < n; ++i) {
            int32_t y = out[i];
            if (dc_enabled) { dc += (y * 4096 - dc) >> 9; y -= (dc + 2048) >> 12; }
            lp += br_fx_mul_q15_split(y * 4096 - lp, alpha);
            out[i] = (lp + 2048) >> 12;
        }
        r->lp = lp; r->dc = dc;
    }
    {                                                   /* dry at unity plus the wet, its gain ramped */
        float g = r->g, step = r->step, sd = 0.0f, sw = 0.0f;
        for (i = 0u; i < n; ++i) {
            float d = (float)in[i], w = (float)out[i];
            sd += d * d; sw += w * w;
            g += step;
            out[i] = in[i] + (int32_t)(w * g);
        }
        r->sd = sd; r->sw = sw;
    }
    return 1u;
}
static BR_FX_PREPARE_CODE float br_drive_sqrt(float r)    /* r in [1/4, 4]: 1/sqrt by bit trick, two Newton steps */
{
    union { float f; uint32_t u; } v;
    float y;
    v.f = r; v.u = 0x5f3759dfu - (v.u >> 1); y = v.f;
    y *= 1.5f - 0.5f * r * y * y;
    y *= 1.5f - 0.5f * r * y * y;
    return r * y;
}
/* The block setup, from flash; the samples go through br_fx_drive_run in RAM. Returns 0 (out untouched,
 * use in) for a silent block, which costs one scan; a send turned to 0 while silent stops the stage there. */
static BR_FX_PREPARE_CODE uint32_t br_fx_drive_block(uint32_t p, const int32_t *in, int32_t *out, uint32_t n)
{
    br_fx_track_t *st = &br_fx_track[p];
    const br_sound_t *s = br_fx_sound_at(p);
    const br_fx_config_t *cfg = br_fx_config_at(p);
    uint32_t mode = cfg->drive_mode > 2u ? 0u : cfg->drive_mode, send = s->drive > 127u ? 127u : s->drive;
    uint32_t gain = br_drive_lerp17(br_drive_gain_q8[mode], cfg->drive_gain > 127u ? 127u : cfg->drive_gain);
    br_drive_run_t r;
    const float gx = (float)gain * (1.0f / 8388608.0f);              /* gain / 256, in units of full scale */
    float prev = (float)st->drive_prev * gx, g = st->drive_send ? st->drive_wet : 0.0f, m = st->drive_m, sd, sw;
    const float g_to = (float)send * m * (0.70795f / 127.0f);       /* s * M * T, T = -3 dB */
    r.alpha = (int32_t)br_drive_lerp17(br_drive_gain_q15, cfg->drive_tone > 127u ? 127u : cfg->drive_tone);
    r.lp = st->drive_lp; r.dc = st->drive_dc;                       /* Q12, so both settle to exactly zero */
    r.dc_enabled = br_part_pending[p].drive_bias != 0u;
    r.bias = (mode == 1u ? BR_DRIVE_DIST_BIAS : 0.0f) + (float)br_fx_bound_ram(br_part_pending[p].drive_bias, 127u) * (0.8f / 127.0f);
    r.centre = mode == 0u ? br_adaa_od_f(r.bias) : mode == 1u ? br_adaa_dist_f(r.bias) : br_adaa_fuzz_f(r.bias);
    r.gx = gx; r.prev = prev + r.bias; r.g = g;
    r.step = n ? (g_to - g) / (float)n : 0.0f;
    if (!BR_FX_PREPARE_INVOKE(br_fx_drive_run, mode, in, out, n, &r)) {   /* silent (or empty) block */
        if (!send) st->drive_send = 0;
        return 0u;
    }
    sd = r.sd; sw = r.sw;
    if (sd >= (float)n * 1024.0f) {                     /* M is held while the dry is below -60 dBFS */
        float k = st->drive_send && st->drive_wet > 0.0f ? (float)n * (1.0f / 1323.0f) : 1.0f;   /* a fresh start primes */
        float ed = st->drive_ed + (sd - st->drive_ed) * k, ew = st->drive_ew + (sw - st->drive_ew) * k;
        st->drive_ed = ed; st->drive_ew = ew;
        m = ew * 4.0f < ed ? 2.0f : ed * 4.0f < ew ? 0.5f : br_drive_sqrt(ed / ew);
    }
    st->drive_m = m; st->drive_wet = g_to; st->drive_send = (int32_t)send;
    st->drive_prev = in[n - 1u];
    st->drive_lp = r.lp; st->drive_dc = r.dc;
    return 1u;
}

/* The shared reverb's block, from flash: sum the sends the tracks left last block
 * into that block's first row (the send is in the domain the reverb is calibrated
 * for: a held note at SEND 127 returns about +6 dB over its dry track, whose level
 * per channel is a quarter of the send), run the reverb, and leave the stereo
 * return in that block's next two rows. */
static BR_FX_PREPARE_CODE __attribute__((noinline)) void br_gr_block(uint32_t n)
{
    const int32_t *row[BR_PARTS];
    uint32_t i, k, p, rows = 0u, prev = br_gr_cur ^ 1u;
    int32_t *in = br_gr_send[prev][BR_GR_IN_ROW];
    int32_t *wet_l = br_gr_send[prev][BR_GR_WET_L_ROW], *wet_r = br_gr_send[prev][BR_GR_WET_R_ROW];
    for (p = 0u; p < BR_PARTS; ++p)
        if (br_gr_have[prev][p]) row[rows++] = br_gr_send[prev][p];
    /* |send| <= 16,000,000 per track: the sum of four fits int32 (the reverb clips it). */
    for (i = 0u; i < n; ++i) {
        int32_t sum = 0;
        for (k = 0u; k < rows; ++k) sum += row[k][i];
        in[i] = sum;
    }
    rv2_process(&br_gr_rv, in, wet_l, wet_r, n & ~1u);   /* the reverb runs frame pairs */
    if (n & 1u) wet_l[n - 1u] = wet_r[n - 1u] = 0;      /* (never at the 256-frame block) */
    if (br_gr_duck && n) {                              /* TYPE change: linear fade, |wet| <= 65,535 */
        for (i = 0u; i < n; ++i) {
            int32_t g = (int32_t)(br_gr_duck == 1u ? n - 1u - i : i + 1u);
            wet_l[i] = wet_l[i] * g / (int32_t)n;
            wet_r[i] = wet_r[i] * g / (int32_t)n;
        }
    }
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    for (i = 0u; i < n; ++i) {
        uint32_t a = (uint32_t)(wet_l[i] < 0 ? -wet_l[i] : wet_l[i]);
        if (a > br_gr_stats[4]) br_gr_stats[4] = a;
    }
#endif
}

/* The shared reverb for one block, run by the host track's job (or as its own job
 * in a split graph). Only this entry is RAM code besides the reverb's per-sample
 * core; an idle reverb returns before touching flash. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_gr_job(uint32_t n)
{
#if defined(FM1_DEVICE)
    uint32_t started = fm1_ticks();
#endif
    if (!br_gr_run) return;
    /* Never run on undesigned coefficients. */
    if (!br_gr_key_valid || !br_gr_rv.configured) {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        ++br_gr_stats[1];
#endif
        return;
    }
    BR_FX_PREPARE_INVOKE(br_gr_block, n);
    br_gr_out_seq = br_fx_block_seq;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    ++br_gr_stats[0];
    br_gr_stats[5] = br_gr_key[0]; br_gr_stats[6] = br_gr_key[1]; br_gr_stats[7] = br_gr_key[2];
#endif
#if defined(FM1_DEVICE)
    started = fm1_ticks() - started;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_fx_gr_work_ticks += started;
    br_gr_stats[2] = started;
    if (started > br_gr_stats[3]) br_gr_stats[3] = started;
#else
    br_gr_cost_ticks = started;
#endif
#endif
}

/* Add the reverb return to the finished mix for frames [base, end): only when this
 * block's reverb job wrote it (the rows hold sends otherwise). */
static BR_FX_RAM_AUDIO void br_gr_add_range(int32_t *out, uint32_t base, uint32_t end)
{
    uint32_t i, prev = br_gr_cur ^ 1u;
    const int32_t *wet_l = br_gr_send[prev][BR_GR_WET_L_ROW], *wet_r = br_gr_send[prev][BR_GR_WET_R_ROW];
    if (!br_gr_run || br_gr_out_seq != br_fx_block_seq) return;
    for (i = base; i < end; ++i) {
        out[2u * i] += wet_l[i];
        out[2u * i + 1u] += wet_r[i];
    }
}
#endif /* BR_FX_GLOBAL_REVERB */


#if BR_FX_STAGED_COMPACT_CALL_ABI
static BR_FX_RAM_AUDIO int br_fx_process_track_job_staged_block(
    uint32_t p, const int32_t *input_bus, uint32_t n, uint32_t sample_base,
    uint32_t pan, uint32_t progress_core
    )
#else
static BR_FX_RAM_AUDIO int br_fx_process_track_job_staged_block(
    uint32_t p, const int32_t *input_bus, uint32_t n, uint32_t sample_base,
    uint32_t pan, uint32_t progress_core, br_fx_track_t *st,
    const br_sound_t *s, const br_fx_config_t *cfg,
    const br_fx_controls_t *c
    )
#endif
{
#if BR_FX_STAGED_COMPACT_CALL_ABI
    /* p was range-checked by br_fx_process_track_job_impl before staging. The
     * active block controls/sound/config are immutable for this worker job. */
    br_fx_track_t *st = &br_fx_track[p];
    const br_sound_t *s = br_fx_sound_at(p);
    const br_fx_config_t *cfg = br_fx_config_at(p);
    const br_fx_controls_t *c = &br_fx_controls[p];
#endif
    uint32_t i;
    uint32_t tail_samples;
    uint32_t lfo_start, lfo_final, lfo_phase;
    int32_t *mod = br_fx_stage_mod[p];
    int32_t (*stereo)[2] = &br_fx_track_stereo[p][sample_base];
#if BR_FX_STAGE_SKIP_IDLE && BR_FX_BLOCK_STAGE_SPECIALIZE_MOD && BR_FX_MOD_RATE2
    const int32_t *mod_out = c->mod_mix ? mod : input_bus;   /* MIX 0 leaves the input unchanged */
#else
    const int32_t *mod_out = mod;
#endif
    (void)i;
    (void)lfo_phase;

    tail_samples = st->tail_samples;
    lfo_start = st->lfo_phase;

    /* Keep each recursive effect's sample order while grouping work by stage.
     * The delay and reverb feedback loops still see their own previous writes. */
#if BR_FX_BLOCK_STAGE_SPECIALIZE_MOD
    br_fx_stage_mod_block(st, p, sample_base, progress_core, input_bus, n, c, mod);
#else
    for (i = 0u; i < n; ++i) {
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_BUS_READ);
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_MOD);
        mod[i] = br_fx_mod(st, p, input_bus[i], c);
    }
#endif
    lfo_final = st->lfo_phase;

#if BR_FX_BLOCK_STAGE_SPECIALIZE_DELAY
    br_fx_stage_delay_block(st, p, sample_base, progress_core, lfo_start, c->mod_lfo_inc,
                            lfo_final, mod_out, n, c, stereo);
#else
    lfo_phase = lfo_start;
    for (i = 0u; i < n; ++i) {
        int32_t tail_event = 0;
        int32_t delayed;
        lfo_phase += c->mod_lfo_inc;
        st->lfo_phase = lfo_phase;
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_DELAY);
        delayed = br_fx_delay(st, p, mod[i], c BR_FX_TAIL_EVENT_ARG(&tail_event));
        stereo[i][0] = delayed;
        stereo[i][1] = tail_event;
    }
    st->lfo_phase = lfo_final;
#endif

#if BR_FX_GLOBAL_REVERB
    br_fx_stage_send_block(st, p, sample_base, progress_core, pan, lfo_final,
        input_bus, mod_out, n, s, cfg, c, &tail_samples, stereo);
#endif
    st->lfo_phase = lfo_final;
    return 1;
}

static inline __attribute__((always_inline)) void br_fx_stage_clear_memories(
    uint32_t p, br_fx_track_t *st, uint32_t clear_all, uint32_t clear_delay,
    uint32_t clear_reverb, uint32_t clear_mod)
{
    if (clear_all) clear_delay = clear_reverb = clear_mod = 1u;
    if (clear_delay) {
        br_fx_clear32((br_fx_clear_word_t *)BR_POLY_DELAY_LINE(p),
                      sizeof br_delay_line[p] / 4u);
#if BR_FX_DELAY_RATE2
        BR_FX_PREPARE_INVOKE(br_fx_delay_storage_reset, st, st->delay_storage_shift);
        st->delay_output_last = 0;
        st->delay_output_xfade_left = 0u;
        st->glitch_half_error = 0u;
#else
        st->delay_lp = 0;
        st->delay_hp = 0;
#endif
    }
    (void)clear_reverb;   /* the shared reverb has no per-track memory */
    if (clear_mod) {
        br_fx_clear32((br_fx_clear_word_t *)br_mod_line[p],
                      sizeof br_mod_line[p] / 4u);
        st->mod_feedback_last = 0;
        br_fx_clear32((br_fx_clear_word_t *)st->phase_state,
                      sizeof st->phase_state / sizeof(uint32_t));
#if BR_FX_MOD_RATE2
        br_fx_clear32((br_fx_clear_word_t *)st->mod_rate2_input_history,
                      sizeof st->mod_rate2_input_history / sizeof(uint32_t));
        st->mod_rate2_previous_output = 0;
        st->mod_rate2_phase = 2u;
#endif
    }
    if (clear_all) st->tail_storage = 0u;
}

static inline __attribute__((always_inline)) void br_fx_stage_dry_sample(
    uint32_t p, uint32_t sample, uint32_t progress_core, uint32_t pan,
    uint32_t left_q15, uint32_t right_q15, int32_t input)
{
    int32_t direct = br_fx_div_pow2(input, 2u);
    int32_t left, right;
    (void)left_q15;
    (void)right_q15;
    br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample,
                         BR_FX_PROGRESS_BUS_READ);
    if (pan == 64u) left = right = direct;
    else {
#if BR_FX_PAN_COEFF_CACHE
        br_fx_pan_stereo_cached(direct, direct, pan, left_q15, right_q15,
                                &left, &right);
#else
        br_fx_pan_stereo(direct, direct, pan, &left, &right);
#endif
    }
    br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample,
                         BR_FX_PROGRESS_OUTPUT);
    br_fx_track_stereo[p][sample][0] = left;
    br_fx_track_stereo[p][sample][1] = right;
}

#if BR_FX_STAGED_COMPACT_CALL_ABI
static BR_FX_RAM_AUDIO int br_fx_process_track_job_staged(
    uint32_t p, const int32_t *input_bus, uint32_t n, uint32_t enabled,
    uint32_t pan, uint32_t progress_core
    )
#else
static BR_FX_RAM_AUDIO int br_fx_process_track_job_staged(
    uint32_t p, const int32_t *input_bus, uint32_t n, uint32_t enabled,
    uint32_t pan, uint32_t progress_core, br_fx_track_t *st,
    const br_sound_t *s, const br_fx_config_t *cfg,
    const br_fx_controls_t *c
    )
#endif
{
#if BR_FX_STAGED_COMPACT_CALL_ABI
    /* These are the same per-part objects used by the caller; the block
     * snapshot is stable until all worker jobs join. */
    br_fx_track_t *st = &br_fx_track[p];
    const br_sound_t *s = br_fx_sound_at(p);
    const br_fx_config_t *cfg = br_fx_config_at(p);
    const br_fx_controls_t *c = &br_fx_controls[p];
#endif
    uint32_t i;
    uint32_t transition_reverb, transition_mod;
    uint32_t active_wet, restart_possible = 0u;

    if (!n) return 1;
    if (!input_bus || n > BR_BLOCK) return 0;
    /* DELAY MIX reaching 0 is not an edge: the send stops and the unity return lets
     * the tail already in the line decay (the delay stage skips itself once the
     * line holds only zeros). */
    transition_reverb = 0u;   /* the shared reverb has no per-track edge */
    transition_mod = st->previous_mod_mix && !cfg->mod_mix;
    active_wet = s->delay || s->reverb || cfg->mod_mix;
    if (st->tail_storage && st->tail_samples < n && active_wet) {
        for (i = 0u; i < n; ++i) {
            if (input_bus[i]) {
                restart_possible = 1u;
                break;
            }
        }
    }

#if BR_FX_SKIP_SILENT_TRACK
    /* A silent block after the tail has run out produces no output worth
     * computing: emit silence and leave the storage for the existing
     * clear-on-next-input path. Effect transitions still take the full path. */
    if (!st->tail_samples && !transition_reverb && !transition_mod) {
        for (i = 0u; i < n && !input_bus[i]; ++i) {}
        if (i == n) {
            for (i = 0u; i < n; ++i)
                br_fx_track_stereo[p][i][0] = br_fx_track_stereo[p][i][1] = 0;
#if BR_FX_GLOBAL_REVERB
            br_gr_have[br_gr_cur][p] = 0u;     /* silence: nothing for the reverb to hear */
#endif
            br_fx_track_silent_seq[p] = br_fx_block_seq;
            return 1;
        }
    }
#endif
    /* A disabled track whose tail outlasts this block cannot reach the bypass
     * point inside it, so it is staged as one block like an enabled one. */
    if ((enabled || st->tail_samples >= n) && !transition_reverb && !transition_mod &&
        !restart_possible) {
#if BR_FX_STAGED_COMPACT_CALL_ABI
        return br_fx_process_track_job_staged_block(p, input_bus, n, 0u, pan,
                                                     progress_core
                                                     );
#else
        return br_fx_process_track_job_staged_block(p, input_bus, n, 0u, pan,
            progress_core, st, s, cfg, c
            );
#endif
    }

#if BR_FX_GLOBAL_REVERB
    if (c->reverb_wet)
        br_fx_clear32((br_fx_clear_word_t *)br_gr_send[br_gr_cur][p], n);
#endif
    /* Edge blocks are deliberately processed one frame at a time. That keeps
     * the old bypass point, falling-edge clears, and tail-expiry re-excitation
     * boundary exact while the common active path remains block-staged. This
     * diagnostic marks edge work as unprofiled instead of reading the timer
     * once or more per sample. */
    for (i = 0u; i < n; ++i) {
        int32_t input = input_bus[i];
        uint32_t clear_tail, clear_reverb, clear_mod;
        if (!enabled && !st->tail_samples) {
            br_fx_stage_dry_sample(p, i, progress_core, pan,
#if BR_FX_PAN_COEFF_CACHE
                c->pan_left_q15, c->pan_right_q15,
#else
                0u, 0u,
#endif
                input);
            continue;
        }
        clear_tail = !st->tail_samples && st->tail_storage && input && active_wet;
        clear_reverb = 0u;
        clear_mod = st->previous_mod_mix && !cfg->mod_mix;
        if (clear_tail || clear_reverb || clear_mod) {
            br_fx_stage_clear_memories(p, st, clear_tail, 0u,
                                       clear_reverb, clear_mod);
            if (!enabled && (clear_reverb || clear_mod))
                st->tail_samples = 0u;
        }
#if BR_FX_STAGED_COMPACT_CALL_ABI
        (void)br_fx_process_track_job_staged_block(p, input_bus + i, 1u, i,
            pan, progress_core
            );
#else
        (void)br_fx_process_track_job_staged_block(p, input_bus + i, 1u, i,
            pan, progress_core, st, s, cfg, c
            );
#endif
    }
    return 1;
}
#endif

#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
/* A deliberately conservative 50 us inter-chunk gap, measured by the
 * crystal-based T4 rather than CPU instruction timing. Diagnostic only. */
static uint32_t br_paced_clear_used;
#if defined(BR_PACED_CLEAR_HOST_TEST)
extern void br_paced_clear_test_gap(void *address, uint32_t bytes, uint32_t delay_us);
#endif
static void br_paced_clear_buffer(void *ptr, uint32_t size)
{
    uint32_t offset = 0u;
    while (offset < size) {
        uint32_t n = size - offset > 4096u ? 4096u : size - offset;
        memset((uint8_t *)ptr + offset, 0, n);
#if defined(BR_PACED_CLEAR_HOST_TEST)
        br_paced_clear_test_gap((uint8_t *)ptr + offset, n, 50u);
#else
        fm1_delay_us(50u);
#endif
        offset += n;
    }
}
#endif

BR_AUDIO_API __attribute__((minsize, noinline)) void br_fx_init(void)
{
    uint32_t p;
#if BR_FX_STABLE_CONTROL_CACHE
    br_fx_control_key_valid = 0u;
#endif
#if defined(BR_DIAGNOSTIC_TEST) && defined(BR_SPLIT_AUDIO)
    extern uint32_t br_fx_init_test_calls;
    ++br_fx_init_test_calls;
#endif
#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
    if (!br_paced_clear_used) {
        br_paced_clear_used = 1u;
        br_paced_clear_active = 1u;
        br_paced_clear_buffer(br_delay_line, sizeof br_delay_line);
        br_paced_clear_buffer(br_gr_pool16, sizeof br_gr_pool16);
        br_paced_clear_buffer(br_mod_line, sizeof br_mod_line);
    } else
#endif
    {
        memset(br_delay_line, 0, sizeof br_delay_line);
        memset(br_mod_line, 0, sizeof br_mod_line);
    }
#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
    if (br_paced_clear_active) br_paced_clear_buffer(br_fx_track, sizeof br_fx_track);
    else
#endif
        memset(br_fx_track, 0, sizeof br_fx_track);
#if BR_FX_GLOBAL_REVERB
    memset(&br_gf, 0, sizeof br_gf);
    rv2_init(&br_gr_rv, br_gr_pool16, br_gr_pool32);   /* clears its pools; idle */
    br_gr_rv.sync_pre = br_gr_sync_pre;
    memset(br_gr_sync_pre, 0, sizeof br_gr_sync_pre);
    br_gr_sync_key = 0xffffffffu;
    memset(br_gr_send, 0, sizeof br_gr_send);
    memset(br_gr_have, 0, sizeof br_gr_have);
    br_gr_key_valid = br_gr_wanted = br_gr_run = br_gr_cur = br_gr_duck = br_gr_lfo_frames = 0u;
    br_gr_out_seq = br_fx_block_seq - 1u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    memset((void *)br_gr_stats, 0, sizeof br_gr_stats);
#else
    br_gr_cost_ticks = 0u;
#endif
#endif
    br_fx_fusion_pending = 0u;
    for (p = 0; p < BR_PARTS; ++p) br_fx_fusion_done[p] = 0u;
#if BR_FX_MOD_RATE2
    for (p = 0u; p < BR_PARTS; ++p) br_fx_track[p].mod_rate2_phase = 2u;
#endif
    br_fx_job_bus = 0;
    br_fx_job_n = 0u;
#if !BR_AUDIO_FUSION_ONLY_BUILD
    br_fx_pipeline_keys = 0;
    br_fx_pipeline_controls = 0;
    br_fx_pipeline_cpu0_only = 0u;
#endif
    br_comp_init();
    br_master_fx_init();
#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
    br_paced_clear_active = 0u;
#endif
}

static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_fx_process_track_job_impl(
    uint32_t p, const int32_t *input_bus, uint32_t n
    )
{
    uint32_t i;
    uint32_t progress_core;
    br_fx_track_t *st;
    const br_sound_t *s;
    const br_fx_config_t *cfg;
    const br_fx_controls_t *c;
    progress_core = fm1_fault_diag_read_cpu_id();
#if BR_FX_BLOCK_STAGE_ONLY_BUILD
    (void)i;
#endif
#if defined(FM1_DEVICE)
    uint32_t job_start = fm1_ticks();
#endif
    if (p >= BR_PARTS || progress_core > 1u) return;
#if BR_FX_GLOBAL_REVERB
    br_gr_have[br_gr_cur][p] = br_fx_controls[p].reverb_wet != 0u;   /* silent tracks clear it */
#endif
    if ((br_fx_sound_at(p)->drive || br_fx_track[p].drive_send) &&   /* DRIVE first; at send 0 one more block fades it */
        BR_FX_PREPARE_INVOKE(br_fx_drive_block, p, input_bus, br_fx_drive_buf[p], n))
        input_bus = br_fx_drive_buf[p];
    br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, 0u, BR_FX_PROGRESS_ENTRY);
    const uint32_t enabled = br_fx_job_enabled[p];
    st = &br_fx_track[p];
    s = br_fx_sound_at(p);
    cfg = br_fx_config_at(p);
    c = &br_fx_controls[p];
    /* A PAN change: run this block at centre, then ramp (br_fx_pan_ramp). */
    const uint32_t pan_to = ((uint32_t)c->pan_left_q15 << 16 | c->pan_right_q15) ^ 0x80008000u;
    const uint32_t pan_from = br_fx_pan_prev[p];
    const uint32_t pan = pan_from != pan_to ? 64u : br_fx_job_pan[p];
#if BR_FX_MOD_RATE2
    if (st->previous_mod_engine != c->mod_engine) {
        st->mod_feedback_last = 0;
        st->mod_rate2_phase = 2u;
        st->mod_rate2_previous_output = 0;
        st->mod_rate2_input_history[0] = 0;
        st->mod_rate2_input_history[1] = 0;
        st->mod_rate2_input_history[2] = 0;
        st->previous_mod_engine = c->mod_engine;
    }
#endif
#if BR_FX_BLOCK_STAGE_PIPELINE
#if BR_FX_BLOCK_STAGE_ONLY_BUILD
#if BR_FX_STAGED_COMPACT_CALL_ABI
    (void)br_fx_process_track_job_staged(p, input_bus, n, enabled, pan,
                                         progress_core
                                         );
#else
    (void)br_fx_process_track_job_staged(p, input_bus, n, enabled, pan,
                                         progress_core, st, s, cfg, c
                                         );
#endif
    goto track_job_complete;
#else
#if BR_FX_STAGED_COMPACT_CALL_ABI
    if (br_fx_process_track_job_staged(p, input_bus, n, enabled, pan,
                                       progress_core
                                       ))
#else
    if (br_fx_process_track_job_staged(p, input_bus, n, enabled, pan,
                                       progress_core, st, s, cfg, c
                                       ))
#endif
        goto track_job_complete;
#endif
#endif
#if !BR_FX_BLOCK_STAGE_ONLY_BUILD
    for (i = 0; i < n; ++i) {
        int32_t modded, mod_direct, delayed, rv_l, rv_r;
        int32_t track_left, track_right;
        uint32_t wet_gain;
        int32_t input;
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_BUS_READ);
        input = input_bus[i];
        if (!enabled && !st->tail_samples) {
            int32_t direct = br_fx_div_pow2(input, 2u);
            if (pan == 64u) track_left = track_right = direct;
            else {
#if BR_FX_PAN_COEFF_CACHE
                br_fx_pan_stereo_cached(direct, direct, pan, c->pan_left_q15,
                                        c->pan_right_q15, &track_left, &track_right);
#else
                br_fx_pan_stereo(direct, direct, pan, &track_left, &track_right);
#endif
            }
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_OUTPUT);
            br_fx_track_stereo[p][i][0] = track_left;
            br_fx_track_stereo[p][i][1] = track_right;
            continue;
        }
        if (!st->tail_samples && st->tail_storage && input &&
            (s->delay || s->reverb || cfg->mod_mix)) {
            br_fx_clear32((br_fx_clear_word_t *)BR_POLY_DELAY_LINE(p), sizeof br_delay_line[p] / 4u);
            br_fx_clear32((br_fx_clear_word_t *)br_room_line[p], sizeof br_room_line[p] / 4u);
            br_fx_clear32((br_fx_clear_word_t *)br_mod_line[p], sizeof br_mod_line[p] / 4u);
            br_fx_reverb_output_ap_reset(p, st);
            br_fx_clear32((br_fx_clear_word_t *)st->room_lp, sizeof st->room_lp / 4u);
            st->mod_feedback_last = 0;
            br_fx_clear32((br_fx_clear_word_t *)st->phase_state, sizeof st->phase_state / 4u);
#if BR_FX_MOD_RATE2
            br_fx_clear32((br_fx_clear_word_t *)st->mod_rate2_input_history,
                sizeof st->mod_rate2_input_history / sizeof(uint32_t));
            st->mod_rate2_previous_output = 0;
            st->mod_rate2_phase = 2u;
#endif
#if BR_FX_DELAY_RATE2
            BR_FX_PREPARE_INVOKE(br_fx_delay_storage_reset, st, st->delay_storage_shift);
            st->delay_output_last = 0;
            st->delay_output_xfade_left = 0u;
            st->glitch_half_error = 0u;
#else
            st->delay_lp = 0;
            st->delay_hp = 0;
#endif
            st->tail_storage = 0;
        }
        /* DELAY MIX reaching 0 keeps the line: its tail decays through the unity return. */
        if (st->previous_reverb_wet && !s->reverb) {
            br_fx_clear32((br_fx_clear_word_t *)br_room_line[p], sizeof br_room_line[p] / 4u);
            br_fx_clear32((br_fx_clear_word_t *)st->room_lp, sizeof st->room_lp / 4u);
            br_fx_reverb_output_ap_reset(p, st);
#if BR_FX_HAS_REVERB_RATE
            st->reverb_rate2_phase = 0u;
            st->reverb_rate2_ramp = 0u;
            st->reverb_rate2_previous_l = 0;
            st->reverb_rate2_previous_r = 0;
            st->reverb_rate2_input_hist[0] = 0;
            st->reverb_rate2_input_hist[1] = 0;
            st->reverb_rate2_input_hist[2] = 0;
            st->reverb_rate2_input_hist[3] = 0;
            st->reverb_rate2_input_hist[4] = 0;
            st->reverb_rate2_input_hist[5] = 0;
#if BR_FX_HAS_REVERB_RATE4
            st->reverb_rate4_phase = 0u;
            st->reverb_rate4_from_l = 0;
            st->reverb_rate4_from_r = 0;
            st->reverb_rate4_input_hist[0] = 0;
            st->reverb_rate4_input_hist[1] = 0;
            st->reverb_rate4_input_hist[2] = 0;
            st->reverb_rate4_input_hist[3] = 0;
            st->reverb_rate4_input_hist[4] = 0;
            st->reverb_rate4_input_hist[5] = 0;
#if BR_FX_HALL_FIR_MIRROR_HISTORY
            st->reverb_hall_q4_full_history_pos = 0u;
            st->reverb_hall_q4_half_history_pos = 0u;
            st->reverb_hall_q4_history_valid = 0u;
#endif
#endif
#endif
            if (!enabled) st->tail_samples = 0;
        }
        if (st->previous_mod_mix && !cfg->mod_mix) {
            br_fx_clear32((br_fx_clear_word_t *)br_mod_line[p], sizeof br_mod_line[p] / 4u);
            st->mod_feedback_last = 0;
            br_fx_clear32((br_fx_clear_word_t *)st->phase_state, sizeof st->phase_state / 4u);
#if BR_FX_MOD_RATE2
            br_fx_clear32((br_fx_clear_word_t *)st->mod_rate2_input_history,
                sizeof st->mod_rate2_input_history / sizeof(uint32_t));
            st->mod_rate2_previous_output = 0;
            st->mod_rate2_phase = 2u;
#endif
            if (!enabled) st->tail_samples = 0;
        }
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_MOD);
        modded = br_fx_mod(st, p, input, c);
        if (cfg->mod_mix && (input > 2 || input < -2)) {
            uint32_t short_tail = FS / 2u;
            if (st->tail_samples < short_tail) st->tail_samples = short_tail;
        }
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_DELAY);
        delayed = br_fx_delay(st, p, modded, c BR_FX_TAIL_EVENT_ARG(0));
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_REVERB);
#if BR_FX_HAS_REVERB_RATE4
        if (c->reverb_rate4) {
            br_fx_reverb_rate4(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(0));
        }
#if BR_FX_HAS_REVERB_RATE2
        else if (c->reverb_rate2) {
            br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(0));
        }
#endif
        else {
            (void)br_fx_reverb(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(0));
        }
#elif BR_FX_HAS_REVERB_RATE2
        if (c->reverb_rate2) {
            br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(0));
        } else {
            (void)br_fx_reverb(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(0));
        }
#else
        (void)br_fx_reverb(st, p, delayed, c, &rv_l, &rv_r
                           BR_FX_TAIL_EVENT_ARG(0));
#endif
        if (!s->reverb) rv_l = rv_r = 0;
#if BR_FX_HAS_REVERB_RATE
        {
            uint32_t ramp_frames = br_fx_rate_samples(32u);
#if BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K
            if (st->reverb_rate2_ramp < ramp_frames) {
                rv_l = (rv_l * (int32_t)st->reverb_rate2_ramp) / (int32_t)ramp_frames;
                rv_r = (rv_r * (int32_t)st->reverb_rate2_ramp) / (int32_t)ramp_frames;
                ++st->reverb_rate2_ramp;
            }
#else
            if (st->reverb_rate2_ramp < ramp_frames) {
                rv_l = br_fx_div_pow2(rv_l * (int32_t)st->reverb_rate2_ramp, 5u);
                rv_r = br_fx_div_pow2(rv_r * (int32_t)st->reverb_rate2_ramp, 5u);
                ++st->reverb_rate2_ramp;
            }
#endif
        }
#endif
        if (st->tail_samples) st->tail_storage = 1u;
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_WET_MIX);
        wet_gain = br_fx_tail_gain(st->tail_samples);
        mod_direct = input + br_fx_wet_gain(modded - input, wet_gain);
        rv_l = br_fx_wet_gain(rv_l, wet_gain);
        rv_r = br_fx_wet_gain(rv_r, wet_gain);
        delayed = mod_direct + br_fx_wet_gain(delayed - modded, wet_gain);
        if (s->reverb) br_fx_reverb_output_diffuse(st, c, &rv_l, &rv_r);
        if (st->tail_samples) --st->tail_samples;
        st->previous_delay_wet = s->delay;
        st->previous_reverb_wet = s->reverb;
        st->previous_mod_mix = cfg->mod_mix;
        {
            int32_t centered = br_fx_div_pow2(mod_direct, 2u) +
                br_fx_div_pow2(delayed - mod_direct, 2u);
            track_left = centered + rv_l;
            track_right = centered + rv_r;
        }
        if (pan != 64u) {
#if BR_FX_PAN_COEFF_CACHE
            br_fx_pan_stereo_cached(track_left, track_right, pan,
                c->pan_left_q15, c->pan_right_q15, &track_left, &track_right);
#else
            br_fx_pan_stereo(track_left, track_right, pan, &track_left, &track_right);
#endif
        }
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, i, BR_FX_PROGRESS_OUTPUT);
        br_fx_track_stereo[p][i][0] = track_left;
        br_fx_track_stereo[p][i][1] = track_right;
    }
#endif
#if BR_FX_BLOCK_STAGE_PIPELINE
track_job_complete:
    if (pan_from != pan_to) {
        BR_FX_PREPARE_INVOKE(br_fx_pan_ramp, br_fx_track_stereo[p], n, pan_from, pan_to);
        br_fx_pan_prev[p] = pan_to;
    }
#endif
#if BR_FX_GLOBAL_REVERB
    if (p == BR_GR_HOST && n && br_gr_separate_seq != br_fx_block_seq) br_gr_job(n);
#endif
    br_fx_progress_store_force(br_fx_progress_retained.words, progress_core, p,
                               n ? n - 1u : 0u, BR_FX_PROGRESS_DONE);
#if defined(FM1_DEVICE)
    br_fx_job_cost[p] = fm1_ticks() - job_start;
#endif
}

/* Keep CPU-facing entrypoints separate while sharing one immutable DSP body
 * in internal RAM. The part-indexed state and output row still have one owner. */
BR_FX_RAM_AUDIO BR_AUDIO_API __attribute__((noinline, used))
void br_fx_process_track_job(uint32_t p)
{
    if (p < BR_PARTS) br_fx_process_track_job_impl(p, br_fx_job_bus[p], br_fx_job_n
                                                   );
}

static BR_FX_RAM_AUDIO __attribute__((noinline, used))
void br_fx_process_track_job_cpu1(uint32_t p)
{
    if (p < BR_PARTS) br_fx_process_track_job_impl(p, br_fx_job_bus[p], br_fx_job_n
                                                   );
}

static BR_FX_RAM_AUDIO __attribute__((noinline, used))
void br_fx_process_track_input_cpu1(uint32_t p, const int32_t *input, uint32_t n
                                    )
{
    br_fx_process_track_job_impl(p, input, n
                                 );
}

BR_FX_RAM_AUDIO void br_fx_fusion_process_track(uint32_t p,
                                                  const int32_t *input,
                                                  uint32_t n
                                                  )
{
    uint32_t core = fm1_fault_diag_read_cpu_id();
    if (!br_fx_fusion_pending || p >= BR_PARTS || core > 1u ||
        (n && !input) || n != br_fx_job_n) return;
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
    if (br_fx_poly_mode && p != br_fx_poly_part) {
        memset(br_fx_track_stereo[p], 0, n * sizeof br_fx_track_stereo[p][0]);
        br_fx_track_silent_seq[p] = br_fx_block_seq;
#if BR_FX_GLOBAL_REVERB
        br_gr_have[br_gr_cur][p] = 0u;
        if (p == BR_GR_HOST && n && br_gr_separate_seq != br_fx_block_seq) br_gr_job(n);
#endif
        br_fx_fusion_done[p] = 1u;
        return;
    }
#endif
    if (core == 1u) br_fx_process_track_input_cpu1(p, input, n
                                                   );
    else br_fx_process_track_job_impl(p, input, n
                                      );
    br_fx_fusion_done[p] = 1u;
}

BR_FX_RAM_AUDIO uint32_t br_fx_fusion_cost(uint32_t p)
{
    return p < BR_PARTS ? br_fx_job_cost[p] : 0u;
}

#if BR_FX_GLOBAL_REVERB
/* The shared reverb reads only the previous block's sends, so in a split graph it
 * can run as its own job on either core. The builder stamps the block; a stale
 * stamp never matches, so track 3 runs it unless this block's graph has the job. */
uint32_t br_fx_shared_reverb_runs(void) { return br_gr_run && br_gr_key_valid; }
void br_fx_shared_reverb_separate(void) { br_gr_separate_seq = br_fx_block_seq; }
uint32_t br_fx_shared_reverb_cost(void) {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    return br_gr_stats[2];
#else
    return br_gr_cost_ticks;
#endif
}
BR_FX_RAM_AUDIO void br_fx_fusion_shared_reverb(uint32_t n)
{
    if (br_fx_fusion_pending && n && n == br_fx_job_n &&
        br_gr_separate_seq == br_fx_block_seq) br_gr_job(n);
}
#else
uint32_t br_fx_shared_reverb_runs(void) { return 0u; }
void br_fx_shared_reverb_separate(void) { }
uint32_t br_fx_shared_reverb_cost(void) { return 0u; }
void br_fx_fusion_shared_reverb(uint32_t n) { (void)n; }
#endif

/* Once per block; calls only the (XIP) prepare, so it shares its placement. */
BR_FX_PREPARE_CODE uint32_t br_fx_fusion_prepare(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t p;
    if (!bus || n > BR_BLOCK || br_fx_fusion_pending) return 0u;
#if !BR_AUDIO_FUSION_ONLY_BUILD
    br_fx_pipeline_controls = 0;
    br_fx_pipeline_keys = 0;
    br_fx_pipeline_cpu0_only = 0u;
#endif
    br_fx_job_bus = bus;
    br_fx_job_n = n;
    BR_FX_PREPARE_TARGET(br_fx_prepare_block)(n);
    for (p = 0; p < BR_PARTS; ++p) br_fx_fusion_done[p] = 0u;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    FM1_CPU1_WORKER_BARRIER();
#elif defined(BR_SOURCE_QUEUE_TEST)
    FM1_CPU1_WORKER_BARRIER();
#else
    __asm__ volatile("" ::: "memory");
#endif
    br_fx_fusion_pending = 1u;
    return 1u;
}

BR_FX_RAM_AUDIO void br_fx_fusion_cancel(void)
{
    br_fx_fusion_pending = 0u;
#if !BR_AUDIO_FUSION_ONLY_BUILD
    br_fx_pipeline_keys = 0;
    br_fx_pipeline_controls = 0;
    br_fx_pipeline_cpu0_only = 0u;
#endif
}

/* Only call after both source actors have stopped writing fusion state. */
uint32_t br_fx_fusion_complete(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t p;
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
    FM1_CPU1_WORKER_BARRIER();
#else
    __asm__ volatile("" ::: "memory");
#endif
    if (br_fx_fusion_pending != 1u || bus != br_fx_job_bus || n != br_fx_job_n)
        return 0u;
    for (p = 0u; p < BR_PARTS; ++p)
        if (br_fx_fusion_done[p] != 1u) return 0u;
    return 1u;
}

/* Only call after both source actors have joined. Pending tracks may be
 * skipped on a partial deadline drop, but the mask and every flag must be
 * canonical before any partial state can be accepted. */
uint32_t br_fx_fusion_matches_mask(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t expected_mask)
{
    uint32_t p;
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
    FM1_CPU1_WORKER_BARRIER();
#else
    __asm__ volatile("" ::: "memory");
#endif
    if (br_fx_fusion_pending != 1u || bus != br_fx_job_bus ||
        n != br_fx_job_n || (expected_mask >> BR_PARTS)) return 0u;
    for (p = 0u; p < BR_PARTS; ++p)
        if (br_fx_fusion_done[p] != ((expected_mask >> p) & 1u)) return 0u;
    return 1u;
}

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
BR_FX_RAM_AUDIO uint32_t br_fx_progress_get(uint32_t core)
{
    return core < 2u ? br_fx_progress_retained.words[core] : 0u;
}

void br_fx_progress_bootstrap(void)
{
    uint32_t valid;
    if (br_fx_progress_bootstrapped) return;
    valid = br_fx_progress_retained.magic == BR_FX_PROGRESS_RETAINED_MAGIC &&
        br_fx_progress_retained.inverse_magic == ~BR_FX_PROGRESS_RETAINED_MAGIC &&
        br_fx_progress_retained.version == BR_FX_PROGRESS_RETAINED_VERSION;
    br_fx_progress_previous_valid = valid;
    br_fx_progress_previous[0] = valid ? br_fx_progress_retained.words[0] : 0u;
    br_fx_progress_previous[1] = valid ? br_fx_progress_retained.words[1] : 0u;

    /* Invalidate before clearing; a reset during initialization cannot make a
     * partial record appear valid on the next boot. */
    br_fx_progress_retained.magic = 0u;
    br_fx_progress_retained.inverse_magic = 0u;
    br_fx_progress_retained.version = 0u;
    fm1_memory_publish_sync();
    br_fx_progress_retained.words[0] = 0u;
    br_fx_progress_retained.words[1] = 0u;
    br_fx_progress_retained.version = BR_FX_PROGRESS_RETAINED_VERSION;
    br_fx_progress_retained.inverse_magic = ~BR_FX_PROGRESS_RETAINED_MAGIC;
    fm1_memory_publish_sync();
    br_fx_progress_retained.magic = BR_FX_PROGRESS_RETAINED_MAGIC;
    fm1_memory_publish_sync();
    br_fx_progress_bootstrapped = 1u;
}

void br_fx_progress_read(uint32_t out[5])
{
    if (!out) return;
    out[0] = br_fx_progress_previous_valid;
    out[1] = br_fx_progress_previous[0];
    out[2] = br_fx_progress_previous[1];
    out[3] = br_fx_progress_retained.words[0];
    out[4] = br_fx_progress_retained.words[1];
}
#endif

#if BR_FX_FINISH_MASTER_STREAM && BR_FX_STREAM_OUTPUT_SCALE
/* RAM-resident copy of fm1_scale_q12_clip_block (audio_level.h) for CPU1's
 * stream job, which cannot call the XIP copy. Same arithmetic and fast
 * paths, with the gain snapshotted once per block by the caller. */
static inline __attribute__((always_inline)) void br_fx_scale_q12_clip_range(
    int32_t *samples, uint32_t count, uint32_t gain)
{
    uint32_t i;
#if FM1_AUDIO_GAIN_Q12_FASTPATH
    if (gain == 0u) {
        for (i = 0u; i < count; ++i) samples[i] = 0;
        return;
    }
    if (gain == 4096u) {
        for (i = 0u; i < count; ++i) {
            const int32_t sample = samples[i];
            samples[i] = sample > 32767 ? 32767 : sample < -32767 ? -32767 : sample;
        }
        return;
    }
#endif
    for (i = 0u; i < count; ++i) {
        int32_t value = fm1_scale_q12(samples[i], gain);
        samples[i] = value > 32767 ? 32767 : value < -32767 ? -32767 : value;
    }
}
#if !FM1_AUDIO_GAIN_BLOCK_SNAPSHOT
#error "CPU1 output scaling assumes the per-block gain snapshot"
#endif
#endif

/* Punch-in echo borrows a track's delay buffer (mono, half rate). */
BR_AUDIO_API int16_t *br_fx_echo_line(uint32_t part, uint32_t *samples)
{
    if (part >= BR_PARTS) return 0;
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
    if (br_fx_poly_loaned) { *samples = 0u; return 0; }
#endif
    *samples = BR_FX_DELAY_SAMPLES;
    return BR_POLY_DELAY_LINE(part);
}

BR_FX_RAM_AUDIO BR_AUDIO_API uint32_t br_fx_worker_track_batch(void *opaque_batch)
{
    br_fx_job_batch_t *batch;
    uint32_t i;
#if BR_FX_FINISH_MASTER_STREAM
    if (opaque_batch == &br_fx_finish_stream_job) {
        uint32_t master_active, base = 0u;
#if BR_FX_GLOBAL_REVERB
        uint32_t gr_active;
#endif
#if defined(FM1_DEVICE) || \
    (defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK))
        uint32_t start;
#endif
        if (!br_fx_finish_stream_job.out ||
            br_fx_finish_stream_job.n > BR_BLOCK) {
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
            br_fx_stream_test_done();
#endif
            return 0u;
        }
        if (br_fx_finish_stream_job.pre) br_fx_finish_stream_job.pre();
        master_active = BR_FX_MASTER_PREPARE_TARGET(
            br_master_fx_begin_block)(br_fx_finish_stream_job.n);
#if BR_FX_GLOBAL_REVERB
        gr_active = br_gr_run;
#endif
        /* No early return when nothing is active: the master limiter below
         * needs the whole block. */
        while (base < br_fx_finish_stream_job.n) {
            uint32_t end = base + 16u;
            if (end > br_fx_finish_stream_job.n) end = br_fx_finish_stream_job.n;
#if defined(FM1_DEVICE)
            start = fm1_ticks();
#elif defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
            start = br_fx_stream_test_ticks();
            br_fx_stream_test_waiting(end);
#endif
            while (BR_FX_STREAM_LOAD_PRODUCED(&br_fx_finish_stream_job) < end) {
                /* C0 is guaranteed to publish every completed slice. Bound
                 * each producer stall; earlier slice rendering must not eat
                 * this slice's wait budget. */
#if defined(FM1_DEVICE)
                if ((uint32_t)(fm1_ticks() - start) >= 17415u) return 0u;
#elif defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
                if ((uint32_t)(br_fx_stream_test_ticks() - start) >= 17415u) {
                    br_fx_stream_test_done();
                    return 0u;
                }
#endif
            }
            FM1_CPU1_WORKER_BARRIER();
#if BR_FX_GLOBAL_REVERB
            if (gr_active) br_gr_add_range(br_fx_finish_stream_job.out, base, end);
#endif
            if (master_active) {
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
                uint32_t master_started = fm1_ticks();
#endif
                br_master_fx_process_range(br_fx_finish_stream_job.out, base, end);
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
                br_fx_master_work_ticks += fm1_ticks() - master_started;
#endif
            }
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
            br_fx_stream_test_after_range(end);
#endif
            base = end;
        }
        /* Master limiter over the whole block, then master volume (which
         * therefore no longer runs per slice). */
        br_master_limit_block(br_fx_finish_stream_job.out, br_fx_finish_stream_job.n);
#if BR_FX_STREAM_OUTPUT_SCALE
        if (br_fx_finish_stream_job.scale)
            br_fx_scale_q12_clip_range(br_fx_finish_stream_job.out,
                                       2u * br_fx_finish_stream_job.n,
                                       (uint32_t)br_fx_finish_stream_job.gain_q12);
#endif
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
        br_fx_stream_test_done();
#endif
        return 1u;
    }
#endif
#if !BR_AUDIO_FUSION_ONLY_BUILD
    if (opaque_batch == &br_fx_reverse_job) {
        if (!br_fx_reverse_job.bus || !br_fx_reverse_job.keys ||
            !br_fx_reverse_job.controls || !br_fx_reverse_job.out ||
            br_fx_reverse_job.n > BR_BLOCK) return 0u;
        br_fx_process_pipeline(br_fx_reverse_job.bus, br_fx_reverse_job.keys,
            br_fx_reverse_job.controls, br_fx_reverse_job.out, br_fx_reverse_job.n);
        return 1u;
    }
#endif
    if (opaque_batch != &br_fx_worker_batches[1]) return 0u;
    batch = (br_fx_job_batch_t *)opaque_batch;
    if (!batch->count || batch->count > BR_PARTS) return 0u;
    for (i = 0; i < batch->count; ++i) {
        uint32_t p = batch->part[i];
        if (p >= BR_PARTS) return 0u;
        br_fx_process_track_job_cpu1(p);
    }
    return 1u;
}

#if defined(BR_SPLIT_AUDIO) && !BR_AUDIO_FUSION_ONLY_BUILD
/* Fixed registered ID4 task: validate the one static composite context, run
 * prior-frame FX first, then let CPU1 help drain the already-published source
 * queue. Both callbacks are fixed symbols; mailbox data contains no function
 * pointer. */
uint32_t br_fx_worker_hybrid_batch(void *opaque_context)
{
    br_fx_hybrid_job_t *job;
    uint32_t (* volatile source_valid_fn)(void *) =
        br_audio_worker_source_queue_context_valid;
    uint32_t (* volatile source_fn)(void *) = br_audio_worker_source_batch;
    if (opaque_context != &br_fx_hybrid_job) return 0u;
    job = &br_fx_hybrid_job;
    if (job->fx_valid > 1u) return 0u;
    if (!job->source_context || !source_valid_fn(job->source_context)) return 0u;
    if (job->fx_valid) {
        uint32_t (* volatile fx_fn)(void *) = br_fx_worker_track_batch;
        if (job->fx_context != &br_fx_reverse_job || fx_fn(job->fx_context) != 1u)
            return 0u;
    } else if (job->fx_context) {
        return 0u;
    }
#if defined(BR_SOURCE_QUEUE_TEST)
    extern void br_audio_hybrid_test_fx_stage_done(void);
    br_audio_hybrid_test_fx_stage_done();
#endif
    return source_fn(job->source_context);
}
#endif /* BR_SPLIT_AUDIO && !BR_AUDIO_FUSION_ONLY_BUILD */
#if BR_AUDIO_FUSION_ONLY_BUILD && defined(BR_SPLIT_AUDIO) && \
    (defined(BR_SOURCE_QUEUE_TEST) || (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)))
/* The fixed HAL registry retains task ID4 for link-time stability. This build
 * removes its hybrid implementation and never submits that task, so provide a
 * fail-closed callback for both the host harness and the device dispatcher. */
uint32_t br_fx_worker_hybrid_batch(void *opaque_context)
{
    (void)opaque_context;
    return 0u;
}
#endif

#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
static BR_FX_RAM_AUDIO uint32_t br_fx_run_cpu0_batch(const br_fx_job_batch_t *batch)
{
    uint32_t i;
    for (i = 0; i < batch->count; ++i) {
        uint32_t p = batch->part[i];
        if (p >= BR_PARTS) return 0u;
        br_fx_process_track_job(p);
    }
    return 1u;
}

static BR_FX_RAM_AUDIO void br_fx_build_batches(void)
{
    uint32_t order[BR_PARTS], loads[2] = {0u, 0u}, i, j;
    br_fx_worker_batches[0].count = 0u;
    br_fx_worker_batches[1].count = 0u;
    for (i = 0; i < BR_PARTS; ++i) {
        uint32_t at = i;
        order[i] = i;
        while (at && br_fx_job_cost[order[at - 1u]] < br_fx_job_cost[i]) {
            order[at] = order[at - 1u];
            --at;
        }
        order[at] = i;
    }
    for (i = 0; i < BR_PARTS; ++i) {
        uint32_t p = order[i], core = loads[0] <= loads[1] ? 0u : 1u;
        br_fx_job_batch_t *b = &br_fx_worker_batches[core];
        uint32_t cost = br_fx_job_cost[p] < 512u ? 512u : br_fx_job_cost[p];
        b->part[b->count++] = p;
        loads[core] += cost;
    }
    for (i = 0; i < 2u; ++i) {
        br_fx_job_batch_t *b = &br_fx_worker_batches[i];
        for (j = 1u; j < b->count; ++j) {
            uint32_t p = b->part[j], k = j;
            while (k && b->part[k - 1u] > p) { b->part[k] = b->part[k - 1u]; --k; }
            b->part[k] = p;
        }
    }
}
#endif

static BR_FX_RAM_AUDIO void br_fx_mute_block(int32_t *out, uint32_t n);

#if BR_FX_SILENT_FINISH
#if BR_COMP_GAIN_UPDATE_RATE8
#define BR_FX_COMP_IDLE_STEPS 8u
#elif BR_COMP_GAIN_UPDATE_RATE4
#define BR_FX_COMP_IDLE_STEPS 4u
#elif BR_COMP_GAIN_UPDATE_RATE2
#define BR_FX_COMP_IDLE_STEPS 2u
#else
#define BR_FX_COMP_IDLE_STEPS 1u
#endif
/* True when the compressor sits at its zero-input fixed point: envelope and
 * hold at zero, unity gain and target, no reduction and no pending reseed.
 * With a zero detector each gain-update period then leaves every state
 * variable unchanged (the target phase returns to where it started), so any
 * whole number of periods is a no-op. tests/test_comp_local.c checks this. */
static BR_FX_RAM_AUDIO uint32_t br_fx_comp_at_rest(uint32_t n)
{
    if (n % BR_FX_COMP_IDLE_STEPS) return 0u;
    return br_comp_envelope == 0 && br_comp_hold_left == 0u &&
        br_comp_gain_q15 == 32768u && br_comp_reduction_q15 == 0u
#if BR_COMP_GAIN_UPDATE_RATE4 || BR_COMP_GAIN_UPDATE_RATE2 || BR_COMP_GAIN_UPDATE_RATE8
        && !br_comp_gain_target_reseed && br_comp_gain_target_valid &&
        br_comp_gain_target_from_q15 == 32768u && br_comp_gain_target_to_q15 == 32768u
#endif
        ;
}

static BR_FX_RAM_AUDIO uint32_t br_fx_finish_inputs_silent(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t comp_active,
    uint32_t dry_center)
{
    uint32_t p, i;
    for (p = 0u; p < BR_PARTS; ++p) {
        if (dry_center) {
            for (i = 0u; i < n; ++i) if (bus[p][i]) return 0u;
        } else if (br_fx_track_silent_seq[p] != br_fx_block_seq) {
            for (i = 0u; i < n; ++i)
                if (br_fx_track_stereo[p][i][0] | br_fx_track_stereo[p][i][1]) return 0u;
        }
    }
    if (comp_active && br_fx_comp_key_part < BR_PARTS) {
        const int32_t *key;
        p = br_fx_comp_key_part;
        key = br_fx_job_kit[p] ? br_comp_key[p] : bus[p];
        for (i = 0u; i < n; ++i) if (key[i]) return 0u;
    }
    return !comp_active || br_fx_comp_at_rest(n);
}
#endif

/* CPU0 publishes post-track-FX peaks to the foreground; the shared reverb and
 * master processing are later and have no independent per-track outputs. */
static volatile uint32_t br_fx_meter_enabled, br_fx_meter_value[BR_PARTS];
BR_AUDIO_API void br_fx_meter_enable(uint32_t enabled) { br_fx_meter_enabled = !!enabled; RING_PUBLISH(); }
BR_AUDIO_API uint32_t br_fx_meter_read(uint32_t part) {
    uint32_t v = part < BR_PARTS ? br_fx_meter_value[part] : 0u;
    RING_PUBLISH(); return v;
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void br_fx_meter_capture(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t dry)
{
    for (uint32_t p = 0; p < BR_PARTS; ++p) {
        uint32_t peak = 0;
        for (uint32_t i = 0; i < n; ++i) {
            int32_t l = dry ? bus[p][i] : br_fx_track_stereo[p][i][0];
            if (dry) l = l < 0 ? -(int32_t)((0u - (uint32_t)l) >> 2) : l >> 2;
            int32_t r = dry ? l : br_fx_track_stereo[p][i][1];
            uint32_t al = l < 0 ? 0u - (uint32_t)l : (uint32_t)l;
            uint32_t ar = r < 0 ? 0u - (uint32_t)r : (uint32_t)r;
            if (al > peak) peak = al;
            if (ar > peak) peak = ar;
        }
        if (peak > 32768u) peak = 32768u;
        uint32_t previous = br_fx_meter_value[p];
        uint32_t fall = previous > 8u ? previous - (previous >> 3) : 0u;
        br_fx_meter_value[p] = peak > fall ? peak : fall;
    }
    RING_PUBLISH();
}

#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO) && defined(BR_SPLIT_AUDIO)
/* CPU0's wait for the streamed master FX, from flash (called through the RAM
 * finish block's volatile pointer): at least min_ticks, otherwise until the
 * half's source deadline (audio_deadline.h br_audio_stream_wait_ticks). A failure
 * is recorded for 0xB2: site, and the budget waited (bit 31: CPU1 itself gave up,
 * a 17,415-tick stall waiting for one of CPU0's mix slices). */
static __attribute__((noinline)) uint32_t br_fx_stream_wait(uint32_t sequence, uint32_t min_ticks,
                                                             uint32_t *result)
{
    uint32_t budget = br_audio_stream_wait_ticks(br_pipeline_source_deadline_valid,
                                                 br_pipeline_source_deadline_tick, fm1_ticks());
    uint32_t status;
    if (budget < min_ticks) budget = min_ticks;
    status = fm1_cpu1_worker_wait(sequence, budget, result);
    if (status != FM1_CPU1_WORKER_OK || *result != 1u) {
        BR_AUDIO_FAULT_SITE(BR_AUDIO_FAULT_STREAM);
        br_audio_fault_site[2] = budget | (status == FM1_CPU1_WORKER_OK ? 0x80000000u : 0u);
    }
    return status;
}
#endif

static BR_FX_RAM_AUDIO void br_fx_finish_block(
    int32_t bus[BR_PARTS][BR_BLOCK], int32_t *out, uint32_t n,
    uint32_t comp_active
#if BR_FX_MASTER_FRAME_INLINE_FINISH
    , uint32_t dry_center
#endif
    )
{
    uint32_t i, p;
#if BR_AUDIO_FUSION_ONLY_BUILD
    if (br_fx_meter_enabled) {
        void (*volatile capture_fn)(int32_t [BR_PARTS][BR_BLOCK], uint32_t, uint32_t) = br_fx_meter_capture;
#if BR_FX_MASTER_FRAME_INLINE_FINISH
        capture_fn(bus, n, dry_center);
#else
        capture_fn(bus, n, br_fx_dry_center && !comp_active);
#endif
    }
#endif
    uint32_t master_active = 0u;
#if BR_FX_FINISH_MASTER_STREAM
    uint32_t stream_active = 0u;
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
    uint32_t stream_sequence = 0u, stream_result = 0u, stream_status;
    uint32_t (* volatile stream_available_fn)(void) = fm1_cpu1_worker_available;
    uint32_t (* volatile stream_submit_fn)(void *, uint32_t *) =
        fm1_cpu1_worker_submit_fx_batch;
    uint32_t (* volatile stream_wait_fn)(uint32_t, uint32_t, uint32_t *) =
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO) && defined(BR_SPLIT_AUDIO)
        br_fx_stream_wait;   /* XIP: budget to the deadline, fault record */
#else
        fm1_cpu1_worker_wait;
#endif
    uint32_t (* volatile stream_abort_fn)(void) = fm1_cpu1_worker_abort_hold_reset;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    uint32_t on_c0 = fm1_fault_diag_read_cpu_id() == 0u;
#else
    uint32_t on_c0 = 1u;
    extern volatile uint32_t br_audio_source_worker_fault;
#endif
    if (n && on_c0 && !br_audio_source_worker_fault && stream_available_fn()) {
        br_fx_finish_stream_job.out = out;
        br_fx_finish_stream_job.n = n;
#if BR_AUDIO_DEFER_POST_JOIN
        br_fx_finish_stream_job.pre = br_post_join.pending ? br_engine_post_join_run : 0;
#else
        br_fx_finish_stream_job.pre = 0;
#endif
#if BR_FX_STREAM_OUTPUT_SCALE && defined(FM1_DEVICE)
        br_fx_finish_stream_job.scale = br_fx_output_scale_request;
        br_fx_finish_stream_job.gain_q12 = br_fx_output_gain_q12;
#else
        br_fx_finish_stream_job.scale = 0u;
#endif
        BR_FX_STREAM_PUBLISH_PRODUCED(&br_fx_finish_stream_job, 0u);
        FM1_CPU1_WORKER_BARRIER();
        stream_status = stream_submit_fn(&br_fx_finish_stream_job, &stream_sequence);
        if (stream_status == FM1_CPU1_WORKER_OK) stream_active = 1u;
    }
#endif
#endif
#if BR_FX_FINISH_MASTER_FUSE
    /* Fusion snapshots controls before track summing. Stable block controls
     * retain exact output/state; a concurrent update may apply one loop sooner. */
#if BR_FX_FINISH_MASTER_STREAM
    if (!stream_active)
#endif
        master_active = BR_FX_MASTER_PREPARE_TARGET(br_master_fx_begin_block)(n);
#endif
#if BR_FX_SILENT_FINISH
    /* Every input is zero and the compressor is at rest: each frame would add
     * zero to the output, so skip the per-frame mix (exactly equivalent). */
    i = br_fx_finish_inputs_silent(bus, n, comp_active
#if BR_FX_MASTER_FRAME_INLINE_FINISH
                                   , dry_center
#else
                                   , 0u
#endif
                                   ) ? n : 0u;
#if BR_FX_FINISH_MASTER_STREAM
    if (i == n && stream_active) {
        FM1_CPU1_WORKER_BARRIER();
        BR_FX_STREAM_PUBLISH_PRODUCED(&br_fx_finish_stream_job, n);
        FM1_CPU1_WORKER_BARRIER();
    }
#endif
#if BR_COMP_BLOCK_LOCAL_STATE && BR_COMP_INPLACE_TRACKS && BR_COMP_FINISH_SUM_FOLD
#define BR_FX_COMP_LOCAL_ACTIVE 1
    br_comp_local_t comp_local;
    if (comp_active) br_comp_local_load(&comp_local);
#if BR_AUDIO_FUSION_ONLY_BUILD
    /* Block constants, read once instead of once per frame. */
    const uint32_t comp_source = br_comp_cached.source;
    const uint32_t comp_protected = br_fx_comp_key_part;
    const int32_t *const comp_key_row = comp_protected < BR_PARTS ?
        (br_fx_job_kit[comp_protected] ? br_comp_key[comp_protected] : bus[comp_protected]) : 0;
#define BR_FX_COMP_HOISTED 1
#endif
#endif
    for (; i < n; ++i) {
#else
    for (i = 0; i < n; ++i) {
#endif
#if BR_FX_FINISH_MASTER_STREAM && defined(BR_SOURCE_QUEUE_TEST) && \
    defined(BR_FX_STREAM_TEST_CLOCK)
        if (stream_active && i && ((i & 15u) == 0u)) {
            uint32_t next_end = i + 16u;
            if (next_end > n) next_end = n;
            br_fx_stream_test_before_source_next(next_end);
        }
#endif
        int32_t left = 0, right = 0;
#if BR_FX_MASTER_FRAME_INLINE_FINISH
        if (dry_center) {
            /* Keep the original per-track signed division and its rounding. */
            left = br_fx_div_pow2(bus[0][i], 2u) + br_fx_div_pow2(bus[1][i], 2u) +
                br_fx_div_pow2(bus[2][i], 2u) + br_fx_div_pow2(bus[3][i], 2u);
            right = left;
        } else {
#endif
#if BR_COMP_INPLACE_TRACKS
#if BR_COMP_FINISH_SUM_FOLD
        if (comp_active) {
            int32_t key = 0, detector = 0;
#if defined(BR_FX_COMP_HOISTED)
            const uint32_t source = comp_source;
            const uint32_t protected_part = comp_protected;
#else
            uint32_t source = br_comp_cached.source;
            uint32_t protected_part = br_fx_comp_key_part;
#endif
            uint32_t combined_q;
            if (protected_part < BR_PARTS) {
                p = protected_part;
#if defined(BR_FX_COMP_HOISTED)
                key = comp_key_row[i];
#elif BR_AUDIO_FUSION_ONLY_BUILD
                key = br_fx_job_kit[p] ? br_comp_key[p][i] : bus[p][i];
#else
                key = br_fx_job_kit[p] ?
                    (br_fx_pipeline_keys ? br_fx_pipeline_keys[p][i] : br_comp_key[p][i]) : bus[p][i];
#endif
            }
            if (source == BR_COMP_ALL) {
                for (p = 0; p < BR_PARTS; ++p) {
                    left += br_fx_track_stereo[p][i][0];
                    right += br_fx_track_stereo[p][i][1];
                }
                detector = br_comp_abs(left);
                if (br_comp_abs(right) > detector) detector = br_comp_abs(right);
            } else {
                detector = br_comp_abs(key);
                for (p = 0; p < BR_PARTS; ++p) {
                    if (p != protected_part) {
                        left += br_fx_track_stereo[p][i][0];
                        right += br_fx_track_stereo[p][i][1];
                    }
                }
            }
#if defined(BR_FX_COMP_LOCAL_ACTIVE)
            combined_q = br_comp_local_next(&comp_local, detector, source);
#else
            combined_q = br_comp_next_mix_gain(detector, source);
#endif
            if (combined_q != 32768u) {
                left = br_comp_mul_q15(left, combined_q);
                right = br_comp_mul_q15(right, combined_q);
            }
            if (source != BR_COMP_ALL && protected_part < BR_PARTS) {
                left += br_fx_track_stereo[protected_part][i][0];
                right += br_fx_track_stereo[protected_part][i][1];
            }
        } else {
            for (p = 0; p < BR_PARTS; ++p) {
                left += br_fx_track_stereo[p][i][0];
                right += br_fx_track_stereo[p][i][1];
            }
        }

#else
        if (comp_active) {
            int32_t key = 0;
            if (br_fx_comp_key_part < BR_PARTS) {
                p = br_fx_comp_key_part;
#if BR_AUDIO_FUSION_ONLY_BUILD
                key = br_fx_job_kit[p] ? br_comp_key[p][i] : bus[p][i];
#else
                key = br_fx_job_kit[p] ?
                    (br_fx_pipeline_keys ? br_fx_pipeline_keys[p][i] : br_comp_key[p][i]) : bus[p][i];
#endif
            }
            br_comp_process_tracks(br_fx_track_stereo, i, key, br_fx_comp_key_part);
        }
        for (p = 0; p < BR_PARTS; ++p) {
            left += br_fx_track_stereo[p][i][0];
            right += br_fx_track_stereo[p][i][1];
        }
#endif
#else
#if BR_FX_FINISH_DIRECT_SUM
        if (!comp_active) {
            /* The common compressor-bypassed case can sum directly from the
             * track rows instead of materializing the temporary stereo/key
             * arrays used by the sidechain path. */
            for (p = 0; p < BR_PARTS; ++p) {
                left += br_fx_track_stereo[p][i][0];
                right += br_fx_track_stereo[p][i][1];
            }
        } else
#endif
        {
        int32_t tracks[BR_PARTS][2];
        int32_t keys[BR_PARTS];
        for (p = 0; p < BR_PARTS; ++p) {
            tracks[p][0] = br_fx_track_stereo[p][i][0];
            tracks[p][1] = br_fx_track_stereo[p][i][1];
        }
        if (comp_active) {
            if (br_fx_comp_key_part < BR_PARTS) {
                p = br_fx_comp_key_part;
#if BR_AUDIO_FUSION_ONLY_BUILD
                keys[p] = br_fx_job_kit[p] ? br_comp_key[p][i] : bus[p][i];
#else
                keys[p] = br_fx_job_kit[p] ?
                    (br_fx_pipeline_keys ? br_fx_pipeline_keys[p][i] : br_comp_key[p][i]) : bus[p][i];
#endif
            }
            br_comp_process_tracks(tracks, keys);
        }
        for (p = 0; p < BR_PARTS; ++p) {
            left += tracks[p][0];
            right += tracks[p][1];
        }
        }
#endif
#if BR_FX_MASTER_FRAME_INLINE_FINISH
        }
#endif
        out[2u * i] += left;
        out[2u * i + 1u] += right;
#if BR_FX_FINISH_MASTER_FUSE
#if !BR_FX_FINISH_MASTER_STREAM
        if (master_active) br_master_fx_process_frame(&out[2u * i]);
#endif
#endif
#if BR_FX_FINISH_MASTER_STREAM
        if (stream_active && (((i + 1u) & 15u) == 0u || i + 1u == n)) {
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
            br_fx_stream_test_before_publish(i + 1u);
#endif
            FM1_CPU1_WORKER_BARRIER();
            BR_FX_STREAM_PUBLISH_PRODUCED(&br_fx_finish_stream_job, i + 1u);
            FM1_CPU1_WORKER_BARRIER();
        }
#endif
    }
#if defined(BR_FX_COMP_LOCAL_ACTIVE)
    if (comp_active) br_comp_local_store(&comp_local);
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    br_tail_stamp[BR_TAIL_MIX_END] = fm1_ticks();
#endif
#if BR_FX_GLOBAL_REVERB
#if BR_FX_FINISH_MASTER_STREAM
    if (!stream_active)
#endif
        br_gr_add_range(out, 0u, n);
#endif
#if BR_FX_FINISH_MASTER_STREAM && BR_FX_FINISH_MASTER_FUSE
    if (!stream_active && master_active)
        br_master_fx_process_range(out, 0u, n);
#endif
#if BR_FX_FINISH_MASTER_STREAM && \
    ((defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST))
    if (stream_active) {
        /* At least 18,415 ticks, on the unit until the half's source deadline
         * (br_fx_stream_wait). Only the wait guard changes; successful DSP
         * output and controls are unchanged. */
        uint32_t stream_status = stream_wait_fn(stream_sequence,
            BR_AUDIO_STREAM_WAIT_MIN_TICKS, &stream_result);
        if (stream_status != FM1_CPU1_WORKER_OK || stream_result != 1u) {
            br_audio_source_worker_fault = 1u;
            (void)stream_abort_fn();
            FM1_CPU1_WORKER_BARRIER();
            if (fm1_cpu1_worker_uncontained_fault) return;
            br_fx_mute_block(out, n);
            return;
        }
    }
#endif
#if defined(FM1_DEVICE)
#if BR_FX_FINISH_MASTER_STREAM && BR_FX_STREAM_OUTPUT_SCALE && defined(BR_CPU1_AUDIO)
    if (stream_active && br_fx_finish_stream_job.scale) br_fx_output_prescaled = 1u;
#endif
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_tail_stamp[BR_TAIL_WAIT_END] = fm1_ticks();
#endif
#endif
#if !BR_FX_FINISH_MASTER_FUSE
    br_master_fx_process(out, n);
#endif
#if BR_FX_FINISH_MASTER_STREAM
    if (!stream_active) /* the stream job limits on CPU1 */
#endif
        br_master_limit_block(out, n);
}

static BR_FX_RAM_AUDIO void br_fx_mute_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        out[2u * i] = 0;
        out[2u * i + 1u] = 0;
    }
}

static BR_FX_RAM_AUDIO void br_fx_process_ram(int32_t bus[BR_PARTS][BR_BLOCK], int32_t *out, uint32_t n)
{
    uint32_t i, p;
    uint32_t dry_center, comp_active;
    if (n > BR_BLOCK) n = BR_BLOCK;
    if (br_fx_fusion_pending) {
        uint32_t complete = 1u;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
        FM1_CPU1_WORKER_BARRIER();
#elif defined(BR_SOURCE_QUEUE_TEST)
        FM1_CPU1_WORKER_BARRIER();
#else
        __asm__ volatile("" ::: "memory");
#endif
        if (bus != br_fx_job_bus || n != br_fx_job_n) complete = 0u;
        for (p = 0; p < BR_PARTS; ++p)
            if (!br_fx_fusion_done[p]) complete = 0u;
        if (complete) {
#if BR_FX_MASTER_FRAME_INLINE_FINISH
            br_fx_finish_block(bus, out, n, br_fx_comp_active, 0u);
#else
            br_fx_finish_block(bus, out, n, br_fx_comp_active);
#endif
        }
        else br_fx_mute_block(out, n);
        br_fx_fusion_cancel();
        return;
    }
    br_fx_job_n = n;
    BR_FX_PREPARE_TARGET(br_fx_prepare_block)(n);
    comp_active = br_fx_comp_active;
    dry_center = br_fx_dry_center;
#if BR_FX_MASTER_FRAME_INLINE_FINISH
    if (dry_center && !comp_active) {
        /* Preserve detector reset before the dry-center block finishes. */
        if (n) BR_FX_PREPARE_INVOKE(br_comp_reset_detector);
        br_fx_finish_block(bus, out, n, 0u, 1u);
        return;
    }
#else
    if (dry_center && !comp_active) {
        /* Keep each track's signed division before summing: combining the
         * divisions would change truncation for negative or quiet samples. */
        if (n) BR_FX_PREPARE_INVOKE(br_comp_reset_detector);
#if BR_FX_FINISH_MASTER_FUSE
        {
        uint32_t master_active = BR_FX_MASTER_PREPARE_TARGET(
            br_master_fx_begin_block)(n);
        for (i = 0; i < n; ++i) {
            int32_t mixed = br_fx_div_pow2(bus[0][i], 2u) + br_fx_div_pow2(bus[1][i], 2u) +
                br_fx_div_pow2(bus[2][i], 2u) + br_fx_div_pow2(bus[3][i], 2u);
            out[2u * i] += mixed;
            out[2u * i + 1u] += mixed;
            if (master_active) br_master_fx_process_frame(&out[2u * i]);
        }
        }
#else
        for (i = 0; i < n; ++i) {
            int32_t mixed = br_fx_div_pow2(bus[0][i], 2u) + br_fx_div_pow2(bus[1][i], 2u) +
                br_fx_div_pow2(bus[2][i], 2u) + br_fx_div_pow2(bus[3][i], 2u);
            out[2u * i] += mixed;
            out[2u * i + 1u] += mixed;
        }
        br_master_fx_process(out, n);
#endif
        return;
    }
#endif
    br_fx_job_bus = bus;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    {
        uint32_t sequence = 0u, worker_result = 0u, use_worker = 0u;
        uint32_t status, cpu0_ok;
        uint32_t (* volatile available_fn)(void) = fm1_cpu1_worker_available;
        uint32_t (* volatile submit_fn)(void *, uint32_t *) = fm1_cpu1_worker_submit_fx_batch;
        uint32_t (* volatile wait_fn)(uint32_t, uint32_t, uint32_t *) = fm1_cpu1_worker_wait;
        uint32_t (* volatile abort_fn)(void) = fm1_cpu1_worker_abort_hold_reset;
        for (p = 0; p < BR_PARTS; ++p) br_fx_job_cost[p] = br_fx_job_cost[p] ? br_fx_job_cost[p] : 512u;
#if BR_AUDIO_FUSION_ONLY_BUILD
        if (!br_audio_source_worker_fault && available_fn()) {
#else
        if (!br_fx_pipeline_cpu0_only && !br_audio_source_worker_fault && available_fn()) {
#endif
            br_fx_build_batches();
            use_worker = br_fx_worker_batches[1].count != 0u;
        }
        if (use_worker) {
            status = submit_fn(&br_fx_worker_batches[1], &sequence);
            if (status == FM1_CPU1_WORKER_OK) {
                if (br_fx_serial_mode) {
                    /* Controlled ablation: run the exact CPU1 batch to completion,
                     * then run the same C0-assigned batch. DSP state/partitioning
                     * are unchanged; only concurrent execution is removed. */
                    /* Diagnostic serialization has no overlap to hide FX cost;
                     * allow at most one full render block, still explicitly bounded. */
                    status = wait_fn(sequence, 139319u, &worker_result);
                    cpu0_ok = status == FM1_CPU1_WORKER_OK && worker_result == 1u ?
                        br_fx_run_cpu0_batch(&br_fx_worker_batches[0]) : 0u;
                } else {
                    cpu0_ok = br_fx_run_cpu0_batch(&br_fx_worker_batches[0]);
                    status = wait_fn(sequence, 17415u, &worker_result);
                }
                if (!cpu0_ok || status != FM1_CPU1_WORKER_OK || worker_result != 1u) {
                    br_audio_source_worker_fault = 1u;
                    BR_AUDIO_FAULT_SITE(BR_AUDIO_FAULT_FX_BATCH);
                    (void)abort_fn();
                    /* This routine runs from .ram_audio. Read the HAL's
                     * sticky ownership word directly so the uncontained
                     * path cannot branch from RAM audio into XIP. */
                    FM1_CPU1_WORKER_BARRIER();
                    if (fm1_cpu1_worker_uncontained_fault) return;
                    /* The joined block is no longer trustworthy. Do not
                     * touch bus or DSP state here; mute this block only. */
                    for (i = 0; i < n; ++i) {
                        out[2u * i] = 0;
                        out[2u * i + 1u] = 0;
                    }
                    return;
                }
            } else use_worker = 0u;
        }
        if (!use_worker)
            for (p = 0; p < BR_PARTS; ++p) br_fx_process_track_job(p);
    }
#else
    for (p = 0; p < BR_PARTS; ++p) br_fx_process_track_job(p);
#endif
#if BR_FX_MASTER_FRAME_INLINE_FINISH
    br_fx_finish_block(bus, out, n, comp_active, 0u);
#else
    br_fx_finish_block(bus, out, n, comp_active);
#endif
}

BR_AUDIO_API void br_fx_process(int32_t bus[BR_PARTS][BR_BLOCK], int32_t *out, uint32_t n)
{
    if (n > BR_BLOCK) n = BR_BLOCK;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    uint32_t i;
    /* Only this half's failure mutes; a contained (held) C1 is not one. */
    if (br_audio_source_worker_fault) {
        for (i = 0; i < n; ++i) {
            out[2u * i] = 0;
            out[2u * i + 1u] = 0;
        }
        return;
    }
#endif
#if defined(FM1_DEVICE)
    typedef void (*br_fx_process_fn)(int32_t [BR_PARTS][BR_BLOCK], int32_t *, uint32_t);
    void *volatile target = (void *)&br_fx_process_ram;
    ((br_fx_process_fn)target)(bus, out, n);
#else
    br_fx_process_ram(bus, out, n);
#endif
}

#if !BR_AUDIO_FUSION_ONLY_BUILD
BR_FX_RAM_AUDIO void br_fx_process_pipeline(int32_t bus[BR_PARTS][BR_BLOCK],
                            int32_t keys[BR_PARTS][BR_BLOCK],
                            const br_pipeline_controls_t *controls,
                            int32_t *out, uint32_t n)
{
    if (!bus || !keys || !controls || !out || n > BR_BLOCK) return;
    br_fx_pipeline_controls = controls;
    br_fx_pipeline_keys = keys;
    br_fx_pipeline_cpu0_only = 1u;
#if defined(FM1_DEVICE)
    {
        typedef void (*br_fx_process_fn)(int32_t [BR_PARTS][BR_BLOCK], int32_t *, uint32_t);
        void *volatile target = (void *)&br_fx_process_ram;
        ((br_fx_process_fn)target)(bus, out, n);
    }
#else
    br_fx_process_ram(bus, out, n);
#endif
    br_fx_pipeline_cpu0_only = 0u;
    br_fx_pipeline_keys = 0;
    br_fx_pipeline_controls = 0;
}

uint32_t br_fx_process_pipeline_reverse_start(int32_t bus[BR_PARTS][BR_BLOCK],
    int32_t keys[BR_PARTS][BR_BLOCK], const br_pipeline_controls_t *controls,
    int32_t *out, uint32_t n)
{
    if (!bus || !keys || !controls || !out || n > BR_BLOCK) return 0u;
    br_fx_reverse_job.bus = bus;
    br_fx_reverse_job.keys = keys;
    br_fx_reverse_job.controls = controls;
    br_fx_reverse_job.out = out;
    br_fx_reverse_job.n = n;
    br_fx_reverse_job.sequence = 0u;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (!br_audio_source_worker_fault && fm1_cpu1_worker_available()) {
        uint32_t (* volatile submit_fn)(void *, uint32_t *) = fm1_cpu1_worker_submit_fx_batch;
        if (submit_fn(&br_fx_reverse_job, (uint32_t *)&br_fx_reverse_job.sequence) ==
            FM1_CPU1_WORKER_OK) return 1u;
    }
#elif defined(BR_SOURCE_QUEUE_TEST)
    extern uint32_t br_audio_fx_test_start(void *context);
    if (br_audio_fx_test_start(&br_fx_reverse_job)) return 1u;
#endif
    /* If the resident worker cannot accept this fixed job, keep the exact
     * delayed output by processing it serially on CPU0. */
#if defined(FM1_DEVICE)
    {
        typedef void (*br_fx_pipeline_fn)(int32_t [BR_PARTS][BR_BLOCK],
            int32_t [BR_PARTS][BR_BLOCK], const br_pipeline_controls_t *,
            int32_t *, uint32_t);
        void *volatile target = (void *)&br_fx_process_pipeline;
        ((br_fx_pipeline_fn)target)(bus, keys, controls, out, n);
    }
#else
    br_fx_process_pipeline(bus, keys, controls, out, n);
#endif
    return 0u;
}

uint32_t br_fx_process_pipeline_hybrid_start(
    int32_t bus[BR_PARTS][BR_BLOCK], int32_t keys[BR_PARTS][BR_BLOCK],
    const br_pipeline_controls_t *controls, int32_t *out, uint32_t n,
    uint32_t fx_valid, void *source_queue)
{
    if (!out || n > BR_BLOCK || !source_queue ||
        (fx_valid && (!bus || !keys || !controls))) return 0u;
    if (fx_valid) {
        br_fx_reverse_job.bus = bus;
        br_fx_reverse_job.keys = keys;
        br_fx_reverse_job.controls = controls;
        br_fx_reverse_job.out = out;
        br_fx_reverse_job.n = n;
    }
    br_fx_reverse_job.sequence = 0u;
    br_fx_hybrid_job.fx_context = fx_valid ? &br_fx_reverse_job : 0;
    br_fx_hybrid_job.source_context = source_queue;
    br_fx_hybrid_job.fx_valid = fx_valid ? 1u : 0u;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    FM1_CPU1_WORKER_BARRIER();
    if (!br_audio_source_worker_fault && fm1_cpu1_worker_available()) {
        uint32_t (* volatile submit_fn)(void *, uint32_t *) =
            fm1_cpu1_worker_submit_audio_hybrid;
        if (submit_fn(&br_fx_hybrid_job,
                (uint32_t *)&br_fx_reverse_job.sequence) == FM1_CPU1_WORKER_OK)
            return 1u;
    }
#elif defined(BR_SOURCE_QUEUE_TEST)
    FM1_CPU1_WORKER_BARRIER();
    {
        extern uint32_t br_audio_source_test_parallel;
        extern uint32_t br_audio_fx_hybrid_test_start(uint32_t task_id,
                                                       void *context);
        if (br_audio_source_test_parallel &&
            br_audio_fx_hybrid_test_start(
                FM1_CPU1_WORKER_TASK_AUDIO_FX_SOURCE_BATCH,
                &br_fx_hybrid_job)) return 1u;
    }
#else
    __sync_synchronize();
#endif
    /* No worker accepted the composite; serial FX plus CPU0 source rendering
     * preserves the existing one-block pipeline without creating ownership. */
    if (fx_valid) {
#if defined(FM1_DEVICE)
        {
            typedef void (*br_fx_pipeline_fn)(int32_t [BR_PARTS][BR_BLOCK],
                int32_t [BR_PARTS][BR_BLOCK], const br_pipeline_controls_t *,
                int32_t *, uint32_t);
            void *volatile target = (void *)&br_fx_process_pipeline;
            ((br_fx_pipeline_fn)target)(bus, keys, controls, out, n);
        }
#else
        br_fx_process_pipeline(bus, keys, controls, out, n);
#endif
    }
    return 0u;
}

uint32_t br_fx_process_pipeline_reverse_finish(int32_t *out, uint32_t n,
                                                uint32_t timeout_ticks)
{
    uint32_t ok = 0u;
    uint32_t contained = 1u;
    if (!out || n > BR_BLOCK) return 0u;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    {
        uint32_t result = 0u;
        uint32_t status = FM1_CPU1_WORKER_TIMEOUT;
        if (timeout_ticks) {
            uint32_t (* volatile wait_fn)(uint32_t, uint32_t, uint32_t *) = fm1_cpu1_worker_wait;
            status = wait_fn(br_fx_reverse_job.sequence, timeout_ticks, &result);
        }
        ok = status == FM1_CPU1_WORKER_OK && result == 1u;
        if (!ok) {
            extern volatile uint32_t br_audio_source_worker_fault;
            uint32_t (* volatile abort_fn)(void) = fm1_cpu1_worker_abort_hold_reset;
            br_audio_source_worker_fault = 1u;
            contained = abort_fn() == FM1_CPU1_WORKER_OK;
        } else {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
            /* Record the final CPU1 task wait (ID3 reverse FX or ID4 hybrid)
             * for the tail meter; source completion is not a substitute. */
            fm1_audio_cpu1_join_mark(fm1_ticks());
#endif
        }
    }
#elif defined(BR_SOURCE_QUEUE_TEST)
    {
        extern uint32_t br_audio_fx_test_join_ticks(uint32_t timeout_ticks);
        uint32_t join_status = br_audio_fx_test_join_ticks(timeout_ticks);
        ok = join_status == 1u;
        contained = join_status != 2u;
    }
#else
    (void)timeout_ticks;
    ok = 1u;
#endif
    if (!ok && contained) {
        uint32_t i;
        for (i = 0; i < n; ++i) { out[2u * i] = 0; out[2u * i + 1u] = 0; }
    }
    return ok ? 1u : contained ? 0u : 2u;
}
#endif /* !BR_AUDIO_FUSION_ONLY_BUILD */

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
BR_AUDIO_API void br_fx_set_serial_mode(uint32_t enabled)
{
    br_fx_serial_mode = enabled ? 1u : 0u;
}

BR_AUDIO_API uint32_t br_fx_serial_mode_get(void)
{
    return br_fx_serial_mode;
}
#endif

BR_AUDIO_API int br_fx_quiet(void)
{
    uint32_t p;
    for (p = 0; p < BR_PARTS; ++p) if (br_fx_track[p].tail_samples) return 0;
#if BR_FX_GLOBAL_REVERB
    if (!br_gr_rv.idle) return 0;
#endif
    /* filter_tail also covers the master ladder (see br_master_ladder_frame). */
    return br_master_state.filter_tail == 0u && br_master_state.wow_tail == 0u;
}
