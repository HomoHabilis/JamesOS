/* SPDX-License-Identifier: GPL-3.0-only
 * Deterministic real-app PCM trace for comparing an unchanged saved session
 * across target-availability changes. No device access or SAVE. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

/* Test the audible output, not merely the target-support predicate. Velocity
 * supplies a stable modulation signal once the real queued note has started.
 * RELEASE uses a live note-off; sequenced kit lane gates do not release the
 * shared amp envelope. */
#define ENV_TEST_BLOCKS 40u
static void render_sample_envelope(uint32_t preset, uint32_t target, uint32_t enabled,
                                   int32_t pcm[ENV_TEST_BLOCKS][BR_BLOCK * 2u])
{
    const uint32_t part = BR_DRUM_PART, note = br_drum_lane_note(0u);
    br_app_init(); fm1_ms = 2000u;
    br_load_preset(part, preset);
    br_part[part].revision++;
    br_part[part].sound.attack = 0u;
    br_part[part].sound.decay = 73u;   /* about 220 ms, as 35 was under the old time law */
    br_part[part].sound.sustain = 0u;
    br_part[part].sound.release = 0u;
    br_part[part].sound.gate = BR_GATE_OPEN;
    br_part[part].mod.route[0].source = BR_SOURCE_VELOCITY;
    br_part[part].mod.route[0].target = target;
    br_part[part].mod.route[0].amount = 110;
    br_part[part].mod.route[0].enabled = enabled;
    RING_PUBLISH(); br_part[part].revision++;
    br_engine_note_on(part, note, 127u);
    for (uint32_t b = 0; b < ENV_TEST_BLOCKS; ++b) {
        if (target == BR_TARGET_RELEASE && b == 4u)
            br_engine_note_off(part, note);
        memset(pcm[b], 0, sizeof pcm[b]);
        assert(app_audio(pcm[b], BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    }
}
static void test_sample_envelope_audio(void)
{
    static int32_t plain[ENV_TEST_BLOCKS][BR_BLOCK * 2u];
    static int32_t modulated[ENV_TEST_BLOCKS][BR_BLOCK * 2u];
    static const uint32_t kits[] = {BR_KIT_BRAIDS, BR_KIT_PLAITS};
    static const uint32_t targets[] = {BR_TARGET_ATTACK, BR_TARGET_SUSTAIN, BR_TARGET_RELEASE};
    for (uint32_t k = 0u; k < 2u; ++k) for (uint32_t t = 0u; t < 3u; ++t) {
        uint32_t differing = 0u, audible = 0u;
        uint32_t first = targets[t] == BR_TARGET_RELEASE ? 4u : 0u;
        render_sample_envelope(kits[k], targets[t], 0u, plain);
        render_sample_envelope(kits[k], targets[t], 0u, modulated);
        assert(!memcmp(plain, modulated, sizeof plain));
        render_sample_envelope(kits[k], targets[t], 1u, modulated);
        /* RELEASE must have no effect before the live note-off. */
        if (targets[t] == BR_TARGET_RELEASE)
            assert(!memcmp(plain, modulated, 4u * sizeof plain[0]));
        for (uint32_t b = first; b < ENV_TEST_BLOCKS; ++b)
            for (uint32_t i = 0u; i < BR_BLOCK * 2u; ++i) {
                audible += plain[b][i] != 0 || modulated[b][i] != 0;
                differing += plain[b][i] != modulated[b][i];
            }
        assert(audible && differing);
    }
    puts("PASS: synthesized kit amp attack, sustain and release modulation affects rendered audio");
}

static void render_filter_route(uint32_t preset, int32_t amount,
                                int32_t pcm[ENV_TEST_BLOCKS][BR_BLOCK * 2u])
{
    uint32_t part=br_is_kit(preset)?BR_DRUM_PART:0u;
    uint32_t note=br_is_kit(preset)?br_drum_lane_note(0u):60u;
    br_app_init(); fm1_ms=2000u; br_load_preset(part,preset);
    br_part[part].revision++;
    br_part[part].sound.filter=64u; br_part[part].sound.filter_env_depth=0;
    br_part[part].sound.attack=0; br_part[part].sound.sustain=100;
    br_part[part].mod.route[0]=(br_route_config_t){BR_SOURCE_VELOCITY,BR_TARGET_FILTER,amount,1u};
    RING_PUBLISH(); br_part[part].revision++;
    br_engine_note_on(part,note,127u);
    for (uint32_t b=0;b<ENV_TEST_BLOCKS;++b) {
        memset(pcm[b],0,sizeof pcm[b]);
        assert(app_audio(pcm[b],BR_BLOCK)==FM1_AUDIO_RENDER_OK);
    }
}
static void test_filter_route_audio(void)
{
    static int32_t open[ENV_TEST_BLOCKS][BR_BLOCK*2u], low[ENV_TEST_BLOCKS][BR_BLOCK*2u], high[ENV_TEST_BLOCKS][BR_BLOCK*2u];
    static const uint32_t presets[]={BR_FIRST_PLAITS,BR_KIT_BRAIDS,BR_KIT_PLAITS};
    for (uint32_t p=0;p<3;++p) {
        uint32_t audible=0;
        render_filter_route(presets[p],0,open);
        render_filter_route(presets[p],-55,low);
        render_filter_route(presets[p],55,high);
        for(uint32_t b=0;b<ENV_TEST_BLOCKS;++b) for(uint32_t i=0;i<BR_BLOCK*2u;++i) audible+=open[b][i]!=0;
        assert(audible && memcmp(open,low,sizeof open) && memcmp(open,high,sizeof open) && memcmp(low,high,sizeof low));
    }
    puts("PASS: patch modulation sweeps open filter into audible LP and HP on synth and kits");
}

static void test_note_random_kit_events(void)
{
    static int32_t pcm[BR_BLOCK*2u];
    const uint32_t kits[]={BR_KIT_BRAIDS,BR_KIT_PLAITS};
    for(uint32_t k=0;k<2;++k) {
        br_app_init(); br_load_preset(BR_DRUM_PART,kits[k]);
        uint32_t old=br_mod_state[BR_DRUM_PART].note_random;
        br_engine_note_on(BR_DRUM_PART,br_drum_lane_note(0),100);
        memset(pcm,0,sizeof pcm); assert(app_audio(pcm,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
        uint32_t first=br_mod_state[BR_DRUM_PART].note_random; assert(first!=old);
        br_engine_note_on(BR_DRUM_PART,br_drum_lane_note(1),100);
        memset(pcm,0,sizeof pcm); assert(app_audio(pcm,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
        assert(br_mod_state[BR_DRUM_PART].note_random!=first);
        first=br_mod_state[BR_DRUM_PART].note_random;
        memset(pcm,0,sizeof pcm); assert(app_audio(pcm,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
        assert(br_mod_state[BR_DRUM_PART].note_random==first);
    }
    puts("PASS: drum-lane note-ons update one held per-track random value");
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--sample-envelope")) {
        test_sample_envelope_audio();
        test_filter_route_audio();
        test_note_random_kit_events();
        return 0;
    }
    if (argc != 3) return 2;
    br_app_init();
    br_session_t session;
    FILE *input = fopen(argv[1], "rb");
    assert(input && fread(&session, 1u, sizeof session, input) == sizeof session);
    assert(fgetc(input) == EOF); fclose(input);
    assert(br_restore(&session));
    FILE *output = fopen(argv[2], "wb"); assert(output);
    br_midi(0xFAu, 0u, 0u);
    uint32_t nonzero = 0u;
    for (uint32_t block = 0u; block < 2048u; ++block) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        for (uint32_t i = 0u; i < BR_BLOCK * 2u; ++i) nonzero += audio[i] != 0;
        assert(fwrite(audio, sizeof audio, 1u, output) == 1u);
    }
    fclose(output); assert(nonzero);
    printf("frames=%u nonzero_samples=%u drum_mod_inactive=%u\n",
        2048u * BR_BLOCK, nonzero, br_cached_mod_inactive[BR_DRUM_PART]);
    return 0;
}
