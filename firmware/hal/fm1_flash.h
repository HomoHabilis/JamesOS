/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 SPI NOR at runtime: erase / program / read. Used only when
 * FELUCCA_FLASH is 1.
 *
 * Everything that executes while the SFC (XIP) is switched off lives in
 * .ram_text and touches only SFRs, the stack and RAM buffers: no .rodata, no
 * libcalls, no XIP-resident helpers. Call the fl_*_ram() entry points with
 * interrupts disabled. Same sequence as the AC79 SDK flash driver
 * (enter_spi_code, norflash_erase, norflash_wait_ok, exit_spi_code).
 *
 * Slow parts (busy wait after an erase or a page program). XIP is never
 * switched back on while status bit 0 (busy) reads set: a NOR part does not
 * answer reads while it erases or programs. Two bounds per operation, timed
 * from before the command is sent (TIMER4, 24 MHz, wrap-safe):
 *   normal    erase 500 ms, page 10 ms   busy clears within it:    0
 *   extended  erase 2 s,    page 50 ms   busy clears within it:    FL_LATE
 *   beyond    reset the part (66h, 99h, one chip select each), wait 20 ms:
 *             answers -> FL_EHARD, then XIP on; still busy -> CPU reset from
 *             RAM (FL_CPU_RESET, PWR_CON core reset), XIP never comes back.
 * Return codes of fl_erase4k_ram / fl_prog_ram:
 *   0 ok; FL_LATE (1) done after the normal bound, the data IS written: success
 *   (fl_erase4k / fl_write turn it into 0, so their callers only ever see 0);
 *   -1 bad range, -2 continuous-read XIP mode, -3 WEL did not set (nothing was
 *   issued); FL_EHARD (-5) the part had to be reset: content undefined, a failed
 *   write. -4 (old: bound passed, XIP restored on a busy part) is not returned.
 * fl_stats: late operations, longest erase / page (us), part resets, JEDEC ID
 * (RAM, since boot; read-only diagnostic 0xA1).
 *
 * FL_HOST_MODEL (tests/test_flash_wait.c): the test defines RAMFN, T4_CNT,
 * FL_CPU_RESET and FL_WDT_FEED first and the SFR primitives (fl_cs, fl_tx,
 * fl_rx, fl_enter, fl_exit, fl_inval) and irq_save / irq_restore after; all
 * the rest is the device code.
 */
#pragma once
#include <stdint.h>
#include "fm1_xip.h"
#include "fm1_flash_ids.h"
#include "../product.h"
#if BR_PRODUCT_SMK37
#include "smk37/smk37_flash_map.h"
#endif
#ifndef FL_HOST_MODEL
#include "fm1_sys.h"                               /* fm1_core_reset, fm1_wdt_feed */
#endif

#ifndef FL_HOST_MODEL
#define RAMFN  __attribute__((section(".ram_text"), noinline, used))
#endif
#define RAMINL static inline __attribute__((always_inline))

/* XIP (0x02xxxxxx) -> RAM (0x01C0xxxx) is beyond the 23-bit call range:
 * call RAM code through a pointer the compiler cannot fold back. */
static inline void *fl_far(void *p) { void *volatile q = p; return q; }
#define FL_FAR(fn) ((__typeof__(&fn))fl_far((void *)&fn))

#ifndef FL_HOST_MODEL
#define REG32(a) (*(volatile uint32_t *)(a))
#define SFC_CON         REG32(0x40200u)            /* bit31 busy; [11:8] read mode */
#define SFCENC_CON      (*(volatile uint8_t *)0x40300u)  /* b0 decrypt, b1 plain window */
#define SFCENC_UNENC_H  REG32(0x40308u)            /* WO, XIP (CPU) address */
#define SFCENC_UNENC_L  REG32(0x4030Cu)            /* WO, XIP (CPU) address */
#define SPI0_CON        REG32(0x11C00u)            /* b15 PND, b14 PCLR, b12 DIR=rx */
#define SPI0_BUF        REG32(0x11C08u)
#define IOMAP_CON0      REG32(0x5101Cu)            /* b5: 1 = SFC owns the flash pins */
#define PORTD_OUT       REG32(0x500C0u)            /* PD0 = flash CS# */
#define PORTD_DIR       REG32(0x500C8u)
#define CACHE_CON       REG32(0x1EEE008u)          /* b14 = cache idle */
#define T4_CNT          REG32(0x10804u)            /* TIMER4, 24 MHz free-running */
/* Still busy 20 ms after the part reset: reset the CPU. fm1_core_reset is
 * FM1_INLINE (always_inline): one PWR_CON store and a spin, inside the RAM
 * function itself; the boot ROM then starts over. */
