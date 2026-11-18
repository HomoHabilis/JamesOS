/* SPDX-License-Identifier: GPL-3.0-only */
#define BR_MODEL_MOD_EXPERIMENT 1
#define FM1_PLAITS_PREWARM_MODEL 0
#include <assert.h>
#include "instrument_poly_source.h"
int main(void)
{
    br_poly_source_t s = {0};
    s.voice_bytes = 23576u;
    br_model_sweep_init(&s.sweep, 0u);
    assert(!br_poly_source_prepare_voice(&s, 0u, 18u));
    assert(s.voice_model[0] == 0u && s.sweep.voice_model[0] == 0u);
    return 0;
}
