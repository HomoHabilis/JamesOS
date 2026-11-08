#include "../firmware/src/midi_clock.h"
#include <assert.h>
#include <stdio.h>

static void setup(fm1_midi_clock_t *c, uint64_t *t, uint32_t period)
{
    uint32_t f=0;
    *t=0; fm1_mc_init(c,48000u);
    assert(!fm1_mc_event(c,0xfau,0u));
    assert(fm1_mc_event(c,0xf8u,0u)&FM1_MC_START);
    for (int i=0;i<55;++i) { *t+=period; f=fm1_mc_event(c,0xf8u,*t); }
    assert(c->locked && !(f&FM1_MC_INVALID));
}
static void change(uint32_t old_period, uint32_t new_period)
{
    fm1_midi_clock_t c; uint64_t t, last; uint32_t f, saw_suspect=0, relocked=0;
    setup(&c,&t,old_period);
    for (uint32_t i=0;i<80u;++i) {
        t+=new_period; last=c.phase_q16;
        f=fm1_mc_event(&c,0xf8u,t);
        assert(!(f & (FM1_MC_INVALID|FM1_MC_LOST)));
        assert(c.phase_q16>=last);
        if (f&FM1_MC_SUSPECT) { saw_suspect=1; assert(!c.locked); }
        if (f&FM1_MC_LOCKED) relocked=1;
    }
    assert(saw_suspect && relocked && c.locked);
    assert(c.period_q16 >= ((uint64_t)new_period-1u)*65536u &&
           c.period_q16 <= ((uint64_t)new_period+1u)*65536u);
}
int main(void)
{
    fm1_midi_clock_t c; uint64_t t; uint32_t f;
    change(1000u,500u);  /* 120 -> 240 */
    change(500u,1000u);  /* 240 -> 120 */
    change(2000u,400u);  /* 60 -> 300 */
    change(4000u,400u);  /* 30 -> 300 */
    change(400u,2000u);  /* 300 -> 60 */
    change(1000u,2000u); /* 120 -> 60 */
    setup(&c,&t,1000u);
    f=fm1_mc_event(&c,0xf8u,t+500u);
    assert((f&FM1_MC_REJECTED) && c.locked);
    f=fm1_mc_event(&c,0xf8u,t+1000u);
    assert((f&FM1_MC_REJECTED) && c.locked);
    for (int i=2;i<=240;++i) {
        f=fm1_mc_event(&c,0xf8u,t+(uint32_t)i*1000u);
        assert(!(f&(FM1_MC_INVALID|FM1_MC_LOST)));
    }
    assert(c.locked && c.period_q16>((uint64_t)995u<<16) &&
           c.period_q16<((uint64_t)1005u<<16));
    /* A single implausibly early pulse does not stop or de-lock the clock. */
    setup(&c,&t,1000u);
    f=fm1_mc_event(&c,0xf8u,t+100u);
    assert((f&FM1_MC_REJECTED) && !(f&(FM1_MC_INVALID|FM1_MC_SUSPECT)) && c.running && c.locked);
    f=fm1_mc_event(&c,0xf8u,t+1000u);
    assert(!(f&(FM1_MC_INVALID|FM1_MC_LOST|FM1_MC_SUSPECT)) && c.running);
    /* Sample-level timestamp jitter at the 300 BPM endpoint remains valid. */
    setup(&c,&t,2000u);
    for (uint32_t i=0;i<80u;++i) {
        t+=(i&1u) ? 401u : 399u;
        f=fm1_mc_event(&c,0xf8u,t);
        assert(!(f&(FM1_MC_INVALID|FM1_MC_LOST)));
    }
    assert(c.locked && c.period_q16>((uint64_t)398u<<16) &&
           c.period_q16<((uint64_t)402u<<16));
    setup(&c,&t,1000u);
    /* An unambiguous same-frame duplicate remains invalid. */
    assert(fm1_mc_event(&c,0xf8u,t)&FM1_MC_INVALID);
    puts("PASS: faster/slower clock reacquisition and isolated early pulse");
    return 0;
}