#define FL_CPU_RESET()  fm1_core_reset()
#define FL_WDT_FEED()   fm1_wdt_feed()
#endif /* !FL_HOST_MODEL */

#define FL_XIP(off)     FM1_XIP(off)              /* fm1_xip.h */
#if BR_PRODUCT_SMK37
/* SMK-37 Pro (from jamesos-k-pre-unify:firmware/hal/fm1_flash.h): windows and
 * FL_IN come from smk37/smk37_flash_map.h. */
/* Only the loader has a writable layout during SMK-37 bring-up. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) 0
#endif
#else
#define FL_DATA_LO      0x00097000u                /* Felucca main store */
#define FL_DATA_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* Felucca's own store (projects, user samples; settings) */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI))
/* Where the RAM driver may erase / program. The app build allows only its own
 * data regions; the update loader (firmware/loader) defines its own window. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
#endif /* BR_PRODUCT_SMK37 */

typedef struct { uint32_t sfc_con, spi_con; } fl_saved_t;

#ifndef FL_HOST_MODEL
RAMINL void fl_csync(void) { __asm__ volatile("csync" ::: "memory"); }
RAMINL void fl_cache_idle(void) { while (!(CACHE_CON & 0x4000u)) ; }
RAMINL void fl_delay(uint32_t n) { volatile uint32_t i = n; while (i--) ; }

RAMINL void fl_cs(int high)
{
    PORTD_DIR &= ~1u;
    if (high) PORTD_OUT |= 1u; else PORTD_OUT &= ~1u;
    fl_delay(10);
}
RAMINL int fl_spi_wait(void)
{
    uint32_t n = 50000000u;
    fl_csync();
    while (!(SPI0_CON & 0x8000u))
        if (--n == 0) break;
    SPI0_CON |= 0x4000u;
    return n ? 0 : -1;
}
RAMINL void fl_tx(uint8_t b)
{
    SPI0_CON |= 8u; SPI0_CON &= ~0x1000u;
    SPI0_BUF = b; fl_spi_wait();
    SPI0_CON &= ~8u;
}
RAMINL uint8_t fl_rx(void)
{
    uint8_t v;
    SPI0_CON |= 8u; SPI0_CON |= 0x1000u;
    SPI0_BUF = 0xFFu; fl_spi_wait();
    v = (uint8_t)SPI0_BUF;
    SPI0_CON &= ~8u;
    return v;
}
RAMINL int fl_enter(fl_saved_t *s)
{
    uint32_t mode;
    fl_cache_idle();
    s->sfc_con = SFC_CON;
    s->spi_con = SPI0_CON;
    mode = (s->sfc_con >> 8) & 0xFu;
    if (mode == 6u || mode == 7u)                 /* continuous-read XIP: not handled here */
        return -1;
    fl_csync();
    while ((int32_t)SFC_CON < 0) ;                /* SFC idle */
    SFC_CON = 0;                                  /* XIP OFF from here on */
    IOMAP_CON0 &= ~0x20u;                         /* pins -> SPI0 / GPIO */
    SPI0_CON = 0;
    SPI0_CON = 0x1029u;                           /* SDK default */
    fl_csync();
    return 0;
}
RAMINL void fl_exit(const fl_saved_t *s)
{
    SPI0_CON = s->spi_con;
    IOMAP_CON0 |= 0x20u;
    fl_csync();
    while ((int32_t)SFC_CON < 0) ;
    SFC_CON = s->sfc_con;                         /* XIP back on */
    fl_csync();
}
RAMINL void fl_inval(uint32_t off, uint32_t len)
{
    uint32_t a = FL_XIP(off) & ~31u, e = FL_XIP(off) + len;
    fl_cache_idle();
    for (; a < e; a += 32u) {
        fl_csync();
        __asm__ volatile("flushinv [%0]" :: "r"(a) : "memory");
        fl_csync();
        fl_cache_idle();
    }
}
#else
/* host model (tests/test_flash_wait.c): the simulated part, defined by the test */
static void fl_cs(int high);
static void fl_tx(uint8_t b);
static uint8_t fl_rx(void);
static int fl_enter(fl_saved_t *s);
static void fl_exit(const fl_saved_t *s);
static void fl_inval(uint32_t off, uint32_t len);
static uint32_t irq_save(void);
static void irq_restore(uint32_t old);
#endif /* !FL_HOST_MODEL */
RAMINL void fl_addr(uint32_t a) { fl_tx((uint8_t)(a >> 16)); fl_tx((uint8_t)(a >> 8)); fl_tx((uint8_t)a); }
RAMINL uint8_t fl_rdsr(uint8_t cmd) { uint8_t v; fl_cs(0); fl_tx(cmd); v = fl_rx(); fl_cs(1); return v; }
RAMINL void fl_wren(void) { fl_cs(0); fl_tx(0x06); fl_cs(1); }

