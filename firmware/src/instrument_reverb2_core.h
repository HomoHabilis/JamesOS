/* rv2 per-sample loop. SPDX-License-Identifier: GPL-3.0-only
 * Include once, from the file that drives it (rv2_ctl.c). rv2_core_run() is a
 * single static function with no calls out; every helper below is a macro:
 *   RV2_TZ15      Q15 product rounded toward zero (allpasses, end mode)
 *   RV2_SAT16     clamp to +-32767
 *   RV2_LN        per-line field access (SoA or struct-of-lines, rv2.h)
 * Tables it reads: rv2_off[], rv2_blen[] (SoA layout only), rv2_ap_off/len[].
 * Integer only. Products are 32x32->32 and bounded as commented. */
#ifndef RV2_CORE_H
#define RV2_CORE_H
#include "instrument_reverb2.h"

#define RV2_TZ15(p) (((p) + (((p) >> 31) & 32767)) >> 15)
#define RV2_SAT16(x) ((x) > 32767 ? 32767 : (x) < -32767 ? -32767 : (x))
#define RV2_KNEE 24575
#define RV2_OUT_MAX 65535

/* 8-bit logarithmic storage for the long pre-delay. Compact input keeps the
 * whole 1/8 note at 60 BPM in RAM; the short path remains 16-bit full-rate. */
static inline __attribute__((always_inline)) uint8_t rv2_sync_encode(int32_t x)
{
    uint32_t neg = x < 0, mag = (uint32_t)(x < 0 ? -x : x);
    uint32_t exponent, mantissa;
    mag += 132u; if (mag > 32767u) mag = 32767u;
    exponent = 31u - (uint32_t)__builtin_clz(mag | 255u) - 7u;
    mantissa = (mag >> (exponent + 3u)) & 15u;
    return (uint8_t)((neg << 7) | (exponent << 4) | mantissa);
}
static inline __attribute__((always_inline)) int32_t rv2_sync_decode(uint8_t v)
{
    int32_t mag = (int32_t)((((uint32_t)(v & 15u) << 3) + 132u) << ((v >> 4) & 7u)) - 132;
    return (v & 128u) ? -mag : mag;
}

#define RV2_O0 0
#define RV2_O1 (RV2_O0 + RV2_L0 + RV2_LINE_PAD)
#define RV2_O2 (RV2_O1 + RV2_L1 + RV2_LINE_PAD)
#define RV2_O3 (RV2_O2 + RV2_L2 + RV2_LINE_PAD)
#define RV2_O4 (RV2_O3 + RV2_L3 + RV2_LINE_PAD)
#define RV2_O5 (RV2_O4 + RV2_L4 + RV2_LINE_PAD)
#define RV2_O6 (RV2_O5 + RV2_L5 + RV2_LINE_PAD)
#define RV2_O7 (RV2_O6 + RV2_L6 + RV2_LINE_PAD)
#define RV2_OPRE (RV2_O7 + RV2_L7 + RV2_LINE_PAD)
#define RV2_OSW0 (RV2_OPRE + RV2_PRE_LEN)
#define RV2_OSW1 (RV2_OSW0 + RV2_SW0_LEN)
static const uint16_t rv2_off[RV2_LINES] RV2_TABLE(rv2_off) = {RV2_O0, RV2_O1, RV2_O2, RV2_O3, RV2_O4, RV2_O5, RV2_O6, RV2_O7};
static const uint16_t rv2_blen[RV2_LINES] RV2_TABLE(rv2_blen) = {
    RV2_L0 + RV2_LINE_PAD, RV2_L1 + RV2_LINE_PAD, RV2_L2 + RV2_LINE_PAD, RV2_L3 + RV2_LINE_PAD,
    RV2_L4 + RV2_LINE_PAD, RV2_L5 + RV2_LINE_PAD, RV2_L6 + RV2_LINE_PAD, RV2_L7 + RV2_LINE_PAD};
