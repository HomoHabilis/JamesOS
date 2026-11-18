/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_poly_allocator.h"
#include "../firmware/src/instrument_poly_storage.h"
#include "../firmware/src/instrument_model_sweep.h"
#include "../firmware/src/instrument_poly_limits.h"
int main(void) {
    _Alignas(8) uint8_t memory[512] = {0};
    for (uint8_t cap = 3; cap <= 8; ++cap) {
        br_poly_allocator_t a = {0}; a.limit = cap;
        uint32_t generations[8]; uint8_t stolen;
        for (uint8_t i=0; i<cap; ++i) {
            assert(br_poly_note_on(&a, 40u+i, &stolen, &generations[i])==i);
            assert(stolen==BR_POLY_NO_VOICE);
        }
        uint32_t fresh;
        assert(br_poly_note_on(&a, 80, &stolen, &fresh)==0 && stolen==40);
        assert(br_poly_note_off(&a, 40, generations[0])==BR_POLY_NO_VOICE);
        assert(br_poly_note_off(&a, 80, fresh)==0);
        for (uint8_t i=cap; i<8; ++i) assert(a.voice[i].count==0);
        br_poly_storage_t plan = {0};
        assert(br_poly_storage_plan_count(&plan,memory,sizeof memory,0,0,32,cap));
        for(uint8_t i=0;i<8;++i) assert((plan.voice[i]!=0)==(i<cap));
        br_model_sweep_plan sweep; br_model_sweep_init(&sweep,0);sweep.voice_count=cap;
        assert(br_model_sweep_request(&sweep,1));
        for(uint8_t i=0;i<cap;++i) {uint8_t n=br_model_sweep_next(&sweep,i);assert(n==i);assert(br_model_sweep_commit(&sweep,n,1,i));}
        assert(br_model_sweep_next(&sweep,cap)==BR_SWEEP_NONE);
    }
    for (uint32_t i=0;i<24;++i) assert(br_poly_model_limit(i)>=3 && br_poly_model_limit(i)<=8);
    assert(br_poly_model_limit(20)==3);
    return 0;
}
