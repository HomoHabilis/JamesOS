/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 396 MHz supply policy; no device I/O in this file.
 *
 * Ordered operations follow the CRC-verified M-VAVE V15 application supply
 * block at 0x02004f4a..0x0200509a; see research/community/v15.dis in the
 * investigation workspace. The three VLVD operations at 0x02004f4e/5c/76
 * precede the regulator handoff at 0x02004f98. Checked transfers, extra
 * readbacks and held-core and interrupt guards are additions. Intervening
 * vendor RTC, wake and software bookkeeping is not reproduced here.
 *
 * Target entry states and clock must be explicitly qualified before enabling
 * writes. A completed digital readback is not evidence of analog power-good.
 */
#pragma once
#include <stdint.h>

#define FM1_VP_COUNT 11u
#define FM1_VP_UNKNOWN 0xffffffffu
enum {
    FM1_VP_OK = 0u, FM1_VP_BAD_IO = 1u, FM1_VP_PREVIOUS_TIMEOUT = 2u,
    FM1_VP_CORE_NOT_HELD = 3u, FM1_VP_IRQ_NOT_DISABLED = 4u,
    FM1_VP_READ_ERROR = 5u, FM1_VP_WRITE_ERROR = 6u,
    FM1_VP_WAIT_ERROR = 7u, FM1_VP_READBACK_ERROR = 8u,
    FM1_VP_INELIGIBLE = 9u, FM1_VP_UNSAFE_MODE = 10u,
    FM1_VP_STATE_CHANGED = 11u
};

typedef struct { uint32_t raw[FM1_VP_COUNT]; } fm1_vp_sample_t;
typedef struct {
    uint32_t status, timeouts, write_count, last_step, last_reg, last_confirmed;
    fm1_vp_sample_t before, after;
} fm1_vp_report_t;
typedef struct {
    void *ctx;
    int (*read)(void *, uint32_t, uint32_t *);
    int (*write)(void *, uint32_t, uint32_t);
    int (*handoff_loop)(void *);
    int (*core_held)(void *);
    int (*irq_disabled)(void *);
    uint32_t (*timeouts)(void *);
    int (*eligible)(void *, const fm1_vp_sample_t *);
    int (*or_bits)(void *, uint32_t, uint32_t);
    int (*timer_ready)(void *);
} fm1_vp_io_t;

/* Reset signatures observed on this instrument: physical power-on (1,1),
 * software P33 reset (0x40,1), and update handoff (0x40,0x20). The 396
 * physical power tuple remains inferred. Reset matching alone never authorizes
 * a write: the target must also match clock and P33 profiles. */
static inline int fm1_vp_reset_profile_eligible(uint32_t p3, uint32_t rst_src)
{
    return (p3 == 0x01u && rst_src == 0x01u) ||
           (p3 == 0x40u && (rst_src == 0x01u || rst_src == 0x20u));
}

/* The 396 MHz updater and P33 reset tuples were observed on this instrument.
 * The physical tuple is inferred from the measured 360 physical detector and
 * 396 boot selectors; it requires a real full-off 396 observation. */
static inline int fm1_vp_entry_profile_eligible(uint32_t p3, uint32_t rst_src,
                                                const fm1_vp_sample_t *sample)
{
    static const uint8_t warm[FM1_VP_COUNT] =
        {0xfb,0x20,0x58,0x44,0x0c,0xa5,0x96,0x1e,0x3d,0x00,0x00};
    static const uint8_t cold[FM1_VP_COUNT] =
        {0x77,0x60,0x18,0x44,0x0c,0x00,0x00,0x1e,0x3d,0x00,0x20};
    static const uint8_t physical[FM1_VP_COUNT] =
        {0x77,0x60,0x18,0x44,0x0c,0x00,0x00,0x1e,0x05,0x00,0x20};
    const uint8_t *expected;
    uint32_t i;
    if (!sample || !fm1_vp_reset_profile_eligible(p3, rst_src)) return 0;
    expected = p3 == 1u ? physical : (rst_src == 0x20u ? warm : cold);
    for (i = 0u; i < FM1_VP_COUNT; ++i)
        if (sample->raw[i] != expected[i]) return 0;
    return 1;
}

/* 1481's observed 396/1/2 fingerprint with a 24-MHz oscillator. Register
 * equality is not an independent measurement of frequency or rail voltage. */
static inline int fm1_vp_clock_profile_eligible(uint32_t div, uint32_t clk0,
                                                uint32_t source,
                                                uint32_t pll0, uint32_t pll1)
{
    return div == 0x00010200u && clk0 == 0x000001c1u &&
           (source & 0x0fu) == 0x07u && pll0 == 0x0940022bu &&
           pll1 == 0x075030c4u;
}

