/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_PUNCH_H
#define BR_INSTRUMENT_PUNCH_H
#include <stdint.h>
#include <string.h>

/* Punch-in FX keys. Of the 27 physical keys the black ones are
 * 1 3 5 | 8 10 | 13 15 17 | 20 22 | 25. The rightmost (25) is the shift key;
 * holding it turns the other ten into four effect groups. One effect runs at
 * a time and it stays on while any key of its group is held, so sliding
 * between keys of the group neither restarts nor changes it. */
enum { BR_PUNCH_NONE, BR_PUNCH_LOOP, BR_PUNCH_ECHO, BR_PUNCH_FILTER, BR_PUNCH_TREM };
#define BR_PUNCH_SHIFT_KEY 25u

typedef struct {
    uint8_t shift;         /* shift key is down */
    uint8_t active;        /* BR_PUNCH_* effect that is running */
    uint32_t held;         /* group keys that are down and belong to `active` */
    uint32_t consumed;     /* keys pressed in the mode, so their release is not a note */
} br_punch_keys_t;

/* Group of a key: 1..4, or 0 if it is not a group key. */
static inline uint32_t br_punch_group(uint32_t key)
{
    switch (key) {
    case 1u: case 3u: case 5u: return BR_PUNCH_LOOP;
    case 8u: case 10u: return BR_PUNCH_ECHO;
    case 13u: case 15u: case 17u: return BR_PUNCH_FILTER;
    case 20u: case 22u: return BR_PUNCH_TREM;
    default: return 0u;
    }
}

/* Feed a key edge. Returns 1 if the mode consumed it (it must not play a
 * note or take part in any other key logic), 0 if it is an ordinary key. */
static inline uint32_t br_punch_key(br_punch_keys_t *s, uint32_t key, uint32_t down)
{
    uint32_t group = br_punch_group(key);
    if (key >= 27u) return 0u;
    if (key == BR_PUNCH_SHIFT_KEY) {
        if (down) { s->shift = 1u; s->consumed |= 1u << key; return 1u; }
        if (!(s->consumed & (1u << key))) return 0u;
        s->shift = 0u; s->active = BR_PUNCH_NONE; s->held = 0u;
        s->consumed &= ~(1u << key);
        return 1u;
    }
    if (down) {
        if (!s->shift || !group) return 0u;           /* ordinary note */
        s->consumed |= 1u << key;
        if (s->active == BR_PUNCH_NONE) s->active = (uint8_t)group;
        if (s->active == group) s->held |= 1u << key;  /* else: other group, ignored */
        return 1u;
    }
    if (!(s->consumed & (1u << key))) return 0u;
    s->consumed &= ~(1u << key);
    s->held &= ~(1u << key);
    if (s->active != BR_PUNCH_NONE && !s->held) s->active = BR_PUNCH_NONE;
    return 1u;
}

/* ---- Settings. Every control has 4 choices or 32 steps so the whole set packs
 * into four bytes of the session. */
typedef struct {
    uint8_t loop_len;       /* 0..3: 1 bar, 1/2, 1/4, 1/8 bar */
    uint8_t echo_time;      /* 0..3: 1/32, 1/16, 1/8, dotted 1/8 note */
    uint8_t filter_shape;   /* 0..3: LP, HP, LP sweep, HP sweep */
    uint8_t trem_rate;      /* 0..3: 1/4, 1/8, 1/16, 1/32 note */
    uint8_t echo_fb;        /* 0..31 */
    uint8_t filter_amount;  /* 0..31 */
    uint8_t trem_depth;     /* 0..31 */
} br_punch_cfg_t;
#define BR_PUNCH_CFG_DEFAULTS {2u, 2u, 0u, 2u, 12u, 20u, 24u}
#define BR_PUNCH_SAVE_MARK 0x50u

static inline uint32_t br_punch_cfg_is_default(const br_punch_cfg_t *c)
{
    static const br_punch_cfg_t d = BR_PUNCH_CFG_DEFAULTS;
    return c->loop_len == d.loop_len && c->echo_time == d.echo_time &&
        c->filter_shape == d.filter_shape && c->trem_rate == d.trem_rate &&
        c->echo_fb == d.echo_fb && c->filter_amount == d.filter_amount &&
        c->trem_depth == d.trem_depth;
}
/* Four spare bytes of the kit track's saved lane levels: a marker, the four
 * two-bit choices, then the three 5-bit levels. All zero means defaults. */
static inline void br_punch_cfg_pack(const br_punch_cfg_t *c, uint8_t out[4])
{
    uint32_t v;
    out[0] = out[1] = out[2] = out[3] = 0u;
    if (br_punch_cfg_is_default(c)) return;
    v = (uint32_t)(c->echo_fb & 31u) | (uint32_t)(c->filter_amount & 31u) << 5 |
        (uint32_t)(c->trem_depth & 31u) << 10;
    out[0] = BR_PUNCH_SAVE_MARK;
    out[1] = (uint8_t)((c->loop_len & 3u) | (c->echo_time & 3u) << 2 |
                       (c->filter_shape & 3u) << 4 | (c->trem_rate & 3u) << 6);
    out[2] = (uint8_t)(v & 255u); out[3] = (uint8_t)(v >> 8);
}
static inline uint32_t br_punch_cfg_packed_valid(const uint8_t in[4])
{
    if (!in[0] && !in[1] && !in[2] && !in[3]) return 1u;
    return in[0] == BR_PUNCH_SAVE_MARK && in[3] < 0x80u;
}
static inline void br_punch_cfg_unpack(br_punch_cfg_t *c, const uint8_t in[4])
{
    static const br_punch_cfg_t d = BR_PUNCH_CFG_DEFAULTS;
    uint32_t v;
    *c = d;
    if (in[0] != BR_PUNCH_SAVE_MARK || !br_punch_cfg_packed_valid(in)) return;
    v = (uint32_t)in[2] | (uint32_t)in[3] << 8;
    c->loop_len = in[1] & 3u; c->echo_time = (in[1] >> 2) & 3u;
    c->filter_shape = (in[1] >> 4) & 3u; c->trem_rate = (in[1] >> 6) & 3u;
    c->echo_fb = v & 31u; c->filter_amount = (v >> 5) & 31u; c->trem_depth = (v >> 10) & 31u;
}

