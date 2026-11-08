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
#ifndef BR_FX_REVERB_FLOAT_Q15
#define BR_FX_REVERB_FLOAT_Q15 1
#endif
#if BR_FX_REVERB_FLOAT_Q15 != 0 && BR_FX_REVERB_FLOAT_Q15 != 1
#error "BR_FX_REVERB_FLOAT_Q15 must be 0 or 1"
#endif
#ifndef BR_FX_GENERIC_FEEDBACK_STABLE_CACHE
#define BR_FX_GENERIC_FEEDBACK_STABLE_CACHE 1
#endif
#if BR_FX_GENERIC_FEEDBACK_STABLE_CACHE != 0 && BR_FX_GENERIC_FEEDBACK_STABLE_CACHE != 1
#error "BR_FX_GENERIC_FEEDBACK_STABLE_CACHE must be 0 or 1"
#endif
#ifndef BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE
#define BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE 1
#endif
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE != 0 && BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE != 1
#error "BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE must be 0 or 1"
#endif
#ifndef BR_FX_REVERB_Q2_INLINE_DECODE
#define BR_FX_REVERB_Q2_INLINE_DECODE 1
#endif
#if BR_FX_REVERB_Q2_INLINE_DECODE != 0 && BR_FX_REVERB_Q2_INLINE_DECODE != 1
#error "BR_FX_REVERB_Q2_INLINE_DECODE must be 0 or 1"
#endif
#ifndef BR_FX_REVERB_OUTPUT_DIFFUSER_INLINE
#define BR_FX_REVERB_OUTPUT_DIFFUSER_INLINE 1
#endif
#if BR_FX_REVERB_OUTPUT_DIFFUSER_INLINE != 0 && BR_FX_REVERB_OUTPUT_DIFFUSER_INLINE != 1
#error "BR_FX_REVERB_OUTPUT_DIFFUSER_INLINE must be 0 or 1"
#endif
#if BR_FX_REVERB_OUTPUT_DIFFUSER_INLINE
#define BR_FX_OUTPUT_DIFFUSER_FN static inline __attribute__((always_inline))
#else
#define BR_FX_OUTPUT_DIFFUSER_FN static BR_FX_RAM_AUDIO
#endif
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

#ifndef BR_FX_REVERB_RATE2_HALL
#define BR_FX_REVERB_RATE2_HALL 0
#endif
/* Skip the work of a track's modulation stage at MIX 0 and of its delay stage at send 0.
 * Neither is heard then; only the state a later enable reads is kept. */
#ifndef BR_FX_STAGE_SKIP_IDLE
#define BR_FX_STAGE_SKIP_IDLE 1
#endif
/* The half-rate reverb keeps its input history in a mirrored ring (needs Hall's storage). */
#ifndef BR_FX_REVERB_RATE2_MIRROR
#define BR_FX_REVERB_RATE2_MIRROR BR_FX_HALL_FIR_MIRROR_HISTORY
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
#ifndef BR_FX_REVERB_RATE2_ALL
#define BR_FX_REVERB_RATE2_ALL 1
#endif
#ifndef BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL
#define BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL 1
#endif
#ifndef BR_FX_REVERB_RATE4_ROOM
#define BR_FX_REVERB_RATE4_ROOM 0
#endif
#ifndef BR_FX_REVERB_RATE4_HALL
#define BR_FX_REVERB_RATE4_HALL 1
#endif
#ifndef BR_FX_REVERB_RATE4_HALL_COMPACT
#define BR_FX_REVERB_RATE4_HALL_COMPACT 1
#endif
#ifndef BR_FX_REVERB_RATE4_HALL_HALF_BAND
#define BR_FX_REVERB_RATE4_HALL_HALF_BAND 1
#endif
#ifndef BR_FX_REVERB_RATE4_HALL_HALF_BAND_MAKEUP_73_64
#define BR_FX_REVERB_RATE4_HALL_HALF_BAND_MAKEUP_73_64 1
#endif
#ifndef BR_FX_HALL_FIR_MIRROR_HISTORY
#define BR_FX_HALL_FIR_MIRROR_HISTORY 1
#endif
#if BR_FX_HALL_FIR_MIRROR_HISTORY != 0 && BR_FX_HALL_FIR_MIRROR_HISTORY != 1
#error "BR_FX_HALL_FIR_MIRROR_HISTORY must be 0 or 1"
#endif
#if BR_FX_REVERB_RATE4_HALL_HALF_BAND && !BR_FX_REVERB_RATE4_HALL
#error "the Hall half-band requires BR_FX_REVERB_RATE4_HALL"
#endif
#if BR_FX_REVERB_RATE4_HALL_HALF_BAND_MAKEUP_73_64 && !BR_FX_REVERB_RATE4_HALL_HALF_BAND
#error "the 73/64 Hall makeup requires the Hall half-band"
#endif
#ifndef BR_FX_REVERB_SEND_EVENT_PRODUCT
#define BR_FX_REVERB_SEND_EVENT_PRODUCT 1
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
#ifndef BR_FX_STAGE_REVERB_RATE_DISPATCH
#define BR_FX_STAGE_REVERB_RATE_DISPATCH 1
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
#if BR_FX_STAGE_REVERB_RATE_DISPATCH != 0 && BR_FX_STAGE_REVERB_RATE_DISPATCH != 1
#error "BR_FX_STAGE_REVERB_RATE_DISPATCH must be 0 or 1"
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
#ifndef BR_FX_REVERB_PACK_ALL_RATE2_ROOM
#define BR_FX_REVERB_PACK_ALL_RATE2_ROOM 1
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
#ifndef BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB
#define BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB 1
#endif
#if BR_FX_REVERB_RATE4_HALL_COMPACT && \
    (!BR_FX_BLOCK_STAGE_ONLY_BUILD || !BR_FX_BLOCK_STAGE_PIPELINE || \
     !BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB || !BR_FX_REVERB_RATE4_HALL || \
     BR_FX_REVERB_RATE4_ROOM || BR_FX_REVERB_RATE2_HALL || \
     !BR_FX_REVERB_RATE4_HALL_HALF_BAND)
#error "compact Hall Q4 requires stage-only Hall Q4 + half-band, with no other Hall/Q4 engine path"
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
#if BR_FX_REVERB_PACK_ALL_RATE2_ROOM && !BR_FX_REVERB_RATE2_ALL
#error "packed room storage requires all-engine rate-2 reverb"
#endif
#if BR_FX_REVERB_RATE2_HALL || BR_FX_REVERB_RATE2_ALL
#define BR_FX_HAS_REVERB_RATE2 1
#else
#define BR_FX_HAS_REVERB_RATE2 0
#endif
#if BR_FX_REVERB_RATE4_ROOM || BR_FX_REVERB_RATE4_HALL
#define BR_FX_HAS_REVERB_RATE4 1
#else
#define BR_FX_HAS_REVERB_RATE4 0
#endif
#if BR_FX_HAS_REVERB_RATE2 || BR_FX_HAS_REVERB_RATE4
#define BR_FX_HAS_REVERB_RATE 1
#else
#define BR_FX_HAS_REVERB_RATE 0
#endif
#if BR_FX_HALL_FIR_MIRROR_HISTORY && \
    (!BR_FX_HAS_REVERB_RATE4 || !BR_FX_REVERB_RATE4_HALL || \
     !BR_FX_REVERB_RATE4_HALL_HALF_BAND)
#error "Hall FIR mirror history requires the Hall quarter-rate half-band path"
#endif
#if BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL != 0 && \
    BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL != 1
#error "BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL must be 0 or 1"
#endif
#if BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL && \
    (!BR_FX_REVERB_RATE2_ALL || !BR_FX_BLOCK_STAGE_ONLY_BUILD || \
     !BR_FX_BLOCK_STAGE_PIPELINE || !BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB)
#error "native reverb pruning requires all-engine rate-2 selection and the staged stage-only reverb path"
#endif
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE && \
    (!BR_FX_REVERB_RATE2_ALL || !BR_FX_REVERB_STAGE_PRUNE_NATIVE_FOR_RATE2_ALL)
#error "generic reverb read cursor cache requires all-engine Q2 with the native path pruned"
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

#if BR_FX_REVERB_SEND_EVENT_PRODUCT
/* Keep the configured floating-point boundary query out of the hot RAM
 * stage. Only products at +/-381 need it; all other inputs use the exact
 * magnitude threshold. */
static inline __attribute__((always_inline)) int
br_fx_reverb_send_event_from_product(int32_t product)
{
    uint32_t raw = (uint32_t)product;
    uint32_t sign = 0u - (uint32_t)(product < 0);
    uint32_t magnitude = (raw ^ sign) - sign;
    return magnitude >= 381u;
}
#endif

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
#define BR_FX_DELAY_MAX_SHIFT 5u
#else
#define BR_FX_DELAY_SAMPLES BR_FX_DELAY_FULL_SAMPLES
#define BR_FX_DELAY_MAX_SHIFT 0u
#endif
#define BR_FX_DELAY_XFADE_FRAMES 64u
#if BR_FX_REVERB_PACK_ALL_RATE2_ROOM
#if BR_FX_NATIVE_RATE_48K
#define BR_FX_ROOM_SAMPLES 4664u
#else
/* Max packed half-rate room lengths plus their per-engine offsets. */
#define BR_FX_ROOM_SAMPLES 4286u
#endif
#else
#if BR_FX_NATIVE_RATE_48K
#define BR_FX_ROOM_SAMPLES 9322u
#else
#define BR_FX_ROOM_SAMPLES 8566u
#endif
#endif
#if BR_FX_NATIVE_RATE_CALIBRATION && BR_FX_REVERB_PACK_ALL_RATE2_ROOM && \
    BR_FX_ROOM_SAMPLES < 3110u
#error "native32 packed room geometry exceeds the allocated room ring"
#endif
#if BR_FX_MOD_RATE2
#define BR_FX_MOD_SAMPLES 1024u
#else
#define BR_FX_MOD_SAMPLES 2048u
#endif
#define BR_FX_TAIL_SAMPLES (FS * 3u)
#define BR_FX_REVERB_OUTPUT_AP0_LENGTH 31u
#define BR_FX_REVERB_OUTPUT_AP1_LENGTH 47u

#ifndef BR_FX_GLOBAL_REVERB
#define BR_FX_GLOBAL_REVERB 1
#endif
#if BR_FX_GLOBAL_REVERB
/* The shared reverb needs one room line. It runs inside the render job of this
 * track, one block behind the tracks that feed it, so any track can host it.
 * Measured on build 1578, hosting it in the kit finalizer lowered the mean finish
 * time with stacked effects but raised the worst half: the finalizer starts after
 * the four lane jobs, so the reverb became a late tail. Track 3 stays the host. */
#define BR_FX_ROOM_LINES 1u
#ifndef BR_GR_HOST
#define BR_GR_HOST 2u
#endif
#else
#define BR_FX_ROOM_LINES BR_PARTS
#endif

/* Four independent mono effect memories. Their combined data reservation is
 * exactly 215,984 bytes: 131,072 delay + 68,528 reverb + 16,384 modulation. */
static int16_t br_delay_line[BR_PARTS][BR_FX_DELAY_SAMPLES] BR_POOL __attribute__((aligned(4)));
typedef int16_t br_fx_room_sample_t;
static br_fx_room_sample_t br_room_line[BR_FX_ROOM_LINES][BR_FX_ROOM_SAMPLES] BR_POOL __attribute__((aligned(4)));
static int16_t br_mod_line[BR_PARTS][BR_FX_MOD_SAMPLES] BR_POOL __attribute__((aligned(4)));
/* Two short output allpasses diffuse one shared wet sum into stereo. Their
 * 31 + 47 int32 samples use 1,248 bytes across all tracks (+624 bytes). */
static int32_t br_reverb_output_ap0[BR_PARTS][BR_FX_REVERB_OUTPUT_AP0_LENGTH]
    BR_POOL __attribute__((aligned(4)));
static int32_t br_reverb_output_ap1[BR_PARTS][BR_FX_REVERB_OUTPUT_AP1_LENGTH]
    BR_POOL __attribute__((aligned(4)));
/* Shared stereo master wow line: 2 KiB, linked modulation with separate channels. */
static int32_t br_master_wow_line[2][256] BR_POOL;

typedef struct {
    uint32_t delay_pos, room_pos[4], mod_pos, lfo_phase;
    uint32_t room_delay_q8[4], room_feedback[4];
    uint32_t delay_storage_shift, delay_valid_samples, delay_last_time;
    uint32_t delay_output_xfade_left;
    int32_t delay_output_last, delay_output_xfade_from;
#if BR_FX_MOD_RATE2
    uint32_t mod_rate2_phase, previous_mod_engine;
    int32_t mod_rate2_input_history[3], mod_rate2_previous_output;
#endif
    uint32_t glitch_count;
    int32_t delay_lp, room_lp[4], phase_state[4], reverb_send_lp;
    int32_t drive_lp, drive_dc, drive_prev, drive_send;     /* drive_send: last block's send, Q23 */
    float reverb_output_scale; /* unity 32; normalized wet output for Space */
    uint8_t reverb_output_ap_pos[2];
    uint8_t reverb_output_unity;
    int32_t glitch_hold;
    uint32_t tail_samples;
    uint32_t tail_storage;
    uint32_t previous_delay_wet, previous_reverb_wet, previous_mod_mix;
#if BR_FX_DELAY_RATE2
    uint32_t delay_rate2_phase, delay_rate2_history_pos;
    uint32_t glitch_half_error;
    int32_t delay_rate2_send_history[8], delay_rate2_previous_echo;
#if BR_FX_DELAY_MAX_SHIFT
    uint32_t delay_long_pos[BR_FX_DELAY_MAX_SHIFT];
    uint32_t delay_long_phase[BR_FX_DELAY_MAX_SHIFT];
    uint32_t delay_long_interp_phase;
    int32_t delay_long_history[BR_FX_DELAY_MAX_SHIFT][8];
    int32_t delay_long_previous_echo, delay_long_echo;
#endif
#endif
#if BR_FX_HAS_REVERB_RATE
    uint32_t reverb_rate2_phase, previous_reverb_rate2, reverb_rate2_ramp;
    uint32_t previous_reverb_engine;
    int32_t reverb_rate2_input_hist[6], reverb_rate2_previous_l, reverb_rate2_previous_r;
#endif
#if BR_FX_HAS_REVERB_RATE4
    uint32_t reverb_rate4_phase, previous_reverb_rate4;
    int32_t reverb_rate4_input_hist[6], reverb_rate4_from_l, reverb_rate4_from_r;
#endif
#if BR_FX_HALL_FIR_MIRROR_HISTORY
    /* Six-sample duplicated rings for Hall's two Q4 half-band stages. */
    uint32_t reverb_hall_q4_full_history_pos, reverb_hall_q4_half_history_pos;
    uint32_t reverb_hall_q4_history_valid;
    int32_t reverb_hall_q4_full_history_mirror[12];
    int32_t reverb_hall_q4_half_history_mirror[12];
#endif
} br_fx_track_t;
static br_fx_track_t br_fx_track[BR_PARTS] BR_POOL;

typedef struct {
    uint32_t wow_pos, wow_phase, filter_tail, wow_tail, wow_valid_samples;
    uint32_t filter, saturation, wow, resonance;
    uint32_t heat_type, heat_amount;
    uint32_t bit_hold_count;
    int32_t bit_hold[2];
    int32_t low[2];
    float svf_low[2], band[2];
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
} br_master_state_t;

/* The serialized br_master_fx_t keeps its legacy distortion member. The
 * renderer caches only the controls that still have an audible path. */
typedef struct {
    uint32_t filter, saturation, wow, resonance;
    uint32_t heat_type, heat_amount;
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
    uint16_t delay_tape_alpha;
#endif
    uint16_t glitch_hold, glitch_step;
    uint32_t reverb_engine, reverb_wet, reverb_tone, reverb_space;
    uint32_t reverb_return_q8; /* shared reverb: gain on the summed sends, 256 = unity */
    float reverb_output_scale; /* output-diffuser target */
    int32_t *reverb_output_ap0_base;
    int32_t *reverb_output_ap1_base;
    uint32_t reverb_feedback, reverb_alpha, reverb_tail_hold;
    uint16_t reverb_size_delay[4];
    uint32_t reverb_slew_shift;
    uint16_t reverb_feedback_line[4];
#if BR_FX_REVERB_FLOAT_Q15
    float reverb_alpha_float;
    float reverb_feedback_line_float[4];
#if BR_FX_GENERIC_FEEDBACK_STABLE_CACHE
    uint8_t reverb_feedback_stable;
#endif
#endif
    uint16_t reverb_start[4], reverb_length[4];
#if BR_FX_PAN_COEFF_CACHE
    uint16_t pan_left_q15, pan_right_q15;
#endif
#if BR_FX_HAS_REVERB_RATE
    uint32_t reverb_rate2;
    uint32_t reverb_rate4;
#endif
    uint32_t mod_engine, mod_depth, mod_mix, mod_lfo_inc, mod_base_q8, mod_span;
#if BR_FX_Q15_MIX_GAIN
    uint16_t delay_amount_gain, mod_mix_gain;
#elif BR_FX_FLOAT_MIX_GAIN
    float delay_amount_gain, mod_mix_gain;
#endif
} br_fx_controls_t;
static br_fx_controls_t br_fx_controls[BR_PARTS] BR_POOL;
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE
/* The generic Q2 kernel advances these only after a real internal wet tick.
 * Per-block preparation rebuilds them from room_pos without touching DSP state. */
static uint16_t br_fx_reverb_read_cursor[BR_PARTS][4] BR_POOL;
static const br_fx_room_sample_t *br_fx_reverb_read_ring[BR_PARTS][4] BR_POOL;
static uint8_t br_fx_reverb_read_cursor_stable[BR_PARTS] BR_POOL;
static uint8_t br_fx_reverb_read_cursor_init_mask[BR_PARTS] BR_POOL;
#endif
#ifndef BR_FX_STABLE_CONTROL_CACHE
#define BR_FX_STABLE_CONTROL_CACHE 1
#endif
#if BR_FX_STABLE_CONTROL_CACHE != 0 && BR_FX_STABLE_CONTROL_CACHE != 1
#error "BR_FX_STABLE_CONTROL_CACHE must be 0 or 1"
#endif
#if BR_FX_STABLE_CONTROL_CACHE
/* Exact raw control key: derived values never depend on advancing DSP state.
 * Full words preserve out-of-range inputs and low-bit glitch controls too. */
static uint32_t br_fx_control_key[BR_PARTS][14];
static uint32_t br_fx_control_key_valid;
/* Private warm prototype: remembers the pre-warm control baseline until the
 * first real non-dry block so cache preparation is behaviorally transparent. */
static uint32_t br_fx_warm_pending;
typedef struct {
    uint32_t engine, space, feedback;
    uint16_t feedback_line[4];
} br_fx_warm_baseline_t;
static br_fx_warm_baseline_t br_fx_warm_baseline[BR_PARTS];
typedef struct {
    float output_scale;
    uint32_t room_feedback[4];
    int32_t reverb_send_lp;
    uint8_t output_unity;
} br_fx_warm_saved_t;
/* This is called only from a foreground quiescent transaction, so static
 * scratch avoids adding 112 bytes to the audio/foreground stack. */
static br_fx_warm_saved_t br_fx_warm_saved[BR_PARTS];
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
    64u, 0u, 0u, 0u, BR_HEAT_BIT, 0u
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
static int32_t br_fx_track_stereo[BR_PARTS][BR_BLOCK][2] BR_POOL;
/* A track job that took the silent-skip path records the current block's
 * sequence, so the finish stage knows that row is all zero without a scan. */
static uint32_t br_fx_block_seq, br_fx_track_silent_seq[BR_PARTS];
static volatile uint32_t br_gr_separate_seq = 0xffffffffu; /* block whose reverb is its own job */
#if BR_FX_GLOBAL_REVERB
/* One reverb for all tracks. Each track job leaves its send (post-delay signal
 * times its send level) in the buffer for the current block; the shared reverb
 * sums the previous block's sends, so its wet return lags the tracks by one
 * block (5.8 ms) and runs in the render phase instead of the serial tail. */
static int32_t br_gr_send[2][BR_PARTS][BR_BLOCK] BR_POOL;
static int32_t br_gr_wet[BR_BLOCK][2] BR_POOL;
static br_fx_track_t br_gr_state BR_POOL;
static br_fx_controls_t br_gr_controls BR_POOL;
static uint8_t br_gr_have[2][BR_PARTS];
/* Input diffusion: four allpasses ahead of the reverb turn each hit into a dense burst
 * (without them the comb network answers a click with a few separate echoes). */
#define BR_GR_AP0_LEN 211u
#define BR_GR_AP1_LEN 157u
#define BR_GR_AP2_LEN 563u
#define BR_GR_AP3_LEN 409u
static int32_t br_gr_ap0[BR_GR_AP0_LEN] BR_POOL, br_gr_ap1[BR_GR_AP1_LEN] BR_POOL;
static int32_t br_gr_ap2[BR_GR_AP2_LEN] BR_POOL, br_gr_ap3[BR_GR_AP3_LEN] BR_POOL;
static uint16_t br_gr_ap_pos[4];
static uint32_t br_gr_key[3], br_gr_key_valid, br_gr_wanted, br_gr_run, br_gr_cur;
static void br_gr_prepare(void);
static void br_sp_clear(void);
/* Output of the DRIVE stage, one row per track; the rest of the track chain reads it as its input. */
static int32_t br_fx_drive_buf[BR_PARTS][BR_BLOCK] BR_POOL;
static void br_fx_drive_block(uint32_t p, const int32_t *in, int32_t *out, uint32_t n);
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
static volatile uint8_t br_fx_fusion_done[BR_PARTS];
#if !BR_AUDIO_FUSION_ONLY_BUILD
static br_master_fx_t br_pipeline_last_master = {64u, 0u, 0u, 0u, 0u};
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

static BR_FX_RAM_AUDIO void br_fx_pan_stereo(int32_t left, int32_t right, uint32_t pan,
                                              int32_t *out_left, int32_t *out_right)
{
    if (pan > 127u) pan = 127u;
    if (pan == 64u) {
        *out_left = left;
        *out_right = right;
    } else if (pan < 64u) {
        *out_left = left + br_fx_div_pow2(right * (int32_t)(64u - pan), 6u);
        *out_right = br_fx_div_pow2(right * (int32_t)pan, 6u);
    } else {
        *out_left = (left * (int32_t)(127u - pan)) / 63;
        *out_right = right + (left * (int32_t)(pan - 64u)) / 63;
    }
}

#if BR_FX_PAN_COEFF_CACHE
/* Right-pan gains are block-constant. A Q15 split replaces two per-sample
 * divisions by 63 with one multiply per output; rounded coefficients keep
 * each gain within half a Q15 LSB of the original rational pan law. */
static inline __attribute__((always_inline)) void br_fx_pan_stereo_cached(
    int32_t left, int32_t right, uint32_t pan, uint32_t left_q15,
    uint32_t right_q15, int32_t *out_left, int32_t *out_right)
{
    if (pan <= 64u) {
        br_fx_pan_stereo(left, right, pan, out_left, out_right);
    } else if (pan == 127u) {
        *out_left = 0;
        *out_right = right + left;
    } else {
        *out_left = br_fx_mul_q15(left, left_q15);
        *out_right = right + br_fx_mul_q15(left, right_q15);
    }
}

