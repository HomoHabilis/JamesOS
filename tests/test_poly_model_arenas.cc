/* SPDX-License-Identifier: GPL-3.0-only */
/* Compare model-specific arenas with the full-sized Plaits arena. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "plaits/dsp/voice.h"
#include "stmlib/utils/random.h"

struct TestVoice {
    alignas(8) uint8_t arena[16384 + 16];
    stmlib::BufferAllocator allocator;
    plaits::Voice voice;
    uint32_t random;
    size_t capacity;
    TestVoice(size_t bytes, uint32_t seed) : allocator(arena, bytes), random(seed), capacity(bytes) {
        std::memset(arena, 0, sizeof arena);
        std::memset(arena + bytes, 0xa5, 16);
        voice.Init(&allocator);
    }
    void prepare(int model) {
        stmlib::Random::Seed(random);
        voice.PrepareEngine(model);
        random = stmlib::Random::state();
        check();
    }
    void render(plaits::Patch &patch, plaits::Modulations &mod, plaits::Voice::Frame *frames) {
        stmlib::Random::Seed(random);
        voice.Render(patch, mod, frames, plaits::kBlockSize);
        random = stmlib::Random::state();
        check();
    }
    void check() const {
        for (unsigned i = 0; i < 16; ++i) assert(arena[capacity + i] == 0xa5);
    }
};

int main() {
    for (int model = 0; model < 24; ++model) {
        const size_t capacity = model == 15 || model == 18 || model == 19 ? 16384 : 8192;
        TestVoice full(16384, 0x12345678), compact(capacity, 0x12345678);
        full.prepare(model);
        compact.prepare(model);
        plaits::Patch patch = {};
        patch.engine = model; patch.note = 60.0f;
        patch.harmonics = 0.4f; patch.timbre = 0.4f; patch.morph = 0.5f; patch.decay = 0.5f;
        plaits::Modulations mod = {};
        mod.trigger_patched = true; mod.level_patched = true; mod.level = 0.8f;
        for (int block = 0; block < 24; ++block) {
            plaits::Voice::Frame a[plaits::kBlockSize], b[plaits::kBlockSize];
            mod.trigger = block < 12 ? 1.0f : 0.0f;
            full.render(patch, mod, a);
            compact.render(patch, mod, b);
            assert(std::memcmp(a, b, sizeof a) == 0);
        }
        std::printf("model %d: %zu bytes of %zu\n", model, capacity - compact.allocator.free(), capacity);
    }
}
