#include "../firmware/src/midi_clock_timeline.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    fm1_mct_anchor_t a;
    fm1_mct_queue_t q;
    fm1_mct_event_t e;
    uint64_t frame, scheduled;
    uint32_t tick = 0xfffff000u;
    assert(fm1_mct_anchor_init(&a,24000000u,48000u,240u,100u,tick,48000u,0u));
    /* True edges advance 120000 ticks. ISR jitter must not accumulate. */
    for (uint32_t i=1;i<=1000u;i++) {
        uint32_t jitter = (i & 1u) ? 80u : (uint32_t)-80;
        tick+=120000u;
        assert(fm1_mct_edge(&a,tick+jitter,(uint8_t)(i&1u)));
        int32_t error=(int32_t)(a.expected_tick-tick);
        assert(error >= -100 && error <= 100);
        assert(a.edge_frame==48000u+(uint64_t)i*240u);
    }
    tick=a.expected_tick;
    assert(fm1_mct_map(&a,tick+501u,1000000u,&frame) && frame==a.edge_frame+2u);
    assert(fm1_mct_map(&a,tick-501u,1000000u,&frame) && frame==a.edge_frame-1u);
    assert(!fm1_mct_map(&a,tick+1000001u,1000000u,&frame));
    fm1_mct_queue_init(&q,80000u);
    /* Physical event time vs next writable (one half ahead) means no
     * retroactive write; future event waits for an eligible block. */
    assert(fm1_mct_push(&q,0xfau,tick));
    assert(fm1_mct_push(&q,0xf8u,tick+120000u));
    assert(fm1_mct_push(&q,0xf8u,tick+120000u));
    assert(fm1_mct_pop(&q,&a,a.edge_frame+240u,1000000u,&e,&scheduled)==1);
    assert(e.status==0xfau && e.flags==FM1_MCT_LATE && scheduled==a.edge_frame+240u);
    assert(fm1_mct_pop(&q,&a,a.edge_frame+240u,1000000u,&e,&scheduled)==1);
    assert(e.status==0xf8u && e.flags==0u && scheduled==a.edge_frame+240u);
    assert(fm1_mct_pop(&q,&a,a.edge_frame+240u,1000000u,&e,&scheduled)==1);
    assert(e.flags&FM1_MCT_AMBIGUOUS);
    assert(!fm1_mct_push(&q,0xf8u,tick+119999u)); /* order after drain */
    assert(fm1_mct_push(&q,0xf8u,tick+240000u));
    assert(fm1_mct_pop(&q,&a,a.edge_frame+240u,1000000u,&e,&scheduled)==0);
    assert(fm1_mct_pop(&q,&a,a.edge_frame+480u,1000000u,&e,&scheduled)==1);
    assert(scheduled==a.edge_frame+480u && !e.flags);
    /* Two-half output pipeline: an event at the current physical edge is
     * applied at the first still-writable playback block, not retroactively. */
    fm1_mct_queue_init(&q,80000u);
    assert(fm1_mct_push(&q,0xfau,tick));
    assert(fm1_mct_pop(&q,&a,a.edge_frame+2u*240u,1000000u,&e,&scheduled)==1);
    assert(scheduled==a.edge_frame+480u && (e.flags&FM1_MCT_LATE));
    /* Queue overflow is sticky and cannot silently discard a Stop. */
    fm1_mct_queue_init(&q,80000u);
    for(uint32_t i=0;i<FM1_MCT_QUEUE_CAPACITY;i++) assert(fm1_mct_push(&q,0xf8u,tick+i*100000u));
    assert(!fm1_mct_push(&q,0xfcu,tick+3200000u));
    assert(fm1_mct_pop(&q,&a,a.edge_frame,1000000u,&e,&scheduled)==-1);
    /* Same half, and a missed pair ending at opposite half, fail closed. */
    assert(!fm1_mct_edge(&a,tick+120000u,a.last_half) && !a.valid);
    assert(fm1_mct_anchor_init(&a,24000000u,48000u,240u,100u,tick,48000u,0u));
    assert(!fm1_mct_edge(&a,tick+360000u,1u) && !a.valid);
    /* Fractional timer ticks per half do not accumulate rounding error. */
    assert(fm1_mct_anchor_init(&a,24000000u,44100u,128u,10u,0u,0u,0u));
    for(uint32_t i=1;i<1000u;i++) {
        uint32_t t=(uint32_t)((uint64_t)i*128u*24000000u/44100u);
        assert(fm1_mct_edge(&a,t,(uint8_t)(i&1u)) && a.expected_tick==t);
    }
    /* Run an hour at each sign of 100 ppm skew, with initial and subsequent
     * bounded interrupt jitter in opposite directions, across timer wraps. */
    for (int sign=-1; sign<=1; sign+=2) for (uint32_t mode=0; mode<2u; ++mode) {
        uint32_t base=0xff000000u;
        assert(fm1_mct_anchor_init(&a,24000000u,48000u,240u,100u,base+100u,0u,0u));
        for (uint32_t i=1; i<=720000u; ++i) {
            uint64_t actual=(uint64_t)i*(uint32_t)(120000+12*sign);
            int32_t jitter=mode ? (int32_t)((i*1664525u+1013904223u)%201u)-100 :
                                  (i&1u) ? -100 : 100;
            uint32_t true_tick=base+(uint32_t)actual;
            assert(fm1_mct_edge(&a,true_tick+(uint32_t)jitter,(uint8_t)(i&1u)));
            int32_t err=(int32_t)(a.expected_tick-true_tick);
            assert(err >= -200 && err <= 200);
        }
    }
    puts("PASS: MIDI clock timeline wrap, jitter, drift, queue and discontinuity model");
    return 0;
}
