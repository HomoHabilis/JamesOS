/* rv2 control/block-rate side. SPDX-License-Identifier: GPL-3.0-only
 * rv2_design: control task. Float arithmetic, the only library call is sqrtf
 *   (exp2/log2 are local polynomials). About 4k flops per call (see report).
 * rv2_apply / rv2_process / end-of-tail bookkeeping: block rate, flash. */
#include "instrument_reverb2_core.h"
#include <math.h>     /* sqrtf only */
#include <string.h>

/* ---- per-mode tables ---------------------------------------------------- */
typedef struct {
    float rt0, rt64, rt127;     /* mid-band (1 kHz) RT60 at DECAY 0/64/127, s */
    float sc0, sc127;           /* line length scale over SIZE */
    float pre0, pre127;         /* pre-delay ms over SIZE */
    float hf0, hf127;           /* RT(4k)/RT(1k) over SIZE */
    float lf;                   /* RT(DC)/RT(1k) before the shared high-pass */
    float lowcut;               /* shared in-loop HP corner, Hz */
    float mod_lo, mod_hi;       /* LFO depth (core samples) at DECAY <= 64 / >= 96 */
    float mod_rate;             /* Hz (second LFO at 1.37x) */
    float apg_a, apg_b;         /* short input diffusers, pairs 0/1 and 2/3 */
    float swell;                /* long input allpasses (0 = bypass) */
    int bloom;                  /* 1: input to lines 0-3, output from 4-7 */
    float width_k;              /* L/R cross-feed (+ narrows, - widens) */
    float er_gain, er_ms;       /* early taps: gain, span at line scale 1 */
    float gin, gout;            /* send -> line, line sum -> wet return */
    float lvl0, lvl64, lvl127;  /* input trim (dB) over SIZE */
    float comp;                 /* level compensation exponent vs DECAY */
} rv2_mode_t;

static const rv2_mode_t rv2_modes[4] = {
    [0] = { .rt0 = 0.25f, .rt64 = 0.9f, .rt127 = 6.0f, .sc0 = 0.38f, .sc127 = 0.68f,
            .pre0 = 0.0f, .pre127 = 12.0f, .hf0 = 0.75f, .hf127 = 0.65f, .lf = 1.10f,
            .lowcut = 30.0f, .mod_lo = 8.0f, .mod_hi = 12.0f, .mod_rate = 0.35f,
            .apg_a = 0.60f, .apg_b = 0.50f, .swell = 0.0f, .bloom = 0, .width_k = 0.12f,
            .er_gain = 1.5f, .er_ms = 30.0f, .gin = 0.0355f, .gout = 2.40f,
            .lvl0 = -1.9f, .lvl64 = 0.4f, .lvl127 = 0.7f, .comp = 0.30f },
    [1] = { .rt0 = 0.8f, .rt64 = 2.8f, .rt127 = 20.0f, .sc0 = 0.55f, .sc127 = 1.00f,
            .pre0 = 10.0f, .pre127 = 45.0f, .hf0 = 0.50f, .hf127 = 0.42f, .lf = 1.20f,
            .lowcut = 40.0f, .mod_lo = 8.0f, .mod_hi = 11.0f, .mod_rate = 0.40f,
            .apg_a = 0.70f, .apg_b = 0.60f, .swell = 0.50f, .bloom = 0, .width_k = 0.05f,
            .er_gain = 0.5f, .er_ms = 25.0f, .gin = 0.0265f, .gout = 2.40f,
            .lvl0 = 0.3f, .lvl64 = 0.1f, .lvl127 = 1.0f, .comp = 0.30f },
    [3] = { .rt0 = 2.5f, .rt64 = 8.0f, .rt127 = 60.0f, .sc0 = 0.75f, .sc127 = 1.00f,
            .pre0 = 20.0f, .pre127 = 60.0f, .hf0 = 0.38f, .hf127 = 0.32f, .lf = 1.00f,
            .lowcut = 100.0f, .mod_lo = 16.0f, .mod_hi = 16.0f, .mod_rate = 0.35f,
            .apg_a = 0.70f, .apg_b = 0.60f, .swell = 0.70f, .bloom = 1, .width_k = -0.08f,
            .er_gain = 0.0f, .er_ms = 0.0f, .gin = 0.0355f, .gout = 3.39f,
            .lvl0 = -0.4f, .lvl64 = -0.9f, .lvl127 = 0.5f, .comp = 0.30f },
};
static const uint16_t rv2_len[RV2_LINES] = {RV2_L0, RV2_L1, RV2_L2, RV2_L3, RV2_L4, RV2_L5, RV2_L6, RV2_L7};

