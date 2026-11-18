/* SPDX-License-Identifier: GPL-3.0-only */
/* Small helpers that both the main program (dinghy.c) and the separately
 * compiled audio unit (instrument_audio_unit.c) call. On the split device
 * build they use the C99 inline model: either unit may still inline a call,
 * and every out-of-line call reaches the one external copy that dinghy.c
 * provides (its extern declarations), instead of one static copy per unit.
 * Everywhere else (host, tests, unsplit builds) they stay static inline. */
#ifndef BR_SHARED_INLINE_H
#define BR_SHARED_INLINE_H
#if defined(FM1_DEVICE) && defined(BR_SPLIT_AUDIO)
#define BR_SHARED_INLINE_EXTERN 1
#define BR_SHARED_INLINE inline
#else
#define BR_SHARED_INLINE_EXTERN 0
#define BR_SHARED_INLINE static inline
#endif
#endif
