/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DINGHY_PLAITS_CONFIG_H_
#define DINGHY_PLAITS_CONFIG_H_

/* Prepare shared controls once for public C API calls larger than 32 frames. */
#ifndef FM1_PLAITS_CAPI_PREPARE_LARGE_CALL
#define FM1_PLAITS_CAPI_PREPARE_LARGE_CALL 1
#endif
#if FM1_PLAITS_CAPI_PREPARE_LARGE_CALL != 0 && \
    FM1_PLAITS_CAPI_PREPARE_LARGE_CALL != 1
#error "FM1_PLAITS_CAPI_PREPARE_LARGE_CALL must be 0 or 1"
#endif

/* Let the app pass a full Plaits block to the C API. The adapter still renders
 * bounded FIFO-safe chunks internally; this avoids repeating C API setup in
 * instrument_audio.c. Keep off until target timing is qualified. */
#ifndef FM1_PLAITS_AUDIO_LARGE_CALL
#define FM1_PLAITS_AUDIO_LARGE_CALL 1
#endif
#if FM1_PLAITS_AUDIO_LARGE_CALL != 0 && FM1_PLAITS_AUDIO_LARGE_CALL != 1
#error "FM1_PLAITS_AUDIO_LARGE_CALL must be 0 or 1"
#endif

/* Cache the four mixed source points and cubic coefficients while consecutive
 * output frames reuse a source index. This changes only C API resampling work;
 * the default remains the original per-output calculation. */
#ifndef FM1_PLAITS_SRC_ROLLING_CACHE
#define FM1_PLAITS_SRC_ROLLING_CACHE 1
#endif
#if FM1_PLAITS_SRC_ROLLING_CACHE != 0 && FM1_PLAITS_SRC_ROLLING_CACHE != 1
#error "FM1_PLAITS_SRC_ROLLING_CACHE must be 0 or 1"
#endif

/* Use the native source phase remainder directly for the 32 kHz cubic
 * resampler's float parameter. Keep the original Q16 conversion by default. */
#ifndef FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION
#define FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION 1
#endif
#if FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION != 0 && \
    FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION != 1
#error "FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION must be 0 or 1"
#endif

/* Native unity-rate adapter: validate FIFO coverage once per block, then
 * sample without reloading/checking the FIFO count on every output frame. */
#ifndef FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH
#define FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH 1
#endif
#if FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH != 0 && \
    FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH != 1
#error "FM1_PLAITS_NATIVE_BLOCK_BOUNDS_FASTPATH must be 0 or 1"
#endif

/* Optional one-LSB OUT/AUX mix approximation for the native-rate sample path. */
#ifndef FM1_PLAITS_AUX_MIX_Q15_SHIFT
#define FM1_PLAITS_AUX_MIX_Q15_SHIFT 1
#endif
#if FM1_PLAITS_AUX_MIX_Q15_SHIFT != 0 && FM1_PLAITS_AUX_MIX_Q15_SHIFT != 1
#error "FM1_PLAITS_AUX_MIX_Q15_SHIFT must be 0 or 1"
#endif

/* Experimental source cadence change for Modal engine 20 only. The existing
 * model-mask policy (including Harmonic model 12) remains independently
 * selected; this default-off bit adds Modal to that mask. */
#ifndef FM1_MODAL20_RENDER_CHUNK24
#define FM1_MODAL20_RENDER_CHUNK24 1
#endif
#if FM1_MODAL20_RENDER_CHUNK24 != 0 && FM1_MODAL20_RENDER_CHUNK24 != 1
#error "FM1_MODAL20_RENDER_CHUNK24 must be 0 or 1"
#endif

/* Shared by the C app wrapper and every Plaits DSP translation unit so an
 * opt-in model-preparation build cannot compile different policies per TU. */
#ifndef FM1_PLAITS_PREWARM_MODEL
#define FM1_PLAITS_PREWARM_MODEL 1
#endif

/* Use a render-local Particle RNG word to avoid repeated core-state lookups. */
#ifndef FM1_PLAITS_PARTICLE_LOCAL_RNG
#define FM1_PLAITS_PARTICLE_LOCAL_RNG 1
#endif

/* Fast-path stable modal direct-form coefficient-cache hits with persistent
 * float recurrence state. Keep off until target timing is qualified. */
#ifndef FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT
#define FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT 1
#endif
#if FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT != 0 && \
    FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT != 1
