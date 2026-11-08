/* Deterministic real-app PCM for a saved session: play, stop and let every
 * tail and quiet timer run out, then play again. Used to compare builds
 * sample for sample. No device access or SAVE. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void render(FILE *out, uint32_t blocks)
{
    for (uint32_t block = 0u; block < blocks; ++block) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        assert(fwrite(audio, sizeof audio, 1u, out) == 1u);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    br_app_init();
    br_session_t session;
    FILE *input = fopen(argv[1], "rb");
    assert(input && fread(&session, 1u, sizeof session, input) == sizeof session);
    assert(fgetc(input) == EOF); fclose(input);
    assert(br_restore(&session));
    FILE *output = fopen(argv[2], "wb"); assert(output);
    br_midi(0xFAu, 0u, 0u); render(output, 1200u);   /* ~7 s of playback */
    br_midi(0xFCu, 0u, 0u); render(output, 2400u);   /* ~14 s: tails and quiet timers expire */
    br_midi(0xFAu, 0u, 0u); render(output, 1200u);   /* cold restart */
    fclose(output);
    return 0;
}
