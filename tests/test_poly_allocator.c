/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "instrument_poly_allocator.h"
int main(void)
{
    br_poly_allocator_t a = {0};
    uint8_t stolen;
    uint32_t g[BR_POLY_VOICES], g70, g72, duplicate, g73, g74;
    for (uint8_t i = 0; i < BR_POLY_VOICES; ++i) {
        assert(br_poly_note_on(&a, 60u + i, &stolen, &g[i]) == i);
        assert(stolen == BR_POLY_NO_VOICE);
    }
    assert(br_poly_note_on(&a, 70u, &stolen, &g70) == 0u && stolen == 60u);
    assert(br_poly_note_off(&a, 60u, g[0]) == BR_POLY_NO_VOICE);
    assert(br_poly_note_off(&a, 63u, g[3]) == 3u);
    assert(br_poly_note_on(&a, 72u, &stolen, &g72) == 3u && stolen == BR_POLY_NO_VOICE);
    assert(br_poly_note_on(&a, 72u, &stolen, &duplicate) == 3u && duplicate == g72);
    assert(br_poly_note_off(&a, 72u, g72) == BR_POLY_NO_VOICE);
    assert(br_poly_note_off(&a, 72u, duplicate) == 3u);
    assert(br_poly_note_on(&a, 60u, &stolen, &g73) == 3u);
    assert(br_poly_note_off(&a, 60u, g[0]) == BR_POLY_NO_VOICE);
    assert(br_poly_note_off(&a, 60u, g73) == 3u);
    a.clock = UINT32_MAX - 1u;
    a.voice[1].age = UINT32_MAX - 3u;
    for (unsigned i = 0; i < BR_POLY_VOICES; ++i)
        if (i != 1u) a.voice[i].age = UINT32_MAX - 2u;
    assert(br_poly_note_on(&a, 73u, &stolen, &g73) == 3u);
    assert(br_poly_note_on(&a, 74u, &stolen, &g74) == 1u && stolen == 61u);
}