static const rv2_mode_t *mode_of(unsigned t) { return &rv2_modes[(t == 1u || t == 3u) ? t : 0u]; }

/* Helpers called from many places in rv2_design (control rate, flash). Out of line
 * they exist once; the vendor compiler otherwise inlines every call even under -Oz.
 * Strict floating point (no contraction, no fast math), so results are unchanged. */
#ifndef RV2_OUTLINE
#define RV2_OUTLINE __attribute__((noinline, minsize))
#endif

/* ---- local exp2/log2 (no libm beyond sqrtf); rel. error < 2e-7 ---------- */
typedef union { float f; uint32_t u; } rv2_fu;
static RV2_OUTLINE float rv2_log2(float x)
{
    rv2_fu v; float m, t, t2;
    int e;
    if (!(x > 0.0f)) return -150.0f;
    v.f = x;
    e = (int)((v.u >> 23) & 255u) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
    m = v.f;
    if (m > 1.41421356f) { m *= 0.5f; ++e; }
    t = (m - 1.0f) / (m + 1.0f); t2 = t * t;
    return (float)e + 2.88539008f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * (0.14285714f + t2 * 0.11111111f))));
}
static RV2_OUTLINE float rv2_exp2(float x)
{
    rv2_fu v; int i; float y;
    if (x < -126.0f) return 0.0f;
    if (x > 126.0f) x = 126.0f;
    i = (int)x; if ((float)i > x) --i;
    y = (x - (float)i) * 0.69314718f;
    v.u = (uint32_t)(i + 127) << 23;
    return v.f * (1.0f + y * (1.0f + y * (0.5f + y * (0.16666667f + y * (0.041666667f + y * (0.0083333333f
                 + y * (0.0013888889f + y * (0.00019841270f + y * 0.000024801587f))))))));
}
#define LOG2_10 3.32192809f
#define LOG2_E 1.44269504f
/* trig of the design frequencies at 22,050 Hz (constants, no sin/cos at run time) */
#define C1K 0.95967523f
#define S1K 0.28111111f
#define SH1K 0.14199432f   /* sin(w/2) at 1 kHz */
#define C3K 0.65632781f
#define S3K 0.75447585f

static float rt_of(const rv2_mode_t *m, unsigned decay)
{
    /* quadratic in log2(RT) through (0,rt0), (64,rt64), (127,rt127) */
    float y0 = rv2_log2(m->rt0), y1 = rv2_log2(m->rt64), y2 = rv2_log2(m->rt127);
    float s1 = (y1 - y0) / 64.0f, s2 = (y2 - y1) / 63.0f;
    float c = (s2 - s1) / 127.0f, b = s1 - c * 64.0f, d = (float)decay;
    return rv2_exp2(y0 + b * d + c * d * d);
}
RV2_API float rv2_target_rt(unsigned type, unsigned decay) { return rt_of(mode_of(type), decay); }

static unsigned gcd_u(unsigned a, unsigned b) { while (b) { unsigned t = a % b; a = b; b = t; } return a; }

static float held_energy(float rt)   /* energy of a 2 s held note, for level compensation */
{
    float tau = rt / (6.0f * 2.302585f);
    return tau * (1.0f - rv2_exp2(-2.0f / tau * LOG2_E));
}