#if BR_FX_STAGE_EMIT_PAN_DIRECT
/* StageEmit calls this only when pan != 64 and block preparation already
 * clamped pan to 0..127. Skip the general helper's redundant center/clamp
 * tests while preserving its exact left/right pan arithmetic. */
static inline __attribute__((always_inline)) void
br_fx_pan_stereo_cached_noncenter(int32_t left, int32_t right, uint32_t pan,
    uint32_t left_q15, uint32_t right_q15, int32_t *out_left, int32_t *out_right)
{
    if (pan < 64u) {
        *out_left = left + br_fx_div_pow2(right * (int32_t)(64u - pan), 6u);
        *out_right = br_fx_div_pow2(right * (int32_t)pan, 6u);
    } else if (pan == 127u) {
        *out_left = 0;
        *out_right = right + left;
    } else {
        *out_left = br_fx_mul_q15(left, left_q15);
        *out_right = right + br_fx_mul_q15(left, right_q15);
    }
}
#endif
#endif

#if defined(FM1_DEVICE)
#define BR_FX_TABLE_DATA __attribute__((section(".data.fm1_fx_tables"), used))
#else
#define BR_FX_TABLE_DATA
#endif
#include "instrument_softclip_table.h"
/* The CPU1 FX worker reads these from inside the reverb inner loop.
 * Keep the immutable tables in copied internal RAM so CPU1 never fetches
 * this hot-path metadata from XIP while CPU0 is executing in flash. */
#if !BR_FX_REVERB_PACK_ALL_RATE2_ROOM
static const uint16_t br_room_start[4] BR_FX_TABLE_DATA = {0, 1777, 3776, 6015};
#endif
static const uint16_t br_room_length[BR_REVERB_COUNT][4] BR_FX_TABLE_DATA = {
    {521, 613, 733, 907},       /* room */
    {1061, 1277, 1499, 1723},   /* hall */
    {521, 613, 733, 907},       /* retired id 2 (Spring) runs as room */
    {1777, 1999, 2239, 2551},   /* space */
    {1777, 1999, 2239, 2551}    /* retired id 4 (Ethereal) runs as space */
};

/* Match per-second decay: g_i = g_longest^(length_i / longest_length).
 * The longest line keeps its original gain; shorter lines retain more on
 * each, more frequent round trip. A positive eight-term binomial expansion
 * of (1 - (1-g))^ratio is evaluated only during control preparation. The
 * SIZE/DECAY endpoint oracle bounds its Q15 approximation; no audio-rate power/LUT. */
