#include "../firmware/src/instrument_midi_routes.h"
#include <assert.h>
int main(void)
{
    br_midi_routes_t s = {0}; volatile uint8_t ch[7] = {1,2,3,4,5,6,7};
    for (unsigned i=0; i<7; ++i) assert(br_midi_route_targets(ch,i+1) == (1u<<i));
    assert(!br_midi_route_targets(ch,0) && !br_midi_route_targets(ch,17));
    assert(br_midi_route_on(&s,ch,4,50) == 8u);
    assert(br_midi_route_set(&s,ch,3,0)==8u && !br_midi_route_targets(ch,4));
    assert(!br_midi_route_off(&s,4,50)); /* switching OFF retires the held note */
    assert(br_midi_route_set(&s,ch,3,4)==8u);
    assert(br_midi_route_on(&s,ch,4,50) == 8u);
    assert(br_midi_route_on(&s,ch,4,51) == 8u);
    assert(!br_midi_route_off(&s,4,50)); /* older pitch must not release latest lane */
    assert(br_midi_route_off(&s,4,51) == 8u);
    ch[0]=4; ch[4]=4;
    assert(br_midi_route_on(&s,ch,4,30) == (1u|8u|16u));
    assert(br_midi_route_set(&s,ch,0,16)==1u);
    assert(br_midi_route_off(&s,4,30) == (8u|16u));
    assert(br_midi_route_on(&s,ch,4,31)==(8u|16u));
    assert(br_midi_route_set(&s,ch,3,9)==8u); /* kit panic purges both lanes */
    assert(!br_midi_route_off(&s,4,31));
    assert(br_midi_route_on(&s,ch,9,55)==8u);
    assert(br_midi_route_on(&s,ch,9,55)==8u);
    assert(!br_midi_route_off(&s,9,55));
    assert(br_midi_route_off(&s,9,55)==8u);
    assert(!br_midi_route_set(&s,ch,3,17));
    assert(br_midi_route_on(&s,ch,16,42)==1u);
    br_midi_route_invalidate(&s, 1u);
    assert(br_midi_route_on(&s,ch,16,42)==1u);
    assert(!br_midi_route_off(&s,16,42)); /* pre-load off cannot end new hit */
    assert(br_midi_route_off(&s,16,42)==1u);
    memset(&s, 0, sizeof s);
    for (unsigned i=0;i<BR_MIDI_ROUTE_NOTES;++i) assert(br_midi_route_on(&s,ch,16,i)==1u);
    assert(!br_midi_route_on(&s,ch,16,100)); /* fail closed if full */
    for (unsigned i=0;i<BR_MIDI_ROUTE_NOTES;++i) assert(br_midi_route_off(&s,16,i)==1u);
    return 0;
}
