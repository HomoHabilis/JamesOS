/* SPDX-License-Identifier: GPL-3.0-only */
#include <cstdint>
#include <cstdio>
#include "plaits_c_api.h"

int main() {
    alignas(8) uint8_t state[PLAITS_STATE_BYTES];
    for (uint32_t model = 0; model < PLAITS_MODEL_COUNT; ++model) {
        plaits_state_init(state, 0x3456789u);
        for (uint32_t block = 0; block < 40; ++block) {
            if (block == 0 || block == 13)
                plaits_note_on(state, model, (60 + block / 13) * 128, 12000, 16000, 20000, 99);
            if (block == 25) plaits_note_off(state, 128);
            int16_t out[32];
            plaits_render(state, model, (60 + (block >= 13)) * 128, 12000, 16000, 20000, 7000, out, 32);
            if (std::fwrite(out, sizeof out, 1, stdout) != 1) return 1;
        }
    }
}