static inline uint32_t fm1_vp_reg(uint32_t i)
{
    if (i < 3u) return i;
    if (i < 8u) return i + 2u;
    return i == 8u ? 0x11u : i == 9u ? 0x16u : 0x17u;
}
static inline uint32_t fm1_vp_guard(const fm1_vp_io_t *io)
{
    if (!io->core_held(io->ctx)) return FM1_VP_CORE_NOT_HELD;
    if (!io->irq_disabled(io->ctx)) return FM1_VP_IRQ_NOT_DISABLED;
    return FM1_VP_OK;
}
static inline uint32_t fm1_vp_read(const fm1_vp_io_t *io, uint32_t reg, uint32_t *out)
{
    uint32_t rc = fm1_vp_guard(io), before, value = FM1_VP_UNKNOWN;
    int ok;
    if (rc) return rc;
    before = io->timeouts(io->ctx);
    ok = io->read(io->ctx, reg, &value);
    if (!ok || io->timeouts(io->ctx) != before || value > 0xffu) return FM1_VP_READ_ERROR;
    *out = value;
    return FM1_VP_OK;
}
static inline uint32_t fm1_vp_sample(const fm1_vp_io_t *io, fm1_vp_sample_t *sample)
{
    uint32_t i, rc;
    for (i = 0u; i < FM1_VP_COUNT; ++i) {
        rc = fm1_vp_read(io, fm1_vp_reg(i), &sample->raw[i]);
        if (rc) return rc;
    }
    return FM1_VP_OK;
}

/* Even an unchanged value is written, matching the vendor operations. The
 * bounded handoff loop follows the actual write and precedes its readback.
 * A failed transfer may have reached hardware; leave last_confirmed unknown. */
static inline uint32_t fm1_vp_step(const fm1_vp_io_t *io, fm1_vp_report_t *out,
                                   fm1_vp_sample_t *state, uint32_t step,
                                   uint32_t index, uint32_t clear, uint32_t set,
                                   uint32_t handoff)
{
    uint32_t reg = fm1_vp_reg(index), actual, desired, rc, before;
    int ok;
    out->last_step = step;
    out->last_reg = reg;
    out->last_confirmed = FM1_VP_UNKNOWN;
    rc = fm1_vp_read(io, reg, &actual); if (rc) return rc;
    out->last_confirmed = actual;
    if (actual != state->raw[index]) return FM1_VP_STATE_CHANGED;
    desired = (actual & ~clear) | set;
    rc = fm1_vp_guard(io); if (rc) return rc;
    out->last_confirmed = FM1_VP_UNKNOWN;
    ++out->write_count;
    before = io->timeouts(io->ctx);
    ok = io->write(io->ctx, reg, desired);
    if (!ok || io->timeouts(io->ctx) != before) return FM1_VP_WRITE_ERROR;
    if (handoff) {
        rc = fm1_vp_guard(io); if (rc) return rc;
        if (!io->handoff_loop(io->ctx)) return FM1_VP_WAIT_ERROR;
        rc = fm1_vp_guard(io); if (rc) return rc;
    }
    rc = fm1_vp_read(io, reg, &actual); if (rc) return rc;
    out->last_confirmed = actual;
    if (actual != desired) return FM1_VP_READBACK_ERROR;
    state->raw[index] = actual;
    return FM1_VP_OK;
}

/* During CON5's temporary VDDIOM selector-0 state, write the vendor's
 * consecutive CON6/7/8/9 operations without extra readbacks. For a complete
 * byte overwrite, a pre-read serves no RMW purpose; CON9 still requires and
 * checks its RMW read. Each transfer is checked and a failure stops the next
 * operation. State becomes assumed, not confirmed, until the later survey. */
static inline uint32_t fm1_vp_step_deferred(const fm1_vp_io_t *io,
                                            fm1_vp_report_t *out,
                                            fm1_vp_sample_t *state,
                                            uint32_t step, uint32_t index,
                                            uint32_t clear, uint32_t set)
{
    uint32_t reg = fm1_vp_reg(index), actual, desired, rc, before;
    int ok;
    out->last_step = step; out->last_reg = reg; out->last_confirmed = FM1_VP_UNKNOWN;
    if (clear == 0xffu) {
        desired = set;
    } else {
        rc = fm1_vp_read(io, reg, &actual); if (rc) return rc;
        out->last_confirmed = actual;
        if (actual != state->raw[index]) return FM1_VP_STATE_CHANGED;
        desired = (actual & ~clear) | set;
    }
    rc = fm1_vp_guard(io); if (rc) return rc;
    out->last_confirmed = FM1_VP_UNKNOWN;
    ++out->write_count;
    before = io->timeouts(io->ctx);
    ok = io->write(io->ctx, reg, desired);
    if (!ok || io->timeouts(io->ctx) != before) return FM1_VP_WRITE_ERROR;
    state->raw[index] = desired;
    return FM1_VP_OK;
}