/* P(w) = (1-p)/(1 - p e^-jw) for the two design frequencies */
static void pw(float p, float c, float sn, float *pr, float *pi)
{
    float dr = 1.0f - p * c, di = p * sn, den = dr * dr + di * di;
    *pr = (1.0f - p) * dr / den; *pi = -(1.0f - p) * di / den;
}
/* Gh such that |Gl P1 + Gh (1-P1)| = g at 1 kHz; returns -1 if none in [0, Gl] */
static RV2_OUTLINE float solve_gh(float p, float Gl, float g)
{
    float pr, pi, Ar, Ai, Br, Bi, qa, qb, qc, disc, gh;
    pw(p, C1K, S1K, &pr, &pi);
    Ar = Gl * pr; Ai = Gl * pi; Br = 1.0f - pr; Bi = -pi;
    qa = Br * Br + Bi * Bi; qb = 2.0f * (Ar * Br + Ai * Bi); qc = Ar * Ar + Ai * Ai - g * g;
    disc = qb * qb - 4.0f * qa * qc;
    if (qa < 1e-12f || disc < 0.0f) return -1.0f;
    gh = (-qb + sqrtf(disc)) / (2.0f * qa);
    if (gh > Gl && gh < Gl * 1.002f) gh = Gl;          /* flat shelf: rounding */
    return (gh < 0.0f || gh > Gl) ? -1.0f : gh;
}
/* cost of pole p: |hf_got - hf|/hf, for mid gain g (log2 lg) and DC gain Gl.
 * About 65 flops, 2 divisions, 2 sqrtf. */
static RV2_OUTLINE float shelf_cost(float p, float lg, float g, float Gl, float hf)
{
    float gh = solve_gh(p, Gl, g), pr, pi, hr, hi, h3, l3, d;
    if (gh < 0.0f) return 1e9f;
    pw(p, C3K, S3K, &pr, &pi);
    hr = Gl * pr + gh * (1.0f - pr); hi = (Gl - gh) * pi;
    h3 = sqrtf(hr * hr + hi * hi);
    l3 = rv2_log2(h3 < 0.99999f ? h3 : 0.99999f);
    d = lg / l3 - hf;
    return (d < 0.0f ? -d : d) / hf;
}

