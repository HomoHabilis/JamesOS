/* SPDX-License-Identifier: GPL-3.0-only
 * Private AC791N read-only clock-measurement prototype.
 *
 * Hardware provenance: JieLi AC79 SDK V1.2.14, include_lib/driver/cpu/wl82/asm/WL82.h
 * defines JL_GPCNT as { CON, NUM } at lsfr_base + map_adr(0x11, 0), and
 * include_lib/driver/device/gpcnt.h defines EN=CON[0], CSS=CON[3:1],
 * CLR_PEND=CON[6] (clear-pending command), PND=CON[7], GTS=CON[11:8],
 * GSS=CON[14:12]. The same header maps CSS value 0 to LSB and sample-source
 * value 1 to OSC; CYCLE_12 is 32*2^12 sample-clock cycles. The active
 * include_lib/driver/cpu/wl82/clock_hw.h maps GPCNT module clock to
 * CLK_CON1[6:4], with NULL=0 and HSB=3. No PLL or voltage registers are touched.
 *
 * This deliberately is not wired into the running firmware. It requires a
 * quiet main context, a running TIMER4 crystal timebase, disabled GPCNT, and
 * the GPCNT module mux at NULL. All hardware changes are temporary and restored.
 */
#pragma once
#include <stdint.h>

#define FM1_CM_CLK_CON1_ADDR 0x10010u
#define FM1_CM_SYS_DIV_ADDR  0x10008u
#define FM1_CM_CLK_CON0_ADDR 0x1000cu
#define FM1_CM_T4_CON_ADDR   0x10800u
#define FM1_CM_T4_CNT_ADDR   0x10804u
#define FM1_CM_T4_PRD_ADDR   0x10808u
#define FM1_CM_GPCNT_ADDR    0x11100u
#define FM1_CM_GPCNT_CON_ADDR (FM1_CM_GPCNT_ADDR + 0u)
#define FM1_CM_GPCNT_NUM_ADDR (FM1_CM_GPCNT_ADDR + 4u)

#define FM1_CM_MUX_MASK      (7u << 4)
#define FM1_CM_MUX_NULL      0u
#define FM1_CM_MUX_HSB       (3u << 4)

#define FM1_CM_CON_EN        (1u << 0)
#define FM1_CM_CON_CSS_MASK  (7u << 1)
#define FM1_CM_CON_CLR_PEND  (1u << 6)
#define FM1_CM_CON_PND       (1u << 7)
#define FM1_CM_CON_GTS_MASK  (15u << 8)
#define FM1_CM_CON_GSS_MASK  (7u << 12)
#define FM1_CM_CON_FIELDS    (FM1_CM_CON_EN | FM1_CM_CON_CSS_MASK | \
                              FM1_CM_CON_CLR_PEND | FM1_CM_CON_PND | \
                              FM1_CM_CON_GTS_MASK | FM1_CM_CON_GSS_MASK)

#define FM1_CM_CSS_LSB       0u
#define FM1_CM_GSS_OSC       1u
#define FM1_CM_CYCLE         12u
#define FM1_CM_OSC_HZ        24000000u
#define FM1_CM_SAMPLE_CYCLES (32u << FM1_CM_CYCLE)
#define FM1_CM_T4_TIMEOUT_TICKS (20u * 24000u) /* 20 ms at 24 MHz */
#define FM1_CM_T4_CON_LOW8   0x09u /* run, OSC source, /1 */
#define FM1_CM_MAIN_SEL_MASK (7u << 6)
#define FM1_CM_MAIN_SEL_PLL  (7u << 6)
#define FM1_CM_SYS_HSB_RAW   1u /* captured SYS_DIV[17:16], divisor raw+1 */
#define FM1_CM_SYS_LSB_RAW   2u /* captured SYS_DIV[10:8], divisor raw+1 */

typedef struct {
    void *ctx;
    uint32_t (*read32)(void *ctx, uint32_t address);
    void (*write32)(void *ctx, uint32_t address, uint32_t value);
    uint32_t (*irq_save_disable)(void *ctx);
    void (*irq_restore)(void *ctx, uint32_t state);
} fm1_clock_measure_io_t;

