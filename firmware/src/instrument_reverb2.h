/* rv2: 8-line Hadamard FDN reverb for JamesOS (FM-1): the global reverb.
 * SPDX-License-Identifier: GPL-3.0-only
 * Firmware port of the host prototype (rv2.h, rv2_core.h,
 * rv2_ctl.c), kept textually identical except for the port hooks below (RV2_API,
 * RV2_TABLE, RV2_CORE_CALL) and these file names; tests/test_reverb2_golden.c
 * reproduces the prototype's golden hashes (out/golden.txt) bit for bit.
 * Written fresh. Structure (ideas only, no code borrowed): Jot/Stautner-Puckette
 * FDN with a Hadamard matrix as in ChoralRoot; per-line g^(L/Lref) gains (Jot);
 * input allpass diffusers as in the existing JamesOS reverb.
 *
 * Files
 *   instrument_reverb2.h       this header (prototype rv2.h)
 *   instrument_reverb2_core.h  (rv2_core.h) the per-sample loop: ONE static function, rv2_core_run(), no calls
 *               out (helpers are macros), meant for a RAM section (RV2_RAM)
 *   instrument_reverb2.c       (rv2_ctl.c) everything at block or control rate, can run from flash:
 *               rv2_design (control task, float + sqrtf only), rv2_apply (block
 *               start), rv2_process (driver: LFO/slew plan per 32 core samples,
 *               core call, end-of-tail bookkeeping), init/clear/idle.
 *
 * Rates: block API at 44.1 kHz (n even), core at 22.05 kHz.
 * Input: mono int32 send domain (clip +-250,000). Output: stereo int32 wet
 * return, clamped to +-65,535.
 *
 * MEMORY
 *   pool16  int16[RV2_POOL16_LEN = 12,328]  24,656 B  (budget 12,400 samples)
 *           8 lines (max+27 each) 9,638 | pre-delay 1,330 | swell APs 773+587
 *   pool32  int32[RV2_POOL32_LEN = 690]      2,760 B  (budget 1,340 words)
 *           input APs 113/83/283/211
 *   rv2_state_t (control, incl. applied coefficients)  RV2_STATE_BYTES
 *   rv2_coeffs_t (design output, control task)          RV2_COEFFS_BYTES
 */
#ifndef RV2_H
#define RV2_H
#include <stdint.h>

/* ---- compile-time CPU options ------------------------------------------- */
#ifndef RV2_UPSAMPLE_TAPS     /* 6: 11-tap half-band interpolator, 4: 7-tap */
#define RV2_UPSAMPLE_TAPS 6
#endif
#ifndef RV2_LINE_STRUCT       /* 1: per-line state as an array of structs */
#define RV2_LINE_STRUCT 0
#endif
#ifndef RV2_INT_READS         /* 1: lines 1,3,5,7 read at integer delay, unmodulated */
#define RV2_INT_READS 0
#endif
#ifndef RV2_RAM               /* firmware: __attribute__((section(".ram_audio"))) */
#define RV2_RAM
#endif
/* Port hooks (firmware: instrument_fx.c sets them before including instrument_reverb2.c).
 * RV2_API: linkage of the API functions (firmware: static, one translation unit).
 * RV2_TABLE(name): placement of the core's small constant tables (firmware: copied RAM
 * data, one section per table as -fdata-sections names a variable's).
 * RV2_CORE_CALL: how the block driver calls the core (firmware: an absolute indirect
 * call, because the driver runs from XIP flash and the core from RAM). */
#ifndef RV2_API
#define RV2_API
#endif
#ifndef RV2_TABLE
#define RV2_TABLE(name)
#endif
#ifndef RV2_CORE_CALL
#define RV2_CORE_CALL(fn, ...) fn(__VA_ARGS__)
#endif

/* ---- sizes -------------------------------------------------------------- */
#define RV2_LINES 8
#define RV2_L0 601
#define RV2_L1 743
#define RV2_L2 907
#define RV2_L3 1069
#define RV2_L4 1249
#define RV2_L5 1429
#define RV2_L6 1613
#define RV2_L7 1811
#define RV2_MOD_MAX 24
#define RV2_LINE_PAD (RV2_MOD_MAX + 3)
#define RV2_LINE_MEM (RV2_L0 + RV2_L1 + RV2_L2 + RV2_L3 + RV2_L4 + RV2_L5 + RV2_L6 + RV2_L7 + 8 * RV2_LINE_PAD)
#define RV2_PRE_LEN 1330
/* Additional full-rate line for synced short musical pre-delays, through 1/32 at 60 BPM. */
#define RV2_SYNC_PRE_LEN 2053
#define RV2_SYNC_PRE_COMP_LEN (RV2_SYNC_PRE_LEN * 2)
#define RV2_SW0_LEN 773
#define RV2_SW1_LEN 587
#define RV2_POOL16_LEN (RV2_LINE_MEM + RV2_PRE_LEN + RV2_SW0_LEN + RV2_SW1_LEN)
#define RV2_AP0_LEN 113
#define RV2_AP1_LEN 83
#define RV2_AP2_LEN 283
#define RV2_AP3_LEN 211
#define RV2_POOL32_LEN (RV2_AP0_LEN + RV2_AP1_LEN + RV2_AP2_LEN + RV2_AP3_LEN)
#define RV2_POOL16_BYTES (RV2_POOL16_LEN * 2)
#define RV2_POOL32_BYTES (RV2_POOL32_LEN * 4)
#define RV2_STATE_BYTES (sizeof(rv2_state_t))
#define RV2_COEFFS_BYTES (sizeof(rv2_coeffs_t))
#define RV2_CORE_FS 22050
#define RV2_SUB 32                     /* core samples per plan (64 frames) */