static inline uint32_t br_fx_reverb_feedback_for_length(
    uint32_t feedback, uint32_t length, uint32_t reference_length)
{
    uint32_t ratio, drop, term, sum, factor;
    if (!length || !reference_length || length >= reference_length)
        return feedback;
    ratio = (length * 32768u + reference_length / 2u) / reference_length;
    drop = 32768u - feedback;
    term = (ratio * drop + 16384u) >> 15;
    sum = term;
    factor = ((32768u - ratio) / 2u * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    factor = (((65536u - ratio) / 3u) * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    factor = (((98304u - ratio) / 4u) * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    factor = (((131072u - ratio) / 5u) * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    factor = (((163840u - ratio) / 6u) * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    factor = (((196608u - ratio) / 7u) * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    factor = (((229376u - ratio) / 8u) * drop + 16384u) >> 15;
    term = (term * factor + 16384u) >> 15;
    sum += term;
    return 32768u - sum;
}

#if BR_FX_NATIVE_RATE_CALIBRATION
static const uint16_t br_fx_reverb_alpha_32k[128] BR_FX_TABLE_DATA = {
    1234u, 1561u, 1887u, 2211u, 2535u, 2859u, 3181u, 3502u,
    3822u, 4141u, 4459u, 4777u, 5093u, 5408u, 5722u, 6036u,
    6348u, 6659u, 6969u, 7279u, 7587u, 7894u, 8200u, 8505u,
    8809u, 9112u, 9414u, 9715u, 10015u, 10314u, 10611u, 10908u,
    11203u, 11498u, 11791u, 12083u, 12374u, 12664u, 12952u, 13240u,
    13526u, 13812u, 14096u, 14378u, 14660u, 14941u, 15220u, 15498u,
    15775u, 16050u, 16325u, 16598u, 16870u, 17140u, 17410u, 17678u,
    17944u, 18210u, 18474u, 18736u, 18998u, 19258u, 19517u, 19774u,
    20030u, 20284u, 20537u, 20789u, 21039u, 21288u, 21535u, 21781u,
    22026u, 22268u, 22510u, 22749u, 22988u, 23224u, 23459u, 23692u,
    23924u, 24154u, 24383u, 24609u, 24834u, 25058u, 25279u, 25499u,
    25717u, 25933u, 26147u, 26359u, 26570u, 26778u, 26985u, 27189u,
    27392u, 27592u, 27790u, 27986u, 28180u, 28372u, 28562u, 28749u,
    28934u, 29116u, 29296u, 29473u, 29648u, 29820u, 29990u, 30156u,
    30320u, 30481u, 30639u, 30793u, 30945u, 31093u, 31237u, 31378u,
    31515u, 31648u, 31777u, 31901u, 32021u, 32136u, 32245u, 32348u,
};
static const uint16_t br_fx_master_alpha_32k[63] BR_FX_TABLE_DATA = {
    248u, 906u, 1561u, 2211u, 2859u, 3502u, 4141u, 4777u,
    5408u, 6036u, 6659u, 7279u, 7894u, 8505u, 9112u, 9715u,
    10314u, 10908u, 11498u, 12083u, 12664u, 13240u, 13812u, 14378u,
    14941u, 15498u, 16050u, 16598u, 17140u, 17678u, 18210u, 18736u,
    19258u, 19774u, 20284u, 20789u, 21288u, 21781u, 22268u, 22749u,
    23224u, 23692u, 24154u, 24609u, 25058u, 25499u, 25933u, 26359u,
    26778u, 27189u, 27592u, 27986u, 28372u, 28749u, 29116u, 29473u,
    29820u, 30156u, 30481u, 30793u, 31093u, 31378u, 31648u,
};
#endif
#if BR_FX_NATIVE_RATE_48K
/* Per-sample coefficients converted from the established 44.1 kHz curve by
 * alpha48 = 1 - (1 - alpha44)^(441/480). Half-rate wet loops subsequently
 * apply their existing exact two-sample pole remap in control preparation. */
static const uint16_t br_fx_reverb_alpha_48k[128] BR_FX_TABLE_DATA = {
    828u, 1049u, 1270u, 1491u, 1713u, 1935u, 2156u, 2378u,
    2600u, 2822u, 3045u, 3267u, 3490u, 3713u, 3936u, 4159u,
    4382u, 4605u, 4829u, 5053u, 5276u, 5500u, 5725u, 5949u,
    6174u, 6398u, 6623u, 6848u, 7073u, 7299u, 7524u, 7750u,
    7976u, 8202u, 8428u, 8655u, 8881u, 9108u, 9335u, 9563u,
    9790u, 10018u, 10245u, 10473u, 10702u, 10930u, 11159u, 11388u,
    11617u, 11846u, 12076u, 12305u, 12535u, 12766u, 12996u, 13227u,
    13458u, 13689u, 13920u, 14152u, 14384u, 14616u, 14849u, 15081u,
    15314u, 15548u, 15781u, 16015u, 16249u, 16484u, 16718u, 16953u,
    17189u, 17424u, 17660u, 17896u, 18133u, 18370u, 18607u, 18845u,
    19083u, 19321u, 19560u, 19799u, 20039u, 20279u, 20519u, 20760u,
    21001u, 21243u, 21485u, 21727u, 21970u, 22214u, 22458u, 22702u,
    22947u, 23193u, 23439u, 23686u, 23933u, 24181u, 24429u, 24679u,
    24929u, 25179u, 25430u, 25683u, 25935u, 26189u, 26444u, 26699u,
    26956u, 27213u, 27472u, 27731u, 27992u, 28254u, 28518u, 28783u,
    29049u, 29317u, 29587u, 29859u, 30134u, 30410u, 30690u, 30973u,
};
static const uint16_t br_fx_master_alpha_48k[63] BR_FX_TABLE_DATA = {
    165u, 607u, 1049u, 1491u, 1935u, 2378u, 2822u, 3267u,
    3713u, 4159u, 4605u, 5053u, 5500u, 5949u, 6398u, 6848u,
    7299u, 7750u, 8202u, 8655u, 9108u, 9563u, 10018u, 10473u,
    10930u, 11388u, 11846u, 12305u, 12766u, 13227u, 13689u, 14152u,
    14616u, 15081u, 15548u, 16015u, 16484u, 16953u, 17424u, 17896u,
    18370u, 18845u, 19321u, 19799u, 20279u, 20760u, 21243u, 21727u,
    22214u, 22702u, 23193u, 23686u, 24181u, 24679u, 25179u, 25683u,
    26189u, 26699u, 27213u, 27731u, 28254u, 28783u, 29317u,
};
#endif
#if BR_FX_TAPE_WOW_DELTA_LUT
/* One complete, continuous triangular wow cycle. Interpolation below uses
 * the full oscillator phase, rather than repeating an audio-rate nibble.
 * Keep 64 bytes: smaller first tables trigger a vendor merge-section bug. */
static const int32_t br_fx_tape_wow_delta[16] BR_FX_TABLE_DATA = {
    -5, -4, -2, -1, 1, 2, 4, 5,
     5,  4,  2,  1,-1,-2,-4,-5
};
#endif
/* Durations are quarter-note beats. The requested fractional divisions use
 * standard note values; 1/4 aliases one beat and 1/2 aliases two beats. */
static const uint8_t br_fx_delay_note_num[11] BR_FX_TABLE_DATA =
    {1u, 1u, 1u, 1u, 1u, 1u, 1u, 2u, 2u, 3u, 4u};
static const uint8_t br_fx_delay_note_den[11] BR_FX_TABLE_DATA =
    {8u, 6u, 4u, 3u, 2u, 1u, 1u, 1u, 1u, 1u, 1u};
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

BR_AUDIO_API uint32_t br_fx_delay_note_index(uint32_t stored)
{
    stored = stored > 127u ? 127u : stored;
    return stored * 11u / 128u;
}

BR_AUDIO_API uint32_t br_fx_delay_note_store(uint32_t index)
{
    if (index > 10u) index = 10u;
    return (index * 127u + 5u) / 10u;
}

static BR_FX_PREPARE_CODE uint32_t br_fx_delay_note_samples(uint32_t stored,
                                                          uint32_t bpm)
{
    uint32_t index;
    uint32_t numerator, denominator;
    stored = stored > 127u ? 127u : stored;
    index = stored * 11u / 128u;
    bpm = bpm < BR_BPM_MIN ? BR_BPM_MIN : bpm > BR_BPM_MAX ? BR_BPM_MAX : bpm;
    numerator = br_fx_delay_note_num[index];
    denominator = br_fx_delay_note_den[index] * bpm;
    return (FS * 60u * numerator + denominator / 2u) / denominator;
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
static inline __attribute__((always_inline)) uint32_t br_fx_reverb_alpha(uint32_t tone)
{
    tone = tone > 127u ? 127u : tone;
#if BR_FX_NATIVE_RATE_CALIBRATION
    return br_fx_reverb_alpha_32k[tone];
#elif BR_FX_NATIVE_RATE_48K
    return br_fx_reverb_alpha_48k[tone];
#else
    return 900u + tone * 240u;
#endif
}
static inline __attribute__((always_inline)) uint32_t br_fx_master_filter_alpha(uint32_t filter)
{
    uint32_t index;
    if (filter == 63u || filter == 64u) return 0u;
    index = filter < 63u ? filter : filter - 65u;
#if BR_FX_NATIVE_RATE_CALIBRATION
    return br_fx_master_alpha_32k[index];
#elif BR_FX_NATIVE_RATE_48K
    return br_fx_master_alpha_48k[index];
#else
    return 180u + index * 480u;
#endif
}
/* Stay well inside the SVF stability boundary at low Q, avoiding an
 * unintended near-Nyquist pole. The cap opens smoothly with resonance;
 * maximum resonance retains the full calibrated cutoff table. */
static inline __attribute__((always_inline)) uint32_t
br_fx_master_resonance_alpha(uint32_t alpha, uint32_t resonance)
{
    uint32_t limit = 24576u + resonance * 64u;
    return alpha > limit ? limit : alpha;
}
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

#if BR_FX_DELAY_RATE2
static inline __attribute__((always_inline)) uint32_t
br_fx_tape_alpha_for_shift(uint32_t shift)
{
    uint32_t complement = 32768u - br_fx_tape_alpha_rate2();
    while (shift--)
        complement = (complement * complement + 16384u) >> 15;
    {
        uint32_t alpha = 32768u - complement;
        if (alpha > 32767u) alpha = 32767u;
        return alpha ? alpha : 1u;
    }
}
#endif
static inline __attribute__((always_inline)) uint32_t br_fx_reverb_ramp_frames(void)
{
    return br_fx_rate_samples(32u);
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

/* One common wet sum drives two unity-magnitude output allpasses. Their
 * unequal delays and opposite half-gains decorrelate the channels without
 * giving either side its own recursively decaying comb output. Inputs and
 * Q8 state carries fractional wet-sum history; output converts back to
 * track-output units after symmetric nearest rounding. */
BR_FX_OUTPUT_DIFFUSER_FN void br_fx_reverb_output_diffuse(br_fx_track_t *st,
    const br_fx_controls_t *c, int32_t *left, int32_t *right)
{
    int32_t rv_l = *left, rv_r = *right;
    int32_t mono_q8;
    /* Expanded ring storage plus Hall's 73/64 makeup bounds |rv| by
     * 4,782,960. Thus |mono_q8| <= 306,109,440, each stable half-feedback
     * allpass state <= 459,164,160 and |y_q8| <= 612,218,880: int32-safe. */
    if (st->reverb_output_unity) {
        mono_q8 = (rv_l + rv_r) * 32;
    } else {
        float output_scale_target = c->reverb_output_scale;
        float output_scale = st->reverb_output_scale;
        if (output_scale != output_scale_target) {
            float delta = output_scale_target - output_scale;
            output_scale += delta * (1.0f / 512.0f); /* 16 ms time constant at 32 kHz */
            /* Snap before a sub-ULP update stalls below the unity fast path. */
            if (delta > -(1.0f / 1024.0f) && delta < (1.0f / 1024.0f))
                output_scale = output_scale_target;
            st->reverb_output_scale = output_scale;
        }
        if (output_scale == 32.0f && output_scale_target == 32.0f)
            st->reverb_output_unity = 1u;
        /* Keep the unity path exactly integer; only normalized engines use FPU scaling. */
        mono_q8 = output_scale == 32.0f
            ? (rv_l + rv_r) * 32
            : (int32_t)((float)(rv_l + rv_r) * output_scale);
    }
    int32_t half_q8 = br_fx_div_pow2(mono_q8, 1u);
    uint32_t pos0 = st->reverb_output_ap_pos[0];
    uint32_t pos1 = st->reverb_output_ap_pos[1];
    int32_t delayed0 = c->reverb_output_ap0_base[pos0];
    int32_t delayed1 = c->reverb_output_ap1_base[pos1];
    int32_t y0_q8 = delayed0 - half_q8;
    int32_t y1_q8 = delayed1 + half_q8;

    c->reverb_output_ap0_base[pos0] = mono_q8 + br_fx_div_pow2(y0_q8, 1u);
    c->reverb_output_ap1_base[pos1] = mono_q8 - br_fx_div_pow2(y1_q8, 1u);
    pos0 = pos0 + 1u == BR_FX_REVERB_OUTPUT_AP0_LENGTH ? 0u : pos0 + 1u;
    pos1 = pos1 + 1u == BR_FX_REVERB_OUTPUT_AP1_LENGTH ? 0u : pos1 + 1u;
    st->reverb_output_ap_pos[0] = (uint8_t)pos0;
    st->reverb_output_ap_pos[1] = (uint8_t)pos1;
    /* Round symmetrically to nearest PCM unit, ties away from zero. */
    *left = (y0_q8 + (y0_q8 < 0 ? 127 : 128)) >> 8;
    *right = (y1_q8 + (y1_q8 < 0 ? 127 : 128)) >> 8;
}
static inline __attribute__((always_inline)) void br_fx_reverb_output_ap_reset(
    uint32_t p, br_fx_track_t *st)
{
    br_fx_clear32((br_fx_clear_word_t *)br_reverb_output_ap0[p], BR_FX_REVERB_OUTPUT_AP0_LENGTH);
    br_fx_clear32((br_fx_clear_word_t *)br_reverb_output_ap1[p], BR_FX_REVERB_OUTPUT_AP1_LENGTH);
    st->reverb_output_ap_pos[0] = 0u;
    st->reverb_output_ap_pos[1] = 0u;
    st->reverb_send_lp = 0;
    br_fx_clear32((br_fx_clear_word_t *)st->room_delay_q8, 4u);
    br_fx_clear32((br_fx_clear_word_t *)st->room_feedback, 4u);
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

/* Keep the original one-product path for normal int16-range deltas. The
 * compacted ring can widen poles beyond that range, so use the exact split
 * Q15 multiply there rather than overflowing x * alpha. */
static BR_FX_RAM_AUDIO int32_t br_fx_mul_q15_reverb_pole(int32_t x, uint32_t alpha)
{
    if (x >= -65535 && x <= 65535) {
        int32_t product = x * (int32_t)alpha;
        if (product < 0) product += 32767;
        return product >> 15;
    }
    return br_fx_mul_q15(x, alpha);
}

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

static inline __attribute__((always_inline)) uint32_t
br_fx_reverb_rate4_selected(uint32_t engine)
{
#if BR_FX_HAS_REVERB_RATE4
    return (BR_FX_REVERB_RATE4_ROOM && engine == BR_REVERB_ROOM) ||
           (BR_FX_REVERB_RATE4_HALL && engine == BR_REVERB_HALL);
#else
    (void)engine;
    return 0u;
#endif
}

#if BR_FX_HAS_REVERB_RATE2
static BR_FX_PREPARE_CODE uint32_t br_fx_reverb_rate2_selected(uint32_t engine)
{
#if BR_FX_REVERB_RATE2_ALL
    /* A selected quarter-rate Room/Hall path takes precedence. */
    return !br_fx_reverb_rate4_selected(engine);
#else
    return engine == BR_REVERB_HALL && !br_fx_reverb_rate4_selected(engine);
#endif
}
#endif

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
        return;
    }
#endif
    uint32_t before = br_master.revision;
    uint32_t filter, heat_type, heat_amount, resonance, after;
    if (before & 1u) return;
    filter = br_master.fx.filter;
    heat_type = br_master.fx.heat_type;
    heat_amount = br_master.fx.heat_amount;
    resonance = br_master.fx.resonance;
    after = br_master.revision;
    if (before != after || (after & 1u)) return;
    br_master_cached.filter = filter > 127u ? 127u : filter;
    br_master_cache_heat(heat_type, heat_amount);
    br_master_cached.resonance = resonance > 127u ? 127u : resonance;
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

/* Global filter, once per block: the knob is smoothed (smoothness 0 follows it raw; 100
 * ramps for up to a second, longer for bigger moves) and a short envelope retriggered
 * by every note adds its bipolar depth on top. */
static struct {
    uint32_t knob_q8, target, remaining, env;
    int32_t step_q8;
    uint32_t valid;
} br_gf;
static BR_FX_MASTER_PREPARE_CODE void br_gf_apply(void)
{
    uint32_t target = br_master.fx.filter, smooth = br_master.fx.smooth;
    int32_t depth = br_master.fx.env_depth, value, delta;
    if (target > 127u) target = 127u;
    if (smooth > 100u) smooth = 100u;
    if (depth < -127) depth = -127;
    if (depth > 127) depth = 127;
    if (!br_gf.valid) { br_gf.knob_q8 = target << 8; br_gf.target = target; br_gf.remaining = 0u; br_gf.valid = 1u; }
    if (!smooth) {
        br_gf.knob_q8 = target << 8; br_gf.target = target; br_gf.remaining = 0u;
    } else if (target != br_gf.target) {
        uint32_t magnitude, blocks;
        delta = (int32_t)(target << 8) - (int32_t)br_gf.knob_q8;
        magnitude = (uint32_t)(delta < 0 ? -delta : delta) >> 8;
        /* 172 blocks of 256 frames are about one second at 44.1 kHz */
        blocks = (smooth * magnitude * 172u + 6350u) / (100u * 127u);
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
    br_gf_apply();
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
        uint32_t filter = br_master_state.filter;
        uint32_t saturation = br_master_state.saturation;
        uint32_t resonance = br_master_state.resonance;
        br_master_state.stable_filter_alpha = br_fx_master_filter_alpha(filter);
        br_master_state.stable_resonance_alpha = br_fx_master_q15_float(
            br_fx_master_resonance_alpha(br_master_state.stable_filter_alpha,
                                         resonance));
        br_master_state.stable_saturation_gain = 128u + saturation * 4u;
        br_master_state.stable_filter_damping = br_fx_master_q15_float(
            65534u - resonance * 477u);
        br_master_state.stable_resonance_mix = resonance >= 127u ? 1.0f :
            br_fx_master_q15_float(resonance * 258u);
#endif
    }
#endif
    /* Once neutral, the master path has no per-sample state to advance.
     * A 63 <-> 64 filter change settles on the first sample in either path. */
    if ((br_master_cached.filter == 63u || br_master_cached.filter == 64u) &&
        (br_master_state.filter == 63u || br_master_state.filter == 64u) &&
        !br_master_cached.saturation && !br_master_state.saturation &&
        !br_master_cached.wow && !br_master_state.wow && !br_master_state.wow_tail &&
        !br_master_cached.heat_amount && !br_master_state.heat_amount) {
        if (n) {
            br_master_state.filter = br_master_cached.filter;
            br_master_state.resonance = br_master_cached.resonance;
            br_master_state.filter_tail = 0;
            br_master_state.low[0] = br_master_state.low[1] = 0;
            br_master_state.svf_low[0] = br_master_state.svf_low[1] = 0;
            br_master_state.band[0] = br_master_state.band[1] = 0;
        }
        return 0u;
    }
    return 1u;
}

/* The 44.1 kHz master filter maps valid controls 0..127 to alpha 0 or
 * 180 + index*480, index <= 62: alpha <= 29940 < 32768.  Elide the generic
 * Q15 alpha range test on the hot bounded-magnitude path only; retain the
 * original wide-magnitude fallback and exact truncation toward zero. */
#if !BR_FX_NATIVE_RATE_CALIBRATION && !BR_FX_NATIVE_RATE_48K
static inline __attribute__((always_inline)) int32_t
br_master_pole_alpha_bounded(int32_t current, int32_t target, uint32_t alpha)
{
    int32_t delta = target - current;
    uint32_t magnitude = delta < 0 ? 0u - (uint32_t)delta : (uint32_t)delta;
    int32_t step;
    if (magnitude <= 131071u) {
        uint32_t scaled = (magnitude * alpha) >> 15;
        step = delta < 0 ? -(int32_t)scaled : (int32_t)scaled;
    } else step = br_fx_mul_q15(delta, alpha);
    if (!step && delta) step = delta > 0 ? 1 : -1;
    return current + step;
}
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
        br_master_state.stable_filter_alpha =
            br_fx_master_filter_alpha(br_master_state.filter);
        br_master_state.stable_saturation_gain =
            128u + br_master_state.saturation * 4u;
        br_master_state.stable_resonance_alpha = br_fx_master_q15_float(
            br_fx_master_resonance_alpha(br_master_state.stable_filter_alpha,
                                         br_master_state.resonance));
        br_master_state.stable_filter_damping =
            br_fx_master_q15_float(65534u - br_master_state.resonance * 477u);
        br_master_state.stable_resonance_mix = br_master_state.resonance >= 127u ?
            1.0f : br_fx_master_q15_float(br_master_state.resonance * 258u);
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
    float filter_damping = br_master_state.stable_filter_damping;
    float resonance_alpha = br_master_state.stable_resonance_alpha;
    float resonance_mix = br_master_state.stable_resonance_mix;
#else
    float filter_damping = br_fx_master_q15_float(65534u - resonance * 477u);
    float resonance_alpha = br_fx_master_q15_float(
        br_fx_master_resonance_alpha(br_fx_master_filter_alpha(filter), resonance));
    float resonance_mix = resonance >= 127u ? 1.0f :
        br_fx_master_q15_float(resonance * 258u);
#endif
    int32_t input[2], filtered[2], shaped[2], result[2];
    int32_t peak;
    input[0] = stereo[0]; input[1] = stereo[1];
    peak = br_fx_abs(input[0]);
    if (br_fx_abs(input[1]) > peak) peak = br_fx_abs(input[1]);
    if (filter == 63u || filter == 64u) {
        br_master_state.filter_tail = 0;
        br_master_state.low[0] = br_master_state.low[1] = 0;
        br_master_state.svf_low[0] = br_master_state.svf_low[1] = 0;
        br_master_state.band[0] = br_master_state.band[1] = 0;
        filtered[0] = input[0]; filtered[1] = input[1];
    } else {
#if BR_FX_MASTER_STABLE_FASTPATH && BR_FX_MASTER_COEFF_CACHE
        uint32_t alpha = br_master_state.stable_filter_alpha;
#else
        uint32_t alpha = br_fx_master_filter_alpha(filter);
#endif
        if (peak > 2) br_master_state.filter_tail = FS * 2u;
        else if (br_master_state.filter_tail) --br_master_state.filter_tail;
#if BR_FX_MASTER_TAIL_GAIN_HOIST
        {
            uint32_t tail_gain = 127u;
            if (peak <= 2 && br_master_state.filter_tail > 0u &&
                br_master_state.filter_tail < FS / 4u)
                tail_gain = br_fx_tail_gain(br_master_state.filter_tail);
#endif
        /* Keep one stereo filter loop rather than duplicating the float
         * stage in RAM. Channel state and sample order remain independent. */
#if defined(__clang__)
#pragma clang loop unroll(disable)
#endif
        for (ch = 0; ch < 2u; ++ch) {
#if !BR_FX_NATIVE_RATE_CALIBRATION && !BR_FX_NATIVE_RATE_48K
            br_master_state.low[ch] = br_master_pole_alpha_bounded(br_master_state.low[ch], input[ch], alpha);
#else
            br_master_state.low[ch] = br_fx_pole(br_master_state.low[ch], input[ch], alpha);
#endif
            filtered[ch] = filter < 63u ? br_master_state.low[ch] :
                input[ch] - br_master_state.low[ch];
            if (resonance) {
                float svf_low = br_master_state.svf_low[ch] +
                    br_master_state.band[ch] * resonance_alpha;
                float svf_high, next_band, svf_filtered;
                svf_low = svf_low < -1048576.0f ? -1048576.0f :
                    svf_low > 1048576.0f ? 1048576.0f : svf_low;
                svf_high = (float)input[ch] - svf_low -
                    br_master_state.band[ch] * filter_damping;
                next_band = br_master_state.band[ch] + svf_high * resonance_alpha;
                next_band = next_band < -1048576.0f ? -1048576.0f :
                    next_band > 1048576.0f ? 1048576.0f : next_band;
                br_master_state.svf_low[ch] = svf_low;
                br_master_state.band[ch] = next_band;
                svf_filtered = filter < 63u ? svf_low : svf_high;
                /* Maximum resonance is exactly unity; otherwise crossfade in
                 * float and truncate only once at the output boundary. */
                if (resonance == 127u) filtered[ch] = (int32_t)svf_filtered;
                else filtered[ch] = (int32_t)((float)filtered[ch] +
                    (svf_filtered - (float)filtered[ch]) * resonance_mix);
            } else {
                br_master_state.svf_low[ch] = (float)br_master_state.low[ch];
                br_master_state.band[ch] = 0;
            }
            if (!br_master_state.filter_tail && peak <= 2) {
                br_master_state.low[ch] = 0;
                br_master_state.svf_low[ch] = 0;
                br_master_state.band[ch] = 0;
                filtered[ch] = 0;
            } else if (peak <= 2 && br_master_state.filter_tail < FS / 4u) {
#if BR_FX_MASTER_TAIL_GAIN_HOIST
                filtered[ch] = br_fx_wet_gain(filtered[ch], tail_gain);
#else
                uint32_t g = br_fx_tail_gain(br_master_state.filter_tail);
                filtered[ch] = br_fx_wet_gain(filtered[ch], g);
#endif
            }
        }
#if BR_FX_MASTER_TAIL_GAIN_HOIST
        }
#endif
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
    if (!br_master_state.filter_tail && !br_master_state.wow_tail &&
        !(br_master_state.low[0] | br_master_state.low[1] |
          br_master_state.bit_hold[0] | br_master_state.bit_hold[1]) &&
        br_master_state.svf_low[0] == 0.0f && br_master_state.svf_low[1] == 0.0f &&
        br_master_state.band[0] == 0.0f && br_master_state.band[1] == 0.0f) {
        int32_t any = 0;
        for (i = 2u * first; i < 2u * end; ++i) any |= stereo[i];
        if (!any) return;
    }
#endif
    for (i = first; i < end; ++i)
        br_master_fx_process_frame(&stereo[2u * i]);
}
#endif

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

static BR_FX_RAM_AUDIO int32_t br_fx_pole_reverb(int32_t current, int32_t target, uint32_t alpha)
{
    int32_t delta = target - current;
    int32_t step = br_fx_mul_q15_reverb_pole(delta, alpha);
    if (!step && delta) step = delta > 0 ? 1 : -1;
    return current + step;
}
#if BR_FX_REVERB_FLOAT_Q15
/* Generic reverb coefficients are cached blockwise; all feedback and pole
 * products stay inside the bounded int32 signal/state domain. */
static inline __attribute__((always_inline)) int32_t
br_fx_mul_q15_reverb_float(int32_t x, float q15)
{
    return (int32_t)((float)x * q15);
}
static inline __attribute__((always_inline)) int32_t
br_fx_pole_reverb_float(int32_t current, int32_t target, float alpha)
{
    int32_t delta = target - current;
    int32_t step = br_fx_mul_q15_reverb_float(delta, alpha);
    if (!step && delta) step = delta > 0 ? 1 : -1;
    return current + step;
}
#endif

static BR_FX_RAM_AUDIO int32_t br_fx_softclip(int32_t x)
{
    return br_fx_softclip_table_bounded(x);
}

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

/* Symmetric triangle keeps both endpoints inside [-32767,32767]. */
static BR_FX_RAM_AUDIO int32_t br_fx_tri(uint32_t phase)
{
    uint32_t x = phase >> 16;
    if (x < 32768u) return (int32_t)(x * 2u) - 32767;
    return 98303 - (int32_t)(x * 2u);
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
        for (i = 0u; i < 4u; ++i) {
            int32_t y = st->phase_state[i] -
                br_fx_mul_q15_phaser_selected(wet, (uint32_t)a);
            st->phase_state[i] = br_fx_clip_ram(
                wet + br_fx_mul_q15_phaser_selected(y, (uint32_t)a), -65535, 65535);
            wet = y;
        }
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
        br_mod_line[p][st->mod_pos] = br_fx_clip16_ram(filtered_input);
        delay_q8 = br_fx_mod_delay_q8(c->mod_base_q8, c->mod_span, tri) >> 1;
        if (delay_q8 >= BR_FX_MOD_SAMPLES * 256u)
            delay_q8 = (BR_FX_MOD_SAMPLES - 1u) * 256u;
        read_pos = (st->mod_pos - (delay_q8 >> 8)) & (BR_FX_MOD_SAMPLES - 1u);
        frac = delay_q8 & 255u;
        sample_a = br_mod_line[p][read_pos];
        sample_b = br_mod_line[p][(read_pos - 1u) & (BR_FX_MOD_SAMPLES - 1u)];
        wet = br_fx_interp_q8_i16(sample_a, sample_b, frac);
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

BR_AUDIO_API uint32_t br_fx_delay_samples(uint32_t value)
{
    uint32_t delay;
    value = br_fx_bound(value, 127u);
    delay = br_fx_rate_samples(32u + value * value);
    return delay >= BR_FX_DELAY_FULL_SAMPLES ? BR_FX_DELAY_FULL_SAMPLES - 1u : delay;
}

static BR_FX_RAM_AUDIO uint32_t br_fx_delay_samples_ram(uint32_t value)
{
    uint32_t delay;
    value = br_fx_bound_ram(value, 127u);
    delay = br_fx_rate_samples(32u + value * value);
    return delay >= BR_FX_DELAY_FULL_SAMPLES ? BR_FX_DELAY_FULL_SAMPLES - 1u : delay;
}

/* Derive controls only after a cache miss. Keep this cold setup compact;
 * the block dispatcher and per-sample kernels retain their O3 settings. */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void
br_fx_prepare_track_controls(uint32_t p, const br_sound_t *s,
                             const br_fx_config_t *cfg, br_fx_controls_t *c,
                             uint32_t bpm)
{
    uint32_t i;
#if BR_FX_NATIVE_RATE_CALIBRATION && BR_FX_REVERB_PACK_ALL_RATE2_ROOM
    uint32_t packed_room_start = 0u;
#endif
    uint32_t output_engine = br_fx_reverb_engine_of(cfg->reverb_engine);
    float output_target = 32.0f;
    if (output_engine == BR_REVERB_SPACE)
        output_target = 18.0f;
    if (!(br_fx_track[p].reverb_output_scale > 0.0f))
        br_fx_track[p].reverb_output_scale = output_target;
    c->reverb_output_scale = output_target;
    br_fx_track[p].reverb_output_unity =
        br_fx_track[p].reverb_output_scale == 32.0f && output_target == 32.0f;
        /* Loop-gain normalization depends only on engine geometry and SIZE.
         * Wet, PAN, delay and modulation edits must not redo its expansion. */
        uint32_t reverb_decay_refresh = !c->reverb_feedback_line[3] ||
            c->reverb_engine != br_fx_reverb_engine_of(cfg->reverb_engine) ||
            c->reverb_space != br_fx_bound_ram(s->space, 127u) ||
            c->reverb_feedback != 14000u + 130u * br_fx_bound_ram(cfg->reverb_tone, 127u);
        c->delay_engine = br_fx_bound_ram(cfg->delay_engine, BR_DELAY_COUNT - 1u);
        c->delay_amount = br_fx_bound_ram(s->delay, 127u);
#if BR_FX_Q15_MIX_GAIN
        c->delay_amount_gain = (uint16_t)((c->delay_amount * 32768u + 63u) / 127u);
#elif BR_FX_FLOAT_MIX_GAIN
        c->delay_amount_gain = c->delay_amount >= 127u ? 1.0f :
            (float)c->delay_amount * (1.0f / 127.0f);
#endif
        c->delay_time = BR_FX_PREPARE_INVOKE(br_fx_delay_note_samples, cfg->delay_time, bpm);
        c->delay_tail_hold = BR_FX_TAIL_SAMPLES;
        if (c->delay_time >= BR_FX_TAIL_SAMPLES - FS / 4u ||
            br_fx_bound_ram(cfg->delay_feedback, 127u))
            c->delay_tail_hold = c->delay_time + BR_FX_TAIL_SAMPLES;
        c->delay_storage_shift = 0u;
#if BR_FX_DELAY_RATE2
        while (c->delay_storage_shift < BR_FX_DELAY_MAX_SHIFT &&
               c->delay_time > (BR_FX_DELAY_SAMPLES << (c->delay_storage_shift + 1u)))
            ++c->delay_storage_shift;
        c->delay_tape_alpha = (uint16_t)br_fx_tape_alpha_for_shift(c->delay_storage_shift);
#endif
        for (i = 1u; i <= 4u; ++i) {
            uint32_t d = (c->delay_time * i) >> 2;
            c->delay_tap[i - 1u] = d < 1u ? 1u : d;
        }
        c->delay_feedback = br_fx_bound_ram(cfg->delay_feedback, 127u) * 145u;
        c->glitch_hold = (uint16_t)br_fx_rate_samples(1u + (cfg->delay_time & 7u));
        c->glitch_step = (uint16_t)(1u << (1u + (br_fx_bound_ram(cfg->delay_time, 127u) >> 4)));
        /* A different diffusion matrix has a different folded gain bound.
         * Never slew a previous unscaled gain through the dense matrix. */
        if (c->reverb_engine != br_fx_reverb_engine_of(cfg->reverb_engine)) {
            br_fx_clear32((br_fx_clear_word_t *)br_fx_track[p].room_feedback, 4u);
            br_fx_track[p].reverb_send_lp = 0;
        }
        c->reverb_engine = br_fx_reverb_engine_of(cfg->reverb_engine);
        c->reverb_wet = br_fx_bound_ram(s->reverb, 127u);
        /* Legacy serialized TONE is now independent DECAY. SIZE grows the
         * physical comb taps while progressively damping high frequencies. */
        c->reverb_tone = 86u - (62u * br_fx_bound_ram(s->space, 127u)) / 127u;
        c->reverb_space = br_fx_bound_ram(s->space, 127u);
        c->reverb_feedback = 14000u + 130u * br_fx_bound_ram(cfg->reverb_tone, 127u);
        c->reverb_tail_hold = FS * (2u + br_fx_bound_ram(cfg->reverb_tone, 127u) / 8u);
        c->reverb_alpha = br_fx_reverb_alpha(c->reverb_tone);
#if BR_FX_PAN_COEFF_CACHE
        if (br_fx_job_pan[p] > 64u) {
            uint32_t left_weight = 127u - br_fx_job_pan[p];
            c->pan_left_q15 = (uint16_t)((left_weight * 32768u + 31u) / 63u);
            c->pan_right_q15 = (uint16_t)(32768u - c->pan_left_q15);
        } else {
            c->pan_left_q15 = c->pan_right_q15 = 0u;
        }
#endif
        {
#if BR_FX_REVERB_PACK_ALL_RATE2_ROOM
            uint32_t packed_room_start = 0u;
#endif
            for (i = 0u; i < 4u; ++i) {
#if BR_FX_REVERB_PACK_ALL_RATE2_ROOM
                uint32_t packed_max = br_fx_rate_samples(
                    br_room_length[BR_REVERB_SPACE][i]);
                c->reverb_start[i] = (uint16_t)packed_room_start;
                packed_room_start += (packed_max + 1u) >> 1;
#else
                c->reverb_start[i] =
                    (uint16_t)br_fx_rate_samples(br_room_start[i]);
#endif
                c->reverb_length[i] = (uint16_t)br_fx_rate_samples(
                    br_room_length[c->reverb_engine][i]);
            }

        }
#if BR_FX_HAS_REVERB_RATE
#if BR_FX_HAS_REVERB_RATE2
        c->reverb_rate2 = BR_FX_PREPARE_INVOKE(br_fx_reverb_rate2_selected, c->reverb_engine);
#else
        c->reverb_rate2 = 0u;
#endif
#if BR_FX_HAS_REVERB_RATE4
        c->reverb_rate4 = br_fx_reverb_rate4_selected(c->reverb_engine);
#else
        c->reverb_rate4 = 0u;
#endif
        if (c->reverb_rate4) {
            /* Two cascaded native-to-half-rate decimators make a quarter-rate
             * path. Preserve the one-pole time constant across four samples. */
            uint32_t q = 32768u - c->reverb_alpha;
            uint32_t q2 = (q * q + 16384u) >> 15;
            uint32_t q4 = (q2 * q2 + 16384u) >> 15;
            c->reverb_alpha = 32768u - q4;
            for (i = 0u; i < 4u; ++i)
                c->reverb_length[i] = (uint16_t)((c->reverb_length[i] + 3u) >> 2);
        } else if (c->reverb_rate2) {
            /* A half-rate traversal lasts the same time through half as many
             * comb cells; remap the one-pole coefficient to that cadence. */
            c->reverb_alpha = 2u * c->reverb_alpha -
                ((c->reverb_alpha * c->reverb_alpha) >> 15);
            for (i = 0u; i < 4u; ++i)
                c->reverb_length[i] = (uint16_t)((c->reverb_length[i] + 1u) >> 1);
        }
#endif
        c->reverb_slew_shift = 10u;
#if BR_FX_HAS_REVERB_RATE
        if (c->reverb_rate4) c->reverb_slew_shift = 8u;
        else if (c->reverb_rate2) c->reverb_slew_shift = 9u;
#endif
        for (i = 0u; i < 4u; ++i)
            c->reverb_size_delay[i] = (uint16_t)((c->reverb_length[i] *
                (127u + c->reverb_space) + 127u) / 254u);
        if (reverb_decay_refresh) {
            uint32_t reference_length = 0u;
            for (i = 0u; i < 4u; ++i)
                if (c->reverb_length[i] > reference_length)
                    reference_length = c->reverb_length[i];
            for (i = 0u; i < 4u; ++i)
                c->reverb_feedback_line[i] = (uint16_t)
                    br_fx_reverb_feedback_for_length(c->reverb_feedback,
                        c->reverb_size_delay[i], reference_length);
            if (c->reverb_engine == BR_REVERB_SPACE)
                for (i = 0u; i < 4u; ++i)
                    c->reverb_feedback_line[i] = (uint16_t)
                        ((c->reverb_feedback_line[i] * 23170u + 16384u) >> 15);
        }
#if BR_FX_REVERB_FLOAT_Q15
        /* Q15 fractions are exactly representable as binary32. */
        c->reverb_alpha_float = (float)c->reverb_alpha * (1.0f / 32768.0f);
        for (i = 0u; i < 4u; ++i)
            c->reverb_feedback_line_float[i] =
                (float)c->reverb_feedback_line[i] * (1.0f / 32768.0f);
#endif
        c->mod_engine = br_fx_bound_ram(cfg->mod_engine, BR_MOD_COUNT - 1u);
        c->mod_depth = br_fx_bound_ram(cfg->mod_depth, 127u);
        c->mod_mix = br_fx_bound_ram(cfg->mod_mix, 127u);
#if BR_FX_Q15_MIX_GAIN
        c->mod_mix_gain = (uint16_t)((c->mod_mix * 32768u + 63u) / 127u);
#elif BR_FX_FLOAT_MIX_GAIN
        c->mod_mix_gain = c->mod_mix >= 127u ? 1.0f :
            (float)c->mod_mix * (1.0f / 127.0f);
#endif
        c->mod_lfo_inc = br_fx_rate_phase(
            4096u + br_fx_bound_ram(cfg->mod_rate, 127u) * 6144u);
        if (c->mod_engine == BR_MOD_FLANGE) {
            c->mod_base_q8 = 24u * 256u;
            c->mod_span = 1u + c->mod_depth * 4u;
        } else if (c->mod_engine == BR_MOD_VIBRATO) {
            c->mod_base_q8 = 48u * 256u;
            c->mod_span = 8u + c->mod_depth * 7u;
        } else {
            c->mod_base_q8 = 320u * 256u;
            c->mod_span = 1u + c->mod_depth * 4u;
        }
        c->mod_base_q8 = br_fx_rate_q8(c->mod_base_q8);
        c->mod_span = br_fx_rate_samples(c->mod_span);
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
        if (br_fx_job_enabled[p] || br_fx_track[p].tail_samples || br_fx_job_pan[p] != 64u)
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
        uint32_t key[14] = {
            s->delay, s->reverb, s->space, s->pan,
            cfg->delay_engine, cfg->delay_time, cfg->delay_feedback,
            cfg->reverb_engine, cfg->reverb_tone,
            cfg->mod_engine, cfg->mod_depth, cfg->mod_mix, cfg->mod_rate, bpm
        };
        uint32_t key_index = 0u;
        if (br_fx_control_key_valid & (1u << p)) {
            for (; key_index < 14u; ++key_index)
                if (key[key_index] != br_fx_control_key[p][key_index]) break;
            if (key_index == 14u) {
                uint32_t bit = 1u << p;
                if (br_fx_warm_pending & bit) {
                    uint32_t engine = br_fx_reverb_engine_of(cfg->reverb_engine);
                    if (!(br_fx_track[p].reverb_output_scale > 0.0f)) {
                        br_fx_track[p].reverb_output_scale = c->reverb_output_scale;
                        br_fx_track[p].reverb_output_unity =
                            c->reverb_output_scale == 32.0f;
                    }
                    if (br_fx_warm_baseline[p].engine != engine) {
                        br_fx_clear32((br_fx_clear_word_t *)
                            br_fx_track[p].room_feedback, 4u);
                        br_fx_track[p].reverb_send_lp = 0;
                    }
                    br_fx_warm_pending &= ~bit;
                }
                continue;
            }
        }
        for (key_index = 0u; key_index < 14u; ++key_index)
            br_fx_control_key[p][key_index] = key[key_index];
        br_fx_control_key_valid |= 1u << p;
        if (br_fx_warm_pending & (1u << p)) {
            c->reverb_engine = br_fx_warm_baseline[p].engine;
            c->reverb_space = br_fx_warm_baseline[p].space;
            c->reverb_feedback = br_fx_warm_baseline[p].feedback;
            memcpy(c->reverb_feedback_line,
                   br_fx_warm_baseline[p].feedback_line,
                   sizeof br_fx_warm_baseline[p].feedback_line);
        }
#endif
        BR_FX_PREPARE_INVOKE(br_fx_prepare_track_controls, p, s, cfg, c, bpm);
#if BR_FX_STABLE_CONTROL_CACHE
        br_fx_warm_pending &= ~(1u << p);
#endif
    }
#if BR_FX_REVERB_FLOAT_Q15 && BR_FX_GENERIC_FEEDBACK_STABLE_CACHE
    /* Per-block cache, including raw-control-key hits. A zero feedback state
     * is the lazy-init sentinel. In-flight nonzero changes keep the exact
     * per-sample loop-gain helper until a later block is stable. */
    for (p = 0; p < BR_PARTS; ++p) {
        const br_fx_controls_t *c = &br_fx_controls[p];
        uint32_t line, stable = 1u;
        for (line = 0u; line < 4u; ++line) {
            uint32_t current = br_fx_track[p].room_feedback[line];
            if (current && current != c->reverb_feedback_line[line])
                stable = 0u;
        }
        br_fx_controls[p].reverb_feedback_stable = (uint8_t)stable;
    }
#endif
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE && !BR_FX_GLOBAL_REVERB
    /* Accept only zero/lazy or settled delays. Invalid positions and zero
     * delays are normalized at the original first-read point, not here. */
    for (p = 0; p < BR_PARTS; ++p) {
        const br_fx_controls_t *c = &br_fx_controls[p];
        uint32_t line, stable = 1u;
        uint8_t init_mask = 0u;
        for (line = 0u; line < 4u; ++line) {
            uint32_t target = (uint32_t)c->reverb_size_delay[line] << 8u;
            uint32_t current = br_fx_track[p].room_delay_q8[line];
            uint32_t length = c->reverb_length[line];
            uint32_t pos = br_fx_track[p].room_pos[line];
            uint32_t index;
            uint8_t bit = (uint8_t)(1u << line);
            if (current && current != target) stable = 0u;
            if (!current && target) init_mask |= bit;
            if (pos >= length) {
                pos = 0u;
                init_mask |= bit;
            }
            index = pos + length - c->reverb_size_delay[line];
            if (index >= length) index -= length;
            br_fx_reverb_read_cursor[p][line] = (uint16_t)index;
            br_fx_reverb_read_ring[p][line] =
                &br_room_line[p][c->reverb_start[line]];
        }
        br_fx_reverb_read_cursor_stable[p] = (uint8_t)stable;
        br_fx_reverb_read_cursor_init_mask[p] = init_mask;
    }
#endif
#if BR_FX_GLOBAL_REVERB
    /* The shared reverb runs as "track 0" of the kernels' per-track caches: no
     * track uses its own reverb any more, so slot 0 is rebuilt from its state. */
    if (br_gr_wanted) {
        br_fx_controls_t *c = &br_gr_controls;
        br_fx_track_t *st = &br_gr_state;
        uint32_t line;
        br_gr_prepare();
#if BR_FX_REVERB_FLOAT_Q15 && BR_FX_GENERIC_FEEDBACK_STABLE_CACHE
        {
            uint32_t stable = 1u;
            for (line = 0u; line < 4u; ++line) {
                uint32_t current = st->room_feedback[line];
                if (current && current != c->reverb_feedback_line[line]) stable = 0u;
            }
            c->reverb_feedback_stable = (uint8_t)stable;
        }
#endif
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE
        {
            uint32_t stable = 1u;
            uint8_t init_mask = 0u;
            for (line = 0u; line < 4u; ++line) {
                uint32_t target = (uint32_t)c->reverb_size_delay[line] << 8u;
                uint32_t current = st->room_delay_q8[line];
                uint32_t length = c->reverb_length[line];
                uint32_t pos = st->room_pos[line];
                uint32_t index;
                uint8_t bit = (uint8_t)(1u << line);
                if (current && current != target) stable = 0u;
                if (!current && target) init_mask |= bit;
                if (pos >= length) { pos = 0u; init_mask |= bit; }
                index = pos + length - c->reverb_size_delay[line];
                if (index >= length) index -= length;
                br_fx_reverb_read_cursor[0][line] = (uint16_t)index;
                br_fx_reverb_read_ring[0][line] = &br_room_line[0][c->reverb_start[line]];
            }
            br_fx_reverb_read_cursor_stable[0] = (uint8_t)stable;
            br_fx_reverb_read_cursor_init_mask[0] = init_mask;
        }
#endif
        (void)st; (void)line;
    }
#endif
}

/* Call only after session/effective snapshots are rebuilt, under the app's
 * quiescent transaction. The stopped+quiet check prevents state advancement.
 * Cache/control writes are retained; track-state initializer side effects
 * are restored until the first real block. */
BR_AUDIO_API void br_fx_warm_controls_quiescent(void)
{
#if BR_FX_STABLE_CONTROL_CACHE
    if (br_playing || !br_engine_quiet() || !br_fx_quiet()) return;
    uint32_t p, keep_original = br_fx_warm_pending != 0u;
    for (p = 0u; p < BR_PARTS; ++p) {
        br_fx_warm_saved[p].output_scale = br_fx_track[p].reverb_output_scale;
        memcpy(br_fx_warm_saved[p].room_feedback, br_fx_track[p].room_feedback,
               sizeof br_fx_warm_saved[p].room_feedback);
        br_fx_warm_saved[p].reverb_send_lp = br_fx_track[p].reverb_send_lp;
        br_fx_warm_saved[p].output_unity = br_fx_track[p].reverb_output_unity;
        if (!keep_original) {
            br_fx_warm_baseline[p].engine = br_fx_controls[p].reverb_engine;
            br_fx_warm_baseline[p].space = br_fx_controls[p].reverb_space;
            br_fx_warm_baseline[p].feedback = br_fx_controls[p].reverb_feedback;
            memcpy(br_fx_warm_baseline[p].feedback_line,
                   br_fx_controls[p].reverb_feedback_line,
                   sizeof br_fx_warm_baseline[p].feedback_line);
        }
    }
    br_fx_prepare_block(0u);
    if (br_fx_dry_center && !br_fx_comp_active) return;
    for (p = 0u; p < BR_PARTS; ++p) {
        br_fx_track[p].reverb_output_scale = br_fx_warm_saved[p].output_scale;
        memcpy(br_fx_track[p].room_feedback, br_fx_warm_saved[p].room_feedback,
               sizeof br_fx_warm_saved[p].room_feedback);
        br_fx_track[p].reverb_send_lp = br_fx_warm_saved[p].reverb_send_lp;
        br_fx_track[p].reverb_output_unity = br_fx_warm_saved[p].output_unity;
    }
    br_fx_warm_pending = (1u << BR_PARTS) - 1u;
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
        echo = br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]);
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
            weighted_sum += (int32_t)br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]) * (int32_t)(5u - tap);
#else
            sum += br_fx_div10((int32_t)br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]) * (int32_t)(5u - tap));
#endif
        }
#if BR_FX_MULTITAP_COMBINED_DIV
        sum = br_fx_div10(weighted_sum);
#endif
        echo = sum;
    } else {
        read_pos = (st->delay_pos - time) & (BR_FX_DELAY_SAMPLES - 1u);
        echo = br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]);
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
    write = send + br_fx_delay_feedback_q15(echo, feedback);
    if (engine == BR_DELAY_GLITCH) write = send + br_fx_delay_feedback_q15(echo, (feedback * 3u) >> 2);
    /* Drop quantization residue instead of letting feedback hold a DC tick forever. */
    if (br_fx_abs(write) <= 2) write = 0;
    br_delay_line[p][st->delay_pos] = br_fx_reverb_compress16_fast(write);
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
    return input + BR_FX_BLEND127(echo, amount, c->delay_amount_gain);
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
    int32_t a = br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]);
    if (!fraction) return a;
    int32_t b = br_fx_reverb_decompress16_fast(br_delay_line[p][(read_pos - 1u) &
        (BR_FX_DELAY_SAMPLES - 1u)]);
    return br_fx_reverb_lerp_q8_companded(a, b, fraction);
}

