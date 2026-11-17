/* SPDX-License-Identifier: GPL-3.0-only
 * Full-size Plaits live model-transition stress. Host timings are comparative
 * only: they do not establish device audio deadline compliance. */
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "plaits_c_api.h"

extern "C" void plaits_test_metadata(const void*, uint32_t[9]);
struct Voice { alignas(8) uint8_t data[PLAITS_STATE_BYTES]; };
static void require(bool ok, const char* what, unsigned a = 0, unsigned b = 0) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s (%u -> %u)\n", what, a, b); std::exit(1); }
}
static void init(Voice& v, unsigned model, unsigned seed) {
  plaits_state_init(v.data, seed);
  plaits_prepare_model(v.data, model);
  plaits_note_on(v.data, model, 60 * 128, 12000, 16000, 20000, 99);
}
static void render(Voice& v, unsigned model, unsigned n, int16_t* out) {
  plaits_render(v.data, model, 60 * 128, 12000, 16000, 20000, 7000, out, n);
}
static void gate(Voice& v, unsigned model) {
  uint32_t m[9]; plaits_test_metadata(v.data, m);
  require(m[1] == model && m[3] == 1 && m[2] == 99, "held controls", model);
}
static void all_pairs() {
  Voice v; int16_t out[128];
  for (unsigned from = 0; from < 24; ++from) for (unsigned to = 0; to < 24; ++to) {
    init(v, from, 1234); render(v, from, 128, out);
    require(plaits_active_engine(v.data) == (int)from, "initial model", from, to);
    render(v, to, 128, out); gate(v, to);
    require(plaits_active_engine(v.data) == (int)to, "transition converges", from, to);
    plaits_note_off(v.data, 128); render(v, to, 128, out);
    uint32_t m[9]; plaits_test_metadata(v.data, m);
    require(m[3] == 0, "release after transition", from, to);
  }
}
static void fifo_and_rapid() {
  Voice v; int16_t out[128]; uint32_t m[9];
  init(v, 8, 4321); render(v, 8, 1, out);
  plaits_test_metadata(v.data, m);
  require(m[4] > 4, "FIFO setup");
  render(v, 12, 1, out); gate(v, 12);
  require(plaits_active_engine(v.data) == 8, "buffered samples retain old engine");
  /* Supersede requests before the next native append: the last request wins. */
  render(v, 13, 1, out); render(v, 14, 1, out); render(v, 15, 128, out);
  require(plaits_active_engine(v.data) == 15, "latest request reaches engine"); gate(v, 15);
  for (unsigned i = 0; i < 3000; ++i) {
    unsigned model = (i * 17u + i / 24u) % 24u;
    render(v, model, 1 + i % 64, out); gate(v, model);
  }
  render(v, 20, 128, out);
  require(plaits_active_engine(v.data) == 20, "rapid switches converge"); gate(v, 20);
}
static void multi_track() {
  Voice interleaved[3], isolated; int16_t out[64];
  std::vector<int16_t> expected[3];
  /* Render each reference track independently, then interleave the same
   * streams. This exposes shared-state or RNG leakage between tracks. */
  for (unsigned p = 0; p < 3; ++p) {
    init(isolated, p * 7, 100 + p);
    init(interleaved[p], p * 7, 100 + p);
    for (unsigned i = 0; i < 300; ++i) {
      render(isolated, (i * 7 + p * 5) % 24, 64, out);
      expected[p].insert(expected[p].end(), out, out + 64);
    }
  }
  for (unsigned i = 0; i < 300; ++i) for (unsigned p = 0; p < 3; ++p) {
    const unsigned model = (i * 7 + p * 5) % 24;
    render(interleaved[p], model, 64, out);
    require(std::equal(out, out + 64, expected[p].begin() + i * 64), "track independence", i, p);
    gate(interleaved[p], model);
  }
}
static void benchmark() {
  using clock = std::chrono::steady_clock;
  Voice v; int16_t out[128]; std::vector<long long> steady, switched;
  long long max_transition = 0; unsigned max_from = 0, max_to = 0;
  /* Warm each voice before each measured pair. 128 output frames force an
   * append at the default source rate, so transition includes activation. */
  for (unsigned repeat = 0; repeat < 4; ++repeat)
    for (unsigned from = 0; from < 24; ++from) for (unsigned to = 0; to < 24; ++to) if (from != to) {
      init(v, from, 1000 + repeat); render(v, from, 128, out);
      auto a = clock::now(); render(v, to, 128, out); auto b = clock::now();
      render(v, to, 128, out); auto c = clock::now();
      auto t = std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();
      auto s = std::chrono::duration_cast<std::chrono::nanoseconds>(c-b).count();
      steady.push_back(s); switched.push_back(t);
      if (t > max_transition) { max_transition = t; max_from = from; max_to = to; }
    }
  std::sort(steady.begin(), steady.end()); std::sort(switched.begin(), switched.end());
  auto report = [](const char* label, const std::vector<long long>& v) {
    std::printf("%s n=%zu median=%lld ns p99=%lld ns max=%lld ns\n", label, v.size(), v[v.size()/2], v[v.size()*99/100], v.back());
  };
  report("steady", steady); report("transition", switched);
  std::printf("maximum transition %u -> %u (host scheduling noise may dominate)\n", max_from, max_to);
}
/* Characterize sample discontinuities without treating any particular signal
 * threshold as a pass/fail guarantee of click-free sound. */
static void discontinuities() {
  Voice switched, reference; int16_t warm_a[128], warm_b[128], a[128], b[128];
  uint32_t largest = 0, reference_largest = 0; unsigned worst_from = 0, worst_to = 0;
  for (unsigned from = 0; from < 24; ++from) for (unsigned to = 0; to < 24; ++to) if (from != to) {
    init(switched, from, 1234); init(reference, from, 1234);
    for (unsigned i = 0; i < 8; ++i) { render(switched, from, 128, warm_a); render(reference, from, 128, warm_b); }
    render(switched, to, 128, a); render(reference, from, 128, b);
    int previous_a = warm_a[127], previous_b = warm_b[127];
    for (unsigned i = 0; i < 128; ++i) {
      uint32_t da = (uint32_t)std::abs((int)a[i] - previous_a), db = (uint32_t)std::abs((int)b[i] - previous_b);
      if (da > largest) { largest = da; worst_from = from; worst_to = to; }
      if (db > reference_largest) reference_largest = db;
      previous_a = a[i]; previous_b = b[i];
    }
  }
  std::printf("max adjacent-sample jump: switch %u (%u -> %u), steady %u; exploratory, not a click-free test\n",
              largest, worst_from, worst_to, reference_largest);
}
int main() { all_pairs(); fifo_and_rapid(); multi_track(); std::puts("model transition stress: PASS"); benchmark(); discontinuities(); }
