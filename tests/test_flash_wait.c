/* SPDX-License-Identifier: GPL-3.0-only */
/* Host model of firmware/hal/fm1_flash.h's busy wait (slow SPI NOR parts).
 * The driver is the device code; only the SFR primitives, TIMER4, interrupts,
 * the CPU reset and the watchdog are replaced by a simulated part, which
 * traps (aborts) on any XIP restore, data read or command while it is busy.
 * Never touches hardware.
 *
 * FLW_E2E (FM-1 product): also the real callers on the simulated part: the
 * project store (instrument_project_device.c, project_store.c), the update
 * staging (ota.c ota_stage, with platform.c's ota_erase / ota_prog hooks), the
 * loader's ldr_erase / ldr_prog (extracted from loader.c) and the pattern
 * target raw callbacks (extracted from dinghy.c) when flw_extracted.inc has them. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FL_HOST_MODEL 1
#define RAMFN __attribute__((noinline))
#define US(x) ((uint64_t)(x) * 24u)
#define MS(x) ((uint64_t)(x) * 24000u)
#define STUCK (UINT64_MAX / 4u)

static uint64_t now;                    /* simulated TIMER4 ticks since the test started */
static uint32_t t4_base;                /* T4_CNT = t4_base + now (wraps) */
static uint32_t sim_t4(void) { now += 4u; return t4_base + (uint32_t)now; }
static jmp_buf cpu_jb;
static int cpu_resets, wdt_feeds, irq_depth;
#define T4_CNT sim_t4()
#define FL_CPU_RESET() do { cpu_resets++; longjmp(cpu_jb, 1); } while (0)
#define FL_WDT_FEED() (wdt_feeds++)

#ifdef FLW_RANGE_SMK37
#define FL_RANGE_OK(off, n) SMK37_APP_WRITE_OK(off, n)       /* platform.c, SMK-37 Pro */
#elif defined(FLW_E2E)
#define SETTINGS_AT 0x000FC000u                              /* platform.c, FM-1 */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || FL_IN(off, n, 0x97000u, 0xB1000u))
#endif
#include "../firmware/hal/fm1_flash.h"

#ifdef FLW_RANGE_SMK37
#define A0 0x9D000u                     /* SMK-37 Pro project window (identity remap) */
#else
#define A0 0x97000u                     /* FM-1 project store */
#endif

/* ---- the simulated part ---- */
static uint8_t mem[0x100000];
static int xip = 1, cs_high = 1, wel, rst_en, release_on_reset;
static uint8_t fr[300];
static uint32_t fn, rxi;
static uint64_t busy_until, erase_ticks = MS(18), prog_ticks = US(700);
static int hard_erase_at, erases, progs, part_resets, enters;
static uint32_t jedec_id = 0xB36014u;
typedef struct { uint8_t b[4]; uint32_t n; int busy; uint64_t t; } frame_t;
static frame_t trace[4096];
static uint32_t ntrace;