RV2_API void rv2_design(unsigned type, unsigned size, unsigned decay, const rv2_overrides_t *ov, rv2_coeffs_t *c)
{
    const rv2_mode_t *m = mode_of(type);
    static const rv2_overrides_t none = {RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE, RV2_DERIVE};
    unsigned i, j, len[RV2_LINES];
    float fs = (float)RV2_CORE_FS, u, rt, sc, hf, lowcut, depth, pre_ms, wk, Lref = 0.0f;
    if (!ov) ov = &none;
    if (size > 127u) size = 127u;
    if (decay > 127u) decay = 127u;
    u = (float)size / 127.0f;
    rt = rt_of(m, decay);
    sc = m->sc0 + (m->sc127 - m->sc0) * u;
    hf = ov->tone <= 127u ? 0.25f + 0.70f * (float)ov->tone / 127.0f : m->hf0 + (m->hf127 - m->hf0) * u;
    lowcut = ov->lowcut <= 127u ? 10.0f * rv2_exp2((float)ov->lowcut / 127.0f * 4.9068906f) : m->lowcut;
    if (ov->mod <= 127u) depth = 20.0f * (float)ov->mod / 127.0f;
    else {
        float t = decay <= 64u ? 0.0f : decay >= 96u ? 1.0f : (float)(decay - 64u) / 32.0f;
        depth = m->mod_lo + (m->mod_hi - m->mod_lo) * t;
    }
    pre_ms = ov->pre <= 127u ? 60.0f * (float)ov->pre / 127.0f : m->pre0 + (m->pre127 - m->pre0) * u;
    if (ov->width <= 127u)
        wk = ov->width <= 64u ? (float)(64u - ov->width) / 64.0f : -0.25f * (float)(ov->width - 64u) / 63.0f;
    else wk = m->width_k;

    /* read lengths, mutually prime */
    for (i = 0; i < RV2_LINES; ++i) {
        unsigned l = (unsigned)((float)rv2_len[i] * sc + 0.5f);
        if (l < 40u) l = 40u;
        for (;;) {
            for (j = 0; j < i; ++j) if (gcd_u(l, len[j]) != 1u) break;
            if (j == i) break;
            ++l;
        }
        if (l > rv2_len[i]) l = rv2_len[i];
        len[i] = l; Lref += (float)l;
        c->target_q16[i] = (int32_t)(l << 16);
    }
    Lref *= 1.0f / RV2_LINES;

    /* shared HP on line 0: y = (1-c/2)(1-z^-1)/(1-(1-c)z^-1); its 1 kHz loss is
     * spread over the eight line gains (energy-average equivalent) */
    float hc = 1.0f - rv2_exp2(-6.2831853f * lowcut / fs * LOG2_E);
    c->hp_c = (int32_t)(hc * 32768.0f + 0.5f);
    float hp1k, hp_lcomp;
    {
        float num = 2.0f * SH1K * (1.0f - 0.5f * hc), re = 1.0f - (1.0f - hc) * C1K, im = (1.0f - hc) * S1K;
        hp1k = num / sqrtf(re * re + im * im);
        hp_lcomp = -rv2_log2(hp1k) / 8.0f;                 /* log2 of the per-line make-up */
    }

    /* Loop shelf H = Gh + (Gl-Gh)(1-p)/(1-p z^-1), designed once on the mean
     * line length: |H(1k)| = g (mid RT), |H(3k)| = g^(1/hf), H(0) = Gl = g^(1/(lf k)).
     * k in {1, 1.25, 1.6}: a higher LF RT is accepted when it buys the HF
     * target (cost = HF miss + 0.5 (k-1)). p: 8-point grid + 10 golden steps.
     * Per line: same p and lf k, gains scaled to that line's length, Gh
     * re-solved so each line's mid gain is exact. */
    float lg_ref = -3.0f * Lref * LOG2_10 / (rt * fs);
    float best_p = 0.5f, best_k = 1.0f, best_cost = 1e9f;
    {
        static const float ks[3] = {1.0f, 1.25f, 1.6f};
        unsigned kk;
        for (kk = 0; kk < 3u; ++kk) {
            float lfk = m->lf * ks[kk], extra = 0.5f * (ks[kk] - 1.0f), lo, hi, bp = 0.5f, bc = 1e9f;
            float gr = rv2_exp2(lg_ref), Glr = rv2_exp2(lg_ref / lfk);
            for (j = 0; j < 8u; ++j) {
                float p = 0.06f + 0.12f * (float)j, cst = shelf_cost(p, lg_ref, gr, Glr, hf);
                if (cst < bc) { bc = cst; bp = p; }
            }
            lo = bp - 0.12f; hi = bp + 0.12f;
            if (lo < 0.0f) lo = 0.0f;
            if (hi > 0.975f) hi = 0.975f;
            {
                float x1 = hi - 0.618034f * (hi - lo), x2 = lo + 0.618034f * (hi - lo);
                float f1 = shelf_cost(x1, lg_ref, gr, Glr, hf), f2 = shelf_cost(x2, lg_ref, gr, Glr, hf);
                for (j = 0; j < 10u; ++j) {
                    if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - 0.618034f * (hi - lo); f1 = shelf_cost(x1, lg_ref, gr, Glr, hf); }
                    else { lo = x1; x1 = x2; f1 = f2; x2 = lo + 0.618034f * (hi - lo); f2 = shelf_cost(x2, lg_ref, gr, Glr, hf); }
                }
                if (f1 < bc) { bc = f1; bp = x1; }
                if (f2 < bc) { bc = f2; bp = x2; }
            }
            if (bc + extra < best_cost) { best_cost = bc + extra; best_p = bp; best_k = lfk; }
        }
    }
    if (best_cost >= 1e8f) { best_p = 0.0f; best_k = m->lf; }   /* no shelf fits: flat loop filter */
    float rt_dc_max = rt, mk = rv2_exp2(hp_lcomp);
    for (i = 0; i < RV2_LINES; ++i) {
        float L = (float)len[i], lg = lg_ref * L / Lref, g = rv2_exp2(lg);
        float Gl = rv2_exp2(lg / best_k), p = best_p, gh;
        if (Gl > 0.9990f) Gl = 0.9990f;
        gh = solve_gh(p, Gl, g);
        if (gh < 0.0f) {   /* fall back to a plain low-pass with exact mid gain */
            float pr, pi; pw(p, C1K, S1K, &pr, &pi);
            gh = 0.0f; Gl = g / sqrtf(pr * pr + pi * pi);
            if (Gl > 0.9990f) Gl = 0.9990f;
        }
        c->a[i] = (int32_t)((1.0f - p) * 32768.0f + 0.5f);
        c->b[i] = (int32_t)((Gl - gh) * mk * (1.0f - p) * 32768.0f * 0.35355339f + 0.5f);
        c->dd[i] = (int32_t)(gh * mk * 32768.0f * 0.35355339f + 0.5f);
        {
            float glc = Gl * mk; if (glc > 0.9995f) glc = 0.9995f;
            float rt_dc = -3.0f * L * LOG2_10 / (fs * rv2_log2(glc));
            if (rt_dc > rt_dc_max) rt_dc_max = rt_dc;
        }
    }

    /* level: input gain with SIZE trim and DECAY compensation (held 2 s note) */
    {
        float lvl = size <= 64u ? m->lvl0 + (m->lvl64 - m->lvl0) * (float)size / 64.0f
                                : m->lvl64 + (m->lvl127 - m->lvl64) * (float)(size - 64u) / 63.0f;
        float comp = rv2_exp2(m->comp * rv2_log2(held_energy(rt_of(m, 64u)) / held_energy(rt)));
        float gin = m->gin * rv2_exp2(lvl / 20.0f * LOG2_10) * comp;
        c->gin_q12 = (int32_t)(gin * 4096.0f + 0.5f);
    }
    c->gout_q10 = (int32_t)(m->gout * 1024.0f + 0.5f);
    c->width_q15 = (int32_t)(wk * 32768.0f + (wk < 0.0f ? -0.5f : 0.5f));
    if (c->width_q15 > 32768) c->width_q15 = 32768;
    c->ap_g[0] = (int32_t)(m->apg_a * 32768.0f + 0.5f);
    c->ap_g[1] = (int32_t)(m->apg_b * 32768.0f + 0.5f);
    c->sw_g = (int32_t)(m->swell * 32768.0f + 0.5f);
    c->bloom = (uint32_t)m->bloom;
    if (depth > (float)RV2_MOD_MAX - 1.0f) depth = (float)RV2_MOD_MAX - 1.0f;
    c->mod_depth_q8 = (int32_t)(depth * 256.0f + 0.5f);
    c->lfo_inc[0] = (uint32_t)(m->mod_rate / fs * 4294967296.0f);
    c->lfo_inc[1] = (uint32_t)(m->mod_rate * 1.37f / fs * 4294967296.0f);
    c->pre_target = (int32_t)(pre_ms * 0.001f * fs + 0.5f);
    if (c->pre_target > RV2_PRE_LEN - 2) c->pre_target = RV2_PRE_LEN - 2;
    c->er_g = (int32_t)(m->er_gain * 4096.0f + 0.5f);
    {
        static const float frac[4] = {0.17f, 0.39f, 0.64f, 1.0f};
        for (i = 0; i < 4u; ++i) {
            int32_t t = c->pre_target + (int32_t)(m->er_ms * 0.001f * fs * sc * frac[i] + 0.5f);
            c->er_t[i] = t > RV2_PRE_LEN - 1 ? RV2_PRE_LEN - 1 : t;
        }
    }
    /* hard cap 1.6 RT (-96 dB) of the slowest band + 0.3 s; slowest RT in samples */
    c->cap = (uint32_t)((1.6f * rt_dc_max + 0.3f) * fs);
    c->rt_slow = (uint32_t)(rt_dc_max * fs);
}

