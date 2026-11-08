#include "../firmware/src/midi_clock_math.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint64_t rnd(uint64_t *s) { *s^=*s<<13; *s^=*s>>7; *s^=*s<<17; return *s; }
int main(void)
{
    uint64_t seed=0x123456789abcdef0ull, rem;
    assert(fm1_clock_div64(UINT64_MAX,0u,&rem)==UINT64_MAX && rem==UINT64_MAX);
    for (uint32_t i=0u;i<200000u;++i) {
        uint64_t n=rnd(&seed), d=rnd(&seed)|1u;
        assert(fm1_clock_div64(n,d,&rem)==n/d && rem==n%d);
        d=(uint32_t)rnd(&seed)|1u;
        assert(fm1_clock_div64(n,d,&rem)==n/d && rem==n%d);
    }
    assert(fm1_clock_div64(UINT64_MAX,1u,&rem)==UINT64_MAX && !rem);
    assert(fm1_clock_div64(UINT64_MAX,UINT64_MAX,&rem)==1u && !rem);
    puts("MIDI clock integer division checks passed");
    return 0;
}