static int busy(void) { return now < busy_until; }
static void trap(const char *why)
{
    fprintf(stderr, "TRAP: %s (t=%llu us)\n", why, (unsigned long long)(now / 24u));
    abort();
}
static void frame_end(void)
{
    uint8_t cmd = fr[0];
    uint32_t a = ((uint32_t)fr[1] << 16 | (uint32_t)fr[2] << 8 | fr[3]) & 0xFFFFFu, i;
    frame_t *t;
    if (ntrace && cmd == 0x05 && trace[ntrace - 1].b[0] == 0x05 && trace[ntrace - 1].busy == busy() &&
        fn == 1 && trace[ntrace - 1].n == 1)
        t = &trace[ntrace - 1];                    /* repeated identical polls: one entry */
    else {
        if (ntrace >= sizeof trace / sizeof trace[0]) trap("trace full");
        t = &trace[ntrace++];
    }
    memcpy(t->b, fr, 4); t->n = fn; t->busy = busy(); t->t = now;
    if (!fn) trap("empty chip-select cycle");
    if (busy() && cmd != 0x05 && cmd != 0x66 && cmd != 0x99) trap("command while busy");
    if (cmd != 0x66 && cmd != 0x99) rst_en = 0;
    switch (cmd) {
    case 0x05: case 0x35: case 0x9F: case 0x0B: break;
    case 0x06: wel = 1; break;
    case 0x20:
        if (fn != 4 || !wel) trap("bad erase");
        a &= ~0xFFFu; erases++;
        if (hard_erase_at && erases == hard_erase_at) {
            memset(mem + a, 0x5A, 0x1000);              /* interrupted: content undefined */
            busy_until = STUCK;
        } else {
            memset(mem + a, 0xFF, 0x1000);
            busy_until = erase_ticks == STUCK ? STUCK : now + erase_ticks;
        }
        wel = 0;
        break;
    case 0x02:
        if (fn < 5 || !wel) trap("bad program");
        for (i = 4; i < fn; i++) mem[(a & ~0xFFu) | ((a + i - 4u) & 0xFFu)] &= fr[i];
        progs++;
        busy_until = prog_ticks == STUCK ? STUCK : now + prog_ticks;
        wel = 0;
        break;
    case 0x66: rst_en = fn == 1; break;
    case 0x99:
        if (rst_en && fn == 1) {
            part_resets++; wel = 0;
            if (release_on_reset) busy_until = now;
        }
        rst_en = 0;
        break;
    default: trap("unknown command");
    }
}
static void fl_cs(int high)
{
    now += 24u;
    if (!high) {
        if (!cs_high || xip) trap("chip select low twice or with XIP on");
        cs_high = 0; fn = rxi = 0;
    } else {
        if (cs_high) trap("chip select high twice");
        cs_high = 1; frame_end();
    }
}
static void fl_tx(uint8_t b)
{
    if (cs_high) trap("tx without chip select");
    if (fn < sizeof fr) fr[fn] = b;
    fn++; now += 8u;
}
static uint8_t fl_rx(void)
{
    uint32_t a;
    if (cs_high) trap("rx without chip select");
    now += 8u;
    switch (fr[0]) {
    case 0x05: return (uint8_t)((busy() ? 1u : 0u) | (wel ? 2u : 0u));
    case 0x35: return 0;
    case 0x9F: return (uint8_t)(jedec_id >> (16u - 8u * (rxi++ % 3u)));
    case 0x0B:
        if (busy()) trap("data read while busy");
        a = ((uint32_t)fr[1] << 16 | (uint32_t)fr[2] << 8 | fr[3]) + rxi++;
        return mem[a & 0xFFFFFu];
    default: trap("rx in unknown command"); return 0;
    }
}
static int fl_enter(fl_saved_t *s)
{
    if (!xip || !cs_high) trap("enter twice");
    xip = 0; enters++;
    s->sfc_con = 0x5AFEu; s->spi_con = 0x1029u;
    return 0;
}
static void fl_exit(const fl_saved_t *s)
{
    if (busy()) trap("XIP restored while the part is busy");
    if (xip || s->sfc_con != 0x5AFEu) trap("exit without enter");
    xip = 1;
}
static void fl_inval(uint32_t off, uint32_t len) { (void)off; (void)len; }
static uint32_t irq_save(void) { irq_depth++; return 0x100u; }
static void irq_restore(uint32_t f) { if (f != 0x100u || !irq_depth) trap("irq_restore"); irq_depth--; }

/* ---- scenario helpers ---- */
static void part_reset_model(uint64_t erase, uint64_t prog, int rel)
{
    erase_ticks = erase; prog_ticks = prog; release_on_reset = rel;
    busy_until = 0; wel = rst_en = 0; hard_erase_at = 0;
    ntrace = 0;
}
static int count_cmd(uint8_t c)
{
    uint32_t i, k = 0;
    for (i = 0; i < ntrace; i++) if (trace[i].b[0] == c) k++;
    return (int)k;
}
/* The trace of one erase / program: 06, 05 (WEL), the command, then only 05
 * polls; with a part reset: ... 05 (busy), 66, 99 (one byte, own chip select),
 * then one 05. */
