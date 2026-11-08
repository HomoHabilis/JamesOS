/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_AUDIO_RATE_H
#define FM1_AUDIO_RATE_H

/* Experimental native-rate profiles. Defaults preserve the existing 32 kHz
 * firmware selection; a profile must be selected explicitly by its build. */
#ifndef FM1_NATIVE_OUTPUT_32K
#define FM1_NATIVE_OUTPUT_32K 1
#endif
#ifndef FM1_NATIVE_OUTPUT_48K
#define FM1_NATIVE_OUTPUT_48K 0
#endif
#ifndef FM1_NATIVE32_EXPERIMENTAL_CLOCK
#define FM1_NATIVE32_EXPERIMENTAL_CLOCK 1
#endif
#ifndef FM1_NATIVE48_EXPERIMENTAL_CLOCK
#define FM1_NATIVE48_EXPERIMENTAL_CLOCK 0
#endif

#if FM1_NATIVE_OUTPUT_32K && FM1_NATIVE_OUTPUT_48K
#error "Select only one native output-rate profile"
#endif

#if FM1_NATIVE_OUTPUT_32K
#if defined(FM1_DEVICE) && !FM1_NATIVE32_EXPERIMENTAL_CLOCK
#error "Native 32 kHz target builds require explicit FM1_NATIVE32_EXPERIMENTAL_CLOCK=1 after reviewing the SDK-derived clock recipe."
#endif
#define FM1_AUDIO_RATE_HZ 32000u
#define FM1_AUDIO_HALF_FRAMES 160u
#define FM1_AUDIO_HALF_TICKS 120000u /* 5 ms at the 24 MHz T4 clock. */
#define FM1_AUDIO_INTERNAL_RATE_HZ 16000u
/* Provisional only: target-specific post-worker/finalizer reserve has not
 * been measured at this geometry. Keep time out of worker scheduling for the
 * finalizer, but do not treat this as a qualification value. */
#ifndef FM1_NATIVE32_TAIL_RESERVE_TICKS
#define FM1_NATIVE32_TAIL_RESERVE_TICKS 12000u
#endif
#elif FM1_NATIVE_OUTPUT_48K
#if defined(FM1_DEVICE) && !FM1_NATIVE48_EXPERIMENTAL_CLOCK
#error "Native 48 kHz target builds require explicit FM1_NATIVE48_EXPERIMENTAL_CLOCK=1 after reviewing the clock recipe."
#endif
#define FM1_AUDIO_RATE_HZ 48000u
#define FM1_AUDIO_HALF_FRAMES 240u
#define FM1_AUDIO_HALF_TICKS 120000u /* 5 ms at the 24 MHz T4 clock. */
#define FM1_AUDIO_INTERNAL_RATE_HZ 24000u
#else
#define FM1_AUDIO_RATE_HZ 44100u
#define FM1_AUDIO_HALF_FRAMES 256u
#define FM1_AUDIO_HALF_TICKS 139319u
#define FM1_AUDIO_INTERNAL_RATE_HZ 22050u
#endif

#endif
