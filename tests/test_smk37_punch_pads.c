/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "smk37_punch_pads.h"

int main(void)
{
    smk_punch_pads_t s = {0};
    assert(smk_punch_pads_update(&s, 1u, 1u, 1) == 1); /* loop */
    assert(smk_punch_pads_update(&s, 0x10u, 0x11u, 1) == 1); /* same effect */
    assert(smk_punch_pads_update(&s, 2u, 0x13u, 1) == 1); /* other effect ignored */
    assert(smk_punch_pads_update(&s, 0u, 0x12u, 1) == 1);
    assert(smk_punch_pads_update(&s, 0u, 2u, 1) == 0); /* ignored pad cannot start later */
    assert(smk_punch_pads_update(&s, 4u, 4u, 1) == 3);
    assert(smk_punch_pads_update(&s, 0u, 4u, 0) == 0); /* leaving performance */
    assert(smk_punch_pads_update(&s, 0u, 4u, 1) == 0); /* held across return */
    assert(smk_punch_pads_update(&s, 8u, 8u, 1) == 4);
    assert(smk_punch_pads_update(&s, 0u, 0u, 1) == 0);
}
