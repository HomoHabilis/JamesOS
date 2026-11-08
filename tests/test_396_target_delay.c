/* Exercise the extracted actual 1495 target timer and callback table on host.
 * T4 is simulated; this does not establish silicon clock or analog settling. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/hal/fm1_vendor360_policy.h"

static uint32_t mock_t4_con, mock_t4_prd, mock_now, mock_step, script[4];
static uint32_t clock_regs[5];
static uint32_t script_len, script_pos;
static volatile uint32_t tick_calls;
#define FM1_T4_CON mock_t4_con
#define FM1_T4_PRD mock_t4_prd
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void)
{
    uint32_t result;
    ++tick_calls;
    if (script_len) {
        result = script[script_pos];
        if (script_pos + 1u < script_len) ++script_pos;
        return result;
    }
    result = mock_now;
    mock_now += mock_step;
    return result;
}

typedef struct { uint32_t r[32], writes, p3, rst_src; } fm1_vp_target_t;
static int fm1_vp_target_read(void *p, uint32_t r, uint32_t *out)
{ fm1_vp_target_t *t=p; assert(r<32u); *out=t->r[r]; return 1; }
static int fm1_vp_target_write(void *p, uint32_t r, uint32_t v)
{
    fm1_vp_target_t *t=p;
    assert(r<32u);
    ++t->writes;
    t->r[r] = r==0x11u ? v & 0x3fu : v;
    return 1;
}
static int fm1_vp_target_or(void *p, uint32_t r, uint32_t v)
{
    fm1_vp_target_t *t=p;
    assert(r==0x11u && (v==0x40u || v==1u));
    ++t->writes;
    if (v==1u) t->r[r] |= v; /* clear-pending bit is a self-clearing pulse */
    return 1;
}
static int fm1_vp_target_held(void *p) { (void)p; return 1; }
static int fm1_vp_target_irq_disabled(void *p) { (void)p; return 1; }
static uint32_t fm1_vp_target_timeouts(void *p) { (void)p; return 0u; }
static uint32_t test_mmio_read(uint32_t address)
{
    switch (address) {
    case 0x10008u: return clock_regs[0];
    case 0x1000cu: return clock_regs[1];
    case 0x10014u: return clock_regs[2];
    case 0x119a8u: return clock_regs[3];
    case 0x119acu: return clock_regs[4];
    default: assert(0); return 0u;
    }
}

/* This file is generated afresh from the candidate target header by the
 * runner, so the waits and target callback wiring exercised here are real. */
#include "target_396_delay_extracted.h"

static void timer_reset(void)
{
    mock_t4_con=9u; mock_t4_prd=0xffffffffu;
    mock_now=100u; mock_step=1u;
    script_len=script_pos=tick_calls=0u;
    clock_regs[0]=0x00010200u; clock_regs[1]=0x000001c1u;
    clock_regs[2]=7u; clock_regs[3]=0x0940022bu; clock_regs[4]=0x075030c4u;
}
static void script3(uint32_t a, uint32_t b, uint32_t c)
{
    script[0]=a;script[1]=b;script[2]=c;
    script_len=3u;script_pos=tick_calls=0u;
}
static void init_reset_sample(fm1_vp_target_t *t)
{
    static const uint32_t bytes[11] =
        {0x77,0x60,0x18,0x44,0x0c,0,0,0x1e,0x3d,0,0x20};
    uint32_t i;
    memset(t,0,sizeof *t);
    t->p3=0x40u;t->rst_src=1u;
    for (i=0u;i<11u;i++) t->r[fm1_vp_reg(i)]=bytes[i];
}

int main(void)
{
    fm1_vp_target_t target;
    fm1_vp_io_t io;
    fm1_vp_report_t report;
    timer_reset();
    script3(100u,24099u,24100u);
    assert(fm1_vp_target_handoff_loop(0) && tick_calls==3u);
    /* One tick short is not enough even after the bounded clock-stall limit. */
    timer_reset(); script3(100u,24099u,24099u);
    assert(!fm1_vp_target_handoff_loop(0) && tick_calls==0x500001u);
    /* Unsigned elapsed arithmetic accepts exactly 24000 across wrap. */
    timer_reset(); script3(0xfffffff0u,0x00005dafu,0x00005db0u);
    assert(fm1_vp_target_handoff_loop(0) && tick_calls==3u);
    timer_reset(); mock_step=0u;
    assert(!fm1_vp_target_handoff_loop(0) && tick_calls==0x500001u);
    timer_reset(); mock_t4_con=8u;
    assert(!fm1_vp_target_handoff_loop(0) && tick_calls==0u);
    timer_reset(); mock_t4_prd=0u;
    assert(!fm1_vp_target_handoff_loop(0) && tick_calls==0u);

    /* Preflight checks progress and clock configuration before any P33 write. */
    timer_reset(); script3(100u,100u,101u);
    assert(fm1_vp_target_timer(0) && tick_calls==3u);
    timer_reset(); mock_step=0u;
    assert(!fm1_vp_target_timer(0) && tick_calls==1000001u);
    timer_reset(); mock_t4_con=8u;
    assert(!fm1_vp_target_timer(0) && tick_calls==0u);
    timer_reset(); mock_t4_prd=0u;
    assert(!fm1_vp_target_timer(0) && tick_calls==0u);

    init_reset_sample(&target); io=fm1_vp_target_ops(&target);
    assert(io.timer_ready==fm1_vp_target_timer);
    assert(io.handoff_loop==fm1_vp_target_handoff_loop);
    timer_reset(); mock_step=0u;
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_WAIT_ERROR);
    assert(target.writes==0u && report.write_count==0u);
    /* Replay the former 360 source+PLL, and each damaged 396 fingerprint
     * word, through the real target gate: no timer test or supply writes. */
    for (uint32_t i=0u;i<5u;i++) {
        init_reset_sample(&target); timer_reset(); clock_regs[i]^=1u;
        assert(fm1_vp_run(&io,0u,&report)==FM1_VP_INELIGIBLE);
        assert(target.writes==0u && tick_calls==0u);
    }
    init_reset_sample(&target); timer_reset();
    clock_regs[2]=6u;clock_regs[4]=0x0750310cu;
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_INELIGIBLE);
    assert(target.writes==0u && tick_calls==0u);
    init_reset_sample(&target); timer_reset(); mock_step=24000u;
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_OK);
    assert(target.writes==20u && report.write_count==20u);

    /* Timer can stop after a successful readiness test. The first handoff
     * then stops the sequence immediately after its own checked write. */
    init_reset_sample(&target); timer_reset();
    script[0]=0u;script[1]=1u;script[2]=1u;script[3]=1u;
    script_len=4u;
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_WAIT_ERROR);
    assert(target.writes==4u && report.write_count==4u && report.last_step==4u);

    /* Timer progresses through preflight and the first handoff, then stalls.
     * The second handoff fails and no subsequent supply write is attempted. */
    init_reset_sample(&target); timer_reset();
    script[0]=0u;script[1]=1u;script[2]=1u;script[3]=24001u;
    script_len=4u;
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_WAIT_ERROR);
    assert(target.writes==5u && report.write_count==5u && report.last_step==5u);
    puts("PASS: extracted 396 target waits, exact boundary, wrap, stalled timer and policy callback wiring");
    return 0;
}
