#include "../firmware/src/midi_clock.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    fm1_midi_clock_t c;
    uint64_t t = 0u, last_phase;
    uint32_t flags, locks = 0u;
    static const int jitter[] = {0, 7, -5, 3, -4, 2, -3, 0};
    fm1_mc_init(&c, 48000u);
    assert(!fm1_mc_event(&c, 0xfau, t));
    assert(!c.running && c.pending);
    assert(fm1_mc_event(&c, 0xf8u, t) & FM1_MC_START);
    for (uint32_t i = 0u; i < 240u; ++i) {
        t += 1000u + jitter[i % 8u]; /* 120 BPM, 24 PPQN */
        last_phase = c.phase_q16;
        flags = fm1_mc_event(&c, 0xf8u, t);
        assert(!(flags & (FM1_MC_INVALID | FM1_MC_LOST)));
        assert(c.phase_q16 >= last_phase);
        if (flags & FM1_MC_LOCKED) ++locks;
    }
    assert(locks == 1u && c.locked);
    assert(c.period_q16 > (990u << 16) && c.period_q16 < (1010u << 16));
    assert(c.phase_q16 > 239u * 65536u && c.phase_q16 < 241u * 65536u);
    /* A missing clock is bridged without declaring loss or doubling tempo. */
    t += 2000u;
    assert(!(fm1_mc_event(&c, 0xf8u, t) & (FM1_MC_INVALID | FM1_MC_LOST)));
    assert(c.period_q16 > (990u << 16) && c.period_q16 < (1010u << 16));
    /* Duplicate and out-of-order timestamps cannot corrupt the filter. */
    assert(fm1_mc_event(&c, 0xf8u, t) & FM1_MC_INVALID);
    assert(fm1_mc_event(&c, 0xf8u, t - 1u) & FM1_MC_INVALID);
    assert(fm1_mc_event(&c, 0xfcu, t) & FM1_MC_STOP);
    last_phase = c.phase_q16;
    t += 1000u;
    assert(!(fm1_mc_event(&c, 0xf8u, t) & FM1_MC_INVALID));
    assert(c.phase_q16 == last_phase);
    assert(!fm1_mc_event(&c, 0xfbu, t));
    assert(!c.running);
    t += 1000u;
    assert(fm1_mc_event(&c, 0xf8u, t) & FM1_MC_CONTINUE);
    assert(c.running && c.phase_q16 == last_phase);
    t += 4801u; /* 100 ms is the timeout floor */
    assert(fm1_mc_advance(&c, t) & FM1_MC_LOST);
    assert(!c.locked && c.running); /* loss policy belongs to owner */
    assert(!(fm1_mc_advance(&c, t + 1u) & FM1_MC_LOST));
    t += 1000u;
    fm1_mc_event(&c, 0xf8u, t);
    uint32_t relocked = 0u;
    for (uint32_t i = 0u; i < 55u; ++i) {
        t += 1333u; flags = fm1_mc_event(&c, 0xf8u, t);
        if (flags & FM1_MC_LOCKED) relocked = 1u;
    }
    assert(relocked && !c.lost);
    assert(c.period_q16 > (1320u << 16) && c.period_q16 < (1340u << 16));
    assert(fm1_mc_event(&c, 0xfau, t) & FM1_MC_STOP);
    t += 1333u;
    assert(fm1_mc_event(&c, 0xf8u, t) & FM1_MC_START);
    assert(c.phase_q16 == 0u);
    fm1_mc_init(&c, 48000u);
    assert(!fm1_mc_event(&c, 0xfau, 0u));
    assert(fm1_mc_advance(&c, 4u * c.max_period + 1u) & FM1_MC_LOST);
    /* Phase accumulation must not depend on audio block partition. */
    {
        fm1_midi_clock_t a, b;
        fm1_mc_init(&a, 48000u); fm1_mc_init(&b, 48000u);
        a.running = b.running = 1u; a.locked = b.locked = 1u;
        a.period_q16 = b.period_q16 = (uint64_t)1000u << 16;
        fm1_mc_advance(&a, 1000u);
        for (uint32_t i = 1u; i <= 1000u; ++i) fm1_mc_advance(&b, i);
        assert(a.phase_q16 == b.phase_q16);
    }
    /* At a fractional 30 BPM period, both adjacent frame counts are valid. */
    fm1_mc_init(&c, 32000u); t = 0u; fm1_mc_event(&c, 0xf8u, t);
    for (uint32_t i = 0u; i < 55u; ++i) {
        t += i % 3u ? 2667u : 2666u;
        assert(!(fm1_mc_event(&c, 0xf8u, t) & FM1_MC_INVALID));
    }
    assert(c.locked);
    /* Several consecutive double intervals indicate a sustained tempo change. */
    fm1_mc_init(&c, 48000u); t = 0u; fm1_mc_event(&c, 0xf8u, t);
    for (uint32_t i = 0u; i < 55u; ++i) { t += 1000u; fm1_mc_event(&c, 0xf8u, t); }
    for (uint32_t i = 0u; i < 60u; ++i) { t += 2000u; fm1_mc_event(&c, 0xf8u, t); }
    assert(c.period_q16 > (1990u << 16) && c.period_q16 < (2010u << 16));
    puts("MIDI clock timing checks passed");
    return 0;
}