static BR_FX_RAM_AUDIO int32_t br_fx_delay_rate2_step(br_fx_track_t *st,
    uint32_t p, int32_t send, const br_fx_controls_t *c)
{
    uint32_t engine = c->delay_engine;
    uint32_t shift = c->delay_storage_shift;
    uint32_t time_q8 = (c->delay_time * 128u) >> shift;
    uint32_t feedback = c->delay_feedback;
    uint32_t tap;
    int32_t echo, write;
    if (engine == BR_DELAY_TAPE) {
        int32_t wow_time_q8 = (int32_t)(c->delay_time * 256u) +
            br_fx_tape_wow_q8(st->lfo_phase);
        if (wow_time_q8 < 256) wow_time_q8 = 256;
        echo = br_fx_delay_rate2_read(p, st->delay_pos,
            (uint32_t)wow_time_q8 >> (shift + 1u), st->delay_valid_samples);
        echo = br_fx_delay_softclip(echo);
        uint32_t alpha = c->delay_tape_alpha;
        if (!alpha) alpha = 1u;
        st->delay_lp = br_fx_pole_bounded(st->delay_lp, echo, alpha);
        echo = st->delay_lp;
    } else if (engine == BR_DELAY_MULTITAP) {
        int32_t sum = 0;
#if BR_FX_MULTITAP_COMBINED_DIV
        int32_t weighted_sum = 0;
#endif
        for (tap = 1u; tap <= 4u; ++tap) {
            int32_t tap_echo = br_fx_delay_rate2_read(p, st->delay_pos,
                BR_FX_DELAY_GRID_Q8((c->delay_tap[tap - 1u] * 128u) >> shift),
                st->delay_valid_samples);
#if BR_FX_MULTITAP_COMBINED_DIV
            weighted_sum += tap_echo * (int32_t)(5u - tap);
#else
            sum += br_fx_div10(tap_echo * (int32_t)(5u - tap));
#endif
        }
#if BR_FX_MULTITAP_COMBINED_DIV
        sum = br_fx_div10(weighted_sum);
#endif
        echo = sum;
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
        echo = st->glitch_hold;
        --st->glitch_count;
    } else {
        echo = br_fx_delay_rate2_read(p, st->delay_pos, time_q8,
                                      st->delay_valid_samples);
    }
    if (engine == BR_DELAY_TAPE) echo = br_fx_clip_ram(echo, -32767, 32767);
    write = send + br_fx_delay_feedback_q15(echo, feedback);
    if (engine == BR_DELAY_GLITCH)
        write = send + br_fx_delay_feedback_q15(echo, (feedback * 3u) >> 2);
    if (br_fx_abs(write) <= 2) write = 0;
    br_delay_line[p][st->delay_pos] = br_fx_reverb_compress16_fast(write);
    st->delay_pos = (st->delay_pos + 1u) & (BR_FX_DELAY_SAMPLES - 1u);
    if (st->delay_valid_samples < BR_FX_DELAY_SAMPLES) ++st->delay_valid_samples;
    return echo;
}

#if BR_FX_DELAY_MAX_SHIFT
/* Further powers-of-two decimation for long wet delays. Each stage uses the
 * same seven-tap binomial half-band used by the existing rate-2 path. */
static BR_FX_RAM_AUDIO uint32_t br_fx_delay_long_decimate(br_fx_track_t *st,
    int32_t input, uint32_t levels, int32_t *output)
{
    uint32_t level;
    int32_t sample = input;
    for (level = 0u; level < levels; ++level) {
        uint32_t pos = st->delay_long_pos[level];
        uint32_t d1 = (pos - 1u) & 7u, d2 = (pos - 2u) & 7u;
        uint32_t d3 = (pos - 3u) & 7u, d4 = (pos - 4u) & 7u;
        uint32_t d5 = (pos - 5u) & 7u, d6 = (pos - 6u) & 7u;
        int32_t *history = st->delay_long_history[level];
        history[pos] = sample;
        st->delay_long_pos[level] = (pos + 1u) & 7u;
        if (!st->delay_long_phase[level]) {
            st->delay_long_phase[level] = 1u;
            return 0u;
        }
        st->delay_long_phase[level] = 0u;
        sample = br_fx_div_pow2(history[pos] + 6 * history[d1] +
            15 * history[d2] + 20 * history[d3] + 15 * history[d4] +
            6 * history[d5] + history[d6], 6u);
    }
    *output = sample;
    return 1u;
}
#endif

static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_fx_delay_storage_reset(br_fx_track_t *st,
                                                        uint32_t shift)
{
    st->delay_storage_shift = shift;
    st->delay_pos = 0u;
    st->delay_valid_samples = 0u;
    st->delay_rate2_phase = 0u;
    st->delay_rate2_history_pos = 0u;
    st->delay_rate2_previous_echo = 0;
    st->delay_long_previous_echo = st->delay_long_echo = 0;
    st->delay_long_interp_phase = 0u;
    st->delay_lp = 0;
    st->glitch_count = 0u;
    st->glitch_hold = 0;
    br_fx_clear32((br_fx_clear_word_t *)st->delay_rate2_send_history,
                  sizeof st->delay_rate2_send_history / sizeof(uint32_t));
#if BR_FX_DELAY_MAX_SHIFT
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_history,
                  sizeof st->delay_long_history / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_pos,
                  sizeof st->delay_long_pos / sizeof(uint32_t));
    br_fx_clear32((br_fx_clear_word_t *)st->delay_long_phase,
                  sizeof st->delay_long_phase / sizeof(uint32_t));
#endif
}