static void check_trace(uint8_t cmd, int reset)
{
    uint32_t i = 0, k;
    assert(ntrace >= 4);
    assert(trace[0].b[0] == 0x06 && trace[0].n == 1);
    assert(trace[1].b[0] == 0x05 && trace[1].n == 1);
    assert(trace[2].b[0] == cmd);
    for (i = 3; i < ntrace && trace[i].b[0] == 0x05; i++) assert(trace[i].n == 1);
    if (!reset) { assert(i == ntrace); return; }
    assert(trace[i - 1].busy);                       /* the last poll before it said busy */
    k = i;
    assert(ntrace == k + 3);
    assert(trace[k].b[0] == 0x66 && trace[k].n == 1);
    assert(trace[k + 1].b[0] == 0x99 && trace[k + 1].n == 1);
    assert(trace[k + 2].b[0] == 0x05 && trace[k + 2].n == 1);
    assert(trace[k + 2].t - trace[k + 1].t >= MS(20) && trace[k + 2].t - trace[k + 1].t < MS(20) + US(50));
}

typedef struct { const char *name; uint64_t dur; int release, rc, cpu_reset; } case_t;

static void run_erase(const case_t *c, uint32_t base)
{
    uint32_t took = 0, late0 = fl_stats.late, resets0 = fl_stats.resets, emax0;
    int rc = 99, cpu0 = cpu_resets, pr0 = part_resets;
    volatile int reached = 0;
    t4_base = base; now = 0;
    part_reset_model(c->dur, US(700), c->release);
    fl_stats.erase_max_us = 0; emax0 = 0;
    if (!setjmp(cpu_jb)) {
        rc = fl_erase4k_ram(A0, &took);
        reached = 1;
    }
    if (c->cpu_reset) {
        assert(!reached && cpu_resets == cpu0 + 1);
        assert(!xip);                                  /* XIP never came back */
        assert(fl_stats.resets == resets0 + 1 && part_resets == pr0 + 1);
        assert(fl_stats.late == late0);
        check_trace(0x20, 1);
        printf("  erase %-28s CPU reset requested, XIP stays off, part resets %u\n", c->name, fl_stats.resets - resets0);
        xip = 1; cs_high = 1; busy_until = 0;          /* "boot" */
        return;
    }
    assert(reached && cpu_resets == cpu0 && xip);
    assert(rc == c->rc);
    check_trace(0x20, rc == FL_EHARD);
    assert(fl_stats.late == late0 + (rc == FL_LATE));
    assert(fl_stats.resets == resets0 + (rc == FL_EHARD));
    assert(part_resets == pr0 + (rc == FL_EHARD));
    assert(fl_stats.erase_max_us >= emax0 && fl_stats.erase_max_us == took);
    if (rc != FL_EHARD) {
        assert(took * 24ull >= c->dur && took * 24ull <= c->dur + US(100));
        assert(mem[A0] == 0xFF);
    } else {
        assert(took >= 2020000u && took <= 2020100u);
    }
    printf("  erase %-28s rc %2d  took %7u us  late %u resets %u%s\n", c->name, rc, took,
           fl_stats.late - late0, fl_stats.resets - resets0, base > 0xF0000000u ? "  (TIMER4 wraps)" : "");
}

static void run_prog(const case_t *c, uint32_t base)
{
    uint8_t src[256];
    uint32_t took = 0, late0 = fl_stats.late, resets0 = fl_stats.resets, i;
    int rc = 99, cpu0 = cpu_resets;
    volatile int reached = 0;
    t4_base = base; now = 0;
    for (i = 0; i < 256u; i++) src[i] = (uint8_t)(i * 7u + 1u);
    memset(mem + (A0 + 0x1000u), 0xFF, 256);
    part_reset_model(MS(18), c->dur, c->release);
    fl_stats.prog_max_us = 0;
    if (!setjmp(cpu_jb)) {
        rc = fl_prog_ram((A0 + 0x1000u), src, 256, &took);
        reached = 1;
    }
    if (c->cpu_reset) {
        assert(!reached && cpu_resets == cpu0 + 1 && !xip);
        assert(fl_stats.resets == resets0 + 1);
        check_trace(0x02, 1);
        printf("  page  %-28s CPU reset requested, XIP stays off\n", c->name);
        xip = 1; cs_high = 1; busy_until = 0;
        return;
    }
    assert(reached && xip && rc == c->rc);
    check_trace(0x02, rc == FL_EHARD);
    assert(fl_stats.late == late0 + (rc == FL_LATE));
    assert(fl_stats.resets == resets0 + (rc == FL_EHARD));
    assert(fl_stats.prog_max_us == took);
    if (rc != FL_EHARD) {
        assert(took * 24ull >= c->dur && took * 24ull <= c->dur + US(100));
        assert(!memcmp(mem + (A0 + 0x1000u), src, 256));
    } else {
        assert(took >= 70000u && took <= 70100u);
    }
    printf("  page  %-28s rc %2d  took %7u us  late %u resets %u%s\n", c->name, rc, took,
           fl_stats.late - late0, fl_stats.resets - resets0, base > 0xF0000000u ? "  (TIMER4 wraps)" : "");
}