#define FL_LATE         1                          /* done after the normal bound: success */
#define FL_EHARD        (-5)                       /* part reset: content undefined, failed write */
#define FL_ERASE_US     500000u                    /* generic NOR max 400 ms */
#define FL_ERASE_EXT_US 2000000u
#define FL_PROG_US      10000u                     /* generic NOR max 3 ms */
#define FL_PROG_EXT_US  50000u
#define FL_RESET_US     20000u
#define FL_TPU          24u                        /* TIMER4 ticks per us */

/* Since boot, RAM only. Written with interrupts off by the RAM functions;
 * read (word by word) by diagnostic 0xA1. */
typedef struct { uint32_t late, erase_max_us, prog_max_us, resets, jedec; } fl_stats_t;
static fl_stats_t fl_stats;

/* Poll busy from t0 (taken before the command). See the header comment.
 * After the part reset the same poll runs once more with no time left: busy
 * there means FL_CPU_RESET. (One copy of each SPI sequence: this is inlined
 * into both RAM entry points.) */
RAMINL int fl_wait_done(uint32_t t0, uint32_t normal_us, uint32_t ext_us,
                        uint32_t *max_us, uint32_t *took_us)
{
    uint32_t dt, limit = ext_us * FL_TPU, c;
    int rc = 0;
    for (;;) {
        dt = T4_CNT - t0;
        if (!(fl_rdsr(0x05) & 1u)) break;
        if (dt > limit) {
            if (rc) FL_CPU_RESET();                /* never back to XIP on a busy part */
            for (c = 0x66u;; c = 0x99u) {          /* enable reset, reset: one CS each */
                fl_cs(0); fl_tx((uint8_t)c); fl_cs(1);
                if (c == 0x99u) break;
            }
            fl_stats.resets++;
            rc = FL_EHARD;
            limit = T4_CNT;
            while (T4_CNT - limit < FL_RESET_US * FL_TPU) ;
            limit = 0;
        }
    }
    dt /= FL_TPU;
    *took_us = dt;
    if (dt > *max_us) *max_us = dt;
    if (!rc && dt > normal_us) { fl_stats.late++; rc = FL_LATE; }
    return rc;
}

/* ---- entry points: IRQs OFF, args/buffers in RAM ---- */

static RAMFN uint32_t fl_jedec_ram(void)                 /* 24-bit JEDEC ID */
{
    fl_saved_t s; uint32_t id;
    if (fl_enter(&s)) return 0;
    fl_cs(0); fl_tx(0x9F);
    id = (uint32_t)fl_rx() << 16; id |= (uint32_t)fl_rx() << 8; id |= fl_rx();
    fl_cs(1);
    fl_exit(&s);
    fl_stats.jedec = id;
    return id;
}

static RAMFN uint32_t fl_status_ram(void)                /* SR1 | SR2 << 8 */
{
    fl_saved_t s; uint32_t v;
    if (fl_enter(&s)) return 0xFFFFFFFFu;
    v = fl_rdsr(0x05) | ((uint32_t)fl_rdsr(0x35) << 8);
    fl_exit(&s);
    return v;
}

static RAMFN int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    fl_saved_t s; int rc; uint32_t t0;
    if (!FL_RANGE_OK(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    if (fl_enter(&s)) return -2;
    fl_wren();
    if (!(fl_rdsr(0x05) & 2u)) rc = -3;           /* WEL did not set */
    else {
        t0 = T4_CNT;
        fl_cs(0); fl_tx(0x20); fl_addr(off); fl_cs(1);
        fl_inval(off, 4096u);
        rc = fl_wait_done(t0, FL_ERASE_US, FL_ERASE_EXT_US, &fl_stats.erase_max_us, took_us);
    }
    fl_exit(&s);
    return rc;
}

static RAMFN int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    fl_saved_t s; int rc; uint32_t i, t0;
    if (!FL_RANGE_OK(off, n) ||
        n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u) return -1;    /* one page, no wrap */
    if (fl_enter(&s)) return -2;
    fl_wren();
    if (!(fl_rdsr(0x05) & 2u)) rc = -3;
    else {
        t0 = T4_CNT;
        fl_cs(0); fl_tx(0x02); fl_addr(off);
        for (i = 0; i < n; i++) fl_tx(src[i]);
        fl_cs(1);
        fl_inval(off, n);
        rc = fl_wait_done(t0, FL_PROG_US, FL_PROG_EXT_US, &fl_stats.prog_max_us, took_us);
    }
    fl_exit(&s);
    return rc;
}