/* ---- state -------------------------------------------------------------- */
static void rv2_clear(rv2_state_t *s)
{
    unsigned i;
    memset(s->p16, 0, RV2_POOL16_BYTES);
    memset(s->p32, 0, RV2_POOL32_BYTES);
    if (s->sync_pre) memset(s->sync_pre, 0, RV2_SYNC_PRE_LEN * sizeof *s->sync_pre);
    memset(s->sync_hist, 0, sizeof s->sync_hist);
    s->sync_phase = 0u; s->sync_pos = 0u; s->sync_pending = 0u;
    for (i = 0; i < RV2_LINES; ++i) {
        RV2_LN(s, i, s) = RV2_LN(s, i, e) = RV2_LN(s, i, es) = RV2_LN(s, i, yap) = 0;
    }
    s->hp_lz = s->hp_e = 0;
    memset(s->dec_h, 0, sizeof s->dec_h);
    memset(s->up_l, 0, sizeof s->up_l);
    memset(s->up_r, 0, sizeof s->up_r);
    s->er_l = s->er_r = 0;
    s->quiet = s->since_input = s->floor_blocks = s->blk_abs = 0u;
    s->blk_peak = s->peak_in = 0;
    s->end = 0u;
    s->idle = 1u;
}

RV2_API void rv2_init(rv2_state_t *s, int16_t *pool16, int32_t *pool32)
{
    memset(s, 0, sizeof *s);
    s->p16 = pool16; s->p32 = pool32;
#if RV2_LINE_STRUCT
    { unsigned i; for (i = 0; i < RV2_LINES; ++i) { s->ln[i].off = rv2_off[i]; s->ln[i].blen = rv2_blen[i]; } }
#endif
    s->lfo_ph[1] = 0x40000000u;
    rv2_clear(s);
}

