/* SPDX-License-Identifier: GPL-3.0-only */
#include <cassert>
#include <cstdint>
#include <cstring>
#include "plaits_c_api.h"

int main() {
    alignas(8) uint8_t full[PLAITS_STATE_BYTES];
    alignas(8) uint8_t compact[PLAITS_STATE_BYTES + 16];
    for (uint32_t model = 0; model < PLAITS_MODEL_COUNT; ++model) {
        uint32_t bytes = plaits_compact_state_bytes(model);
        assert(bytes && bytes <= sizeof full);
        std::memset(compact, 0xa5, sizeof compact);
        assert(!plaits_compact_state_init(compact, bytes - 1u, model, 123));
        assert(plaits_compact_state_init(compact, bytes, model, 123));
        plaits_state_init(full, 123);
        plaits_note_on(full, model, 60 * 128, 15000, 16000, 17000, 100);
        plaits_note_on(compact, model, 60 * 128, 15000, 16000, 17000, 100);
        for (unsigned block = 0; block < 24; ++block) {
            int16_t a[32], b[32];
            if (block == 12) { plaits_note_off(full, 128); plaits_note_off(compact, 128); }
            plaits_render(full, model, 60 * 128, 15000, 16000, 17000, 0, a, 32);
            plaits_render(compact, model, 60 * 128, 15000, 16000, 17000, 0, b, 32);
            assert(std::memcmp(a, b, sizeof a) == 0);
            for (unsigned i = 0; i < 16; ++i) assert(compact[bytes + i] == 0xa5);
        }
        if (model != 15u && model != 18u && model != 19u) {
            int16_t out[32];
            std::memset(out, 0x5a, sizeof out);
            plaits_render(compact, 18, 60 * 128, 0, 0, 0, 0, out, 32);
            for (auto sample : out) assert(sample == 0);
        }
    }
    assert(plaits_compact_state_bytes(PLAITS_MODEL_COUNT) == 0);
    assert(!plaits_compact_state_init(compact + 1, sizeof compact - 1, 0, 1));
}