typedef enum {
    FM1_CM_OK = 0,
    FM1_CM_BAD_IO,
    FM1_CM_BAD_T4,
    FM1_CM_BUSY,
    FM1_CM_TIMEOUT,
    FM1_CM_BAD_COUNT
} fm1_clock_measure_result_t;

/* Measure LSB against the same nominal 24 MHz OSC used by TIMER4. The count
 * gate is 32*2^12 OSC cycles (5.461 ms); timeout is 20 ms by unsigned T4 delta.
 * On success, both outputs are rounded to nearest Hz. SYS=LSB*2*3 because the
 * captured SYS_DIV encodings are HSB raw 1 and LSB raw 2. Caller must stop
 * synthesis and call from main context; interrupts are masked only around the
 * two short register setup/restore sections, never during the measurement. */
static inline fm1_clock_measure_result_t
fm1_clock_measure_once(const fm1_clock_measure_io_t *io,
                       uint32_t *lsb_hz_out, uint32_t *sys_hz_out,
                       uint32_t *raw_count_out)
{
    uint32_t irq, old_con, old_mux, start, count = 0u, sys_div;
    uint32_t timed_out = 0u, started = 0u;
    uint32_t setup_con, cleanup_con;
    uint64_t scaled;

    if (!io || !io->read32 || !io->write32 || !io->irq_save_disable ||
        !io->irq_restore || !lsb_hz_out || !sys_hz_out || !raw_count_out)
        return FM1_CM_BAD_IO;
    if ((io->read32(io->ctx, FM1_CM_T4_CON_ADDR) & 0xffu) != FM1_CM_T4_CON_LOW8 ||
        io->read32(io->ctx, FM1_CM_T4_PRD_ADDR) != 0xffffffffu)
        return FM1_CM_BAD_T4;
    if ((io->read32(io->ctx, FM1_CM_CLK_CON0_ADDR) & FM1_CM_MAIN_SEL_MASK) !=
        FM1_CM_MAIN_SEL_PLL)
        return FM1_CM_BAD_IO;
    sys_div = io->read32(io->ctx, FM1_CM_SYS_DIV_ADDR);
    if (((sys_div >> 16) & 3u) != FM1_CM_SYS_HSB_RAW ||
        ((sys_div >> 8) & 7u) != FM1_CM_SYS_LSB_RAW)
        return FM1_CM_BAD_IO;

    irq = io->irq_save_disable(io->ctx);
    old_mux = io->read32(io->ctx, FM1_CM_CLK_CON1_ADDR) & FM1_CM_MUX_MASK;
    old_con = io->read32(io->ctx, FM1_CM_GPCNT_CON_ADDR);
    /* PND is a status/interrupt-request bit. A pre-existing pending event is
     * not safely restorable after the measurement clears its own completion. */
    if (old_mux != FM1_CM_MUX_NULL ||
        (old_con & (FM1_CM_CON_EN | FM1_CM_CON_PND))) {
        io->irq_restore(io->ctx, irq);
        return FM1_CM_BUSY;
    }

    start = io->read32(io->ctx, FM1_CM_T4_CNT_ADDR);
    io->write32(io->ctx, FM1_CM_CLK_CON1_ADDR,
                (io->read32(io->ctx, FM1_CM_CLK_CON1_ADDR) & ~FM1_CM_MUX_MASK) |
                FM1_CM_MUX_HSB);
    setup_con = (old_con & ~FM1_CM_CON_FIELDS) |
                FM1_CM_CON_CLR_PEND | (FM1_CM_CYCLE << 8) |
                (FM1_CM_GSS_OSC << 12) | (FM1_CM_CSS_LSB << 1) |
                FM1_CM_CON_EN;
    io->write32(io->ctx, FM1_CM_GPCNT_CON_ADDR, setup_con);
    started = 1u;
    io->irq_restore(io->ctx, irq);

    for (;;) {
        uint32_t con = io->read32(io->ctx, FM1_CM_GPCNT_CON_ADDR);
        if (con & FM1_CM_CON_PND) {
            count = io->read32(io->ctx, FM1_CM_GPCNT_NUM_ADDR);
            break;
        }
        if ((uint32_t)(io->read32(io->ctx, FM1_CM_T4_CNT_ADDR) - start) >=
            FM1_CM_T4_TIMEOUT_TICKS) {
            timed_out = 1u;
            break;
        }
    }

    if (started) {
        irq = io->irq_save_disable(io->ctx);
        /* Disable and clear pending first, with the temporary module clock
         * still selected; then restore the exact prior writable CON state. */
        cleanup_con = old_con & ~(FM1_CM_CON_EN | FM1_CM_CON_CLR_PEND |
                                  FM1_CM_CON_PND | FM1_CM_CON_GSS_MASK |
                                  FM1_CM_CON_GTS_MASK | FM1_CM_CON_CSS_MASK);
        io->write32(io->ctx, FM1_CM_GPCNT_CON_ADDR,
                    cleanup_con | FM1_CM_CON_CLR_PEND);
        io->write32(io->ctx, FM1_CM_GPCNT_CON_ADDR,
                    old_con & ~(FM1_CM_CON_CLR_PEND | FM1_CM_CON_PND));
        io->write32(io->ctx, FM1_CM_CLK_CON1_ADDR,
                    (io->read32(io->ctx, FM1_CM_CLK_CON1_ADDR) & ~FM1_CM_MUX_MASK) |
                    old_mux);
        io->irq_restore(io->ctx, irq);
    }

    if (timed_out)
        return FM1_CM_TIMEOUT;
    if (count == 0u)
        return FM1_CM_BAD_COUNT;
    if (io->read32(io->ctx, FM1_CM_SYS_DIV_ADDR) != sys_div ||
        (io->read32(io->ctx, FM1_CM_CLK_CON0_ADDR) & FM1_CM_MAIN_SEL_MASK) !=
        FM1_CM_MAIN_SEL_PLL)
        return FM1_CM_BAD_IO;
    scaled = (uint64_t)count * (uint64_t)FM1_CM_OSC_HZ;
    *lsb_hz_out = (uint32_t)((scaled + FM1_CM_SAMPLE_CYCLES / 2u) /
                             FM1_CM_SAMPLE_CYCLES);
    *sys_hz_out = *lsb_hz_out * 6u;
    *raw_count_out = count;
    return FM1_CM_OK;
}