RV2_API void rv2_apply(rv2_state_t *s, const rv2_coeffs_t *c)
{
    unsigned i;
    for (i = 0; i < RV2_LINES; ++i) {
        RV2_LN(s, i, a) = c->a[i]; RV2_LN(s, i, b) = c->b[i]; RV2_LN(s, i, dd) = c->dd[i];
        s->target[i] = c->target_q16[i];
    }
    s->hp_c = c->hp_c; s->gin_q12 = c->gin_q12; s->gout_q10 = c->gout_q10; s->width_q15 = c->width_q15;
    s->ap_g[0] = c->ap_g[0]; s->ap_g[1] = c->ap_g[1];
    if (c->sw_g && !s->sw_g)   /* swell APs were idle: drop stale content */
        memset(s->p16 + RV2_OSW0, 0, (RV2_SW0_LEN + RV2_SW1_LEN) * sizeof(int16_t));
    s->sw_g = c->sw_g; s->bloom = c->bloom;
    s->er_g = c->er_g;
    if (!s->er_g) s->er_l = s->er_r = 0;
    for (i = 0; i < 4u; ++i) s->er_t[i] = c->er_t[i];
    s->mod_depth_q8 = c->mod_depth_q8;
    s->lfo_inc[0] = c->lfo_inc[0]; s->lfo_inc[1] = c->lfo_inc[1];
    s->pre_target = c->pre_target; s->cap = c->cap; s->rt_slow = c->rt_slow;
    if (!s->configured) {   /* first apply: no slew */
        for (i = 0; i < RV2_LINES; ++i) s->base[i] = RV2_LN(s, i, d) = s->target[i];
        s->pre_d = s->pre_target;
        for (i = 0; i < 4u; ++i) s->er_d[i] = s->er_t[i];
        s->configured = 1u;
    }
}

RV2_API int rv2_idle(const rv2_state_t *s) { return (int)s->idle; }

