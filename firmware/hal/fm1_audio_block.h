/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_AUDIO_BLOCK_H
#define FM1_AUDIO_BLOCK_H
#include "fm1_audio_rate.h"

/* Optional internal scheduling experiments. Physical DMA geometry stays
 * fixed; all engine consumers include this shared selector. */
#ifndef BR_AUDIO_INTERNAL_BLOCK160
#define BR_AUDIO_INTERNAL_BLOCK160 1
#endif
#ifndef BR_AUDIO_INTERNAL_BLOCK240
#define BR_AUDIO_INTERNAL_BLOCK240 0
#endif
#ifndef BR_AUDIO_INTERNAL_BLOCK64
#define BR_AUDIO_INTERNAL_BLOCK64 0
#endif
#ifndef BR_AUDIO_INTERNAL_BLOCK256
#define BR_AUDIO_INTERNAL_BLOCK256 0
#endif
#ifndef BR_AUDIO_INTERNAL_BLOCK128
#if BR_AUDIO_INTERNAL_BLOCK64 || BR_AUDIO_INTERNAL_BLOCK160 || \
    BR_AUDIO_INTERNAL_BLOCK240 || BR_AUDIO_INTERNAL_BLOCK256
#define BR_AUDIO_INTERNAL_BLOCK128 0
#else
#define BR_AUDIO_INTERNAL_BLOCK128 0
#endif
#endif

#if (BR_AUDIO_INTERNAL_BLOCK160 + BR_AUDIO_INTERNAL_BLOCK240 + BR_AUDIO_INTERNAL_BLOCK64 + \
     BR_AUDIO_INTERNAL_BLOCK128 + BR_AUDIO_INTERNAL_BLOCK256) > 1
#error "select only one experimental internal audio block size"
#elif BR_AUDIO_INTERNAL_BLOCK160
#define BR_AUDIO_BLOCK_FRAMES 160u
#elif BR_AUDIO_INTERNAL_BLOCK240
#define BR_AUDIO_BLOCK_FRAMES 240u
#elif BR_AUDIO_INTERNAL_BLOCK256
#define BR_AUDIO_BLOCK_FRAMES 256u
#elif BR_AUDIO_INTERNAL_BLOCK128
#define BR_AUDIO_BLOCK_FRAMES 128u
#elif BR_AUDIO_INTERNAL_BLOCK64
#define BR_AUDIO_BLOCK_FRAMES 64u
#else
#define BR_AUDIO_BLOCK_FRAMES 32u
#endif

#if FM1_NATIVE_OUTPUT_32K && BR_AUDIO_BLOCK_FRAMES != 32u && BR_AUDIO_BLOCK_FRAMES != 160u
#error "Native 32 kHz profile requires a 32- or 160-frame engine block."
#endif
#if FM1_NATIVE_OUTPUT_48K && BR_AUDIO_BLOCK_FRAMES != 240u
#error "Native 48 kHz profile requires the 240-frame engine block."
#endif

#endif
