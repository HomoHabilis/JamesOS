/* SPDX-License-Identifier: GPL-3.0-only */
/* Host experiment: full-capacity voices can change to every Plaits model
 * without exceeding their arenas or losing their held-note gate. This does
 * not establish that these transitions meet the device's audio deadline. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define BR_MODEL_MOD_EXPERIMENT 1
#include "instrument_poly_source.h"
#include "instrument_model_sweep.h"
static_assert(BR_SWEEP_VOICES == BR_POLY_VOICES, "planner voice count");
static_assert(BR_SWEEP_MODELS == PLAITS_MODEL_COUNT, "planner model count");
extern "C" void plaits_test_metadata(const void *, uint32_t[9]);

int main() {
    alignas(8) static uint8_t primary[4u * PLAITS_STATE_BYTES];
    alignas(8) static uint8_t extra[2u * PLAITS_STATE_BYTES];
    alignas(8) static uint8_t guarded[PLAITS_STATE_BYTES + 32u];
    const uint32_t full = plaits_compact_state_bytes(15u);
    assert(full <= PLAITS_STATE_BYTES && full > plaits_compact_state_bytes(0u));
    br_poly_source_t source;
    assert(!br_poly_source_init_capacity_count(&source, 15u, 0u, primary, sizeof primary, extra, sizeof extra, 6u));
    assert(br_poly_source_init_capacity_count(&source, 0u, 15u, primary, sizeof primary, extra, sizeof extra, 6u));
    uint8_t stolen; uint32_t generation[6];
    for (uint8_t v = 0; v < 6u; ++v)
        assert(br_poly_source_on(&source, 60u + v, (60u + v) * 128, 15000, 16000, 17000,
                                 100, &stolen, &generation[v]) == v);
    for (uint32_t model = 0; model < PLAITS_MODEL_COUNT; ++model)
        for (uint8_t v = 0; v < 6u; ++v) {
            int16_t out[256]; uint32_t metadata[9];
            plaits_render(source.storage.voice[v], model, (60u + v) * 128, 15000, 16000, 17000, 0, out, 256u);
            plaits_test_metadata(source.storage.voice[v], metadata);
            assert(metadata[1] == model && metadata[3] == 1u && plaits_active_engine(source.storage.voice[v]) == (int32_t)model);
        }
    for (uint32_t from = 0; from < PLAITS_MODEL_COUNT; ++from) {
        for (uint32_t to = 0; to < PLAITS_MODEL_COUNT; ++to) {
            std::memset(guarded, 0xa5, sizeof guarded);
            assert(plaits_compact_state_init(guarded, full, 15u, 0x12345u));
            plaits_prepare_model(guarded, from);
            plaits_note_on(guarded, from, 60 * 128, 15000, 16000, 17000, 100u);
            int16_t out[256]; uint32_t metadata[9];
            plaits_render(guarded, from, 60 * 128, 15000, 16000, 17000, 0, out, 256u);
            plaits_render(guarded, to, 60 * 128, 15000, 16000, 17000, 0, out, 256u);
            plaits_test_metadata(guarded, metadata);
            assert(metadata[1] == to && metadata[3] == 1u && plaits_active_engine(guarded) == (int32_t)to);
            for (uint32_t i = full; i < sizeof guarded; ++i) assert(guarded[i] == 0xa5);
        }
    }
    for (uint8_t v = 0; v < 6u; ++v)
        assert(br_poly_source_off(&source, 60u + v, generation[v]) == v);
    /* Scheduler contract: one worker owns one selected voice, then the audio
     * owner commits after join. Retargeting leaves every other voice alone. */
    assert(br_poly_source_init_capacity_count(&source, 0u, 15u, primary, sizeof primary, extra, sizeof extra, 6u));
    for (uint8_t v = 0; v < 6u; ++v)
        assert(br_poly_source_on(&source, 60u + v, (60u + v) * 128, 15000, 16000, 17000,
                                 100u, &stolen, &generation[v]) == v);
    assert(br_poly_source_request_model(&source, 18u));
    for (uint32_t interval = 0; interval < 6; ++interval) {
        uint8_t v = br_poly_source_next_model_voice(&source, interval);
        assert(v == interval);
        assert(br_poly_source_prepare_voice(&source, v, 18u));
        int16_t out[32]; uint32_t metadata[9];
        br_poly_source_render(&source, v, (60u + v) * 128, 15000, 16000, 17000, 0, out, 32);
        plaits_test_metadata(source.storage.voice[v], metadata);
        assert(metadata[1] == 18u && metadata[3] == 1u && source.voice_model[v] == 18u);
        assert(br_poly_source_commit_model(&source, v, interval));
        assert(br_poly_source_next_model_voice(&source, interval) == BR_SWEEP_NONE);
    }
    assert(br_poly_source_next_model_voice(&source, 6u) == BR_SWEEP_NONE);
    assert(br_poly_source_off(&source, 60u, generation[0]) == 0u);
    assert(br_poly_source_on(&source, 70u, 70 * 128, 14000, 15000, 16000,
                             90u, &stolen, &generation[0]) == 0u);
    { uint32_t metadata[9];
      plaits_test_metadata(source.storage.voice[0], metadata);
      assert(metadata[1] == 18u && metadata[3] == 1u); }
    assert(br_poly_source_request_model(&source, 19u));
    uint8_t v = br_poly_source_next_model_voice(&source, 6u);
    assert(br_poly_source_prepare_voice(&source, v, 19u));
    assert(br_poly_source_request_model(&source, 1u));
    assert(br_poly_source_commit_model(&source, v, 6u));
    assert(source.sweep.voice_model[v] == 19u && source.sweep.target == 1u);
    assert(br_poly_source_next_model_voice(&source, 7u) != BR_SWEEP_NONE);
    assert(!br_poly_source_prepare_voice(&source, BR_POLY_VOICES, 1u));
    assert(!br_poly_source_request_model(&source, PLAITS_MODEL_COUNT));
    assert(br_poly_source_init_capacity_count(&source, 0u, 0u, primary, sizeof primary, extra, sizeof extra, 6u));
    assert(!br_poly_source_request_model(&source, 18u));
    assert(!br_poly_source_prepare_voice(&source, 0u, 18u));
    std::printf("full-capacity held-note model transitions: %u pairs passed\n", PLAITS_MODEL_COUNT * PLAITS_MODEL_COUNT);
}