enum { RV2_ROOM = 0, RV2_HALL = 1, RV2_SPACE = 3 };

/* Optional user controls (save format extension). Each 0..127, or RV2_DERIVE
 * to derive from TYPE/SIZE/DECAY exactly as the mode tables do. */
#define RV2_DERIVE 255u
typedef struct {
    uint8_t pre;      /* pre-delay 0..60 ms (linear)                       */
    uint8_t tone;     /* RT(4k)/RT(1k) 0.25 (0) .. 0.95 (127)              */
    uint8_t mod;      /* modulation depth 0..20 core samples (linear)      */
    uint8_t lowcut;   /* in-loop low cut 10 Hz (0) .. 300 Hz (127), log    */
    uint8_t width;    /* 0 mono, 64 natural (uncorrelated), 127 extra wide */
} rv2_overrides_t;

/* Output of rv2_design, input of rv2_apply. */
typedef struct {
    int32_t target_q16[RV2_LINES];     /* read lengths, Q16 core samples */
    int32_t a[RV2_LINES], b[RV2_LINES], dd[RV2_LINES];   /* loop shelf, Q15 */
    int32_t hp_c;                      /* shared HP, Q15 */
    int32_t gin_q12, gout_q10, width_q15;
    int32_t ap_g[2], sw_g;             /* input diffusers (pairs), swell APs, Q15 */
    int32_t er_g, er_t[4];             /* early taps: Q12 gain, delays (core samples) */
    int32_t mod_depth_q8, pre_target;
    uint32_t lfo_inc[2], bloom, cap, rt_slow;
} rv2_coeffs_t;

#if RV2_LINE_STRUCT
typedef struct {
    int32_t d, inc, s, e, es, yap, a, b, dd;
    uint16_t wpos, off, blen, pad;
} rv2_line_t;
#define RV2_LN(st, i, f) ((st)->ln[i].f)
#else
#define RV2_LN(st, i, f) ((st)->f[i])
#endif

typedef struct {
    int16_t *p16;                      /* pools (placed by the caller) */
    int16_t *sync_pre;                  /* optional independent full-quality extension */
    uint16_t sync_pos, sync_d, sync_target;
    uint8_t sync_mode, sync_phase;
    uint16_t sync_pending;
    int16_t sync_hist[4];
    int32_t *p32;
#if RV2_LINE_STRUCT
    rv2_line_t ln[RV2_LINES];
#else
    int32_t d[RV2_LINES], inc[RV2_LINES];          /* read delay Q16 and per-sample ramp */
    int32_t s[RV2_LINES], e[RV2_LINES], es[RV2_LINES], yap[RV2_LINES];
    int32_t a[RV2_LINES], b[RV2_LINES], dd[RV2_LINES];
    uint16_t wpos[RV2_LINES];
#endif
    int32_t base[RV2_LINES], target[RV2_LINES];    /* slewed / target lengths Q16 */
    int32_t hp_lz, hp_e, hp_c;
    int32_t gin_q12, gout_q10, width_q15, ap_g[2], sw_g;
    int32_t er_g, er_d[4], er_t[4], er_l, er_r;
    uint32_t lfo_ph[2], lfo_inc[2];
    int32_t mod_depth_q8, pre_d, pre_target;
    uint16_t pre_pos, sw_pos[2], ap_pos[4];
    int32_t dec_h[11], up_l[6], up_r[6];           /* rate-conversion history */
    uint32_t bloom, end, loud, idle, configured;
    uint32_t quiet, since_input, cap, rt_slow, floor_blocks, blk_abs;
    int32_t blk_peak, peak_in;
    int32_t peak_line;                             /* host diagnostic only */
} rv2_state_t;

RV2_API void rv2_init(rv2_state_t *s, int16_t *pool16, int32_t *pool32);
RV2_API void rv2_design(unsigned type, unsigned size, unsigned decay, const rv2_overrides_t *ov, rv2_coeffs_t *c);
RV2_API void rv2_apply(rv2_state_t *s, const rv2_coeffs_t *c);
RV2_API void rv2_process(rv2_state_t *s, const int32_t *in, int32_t *out_l, int32_t *out_r, unsigned n);
RV2_API int rv2_idle(const rv2_state_t *s);
/* host helper: mid-band RT60 the tables ask for */
RV2_API float rv2_target_rt(unsigned type, unsigned decay);

#endif