static void units(uint32_t base_addr)
{
    static const case_t erase[] = {
        {"18 ms", MS(18), 0, 0, 0},
        {"499 ms", MS(499), 0, 0, 0},
        {"501 ms", MS(501), 0, FL_LATE, 0},
        {"1.9 s", MS(1900), 0, FL_LATE, 0},
        {"2.1 s, released by reset", MS(2100), 1, FL_EHARD, 0},
        {"2.1 s, reset ignored", MS(2100), 0, 0, 1},
        {"stuck forever", STUCK, 0, 0, 1},
    };
    static const case_t prog[] = {
        {"1 ms", MS(1), 0, 0, 0},
        {"9 ms", MS(9), 0, 0, 0},
        {"11 ms", MS(11), 0, FL_LATE, 0},
        {"49 ms", MS(49), 0, FL_LATE, 0},
        {"51 ms (done 1 ms after reset)", MS(51), 0, FL_EHARD, 0},
        {"stuck forever", STUCK, 0, 0, 1},
    };
    static const uint32_t bases[] = {0x12345678u, 0xFFFFFFFFu - 24000u * 300u};  /* 2nd: wraps 300 ms in */
    uint32_t b, i, took;
    (void)base_addr;
    for (b = 0; b < 2u; b++) {
        for (i = 0; i < sizeof erase / sizeof erase[0]; i++) run_erase(&erase[i], bases[b]);
        for (i = 0; i < sizeof prog / sizeof prog[0]; i++) run_prog(&prog[i], bases[b]);
    }
    /* longest seen survives smaller operations; counters only grow */
    now = 0; t4_base = 7; part_reset_model(MS(700), US(700), 0);
    fl_stats.erase_max_us = 0;
    assert(fl_erase4k_ram(A0, &took) == FL_LATE);
    part_reset_model(MS(20), US(700), 0);
    assert(fl_erase4k_ram(A0, &took) == 0 && fl_stats.erase_max_us >= 700000u);
    /* existing codes unchanged: range, misalignment, WEL that does not set */
    assert(fl_erase4k_ram(0x1000u, &took) == -1 && fl_erase4k_ram((A0 + 1u), &took) == -1);
    assert(fl_prog_ram((A0 + 0x1000u), mem, 0, &took) == -1 && fl_prog_ram((A0 + 0x10F0u), mem, 32, &took) == -1);
    /* the JEDEC ID is kept for the diagnostic */
    jedec_id = 0xB36014u; assert(fl_jedec_ram() == 0xB36014u && fl_stats.jedec == 0xB36014u && xip);
    /* fl_erase4k / fl_write: FL_LATE is success (0), FL_EHARD fails, watchdog fed per erase */
    {
        uint8_t src[600];
        int feeds = wdt_feeds;
        for (i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i ^ 0x3Cu);
        part_reset_model(MS(800), MS(30), 0);
        assert(fl_erase4k((A0 + 0x2000u), &took) == 0 && wdt_feeds == feeds + 1 && !irq_depth);
        assert(fl_write((A0 + 0x2010u), src, sizeof src) == 0 && !irq_depth);
        assert(!memcmp(mem + (A0 + 0x2010u), src, sizeof src));     /* every page after a late one too */
        part_reset_model(MS(2100), US(700), 1);
        assert(fl_erase4k((A0 + 0x2000u), &took) == FL_EHARD && !irq_depth);
        part_reset_model(MS(18), MS(60), 0);
        memset(mem + (A0 + 0x3000u), 0xFF, 0x1000);
        assert(fl_write((A0 + 0x3000u), src, sizeof src) == FL_EHARD && !irq_depth);
        assert(progs && mem[(A0 + 0x3100u)] == 0xFF);                  /* stopped after the failed page */
    }
    printf("units: all erase / page durations, codes, counters, reset framing and TIMER4 wrap passed\n");
}

