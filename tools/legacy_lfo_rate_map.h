/* Map 1.0a's 30..19969 mHz linear LFO rate to the closest current rate. */
#ifndef JAMESOS_LEGACY_LFO_RATE_MAP_H
#define JAMESOS_LEGACY_LFO_RATE_MAP_H
#include <stdint.h>
static inline uint32_t jamesos_current_lfo_hz_q20(uint32_t rate)
{
    static const uint16_t exp2_16[17] = {0,1451,2966,4548,6200,7925,9727,11608,13573,
                                         15625,17767,20005,22341,24781,27329,29989,32768};
    uint32_t x=(rate << 8) * 41141u >> 11;
    uint32_t i=(x >> 12) & 15u;
    uint32_t m=32768u + exp2_16[i] + ((exp2_16[i+1] - exp2_16[i]) * (x & 4095u) >> 12);
    return 20972u * m >> (15u - (x >> 16));
}
static inline uint32_t jamesos_v16_lfo_rate(uint32_t old)
{
    uint32_t best=0;
    uint64_t target;
    uint64_t best_error=UINT64_MAX;
    if (old > 127u) old=127u;
    target=(uint64_t)(30u + 157u * old) * (1u << 20);
    for (uint32_t rate=0; rate<128u; ++rate) {
        uint64_t actual=(uint64_t)jamesos_current_lfo_hz_q20(rate) * 1000u;
        uint64_t error=actual > target ? actual - target : target - actual;
        if (error < best_error) { best=rate; best_error=error; }
    }
    return best;
}
#endif