static BR_FX_RAM_AUDIO int32_t br_fx_delay(br_fx_track_t *st, uint32_t p, int32_t input,
    const br_fx_controls_t *c BR_FX_TAIL_EVENT_PARAM)
{
    uint32_t amount = c->delay_amount;
    uint32_t shift = c->delay_storage_shift;
    uint32_t history_pos = st->delay_rate2_history_pos;
    int32_t send = amount ? br_fx_div127(input * (int32_t)amount) : 0;
    int32_t echo = st->delay_rate2_previous_echo;
    int32_t wet_echo;
    uint32_t phase = st->delay_rate2_phase;
    uint32_t long_write = 0u;
    uint32_t d1 = (history_pos - 1u) & 7u;
    uint32_t d2 = (history_pos - 2u) & 7u;
    uint32_t d3 = (history_pos - 3u) & 7u;
    uint32_t d4 = (history_pos - 4u) & 7u;
    uint32_t d5 = (history_pos - 5u) & 7u;
    uint32_t d6 = (history_pos - 6u) & 7u;
    if (shift != st->delay_storage_shift || c->delay_time != st->delay_last_time) {
        st->delay_output_xfade_from = st->delay_output_last;
        st->delay_output_xfade_left = BR_FX_DELAY_XFADE_FRAMES;
        st->delay_last_time = c->delay_time;
        if (shift != st->delay_storage_shift)
            br_fx_delay_storage_reset(st, shift);
    }
    history_pos = st->delay_rate2_history_pos;
    phase = st->delay_rate2_phase;
    st->delay_rate2_send_history[history_pos] = send;
    if (phase == 1u) {
        int32_t weighted = st->delay_rate2_send_history[history_pos] +
            6 * st->delay_rate2_send_history[d1] +
            15 * st->delay_rate2_send_history[d2] +
            20 * st->delay_rate2_send_history[d3] +
            15 * st->delay_rate2_send_history[d4] +
            6 * st->delay_rate2_send_history[d5] +
            st->delay_rate2_send_history[d6];
        int32_t downsampled_send = br_fx_div_pow2(weighted, 6u);
        if (!shift) {
            echo = br_fx_delay_rate2_step(st, p, downsampled_send, c);
            wet_echo = br_fx_div_pow2(st->delay_rate2_previous_echo + echo, 1u);
            st->delay_rate2_previous_echo = echo;
        } else {
#if BR_FX_DELAY_MAX_SHIFT
            int32_t long_send;
            if (br_fx_delay_long_decimate(st, downsampled_send, shift, &long_send)) {
                echo = br_fx_delay_rate2_step(st, p, long_send, c);
                st->delay_long_previous_echo = st->delay_long_echo;
                st->delay_long_echo = echo;
                st->delay_long_interp_phase = 1u;
                long_write = 1u;
            }
#endif
        }
    }
    if (shift) {
#if BR_FX_DELAY_MAX_SHIFT
        uint32_t period = 2u << shift;
        if (!long_write && st->delay_long_interp_phase < period)
            ++st->delay_long_interp_phase;
        if (st->delay_long_interp_phase > period)
            st->delay_long_interp_phase = period;
        wet_echo = st->delay_long_previous_echo +
            (st->delay_long_echo - st->delay_long_previous_echo) *
            (int32_t)st->delay_long_interp_phase / (int32_t)period;
#else
        wet_echo = 0;
#endif
    } else if (phase != 1u) {
        wet_echo = echo;
    }
    st->delay_rate2_history_pos = (history_pos + 1u) & 7u;
    st->delay_rate2_phase = phase ^ 1u;
    if (send > 2 || send < -2 ||
        ((c->delay_feedback || c->delay_tail_hold > BR_FX_TAIL_SAMPLES) &&
         (wet_echo > 2 || wet_echo < -2))) {
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
    if (st->delay_output_xfade_left) {
        uint32_t elapsed = BR_FX_DELAY_XFADE_FRAMES - st->delay_output_xfade_left + 1u;
        wet_echo = st->delay_output_xfade_from +
            (wet_echo - st->delay_output_xfade_from) * (int32_t)elapsed /
            (int32_t)BR_FX_DELAY_XFADE_FRAMES;
        --st->delay_output_xfade_left;
    }
    st->delay_output_last = wet_echo;
    return input + BR_FX_BLEND127(wet_echo, amount, c->delay_amount_gain);
}
#endif

/* Fixed maximum rings retain their history during SIZE changes. Fractional
 * read taps and feedback gains settle over about 32 ms at each wet cadence;
 * the stable-control path needs neither interpolation nor a slew multiply. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) int32_t br_fx_reverb_tap(br_fx_track_t *st,
    uint32_t p, uint32_t line, const br_fx_controls_t *c)
{
    uint32_t target = (uint32_t)c->reverb_size_delay[line] << 8;
    uint32_t current = st->room_delay_q8[line];
    uint32_t length = c->reverb_length[line], pos, index, fraction;
    int32_t a;
    const br_fx_room_sample_t *ring = &br_room_line[p][c->reverb_start[line]];
    if (!current) current = target;
    if (current != target) {
        int32_t delta = (int32_t)target - (int32_t)current;
        int32_t step = br_fx_div_pow2(delta, c->reverb_slew_shift);
        current += step ? step : delta > 0 ? 1 : -1;
        if (current > (length << 8)) current = length << 8;
    }
    st->room_delay_q8[line] = current;
    pos = st->room_pos[line];
    if (pos >= length) pos = st->room_pos[line] = 0u;
    index = pos + length - (current >> 8);
    if (index >= length) index -= length;
    a = br_fx_reverb_decompress16_inline(
        ring[index]);
    fraction = current & 255u;
    if (fraction) {
        int32_t b;
        index = index ? index - 1u : length - 1u;
        b = br_fx_reverb_decompress16_inline(
            ring[index]);
        a = br_fx_reverb_lerp_q8_companded(a, b, fraction);
    }
    return a;
}

/* Unchanged knobs use an integer tap without entering the interpolation
 * leaf or saving its transition registers on every comb traversal. */
static inline __attribute__((always_inline)) int32_t br_fx_reverb_read(
    br_fx_track_t *st, uint32_t p, uint32_t line, const br_fx_controls_t *c,
    uint32_t inline_decode, uint32_t cursor_stable)
{
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE
    if (cursor_stable) {
        uint8_t bit = (uint8_t)(1u << line);
        if (br_fx_reverb_read_cursor_init_mask[p] & bit) {
            if (!st->room_delay_q8[line])
                st->room_delay_q8[line] =
                    (uint32_t)c->reverb_size_delay[line] << 8u;
            if (st->room_pos[line] >= c->reverb_length[line])
                st->room_pos[line] = 0u;
            br_fx_reverb_read_cursor_init_mask[p] &= (uint8_t)~bit;
        }
        br_fx_room_sample_t stored = br_fx_reverb_read_ring[p][line]
            [br_fx_reverb_read_cursor[p][line]];
        if (inline_decode) return br_fx_reverb_decompress16_inline(stored);
        return br_fx_reverb_decompress16_fast(stored);
    }
#else
    (void)cursor_stable;
#endif
    uint32_t delay = c->reverb_size_delay[line];
    if (st->room_delay_q8[line] == (delay << 8)) {
        uint32_t length = c->reverb_length[line];
        uint32_t pos = st->room_pos[line], index;
        if (pos >= length) pos = st->room_pos[line] = 0u;
        index = pos + length - delay;
        if (index >= length) index -= length;
        br_fx_room_sample_t stored = br_room_line[p][c->reverb_start[line] + index];
        if (inline_decode) return br_fx_reverb_decompress16_inline(stored);
        return br_fx_reverb_decompress16_fast(stored);
    }
    return br_fx_reverb_tap(st, p, line, c);
}

static BR_FX_RAM_AUDIO uint32_t br_fx_reverb_loop_gain(br_fx_track_t *st,
    uint32_t line, const br_fx_controls_t *c)
{
    uint32_t target = c->reverb_feedback_line[line];
    uint32_t current = st->room_feedback[line];
    if (!current) current = target;
    if (current != target) {
        int32_t delta = (int32_t)target - (int32_t)current;
        int32_t step = br_fx_div_pow2(delta, c->reverb_slew_shift);
        current += step ? step : delta > 0 ? 1 : -1;
    }
    st->room_feedback[line] = current;
    return current;
}

static BR_FX_RAM_AUDIO int32_t br_fx_reverb(br_fx_track_t *st, uint32_t p, int32_t input,
    const br_fx_controls_t *c, int32_t *left, int32_t *right BR_FX_TAIL_EVENT_PARAM)
{
    uint32_t engine = c->reverb_engine;
    uint32_t wet = c->reverb_wet;
    uint32_t i;
    uint32_t read_cursor_stable = 0u;
    int32_t delayed[4], filtered[4], feedback[4];
    int32_t sum_left = 0, sum_right = 0, send;
    send = br_fx_div127(input * (int32_t)wet);
    if (!wet) {
        for (i = 0; i < 4u; ++i) {
            uint32_t len = c->reverb_length[i];
            if (++st->room_pos[i] >= len) st->room_pos[i] = 0;
        }
        *left = 0;
        *right = 0;
        return 0;
    }
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE
    read_cursor_stable = br_fx_reverb_read_cursor_stable[p];
#endif
    if (engine == BR_REVERB_SPACE) {
        /* Shape the first reflection too, rather than damping only its
         * recirculation. The general pole retains the wide-send bounds. */
        st->reverb_send_lp = br_fx_pole(st->reverb_send_lp, send, c->reverb_alpha);
        send = st->reverb_send_lp;
    }
    /* Gather all four taps and update their one-pole states before feedback
     * writes. The antisymmetric pair rotation below is therefore independent
     * of line iteration order. */
    for (i = 0; i < 4u; ++i) {
        delayed[i] = br_fx_reverb_read(st, p, i, c,
            BR_FX_REVERB_Q2_INLINE_DECODE ? 1u : 0u, read_cursor_stable);
#if BR_FX_REVERB_FLOAT_Q15
        st->room_lp[i] = br_fx_pole_reverb_float(
            st->room_lp[i], delayed[i], c->reverb_alpha_float);
#else
        st->room_lp[i] = br_fx_pole_reverb(st->room_lp[i], delayed[i], c->reverb_alpha);
#endif
        filtered[i] = st->room_lp[i];
        if (i & 1u) sum_right += delayed[i];
        else sum_left += delayed[i];
    }
    /* U=[[0,1],[-1,0]] for lines 0/1 and its opposite orientation for 2/3.
     * This preserves pair energy while ensuring neither side owns the longer
     * feedback pole. */
    feedback[0] = filtered[1];
    feedback[1] = -filtered[0];
    feedback[2] = -filtered[3];
    feedback[3] = filtered[2];
    if (engine == BR_REVERB_SPACE) {
        /* Dense skew-orthogonal rotation. 1/sqrt(2) is folded into cached
         * line gains, so diffusion adds no per-sample multiply. */
        feedback[0] = filtered[2] + filtered[3];
        feedback[1] = filtered[2] - filtered[3];
        feedback[2] = -filtered[0] - filtered[1];
        feedback[3] = filtered[1] - filtered[0];
    }
    for (i = 0; i < 4u; ++i) {
        uint32_t len = c->reverb_length[i];
        uint32_t loop_gain;
#if BR_FX_REVERB_FLOAT_Q15 && BR_FX_GENERIC_FEEDBACK_STABLE_CACHE
        if (c->reverb_feedback_stable) {
            loop_gain = c->reverb_feedback_line[i];
            /* Match the legacy helper's zero-state initialization at the
             * same feedback-write point, without its per-sample slew test. */
            st->room_feedback[i] = loop_gain;
        } else {
            loop_gain = br_fx_reverb_loop_gain(st, i, c);
        }
#elif BR_FX_REVERB_FLOAT_Q15
        loop_gain = br_fx_reverb_loop_gain(st, i, c);
#endif
#if BR_FX_REVERB_FLOAT_Q15
        float coefficient = loop_gain == c->reverb_feedback_line[i]
            ? c->reverb_feedback_line_float[i]
            : (float)loop_gain * (1.0f / 32768.0f);
        int32_t write = send + br_fx_mul_q15_reverb_float(feedback[i], coefficient);
#else
        loop_gain = br_fx_reverb_loop_gain(st, i, c);
        int32_t write = send + br_fx_mul_q15(feedback[i], loop_gain);
#endif
        if (br_fx_abs(write) <= 2) write = 0;
        br_room_line[p][c->reverb_start[i] + st->room_pos[i]] = br_fx_reverb_compress16_fast(write);
#if BR_FX_GENERIC_REVERB_READ_CURSOR_CACHE
        if (read_cursor_stable) {
            uint32_t cursor = (uint32_t)br_fx_reverb_read_cursor[p][i] + 1u;
            if (cursor >= len) cursor = 0u;
            br_fx_reverb_read_cursor[p][i] = (uint16_t)cursor;
        }
#endif
        if (++st->room_pos[i] >= len) st->room_pos[i] = 0;
    }
    *left = br_fx_div_pow2(sum_left, 2u);
    *right = br_fx_div_pow2(sum_right, 2u);
    if (send > 2 || send < -2) {
#if BR_FX_BLOCK_STAGE_PIPELINE
        if (tail_event) *tail_event = 1;
        else if (st->tail_samples < c->reverb_tail_hold)
            st->tail_samples = c->reverb_tail_hold;
#else
        if (st->tail_samples < c->reverb_tail_hold)
            st->tail_samples = c->reverb_tail_hold;
#endif
    }
    return (int32_t)wet;
}

#if BR_FX_HAS_REVERB_RATE2
/* Anti-alias the wet send before processing the selected comb engines at half
 * rate. */
static BR_FX_RAM_AUDIO void br_fx_reverb_rate2(br_fx_track_t *st, uint32_t p,
    int32_t input, const br_fx_controls_t *c, int32_t *left, int32_t *right
    BR_FX_TAIL_EVENT_PARAM)
{
    int32_t current_l = 0, current_r = 0;
    uint32_t phase = st->reverb_rate2_phase;
    if (c->reverb_wet) {
        int32_t bounded_input = br_fx_clip_ram(input, -16000000, 16000000);
#if BR_FX_REVERB_RATE2_MIRROR
        /* Six previous inputs, oldest first from pos, mirrored so no index wraps:
         * two stores per sample instead of shifting six words. Room and Hall never
         * run together, so the half-rate path borrows Hall's ring. */
        int32_t *ring = st->reverb_hall_q4_full_history_mirror;
        uint32_t ring_pos = st->reverb_hall_q4_full_history_pos;
        if (!st->reverb_hall_q4_history_valid) {
            br_fx_clear32((br_fx_clear_word_t *)ring, 12u);
            ring_pos = 0u;
            st->reverb_hall_q4_history_valid = 1u;
        }
#endif
#if BR_FX_REVERB_SEND_EVENT_PRODUCT
        int32_t send_product = bounded_input * (int32_t)c->reverb_wet;
        if (br_fx_reverb_send_event_from_product(send_product)) {
#else
        int32_t full_rate_send = br_fx_div127(
            bounded_input * (int32_t)c->reverb_wet);
        if (full_rate_send > 2 || full_rate_send < -2) {
#endif
#if BR_FX_BLOCK_STAGE_PIPELINE
            if (tail_event) *tail_event = 1;
            else if (st->tail_samples < c->reverb_tail_hold)
                st->tail_samples = c->reverb_tail_hold;
#else
            if (st->tail_samples < c->reverb_tail_hold)
                st->tail_samples = c->reverb_tail_hold;
#endif
        }
        if (phase == 1u) {
#if BR_FX_REVERB_RATE2_MIRROR
            int32_t weighted = bounded_input + ring[ring_pos] + 6 * ring[ring_pos + 1u] +
                15 * ring[ring_pos + 2u] + 20 * ring[ring_pos + 3u] +
                15 * ring[ring_pos + 4u] + 6 * ring[ring_pos + 5u];
#else
            int32_t weighted = bounded_input + 6 * st->reverb_rate2_input_hist[0] +
                15 * st->reverb_rate2_input_hist[1] +
                20 * st->reverb_rate2_input_hist[2] +
                15 * st->reverb_rate2_input_hist[3] +
                6 * st->reverb_rate2_input_hist[4] +
                st->reverb_rate2_input_hist[5];
#endif
            int32_t downsampled = br_fx_div_pow2(weighted, 6u);
            (void)br_fx_reverb(st, p, downsampled, c, &current_l, &current_r
                               BR_FX_TAIL_EVENT_ARG(tail_event));
            *left = br_fx_div_pow2(st->reverb_rate2_previous_l + current_l, 1u);
            *right = br_fx_div_pow2(st->reverb_rate2_previous_r + current_r, 1u);
            st->reverb_rate2_previous_l = current_l;
            st->reverb_rate2_previous_r = current_r;
        } else {
            *left = st->reverb_rate2_previous_l;
            *right = st->reverb_rate2_previous_r;
        }
        {
#if BR_FX_REVERB_RATE2_MIRROR
            ring[ring_pos] = bounded_input;
            ring[ring_pos + 6u] = bounded_input;
            st->reverb_hall_q4_full_history_pos = ring_pos == 5u ? 0u : ring_pos + 1u;
#else
            st->reverb_rate2_input_hist[5] = st->reverb_rate2_input_hist[4];
            st->reverb_rate2_input_hist[4] = st->reverb_rate2_input_hist[3];
            st->reverb_rate2_input_hist[3] = st->reverb_rate2_input_hist[2];
            st->reverb_rate2_input_hist[2] = st->reverb_rate2_input_hist[1];
            st->reverb_rate2_input_hist[1] = st->reverb_rate2_input_hist[0];
            st->reverb_rate2_input_hist[0] = bounded_input;
#endif
        }
    } else {
        /* Clear the delayed filter input so a later enable cannot replay a
         * stale wet event through the half-rate interpolation endpoint. */
        if (phase == 1u)
            (void)br_fx_reverb(st, p, 0, c, &current_l, &current_r
                               BR_FX_TAIL_EVENT_ARG(tail_event));
        {
#if BR_FX_REVERB_RATE2_MIRROR
            st->reverb_hall_q4_full_history_pos = 0u;
            st->reverb_hall_q4_history_valid = 0u;
#endif
            st->reverb_rate2_input_hist[0] = 0;
            st->reverb_rate2_input_hist[1] = 0;
            st->reverb_rate2_input_hist[2] = 0;
            st->reverb_rate2_input_hist[3] = 0;
            st->reverb_rate2_input_hist[4] = 0;
            st->reverb_rate2_input_hist[5] = 0;
        }
        st->reverb_rate2_previous_l = 0;
        st->reverb_rate2_previous_r = 0;
        *left = 0;
        *right = 0;
    }
    st->reverb_rate2_phase = phase ^ 1u;
}
#endif

#if BR_FX_HAS_REVERB_RATE4 && BR_FX_REVERB_RATE4_HALL_HALF_BAND
/* Symmetric 7-tap half-band low-pass: [-1, 0, 9, 16, 9, 0, -1] / 32.
 * Inputs are bounded to +/-16M; the first-stage weighted sum is at most
 * 576M and the second at most 648M, so signed 32-bit math is safe. */
static inline __attribute__((always_inline)) int32_t br_fx_reverb_hall_halfband7(
    int32_t current, const int32_t history[6])
{
    int32_t weighted = -(current + history[5]) +
                       9 * (history[1] + history[3]) + 16 * history[2];
    return br_fx_div_pow2(weighted, 5u);
}
#if BR_FX_HALL_FIR_MIRROR_HISTORY
/* The duplicated six-slot ring keeps the six prior samples contiguous from
 * `next` through `next + 5`: oldest first, newest last. */
static inline __attribute__((always_inline)) int32_t
br_fx_reverb_hall_halfband7_mirror6(int32_t current, const int32_t history[12],
                                    uint32_t next)
{
    int32_t weighted = -(current + history[next]) +
        9 * (history[next + 4u] + history[next + 2u]) +
        16 * history[next + 3u];
    return br_fx_div_pow2(weighted, 5u);
}
#endif
#endif

#if BR_FX_HAS_REVERB_RATE4
/* Quarter-rate selected ROOM/HALL engines independently. Two cascaded 7-tap
 * halfband stages attenuate content above the new Nyquist limit; the chosen
 * engine's recursive lines and damping then advance once per four inputs.
 * Unselected models keep the half-rate path. */
static BR_FX_RAM_AUDIO void br_fx_reverb_rate4(br_fx_track_t *st, uint32_t p,
    int32_t input, const br_fx_controls_t *c, int32_t *left, int32_t *right
    BR_FX_TAIL_EVENT_PARAM)
{
    uint32_t phase = st->reverb_rate4_phase;
    int32_t bounded_input = br_fx_clip_ram(input, -16000000, 16000000);
#if BR_FX_HALL_FIR_MIRROR_HISTORY
    uint32_t full_history_pos = st->reverb_hall_q4_full_history_pos;
    uint32_t half_history_pos = st->reverb_hall_q4_half_history_pos;
#endif
#if BR_FX_REVERB_SEND_EVENT_PRODUCT
    if (c->reverb_wet && br_fx_reverb_send_event_from_product(
            bounded_input * (int32_t)c->reverb_wet)) {
#else
    int32_t full_rate_send = c->reverb_wet ? br_fx_div127(
        bounded_input * (int32_t)c->reverb_wet) : 0;

    if (full_rate_send > 2 || full_rate_send < -2) {
#endif
#if BR_FX_BLOCK_STAGE_PIPELINE
        if (tail_event) *tail_event = 1;
        else if (st->tail_samples < c->reverb_tail_hold)
            st->tail_samples = c->reverb_tail_hold;
#else
        if (st->tail_samples < c->reverb_tail_hold)
            st->tail_samples = c->reverb_tail_hold;
#endif
    }

    if (!c->reverb_wet) {
        if (phase == 2u) {
            int32_t unused_l, unused_r;
            (void)br_fx_reverb(st, p, 0, c, &unused_l, &unused_r
                               BR_FX_TAIL_EVENT_ARG(tail_event));
        }
        br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate2_input_hist, 6u);
        br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate4_input_hist, 6u);
        st->reverb_rate2_previous_l = 0;
        st->reverb_rate2_previous_r = 0;
        st->reverb_rate4_from_l = 0;
        st->reverb_rate4_from_r = 0;
#if BR_FX_HALL_FIR_MIRROR_HISTORY
        st->reverb_hall_q4_full_history_pos = 0u;
        st->reverb_hall_q4_half_history_pos = 0u;
        st->reverb_hall_q4_history_valid = 0u;
        full_history_pos = 0u;
        half_history_pos = 0u;
#endif
        *left = 0;
        *right = 0;
    } else {
#if BR_FX_HALL_FIR_MIRROR_HISTORY
        if (c->reverb_engine == BR_REVERB_HALL &&
            !st->reverb_hall_q4_history_valid) {
            br_fx_clear32((br_fx_clear_word_t *)st->reverb_hall_q4_full_history_mirror,
                          sizeof st->reverb_hall_q4_full_history_mirror / sizeof(uint32_t));
            br_fx_clear32((br_fx_clear_word_t *)st->reverb_hall_q4_half_history_mirror,
                          sizeof st->reverb_hall_q4_half_history_mirror / sizeof(uint32_t));
            full_history_pos = 0u;
            half_history_pos = 0u;
            st->reverb_hall_q4_history_valid = 1u;
        }
#endif
        int32_t first_stage = 0;
        int32_t current_l = st->reverb_rate2_previous_l;
        int32_t current_r = st->reverb_rate2_previous_r;
        uint32_t weight = ((phase + 2u) & 3u) + 1u;

        if ((phase & 1u) == 0u) {
#if BR_FX_REVERB_RATE4_HALL_COMPACT
#if BR_FX_HALL_FIR_MIRROR_HISTORY
            first_stage = br_fx_reverb_hall_halfband7_mirror6(
                bounded_input, st->reverb_hall_q4_full_history_mirror, full_history_pos);
#else
            first_stage = br_fx_reverb_hall_halfband7(
                bounded_input, st->reverb_rate2_input_hist);
#endif
#else
#if BR_FX_REVERB_RATE4_HALL_HALF_BAND
            if (c->reverb_engine == BR_REVERB_HALL) {
#if BR_FX_HALL_FIR_MIRROR_HISTORY
                first_stage = br_fx_reverb_hall_halfband7_mirror6(
                    bounded_input, st->reverb_hall_q4_full_history_mirror, full_history_pos);
#else
                first_stage = br_fx_reverb_hall_halfband7(
                    bounded_input, st->reverb_rate2_input_hist);
#endif
            } else
#endif
            {
                int32_t weighted = bounded_input +
                    6 * st->reverb_rate2_input_hist[0] +
                    15 * st->reverb_rate2_input_hist[1] +
                    20 * st->reverb_rate2_input_hist[2] +
                    15 * st->reverb_rate2_input_hist[3] +
                    6 * st->reverb_rate2_input_hist[4] +
                    st->reverb_rate2_input_hist[5];
                first_stage = br_fx_div_pow2(weighted, 6u);
            }
#endif

            if (phase == 0u) {
#if BR_FX_HALL_FIR_MIRROR_HISTORY
                if (c->reverb_engine == BR_REVERB_HALL) {
                    st->reverb_hall_q4_half_history_mirror[half_history_pos] = first_stage;
                    st->reverb_hall_q4_half_history_mirror[half_history_pos + 6u] = first_stage;
                    half_history_pos = half_history_pos == 5u ? 0u : half_history_pos + 1u;
                } else
#endif
                {
                st->reverb_rate4_input_hist[5] = st->reverb_rate4_input_hist[4];
                st->reverb_rate4_input_hist[4] = st->reverb_rate4_input_hist[3];
                st->reverb_rate4_input_hist[3] = st->reverb_rate4_input_hist[2];
                st->reverb_rate4_input_hist[2] = st->reverb_rate4_input_hist[1];
                st->reverb_rate4_input_hist[1] = st->reverb_rate4_input_hist[0];
                st->reverb_rate4_input_hist[0] = first_stage;
                }
            } else {
                int32_t downsampled;
#if BR_FX_REVERB_RATE4_HALL_COMPACT
#if BR_FX_HALL_FIR_MIRROR_HISTORY
                downsampled = br_fx_reverb_hall_halfband7_mirror6(
                    first_stage, st->reverb_hall_q4_half_history_mirror, half_history_pos);
#else
                downsampled = br_fx_reverb_hall_halfband7(
                    first_stage, st->reverb_rate4_input_hist);
#endif
#else
#if BR_FX_REVERB_RATE4_HALL_HALF_BAND
                if (c->reverb_engine == BR_REVERB_HALL) {
#if BR_FX_HALL_FIR_MIRROR_HISTORY
                    downsampled = br_fx_reverb_hall_halfband7_mirror6(
                        first_stage, st->reverb_hall_q4_half_history_mirror, half_history_pos);
#else
                    downsampled = br_fx_reverb_hall_halfband7(
                        first_stage, st->reverb_rate4_input_hist);
#endif
                } else
#endif
                {
                    int32_t weighted2 = first_stage +
                        6 * st->reverb_rate4_input_hist[0] +
                        15 * st->reverb_rate4_input_hist[1] +
                        20 * st->reverb_rate4_input_hist[2] +
                        15 * st->reverb_rate4_input_hist[3] +
                        6 * st->reverb_rate4_input_hist[4] +
                        st->reverb_rate4_input_hist[5];
                    downsampled = br_fx_div_pow2(weighted2, 6u);
                }
#endif
                st->reverb_rate4_from_l = current_l;
                st->reverb_rate4_from_r = current_r;
                /* The two half-band stages can overshoot the bounded input.
                 * Keep the downstream signed send multiply inside its contract. */
                downsampled = br_fx_clip_ram(downsampled, -16000000, 16000000);
                (void)br_fx_reverb(st, p, downsampled, c, &current_l, &current_r
                                   BR_FX_TAIL_EVENT_ARG(tail_event));
                st->reverb_rate2_previous_l = current_l;
                st->reverb_rate2_previous_r = current_r;
#if BR_FX_HALL_FIR_MIRROR_HISTORY
                if (c->reverb_engine == BR_REVERB_HALL) {
                    st->reverb_hall_q4_half_history_mirror[half_history_pos] = first_stage;
                    st->reverb_hall_q4_half_history_mirror[half_history_pos + 6u] = first_stage;
                    half_history_pos = half_history_pos == 5u ? 0u : half_history_pos + 1u;
                } else
#endif
                {
                st->reverb_rate4_input_hist[5] = st->reverb_rate4_input_hist[4];
                st->reverb_rate4_input_hist[4] = st->reverb_rate4_input_hist[3];
                st->reverb_rate4_input_hist[3] = st->reverb_rate4_input_hist[2];
                st->reverb_rate4_input_hist[2] = st->reverb_rate4_input_hist[1];
                st->reverb_rate4_input_hist[1] = st->reverb_rate4_input_hist[0];
                st->reverb_rate4_input_hist[0] = first_stage;
                }
            }
        }

        *left = st->reverb_rate4_from_l + br_fx_div_pow2(
            (current_l - st->reverb_rate4_from_l) * (int32_t)weight, 2u);
        *right = st->reverb_rate4_from_r + br_fx_div_pow2(
            (current_r - st->reverb_rate4_from_r) * (int32_t)weight, 2u);
#if BR_FX_HALL_FIR_MIRROR_HISTORY
        if (c->reverb_engine == BR_REVERB_HALL) {
            st->reverb_hall_q4_full_history_mirror[full_history_pos] = bounded_input;
            st->reverb_hall_q4_full_history_mirror[full_history_pos + 6u] = bounded_input;
            full_history_pos = full_history_pos == 5u ? 0u : full_history_pos + 1u;
        } else
#endif
        {
            st->reverb_rate2_input_hist[5] = st->reverb_rate2_input_hist[4];
            st->reverb_rate2_input_hist[4] = st->reverb_rate2_input_hist[3];
            st->reverb_rate2_input_hist[3] = st->reverb_rate2_input_hist[2];
            st->reverb_rate2_input_hist[2] = st->reverb_rate2_input_hist[1];
            st->reverb_rate2_input_hist[1] = st->reverb_rate2_input_hist[0];
            st->reverb_rate2_input_hist[0] = bounded_input;
        }
    }
#if BR_FX_REVERB_RATE4_HALL_HALF_BAND_MAKEUP_73_64
#if BR_FX_REVERB_RATE4_HALL_COMPACT
    /* The guarded compact selector makes Hall the only possible Q4 caller. */
    *left = br_fx_div_pow2(*left * 73, 6u);
    *right = br_fx_div_pow2(*right * 73, 6u);
#else
    if (c->reverb_engine == BR_REVERB_HALL) {
        /* Small wet-return-only lift for half-band Q4: 73/64 ~= +1.17 dB.
         * It does not alter excitation, feedback, tails, or dry level. The
         * Hall return is <16384 per channel, so the product fits int32. */
        *left = br_fx_div_pow2(*left * 73, 6u);
        *right = br_fx_div_pow2(*right * 73, 6u);
    }
#endif
#endif
    st->reverb_rate4_phase = (phase + 1u) & 3u;
#if BR_FX_HALL_FIR_MIRROR_HISTORY
    st->reverb_hall_q4_full_history_pos = full_history_pos;
    st->reverb_hall_q4_half_history_pos = half_history_pos;
#endif
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
            br_mod_line[p][mod_pos] = br_fx_clip16_ram(filtered_input);
            sample_a = br_mod_line[p][read_pos];
            sample_b = br_mod_line[p][(read_pos - 1u) &
                (BR_FX_MOD_SAMPLES - 1u)];
            wet = br_fx_interp_q8_i16(sample_a, sample_b, frac);
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
            for (j = 0u; j < 4u; ++j) {
                int32_t y = st->phase_state[j] -
                    br_fx_mul_q15_phaser_selected(wet, (uint32_t)a);
                st->phase_state[j] = br_fx_clip_ram(
                    wet + br_fx_mul_q15_phaser_selected(y, (uint32_t)a), -65535, 65535);
                wet = y;
            }
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
#if BR_FX_DELAY_RATE2
/* The block path hoists engine/control dispatch while retaining the same
 * decimation, wow read, feedback write, and wet interpolation at each rate.
 * TAPE reads inline; the other engines share the half-rate step. */
static BR_FX_RAM_AUDIO void br_fx_stage_delay_rate2_tape_block(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core, uint32_t lfo_start,
    uint32_t lfo_inc, uint32_t lfo_final, const int32_t *input, uint32_t n,
    const br_fx_controls_t *c, int32_t (*stereo)[2])
{
#if defined(BR_FX_TEST_LONG_TAPE_BLOCK)
    ++br_fx_test_long_tape_block_calls;
#endif
    uint32_t i;
    uint32_t amount = c->delay_amount;
    uint32_t feedback = c->delay_feedback;
    uint32_t time = c->delay_time;
    uint32_t storage_shift = c->delay_storage_shift;
    uint32_t long_period = 2u << storage_shift;
    uint32_t phase = lfo_start;
    uint32_t delay_pos = st->delay_pos;
    uint32_t delay_valid = st->delay_valid_samples;
    uint32_t history_pos = st->delay_rate2_history_pos;
    uint32_t rate2_phase = st->delay_rate2_phase;
    int32_t history[8];
    int32_t previous_echo = st->delay_rate2_previous_echo;
    int32_t delay_lp = st->delay_lp;
    int32_t last_echo = 0;
    for (i = 0u; i < 8u; ++i)
        history[i] = st->delay_rate2_send_history[i];

    for (i = 0u; i < n; ++i) {
        int32_t sample = input[i];
        int32_t send = amount ? br_fx_div127(sample * (int32_t)amount) : 0;
        int32_t wet_echo = previous_echo;
        uint32_t long_write = 0u;
        phase += lfo_inc;
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_DELAY);
        history[history_pos] = send;
        if (rate2_phase == 1u) {
            uint32_t d1 = (history_pos - 1u) & 7u;
            uint32_t d2 = (history_pos - 2u) & 7u;
            uint32_t d3 = (history_pos - 3u) & 7u;
            uint32_t d4 = (history_pos - 4u) & 7u;
            uint32_t d5 = (history_pos - 5u) & 7u;
            uint32_t d6 = (history_pos - 6u) & 7u;
            int32_t weighted = history[history_pos] +
                6 * history[d1] + 15 * history[d2] + 20 * history[d3] +
                15 * history[d4] + 6 * history[d5] + history[d6];
            int32_t downsampled_send = br_fx_div_pow2(weighted, 6u);
            if (storage_shift) {
                int32_t long_send;
                if (br_fx_delay_long_decimate(st, downsampled_send,
                                               storage_shift, &long_send)) {
                    int32_t echo;
                    st->delay_pos = delay_pos;
                    st->delay_valid_samples = delay_valid;
                    st->delay_lp = delay_lp;
                    st->lfo_phase = phase;
                    echo = br_fx_delay_rate2_step(st, p, long_send, c);
                    delay_pos = st->delay_pos;
                    delay_valid = st->delay_valid_samples;
                    delay_lp = st->delay_lp;
                    st->delay_long_previous_echo = st->delay_long_echo;
                    st->delay_long_echo = echo;
                    st->delay_long_interp_phase = 1u;
                    long_write = 1u;
                }
            } else if (c->delay_engine != BR_DELAY_TAPE) {
                /* Digital, multitap and glitch share the half-rate step; only the
                 * per-sample call, edge checks and history indexing are hoisted. */
                int32_t echo;
                st->delay_pos = delay_pos;
                st->delay_valid_samples = delay_valid;
                echo = br_fx_delay_rate2_step(st, p, downsampled_send, c);
                delay_pos = st->delay_pos;
                delay_valid = st->delay_valid_samples;
                wet_echo = br_fx_div_pow2(previous_echo + echo, 1u);
                previous_echo = echo;
            } else {
                int32_t wow_time_q8 = (int32_t)(time * 256u) +
                    br_fx_tape_wow_q8(phase);
                int32_t echo, write;
                if (wow_time_q8 < 256) wow_time_q8 = 256;
                echo = br_fx_delay_rate2_read(p, delay_pos,
                    (uint32_t)wow_time_q8 >> 1, delay_valid);
                echo = br_fx_delay_softclip(echo);
                delay_lp = br_fx_pole_bounded(delay_lp, echo, br_fx_tape_alpha_rate2());
                echo = br_fx_clip_ram(delay_lp, -32767, 32767);
                write = downsampled_send +
                    br_fx_delay_feedback_q15(echo, feedback);
                if (br_fx_abs(write) <= 2) write = 0;
                br_delay_line[p][delay_pos] = br_fx_reverb_compress16_fast(write);
                delay_pos = (delay_pos + 1u) & (BR_FX_DELAY_SAMPLES - 1u);
                if (delay_valid < BR_FX_DELAY_SAMPLES) ++delay_valid;
                wet_echo = br_fx_div_pow2(previous_echo + echo, 1u);
                previous_echo = echo;
            }
        }
        if (storage_shift) {
            uint32_t interp = st->delay_long_interp_phase;
            if (!long_write && interp < long_period) ++interp;
            if (interp > long_period) interp = long_period;
            st->delay_long_interp_phase = interp;
            wet_echo = st->delay_long_previous_echo +
                (st->delay_long_echo - st->delay_long_previous_echo) *
                (int32_t)interp / (int32_t)long_period;
        }
        stereo[i][0] = sample +
            BR_FX_BLEND127(wet_echo, amount, c->delay_amount_gain);
        stereo[i][1] = send > 2 || send < -2 ||
            ((feedback || c->delay_tail_hold > BR_FX_TAIL_SAMPLES) &&
             (wet_echo > 2 || wet_echo < -2));
        last_echo = wet_echo;
        history_pos = (history_pos + 1u) & 7u;
        rate2_phase ^= 1u;
    }
    if (n) st->delay_output_last = last_echo;

    st->lfo_phase = lfo_final;
    st->delay_pos = delay_pos;
    st->delay_valid_samples = delay_valid;
    st->delay_rate2_history_pos = history_pos;
    st->delay_rate2_phase = rate2_phase;
    st->delay_rate2_previous_echo = previous_echo;
    st->delay_lp = delay_lp;
    for (i = 0u; i < 8u; ++i)
        st->delay_rate2_send_history[i] = history[i];
}
#endif

static inline __attribute__((always_inline)) int32_t br_fx_stage_delay_finish(
    br_fx_track_t *st, uint32_t p, int32_t input, int32_t echo,
    int32_t send, const br_fx_controls_t *c, uint32_t feedback,
    int32_t *tail_event)
{
    uint32_t amount = c->delay_amount;
    int32_t write = send + br_fx_delay_feedback_q15(echo, feedback);
    if (br_fx_abs(write) <= 2) write = 0;
    br_delay_line[p][st->delay_pos] = br_fx_reverb_compress16_fast(write);
    st->delay_pos = (st->delay_pos + 1u) & (BR_FX_DELAY_SAMPLES - 1u);
    if (send > 2 || send < -2 ||
        ((feedback || c->delay_tail_hold > BR_FX_TAIL_SAMPLES) &&
         (echo > 2 || echo < -2))) *tail_event = 1;
    return input + BR_FX_BLEND127(echo, amount, c->delay_amount_gain);
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
    if (!amount && !st->delay_output_xfade_left) {
        /* Send 0: the line was cleared on the falling edge and every write since is
         * zero, so the stage adds nothing. Keep what a later enable reads. */
        if (c->delay_storage_shift != st->delay_storage_shift)
            br_fx_delay_storage_reset(st, c->delay_storage_shift);
        st->delay_last_time = c->delay_time;
        st->delay_rate2_history_pos = (st->delay_rate2_history_pos + n) & 7u;
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
    if ((BR_FX_DELAY_BLOCK_ALL_ENGINES || engine == BR_DELAY_TAPE) &&
        c->delay_storage_shift == st->delay_storage_shift &&
        c->delay_time == st->delay_last_time &&
        !st->delay_output_xfade_left) {
        br_fx_stage_delay_rate2_tape_block(st, p, sample_base, progress_core, lfo_start,
            lfo_inc, lfo_final, input, n, c, stereo);
    } else {
        uint32_t phase = lfo_start;
        for (i = 0u; i < n; ++i) {
            int32_t tail_event = 0;
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_DELAY);
            stereo[i][0] = br_fx_delay(st, p, input[i], c
                BR_FX_TAIL_EVENT_ARG(&tail_event));
            stereo[i][1] = tail_event;
        }
        st->lfo_phase = lfo_final;
    }
    return;
#endif
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
            echo = br_fx_delay_softclip(br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]));
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
                weighted_sum += (int32_t)br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]) * (int32_t)(5u - tap);
