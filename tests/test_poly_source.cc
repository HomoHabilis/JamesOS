/* SPDX-License-Identifier: GPL-3.0-only */
#include <cassert>
#include <cstdint>
#include <cstring>
#include "instrument_poly_source.h"
extern "C" void plaits_test_metadata(const void *, uint32_t[9]);

int main() {
    alignas(8) uint8_t primary[4u * PLAITS_STATE_BYTES], extra[(BR_POLY_VOICES - 4u) * PLAITS_STATE_BYTES];
    for (uint32_t model = 0; model < PLAITS_MODEL_COUNT; ++model) {
        br_poly_source_t synth;
        assert(br_poly_source_init(&synth, model, primary, sizeof primary, extra, sizeof extra));
        uint32_t generation[6]; uint8_t stolen;
        for (uint8_t i = 0; i < 6; ++i)
            assert(br_poly_source_on(&synth, 60 + i, (60 + i) * 128, 15000, 16000,
                                     17000, 100, &stolen, &generation[i]) == i);
        for (unsigned i = 0; i < 6; ++i) {
            int16_t out[32]; uint32_t metadata[9];
            br_poly_source_render(&synth, i, (60 + i) * 128, 15000, 16000, 17000, 0, out, 32);
            plaits_test_metadata(synth.storage.voice[i], metadata);
            assert(metadata[3] == 1u);
        }
        if (model == 8u) {
            uint32_t energy[6] = {};
            for (unsigned block = 0; block < 16; ++block) {
                for (unsigned i = 0; i < 6; ++i) {
                    int16_t out[32];
                    br_poly_source_render(&synth, i, (60 + i) * 128, 15000, 16000, 17000, 0, out, 32);
                    for (int16_t v : out) energy[i] += v < 0 ? -int32_t(v) : v;
                }
            }
            for (auto e : energy) assert(e != 0u);
        }
        assert(br_poly_source_off(&synth, 63, generation[3]) == 3);
        uint32_t metadata[9];
        plaits_test_metadata(synth.storage.voice[3], metadata);
        assert(metadata[3] == 0u);
        assert(br_poly_source_off(&synth, 60, generation[0] + 1) == BR_POLY_NO_VOICE);
        plaits_test_metadata(synth.storage.voice[0], metadata);
        assert(metadata[3] == 1u);
    }
}