/* parabolic sine, phase -> Q15 */
static int32_t para(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y = (x * (32768 - ax)) >> 13;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* ramps for the next RV2_SUB core samples: SIZE slew (1/32 sample per sample)
 * plus LFO, as a per-sample increment; pre-delay and early taps step 1 sample */
static void rv2_plan(rv2_state_t *s)
{
    unsigned i;
    int32_t m[4], step = 2048 * RV2_SUB;
    s->lfo_ph[0] += s->lfo_inc[0] * RV2_SUB;
    s->lfo_ph[1] += s->lfo_inc[1] * RV2_SUB;
    m[0] = para(s->lfo_ph[0]); m[1] = para(s->lfo_ph[1]);
    m[2] = para(s->lfo_ph[0] + 0x40000000u); m[3] = para(s->lfo_ph[1] + 0x40000000u);
    for (i = 0; i < RV2_LINES; ++i) {
        int32_t dlt = s->target[i] - s->base[i], lv, endp, hi = (rv2_blen[i] - 2) << 16;
        if (dlt > step) dlt = step; else if (dlt < -step) dlt = -step;
        s->base[i] += dlt;
        lv = m[i & 3u]; if (i & 4u) lv = -lv;
#if RV2_INT_READS
        if (i & 1u) lv = 0;
#endif
        endp = s->base[i] + ((s->mod_depth_q8 * lv) >> 7);
        if (endp < (2 << 16)) endp = 2 << 16;
        if (endp > hi) endp = hi;
        RV2_LN(s, i, inc) = (endp - RV2_LN(s, i, d)) >> 5;   /* RV2_SUB == 32 */
    }
    {
        uint32_t step = s->sync_mode ? 8u : 1u;
        if (s->sync_d < s->sync_target) {
            uint32_t diff = s->sync_target - s->sync_d;
            s->sync_d += (uint16_t)(diff < step ? diff : step);
        } else if (s->sync_d > s->sync_target) {
            uint32_t diff = s->sync_d - s->sync_target;
            s->sync_d -= (uint16_t)(diff < step ? diff : step);
        }
    }
    if (s->pre_d < s->pre_target) ++s->pre_d; else if (s->pre_d > s->pre_target) --s->pre_d;
    for (i = 0; i < 4u; ++i) { if (s->er_d[i] < s->er_t[i]) ++s->er_d[i]; else if (s->er_d[i] > s->er_t[i]) --s->er_d[i]; }
}

RV2_API void rv2_process(rv2_state_t *s, const int32_t *in, int32_t *out_l, int32_t *out_r, unsigned n)
{
    unsigned k, cores = n / 2u;
    if (s->idle) {
        for (k = 0; k < n; ++k) if (in[k]) break;
        if (k == n) { for (k = 0; k < n; ++k) out_l[k] = out_r[k] = 0; return; }
        s->idle = 0u; s->quiet = s->since_input = 0u;
    }
    s->loud = 0u;
    for (k = 0; k < n; k += 2u * RV2_SUB) {
        unsigned fr = n - k < 2u * RV2_SUB ? n - k : 2u * RV2_SUB;
        rv2_plan(s);
        RV2_CORE_CALL(rv2_core_run, s, in + k, out_l + k, out_r + k, fr);
    }
    /* Clean end. While input is present remember line-0 peak P. With no input:
     * (a) mean |line 0| <= 2 LSB for 0.1 s (rounding-noise floor reached), or
     * (b) the slowest band must have fallen from P to 1 LSB,
     * switches the loop quantisers to truncation (end mode), so the residue dies
     * to exact zero; then clear when every write stayed <= 1 LSB for 0.25 s,
     * or at the hard cap (1.6 RT + 0.3 s). */
    if (s->loud & 2u) {
        s->since_input = 0u; s->end = 0u; s->floor_blocks = 0u;
        s->peak_in = s->blk_peak;
    } else {
        s->since_input += cores;
        if (s->blk_peak > s->peak_in) s->peak_in = s->blk_peak;
    }
    if (!s->end && s->since_input) {
        uint32_t bits = 32u - (uint32_t)__builtin_clz((uint32_t)s->peak_in | 1u) - 2u;
        if (s->blk_abs <= 8u * cores) ++s->floor_blocks; else s->floor_blocks = 0u;
        if (s->floor_blocks * cores >= RV2_CORE_FS / 10u ||
            s->since_input >= s->rt_slow / 10u * bits + RV2_CORE_FS / 10u) s->end = 1u;
    }
    s->blk_peak = 0; s->blk_abs = 0u;
    if (s->loud & 1u) s->quiet = 0u; else s->quiet += cores;
    if (s->quiet >= RV2_CORE_FS / 4u || s->since_input >= s->cap) rv2_clear(s);
}