#else
                sum += br_fx_div10((int32_t)br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]) * (int32_t)(5u - tap));
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
            echo = br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]);
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
            echo = br_fx_reverb_decompress16_fast(br_delay_line[p][read_pos]);
            stereo[i][1] = 0;
            stereo[i][0] = br_fx_stage_delay_finish(st, p, input[i], echo, send,
                c, feedback, &stereo[i][1]);
        }
    }
    st->lfo_phase = lfo_final;
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
#if BR_FX_HAS_REVERB_RATE && !BR_FX_GLOBAL_REVERB
    {
        uint32_t ramp_frames = br_fx_reverb_ramp_frames();
        if (st->reverb_rate2_ramp < ramp_frames) {
#if BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K
            rv_l = (rv_l * (int32_t)st->reverb_rate2_ramp) / (int32_t)ramp_frames;
            rv_r = (rv_r * (int32_t)st->reverb_rate2_ramp) / (int32_t)ramp_frames;
#else
            rv_l = br_fx_div_pow2(rv_l * (int32_t)st->reverb_rate2_ramp, 5u);
            rv_r = br_fx_div_pow2(rv_r * (int32_t)st->reverb_rate2_ramp, 5u);
#endif
            ++st->reverb_rate2_ramp;
        }
    }
#endif
    if (cfg->mod_mix && (input > 2 || input < -2)) {
        uint32_t short_tail = FS / 2u;
        if (*tail_samples < short_tail) *tail_samples = short_tail;
    }
    if (delay_event && *tail_samples < c->delay_tail_hold)
        *tail_samples = c->delay_tail_hold;
    if (reverb_event && *tail_samples < c->reverb_tail_hold)
        *tail_samples = c->reverb_tail_hold;
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
#if !BR_FX_GLOBAL_REVERB
    if (s->reverb) br_fx_reverb_output_diffuse(st, c, &rv_l, &rv_r);
#endif
    if (*tail_samples) --*tail_samples;
    st->tail_samples = *tail_samples;
    st->previous_delay_wet = s->delay;
    st->previous_reverb_wet = s->reverb;
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

#if !BR_FX_GLOBAL_REVERB
#if BR_FX_HAS_REVERB_RATE4 && BR_FX_HAS_REVERB_RATE2 && \
    BR_FX_BLOCK_STAGE_ONLY_BUILD && BR_FX_BLOCK_STAGE_PIPELINE && \
    BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB && BR_FX_STAGE_REVERB_RATE_DISPATCH
/* Cold zero-wet bookkeeping stays in XIP; the staged frame loop remains RAM-resident. */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void
br_fx_reverb_rate4_zero_block(br_fx_track_t *st, const br_fx_controls_t *c, uint32_t n)
{
    uint32_t line;
    uint32_t phase0 = st->reverb_rate4_phase;
    uint32_t first = (2u - phase0) & 3u;
    uint32_t advances = first < n ? 1u + ((n - 1u - first) >> 2u) : 0u;
    for (line = 0u; line < 4u; ++line) {
        uint32_t pos = st->room_pos[line];
        uint32_t len = c->reverb_length[line];
        if (advances) {
            /* Q4 room lines are at least ceil(521 / 4) = 131 cells;
             * no block can wrap them more than once. Preserve invalid-pos behavior. */
            if (pos >= len) pos = advances - 1u;
            else pos += advances;
            if (pos >= len) pos -= len;
            st->room_pos[line] = pos;
        }
    }
    st->reverb_rate4_phase = (phase0 + n) & 3u;
    br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate2_input_hist, 6u);
    br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate4_input_hist, 6u);
    st->reverb_rate2_previous_l = 0;
    st->reverb_rate2_previous_r = 0;
    st->reverb_rate4_from_l = 0;
    st->reverb_rate4_from_r = 0;
#if BR_FX_HALL_FIR_MIRROR_HISTORY
    st->reverb_hall_q4_full_history_pos = 0u;
    st->reverb_hall_q4_half_history_pos = 0u;
    st->reverb_hall_q4_history_valid = 0u;
#endif
}
#endif

#if BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB

static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_fx_stage_reverb_block(
    br_fx_track_t *st, uint32_t p, uint32_t sample_base, uint32_t progress_core, uint32_t pan,
    uint32_t lfo_start, uint32_t lfo_inc, uint32_t lfo_final,
    const int32_t *input, const int32_t *mod, uint32_t n,
    const br_sound_t *s, const br_fx_config_t *cfg, const br_fx_controls_t *c,
    uint32_t *tail_samples, int32_t (*stereo)[2])
{
    uint32_t i;
    uint32_t wet = c->reverb_wet;
    uint32_t phase = lfo_start;

    /* Q2 and Q4 have mutually exclusive per-track selections. Share one
     * frame loop and output emitter so enabling both selected rates does not
     * duplicate the large tail/mix/pan path in RAM. The mode is block-stable;
     * the predictable branch chooses only which recursive kernel runs. */
#if BR_FX_HAS_REVERB_RATE4 && BR_FX_HAS_REVERB_RATE2
#if BR_FX_STAGE_REVERB_RATE_DISPATCH
    if (c->reverb_rate4) {
#if BR_FX_BLOCK_STAGE_ONLY_BUILD && BR_FX_BLOCK_STAGE_PIPELINE && \
    BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB && BR_FX_STAGE_REVERB_RATE_DISPATCH
        if (!wet && n) {
            BR_FX_PREPARE_INVOKE(br_fx_reverb_rate4_zero_block, st, c, n);
        }
#endif
        for (i = 0u; i < n; ++i) {
            int32_t rv_l = 0, rv_r = 0, reverb_event = 0;
            int32_t delayed = stereo[i][0];
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_REVERB);
#if BR_FX_BLOCK_STAGE_ONLY_BUILD && BR_FX_BLOCK_STAGE_PIPELINE && \
    BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB && BR_FX_STAGE_REVERB_RATE_DISPATCH
            if (wet)
#endif
                br_fx_reverb_rate4(st, p, delayed, c, &rv_l, &rv_r
                                   BR_FX_TAIL_EVENT_ARG(&reverb_event));
            br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
                input[i], mod[i], delayed, rv_l, rv_r, stereo[i][1],
                reverb_event, tail_samples);
        }
        st->lfo_phase = lfo_final;
        return;
    }
    if (c->reverb_rate2) {
        if (!wet && n) {
            uint32_t line;
            uint32_t phase0 = st->reverb_rate2_phase;
            uint32_t advances = (n + phase0) >> 1u;
            for (line = 0u; line < 4u; ++line) {
                uint32_t pos = st->room_pos[line] + advances;
                uint32_t len = c->reverb_length[line];
                if (pos >= len) pos -= len;
                st->room_pos[line] = pos;
            }
            st->reverb_rate2_phase = (phase0 + n) & 1u;
            {
                br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate2_input_hist,
                    sizeof st->reverb_rate2_input_hist / sizeof(uint32_t));
            }
            st->reverb_rate2_previous_l = 0;
            st->reverb_rate2_previous_r = 0;
        }
        for (i = 0u; i < n; ++i) {
            int32_t rv_l = 0, rv_r = 0, reverb_event = 0;
            int32_t delayed = stereo[i][0];
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_REVERB);
            if (wet)
                br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                                   BR_FX_TAIL_EVENT_ARG(&reverb_event));
            br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
                input[i], mod[i], delayed, rv_l, rv_r, stereo[i][1],
                reverb_event, tail_samples);
        }
        st->lfo_phase = lfo_final;
        return;
    }
#else
    if (c->reverb_rate4 || c->reverb_rate2) {
        uint32_t use_rate4 = c->reverb_rate4;
        for (i = 0u; i < n; ++i) {
            int32_t rv_l = 0, rv_r = 0, reverb_event = 0;
            int32_t delayed = stereo[i][0];
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_REVERB);
            if (use_rate4)
                br_fx_reverb_rate4(st, p, delayed, c, &rv_l, &rv_r
                                   BR_FX_TAIL_EVENT_ARG(&reverb_event));
            else
                br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                                   BR_FX_TAIL_EVENT_ARG(&reverb_event));
            br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
                input[i], mod[i], delayed, rv_l, rv_r, stereo[i][1],
                reverb_event, tail_samples);
        }
        st->lfo_phase = lfo_final;
        return;
    }
#endif
#elif BR_FX_HAS_REVERB_RATE4
    if (c->reverb_rate4) {
        for (i = 0u; i < n; ++i) {
            int32_t rv_l = 0, rv_r = 0, reverb_event = 0;
            int32_t delayed = stereo[i][0];
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_REVERB);
            br_fx_reverb_rate4(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
            br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
                input[i], mod[i], delayed, rv_l, rv_r, stereo[i][1],
                reverb_event, tail_samples);
        }
        st->lfo_phase = lfo_final;
        return;
    }
#elif BR_FX_HAS_REVERB_RATE2
    if (c->reverb_rate2) {
        for (i = 0u; i < n; ++i) {
            int32_t rv_l = 0, rv_r = 0, reverb_event = 0;
            int32_t delayed = stereo[i][0];
            phase += lfo_inc;
            st->lfo_phase = phase;
            br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                                 BR_FX_PROGRESS_REVERB);
            br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
            br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
                input[i], mod[i], delayed, rv_l, rv_r, stereo[i][1],
                reverb_event, tail_samples);
        }
        st->lfo_phase = lfo_final;
        return;
    }
#endif

    st->lfo_phase = lfo_final;
}
#endif
#endif /* !BR_FX_GLOBAL_REVERB */
#if BR_FX_GLOBAL_REVERB
#if !BR_FX_STAGE_EMIT_TAIL_BOUNDARY || !BR_FX_HAS_REVERB_RATE2
#error "the shared reverb is written for the tail-boundary emit and the rate-2 kernel"
#endif
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
    for (i = 0u; i < n; ++i) {
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
        if (pan != 64u)
            br_fx_pan_stereo_cached_noncenter(centered, centered, pan, pan_l, pan_r,
                                              &left, &right);
        stereo[i][0] = left;
        stereo[i][1] = right;
    }
    if (touched) st->tail_storage = 1u;
    st->tail_samples = tail;
    *tail_samples = tail;
    st->previous_delay_wet = s->delay;
    st->previous_reverb_wet = s->reverb;
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

/* Cold, once per block: derive the shared reverb's controls from track 1's
 * type, decay and size. The three values only matter when they change. */
static BR_FX_PREPARE_CODE __attribute__((minsize, noinline)) void br_gr_prepare(void)
{
    const br_sound_t *s0 = br_fx_sound_at(0u);
    const br_fx_config_t *cfg0 = br_fx_config_at(0u);
    br_fx_controls_t *c = &br_gr_controls;
    br_fx_track_t *st = &br_gr_state;
    uint32_t engine = br_fx_reverb_engine_of(cfg0->reverb_engine);
    uint32_t decay = br_fx_bound_ram(cfg0->reverb_tone, 127u);
    uint32_t space = br_fx_bound_ram(s0->space, 127u);
    uint32_t i, engine_changed, refresh;
    float output_target = engine == BR_REVERB_SPACE ? 18.0f : 32.0f;
    if (br_gr_key_valid && br_gr_key[0] == engine && br_gr_key[1] == decay &&
        br_gr_key[2] == space) return;
    br_gr_key[0] = engine; br_gr_key[1] = decay; br_gr_key[2] = space; br_gr_key_valid = 1u;
    engine_changed = c->reverb_engine != engine;
    refresh = !c->reverb_feedback_line[3] || engine_changed || c->reverb_space != space ||
        c->reverb_feedback != 14000u + 130u * decay;
    if (!(st->reverb_output_scale > 0.0f)) st->reverb_output_scale = output_target;
    c->reverb_output_scale = output_target;
    st->reverb_output_unity = st->reverb_output_scale == 32.0f && output_target == 32.0f;
    if (engine_changed) {
        br_fx_clear32((br_fx_clear_word_t *)st->room_feedback, 4u);
        st->reverb_send_lp = 0;
        st->reverb_hall_q4_history_valid = 0u;   /* Room and Hall share the input ring */
        if (engine == BR_REVERB_SPACE || c->reverb_engine == BR_REVERB_SPACE) {
            br_sp_clear();                       /* the two kinds keep separate memories */
            memset(br_room_line[0], 0, sizeof br_room_line[0]);
        }
    }
    c->reverb_engine = engine;
    /* The engines return -6 to -18 dB below the dry signal at full send; bring each
     * to about the dry level so the send knob has a useful range. */
    {
        static const uint16_t makeup[BR_REVERB_COUNT] = {1536u, 614u, 768u, 140u, 819u};
        c->reverb_return_q8 = makeup[engine];
    }
    c->reverb_wet = 127u;
    c->reverb_tone = 86u - (62u * space) / 127u;
    c->reverb_space = space;
    c->reverb_feedback = 14000u + 130u * decay;
    c->reverb_tail_hold = FS * (2u + decay / 8u);
    c->reverb_alpha = br_fx_reverb_alpha(c->reverb_tone);
    {
#if BR_FX_REVERB_PACK_ALL_RATE2_ROOM
        uint32_t packed_room_start = 0u;
#endif
        for (i = 0u; i < 4u; ++i) {
#if BR_FX_REVERB_PACK_ALL_RATE2_ROOM
            uint32_t packed_max = br_fx_rate_samples(br_room_length[BR_REVERB_SPACE][i]);
            c->reverb_start[i] = (uint16_t)packed_room_start;
            packed_room_start += (packed_max + 1u) >> 1;
#else
            c->reverb_start[i] = (uint16_t)br_fx_rate_samples(br_room_start[i]);
#endif
            c->reverb_length[i] = (uint16_t)br_fx_rate_samples(br_room_length[engine][i]);
        }
    }
    c->reverb_rate2 = BR_FX_PREPARE_INVOKE(br_fx_reverb_rate2_selected, engine);
#if BR_FX_HAS_REVERB_RATE4
    c->reverb_rate4 = br_fx_reverb_rate4_selected(engine);
#else
    c->reverb_rate4 = 0u;
#endif
    if (c->reverb_rate4) {
        uint32_t q = 32768u - c->reverb_alpha;
        uint32_t q2 = (q * q + 16384u) >> 15;
        uint32_t q4 = (q2 * q2 + 16384u) >> 15;
        c->reverb_alpha = 32768u - q4;
        for (i = 0u; i < 4u; ++i)
            c->reverb_length[i] = (uint16_t)((c->reverb_length[i] + 3u) >> 2);
    } else if (c->reverb_rate2) {
        c->reverb_alpha = 2u * c->reverb_alpha - ((c->reverb_alpha * c->reverb_alpha) >> 15);
        for (i = 0u; i < 4u; ++i)
            c->reverb_length[i] = (uint16_t)((c->reverb_length[i] + 1u) >> 1);
    }
    c->reverb_slew_shift = c->reverb_rate4 ? 8u : c->reverb_rate2 ? 9u : 10u;
    for (i = 0u; i < 4u; ++i)
        c->reverb_size_delay[i] = (uint16_t)((c->reverb_length[i] * (127u + space) + 127u) / 254u);
    if (refresh) {
        uint32_t reference_length = 0u;
        for (i = 0u; i < 4u; ++i)
            if (c->reverb_length[i] > reference_length) reference_length = c->reverb_length[i];
        for (i = 0u; i < 4u; ++i)
            c->reverb_feedback_line[i] = (uint16_t)br_fx_reverb_feedback_for_length(
                c->reverb_feedback, c->reverb_size_delay[i], reference_length);
        if (engine == BR_REVERB_SPACE)
            for (i = 0u; i < 4u; ++i)
                c->reverb_feedback_line[i] = (uint16_t)
                    ((c->reverb_feedback_line[i] * 23170u + 16384u) >> 15);
    }
#if BR_FX_REVERB_FLOAT_Q15
    c->reverb_alpha_float = (float)c->reverb_alpha * (1.0f / 32768.0f);
    for (i = 0u; i < 4u; ++i)
        c->reverb_feedback_line_float[i] = (float)c->reverb_feedback_line[i] * (1.0f / 32768.0f);
#endif
    c->reverb_output_ap0_base = br_reverb_output_ap0[0];
    c->reverb_output_ap1_base = br_reverb_output_ap1[0];
    {   /* a change of rate or engine restarts the filter histories, as it did per track */
        uint32_t rate4 = c->reverb_rate4, previous4 = 0u;
#if BR_FX_HAS_REVERB_RATE4
        previous4 = st->previous_reverb_rate4;
#endif
        if (st->previous_reverb_rate2 != c->reverb_rate2 || previous4 != rate4 ||
            (st->previous_reverb_engine != engine &&
             (st->previous_reverb_rate2 || c->reverb_rate2 || previous4 || rate4))) {
            for (i = 0u; i < 4u; ++i)
                if (c->reverb_length[i]) st->room_pos[i] %= c->reverb_length[i];
            st->reverb_rate2_phase = 0u; st->reverb_rate2_ramp = 0u;
            st->reverb_rate2_previous_l = st->reverb_rate2_previous_r = 0;
            br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate2_input_hist,
                          sizeof st->reverb_rate2_input_hist / sizeof(uint32_t));
#if BR_FX_HAS_REVERB_RATE4
            st->reverb_rate4_phase = 0u;
            st->reverb_rate4_from_l = st->reverb_rate4_from_r = 0;
            br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate4_input_hist,
                          sizeof st->reverb_rate4_input_hist / sizeof(uint32_t));
#endif
#if BR_FX_HALL_FIR_MIRROR_HISTORY
            st->reverb_hall_q4_full_history_pos = st->reverb_hall_q4_half_history_pos = 0u;
            st->reverb_hall_q4_history_valid = 0u;
#endif
        }
        st->previous_reverb_rate2 = c->reverb_rate2;
        st->previous_reverb_engine = engine;
