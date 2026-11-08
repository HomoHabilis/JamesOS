/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_BRAIDS_WT_WAVES_PACK_H_
#define FM1_BRAIDS_WT_WAVES_PACK_H_

#include <stdint.h>

/* Decode Braids' immutable wavetable into its existing indexed symbol during
 * the pre-CPU1 resource initialization. Keep this opt-in until target memory
 * and startup timing are validated. */
#ifndef FM1_BRAIDS_PACK_WT_WAVES
#define FM1_BRAIDS_PACK_WT_WAVES 1
#endif
#if FM1_BRAIDS_PACK_WT_WAVES != 0 && FM1_BRAIDS_PACK_WT_WAVES != 1
#error "FM1_BRAIDS_PACK_WT_WAVES must be zero or one"
#endif

#define FM1_BRAIDS_WT_WAVES_COUNT 256u
#define FM1_BRAIDS_WT_WAVE_SAMPLES 129u
#define FM1_BRAIDS_WT_WAVES_SAMPLE_COUNT 33024u
#define FM1_BRAIDS_WT_WAVES_PACKED_BYTES 27034u
#define FM1_BRAIDS_WT_WAVES_CRC32 0x97c79c9cu

#if FM1_BRAIDS_WT_WAVES_SAMPLE_COUNT != \
    FM1_BRAIDS_WT_WAVES_COUNT * FM1_BRAIDS_WT_WAVE_SAMPLES
#error "Braids wavetable dimensions do not match the packed format"
#endif

#if defined(__clang__)
#define FM1_BRAIDS_WT_WAVES_INIT_ATTR __attribute__((minsize, noinline))
#elif defined(__GNUC__)
#define FM1_BRAIDS_WT_WAVES_INIT_ATTR \
  __attribute__((optimize("Os"), noinline))
#else
#define FM1_BRAIDS_WT_WAVES_INIT_ATTR
#endif

#if FM1_BRAIDS_PACK_WT_WAVES
#ifdef __cplusplus
extern "C" {
#endif
/* Called from fm1_plaits_waves_init(), before CPU1 can access resources. */
int fm1_braids_wt_waves_init(void);
#ifdef __cplusplus
}
#endif
#endif

#endif  // FM1_BRAIDS_WT_WAVES_PACK_H_
