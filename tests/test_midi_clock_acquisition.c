/* Cold acquisition sweeps for jitter and endpoint timing; no hardware. */
#include "../firmware/src/midi_clock.h"
#include <assert.h>
#include <stdio.h>
static uint32_t step(uint32_t x) { x^=x<<13; x^=x>>17; x^=x<<5; return x; }
int main(void)
{
    const uint32_t tempos[]={30u,120u,240u,300u};
    for (uint32_t t=0u;t<sizeof tempos/sizeof tempos[0];++t) {
        uint32_t bpm=tempos[t], worst=0u;
        for (uint32_t seed=1u;seed<=300u;++seed) for (uint32_t drop=0u;drop<=1u;++drop) {
            fm1_midi_clock_t c; uint32_t x=seed, lock_count=0u;
            fm1_mc_init(&c,44100u);
            for (uint32_t i=0u;i<600u;++i) {
                x=step(x); int32_t jitter=(int32_t)(x%6001u)-3000;
                if (drop && i && i%48u==0u) continue;
                uint64_t us=(uint64_t)i*2500000u/bpm+3000u+jitter;
                uint32_t flags=fm1_mc_event(&c,0xf8u,us*44100u/1000000u);
                if (flags&FM1_MC_LOCKED) ++lock_count;
                assert(!(flags&(FM1_MC_INVALID|FM1_MC_LOST)));
                /* Two full windows of received edges are required. */
                if (i<48u) assert(!c.locked);
            }
            if (!(c.locked && lock_count && c.period_q16)) {
                fprintf(stderr,"unlocked bpm=%u seed=%u drop=%u period=%llu good=%u intervals=%u\n",bpm,seed,drop,(unsigned long long)(c.period_q16>>16),c.window_good,c.window_intervals); assert(0);
            }
            uint64_t numerator=(uint64_t)44100u*60u<<16, denominator=c.period_q16*24u;
            uint32_t measured=(uint32_t)((numerator+denominator/2u)/denominator);
            uint32_t error=measured>bpm ? measured-bpm : bpm-measured;
            if (error>worst) worst=error;
            if (numerator*100u < denominator*bpm*97u ||
                numerator*100u > denominator*bpm*103u) {
                fprintf(stderr,"bpm=%u seed=%u drop=%u measured=%u\n",bpm,seed,drop,measured);
                assert(0);
            }
        }
        printf("%u BPM: 300 jitter seeds, with and without drops; max integer error %u\n",bpm,worst);
    }
    /* At 30 BPM, miss the first expected clock after Start while the
     * provisional internal estimate is 120 BPM. Acquisition must not time
     * out or discard the pending transport merely because tempo is unknown. */
    {
        fm1_midi_clock_t c; uint64_t frame=0u; uint32_t flags;
        fm1_mc_init(&c,48000u); c.period_q16=(uint64_t)1000u<<16;
        assert(!fm1_mc_event(&c,0xfau,frame));
        assert(fm1_mc_event(&c,0xf8u,frame)&FM1_MC_START);
        frame=8000u; flags=fm1_mc_event(&c,0xf8u,frame);
        assert(!(flags&(FM1_MC_INVALID|FM1_MC_LOST)) && c.running);
        for (uint32_t i=0u;i<100u;++i) {
            frame+=4000u; flags=fm1_mc_event(&c,0xf8u,frame);
            assert(!(flags&(FM1_MC_INVALID|FM1_MC_LOST)));
            if (flags & FM1_MC_LOCKED) assert(c.phase_q16 == ((frame/4000u)<<16));
        }
        assert(c.locked && c.running);
    }
    /* Inferred missed clocks count toward musical phase at lock; phase is
     * held, then moves forward to the validated source position. */
    {
        fm1_midi_clock_t c; uint64_t previous=0u; uint32_t lock_pulse=0u;
        fm1_mc_init(&c,48000u); c.period_q16=(uint64_t)1000u<<16;
        fm1_mc_event(&c,0xfau,0u); fm1_mc_event(&c,0xf8u,0u);
        for (uint32_t i=1u;i<=100u;++i) {
            if (i==10u) continue;
            uint32_t flags=fm1_mc_event(&c,0xf8u,(uint64_t)i*4000u);
            assert(!(flags&(FM1_MC_INVALID|FM1_MC_LOST)));
            assert(c.phase_q16>=previous); previous=c.phase_q16;
            if (flags&FM1_MC_LOCKED) { lock_pulse=i; assert(c.phase_q16==((uint64_t)i<<16)); }
        }
        assert(lock_pulse && c.locked);
    }
    /* Seed 19 used to mistake a no-drop 300 BPM stream with ±4 ms
     * jitter for a missing pulse and silently advance phase by one. */
    {
        fm1_midi_clock_t c; uint32_t x=19u, refused=0u;
        fm1_mc_init(&c,44100u); c.period_q16=(uint64_t)(44100u/48u)<<16;
        fm1_mc_event(&c,0xfau,0u); fm1_mc_event(&c,0xf8u,0u);
        for (uint32_t i=1u;i<200u;++i) {
            x=step(x); int32_t jitter=(int32_t)(x%8001u)-4000;
            uint64_t frame=((uint64_t)i*2500000u/300u+4000u+jitter)*44100u/1000000u;
            uint32_t flags=fm1_mc_event(&c,0xf8u,frame);
            if (flags&(FM1_MC_INVALID|FM1_MC_LOST)) { refused=1u; break; }
            if (flags&FM1_MC_LOCKED) { assert(c.phase_q16==((uint64_t)i<<16)); break; }
        }
        assert(refused || c.locked);
    }
    /* Start during pre-lock clocks anchors at the first ordered clock,
     * even if its individual interval is too noisy for tempo tracking. */
    {
        fm1_midi_clock_t c; uint32_t x=19u;
        fm1_mc_init(&c,44100u);
        for (uint32_t i=0u;i<20u;++i) {
            x=step(x); int32_t jitter=(int32_t)(x%6001u)-3000;
            uint64_t frame=((uint64_t)i*2500000u/300u+3000u+jitter)*44100u/1000000u;
            if (i==13u) assert(!fm1_mc_event(&c,0xfau,frame));
            uint32_t flags=fm1_mc_event(&c,0xf8u,frame);
            if (i==13u) assert((flags&FM1_MC_START) && !(flags&FM1_MC_INVALID));
        }
    }
    puts("MIDI clock cold acquisition sweeps passed");
}
