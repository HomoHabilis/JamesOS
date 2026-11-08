/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DINGHY_AUDIO_LEVEL_H
#define DINGHY_AUDIO_LEVEL_H
#include <stdint.h>

#ifndef FM1_AUDIO_GAIN_BLOCK_SNAPSHOT
#define FM1_AUDIO_GAIN_BLOCK_SNAPSHOT 1
#endif

/* Optional exact shortcuts for the two common endpoint gains. Keep disabled
 * by default until target timing is measured. */
#ifndef FM1_AUDIO_GAIN_Q12_FASTPATH
#define FM1_AUDIO_GAIN_Q12_FASTPATH 1
#endif
#if FM1_AUDIO_GAIN_Q12_FASTPATH != 0 && FM1_AUDIO_GAIN_Q12_FASTPATH != 1
#error "FM1_AUDIO_GAIN_Q12_FASTPATH must be 0 or 1"
#endif

/* Gain is normally 0..4096. Keep hot FX/makeup samples wide until the final
 * clip. The sign-dependent bias makes right-shift rounding symmetric to
 * nearest, with exact half-way cases rounded away from zero. */
static inline __attribute__((always_inline)) int32_t fm1_scale_q12(int32_t sample, uint32_t gain)
{
    /* For the bounded interval, the Q12 product plus rounding bias fits in
     * int32_t at every supported gain. Preserve the wide fallback for all
     * other inputs and out-of-contract gains. */
    if (gain <= 4096u && (uint32_t)sample + 524288u <= 1048575u) {
        int32_t product = sample * (int32_t)gain;
        product += product < 0 ? 2047 : 2048;
        return product >> 12;
    }
    {
        int64_t product = (int64_t)sample * gain;
        product += product < 0 ? 2047 : 2048;
        return (int32_t)(product >> 12);
    }
}

/* The audio ISR runs on CPU0. Its normal gain writer is the foreground ADC
 * poller, which cannot run until this block returns. The diagnostic writer is
 * admitted only while stopped and quiet. Snapshotting once therefore retains
 * the same effective gain for every sample in the block while avoiding one
 * volatile global load per sample. The default path deliberately preserves
 * the original per-sample read semantics for A/B qualification. */
static inline void fm1_scale_q12_clip_block(int32_t *samples, uint32_t count,
                                            const volatile int32_t *gain_q12)
{
    uint32_t i;
#if FM1_AUDIO_GAIN_BLOCK_SNAPSHOT
    const uint32_t gain = (uint32_t)*gain_q12;
#if FM1_AUDIO_GAIN_Q12_FASTPATH
    /* These are exactly equivalent to Q12 multiply then clipping. Keep the
     * block loop and its caller intact: UAC taps and commit accounting still
     * run after this helper, including for a zero-gain block. */
    if (gain == 0u) {
        for (i = 0u; i < count; ++i) samples[i] = 0;
        return;
    }
    if (gain == 4096u) {
        for (i = 0u; i < count; ++i) {
            const int32_t sample = samples[i];
            samples[i] = sample > 32767 ? 32767 :
                         sample < -32767 ? -32767 : sample;
        }
        return;
    }
#endif
#endif
    for (i = 0u; i < count; ++i) {
#if FM1_AUDIO_GAIN_BLOCK_SNAPSHOT
        int32_t value = fm1_scale_q12(samples[i], gain);
#else
        int32_t value = fm1_scale_q12(samples[i], (uint32_t)*gain_q12);
#endif
        samples[i] = value > 32767 ? 32767 : value < -32767 ? -32767 : value;
    }
}
#endif