/* ---- Master-bus effects without buffers: a tempo-synced gain chop and a
 * one-pole filter, applied to the finished stereo mix before output scaling. */
typedef struct {
    uint32_t phase, sweep;       /* frames since the effect started; sweep in Q15 */
    int32_t gain_q15, low[2];
    uint8_t last;                /* effect that ran in the previous block */
    /* Echo: a mono line at half the sample rate, borrowed from an idle track's
     * delay buffer. The caller sets line/line_len before each block. */
    int16_t *line; uint32_t line_len, write;
    int32_t acc, previous_wet;
} br_punch_dsp_t;

static inline int32_t br_punch_lerp(int32_t a, int32_t b, uint32_t t_q15)
{
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)t_q15) >> 15);
}

/* out is interleaved stereo; quarter is the tempo's frames per quarter note.
 * Effects other than TREM and FILTER leave out[] untouched. */
static inline void br_punch_process(br_punch_dsp_t *d, const br_punch_cfg_t *c,
                                    uint32_t effect, int32_t *out, uint32_t n,
                                    uint32_t quarter)
{
    uint32_t i;
    if (effect != d->last) {          /* a new press starts from the top */
        uint32_t was_echo = d->last == BR_PUNCH_ECHO;
        d->last = (uint8_t)effect;
        d->phase = d->sweep = 0u; d->gain_q15 = 32768; d->low[0] = d->low[1] = 0;
        if (was_echo && d->line) memset(d->line, 0, d->line_len * sizeof d->line[0]);
        d->write = 0u; d->acc = d->previous_wet = 0;
    }
    if (effect == BR_PUNCH_ECHO && d->line && d->line_len > 16u) {
        /* Delay in half-rate samples: a note value of the tempo, capped by the line. */
        static const uint8_t sixteenths_of_quarter[4] = {1, 2, 4, 6};   /* 1/32, 1/16, 1/8, dotted 1/8 */
        uint32_t delay = quarter * sixteenths_of_quarter[c->echo_time & 3u] / 16u;
        int32_t feedback = (int32_t)(c->echo_fb & 31u) * 27850 / 31;      /* up to 0.85 */
        if (delay < 2u) delay = 2u;
        if (delay >= d->line_len) delay = d->line_len - 1u;
        for (i = 0u; i < n; ++i) {
            int32_t mono = (out[2u * i] + out[2u * i + 1u]) >> 1, wet;
            if (i & 1u) {
                uint32_t read = d->write >= delay ? d->write - delay : d->write + d->line_len - delay;
                int32_t heard = d->line[read], mix = (d->acc + mono) >> 1;
                int32_t stored = mix + (int32_t)(((int64_t)heard * feedback) >> 15);
                d->line[d->write] = (int16_t)(stored > 32767 ? 32767 : stored < -32768 ? -32768 : stored);
                if (++d->write >= d->line_len) d->write = 0u;
                wet = (d->previous_wet + heard) >> 1;       /* interpolate the half-rate wet */
                d->previous_wet = heard;
            } else {
                d->acc = mono;
                wet = d->previous_wet;
            }
            out[2u * i] += wet; out[2u * i + 1u] += wet;
        }
        d->phase += n;
    } else if (effect == BR_PUNCH_TREM) {
        uint32_t period = quarter >> (c->trem_rate & 3u);
        int32_t floor_q15 = 32768 - (int32_t)(c->trem_depth & 31u) * 32768 / 31;
        if (period < 8u) period = 8u;
        for (i = 0u; i < n; ++i) {
            int32_t target = (d->phase % period) < period / 2u ? 32768 : floor_q15;
            d->gain_q15 += (target - d->gain_q15) >> 5;       /* soft edges */
            out[2u * i] = (int32_t)(((int64_t)out[2u * i] * d->gain_q15) >> 15);
            out[2u * i + 1u] = (int32_t)(((int64_t)out[2u * i + 1u] * d->gain_q15) >> 15);
            ++d->phase;
        }
    } else if (effect == BR_PUNCH_FILTER) {
        uint32_t shape = c->filter_shape & 3u, closed, amount = c->filter_amount & 31u;
        int32_t coeff, target;
        /* Target cutoff coefficient: a bigger amount closes further (squared taper). */
        target = 700 + (int32_t)((31u - amount) * (31u - amount) * (32768u - 700u) / 961u);
        if (shape >= 2u) {            /* sweep from open to the target over two beats */
            uint32_t step = (n << 15) / (2u * quarter + 1u);
            d->sweep = d->sweep + step > 32768u ? 32768u : d->sweep + step;
            closed = d->sweep;
        } else closed = 32768u;
        coeff = br_punch_lerp(32768, target, closed);
        for (i = 0u; i < n; ++i) {
            uint32_t ch;
            for (ch = 0u; ch < 2u; ++ch) {
                int32_t x = out[2u * i + ch];
                d->low[ch] += (int32_t)(((int64_t)(x - d->low[ch]) * coeff) >> 15);
                out[2u * i + ch] = (shape & 1u) ? x - d->low[ch] : d->low[ch];
            }
        }
        d->phase += n;
    } else d->phase += n;
}
#endif
