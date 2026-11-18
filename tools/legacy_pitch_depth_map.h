/* 1.0a pitch LFO/patch depths were linear. Current pitch depths are
 * quadratic; choose the closest available current peak depth. */
#ifndef JAMESOS_LEGACY_PITCH_DEPTH_MAP_H
#define JAMESOS_LEGACY_PITCH_DEPTH_MAP_H
#include <stdint.h>
static inline int32_t jamesos_v16_pitch_depth(int32_t old)
{
    int32_t sign = old < 0 ? -1 : 1;
    uint32_t magnitude = (uint32_t)(old < 0 ? -old : old);
    uint32_t best=0, best_error=UINT32_MAX;
    if (magnitude > 127u) magnitude=127u;
    uint32_t target=magnitude*256u;
    for (uint32_t i=0; i<128u; ++i) {
        uint32_t actual=i*i*256u/127u;
        uint32_t error=actual>target ? actual-target : target-actual;
        if (error<best_error) { best=i; best_error=error; }
    }
    return sign*(int32_t)best;
}
#endif
