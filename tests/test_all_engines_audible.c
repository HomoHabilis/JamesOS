/* Offline coverage: one fresh process per synth model, through the app audio path. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    assert(BR_PLAITS_MODEL_COUNT == 24u && argc == 2);
    uint32_t model = (uint32_t)strtoul(argv[1], 0, 10);
    assert(model < BR_PLAITS_MODEL_COUNT);
    int32_t audio[BR_BLOCK * 2u];
    uint32_t nonzero = 0; uint64_t peak = 0;
    assert(host_init_resources());
    assert(host_init_resources());
    br_app_init();
    br_ui.part = 0u;
    br_preset_sound(BR_FIRST_PLAITS + model, &br_part[0].sound);
    br_part[0].sound.level = 127u;
    br_part[0].revision += 2u;
    for (uint32_t i = 0; i < 4u; ++i) {
        memset(audio, 0, sizeof audio);
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    }
    br_engine_note_on(0u, 60u, 100u);
    for (uint32_t block = 0; block < 100u; ++block) {
        memset(audio, 0, sizeof audio);
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        for (uint32_t i = 0; i < BR_BLOCK * 2u; ++i) {
            int64_t sample = audio[i]; uint64_t magnitude = sample < 0 ? -sample : sample;
            nonzero += sample != 0;
            if (magnitude > peak) peak = magnitude;
        }
    }
    printf("model %u: nonzero=%u peak=%llu\n", model, nonzero, (unsigned long long)peak);
    return nonzero && peak ? 0 : 1;
}