#ifdef FLW_E2E
/* ================= the real callers on the simulated part ================= */
#define DINGHY_HOST 1
typedef struct { uint8_t data[3584]; } br_session_t;          /* session v17 size */
static br_session_t br_pending_save, br_loaded, current;
static uint8_t flash_ok = 1;
static int br_playing, tx, park;
static int br_engine_quiet(void) { return 1; }
static int br_fx_quiet(void) { return 1; }
static int br_session_save_blocked(void) { return 0; }
static int br_quiescent_transaction_begin(void) { if (tx) return 0; tx = 1; return 1; }
static void br_quiescent_transaction_end(void) { tx = 0; }
static int br_engine_pipeline_uncontained_fault_get(void) { return 0; }
static int br_capture(br_session_t *s) { *s = current; return 1; }
static int br_restore(const br_session_t *s) { current = *s; return 1; }
static int br_session_valid(const br_session_t *s) { return s->data[0] >= 0x42 && s->data[0] <= 0x45; }
static int flash_worker_park(void) { assert(!park); park = 1; return 0; }
static int flash_worker_resume(void) { assert(park); park = 0; return 0; }
static int st_read(uint32_t off, void *dst, uint32_t n)          /* platform.c st_read, FM-1 */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc) return rc;
        off += k; d += k; n -= k;
    }
    return 0;
}
#include "../firmware/src/instrument_project_device.c"

/* ota.c with platform.c's hooks (ota_erase / ota_prog / ota_fread, FM-1) */
#define FELUCCA_ID "FM-1_9262340"
#include "../firmware/src/ota.c"
static uint8_t *pkg, wire_frame[OTA_FRAME_MAX];
static uint32_t pkg_len, wire_n, ota_ms;
static int wire_pending;
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t d[640], a[530], msg[540];
    uint32_t z = ota_unpack7(p + 1, n - 2, d, sizeof d), off, len, s = 0, i;
    if (z < 7 || d[2] != 0x30) return 0;
    off = ota_rd32(d + 7); len = ota_rd16(d + 11);
    if (len > 512) return -1;
    memcpy(a, d + 6, 8);
    if (off == 0xE0000000u && len == 8) memcpy(a + 8, "success", 8);
    else { if (off > pkg_len || len > pkg_len - off) return -1; memcpy(a + 8, pkg + off, len); }
    for (i = 0; i < len + 8; i++) s += a[i];
    msg[0] = 0; msg[1] = 0x59; msg[2] = 0x30; msg[3] = (uint8_t)(len + 8); msg[4] = (uint8_t)((len + 8) >> 8); msg[5] = 0;
    memcpy(msg + 6, a, len + 8); msg[14 + len] = (uint8_t)~s;
    wire_n = ota_pack7(msg, len + 15, wire_frame); wire_pending = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!wire_pending) return 0; *p = wire_frame; *n = wire_n; return 1; }
static void ota_frame_done(void) { wire_pending = 0; }
static uint32_t ota_now_ms(void) { return (uint32_t)(now / 24000u) + ota_ms++; }
static void ota_idle(void) { wdt_feeds++; }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    if (flash_worker_park()) return -9;
    { int rc = fl_erase4k(off, &took); return flash_worker_resume() ? -9 : rc; }
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    if (flash_worker_park()) return -9;
    { int rc = fl_write(off, p, n); return flash_worker_resume() ? -9 : rc; }
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t s, int32_t c) { (void)s; (void)c; }
static int ota_commit(const uint8_t *p) { (void)p; return 0; }

/* loader.c's ldr_erase / ldr_prog and dinghy.c's raw target callbacks, extracted
 * by tests/run_flash_wait.py */
#define fm1_wdt_feed() (wdt_feeds++)
#define BR_PROJECT_RAW_BASE BR_PROJECT_BASE
static uint32_t br_ps_target_reserved_sectors;
static int br_ps_target_parked(void *v) { (void)v; return 1; }
static int br_ps_project_watch_ok_locked(void) { return 1; }
#define BR_PATTERN_RAM_RANGE(p, n) ((p) != NULL && (n) != 0u)
#include "flw_extracted.inc"

