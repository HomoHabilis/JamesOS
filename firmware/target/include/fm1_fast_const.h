/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional target-only placement for read-mostly audio tables. */
#ifndef FM1_FAST_CONST_H
#define FM1_FAST_CONST_H

#if defined(FM1_DEVICE) && defined(__APPLE__)
#define FM1_FAST_CONST __attribute__((section("__DATA,fast_const"), used))
#elif defined(FM1_DEVICE)
#define FM1_FAST_CONST __attribute__((section(".data.fast_const"), used))
#else
#define FM1_FAST_CONST
#endif

#endif