/* Used for the deferred checks after CON5 and CON6 selectors are restored,
 * and for V15's three additional reads. No later operation runs after a
 * mismatch. */
static inline uint32_t fm1_vp_verify_read(const fm1_vp_io_t *io,
                                          fm1_vp_report_t *out,
                                          const fm1_vp_sample_t *state,
                                          uint32_t index)
{
    uint32_t reg = fm1_vp_reg(index), actual, rc;
    out->last_reg = reg; out->last_confirmed = FM1_VP_UNKNOWN;
    rc = fm1_vp_read(io, reg, &actual); if (rc) return rc;
    out->last_confirmed = actual;
    return actual == state->raw[index] ? FM1_VP_OK : FM1_VP_READBACK_ERROR;
}

/* CON11 bits 6/7 are pending-clear/pending controls, not ordinary retained
 * state. V15 performs hardware OR 0x40, a field RMW of bits 5:3, then
 * hardware OR 0x01. Compare the six stable bits around each operation and
 * retain the actual readback for telemetry; do not require bit 6 to latch. */
static inline uint32_t fm1_vp_vlvd_step(const fm1_vp_io_t *io,
                                        fm1_vp_report_t *out,
                                        fm1_vp_sample_t *state,
                                        uint32_t step, uint32_t operation)
{
    uint32_t actual, desired, rc, before;
    int ok;
    out->last_step = step; out->last_reg = 0x11u;
    out->last_confirmed = FM1_VP_UNKNOWN;
    rc = fm1_vp_read(io, 0x11u, &actual); if (rc) return rc;
    out->last_confirmed = actual;
    if (((actual ^ state->raw[8]) & 0x3fu) != 0u) return FM1_VP_STATE_CHANGED;
    if (operation == 0u) desired = actual | 0x40u;
    else if (operation == 1u) desired = (actual & ~0x38u) | 0x38u;
    else desired = actual | 0x01u;
    rc = fm1_vp_guard(io); if (rc) return rc;
    out->last_confirmed = FM1_VP_UNKNOWN;
    ++out->write_count;
    before = io->timeouts(io->ctx);
    ok = operation == 1u ? io->write(io->ctx, 0x11u, desired) :
         io->or_bits(io->ctx, 0x11u, operation == 0u ? 0x40u : 0x01u);
    if (!ok || io->timeouts(io->ctx) != before) return FM1_VP_WRITE_ERROR;
    rc = fm1_vp_read(io, 0x11u, &actual); if (rc) return rc;
    out->last_confirmed = actual;
    if (((actual ^ desired) & 0x3fu) != 0u) return FM1_VP_READBACK_ERROR;
    state->raw[8] = actual;
    return FM1_VP_OK;
}

