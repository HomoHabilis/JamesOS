/* Arpeggiator, HOLD and arp recording through the real app on the host. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static uint32_t seen[64], seen_count;

/* Render blocks through the app and log each new arp step on track 0. */
static void run(uint32_t blocks)
{
    for (uint32_t b = 0u; b < blocks; ++b) {
        uint32_t before = br_arp_state[0].step_count;
        int32_t audio[BR_BLOCK * 2u] = {0};
        assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
        if (br_arp_state[0].step_count != before && seen_count < 64u)
            seen[seen_count++] = br_arp_state[0].step_note;
        br_arp_record_drain();
    }
}

static void setup(void)
{
    br_app_init(); fm1_ms = 1000u;
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u;
    br_load_preset(0u, BR_FIRST_PLAITS);
    br_arp_part_defaults(&br_part[0]);
    if (!br_arp_on) br_arp_toggle();
    seen_count = 0u;
}

static void chord(const uint8_t *notes, uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) br_arp_add(0u, notes[i], 0u);
}

static void expect(const uint8_t *want, uint32_t count)
{
    assert(seen_count >= count);
    for (uint32_t i = 0u; i < count; ++i) {
        if (seen[i] != want[i]) { printf("step %u: got %u want %u\n", i, seen[i], want[i]); assert(0); }
    }
}