/* Optional target adapter. Keep out of host tests; inclusion alone performs
 * no access. Caller must ensure main context is quiet and synthesis stopped. */
#ifndef FM1_CLOCK_MEASURE_NO_TARGET_ADAPTER
#include "fm1_irq.h"
static inline uint32_t fm1_cm_target_read(void *ctx, uint32_t address)
{ (void)ctx; return *(volatile uint32_t *)(uintptr_t)address; }
static inline void fm1_cm_target_write(void *ctx, uint32_t address, uint32_t value)
{ (void)ctx; *(volatile uint32_t *)(uintptr_t)address = value; }
static inline uint32_t fm1_cm_target_irq_save(void *ctx)
{ uint32_t old; (void)ctx; old = fm1_icfg(); fm1_irq_off(); return old; }
static inline void fm1_cm_target_irq_restore(void *ctx, uint32_t old)
{
    (void)ctx;
    fm1_icfg_set(old);
    if (old & 0x100u) __asm__ volatile("csync\n\tsti" ::: "memory");
    else __asm__ volatile("csync\n\tcli" ::: "memory");
}
static inline fm1_clock_measure_result_t
fm1_clock_measure_once_target(uint32_t *lsb_hz, uint32_t *sys_hz,
                              uint32_t *raw_count)
{
    const fm1_clock_measure_io_t io = {
        0, fm1_cm_target_read, fm1_cm_target_write,
        fm1_cm_target_irq_save, fm1_cm_target_irq_restore
    };
    return fm1_clock_measure_once(&io, lsb_hz, sys_hz, raw_count);
}
#endif
