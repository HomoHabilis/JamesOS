/* Check the closest current setting for every old saved rate. */
#include "../tools/legacy_lfo_rate_map.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    for (uint32_t old=0; old<128; ++old) {
        uint64_t target=(uint64_t)(30u+old*157u)*(1u<<20);
        uint32_t mapped=jamesos_v16_lfo_rate(old);
        uint64_t value=(uint64_t)jamesos_current_lfo_hz_q20(mapped)*1000u;
        uint64_t error=value>target?value-target:target-value;
        for (uint32_t other=0; other<128; ++other) {
            uint64_t candidate=(uint64_t)jamesos_current_lfo_hz_q20(other)*1000u;
            uint64_t e=candidate>target?candidate-target:target-candidate;
            assert(error<=e);
        }
    }
    puts("all 128 old LFO rates map to the closest current speed");
    return 0;
}