int main(void)
{
    const uint8_t c_major[3] = {64u, 60u, 67u};        /* pressed E, C, G */
    const uint32_t step = (FS * 60u / 120u) / 4u;       /* 1/16 at 120 BPM */
    const uint32_t blocks_per_step = (step + BR_BLOCK - 1u) / BR_BLOCK;

    /* A ninth distinct held note drops the oldest and reports the limit. */
    setup();
    for (uint32_t i = 0u; i < BR_ARP_MAX_NOTES; ++i) br_arp_add(0u, 48u + i, 0u);
    br_arp_add(0u, 56u, 0u);
    assert(br_arp_keys[0].count == BR_ARP_MAX_NOTES);
    assert(br_arp_keys[0].notes[0] == 49u && br_arp_keys[0].notes[BR_ARP_MAX_NOTES - 1u] == 56u);
    assert(!strcmp(br_ui.message, "ARP: 8 MAX, OLDEST OUT"));

    /* UP, stopped: the first note sounds at once, then one per 1/16. */
    setup(); chord(c_major, 3u);
    run(1u); assert(seen_count == 1u && seen[0] == 60u);
    run(blocks_per_step * 4u);
    { const uint8_t want[] = {60, 64, 67, 60, 64}; expect(want, 5u); }

    /* DOWN, UP/DOWN, AS PLAYED and two octaves. */
    setup(); br_part[0].arp_mode = BR_ARP_DOWN; chord(c_major, 3u); run(blocks_per_step * 4u);
    { const uint8_t want[] = {67, 64, 60, 67}; expect(want, 4u); }
    setup(); br_part[0].arp_mode = BR_ARP_UP_DOWN; chord(c_major, 3u); run(blocks_per_step * 6u);
    { const uint8_t want[] = {60, 64, 67, 64, 60, 64}; expect(want, 6u); }
    setup(); br_part[0].arp_mode = BR_ARP_AS_PLAYED; chord(c_major, 3u); run(blocks_per_step * 4u);
    { const uint8_t want[] = {64, 60, 67, 64}; expect(want, 4u); }
    setup(); br_part[0].arp_octaves = 2u; chord(c_major, 3u); run(blocks_per_step * 7u);
    { const uint8_t want[] = {60, 64, 67, 72, 76, 79, 60}; expect(want, 7u); }
    setup(); br_part[0].arp_mode = BR_ARP_RANDOM; br_part[0].arp_octaves = 3u;
    chord(c_major, 3u); run(blocks_per_step * 20u);
    for (uint32_t i = 0u; i < seen_count; ++i) {
        uint32_t base = seen[i] % 12u;
        assert(seen[i] >= 60u && seen[i] < 96u && (base == 0u || base == 4u || base == 7u));
    }

    /* Rates: 1/8 gives half as many steps as 1/16 and 1/32 twice as many. */
    for (uint32_t rate = 0u; rate < BR_ARP_RATES; ++rate) {
        static const uint32_t per_beat[BR_ARP_RATES] = {2, 3, 4, 6, 8};
        setup(); br_part[0].arp_rate = rate; chord(c_major, 3u);
        run((FS * 60u / 120u) * 4u / BR_BLOCK);         /* four beats */
        assert(seen_count + 1u >= per_beat[rate] * 4u && seen_count <= per_beat[rate] * 4u + 1u);
    }

    /* GATE: a short gate stops the note well before the next step. */
    setup(); br_part[0].arp_gate = 16u; chord(c_major, 3u); run(1u);
    assert(br_arp_state[0].sounding == 61u);
    run(blocks_per_step / 2u);
    assert(!br_arp_state[0].sounding);

    /* Global swing applies despite a legacy per-track swing-off value;
     * triplet rates stay straight. Compare first free-running intervals. */
    {
        uint32_t straight, swung, triplet_straight, triplet_swung;
        setup(); br_part[0].swing = 0u; br_part[0].arp_swing = 0u;
        chord(c_major, 3u); run(1u); straight = br_arp_state[0].free_left;
        setup(); br_part[0].swing = BR_SWING_MAX; br_part[0].arp_swing = 0u;
        chord(c_major, 3u); run(1u); swung = br_arp_state[0].free_left;
        assert(swung > straight);
        setup(); br_part[0].arp_rate = 1u; br_part[0].swing = 0u;
        chord(c_major, 3u); run(1u); triplet_straight = br_arp_state[0].free_left;
        setup(); br_part[0].arp_rate = 1u; br_part[0].swing = BR_SWING_MAX;
        chord(c_major, 3u); run(1u); triplet_swung = br_arp_state[0].free_left;
        assert(triplet_swung == triplet_straight);
    }

    /* Check the swing/gate policy in both transport states.
     * sub_frames is the interval used to calculate each new note's gate. */
    for (uint32_t playing = 0u; playing <= 1u; ++playing) {
        for (uint32_t gate_index = 0u; gate_index < 2u; ++gate_index) {
            uint32_t onsets[4], bases[4], gates[4], found = 0u;
            uint32_t gate = gate_index ? 127u : 63u;
            setup(); br_part[0].swing = BR_SWING_MAX; br_part[0].arp_gate = gate;
            chord(c_major, 3u);
            if (playing) br_midi(0xFAu, 0u, 0u);
            for (uint32_t block = 0u; found < 4u && block < 200u; ++block) {
                uint32_t before = br_arp_state[0].step_count;
                run(1u);
                if (br_arp_state[0].step_count != before) {
                    onsets[found] = block * BR_BLOCK;
                    bases[found] = br_arp_state[0].sub_frames;
                    gates[found] = br_arp_state[0].gate_left;
                    ++found;
                }
            }
            assert(found == 4u);
            for (uint32_t i = 0u; i < found; ++i) {
                uint32_t expected = gate == 127u ? bases[i] : bases[i] * (gate + 1u) / 128u;
                assert(gates[i] == (expected > BR_BLOCK ? expected - BR_BLOCK : 1u));
            }
            assert(bases[0] > step && bases[1] < step && bases[2] > step);
            printf("swing=%u gate=%u %s: intervals=%u,%u,%u gate-bases=%u,%u,%u initial-gate-left=%u,%u,%u\n",
                   BR_SWING_MAX, gate, playing ? "playing" : "stopped",
                   onsets[1] - onsets[0], onsets[2] - onsets[1], onsets[3] - onsets[2],
                   bases[0], bases[1], bases[2], gates[0], gates[1], gates[2]);
        }
    }

    /* Enter on every grid position, including the bar wrap: the gate must
     * follow grid parity rather than the number of notes played so far. */
    for (uint32_t rate = 0u; rate < BR_ARP_RATES; ++rate) {
        uint32_t d = br_arp_divisor[rate], quarter = FS * 60u / 120u;
        uint32_t nominal = quarter / d, bar = 4u * quarter;
        uint32_t swing = d % 3u ? BR_SWING_MAX : 0u;
        for (uint32_t k = 0u; k < 4u * d; ++k) {
            uint32_t at = (k / d) * quarter + (k % d) * quarter / d +
                ((k & 1u) ? nominal * swing / 128u : 0u);
            uint32_t next = k + 1u;
            uint32_t next_at = (next / d) * quarter + (next % d) * quarter / d +
                ((next & 1u) ? nominal * swing / 128u : 0u);
            uint32_t end = (at + 1u) % bar;
            setup(); br_part[0].arp_rate = rate; br_part[0].swing = BR_SWING_MAX;
            chord(c_major, 3u); br_midi(0xFAu, 0u, 0u);
            br_take_events(); /* MIDI transport is applied by the audio consumer. */
            br_global_beat = end / quarter; br_metro_frames = end % quarter;
            br_arp_process(0u, BR_BLOCK);
            assert(br_arp_state[0].step_count == 1u);
            assert(br_arp_state[0].sub_frames == (swing ? next_at - at : nominal));
        }
    }

    /* Legacy repeat and latch bytes survive, but no longer alter playback. */
    setup(); br_part[0].arp_repeat = 4u; chord(c_major, 3u); run(1u);
    assert(br_arp_state[0].repeats_left == 0u);
    setup(); br_part[0].arp_latch = 1u;
    br_key_down(0u, 1); br_key_up(0u);
    assert(br_arp_keys[0].count == 0u);
    /* Global HOLD still latches the arpeggiator. */
    br_hold_toggle(); br_key_down(0u, 1); br_key_up(0u);
    assert(br_arp_keys[0].count == 1u);
    br_hold_toggle(); assert(br_arp_keys[0].count == 0u);
    br_arp_toggle(); run(2u); assert(!br_arp_state[0].sounding);

    /* HOLD: a released key keeps sounding until the next fresh key. */
    setup(); br_arp_toggle();                           /* ARP OFF again */
    br_hold_toggle(); assert(br_hold_on);
    br_key_down(0u, 1); br_key_up(0u);
    assert(br_ui.sustain_note[0] && !br_ui.held_note[0]);
    br_key_down(4u, 1);
    assert(!br_ui.sustain_note[0] && br_ui.held_note[4]);
    br_key_up(4u); assert(br_ui.sustain_note[4]);
    br_hold_toggle(); assert(!br_hold_on && !br_ui.sustain_note[4]);

    /* REC while playing: 1/16 arp notes land on consecutive 1/16 steps. */
    setup();
    br_part[0].division = 1u; br_part[0].length = 16u;
    for (uint32_t i = 0u; i < BR_STEPS; ++i) br_part[0].step[i] = 0u;
    br_recording = 1u;
    chord(c_major, 3u);
    br_midi(0xFAu, 0u, 0u);
    run(blocks_per_step * 4u + 2u);
    br_midi(0xFCu, 0u, 0u); run(2u);
    {
        const uint8_t want[4] = {60, 64, 67, 60};
        for (uint32_t i = 0u; i < 4u; ++i) {
            uint32_t word = br_part[0].step[i];
            if (!(word & BR_STEP_ON) || BR_STEP_NOTE(word) != want[i]) {
                printf("recorded step %u = %08x\n", i, word); assert(0);
            }
        }
    }
    br_recording = 0u;

    /* Settings survive a session round trip; defaults save as zero bytes. */
    setup();
    {
        br_session_t saved;
        assert(br_capture(&saved));
        for (uint32_t i = 0u; i < 8u; ++i) assert(!saved.v3.part[0].lane_level[i]);
        br_part[1].arp_mode = BR_ARP_RANDOM; br_part[1].arp_rate = 4u; br_part[1].arp_octaves = 3u;
        br_part[1].arp_gate = 100u; br_part[1].arp_latch = 1u; br_part[1].arp_swing = 0u;
        br_part[1].arp_repeat = 4u;
        assert(br_capture(&saved) && br_session_v16_valid(&saved));
        br_arp_part_defaults(&br_part[1]);
        assert(br_restore(&saved));
        assert(br_part[1].arp_mode == BR_ARP_RANDOM && br_part[1].arp_rate == 4u &&
               br_part[1].arp_octaves == 3u && br_part[1].arp_gate == 100u &&
               br_part[1].arp_latch == 1u && br_part[1].arp_swing == 0u &&
               br_part[1].arp_repeat == 4u);
        saved.v3.part[1].lane_level[1] = 9u;            /* invalid mode */
        assert(!br_session_v16_valid(&saved));
    }
    printf("arp, hold, recording and session checks passed\n");
    return 0;
}
