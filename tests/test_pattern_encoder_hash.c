/* SPDX-License-Identifier: GPL-3.0-only
 * Hash the PSC3 encoders' output (firmware/src/pattern_storage_codec.c:
 * fm1_pattern_codec_encode, fm1_pattern_codec_encode_slice; pattern_codec_source.c:
 * fm1_codec_source_encode) over many inputs: lengths 0..4400 and 84480, zeros,
 * runs, random bytes, sparse step data, repeated blocks and a few large images.
 * Each line: FNV-1a 64 over (return code, encoded length, output bytes).
 * Slices: every (offset, count) pair for the small inputs, sliding windows of
 * several widths for the rest. A source read that fails is also exercised. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pattern_storage_codec.h"
#include "pattern_codec_source.h"

static uint64_t h_full = 1469598103934665603ull, h_slice = 1469598103934665603ull,
                h_src = 1469598103934665603ull, h_dec = 1469598103934665603ull;
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{ const uint8_t *b = p; while (n--) h = (h ^ *b++) * 1099511628211ull; return h; }
static uint64_t fnv_word(uint64_t h, int64_t v) { return fnv(h, &v, sizeof v); }

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

typedef struct { const uint8_t *p; uint32_t n; uint32_t fail_at; } mem_src;
static int mem_read(void *v, uint32_t off, void *dst, uint32_t n)
{
    mem_src *s = v;
    if (off > s->n || n > s->n - off) return -1;
    if (s->fail_at && off + n > s->fail_at) return -1;
    memcpy(dst, s->p + off, n);
    return 0;
}

static uint8_t outbuf[200000], outbuf2[200000];

static void run(const uint8_t *in, size_t n, int all_slices)
{
    size_t need = 0, w = 0, off, cnt;
    int rc = fm1_pattern_codec_encode(in, n, 0, 0, &need);
    h_full = fnv_word(h_full, rc); h_full = fnv_word(h_full, (int64_t)need);
    rc = fm1_pattern_codec_encode(in, n, outbuf, need ? need - 1 : 0, &w);   /* FULL */
    h_full = fnv_word(h_full, rc); h_full = fnv_word(h_full, (int64_t)w);
    memset(outbuf, 0xA5, need + 16);
    rc = fm1_pattern_codec_encode(in, n, outbuf, sizeof outbuf, &w);
    h_full = fnv_word(h_full, rc); h_full = fnv_word(h_full, (int64_t)w);
    h_full = fnv(h_full, outbuf, need + 16);
    if (rc == FM1_CODEC_OK) {     /* the encoded stream decodes back */
        static uint8_t back[200000];
        int d = fm1_pattern_codec_decode(outbuf, w, back, n);
        h_dec = fnv_word(h_dec, d);
        h_dec = fnv_word(h_dec, d == FM1_CODEC_OK && !memcmp(back, in, n));
    }
    /* slices */
    if (all_slices) {
        for (off = 0; off <= need; ++off)
            for (cnt = 0; cnt <= need - off; ++cnt) {
                memset(outbuf2, 0x5A, cnt + 2);
                rc = fm1_pattern_codec_encode_slice(in, n, off, cnt ? outbuf2 : 0, cnt, &w);
                h_slice = fnv_word(h_slice, rc); h_slice = fnv_word(h_slice, (int64_t)w);
                h_slice = fnv(h_slice, outbuf2, cnt + 2);
            }
    } else {
        static const size_t widths[] = {1, 5, 64, 255, 1000};
        unsigned k;
        for (k = 0; k < sizeof widths / sizeof widths[0]; ++k)
            for (off = 0; off <= need + 2; off += widths[k] + (need > 4000 ? 97u : 0u)) {
                cnt = widths[k];
                memset(outbuf2, 0x5A, cnt + 2);
                rc = fm1_pattern_codec_encode_slice(in, n, off, outbuf2, cnt, &w);
                h_slice = fnv_word(h_slice, rc); h_slice = fnv_word(h_slice, (int64_t)w);
                h_slice = fnv(h_slice, outbuf2, cnt + 2);
            }
    }
    /* source encoder over the same bytes, also with a failing read */
    if (n <= 84480u) {
        mem_src s = {in, (uint32_t)n, 0};
        memset(outbuf2, 0xC3, need + 16);
        rc = fm1_codec_source_encode(mem_read, &s, (uint32_t)n, outbuf2, sizeof outbuf2, &w);
        h_src = fnv_word(h_src, rc); h_src = fnv_word(h_src, (int64_t)w);
        h_src = fnv(h_src, outbuf2, need + 16);
        h_src = fnv_word(h_src, rc == FM1_CODEC_OK && w == need && !memcmp(outbuf, outbuf2, need));
        rc = fm1_codec_source_encode(mem_read, &s, (uint32_t)n, outbuf2, need ? need - 1 : 0, &w);
        h_src = fnv_word(h_src, rc); h_src = fnv_word(h_src, (int64_t)w);
        if (n > 8) {
            s.fail_at = (uint32_t)(n / 2);
            rc = fm1_codec_source_encode(mem_read, &s, (uint32_t)n, outbuf2, sizeof outbuf2, &w);
            h_src = fnv_word(h_src, rc);
        }
    }
}

int main(void)
{
    static uint8_t in[84480];
    size_t n, i;
    for (n = 0; n < 70; ++n) {                        /* small: every slice */
        for (i = 0; i < n; ++i) in[i] = (uint8_t)(rnd() % (n % 3 ? 4u : 256u));
        run(in, n, 1);
    }
    for (n = 70; n <= 4400; n += 37) {
        unsigned kind = (unsigned)(n % 5u);
        for (i = 0; i < n; ++i) {
            switch (kind) {
            case 0: in[i] = 0; break;
            case 1: in[i] = (uint8_t)rnd(); break;
            case 2: in[i] = (rnd() % 9u) ? 0 : (uint8_t)(rnd() % 128u); break;     /* sparse steps */
            case 3: in[i] = (uint8_t)((i / 7u) & 0xFFu); break;                  /* runs */
            default: in[i] = i >= 300u ? in[i - 300u - (rnd() % 3u)] : (uint8_t)rnd(); break;  /* matches */
            }
        }
        run(in, n, 0);
    }
    for (i = 0; i < sizeof in; ++i) in[i] = (rnd() % 5u) ? 0 : (uint8_t)rnd();
    run(in, sizeof in, 0);
    for (i = 0; i < sizeof in; ++i) in[i] = (uint8_t)rnd();
    run(in, 9000, 0);
    printf("full %016llx\nslice %016llx\nsource %016llx\nroundtrip %016llx\n",
           (unsigned long long)h_full, (unsigned long long)h_slice,
           (unsigned long long)h_src, (unsigned long long)h_dec);
    return 0;
}
