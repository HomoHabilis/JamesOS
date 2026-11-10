/* Sequencer, arp and drum notes plus MIDI clock and transport reach the timed
 * MIDI output through the actual audio render loop. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

#define TICKS_PER_BLOCK fm1_midi_out_frame_ticks(BR_BLOCK, FM1_MIDI_OUT_TICKS_PER_FRAME_Q8(24000000u, FS))
typedef struct { uint8_t status, d1, d2; uint32_t due; } out_t;
static out_t out[4096];
static uint32_t out_n, block_index;

/* One block as the audio interrupt renders it; everything queued is collected with its due tick. */
static void render(uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) {
        int32_t audio[BR_BLOCK * 2u] = {0};
        fm1_midi_out_block_start(&br_midi_out_schedule, block_index++ * TICKS_PER_BLOCK);
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        while (fm1_midi_out_pending(&br_midi_out_schedule)) {
            uint32_t r = br_midi_out_schedule.read, pkt;
            uint32_t due = br_midi_out_schedule.event[r % FM1_MIDI_OUT_SLOTS].due;
            assert(fm1_midi_out_pop(&br_midi_out_schedule, due, &pkt) && out_n < 4096u);
            assert(!out_n || (int32_t)(due - out[out_n - 1u].due) >= 0);   /* in time order */
            out[out_n].status = (uint8_t)(pkt >> 8); out[out_n].d1 = (uint8_t)(pkt >> 16);
            out[out_n].d2 = (uint8_t)(pkt >> 24); out[out_n].due = due; ++out_n;
        }
    }
}
static uint32_t count(uint32_t status)
{
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < out_n; ++i) n += out[i].status == status;
    return n;
}
static void setup(void)
{
    br_app_init(); fm1_ms = 1000u; out_n = 0u;
    br_playing = 0u; br_midi_clock_out = br_midi_clock_enabled = 0u;
    for (uint32_t i = 0u; i < 7u; ++i) br_midi_channel_set(i, (uint8_t)(i + 1u));
    br_load_preset(0u, BR_FIRST_PLAITS);
    br_part[0].length = 4u; br_part[0].division = 1u; br_part[0].swing = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        br_part[0].step[i] = BR_NOTE_STEP(60u + i, 100u);
        br_seq_synth_velocity[0][i] = 255u;
        br_seq_synth_length[0][i] = 8u;
        br_seq_synth_condition[0][i] = 0u;
        br_seq_synth_micro[0][i] = 0;
    }
    render(2u); out_n = 0u;
}
int main(void)
{
    uint32_t i, on, pulses, step_frames, blocks;
    /* Stopped and with the clock output off: silence. */
    setup(); render(40u); assert(!out_n);

    /* A bar of four steps on track 1, channel 1: every note on has its note off, on the same channel. */
    setup(); step_frames = br_step_base_duration(0u);
    blocks = (4u * step_frames) / BR_BLOCK;
    br_playing = 1u; render(blocks);
    on = 0u;
    for (i = 0u; i < out_n; ++i) {
        assert(out[i].status == 0x90u || out[i].status == 0x80u);
        if (out[i].status == 0x90u) {
            assert(out[i].d1 == 60u + (on & 3u) && out[i].d2 == 100u);
            ++on;
        } else assert(on && out[i].d1 == 60u + ((on - 1u) & 3u));
    }
    assert(on >= 4u && count(0x80u) >= on - 1u);
    /* The second step's note is due one step after the first, not at a block edge. */
    {
        uint32_t first = 0u, second = 0u, seen = 0u;
        for (i = 0u; i < out_n && seen < 2u; ++i) if (out[i].status == 0x90u) {
            if (!seen) first = out[i].due; else second = out[i].due;
            ++seen;
        }
        uint32_t expect = fm1_midi_out_frame_ticks(step_frames % BR_BLOCK, FM1_MIDI_OUT_TICKS_PER_FRAME_Q8(24000000u, FS)) +
            (step_frames / BR_BLOCK) * TICKS_PER_BLOCK;
        assert(seen == 2u && second - first + 2u >= expect && second - first <= expect + 2u);
    }
    /* STOP ends the sounding note. */
    out_n = 0u; br_playing = 0u; render(2u);
    assert(count(0x90u) + 1u >= count(0x80u) && count(0x90u) <= count(0x80u));

    /* The track's channel is the output channel; OFF sends nothing. */
    setup(); br_midi_channel_set(0u, 5u); br_playing = 1u; render(blocks);
    assert(count(0x94u) >= 4u && !count(0x90u));
    setup(); br_midi_channel_set(0u, 0u); br_playing = 1u; render(blocks);
    assert(!out_n);

    /* A note sent before its channel changes is ended on the channel it went out on. */
    setup(); br_playing = 1u; render(2u);
    assert(count(0x90u) == 1u && !count(0x80u));
    br_midi_channel_set(0u, 7u); render(blocks);
    assert(count(0x80u) >= 1u && count(0x96u) >= 3u && count(0x86u) + 1u >= count(0x96u));

    /* Drum lanes send on their own channels, each hit with its note off. */
    setup(); br_midi_channel_set(0u, 0u);
    assert(br_is_kit(br_part[BR_DRUM_PART].sound.preset));
    br_part[BR_DRUM_PART].length = 4u; br_part[BR_DRUM_PART].division = 1u;
    for (i = 0u; i < 4u; ++i) br_part[BR_DRUM_PART].step[i] = 0u;
    br_part[BR_DRUM_PART].step[0] = 1u << 1;                    /* lane 2 only */
    br_seq_kit_velocity[0][1] = 90u; br_seq_kit_condition[0][1] = 0u; br_seq_kit_retrig[0][1] = 0u;
    assert(BR_STEP_HIT(br_part[BR_DRUM_PART].step[0]) == 2u);
    br_midi_channel_set(4u, 10u); br_playing = 1u; render(blocks / 2u);
    assert(count(0x99u) == 1u && count(0x89u) == 1u && out_n == 2u);
    assert(out[0].status == 0x99u && out[0].d1 == br_drum_lane_note(1u) && out[0].d2 == 90u);

    /* The arp's notes go out from the renderer; the held key itself sends nothing more. */
    setup(); for (i = 0u; i < 4u; ++i) br_part[0].step[i] = 0u;
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u; br_hold_on = 0u; br_arp_on = 1u;
    br_key_down(0u, 1); render(blocks);
    assert(count(0x90u) >= 2u && count(0x80u) + 1u >= count(0x90u));
    br_key_up(0u); render(blocks);
    assert(count(0x80u) == count(0x90u));
    br_arp_on = 0u;

    /* Clock out: 24 pulses a beat while stopped, START then its pulse, STOP on stop. */
    setup(); br_midi_channel_set(0u, 0u); br_midi_clock_out = 1u;
    render(1u); out_n = 0u;
    blocks = (4u * br_tempo_quarter_frames()) / BR_BLOCK;
    render(blocks); pulses = count(0xF8u);
    assert(out_n == pulses && pulses >= 95u && pulses <= 96u);
    for (i = 1u; i < out_n; ++i) {                              /* evenly spaced, finer than a block */
        uint32_t gap = out[i].due - out[i - 1u].due;
        uint32_t expect = (uint32_t)((uint64_t)br_tempo_quarter_frames() * 24000000u / FS / 24u);
        assert(gap + 600u >= expect && gap <= expect + 600u);  /* within one frame (544 ticks) */
    }
    out_n = 0u; br_playing = 1u; render(blocks);
    assert(out[0].status == 0xFAu && out[1].status == 0xF8u && out[0].due == out[1].due);
    assert(count(0xFAu) == 1u && count(0xF8u) >= 96u && count(0xF8u) <= 97u && !count(0xFCu));
    out_n = 0u; br_playing = 0u; render(1u);
    assert(count(0xFCu) == 1u);
    /* Turning the output off while playing tells the other gear to stop; following a clock sends none. */
    br_playing = 1u; render(2u); out_n = 0u; br_midi_clock_out = 0u; render(4u);
    assert(out_n == 1u && out[0].status == 0xFCu);
    br_playing = 0u; render(1u); out_n = 0u;
    br_midi_clock_out = 1u; br_midi_clock_enabled = 1u; render(8u); assert(!out_n);
    br_midi_clock_enabled = 0u;

    /* The setting: OFF, RECEIVE, SEND, mutually exclusive and saved with the project. */
    {
        br_session_t s;
        br_app_init(); fm1_ms = 5000u; br_midi_clock_out = br_midi_clock_enabled = 0u;
        br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 7u;
        { br_control_t c = br_control(3u); assert(!strcmp(c.label, "MIDI CLOCK") && !strcmp(c.text, "OFF") && c.high == 2); }
        br_control_turn(3u, 1); assert(br_midi_clock_enabled == 1u && !br_midi_clock_out);
        br_control_turn(3u, 1); assert(!br_midi_clock_enabled && br_midi_clock_out == 1u);
        { br_control_t c = br_control(3u); assert(!strcmp(c.text, "SEND")); }
        assert(br_capture(&s) && br_session_v16_valid(&s));
        br_midi_clock_out = 0u; assert(br_restore(&s) && br_midi_clock_out == 1u && !br_midi_clock_enabled);
        br_midi_clock_enabled = 1u; assert(!br_capture(&s));    /* never both */
        br_midi_clock_enabled = 0u; br_midi_clock_out = 0u;
        assert(br_capture(&s) && br_restore(&s) && !br_midi_clock_out);
    }
    puts("MIDI sequencer, arp, drum, clock and transport output passed");
    return 0;
}