#error "FM1_MODAL_PERSISTENT_FLOAT_DIRECT_CACHE_HIT must be 0 or 1"
#endif

/* Prototype: inline the cache-hit helper at its single batch call site. */
#ifndef FM1_MODAL_CACHE_HIT_FORCE_INLINE
#define FM1_MODAL_CACHE_HIT_FORCE_INLINE 1
#endif
#if FM1_MODAL_CACHE_HIT_FORCE_INLINE != 0 && \
    FM1_MODAL_CACHE_HIT_FORCE_INLINE != 1
#error "FM1_MODAL_CACHE_HIT_FORCE_INLINE must be 0 or 1"
#endif

/* Prototype: let the existing ProcessInternal fallback own alias-safe input
 * copying instead of reserving a second copy buffer in the cache helper. */
#ifndef FM1_MODAL_CACHE_HIT_ALIAS_FALLBACK
#define FM1_MODAL_CACHE_HIT_ALIAS_FALLBACK 1
#endif
#if FM1_MODAL_CACHE_HIT_ALIAS_FALLBACK != 0 && \
    FM1_MODAL_CACHE_HIT_ALIAS_FALLBACK != 1
#error "FM1_MODAL_CACHE_HIT_ALIAS_FALLBACK must be 0 or 1"
#endif

/* Use a render-local LCG word for synthesized kick/snare noise draws. */
#ifndef FM1_PLAITS_KIT_LOCAL_RNG
#define FM1_PLAITS_KIT_LOCAL_RNG 1
#endif

/* Experimental correction for fixed per-sample/per-render smoothing
 * constants when testing the 38.4 kHz source path. */
#ifndef FM1_PLAITS_RATE_TIME_CALIBRATION
#define FM1_PLAITS_RATE_TIME_CALIBRATION 1
#endif
#if FM1_PLAITS_RATE_TIME_CALIBRATION != 0 && \
    FM1_PLAITS_RATE_TIME_CALIBRATION != 1
#error "FM1_PLAITS_RATE_TIME_CALIBRATION must be 0 or 1"
#endif

/* Correct the 808 hi-hat's per-sample envelope coefficients when rendering at
 * a lower internal source rate. This remains a separate opt-in so other
 * source-rate timing changes can be qualified independently. */
#ifndef FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION
#define FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION 1
#endif
#if FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION != 0 && \
    FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION != 1
#error "FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION must be 0 or 1"
#endif
#if FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION && \
    !FM1_PLAITS_RATE_TIME_CALIBRATION
#error "Hi-hat rate calibration requires the shared rate-time calibration policy"
#endif

/* Experimental: retime only the synthetic auxiliary KCK1/KCK2 voice. */
#ifndef BR_PLAITS_KIT_KICK_SYNTH_RATE2
#define BR_PLAITS_KIT_KICK_SYNTH_RATE2 1
#endif
/* Experimental two-step reciprocal only for analog kick SVF coefficients. */
#ifndef BR_PLAITS_KIT_KICK_FAST_COEFFICIENT_RECIP
#define BR_PLAITS_KIT_KICK_FAST_COEFFICIENT_RECIP 1
#endif
#if BR_PLAITS_KIT_KICK_FAST_COEFFICIENT_RECIP != 0 && \
    BR_PLAITS_KIT_KICK_FAST_COEFFICIENT_RECIP != 1
#error "BR_PLAITS_KIT_KICK_FAST_COEFFICIENT_RECIP must be 0 or 1"
#endif
/* Replace the two common LPG size divisions with a shared constant inverse. */
#ifndef FM1_PLAITS_FAST_LPG_INVERSE_SIZE
#define FM1_PLAITS_FAST_LPG_INVERSE_SIZE 1
#endif
#if FM1_PLAITS_FAST_LPG_INVERSE_SIZE != 0 && \
    FM1_PLAITS_FAST_LPG_INVERSE_SIZE != 1
#error "FM1_PLAITS_FAST_LPG_INVERSE_SIZE must be 0 or 1"
#endif

/* Prepare OUT/AUX LPG SVF coefficients once and share the coefficients while
 * leaving each channel's filter history independent. */