static inline uint32_t fm1_vp_run(const fm1_vp_io_t *io, uint32_t prior_timeouts,
                                  fm1_vp_report_t *out)
{
    fm1_vp_sample_t state;
    uint32_t rc = FM1_VP_BAD_IO, i, mode;
    if (!out) return FM1_VP_BAD_IO;
    out->status = rc; out->timeouts = prior_timeouts;
    out->write_count = out->last_step = 0u;
    out->last_reg = out->last_confirmed = FM1_VP_UNKNOWN;
    for (i = 0u; i < FM1_VP_COUNT; ++i)
        out->before.raw[i] = out->after.raw[i] = state.raw[i] = FM1_VP_UNKNOWN;
    if (!io || !io->read || !io->write || !io->handoff_loop || !io->core_held ||
        !io->irq_disabled || !io->timeouts || !io->eligible || !io->or_bits ||
        !io->timer_ready)
        return out->status;
    if (prior_timeouts || io->timeouts(io->ctx)) { rc = FM1_VP_PREVIOUS_TIMEOUT; goto done; }
    rc = fm1_vp_guard(io); if (rc) goto done;
    rc = fm1_vp_sample(io, &out->before); if (rc) goto done;
    for (i = 0u; i < FM1_VP_COUNT; ++i) state.raw[i] = out->before.raw[i];
    mode = state.raw[0] & 0x0cu;
    if (mode != 0x04u && mode != 0x08u) { rc = FM1_VP_UNSAFE_MODE; goto done; }
    rc = fm1_vp_guard(io); if (rc) goto done;
    if (!io->eligible(io->ctx, &out->before)) { rc = FM1_VP_INELIGIBLE; goto done; }
    /* A stalled or misconfigured crystal timer must be caught before even the
     * first VLVD write, not halfway through a regulator handoff. */
    if (!io->timer_ready(io->ctx)) { rc = FM1_VP_WAIT_ERROR; goto done; }
    rc = fm1_vp_guard(io); if (rc) goto done;
    rc = fm1_vp_vlvd_step(io, out, &state, 1u, 0u); if (rc) goto done;
    rc = fm1_vp_vlvd_step(io, out, &state, 2u, 1u); if (rc) goto done;
    rc = fm1_vp_vlvd_step(io, out, &state, 3u, 2u); if (rc) goto done;
#define FM1_VP_STEP(n, index, clear, set, handoff) do { \
    rc = fm1_vp_step(io, out, &state, (n)+3u, index, clear, set, handoff); \
    if (rc) goto done; \
} while (0)
#define FM1_VP_DEFER(n, index, clear, set) do { \
    rc = fm1_vp_step_deferred(io, out, &state, (n)+3u, index, clear, set); \
    if (rc) goto done; \
} while (0)
    /* 00/01/02/05/06/07/08/09/11/16/17 are sample indices 0..10. */
    FM1_VP_STEP( 1u, 0u, 0u,    0x04u, 1u); /* LDO13 on */
    FM1_VP_STEP( 2u, 0u, 0u,    0x08u, 1u); /* DCDC13 on */
    FM1_VP_STEP( 3u, 0u, 0x04u, 0u,    1u); /* LDO13 off */
    FM1_VP_STEP( 4u, 1u, 0xffu, 0x20u, 0u);
    FM1_VP_STEP( 5u, 3u, 0xffu, 0x40u, 0u); /* VDDIOM selector 0 until step 10 */
    FM1_VP_DEFER(6u, 4u, 0xffu, 0x0cu);
    FM1_VP_DEFER(7u, 5u, 0xffu, 0xa5u);
    FM1_VP_DEFER(8u, 6u, 0xffu, 0x96u);
    FM1_VP_DEFER(9u, 7u, 0u,    0x10u);
    FM1_VP_STEP(10u, 3u, 0x07u, 0x04u, 0u);
    /* The mandatory RMW read for step 11 also verifies the deferred CON6
     * byte before raising its selector. Verify the new selector immediately. */
    FM1_VP_STEP(11u, 4u, 0x07u, 0x07u, 0u); /* VDC14 = 7 */
    rc = fm1_vp_verify_read(io, out, &state, 5u); if (rc) goto done;
    rc = fm1_vp_verify_read(io, out, &state, 6u); if (rc) goto done;
    rc = fm1_vp_verify_read(io, out, &state, 7u); if (rc) goto done;
    FM1_VP_STEP(12u, 3u, 0x30u, 0x30u, 0u);
    /* V15 performs these reads even though their values are not subsequently
     * used. Verify them against the last successful writes. */
    rc = fm1_vp_verify_read(io, out, &state, 4u); if (rc) goto done;
    rc = fm1_vp_verify_read(io, out, &state, 3u); if (rc) goto done;
    rc = fm1_vp_verify_read(io, out, &state, 3u); if (rc) goto done;
    FM1_VP_STEP(13u, 0u, 0u,    0x80u, 0u);
    FM1_VP_STEP(14u, 9u, 0x3fu, 0u,    0u);
    FM1_VP_STEP(15u, 2u, 0u,    0x40u, 0u);
    FM1_VP_STEP(16u,10u, 0x20u, 0u,    0u);
    FM1_VP_STEP(17u, 7u, 0x0fu, 0x0fu, 0u); /* SYSVDD = 15 */
#undef FM1_VP_STEP
#undef FM1_VP_DEFER
    rc = fm1_vp_sample(io, &out->after); if (rc) goto done;
    for (i = 0u; i < FM1_VP_COUNT; ++i)
        if (((out->after.raw[i] ^ state.raw[i]) & (i == 8u ? 0x3fu : 0xffu)) != 0u)
            { rc = FM1_VP_STATE_CHANGED; goto done; }
    rc = fm1_vp_guard(io);
done:
    out->timeouts = prior_timeouts + io->timeouts(io->ctx);
    out->status = rc;
    return rc;
}