static void put_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) { put_le16(p, v); put_le16(p + 2, v >> 16); }
/* A package ota_stage accepts: UFW header + entries (flash.bin, ota.bin), an
 * own-loader ota.bin (outer head, inner head, "FELUCCA-LOADER-1" body). */
static void make_package(void)
{
    static uint8_t p[0x400 + 0x1000 + 0x3000];
    uint8_t *h = p, *e0 = p + 0x40, *e1 = p + 0x90, *o;
    uint32_t fl_off = 0x400u, fl_len = 0x1000u, ota_off = 0x1400u, len = 0x2000u, i;
    memset(p, 0, sizeof p);
    o = p + ota_off;
    for (i = 0; i < len; i++) o[0x20 + i] = (uint8_t)(i * 13u + 5u);
    memcpy(o + 0x48, "FELUCCA-LOADER-1", 16);              /* inside the body: b[0x48..] */
    /* inner head at o+0x20: crc16(+0x22,30) at +0x20, +0x24 length <= 0x14000, +0x28 load */
    put_le32(o + 0x24, 0x2000u); put_le32(o + 0x28, 0x01C0A800u);
    put_le16(o + 0x20, ota_crc16(o + 0x22, 30, 0));
    /* outer head: dcrc of the body at +2, 0x20 at +4, body length at +8, 0x41 at +12 */
    put_le16(o + 2, ota_crc16(o + 0x20, len, 0));
    put_le32(o + 4, 0x20u); put_le32(o + 8, len); o[12] = 0x41u;
    put_le16(o, ota_crc16(o + 2, 30, 0));
    /* entries, then the header (both JL-ciphered) */
    put_le16(e0, 0); put_le32(e0 + 8, fl_off); put_le32(e0 + 12, fl_len);
    put_le16(e1, 100); put_le32(e1 + 8, ota_off); put_le32(e1 + 12, len + 0x20u);
    ota_jl_enc(e0, 0x50); ota_jl_enc(e1, 0x50);
    put_le16(h + 8, 2); put_le32(h + 4, sizeof p);
    put_le16(h + 2, ota_crc16(h + 0x40, 2u * 0x50u, 0));
    put_le16(h, ota_crc16(h + 2, 0x3E, 0));
    ota_jl_enc(h, 0x40);
    pkg = p; pkg_len = sizeof p;
}