#if BR_FX_HAS_REVERB_RATE4
        st->previous_reverb_rate4 = rate4;
#endif
    }
}

/* Empty the lines once the tail has run out, so the next phrase starts clean. */
static BR_FX_RAM_AUDIO void br_gr_clear(void)
{
    br_fx_track_t *st = &br_gr_state;
    br_fx_clear32((br_fx_clear_word_t *)br_room_line[0], sizeof br_room_line[0] / 4u);
    br_fx_clear32((br_fx_clear_word_t *)st->room_lp, sizeof st->room_lp / sizeof(uint32_t));
    br_fx_reverb_output_ap_reset(0u, st);
    st->reverb_rate2_phase = 0u; st->reverb_rate2_ramp = 0u;
    st->reverb_rate2_previous_l = st->reverb_rate2_previous_r = 0;
    br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate2_input_hist,
                  sizeof st->reverb_rate2_input_hist / sizeof(uint32_t));
#if BR_FX_HAS_REVERB_RATE4
    st->reverb_rate4_phase = 0u;
    st->reverb_rate4_from_l = st->reverb_rate4_from_r = 0;
    br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate4_input_hist,
                  sizeof st->reverb_rate4_input_hist / sizeof(uint32_t));
#endif
#if BR_FX_HALL_FIR_MIRROR_HISTORY
    st->reverb_hall_q4_full_history_pos = st->reverb_hall_q4_half_history_pos = 0u;
    st->reverb_hall_q4_history_valid = 0u;
#endif
    st->tail_storage = 0u;
    br_fx_clear32((br_fx_clear_word_t *)br_gr_ap0, BR_GR_AP0_LEN);
    br_fx_clear32((br_fx_clear_word_t *)br_gr_ap1, BR_GR_AP1_LEN);
    br_fx_clear32((br_fx_clear_word_t *)br_gr_ap2, BR_GR_AP2_LEN);
    br_fx_clear32((br_fx_clear_word_t *)br_gr_ap3, BR_GR_AP3_LEN);
    br_gr_ap_pos[0] = br_gr_ap_pos[1] = br_gr_ap_pos[2] = br_gr_ap_pos[3] = 0u;
    BR_FX_PREPARE_INVOKE(br_sp_clear);
}

/* Once per block, before any job runs: swap the send buffers and decide whether
 * the reverb has the previous block's sends, or a tail, to work on. */
static BR_FX_PREPARE_CODE void br_gr_plan(void)
{
    uint32_t p, prev = br_gr_cur, present = 0u;
    for (p = 0u; p < BR_PARTS; ++p) present |= br_gr_have[prev][p];
    br_gr_cur = prev ^ 1u;
    for (p = 0u; p < BR_PARTS; ++p) br_gr_have[br_gr_cur][p] = 0u;
    br_gr_run = present || br_gr_state.tail_samples;
}

/* ---- DRIVE: a guitar-pedal style stage ahead of the track's modulation and delay.
 * Three shapers (overdrive: smooth cubic saturation; distortion: a hard knee that
 * flattens at about 0.8 of full scale; fuzz: very high gain into an asymmetric
 * squarish clip), a tone low-pass after the shaper and a dry/wet mix (the SEND).
 * Stored in the existing fields: mode = drive_mode, gain = drive_gain, tone =
 * drive_tone, send = sound.drive. It runs from flash, once per block, and only for
 * a track whose send is above zero. */
#define BR_DRIVE_FS 32768
static const uint32_t br_drive_gain_q8[3][17] = {
    {256, 296, 341, 394, 455, 526, 607, 701, 810, 935, 1080, 1247, 1440, 1662, 1920, 2217, 2560},
    {1024, 1213, 1437, 1701, 2015, 2387, 2827, 3348, 3966, 4697, 5564, 6590, 7805, 9244, 10949, 12968, 15360},
    {5120, 6064, 7183, 8507, 10076, 11934, 14135, 16742, 19830, 23487, 27818, 32948, 39025, 46222, 54746, 64842, 76800}
};
static const uint32_t br_drive_gain_q15[17] = {
    1815, 2251, 2787, 3443, 4241, 5209, 6371, 7753, 9378, 11261, 13400, 15775, 18337, 21001, 23648, 26134, 28310
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

static BR_FX_PREPARE_CODE float br_adaa_od_f(float x)
{
    return x >= 1.0f ? 2.0f / 3.0f : x <= -1.0f ? -2.0f / 3.0f : x - x * x * x * (1.0f / 3.0f);
}
static BR_FX_PREPARE_CODE float br_adaa_od_F(float x)       /* even */
{
    float ax = x < 0.0f ? -x : x;
    return ax <= 1.0f ? x * x * (0.5f - x * x * (1.0f / 12.0f)) : 5.0f / 12.0f + (2.0f / 3.0f) * (ax - 1.0f);
}
static BR_FX_PREPARE_CODE float br_adaa_dist_f(float x)
{
    float ax = x < 0.0f ? -x : x;
    float m = ax <= 0.3f ? ax : ax < 4.0f ? 0.3f + (ax - 0.3f) * 0.125f : 0.7625f;
    return x < 0.0f ? -m : m;
}
static BR_FX_PREPARE_CODE float br_adaa_dist_F(float x)     /* even */
{
    float ax = x < 0.0f ? -x : x, t;
    if (ax <= 0.3f) return ax * ax * 0.5f;
    if (ax < 4.0f) { t = ax - 0.3f; return 0.045f + 0.3f * t + t * t * (1.0f / 16.0f); }
    return 2.010625f + 0.7625f * (ax - 4.0f);
}
static BR_FX_PREPARE_CODE float br_adaa_fuzz_f(float x)
{
    return x > 0.7f ? 0.7f : x < -0.45f ? -0.45f : x;
}
static BR_FX_PREPARE_CODE float br_adaa_fuzz_F(float x)
{
    if (x > 0.7f) return 0.7f * x - 0.245f;
    if (x < -0.45f) return -0.45f * x - 0.10125f;
    return x * x * 0.5f;
}
static BR_FX_PREPARE_CODE inline __attribute__((always_inline)) float br_adaa_step(uint32_t mode, float a, float b)
{
    float d = b - a;
    if (mode == 0u) {
        if (a >= -1.0f && a <= 1.0f && b >= -1.0f && b <= 1.0f) {
            float s = a + b;
            return s * (0.5f - (a * a + b * b) * (1.0f / 12.0f));
        }
        if ((a >= 1.0f && b >= 1.0f) || (a <= -1.0f && b <= -1.0f)) return br_adaa_od_f(b);
        if (d < BR_ADAA_EPS && d > -BR_ADAA_EPS) return br_adaa_od_f(0.5f * (a + b));
        return (br_adaa_od_F(b) - br_adaa_od_F(a)) / d;
    }
    if (mode == 1u) {
        uint32_t ia = (uint32_t)(a > -4.0f) + (uint32_t)(a > -0.3f) + (uint32_t)(a > 0.3f) + (uint32_t)(a > 4.0f);
        uint32_t ib = (uint32_t)(b > -4.0f) + (uint32_t)(b > -0.3f) + (uint32_t)(b > 0.3f) + (uint32_t)(b > 4.0f);
        if (ia == ib || (d < BR_ADAA_EPS && d > -BR_ADAA_EPS)) return br_adaa_dist_f(0.5f * (a + b));
        return (br_adaa_dist_F(b) - br_adaa_dist_F(a)) / d;
    }
    {
        uint32_t ia = (uint32_t)(a > -0.45f) + (uint32_t)(a > 0.7f), ib = (uint32_t)(b > -0.45f) + (uint32_t)(b > 0.7f);
        if (ia == ib || (d < BR_ADAA_EPS && d > -BR_ADAA_EPS)) return br_adaa_fuzz_f(0.5f * (a + b));
        return (br_adaa_fuzz_F(b) - br_adaa_fuzz_F(a)) / d;
    }
}
#ifndef BR_FX_DRIVE_UNSWITCH
#define BR_FX_DRIVE_UNSWITCH 1
#endif
#if BR_FX_DRIVE_UNSWITCH
/* floor(d * a / 2^15) for a < 2^15 and |d| < 2^30 in two 32-bit products: d = hi*2^15 + lo
 * with 0 <= lo < 2^15, so d*a = hi*a*2^15 + lo*a and the floor splits exactly. */
static inline __attribute__((always_inline)) int32_t br_fx_mul_q15_split(int32_t d, int32_t a)
{
    return (d >> 15) * a + (int32_t)(((uint32_t)(d & 0x7fff) * (uint32_t)a) >> 15);
}
static inline __attribute__((always_inline)) void br_fx_drive_loop(uint32_t mode,
    const int32_t *in, int32_t *out, uint32_t n, float gx, float out_scale, int32_t alpha,
    int32_t send_step, int32_t *send_io, int32_t *lp_io, int32_t *dc_io, float prev)
{
    int32_t send_now = *send_io, lp = *lp_io, dc = *dc_io;
    uint32_t i;
    for (i = 0u; i < n; ++i) {
        int32_t dry = in[i], y;
        float x = (float)dry * gx;
        y = (int32_t)(br_adaa_step(mode, prev, x) * out_scale);
        prev = x;
        if (mode == 2u) {                                   /* fuzz: DC removed */
            dc += (y * 4096 - dc) >> 9;
            y -= (dc + 2048) >> 12;
        }
        lp += br_fx_mul_q15_split(y * 4096 - lp, alpha);    /* tone */
        y = (lp + 2048) >> 12;
        send_now += send_step;                              /* the send ramps across the block */
        out[i] = dry + (int32_t)(((int64_t)(y - dry) * send_now) >> 23);
    }
    *send_io = send_now; *lp_io = lp; *dc_io = dc;
}
#endif
static BR_FX_PREPARE_CODE void br_fx_drive_block(uint32_t p, const int32_t *in, int32_t *out, uint32_t n)
{
    br_fx_track_t *st = &br_fx_track[p];
    const br_sound_t *s = br_fx_sound_at(p);
    const br_fx_config_t *cfg = br_fx_config_at(p);
    uint32_t mode = cfg->drive_mode > 2u ? 0u : cfg->drive_mode;
    uint32_t gain = br_drive_lerp17(br_drive_gain_q8[mode], cfg->drive_gain > 127u ? 127u : cfg->drive_gain);
    int32_t alpha = (int32_t)br_drive_lerp17(br_drive_gain_q15, cfg->drive_tone > 127u ? 127u : cfg->drive_tone);
    int32_t send_to = (int32_t)(s->drive > 127u ? 127u : s->drive) * 258 * 256;   /* send / 127 in Q23 */
    int32_t send_now = st->drive_send, send_step = n ? (send_to - send_now) / (int32_t)n : 0;
    int32_t lp = st->drive_lp, dc = st->drive_dc;      /* Q12, so both settle to exactly zero */
    const float gx = (float)gain * (1.0f / 8388608.0f);              /* gain / 256, in units of full scale */
    const float out_scale = (float)BR_DRIVE_FS * (mode == 0u ? 1.25f : mode == 1u ? 0.875f : 1.0f);
    float prev = (float)st->drive_prev * gx;
#if BR_FX_DRIVE_UNSWITCH
    /* One loop per shaper, so the shaper choice is not tested per sample. */
    if (mode == 0u) br_fx_drive_loop(0u, in, out, n, gx, out_scale, alpha, send_step, &send_now, &lp, &dc, prev);
    else if (mode == 1u) br_fx_drive_loop(1u, in, out, n, gx, out_scale, alpha, send_step, &send_now, &lp, &dc, prev);
    else br_fx_drive_loop(2u, in, out, n, gx, out_scale, alpha, send_step, &send_now, &lp, &dc, prev);
#else
    uint32_t i;
    for (i = 0u; i < n; ++i) {
        int32_t dry = in[i], y;
        float x = (float)dry * gx;
        y = (int32_t)(br_adaa_step(mode, prev, x) * out_scale);
        prev = x;
        if (mode == 2u) {                                   /* fuzz: DC removed */
            dc += (y * 4096 - dc) >> 9;
            y -= (dc + 2048) >> 12;
        }
        lp += (int32_t)(((int64_t)(y * 4096 - lp) * alpha) >> 15);   /* tone */
        y = (lp + 2048) >> 12;
        send_now += send_step;                              /* the send ramps across the block */
        out[i] = dry + (int32_t)(((int64_t)(y - dry) * send_now) >> 23);
    }
#endif
    st->drive_send = send_to;
    if (n) st->drive_prev = in[n - 1u];
    st->drive_lp = lp; st->drive_dc = dc;
}

/* ---- SPACE: a dark, slowly modulated plate in the manner of a "black hole" reverb.
 * Two cross-coupled branches (modulated allpass, long delay, damping, allpass, long
 * delay) run at a quarter of the sample rate (11 kHz), which gives the dark, huge
 * character; the decay control sets the loop gain, size the delay read points. The
 * modulation is two slow triangles (0.28 and 0.41 Hz) of +-10 samples. The whole
 * block runs from flash, called once per block, so it adds almost no RAM code. */
/* Modulation: two slow triangles, phase steps per tank step (11,025 Hz): 0.28 and 0.41 Hz. */
#define BR_SP_LFO_A_INC 109085u
#define BR_SP_LFO_B_INC 159700u
#define BR_SP_AP1A 216u
#define BR_SP_AP1A_NOM 200u
#define BR_SP_D1A 479u
#define BR_SP_AP2A 151u
#define BR_SP_D2A 367u
#define BR_SP_AP1B 249u
#define BR_SP_AP1B_NOM 233u
#define BR_SP_D1B 431u
#define BR_SP_AP2B 181u
#define BR_SP_D2B 331u
static int32_t br_sp_ap1a[BR_SP_AP1A] BR_POOL, br_sp_d1a[BR_SP_D1A] BR_POOL;
static int32_t br_sp_ap2a[BR_SP_AP2A] BR_POOL, br_sp_d2a[BR_SP_D2A] BR_POOL;
static int32_t br_sp_ap1b[BR_SP_AP1B] BR_POOL, br_sp_d1b[BR_SP_D1B] BR_POOL;
static int32_t br_sp_ap2b[BR_SP_AP2B] BR_POOL, br_sp_d2b[BR_SP_D2B] BR_POOL;
static uint8_t br_gr_ev[BR_BLOCK] BR_POOL;
static struct {
    uint16_t w[8];                       /* write positions of the eight buffers */
    int32_t lp_a, lp_b, end_a, end_b;    /* damping filters and branch outputs */
    int32_t dec_lp, acc, out_l, out_r;   /* decimation filter and sum, output filters */
    int32_t prev_l, prev_r, cur_l, cur_r;
    int32_t off[4];                      /* smoothed delay read points, Q8 */
    uint32_t cnt, pa, pb;
} br_sp;

static BR_FX_PREPARE_CODE void br_sp_clear(void)
{
    memset(br_sp_ap1a, 0, sizeof br_sp_ap1a); memset(br_sp_d1a, 0, sizeof br_sp_d1a);
    memset(br_sp_ap2a, 0, sizeof br_sp_ap2a); memset(br_sp_d2a, 0, sizeof br_sp_d2a);
    memset(br_sp_ap1b, 0, sizeof br_sp_ap1b); memset(br_sp_d1b, 0, sizeof br_sp_d1b);
    memset(br_sp_ap2b, 0, sizeof br_sp_ap2b); memset(br_sp_d2b, 0, sizeof br_sp_d2b);
    memset(&br_sp, 0, sizeof br_sp);
}

static int32_t br_sp_mul(int32_t a, int32_t g) { return (int32_t)(((int64_t)a * g) >> 15); }
static int32_t br_sp_tri(uint32_t phase)
{
    uint32_t t = phase >> 15, v = t & 65535u;
    if (t & (1u << 16)) v = 65535u - v;
    return (int32_t)v - 32768;
}
/* Delay line read, Q8 delay, linear interpolation; w is the next write position. */
static int32_t br_sp_read(const int32_t *buf, uint32_t len, uint32_t w, int32_t delay_q8)
{
    uint32_t d = (uint32_t)delay_q8 >> 8, frac = (uint32_t)delay_q8 & 255u, i0, i1;
    int32_t a, b;
    if (d + 1u >= len) { d = len - 2u; frac = 0u; }
    i0 = w + len - d; if (i0 >= len) i0 -= len;
    i1 = i0 ? i0 - 1u : len - 1u;
    a = buf[i0]; b = buf[i1];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)frac) >> 8);
}
#define BR_SP_PUT(k, buf, len, value) { buf[br_sp.w[k]] = br_fx_clip_ram((value), -300000000, 300000000); \
    if (++br_sp.w[k] == (len)) br_sp.w[k] = 0u; }

/* One tank step on the decimated input x. g: loop gain Q15, damp: damping pole Q15. */
static void br_sp_step(int32_t x, int32_t g, int32_t damp, uint32_t size)
{
    int32_t a, d, v, m, target;
    static const int32_t LEN1[2] = {BR_SP_D1A, BR_SP_D1B}, LEN2[2] = {BR_SP_D2A, BR_SP_D2B};
    uint32_t k;
    br_sp.pa += BR_SP_LFO_A_INC; br_sp.pb += BR_SP_LFO_B_INC;                 /* 0.28 Hz and 0.41 Hz at 11,025 Hz */
    for (k = 0u; k < 2u; ++k) {                                /* smooth the size-dependent read points */
        target = (LEN1[k] * (int32_t)(127u + size) / 254 - 2) << 8;
        br_sp.off[2u * k] += (target - br_sp.off[2u * k]) >> 6;
        target = (LEN2[k] * (int32_t)(127u + size) / 254 - 2) << 8;
        br_sp.off[2u * k + 1u] += (target - br_sp.off[2u * k + 1u]) >> 6;
    }
    /* branch A */
    a = x + br_sp.end_b;
    m = (br_sp_tri(br_sp.pa) * (10 * 256)) >> 15;
    d = br_sp_read(br_sp_ap1a, BR_SP_AP1A, br_sp.w[0], (int32_t)(BR_SP_AP1A_NOM << 8) + m);
    v = a - br_sp_mul(d, 22938);               /* 0.70 */
    BR_SP_PUT(0, br_sp_ap1a, BR_SP_AP1A, v)
    a = d + br_sp_mul(v, 22938);
    d = br_sp_read(br_sp_d1a, BR_SP_D1A, br_sp.w[1], br_sp.off[0]);
    BR_SP_PUT(1, br_sp_d1a, BR_SP_D1A, a)
    br_sp.lp_a += br_sp_mul(d - br_sp.lp_a, damp);
    a = br_sp_mul(br_sp.lp_a, g);
    d = br_sp_read(br_sp_ap2a, BR_SP_AP2A, br_sp.w[2], (int32_t)((BR_SP_AP2A - 4u) << 8));
    v = a - br_sp_mul(d, 16384);               /* 0.50 */
    BR_SP_PUT(2, br_sp_ap2a, BR_SP_AP2A, v)
    a = d + br_sp_mul(v, 16384);
    d = br_sp_read(br_sp_d2a, BR_SP_D2A, br_sp.w[3], br_sp.off[1]);
    BR_SP_PUT(3, br_sp_d2a, BR_SP_D2A, a)
    br_sp.end_a = d;
    /* branch B */
    a = x + br_sp.end_a;
    m = (br_sp_tri(br_sp.pb) * (10 * 256)) >> 15;
    d = br_sp_read(br_sp_ap1b, BR_SP_AP1B, br_sp.w[4], (int32_t)(BR_SP_AP1B_NOM << 8) + m);
    v = a - br_sp_mul(d, 22938);
    BR_SP_PUT(4, br_sp_ap1b, BR_SP_AP1B, v)
    a = d + br_sp_mul(v, 22938);
    d = br_sp_read(br_sp_d1b, BR_SP_D1B, br_sp.w[5], br_sp.off[2]);
    BR_SP_PUT(5, br_sp_d1b, BR_SP_D1B, a)
    br_sp.lp_b += br_sp_mul(d - br_sp.lp_b, damp);
    a = br_sp_mul(br_sp.lp_b, g);
    d = br_sp_read(br_sp_ap2b, BR_SP_AP2B, br_sp.w[6], (int32_t)((BR_SP_AP2B - 4u) << 8));
    v = a - br_sp_mul(d, 16384);
    BR_SP_PUT(6, br_sp_ap2b, BR_SP_AP2B, v)
    a = d + br_sp_mul(v, 16384);
    d = br_sp_read(br_sp_d2b, BR_SP_D2B, br_sp.w[7], br_sp.off[3]);
    BR_SP_PUT(7, br_sp_d2b, BR_SP_D2B, a)
    br_sp.end_b = d;
    /* stereo taps from the long delays */
    br_sp.prev_l = br_sp.cur_l; br_sp.prev_r = br_sp.cur_r;
    br_sp.cur_l = (br_sp_read(br_sp_d1a, BR_SP_D1A, br_sp.w[1], (int32_t)(150u << 8)) +
                   br_sp_read(br_sp_d2a, BR_SP_D2A, br_sp.w[3], (int32_t)(200u << 8)) -
                   br_sp_read(br_sp_d1b, BR_SP_D1B, br_sp.w[5], (int32_t)(290u << 8))) >> 1;
    br_sp.cur_r = (br_sp_read(br_sp_d1b, BR_SP_D1B, br_sp.w[5], (int32_t)(125u << 8)) +
                   br_sp_read(br_sp_d2b, BR_SP_D2B, br_sp.w[7], (int32_t)(198u << 8)) -
                   br_sp_read(br_sp_d1a, BR_SP_D1A, br_sp.w[1], (int32_t)(340u << 8))) >> 1;
}

/* The SPACE reverb for one block: sums the previous block's sends, diffuses them,
 * runs the tank and leaves the stereo return in br_gr_wet. br_gr_ev flags the
 * frames loud enough to restart the tail. */
