/* SPDX-License-Identifier: GPL-3.0-only */
#include "../firmware/src/instrument_glide.h"
#include "../firmware/src/instrument_lpg.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    br_glide_t g = {0}, h = {0};
    br_glide_note(&g, 60u * 128u, 100u, BR_GLIDE_ALWAYS, 0u, 44100u);
    assert(br_glide_advance(&g, 1u) == 60u * 128u);
    br_glide_note(&g, 72u * 128u, 100u, BR_GLIDE_ALWAYS, 0u, 44100u);
    assert(g.duration == 88200u);
    h = g;
    { uint32_t p = br_glide_advance(&g, 44100u); assert(p >= 66u * 128u - 1u && p <= 66u * 128u + 1u); }
    for (unsigned i = 0; i < 441u; ++i) br_glide_advance(&h, 100u);
    assert(br_glide_pitch(&h) == br_glide_pitch(&g));
    assert(br_glide_advance(&g, 50000u) == 72u * 128u);
    br_glide_note(&g, 60u * 128u, 100u, BR_GLIDE_LEGATO, 0u, 44100u);
    assert(br_glide_advance(&g, 1u) == 60u * 128u);
    br_glide_note(&g, 72u * 128u, 100u, BR_GLIDE_LEGATO, 1u, 44100u);
    { uint32_t p = br_glide_advance(&g, 44100u); assert(p >= 66u * 128u - 1u && p <= 66u * 128u + 1u); }
    br_glide_note(&g, 48u * 128u, 0u, BR_GLIDE_ALWAYS, 1u, 44100u);
    assert(br_glide_advance(&g, 1u) == 48u * 128u);
    { int32_t closed=0, open=0; uint32_t eclosed=0, eopen=0;
      for (unsigned i=0; i<512; ++i) {
          int32_t input = i&1u ? 16000 : -16000;
          int32_t c=br_lpg_sample(input,0,&closed), o=br_lpg_sample(input,32767,&open);
          if (i>50u) { eclosed+=(uint32_t)(c<0?-c:c); eopen+=(uint32_t)(o<0?-o:o); }
      }
      assert(eopen > eclosed * 4u);
    }
    puts("glide core: mode, direction, overlap, endpoints and block invariance passed");
    return 0;
}