#ifndef FM1_PLAITS_SHARED_LPG_COEFFICIENTS
#define FM1_PLAITS_SHARED_LPG_COEFFICIENTS 1
#endif
#if FM1_PLAITS_SHARED_LPG_COEFFICIENTS != 0 && \
    FM1_PLAITS_SHARED_LPG_COEFFICIENTS != 1
#error "FM1_PLAITS_SHARED_LPG_COEFFICIENTS must be 0 or 1"
#endif


#ifndef BR_PLAITS_KIT_KICK_SHARED_KERNEL
#define BR_PLAITS_KIT_KICK_SHARED_KERNEL 1
#endif

/* Candidate 211 selects the audited integer event gate. */
#ifndef FM1_PARTICLE_FAST_RANDOM_GATE
#define FM1_PARTICLE_FAST_RANDOM_GATE 1
#endif
#if FM1_PARTICLE_FAST_RANDOM_GATE != 0 && FM1_PARTICLE_FAST_RANDOM_GATE != 1
#error "FM1_PARTICLE_FAST_RANDOM_GATE must be 0 or 1"
#endif

/* Specialized early-out for the C adapter's unmodulated parameters. */
#ifndef FM1_PLAITS_FAST_ZERO_MODULATION
#define FM1_PLAITS_FAST_ZERO_MODULATION 1
#endif

/* Experimental one-step reciprocal only in the output limiter. Keep the
 * default path exact until target timing and all-model output checks pass. */
#ifndef FM1_PLAITS_FAST_LIMITER_RECIPROCAL
#define FM1_PLAITS_FAST_LIMITER_RECIPROCAL 0
#endif
#if FM1_PLAITS_FAST_LIMITER_RECIPROCAL != 0 && \
    FM1_PLAITS_FAST_LIMITER_RECIPROCAL != 1
#error "FM1_PLAITS_FAST_LIMITER_RECIPROCAL must be 0 or 1"
#endif

/* Cache converted CAPI controls when their integer values are unchanged. */
#ifndef FM1_PLAITS_CACHE_FLOAT_CONTROLS
#define FM1_PLAITS_CACHE_FLOAT_CONTROLS 1
#endif

/* Cache kit lane note-to-frequency conversions while the pitch is unchanged. */
#ifndef FM1_PLAITS_KIT_CACHE_NOTE_FREQUENCY
#define FM1_PLAITS_KIT_CACHE_NOTE_FREQUENCY 1
#endif

/* Cache exact drum-model pitch-ratio coefficients across unchanged render
 * controls. The default build retains the original coefficient path. */
#ifndef FM1_PLAITS_DRUM_CACHE_COEFFICIENTS
#define FM1_PLAITS_DRUM_CACHE_COEFFICIENTS 1
#endif

/* Exact cache for derived synthetic-kick block coefficients. The cache key is
 * the original render controls, so hits reuse the same float results without
 * changing per-sample processing or smoothing. */
#ifndef FM1_PLAITS_KIT_CACHE_DERIVED_KICK
#define FM1_PLAITS_KIT_CACHE_DERIVED_KICK 1
#endif
#if FM1_PLAITS_KIT_CACHE_DERIVED_KICK != 0 && \
    FM1_PLAITS_KIT_CACHE_DERIVED_KICK != 1
#error "FM1_PLAITS_KIT_CACHE_DERIVED_KICK must be 0 or 1"
#endif

/* Avoid BLEP event divisions in the stable hi-hat metallic oscillator bank.
 * The normal source path remains the default until target timing is measured. */
#ifndef FM1_PLAITS_HIHAT_FAST_BLEP_RECIPROCAL
#define FM1_PLAITS_HIHAT_FAST_BLEP_RECIPROCAL 1
#endif

/* Keep rarely-used initialization routines small without changing steady
 * render code. This is an opt-in Clang-only experiment for the embedded build. */
#ifndef FM1_PLAITS_COLD_INIT_SIZE
#define FM1_PLAITS_COLD_INIT_SIZE 1
#endif

#ifndef __has_attribute
#define __has_attribute(attribute) 0
#endif

#if FM1_PLAITS_COLD_INIT_SIZE
#if !__has_attribute(minsize)
#error "FM1_PLAITS_COLD_INIT_SIZE requires compiler support for minsize"
#endif
#define PLAITS_COLD_INIT_ATTR __attribute__((minsize, noinline))
#else
#define PLAITS_COLD_INIT_ATTR
#endif

#endif  // DINGHY_PLAITS_CONFIG_H_