static BR_FX_PREPARE_CODE void br_gr_space_block(uint32_t n)
{
    const br_fx_controls_t *c = &br_gr_controls;
    const int32_t *row[BR_PARTS];
    uint32_t i, k, p, rows = 0u, prev = br_gr_cur ^ 1u;
    int32_t g = 22282 + (int32_t)(br_gr_key[1] * 9800u / 127u);   /* branch gain 0.68 .. 0.98 */
    int32_t damp = 16384;                                          /* loop damping, about 1.7 kHz at 11 kHz */
    for (p = 0u; p < BR_PARTS; ++p)
        if (br_gr_have[prev][p]) row[rows++] = br_gr_send[prev][p];
    for (i = 0u; i < n; ++i) {
        int32_t in = 0, out;
        for (k = 0u; k < rows; ++k) in += row[k][i];
#define BR_GR_AP(buf, len, kk, gq) { int32_t dd = buf[br_gr_ap_pos[kk]]; int32_t vv = in - ((dd * (gq)) >> 12); \
    buf[br_gr_ap_pos[kk]] = vv; in = dd + ((vv * (gq)) >> 12); if (++br_gr_ap_pos[kk] == (len)) br_gr_ap_pos[kk] = 0u; }
        in = br_fx_clip_ram(in, -250000, 250000);
        BR_GR_AP(br_gr_ap0, BR_GR_AP0_LEN, 0u, 3072)
        BR_GR_AP(br_gr_ap1, BR_GR_AP1_LEN, 1u, 3072)
        BR_GR_AP(br_gr_ap2, BR_GR_AP2_LEN, 2u, 2560)
        BR_GR_AP(br_gr_ap3, BR_GR_AP3_LEN, 3u, 2560)
#undef BR_GR_AP
        in = (br_fx_clip_ram(in, -1000000, 1000000) * (int32_t)c->reverb_return_q8) >> 8;
        br_gr_ev[i] = in > 3 || in < -3;
        /* decimate by four through a one-pole low-pass */
        br_sp.dec_lp += br_sp_mul(in - br_sp.dec_lp, 9830);
        br_sp.acc += br_sp.dec_lp;
        if (++br_sp.cnt == 4u) {
            br_sp_step(br_sp.acc >> 2, g, damp, br_gr_key[2]);
            br_sp.acc = 0; br_sp.cnt = 0u;
        }
        /* linear interpolation between the last two tank outputs, then a smoothing pole */
        k = br_sp.cnt;                       /* 0..3 frames since the last step */
        out = br_sp.prev_l + (int32_t)(((int64_t)(br_sp.cur_l - br_sp.prev_l) * (int32_t)(k + 1u)) >> 2);
        br_sp.out_l += br_sp_mul(out - br_sp.out_l, 14746);
        out = br_sp.prev_r + (int32_t)(((int64_t)(br_sp.cur_r - br_sp.prev_r) * (int32_t)(k + 1u)) >> 2);
        br_sp.out_r += br_sp_mul(out - br_sp.out_r, 14746);
        br_gr_wet[i][0] = br_sp.out_l;
        br_gr_wet[i][1] = br_sp.out_r;
    }
}

/* The shared reverb for one block, run by the host track's job: sum the sends
 * the tracks left last block and write the stereo return for this block's mix. */
static BR_FX_RAM_AUDIO __attribute__((noinline)) void br_gr_job(uint32_t n)
{
    br_fx_track_t *st = &br_gr_state;
    br_fx_controls_t *c = &br_gr_controls;
    const int32_t *row[BR_PARTS];
    uint32_t i, k, p, rows = 0u, prev = br_gr_cur ^ 1u, tail = st->tail_samples;
#if defined(FM1_DEVICE)
    uint32_t started = fm1_ticks();
#endif
    if (!br_gr_run) return;
    /* Never run on unprepared controls or out-of-range allpass positions. */
    if (!br_gr_key_valid || st->reverb_output_ap_pos[0] >= BR_FX_REVERB_OUTPUT_AP0_LENGTH ||
        st->reverb_output_ap_pos[1] >= BR_FX_REVERB_OUTPUT_AP1_LENGTH) {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        ++br_gr_stats[1];
#endif
        st->reverb_output_ap_pos[0] = st->reverb_output_ap_pos[1] = 0u;
        if (!br_gr_key_valid) return;
    }
    c->reverb_output_ap0_base = br_reverb_output_ap0[0];
    c->reverb_output_ap1_base = br_reverb_output_ap1[0];
    if (c->reverb_engine == BR_REVERB_SPACE) {
        /* The SPACE tank runs from flash; only the tail gating is done here. */
        BR_FX_PREPARE_INVOKE(br_gr_space_block, n);
        for (i = 0u; i < n; ++i) {
            if (br_gr_ev[i] && tail < c->reverb_tail_hold) tail = c->reverb_tail_hold;
            if (tail) st->tail_storage = 1u;
            if (tail < FS / 4u) {
                uint32_t wet_gain = br_fx_tail_gain_below_fade(tail);
                br_gr_wet[i][0] = br_fx_wet_gain(br_gr_wet[i][0], wet_gain);
                br_gr_wet[i][1] = br_fx_wet_gain(br_gr_wet[i][1], wet_gain);
            }
            if (tail) --tail;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
            { uint32_t a = (uint32_t)(br_gr_wet[i][0] < 0 ? -br_gr_wet[i][0] : br_gr_wet[i][0]); if (a > br_gr_stats[4]) br_gr_stats[4] = a; }
#endif
        }
        goto finished;
    }
    for (p = 0u; p < BR_PARTS; ++p)
        if (br_gr_have[prev][p]) row[rows++] = br_gr_send[prev][p];
    /* Allpass positions, the tail flag and the peak statistic live in locals for the
     * block and are stored once after it. */
    uint32_t ap_pos[4] = {br_gr_ap_pos[0], br_gr_ap_pos[1], br_gr_ap_pos[2], br_gr_ap_pos[3]};
    uint32_t touched = 0u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    uint32_t peak = br_gr_stats[4];
#endif
    for (i = 0u; i < n; ++i) {
        int32_t rv_l = 0, rv_r = 0, event = 0, in = 0;
        for (k = 0u; k < rows; ++k) in += row[k][i];
        {   /* four allpasses (Q12 gains 0.75, 0.75, 0.625, 0.625) */
#define BR_GR_AP(buf, len, k, g) { int32_t d = buf[ap_pos[k]]; int32_t v = in - ((d * (g)) >> 12); \
    buf[ap_pos[k]] = v; in = d + ((v * (g)) >> 12); if (++ap_pos[k] == (len)) ap_pos[k] = 0u; }
            in = br_fx_clip_ram(in, -250000, 250000);
            BR_GR_AP(br_gr_ap0, BR_GR_AP0_LEN, 0u, 3072)
            BR_GR_AP(br_gr_ap1, BR_GR_AP1_LEN, 1u, 3072)
            BR_GR_AP(br_gr_ap2, BR_GR_AP2_LEN, 2u, 2560)
            BR_GR_AP(br_gr_ap3, BR_GR_AP3_LEN, 3u, 2560)
#undef BR_GR_AP
        }
        /* Makeup gain goes in ahead of the reverb, not after it: the delay lines hold
         * 16-bit companded values with a dead zone near zero, and a louder signal
         * through them keeps that noise floor further below the tail. */
        in = (br_fx_clip_ram(in, -1000000, 1000000) * (int32_t)c->reverb_return_q8) >> 8;
#if BR_FX_HAS_REVERB_RATE4
        if (c->reverb_rate4)
            br_fx_reverb_rate4(st, 0u, in, c, &rv_l, &rv_r BR_FX_TAIL_EVENT_ARG(&event));
        else
#endif
            br_fx_reverb_rate2(st, 0u, in, c, &rv_l, &rv_r BR_FX_TAIL_EVENT_ARG(&event));
        {
            uint32_t ramp_frames = br_fx_reverb_ramp_frames();
            if (st->reverb_rate2_ramp < ramp_frames) {
#if BR_FX_NATIVE_RATE_CALIBRATION || BR_FX_NATIVE_RATE_48K
                rv_l = (rv_l * (int32_t)st->reverb_rate2_ramp) / (int32_t)ramp_frames;
                rv_r = (rv_r * (int32_t)st->reverb_rate2_ramp) / (int32_t)ramp_frames;
#else
                rv_l = br_fx_div_pow2(rv_l * (int32_t)st->reverb_rate2_ramp, 5u);
                rv_r = br_fx_div_pow2(rv_r * (int32_t)st->reverb_rate2_ramp, 5u);
#endif
                ++st->reverb_rate2_ramp;
            }
        }
        if (event && tail < c->reverb_tail_hold) tail = c->reverb_tail_hold;
        touched |= tail;
        if (tail < FS / 4u) {
            uint32_t wet_gain = br_fx_tail_gain_below_fade(tail);
            rv_l = br_fx_wet_gain(rv_l, wet_gain);
            rv_r = br_fx_wet_gain(rv_r, wet_gain);
        }
        br_fx_reverb_output_diffuse(st, c, &rv_l, &rv_r);
        if (tail) --tail;
        br_gr_wet[i][0] = rv_l;
        br_gr_wet[i][1] = rv_r;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        { uint32_t a = (uint32_t)(rv_l < 0 ? -rv_l : rv_l); if (a > peak) peak = a; }
#endif
    }
    for (k = 0u; k < 4u; ++k) br_gr_ap_pos[k] = ap_pos[k];
    if (touched) st->tail_storage = 1u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_gr_stats[4] = peak;
#endif
finished:
    st->tail_samples = tail;
    if (!tail && st->tail_storage) br_gr_clear();
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    ++br_gr_stats[0];
    br_gr_stats[5] = c->reverb_engine; br_gr_stats[6] = (c->reverb_feedback - 14000u) / 130u; br_gr_stats[7] = c->reverb_space;
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

/* Add the reverb return to the finished mix for frames [base, end). */
static BR_FX_RAM_AUDIO void br_gr_add_range(int32_t *out, uint32_t base, uint32_t end)
{
    uint32_t i;
    if (!br_gr_run) return;
    for (i = base; i < end; ++i) {
        out[2u * i] += br_gr_wet[i][0];
        out[2u * i + 1u] += br_gr_wet[i][1];
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
#elif BR_FX_BLOCK_STAGE_SPECIALIZE_REVERB
    br_fx_stage_reverb_block(st, p, sample_base, progress_core, pan, lfo_start,
        c->mod_lfo_inc, lfo_final, input_bus, mod, n, s, cfg, c,
        &tail_samples, stereo);
#else
    lfo_phase = lfo_start;
    for (i = 0u; i < n; ++i) {
        int32_t rv_l, rv_r, reverb_event = 0;
        int32_t delayed;
        lfo_phase += c->mod_lfo_inc;
        st->lfo_phase = lfo_phase;
        br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, sample_base + i,
                             BR_FX_PROGRESS_REVERB);
        delayed = stereo[i][0];
#if BR_FX_HAS_REVERB_RATE4
        if (c->reverb_rate4) {
            br_fx_reverb_rate4(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
        }
#if BR_FX_HAS_REVERB_RATE2
        else if (c->reverb_rate2) {
            br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
        }
#endif
        else {
            (void)br_fx_reverb(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
        }
#elif BR_FX_HAS_REVERB_RATE2
        if (c->reverb_rate2) {
            br_fx_reverb_rate2(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
        } else {
            (void)br_fx_reverb(st, p, delayed, c, &rv_l, &rv_r
                               BR_FX_TAIL_EVENT_ARG(&reverb_event));
        }
#else
        (void)br_fx_reverb(st, p, delayed, c, &rv_l, &rv_r
                           BR_FX_TAIL_EVENT_ARG(&reverb_event));
#endif
        br_fx_stage_emit_frame(p, i, sample_base, progress_core, pan, st, s, cfg, c,
            input_bus[i], mod[i], delayed, rv_l, rv_r, stereo[i][1],
            reverb_event, &tail_samples);
    }
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
        br_fx_clear32((br_fx_clear_word_t *)br_delay_line[p],
                      sizeof br_delay_line[p] / 4u);
#if BR_FX_DELAY_RATE2
        br_fx_delay_storage_reset(st, st->delay_storage_shift);
        st->delay_output_last = 0;
        st->delay_output_xfade_left = 0u;
        st->glitch_half_error = 0u;
#else
        st->delay_lp = 0;
#endif
    }
    if (clear_reverb) {
        br_fx_clear32((br_fx_clear_word_t *)br_room_line[p],
                      sizeof br_room_line[p] / 4u);
        br_fx_clear32((br_fx_clear_word_t *)st->room_lp,
                      sizeof st->room_lp / sizeof(uint32_t));
        br_fx_reverb_output_ap_reset(p, st);
#if BR_FX_HAS_REVERB_RATE
        st->reverb_rate2_phase = 0u;
        st->reverb_rate2_ramp = 0u;
        st->reverb_rate2_previous_l = 0;
        st->reverb_rate2_previous_r = 0;
        br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate2_input_hist,
                      sizeof st->reverb_rate2_input_hist / sizeof(uint32_t));
#if BR_FX_HAS_REVERB_RATE4
        st->reverb_rate4_phase = 0u;
        st->reverb_rate4_from_l = 0;
        st->reverb_rate4_from_r = 0;
        br_fx_clear32((br_fx_clear_word_t *)st->reverb_rate4_input_hist,
                      sizeof st->reverb_rate4_input_hist / sizeof(uint32_t));
#endif
#if BR_FX_HALL_FIR_MIRROR_HISTORY
        st->reverb_hall_q4_full_history_pos = 0u;
        st->reverb_hall_q4_half_history_pos = 0u;
        st->reverb_hall_q4_history_valid = 0u;
#endif
#endif
    }
    if (clear_mod) {
        br_fx_clear32((br_fx_clear_word_t *)br_mod_line[p],
                      sizeof br_mod_line[p] / 4u);
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
    uint32_t transition_delay, transition_reverb, transition_mod;
    uint32_t active_wet, restart_possible = 0u;

    if (!n) return 1;
    if (!input_bus || n > BR_BLOCK) return 0;
    transition_delay = st->previous_delay_wet && !s->delay;
#if BR_FX_GLOBAL_REVERB
    transition_reverb = 0u;
#else
    transition_reverb = st->previous_reverb_wet && !s->reverb;
#endif
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
    if (!st->tail_samples && !transition_delay && !transition_reverb &&
        !transition_mod) {
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
    if (enabled && !transition_delay && !transition_reverb && !transition_mod &&
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
        uint32_t clear_tail, clear_delay, clear_reverb, clear_mod;
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
        clear_delay = st->previous_delay_wet && !s->delay;
#if BR_FX_GLOBAL_REVERB
        clear_reverb = 0u;
#else
        clear_reverb = st->previous_reverb_wet && !s->reverb;
#endif
        clear_mod = st->previous_mod_mix && !cfg->mod_mix;
        if (clear_tail || clear_delay || clear_reverb || clear_mod) {
            br_fx_stage_clear_memories(p, st, clear_tail, clear_delay,
                                       clear_reverb, clear_mod);
            if (!enabled && (clear_delay || clear_reverb || clear_mod))
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
    br_fx_warm_pending = 0u;
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
        br_paced_clear_buffer(br_room_line, sizeof br_room_line);
        br_paced_clear_buffer(br_mod_line, sizeof br_mod_line);
    } else
#endif
    {
        memset(br_delay_line, 0, sizeof br_delay_line);
        memset(br_room_line, 0, sizeof br_room_line);
        memset(br_mod_line, 0, sizeof br_mod_line);
    }
    memset(br_reverb_output_ap0, 0, sizeof br_reverb_output_ap0);
    memset(br_reverb_output_ap1, 0, sizeof br_reverb_output_ap1);
#if defined(FM1_DEVICE) || defined(BR_PACED_CLEAR_HOST_TEST)
    if (br_paced_clear_active) br_paced_clear_buffer(br_fx_track, sizeof br_fx_track);
    else
#endif
        memset(br_fx_track, 0, sizeof br_fx_track);
#if BR_FX_GLOBAL_REVERB
    memset(&br_gf, 0, sizeof br_gf);
    memset(&br_gr_state, 0, sizeof br_gr_state);
    memset(&br_gr_controls, 0, sizeof br_gr_controls);
    memset(br_gr_send, 0, sizeof br_gr_send);
    memset(br_gr_wet, 0, sizeof br_gr_wet);
    memset(br_gr_have, 0, sizeof br_gr_have);
    memset(br_gr_ap0, 0, sizeof br_gr_ap0); memset(br_gr_ap1, 0, sizeof br_gr_ap1);
    memset(br_gr_ap2, 0, sizeof br_gr_ap2); memset(br_gr_ap3, 0, sizeof br_gr_ap3);
    memset(br_gr_ap_pos, 0, sizeof br_gr_ap_pos);
    br_gr_key_valid = br_gr_wanted = br_gr_run = br_gr_cur = 0u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    memset((void *)br_gr_stats, 0, sizeof br_gr_stats);
#else
    br_gr_cost_ticks = 0u;
#endif
    br_gr_state.reverb_rate2_ramp = br_fx_reverb_ramp_frames();
#endif
    br_fx_fusion_pending = 0u;
    for (p = 0; p < BR_PARTS; ++p) {
        br_fx_fusion_done[p] = 0u;
        br_fx_controls[p].reverb_output_ap0_base = br_reverb_output_ap0[p];
        br_fx_controls[p].reverb_output_ap1_base = br_reverb_output_ap1[p];
    }
#if BR_FX_HAS_REVERB_RATE
    for (p = 0u; p < BR_PARTS; ++p)
        br_fx_track[p].reverb_rate2_ramp = br_fx_reverb_ramp_frames();
#endif
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
    if (br_fx_sound_at(p)->drive) {          /* DRIVE comes first in the chain */
        uint32_t k;
        for (k = 0u; k < n && !input_bus[k]; ++k) {}   /* a silent track costs nothing */
        if (k < n) {
            BR_FX_PREPARE_INVOKE(br_fx_drive_block, p, input_bus, br_fx_drive_buf[p], n);
            input_bus = br_fx_drive_buf[p];
        }
    }
    br_fx_progress_store(br_fx_progress_retained.words, progress_core, p, 0u, BR_FX_PROGRESS_ENTRY);
    const uint32_t enabled = br_fx_job_enabled[p];
    const uint32_t pan = br_fx_job_pan[p];
    st = &br_fx_track[p];
    s = br_fx_sound_at(p);
    cfg = br_fx_config_at(p);
    c = &br_fx_controls[p];
#if BR_FX_HAS_REVERB_RATE
    {
        uint32_t rate2_transition = st->previous_reverb_rate2 != c->reverb_rate2;
        uint32_t engine_transition = st->previous_reverb_engine != c->reverb_engine;
#if BR_FX_HAS_REVERB_RATE4
        uint32_t rate4_transition = st->previous_reverb_rate4 != c->reverb_rate4;
        uint32_t rate4_active = st->previous_reverb_rate4 || c->reverb_rate4;
#else
        uint32_t rate4_transition = 0u;
        uint32_t rate4_active = 0u;
#endif
        if (rate2_transition || rate4_transition ||
            (engine_transition && (st->previous_reverb_rate2 || c->reverb_rate2 ||
                                   rate4_active))) {
            uint32_t line;
            for (line = 0u; line < 4u; ++line)
                if (c->reverb_length[line])
                    st->room_pos[line] %= c->reverb_length[line];
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
            st->previous_reverb_rate4 = c->reverb_rate4;
#if BR_FX_HALL_FIR_MIRROR_HISTORY
            st->reverb_hall_q4_full_history_pos = 0u;
            st->reverb_hall_q4_half_history_pos = 0u;
            st->reverb_hall_q4_history_valid = 0u;
#endif
#endif
        }
        st->previous_reverb_rate2 = c->reverb_rate2;
        st->previous_reverb_engine = c->reverb_engine;
    }
#endif
#if BR_FX_MOD_RATE2
    if (st->previous_mod_engine != c->mod_engine) {
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
            br_fx_clear32((br_fx_clear_word_t *)br_delay_line[p], sizeof br_delay_line[p] / 4u);
            br_fx_clear32((br_fx_clear_word_t *)br_room_line[p], sizeof br_room_line[p] / 4u);
            br_fx_clear32((br_fx_clear_word_t *)br_mod_line[p], sizeof br_mod_line[p] / 4u);
            br_fx_reverb_output_ap_reset(p, st);
            br_fx_clear32((br_fx_clear_word_t *)st->room_lp, sizeof st->room_lp / 4u);
            br_fx_clear32((br_fx_clear_word_t *)st->phase_state, sizeof st->phase_state / 4u);
#if BR_FX_MOD_RATE2
            br_fx_clear32((br_fx_clear_word_t *)st->mod_rate2_input_history,
                sizeof st->mod_rate2_input_history / sizeof(uint32_t));
            st->mod_rate2_previous_output = 0;
            st->mod_rate2_phase = 2u;
#endif
#if BR_FX_DELAY_RATE2
            br_fx_delay_storage_reset(st, st->delay_storage_shift);
            st->delay_output_last = 0;
            st->delay_output_xfade_left = 0u;
            st->glitch_half_error = 0u;
#else
            st->delay_lp = 0;
#endif
            st->tail_storage = 0;
        }
        if (st->previous_delay_wet && !s->delay) {
            br_fx_clear32((br_fx_clear_word_t *)br_delay_line[p], sizeof br_delay_line[p] / 4u);
#if BR_FX_DELAY_RATE2
            br_fx_delay_storage_reset(st, st->delay_storage_shift);
            st->delay_output_last = 0;
            st->delay_output_xfade_left = 0u;
            st->glitch_half_error = 0u;
#else
            st->delay_lp = 0;
#endif
            if (!enabled) st->tail_samples = 0;
        }
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

BR_FX_RAM_AUDIO uint32_t br_fx_fusion_prepare(
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
    *samples = BR_FX_DELAY_SAMPLES;
    return br_delay_line[part];
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
        if (!master_active && !br_fx_finish_stream_job.scale
#if BR_FX_GLOBAL_REVERB
            && !gr_active
#endif
            ) {
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
            br_fx_stream_test_done();
#endif
            return 1u;
        }
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
#if BR_FX_STREAM_OUTPUT_SCALE
            if (br_fx_finish_stream_job.scale)
                br_fx_scale_q12_clip_range(br_fx_finish_stream_job.out + 2u * base,
                                           2u * (end - base),
                                           (uint32_t)br_fx_finish_stream_job.gain_q12);
#endif
#if defined(BR_SOURCE_QUEUE_TEST) && defined(BR_FX_STREAM_TEST_CLOCK)
            br_fx_stream_test_after_range(end);
#endif
            base = end;
        }
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
        fm1_cpu1_worker_wait;
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
        /* Diagnostic: permit 1,000 extra T4 ticks (41.7 us at 24 MHz) for
         * streamed master FX completion. The prior cold key press timed out
         * at 17,417 ticks against the old 17,415-tick limit. This only changes
         * the wait guard; successful DSP output and controls are unchanged. */
        uint32_t stream_status = stream_wait_fn(stream_sequence, 18415u, &stream_result);
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
        if (n) br_comp_reset_detector();
        br_fx_finish_block(bus, out, n, 0u, 1u);
        return;
    }
#else
    if (dry_center && !comp_active) {
        /* Keep each track's signed division before summing: combining the
         * divisions would change truncation for negative or quiet samples. */
        if (n) br_comp_reset_detector();
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
    extern volatile uint32_t fm1_cpu1_worker_fault;
    uint32_t i;
    if (fm1_cpu1_worker_fault || br_audio_source_worker_fault) {
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
    if (br_gr_state.tail_samples) return 0;
#endif
    return br_master_state.filter_tail == 0u && br_master_state.wow_tail == 0u;
}
