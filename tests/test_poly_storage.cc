/* SPDX-License-Identifier: GPL-3.0-only */
#include <cassert>
#include <cstdint>
#include <cstring>
#include "instrument_poly_storage.h"
#include "plaits_c_api.h"

int main() {
    alignas(8) uint8_t primary[4u * PLAITS_STATE_BYTES + 16u];
    alignas(8) uint8_t extra[(BR_POLY_VOICES - 4u) * PLAITS_STATE_BYTES + 16u];
    for (uint32_t model = 0; model < PLAITS_MODEL_COUNT; ++model) {
        br_poly_storage_t plan = {};
        uint32_t bytes = plaits_compact_state_bytes(model);
        bool heavy = model == 15u || model == 18u || model == 19u;
        std::memset(primary, 0xa5, sizeof primary);
        std::memset(extra, 0xa5, sizeof extra);
        if (heavy) assert(!br_poly_storage_plan(&plan, primary, sizeof primary - 16, nullptr, 0, bytes));
        assert(br_poly_storage_plan(&plan, primary, sizeof primary - 16,
                                    extra, sizeof extra - 16, bytes));
        for (unsigned i = 0; i < BR_POLY_VOICES; ++i)
            assert(plaits_compact_state_init(plan.voice[i], bytes, model, i + 1u));
        for (unsigned i = 0; i < 16; ++i) {
            assert(primary[sizeof primary - 16 + i] == 0xa5);
            assert(extra[sizeof extra - 16 + i] == 0xa5);
        }
    }
    br_poly_storage_t plan = {};
    assert(!br_poly_storage_plan(&plan, primary, sizeof primary, primary + 8, 32, 8));
    assert(!br_poly_storage_plan(&plan, primary + 1, 128, extra, 128, 8));
}