static const uint16_t rv2_ap_len[4] RV2_TABLE(rv2_ap_len) = {RV2_AP0_LEN, RV2_AP1_LEN, RV2_AP2_LEN, RV2_AP3_LEN};
static const uint16_t rv2_ap_off[4] RV2_TABLE(rv2_ap_off) = {0, RV2_AP0_LEN, RV2_AP0_LEN + RV2_AP1_LEN, RV2_AP0_LEN + RV2_AP1_LEN + RV2_AP2_LEN};
#if RV2_LINE_STRUCT
#define RV2_OFF(st, i) ((st)->ln[i].off)
#define RV2_BLEN(st, i) ((st)->ln[i].blen)
#else
#define RV2_OFF(st, i) (rv2_off[i])
#define RV2_BLEN(st, i) (rv2_blen[i])
#endif

/* Run `frames` input frames (even, <= 2*RV2_SUB) at 44.1 kHz. Sets s->loud
 * bit 0 when any line write exceeds 1 LSB, bit 1 when the network had input. */
static RV2_RAM void rv2_core_run(rv2_state_t *s, const int32_t *in, int32_t *out_l,
                                 int32_t *out_r, unsigned frames)
{
    int16_t *const mem = s->p16;
    const uint32_t end = s->end;
    uint32_t loud = s->loud;
    unsigned f;
    for (f = 0; f + 1u < frames; f += 2u) {
        int32_t r[8], v[8], o[8], x, i, ol, orr;
        /* ---- 2:1 decimation, 11-tap half-band (3, 0, -25, 0, 150, 256, ...)/512 */
        {
            int32_t *h = s->dec_h, x0 = in[f], x1 = in[f + 1u];
            x0 = x0 > 250000 ? 250000 : x0 < -250000 ? -250000 : x0;
            x1 = x1 > 250000 ? 250000 : x1 < -250000 ? -250000 : x1;
            for (i = 10; i >= 2; --i) h[i] = h[i - 2];   /* firmware: mirrored ring */
            h[1] = x0; h[0] = x1;
            /* |h| <= 250,000: 256*h + 150*2h ... < 2^28 */
            x = (256 * h[5] + 150 * (h[4] + h[6]) - 25 * (h[2] + h[8]) + 3 * (h[0] + h[10]) + 256) >> 9;
        }
        /* ---- send -> line domain (|x| < 2^18, gin_q12 < 2^12) */
        x = (x * s->gin_q12 + 2048) >> 12;
        x = RV2_SAT16(x);
        /* ---- short input diffusers, int32 pool, products toward zero */
        for (i = 0; i < 4; ++i) {
            int32_t *ab = s->p32 + rv2_ap_off[i];
            int32_t p = s->ap_pos[i], d = ab[p], g = s->ap_g[i >> 1];
            int32_t vv = x - RV2_TZ15(d * g);
            ab[p] = vv;
            x = d + RV2_TZ15(vv * g);
            if (++p == rv2_ap_len[i]) p = 0;
            s->ap_pos[i] = (uint16_t)p;
        }
        x = RV2_SAT16(x);
        /* The extra delay precedes the normal pre-delay and early reflections.
         * A zero extension is transparent, but the ring stays current so enabling
         * sync does not replay stale audio. */
        if (s->sync_pre) {
            uint32_t w = s->sync_pos, d = s->sync_d;
            if (!s->sync_mode) {
                int32_t delayed = x;
                if (d) delayed = s->sync_pre[w >= d ? w - d : w + RV2_SYNC_PRE_LEN - d];
                s->sync_pre[w] = (int16_t)x;
                if (++w == RV2_SYNC_PRE_LEN) w = 0u;
                s->sync_pos = (uint16_t)w;
                x = delayed;
            } else {
                /* 3:1, triangular anti-alias filter and linear reconstruction.
                 * The five input taps sum to nine; all arithmetic is bounded. */
                uint8_t *buf = (uint8_t *)s->sync_pre;
                int32_t h0 = s->sync_hist[0], h1 = s->sync_hist[1];
                int32_t h2 = s->sync_hist[2], h3 = s->sync_hist[3];
                uint32_t phase = s->sync_phase, age, q, rem, a, b;
                /* Do not let the tail manager clear audio still in this long
                 * delay while the network is waiting for it to arrive. */
                if (x) s->sync_pending = (uint16_t)(d + 1u);
                else if (s->sync_pending) --s->sync_pending;
                if (s->sync_pending) loud |= 3u;
                if (!phase) {
                    int32_t filtered = (x + 2*h0 + 3*h1 + 2*h2 + h3) / 9;
                    buf[w] = rv2_sync_encode(filtered);
                    if (++w == RV2_SYNC_PRE_COMP_LEN) w = 0u;
                    s->sync_pos = (uint16_t)w;
                }
                s->sync_hist[3] = (int16_t)h2; s->sync_hist[2] = (int16_t)h1;
                s->sync_hist[1] = (int16_t)h0; s->sync_hist[0] = (int16_t)x;
                s->sync_phase = (uint8_t)(phase == 2u ? 0u : phase + 1u);
                /* The filter itself contributes two core samples of delay. */
                age = d > phase + 2u ? d - phase - 2u : 0u;
                q = age / 3u; rem = age - q * 3u;
                a = w > q ? w - 1u - q : w + RV2_SYNC_PRE_COMP_LEN - 1u - q;
                b = a ? a - 1u : RV2_SYNC_PRE_COMP_LEN - 1u;
                x = (rv2_sync_decode(buf[a]) * (int32_t)(3u - rem) + rv2_sync_decode(buf[b]) * (int32_t)rem) / 3;
            }
        }
        /* ---- pre-delay (holds the diffused input) + early-reflection taps */
        {
            const int16_t *pb = mem + RV2_OPRE;
            int32_t w = s->pre_pos, ri = w - s->pre_d;
            if (ri < 0) ri += RV2_PRE_LEN;
            mem[RV2_OPRE + w] = (int16_t)x;
            x = pb[ri];
            if (s->er_g) {
                int32_t t0, t1, t2, t3, j;
                j = w - s->er_d[0]; if (j < 0) j += RV2_PRE_LEN; t0 = pb[j];
                j = w - s->er_d[1]; if (j < 0) j += RV2_PRE_LEN; t1 = pb[j];
                j = w - s->er_d[2]; if (j < 0) j += RV2_PRE_LEN; t2 = pb[j];
                j = w - s->er_d[3]; if (j < 0) j += RV2_PRE_LEN; t3 = pb[j];
                s->er_l = ((t0 + t1 - t2 + t3) * s->er_g) >> 10;   /* Q12 gain -> Q2 */
                s->er_r = ((t0 - t1 + t2 + t3) * s->er_g) >> 10;
            }
            if (++w == RV2_PRE_LEN) w = 0;
            s->pre_pos = (uint16_t)w;
        }
        /* ---- long swell allpasses, int16 pool (HALL/SPACE) */
        if (s->sw_g) {
            int32_t k;
            for (k = 0; k < 2; ++k) {
                int16_t *ab = mem + (k ? RV2_OSW1 : RV2_OSW0);
                int32_t p = s->sw_pos[k], d = ab[p], vv;
                vv = x - RV2_TZ15(d * s->sw_g);
                vv = RV2_SAT16(vv);
                ab[p] = (int16_t)vv;
                x = d + RV2_TZ15(vv * s->sw_g);
                if (++p == (k ? RV2_SW1_LEN : RV2_SW0_LEN)) p = 0;
                s->sw_pos[k] = (uint16_t)p;
            }
        }
        if (x) loud |= 3u;
        /* ---- line reads, Q2. First-order allpass fractional delay:
         * D = N + 0.5 + frac, eta = (0.5 - frac)/2 (Q15, |eta| <= 1/4),
         * y = eta (x[n-N] - y[-1]) + x[n-N-1]; unit magnitude at all eta. */
        for (i = 0; i < 8; ++i) {
            const int16_t *buf = mem + RV2_OFF(s, i);
            int32_t blen = RV2_BLEN(s, i), w = RV2_LN(s, i, wpos), dq = RV2_LN(s, i, d);
#if RV2_INT_READS
            if (i & 1) {
                int32_t i0 = w - ((dq + 32768) >> 16);
                if (i0 < 0) i0 += blen;
                r[i] = 4 * buf[i0];
            } else
#endif
            {
                int32_t dd = dq - 32768, N = dd >> 16, eta = (32768 - (dd & 0xFFFF)) >> 2;
                int32_t i0 = w - N, i1, y;
                if (i0 < 0) i0 += blen;
                i1 = i0 - 1; if (i1 < 0) i1 += blen;
                /* eta < 2^13, (4 buf - y) < 2^18 */
                y = ((eta * (4 * buf[i0] - RV2_LN(s, i, yap)) + 16384) >> 15) + 4 * buf[i1];
                RV2_LN(s, i, yap) = y;
                r[i] = y;
            }
            RV2_LN(s, i, d) = dq + RV2_LN(s, i, inc);
        }
        /* ---- outputs: orthogonal sign sets over all 8 lines; bloom: lines 4..7 */
        if (!s->bloom) {
            ol  = s->er_l + r[0] + r[1] - r[2] + r[3] - r[4] + r[5] + r[6] - r[7];
            orr = s->er_r + r[0] - r[1] + r[2] + r[3] + r[4] + r[5] - r[6] - r[7];
        } else {
            ol  = r[4] + r[5] - r[6] + r[7];
            orr = r[4] - r[5] + r[6] + r[7];
        }

        /* ---- loop shelf H = Gh + (Gl-Gh)(1-p)/(1-p z^-1), 1/sqrt8 folded in.
         * State s Q2. |b r|,|a s|,|dd r| < 2^30.5; halved before subtracting.
         * Normal: first-order error feedback. End: truncate new state to zero. */
        if (!end) {
            for (i = 0; i < 8; ++i) {
                int32_t acc = ((RV2_LN(s, i, b) * r[i]) >> 1) - ((RV2_LN(s, i, a) * RV2_LN(s, i, s)) >> 1) + RV2_LN(s, i, e);
                int32_t q = (acc + 8192) >> 14;
                RV2_LN(s, i, e) = acc - (q << 14);
                RV2_LN(s, i, s) += q;
                o[i] = RV2_LN(s, i, s) + ((RV2_LN(s, i, dd) * r[i] + 16384) >> 15);
            }
        } else {
            for (i = 0; i < 8; ++i) {
                int32_t t = (RV2_LN(s, i, s) << 14) + ((RV2_LN(s, i, b) * r[i]) >> 1) - ((RV2_LN(s, i, a) * RV2_LN(s, i, s)) >> 1);
                RV2_LN(s, i, s) = (t + ((t >> 31) & 16383)) >> 14;
                RV2_LN(s, i, e) = 0;
                o[i] = RV2_LN(s, i, s) + RV2_TZ15(RV2_LN(s, i, dd) * r[i]);
            }
        }
        /* ---- 8-point fast Walsh-Hadamard transform (unnormalised) */
        {
            int32_t a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3];
            int32_t a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
            int32_t b0 = a0 + a2, b2 = a0 - a2, b1 = a1 + a3, b3 = a1 - a3;
            int32_t b4 = a4 + a6, b6 = a4 - a6, b5 = a5 + a7, b7 = a5 - a7;
            v[0] = b0 + b4; v[4] = b0 - b4; v[1] = b1 + b5; v[5] = b1 - b5;
            v[2] = b2 + b6; v[6] = b2 - b6; v[3] = b3 + b7; v[7] = b3 - b7;
        }
        /* ---- shared high-pass on line 0: y = (1-c/2)(x-lz), lz += c(x-lz).
         * |v0| < 2^19, hp_c < 2^13. Bypassed and cleared in end mode. */
        if (!end) {
            int32_t dlt = v[0] - s->hp_lz, acc = s->hp_c * dlt + s->hp_e, q = (acc + 16384) >> 15;
            s->hp_e = acc - (q << 15);
            s->hp_lz += q;
            v[0] = dlt - (q >> 1);
        } else s->hp_lz = s->hp_e = 0;
        /* ---- inject input (sign pattern is not a Hadamard row) */
        x *= 4;
        v[0] += x; v[1] -= x; v[2] += x; v[3] += x;
        if (!s->bloom) { v[4] -= x; v[5] += x; v[6] -= x; v[7] -= x; }
        /* ---- Q2 -> int16 with error feedback (end: toward zero), soft knee, store */
        for (i = 0; i < 8; ++i) {
            int32_t w, p;
            if (!end) { int32_t t = v[i] + RV2_LN(s, i, es); w = (t + 2) >> 2; RV2_LN(s, i, es) = t - 4 * w; }
            else w = (v[i] + ((v[i] >> 31) & 3)) >> 2;
            if (w > RV2_KNEE) { int32_t ex = w - RV2_KNEE; w = ex >= 16384 ? 32767 : RV2_KNEE + ex - ((ex * ex) >> 15); }
            else if (w < -RV2_KNEE) { int32_t ex = -RV2_KNEE - w; w = ex >= 16384 ? -32767 : -RV2_KNEE - ex + ((ex * ex) >> 15); }
            loud |= (uint32_t)(w + 1) > 2u;
#ifdef RV2_HOST_DIAG
            { int32_t aw = (v[i] < 0 ? -v[i] : v[i]) >> 2; if (aw > s->peak_line) s->peak_line = aw; }
#endif
            p = RV2_LN(s, i, wpos);
            mem[RV2_OFF(s, i) + p] = (int16_t)w;
            if (++p == RV2_BLEN(s, i)) p = 0;
            RV2_LN(s, i, wpos) = (uint16_t)p;
        }
        { int32_t a0 = v[0] < 0 ? -v[0] : v[0]; if (a0 > s->blk_peak) s->blk_peak = a0; s->blk_abs += (uint32_t)a0; }
        /* ---- output gain, then width (|l| < 2^18, gout_q10 < 2^12, |width| <= 2^15) */
        {
            int32_t l = (ol + 2) >> 2, rr = (orr + 2) >> 2, lw, rw, m0, m1;
            l = (l * s->gout_q10 + 512) >> 10;
            rr = (rr * s->gout_q10 + 512) >> 10;
            lw = l + ((s->width_q15 * (rr >> 5)) >> 10);
            rw = rr + ((s->width_q15 * (l >> 5)) >> 10);
            /* ---- 1:2 interpolation: frame 0 = half-sample point, frame 1 = centre */
            {
                int32_t *a = s->up_l, *b = s->up_r;
                for (i = 5; i >= 1; --i) { a[i] = a[i - 1]; b[i] = b[i - 1]; }
                a[0] = lw; b[0] = rw;
#if RV2_UPSAMPLE_TAPS == 4
                m0 = (9 * (a[2] + a[3]) - (a[1] + a[4]) + 8) >> 4;
                m1 = (9 * (b[2] + b[3]) - (b[1] + b[4]) + 8) >> 4;
#else
                m0 = (150 * (a[2] + a[3]) - 25 * (a[1] + a[4]) + 3 * (a[0] + a[5]) + 128) >> 8;
                m1 = (150 * (b[2] + b[3]) - 25 * (b[1] + b[4]) + 3 * (b[0] + b[5]) + 128) >> 8;
#endif
                m0 = m0 > RV2_OUT_MAX ? RV2_OUT_MAX : m0 < -RV2_OUT_MAX ? -RV2_OUT_MAX : m0;
                m1 = m1 > RV2_OUT_MAX ? RV2_OUT_MAX : m1 < -RV2_OUT_MAX ? -RV2_OUT_MAX : m1;
                out_l[f] = m0; out_r[f] = m1;
                m0 = a[2] > RV2_OUT_MAX ? RV2_OUT_MAX : a[2] < -RV2_OUT_MAX ? -RV2_OUT_MAX : a[2];
                m1 = b[2] > RV2_OUT_MAX ? RV2_OUT_MAX : b[2] < -RV2_OUT_MAX ? -RV2_OUT_MAX : b[2];
                out_l[f + 1u] = m0; out_r[f + 1u] = m1;
            }
        }
    }
    s->loud = loud;
}
#endif
