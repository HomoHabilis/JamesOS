/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "instrument_model_sweep.h"
int main(void)
{
    br_model_sweep_plan p;
    br_model_sweep_init(&p, 0u);
    assert(br_model_sweep_next(&p, 0u) == BR_SWEEP_NONE);
    assert(!br_model_sweep_request(&p, 24u) && p.target == 0u);
    assert(br_model_sweep_request(&p, 15u));
    for (unsigned i = 0; i < BR_SWEEP_VOICES; ++i) {
        uint8_t v = br_model_sweep_next(&p, i);
        assert(v == i);
        /* Failure or lack of ownership does not advance the plan. */
        assert(br_model_sweep_next(&p, i) == v);
        assert(br_model_sweep_commit(&p, v, 15u, i));
        assert(!br_model_sweep_commit(&p, (uint8_t)((v + 1u) % BR_SWEEP_VOICES), 15u, i));
        assert(br_model_sweep_next(&p, i) == BR_SWEEP_NONE);
    }
    assert(br_model_sweep_next(&p, 6u) == BR_SWEEP_NONE);
    assert(br_model_sweep_request(&p, 1u));
    uint8_t v = br_model_sweep_next(&p, 6u);
    assert(br_model_sweep_request(&p, 19u));
    /* An old in-flight preparation is recorded accurately and retried later. */
    assert(br_model_sweep_commit(&p, v, 1u, 6u));
    for (unsigned i = 0; i < BR_SWEEP_VOICES; ++i) {
        v = br_model_sweep_next(&p, 7u + i); assert(v != BR_SWEEP_NONE);
        assert(br_model_sweep_commit(&p, v, 19u, 7u + i));
    }
    assert(br_model_sweep_next(&p, 13u) == BR_SWEEP_NONE);
    return 0;
}
