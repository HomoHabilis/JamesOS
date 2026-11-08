/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The few libc routines the compiler may call implicitly (struct copies,
 * zeroing). Freestanding: no other libc. */
#include <stdint.h>
#include <stddef.h>

#ifndef FM1_LIBC_WORD_BULK
#define FM1_LIBC_WORD_BULK 1
#endif

#if FM1_LIBC_WORD_BULK
/* Ordinary RAM only, as required by these non-volatile libc interfaces.
 * Never form an unaligned word access or read outside the requested span. */
typedef uint32_t fm1_alias_word_t __attribute__((__may_alias__));
static void fm1_copy_forward(unsigned char *p, const unsigned char *q, size_t n)
{
    if (n >= 16u && (((uintptr_t)p ^ (uintptr_t)q) & 3u) == 0u) {
        while ((uintptr_t)p & 3u) { *p++ = *q++; --n; }
        fm1_alias_word_t *w = (fm1_alias_word_t *)p;
        const fm1_alias_word_t *v = (const fm1_alias_word_t *)q;
        while (n >= 16u) {
            w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; w[3] = v[3];
            w += 4; v += 4; n -= 16u;
        }
        while (n >= 4u) { *w++ = *v++; n -= 4u; }
        p = (unsigned char *)w; q = (const unsigned char *)v;
    }
    while (n--) *p++ = *q++;
}
void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    if (n >= 16u) {
        uint32_t fill = (unsigned char)c;
        fill |= fill << 8; fill |= fill << 16;
        while ((uintptr_t)p & 3u) { *p++ = (unsigned char)c; --n; }
        fm1_alias_word_t *w = (fm1_alias_word_t *)p;
        while (n >= 16u) {
            w[0] = fill; w[1] = fill; w[2] = fill; w[3] = fill;
            w += 4; n -= 16u;
        }
        while (n >= 4u) { *w++ = fill; n -= 4u; }
        p = (unsigned char *)w;
    }
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    fm1_copy_forward(d, s, n);
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    uintptr_t dst = (uintptr_t)p, src = (uintptr_t)q;
    if (dst == src || n == 0u) return d;
    if (dst < src || dst - src >= n) {
        /* Use our overlap-safe forward kernel, not the memcpy contract. */
        fm1_copy_forward(p, q, n);
    } else {
        p += n; q += n;
        if (n >= 16u && (((uintptr_t)p ^ (uintptr_t)q) & 3u) == 0u) {
            while ((uintptr_t)p & 3u) { *--p = *--q; --n; }
            fm1_alias_word_t *w = (fm1_alias_word_t *)p;
            const fm1_alias_word_t *v = (const fm1_alias_word_t *)q;
            while (n >= 16u) {
                w[-1] = v[-1]; w[-2] = v[-2];
                w[-3] = v[-3]; w[-4] = v[-4];
                w -= 4; v -= 4; n -= 16u;
            }
            while (n >= 4u) { *--w = *--v; n -= 4u; }
            p = (unsigned char *)w; q = (const unsigned char *)v;
        }
        while (n--) *--p = *--q;
    }
    return d;
}
#else
void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    uintptr_t dst = (uintptr_t)p;
    uintptr_t src = (uintptr_t)q;
    unsigned i;

    if (dst == src || n == 0u)
        return d;
    if (dst < src || dst - src >= n) {
        for (i = 0; i < n; i++)
            p[i] = q[i];
    } else {
        while (n--)
            p[n] = q[n];
    }
    return d;
}

#endif

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

/* decimal with an optional fixed point: fmt_fix(buf, -63, 1) -> "-6.3" */
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}

static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
