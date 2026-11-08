/* A resonant track filter must return to zero after its input goes silent. Rounding in the integrator used
 * to freeze its two states at a constant offset (hundreds of units at low cutoffs), so the track never went
 * quiet and the shared reverb ran for ever. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

int main(void)
{
    uint32_t checked = 0u;
    for (uint32_t control = 0u; control <= 127u; control += 3u) {
        if (control == 63u || control == 64u) continue;
        for (uint32_t res = 1u; res <= 127u; res += 9u) {
            br_filter_t f; uint32_t seed = 12345u + control * 7u + res; int32_t out = 0;
            br_filter_init(&f);
            for (int i = 0; i < 20000; ++i) {
                seed = seed * 1664525u + 1013904223u;
                out = br_filter_process_controls(&f, (int32_t)(seed >> 16) - 32768, control, res, 0u, 60u);
            }
            for (int i = 0; i < 44100 * 8; ++i) out = br_filter_process_controls(&f, 0, control, res, 0u, 60u);
            assert(f.low == 0 && f.band == 0 && out == 0);
            ++checked;
        }
    }
    /* Through the real app: a note on a track with a low-pass and resonance goes fully quiet after its release. */
    {
        int32_t a[BR_BLOCK * 2u];
        br_app_init(); fm1_ms = 1000u;
        br_part[1].revision++;
        br_load_preset(1u, BR_FIRST_PLAITS + 12u);
        br_part[1].sound.filter = 47u; br_part[1].sound.filter_resonance = 10u;
        br_part[1].sound.release = 10u; br_part[1].sound.reverb = 25u; br_part[1].sound.space = 110u;
        RING_PUBLISH(); br_part[1].revision++;
        br_engine_note_on(1u, 60u, 100u);
        for (int b = 0; b < 172; ++b) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
        br_engine_note_off(1u, 60u);
        for (int b = 0; b < 172 * 25; ++b) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
        assert(br_engine_quiet() && br_fx_quiet());
    }
    printf("filter settle: %u cutoff/resonance pairs return to zero, and a resonant track goes quiet after its note\n", checked);
    return 0;
}
