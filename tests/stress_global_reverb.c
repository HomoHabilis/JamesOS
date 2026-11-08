/* Randomised start/stop, notes, send and reverb-type changes through the real
 * app, for an AddressSanitizer/UBSan build: sh tests/build_stress.sh
 * tests/stress_global_reverb.c build/stress_gr && ./build/stress_gr */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <stdlib.h>

static void publish(uint32_t p) { RING_PUBLISH(); br_part[p].revision++; }
int main(void)
{
    uint32_t seed = 12345u;
    br_app_init(); fm1_ms = 1000u;
    for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) {
        br_part[p].revision++; br_part[p].sound.reverb = 100u + p * 10u; br_part[p].sound.delay = p ? 60u : 0u;
        br_part[p].fx.reverb_engine = BR_REVERB_SPACE; br_part[p].fx.reverb_tone = 60u; publish(p);
    }
    for (uint32_t blk = 0u; blk < 12000u; ++blk) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        seed = seed * 1664525u + 1013904223u;
        uint32_t r = seed >> 16;
        if (r % 97u == 0u) { br_midi(0xFAu, 0u, 0u); }
        if (r % 211u == 0u) { br_midi(0xFCu, 0u, 0u); }
        if (r % 13u == 0u) br_engine_note_on(r % 3u, 48u + r % 24u, 100u);
        if (r % 17u == 0u) br_engine_note_off(r % 3u, 48u + (r >> 3) % 24u);
        if (r % 331u == 0u) {   /* change the shared reverb type and size mid-stream */
            uint32_t e = (r >> 5) % BR_REVERB_COUNT; if (e == BR_REVERB_SPRING) e = BR_REVERB_HALL;
            for (uint32_t p = 0u; p < BR_PARTS; ++p) { br_part[p].revision++; br_part[p].fx.reverb_engine = e; br_part[p].sound.space = (r >> 9) & 127u; publish(p); }
        }
        if (r % 419u == 0u) { uint32_t p = (r >> 4) % 3u; br_part[p].revision++; br_part[p].sound.reverb = (r >> 8) & 127u; publish(p); }
        if (r % 1009u == 0u) { br_engine_part_off(0u); br_engine_part_off(1u); br_engine_part_off(2u); }
        if (app_audio(audio, BR_BLOCK) != FM1_AUDIO_RENDER_OK) { fprintf(stderr, "render failed at %u\n", blk); return 1; }
    }
    printf("stress ok\n");
    return 0;
}