static void e2e(void)
{
    ps_info info;
    uint32_t late0, took, i;
    int feeds0, rc;
    t4_base = 0xFFFFFFFFu - 24000u * 5000u;                  /* TIMER4 wraps during the run */
    now = 0;
    memset(mem, 0xFF, sizeof mem);
    memset(&current, 0x34, sizeof current); current.data[0] = 0x42;

    /* 1. project SAVE, every erase 800 ms: completes, late operations counted */
    part_reset_model(MS(800), US(700), 0);
    late0 = fl_stats.late; feeds0 = wdt_feeds; erases = 0; fl_stats.erase_max_us = 0;
    assert(br_project_save(0, "SLOW", 0, &info) == PS_OK && !tx && !park && !irq_depth);
    assert(erases >= 2 && fl_stats.late == late0 + (uint32_t)erases && wdt_feeds >= feeds0 + erases);
    assert(fl_stats.erase_max_us >= 800000u && fl_stats.erase_max_us < 800100u);
    current.data[3] = 0;
    assert(br_project_load(0, &info) == PS_OK && current.data[3] == 0x34);
    printf("e2e: project SAVE, 800 ms erases: PS_OK, %d erases all late (late %u), reload ok\n",
           erases, fl_stats.late - late0);

    /* 2. project SAVE whose first erase hard-fails (part reset, content undefined):
     *    PS_IO exactly as for a write error today; slot 0 still loads the old copy */
    current.data[3] = 0x77;
    part_reset_model(MS(20), US(700), 1); erases = 0; hard_erase_at = 1;
    rc = br_project_save(0, "HARD", 0, &info);
    assert(rc == PS_IO && !tx && !park && !irq_depth && current.data[3] == 0x77);
    hard_erase_at = 0;
    current.data[3] = 0;
    assert(br_project_load(0, &info) == PS_OK && current.data[3] == 0x34);
    printf("e2e: project SAVE with a hard-failed erase: PS_IO (as a write error), old copy loads\n");
    /*    and the same failure at every later erase of a save: the store stays loadable */
    for (i = 2; i <= 3; i++) {
        current.data[3] = (uint8_t)(0x50 + i);
        part_reset_model(MS(20), US(700), 1); erases = 0; hard_erase_at = (int)i;
        rc = br_project_save(0, "HARD", 0, &info);
        hard_erase_at = 0;
        if (erases < (int)i) {                     /* this SAVE needed fewer erases */
            assert(rc == PS_OK);
            printf("e2e: SAVE %u used %d erases: no erase %u to fail (PS_OK)\n", i, erases, i);
            continue;
        }
        assert(rc == PS_IO);                      /* any failed erase fails the SAVE */
        current.data[3] = 0;
        assert(br_project_load(0, &info) == PS_OK);
        printf("e2e: hard failure at erase %u of a SAVE: PS_IO, slot still loads (byte %#x)\n", i,
               current.data[3]);
    }

    /* 3. update staging (ota_stage), every erase 800 ms: completes */
    make_package();
    part_reset_model(MS(800), US(700), 0);
    late0 = fl_stats.late; erases = 0;
    rc = ota_stage();
    assert(rc == 0 && erases == 5 && fl_stats.late == late0 + 5u && !park && !irq_depth);
    {
        static uint8_t back[0x2000];
        assert(st_read(0xE0020u, back, sizeof back) == 0);      /* staged body read back */
        assert(!memcmp(back, pkg + 0x1420u, sizeof back));
    }
    printf("e2e: update staging, 800 ms erases: ota_stage 0, 5 erases late, body verified\n");
    /*    hard failure of the 3rd staging erase: -8 as for any erase error today */
    part_reset_model(MS(20), US(700), 1); erases = 0; hard_erase_at = 3;
    rc = ota_stage();
    hard_erase_at = 0;
    assert(rc == -8 && !park && !irq_depth);
    printf("e2e: update staging with a hard-failed erase: ota_stage -8 (erase error)\n");

#ifdef FLW_HAVE_LOADER
    /* 4. the loader's flash hooks: late is success, hard fails, wdt fed per erase */
    part_reset_model(MS(1500), MS(30), 0); feeds0 = wdt_feeds;
    assert(ldr_erase(0xA2000u) == 0 && wdt_feeds == feeds0 + 1);
    assert(ldr_prog(0xA2000u, pkg, 256) == 0 && !memcmp(mem + 0xA2000u, pkg, 256));
    part_reset_model(MS(2100), US(700), 1);
    assert(ldr_erase(0xA2000u) == FL_EHARD);
    printf("e2e: loader ldr_erase / ldr_prog: late 0, hard %d\n", FL_EHARD);
#endif
#ifdef FLW_HAVE_RAW
    /* 5. dinghy.c raw target callbacks (the candidate's SAVE flash layer) */
    {
        uint8_t in[256], out[256];
        memset(in, 0x5A, sizeof in);
        memset(mem + 0xA0000u, 0xFF, 0x1000);
        br_ps_target_reserved_sectors = 0;
        part_reset_model(MS(900), MS(20), 0);
        assert(br_ps_target_erase_raw(0, 0xA0000u) == 0);
        assert(br_ps_target_program_raw(0, 0xA0000u, in, 256) == 0);
        assert(br_ps_target_read_raw(0, 0xA0000u, out, 256) == 0 && !memcmp(in, out, 256));
        part_reset_model(MS(2100), US(700), 1);
        assert(br_ps_target_erase_raw(0, 0xA1000u) == FL_EHARD && !irq_depth);
        printf("e2e: dinghy.c raw erase / program / read: late 0, hard %d\n", FL_EHARD);
    }
#endif
    (void)took;
}
#endif

int main(void)
{
    memset(mem, 0xFF, sizeof mem);
    units(0);
#ifdef FLW_E2E
    e2e();
#endif
    printf("flash wait: all checks passed (late %u, longest erase %u us, longest page %u us, part resets %u, CPU resets %d)\n",
           fl_stats.late, fl_stats.erase_max_us, fl_stats.prog_max_us, fl_stats.resets, cpu_resets);
    return 0;
}
