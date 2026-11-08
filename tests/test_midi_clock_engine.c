/* Experimental clock planner hooked into the real physical-frame sequencer. */
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static uint64_t source_frame, source_phase, source_remainder;
static void render(void)
{
    int32_t audio[BR_BLOCK * 2u] = {0};
    br_clock_experiment.phase_q16 = source_phase;
    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    source_frame += BR_BLOCK;
    uint64_t numerator = ((uint64_t)BR_BLOCK << 32) + source_remainder;
    source_phase += numerator / br_clock_experiment.period_q16;
    source_remainder = numerator % br_clock_experiment.period_q16;
}
static void setup(void)
{
    br_app_init(); br_load_preset(0u, BR_FIRST_PLAITS);
    br_part[0].length = 3u; br_part[0].division = 1u; br_part[0].swing = 77u;
    for (uint32_t i = 0u; i < 3u; ++i) {
        br_part[0].step[i] = BR_NOTE_STEP(60u + i, 100u);
        br_seq_synth_length[0][i] = 16u; br_seq_synth_velocity[0][i] = 255u;
        br_seq_synth_condition[0][i] = 0u; br_seq_synth_micro[0][i] = 0;
    }
    memset(&br_clock_experiment, 0, sizeof br_clock_experiment);
    br_clock_experiment.enabled = 1u; br_clock_experiment.period_q16 = (uint64_t)1000u << 16;
    for (uint32_t p = 0u; p < BR_PARTS; ++p) fm1_mcs_start(&br_clock_experiment.track[p], 0u);
    source_frame = source_phase = source_remainder = 0u; br_engine_midi_transport(0xFAu);
}
int main(void)
{
    setup();
    for (uint32_t i = 0u; i < 2000u; ++i) {
        render(); assert(!br_clock_experiment.invalid && br_playing);
        uint64_t target = (br_clock_experiment.track[0].target_phase_q16 * 1000u) >> 16;
        uint64_t actual = source_frame + br_seq_frames[0];
        assert(actual + 2u >= target && actual <= target + 2u);
    }
    /* An in-flight positive micro-event keeps its original physical gate
     * calculation when the estimate changes before the event fires. */
    setup(); br_part[0].swing = 0u; br_seq_synth_micro[0][0] = 3;
    render();
    uint32_t slot = 0u;
    while (slot < BR_SEQ_EVENT_SLOTS && !br_micro_event[0][slot].active) ++slot;
    assert(slot < BR_SEQ_EVENT_SLOTS);
    assert(br_micro_event[0][slot].clock_base_duration == 6000u);
    uint32_t before = br_seq_frames[0];
    br_clock_experiment.period_q16 = (uint64_t)1500u << 16;
    render();
    assert(br_seq_frames[0] == before - BR_BLOCK);
    while (br_seq_note[0] == 255u) render();
    assert(br_seq_gate_frames[0] > 6000u - BR_BLOCK && br_seq_gate_frames[0] <= 6000u);
    /* Continuous phase through changing tempo: the ARP must emit each grid
     * boundary once despite period changes and odd-length sequencer loops. */
    setup();
    if (!br_arp_on) br_arp_toggle();
    br_part[0].arp_rate = 2u; br_part[0].arp_gate = 63u;
    br_arp_add(0u, 60u, 0u);
    uint32_t expected = 0u;
    uint64_t k = 0u;
    for (uint32_t block = 0u; block < 2400u; ++block) {
        if (block && block % 100u == 0u) {
            uint32_t frames = 900u + ((block / 100u) % 6u) * 100u;
            br_clock_experiment.period_q16 = (uint64_t)frames << 16;
            source_remainder = 0u;
        }
        render();
        while ((k * (6u << 16) + ((k & 1u) ? (6u << 16) * 77u / 128u : 0u)) < source_phase) {
            ++expected; ++k;
        }
        assert(!br_clock_experiment.invalid && br_playing);
        assert(br_arp_state[0].step_count == expected);
    }
    /* Changing rates while running must not replay an already crossed
     * boundary or generate multiple onsets in a single physical block. */
    for (uint32_t rate = 0u; rate < BR_ARP_RATES; ++rate) {
        uint32_t old = br_arp_state[0].step_count;
        br_part[0].arp_rate = rate;
        for (uint32_t b = 0u; b < 200u; ++b) {
            uint32_t before = br_arp_state[0].step_count;
            render();
            assert(!br_clock_experiment.invalid && br_playing);
            assert(br_arp_state[0].step_count - before <= 1u);
        }
        assert(br_arp_state[0].step_count > old);
    }
    puts("MIDI clock experimental physical sequencer checks passed");
    return 0;
}
