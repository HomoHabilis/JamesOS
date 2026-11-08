/* MIDI transport commands through the actual audio render loop. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static void render(uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    }
}
static void setup(void)
{
    br_app_init(); fm1_ms = 1000u;
    br_load_preset(0u, BR_FIRST_PLAITS);
    br_part[0].length = 4u; br_part[0].division = 1u; br_part[0].swing = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        br_part[0].step[i] = BR_NOTE_STEP(60u + i, 100u);
        br_seq_synth_velocity[0][i] = 255u;
        br_seq_synth_length[0][i] = 16u;
        br_seq_synth_condition[0][i] = 0u;
        br_seq_synth_micro[0][i] = 0;
    }
}
int main(void)
{
    uint32_t frames, gate, beat, metro, step;
    setup(); br_midi(0xFAu, 0u, 0u); render(30u);
    assert(br_playing && br_step_pos[0] == 1u && br_seq_gate_frames[0]);
    frames = br_seq_frames[0]; gate = br_seq_gate_frames[0];
    beat = br_global_beat; metro = br_metro_frames; step = br_step_pos[0];
    br_midi(0xFCu, 0u, 0u); render(1u);
    assert(!br_playing && br_midi_paused);
    assert(br_seq_frames[0] == frames && br_seq_gate_frames[0] == gate);
    assert(br_step_pos[0] == step && br_global_beat == beat && br_metro_frames == metro);
    render(20u);
    assert(br_seq_frames[0] == frames && br_seq_gate_frames[0] == gate);
    assert(br_step_pos[0] == step && br_global_beat == beat && br_metro_frames == metro);
    br_midi(0xFBu, 0u, 0u); render(1u);
    assert(br_playing && !br_midi_paused && br_step_pos[0] == step);
    assert(br_seq_frames[0] == frames - BR_BLOCK && br_seq_gate_frames[0] == gate - BR_BLOCK);
    /* Start must reset even while already playing. */
    br_midi(0xFAu, 0u, 0u); render(1u);
    assert(br_step_pos[0] == 0u && br_seq_frames[0] == br_step_base_duration(0u) - BR_BLOCK);
    /* Ordinary local PLAY still restarts from the beginning after a stop. */
    render(30u); assert(br_step_pos[0] == 1u);
    br_playing = 0u; render(1u); br_playing = 1u; render(1u);
    assert(br_step_pos[0] == 0u);
    /* A pending positive microtiming event survives a long pause. */
    setup(); br_seq_synth_micro[0][0] = 3;
    br_midi(0xFAu, 0u, 0u); render(1u);
    uint32_t slot = 0u;
    while (slot < BR_SEQ_EVENT_SLOTS && !br_micro_event[0][slot].active) ++slot;
    assert(slot < BR_SEQ_EVENT_SLOTS);
    uint32_t pending = br_micro_event[0][slot].remaining;
    br_midi(0xFCu, 0u, 0u); render(8u);
    assert(br_micro_event[0][slot].active && br_micro_event[0][slot].remaining == pending);
    br_midi(0xFBu, 0u, 0u); render(1u);
    assert(br_micro_event[0][slot].active && br_micro_event[0][slot].remaining == pending - BR_BLOCK);
    /* A real drum hit's queued retrigger is frozen while stopped. */
    setup(); br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    br_part[BR_DRUM_PART].length = 4u; br_part[BR_DRUM_PART].division = 1u;
    br_part[BR_DRUM_PART].step[0] = 1u;
    br_seq_kit_velocity[0][0] = 100u; br_seq_kit_length[0][0] = 16u;
    br_seq_kit_condition[0][0] = 0u; br_seq_kit_micro[0][0] = 0;
    br_seq_kit_retrig[0][0] = 1u;
    br_midi(0xFAu, 0u, 0u); render(1u);
    assert(br_retrig[0].count && br_retrig[0].remaining > BR_BLOCK);
    pending = br_retrig[0].remaining;
    br_midi(0xFCu, 0u, 0u); render(5u);
    assert(br_retrig[0].count && br_retrig[0].remaining == pending);
    br_midi(0xFBu, 0u, 0u); render(1u);
    assert(br_retrig[0].count && br_retrig[0].remaining == pending - BR_BLOCK);
    /* Multiple transport messages in one block retain their order. */
    setup(); br_midi(0xFAu, 0u, 0u); render(30u);
    assert(br_step_pos[0] == 1u);
    br_midi(0xFAu, 0u, 0u); br_midi(0xFCu, 0u, 0u); render(1u);
    assert(!br_playing && br_midi_paused && br_step_pos[0] == 0u && !br_seq_frames[0]);
    br_midi(0xFBu, 0u, 0u); render(1u);
    assert(br_playing && br_step_pos[0] == 0u);
    /* Queue saturation must fail closed, including a dropped Stop. */
    setup(); br_midi(0xFAu, 0u, 0u); render(1u);
    for (uint32_t i = 0u; i < BR_EVENT_CAP; ++i) br_midi(0xFBu, 0u, 0u);
    br_midi(0xFCu, 0u, 0u); render(1u);
    assert(!br_playing && br_midi_paused && !br_midi_transport_overflow);
    puts("MIDI Start/Stop/Continue audio checks passed");
    return 0;
}