static RAMFN int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n)   /* 0x0B fast read */
{
    fl_saved_t s; uint32_t i;
    if (fl_enter(&s)) return -2;
    fl_cs(0); fl_tx(0x0B); fl_addr(off); fl_tx(0xFF);
    for (i = 0; i < n; i++) dst[i] = fl_rx();
    fl_cs(1);
    fl_exit(&s);
    return 0;
}

/* ---- XIP side (normal .text) ---- */

#ifndef FL_HOST_MODEL
/* Once at boot, before any XIP read of the data region: map the data area
 * as an unencrypted XIP window (as the SDK's IOCTL_SET_VM_INFO does). */
static void fl_plain_window_init(void)
{
    SFCENC_CON &= (uint8_t)~2u;
#if BR_PRODUCT_SMK37
    SFCENC_UNENC_L = FL_XIP(SMK37_APP_HI);
#else
    SFCENC_UNENC_L = FL_XIP(0x93000u);            /* 0x0208F000 */
#endif
    SFCENC_UNENC_H = 0x07FFFFFFu;
    SFCENC_CON |= 2u;
    __asm__ volatile("cli" ::: "memory");
#if BR_PRODUCT_SMK37
    fl_inval(SMK37_APP_HI, SMK37_FLASH_SIZE - SMK37_APP_HI);
#else
    fl_inval(0x93000u, 0x100000u - 0x93000u);     /* drop any decrypted lines */
#endif
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* Follow AC79 SDK local_irq_save/local_irq_restore: clear the ICFG global
 * enable while in the critical section, then restore the exact prior ICFG. */
static inline uint32_t irq_save(void)
{
    uint32_t old, masked;
    __asm__ volatile("%0 = icfg" : "=r"(old) :: "memory");
    masked = old & ~0x100u;
    __asm__ volatile("icfg = %0" :: "r"(masked) : "memory");
    return old;
}
static inline void irq_restore(uint32_t old)
{
    __asm__ volatile("icfg = %0" :: "r"(old) : "memory");
}
#endif /* !FL_HOST_MODEL */

/* FL_LATE is success here: both return 0 for it. The watchdog (8 s in the app
 * and the loader) is fed before each erase, so one erase's wait (at most
 * 2 s + 20 ms) always starts with the whole period left. */

static int fl_erase4k(uint32_t off, uint32_t *took_us)
{
    uint32_t f = irq_save();
    FL_WDT_FEED();
#if BR_PRODUCT_SMK37
#if !defined(FELUCCA_LOADER)
    off = smk37_project_physical(off);
#endif
#endif
    int rc = FL_FAR(fl_erase4k_ram)(off, took_us);
    irq_restore(f);
    return rc == FL_LATE ? 0 : rc;
}

static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n)
{
    uint32_t took;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu);
        int rc;
        uint32_t f;
        if (chunk > n) chunk = n;
        f = irq_save();
#if BR_PRODUCT_SMK37 && !defined(FELUCCA_LOADER)
        rc = FL_FAR(fl_prog_ram)(smk37_project_physical(off), src_ram, chunk, &took);
#else
        rc = FL_FAR(fl_prog_ram)(off, src_ram, chunk, &took);
#endif
        irq_restore(f);
        if (rc < 0) return rc;                    /* FL_LATE: written, carry on */
        off += chunk; src_ram += chunk; n -= chunk;
    }
    return 0;
}

#ifndef FL_HOST_MODEL
/* Read back through XIP (needs fl_plain_window_init) and compare. */
static int fl_verify_xip(uint32_t off, const uint8_t *ref, uint32_t n)
{
    const volatile uint8_t *p = (const volatile uint8_t *)FL_XIP(off);
    uint32_t i;
    for (i = 0; i < n; i++) if (p[i] != ref[i]) return -(int)(i + 1);
    return 0;
}
#endif /* !FL_HOST_MODEL */
