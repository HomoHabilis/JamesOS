/* The frozen 1482 policy rejected the measured physical-cold sample. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../../vendor360-fix/firmware/hal/fm1_vendor360_policy.h"
int main(void) {
    const fm1_vp_sample_t sample={{0x77,0x60,0x18,0x44,0x0b,0,0,0x1b,0x05,0,0x20}};
    assert(fm1_vp_reset_profile_eligible(1u,1u));
    assert(fm1_vp_clock_profile_eligible(0x10200u,6u,0x0940022bu,0x0750310cu));
    assert(!fm1_vp_entry_profile_eligible(1u,1u,&sample));
    puts("PASS: frozen 1482 rejects captured physical-cold tuple before writes");
    return 0;
}
