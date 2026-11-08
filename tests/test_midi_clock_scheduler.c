#include "../firmware/src/midi_clock_scheduler.h"
#include <assert.h>
#include <stdio.h>

static uint64_t phase_at(uint64_t frame, uint64_t period)
{
    return (frame << 32) / period;
}
int main(void)
{
    const uint64_t period = ((uint64_t)1000u << 16) + 21845u;
    for (uint32_t div = 0u; div < 3u; ++div) {
        for (uint32_t swing = 0u; swing <= 77u; swing += swing ? 77u : 77u) {
            fm1_mcs_track_t track;
            uint64_t frame = 0u;
            fm1_mcs_start(&track, 0u);
            for (uint32_t i = 0u; i < 4096u; ++i) {
                uint32_t duration = fm1_mcs_next(&track, phase_at(frame, period), period, div, swing, i & 1u);
                assert(duration > 0u);
                frame += duration;
                int64_t error = (int64_t)track.target_phase_q16 - (int64_t)phase_at(frame, period);
                assert(error > -256 && error < 256); /* < 1/256 pulse */
            }
        }
    }
    /* A phase offset converges by bounded future step adjustments. */
    {
        fm1_mcs_track_t track;
        uint64_t frame = 0u;
        int64_t first, last;
        fm1_mcs_start(&track, 65536u);
        first = (int64_t)track.target_phase_q16;
        for (uint32_t i = 0u; i < 40u; ++i) {
            uint32_t n = fm1_mcs_next(&track, phase_at(frame, period), period, 1u, 0u, 0u);
            assert(n >= 5600u && n <= 6400u);
            frame += n;
        }
        last = (int64_t)track.target_phase_q16 - (int64_t)phase_at(frame, period);
        assert(last > -256 && last < 256 && last < first);
    }
    /* Pattern position, not played-step count, controls swing parity. */
    {
        fm1_mcs_track_t track;
        uint64_t frame = 0u;
        fm1_mcs_start(&track, 0u);
        for (uint32_t i = 0u; i < 12u; ++i) {
            uint32_t step = i % 3u;
            uint32_t n = fm1_mcs_next(&track, phase_at(frame, period), period, 1u, 77u, step);
            assert(step & 1u ? n < 6000u : n > 6000u);
            frame += n;
        }
    }
    /* Bad inputs leave the planner unchanged. */
    {
        fm1_mcs_track_t track;
        fm1_mcs_start(&track, 0u);
        assert(!fm1_mcs_next(&track, 0u, 0u, 0u, 0u, 0u));
        assert(!fm1_mcs_next(&track, 0u, period, 3u, 0u, 0u));
        assert(!fm1_mcs_next(&track, 0u, period, 0u, 78u, 0u));
        assert(track.target_phase_q16 == 0u && track.ordinal == 0u);
    }
    puts("MIDI clock future-boundary model checks passed");
    return 0;
}
