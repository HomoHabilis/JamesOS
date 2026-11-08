#include "../firmware/src/midi_clock_arp.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const uint32_t rates[] = {2u,3u,4u,6u,8u};
    const uint64_t quarter = (uint64_t)24u << 16;
    for (uint32_t r = 0u; r < 5u; ++r) {
        fm1_mca_t a; uint32_t gate, hits = 0u, first = 0u, second = 0u;
        assert(fm1_mca_start(&a, 0u, rates[r], 77u));
        for (uint64_t t = 0u; t < quarter * 20u; t += 5000u) {
            uint64_t end = t + 5000u;
            if (end > quarter * 20u) end = quarter * 20u;
            int result = fm1_mca_advance(&a, end, (uint64_t)1000u << 16, &gate);
            assert(result >= 0);
            if (result) { if (!hits) first = gate; if (hits == 1u) second = gate; ++hits; }
        }
        assert(hits == rates[r] * 20u);
        if (rates[r] % 3u) assert(first > second);
        else assert(first == second);
        /* Mid-grid start chooses the next real boundary, not step parity zero. */
        uint64_t mid = quarter / rates[r] + 1u;
        assert(fm1_mca_start(&a, mid, rates[r], 77u));
        assert(a.next_index == (rates[r] % 3u ? 1u : 2u));
        assert(fm1_mca_advance(&a, mid - 1u, (uint64_t)1000u << 16, &gate) == -1);
    }
    {
        fm1_mca_t a; uint32_t gate;
        assert(fm1_mca_start(&a, 0u, 8u, 0u));
        assert(fm1_mca_advance(&a, quarter, (uint64_t)1000u << 16, &gate) == -1);
        assert(a.next_index == 0u); /* no silently dropped events */
    }
    puts("MIDI clock continuous ARP grid checks passed");
    return 0;
}
