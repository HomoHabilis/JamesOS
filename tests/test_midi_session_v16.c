#include <stdint.h>
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
int main(void)
{
    br_session_t s, old;
    br_app_init();
    for (uint32_t i = 0; i < 7; ++i) br_midi_channels[i] = (uint8_t)(16u - i);
    br_midi_clock_enabled = 1;
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            br_seq_kit_retrig[step][lane] = (step * BR_DRUM_LANES + lane) & 63u;
    assert(br_capture(&s) && sizeof s == 3484u && s.v3.version == 16u && br_session_v16_valid(&s));
    assert(!br_session_v15_valid(&s));
    assert(offsetof(br_session_t, kit_retrig) == 2568u);
    for (uint32_t i = 0; i < 7; ++i) br_midi_channels[i] = 1;
    br_midi_clock_enabled = 0;
    assert(br_restore(&s) && br_midi_clock_enabled == 1);
    { br_session_t same; assert(br_capture(&same)); assert(!memcmp(&same, &s, sizeof s)); }
    for (uint32_t i = 0; i < 7; ++i) assert(br_midi_channels[i] == 16u-i);
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            assert(br_seq_kit_retrig[step][lane] == ((step * BR_DRUM_LANES + lane) & 63u));
    { /* Every channel encoding, duplicate mapping and both clock states round trip. */
        for (uint32_t c = 0; c <= 16; ++c) for (uint32_t clock = 0; clock < 2; ++clock) {
            br_session_t round;
            for (uint32_t i = 0; i < 7; ++i) br_midi_channels[i] = (uint8_t)c;
            br_midi_clock_enabled = (uint8_t)clock;
            assert(br_capture(&round) && br_restore(&round));
            assert(br_midi_clock_enabled == clock);
            for (uint32_t i = 0; i < 7; ++i) assert(br_midi_channels[i] == c);
        }
    }
    { /* Each OFF flag is independent and preserves all retrigger values. */
        for (uint32_t off = 0; off < 7u; ++off) {
            br_session_t round;
            for (uint32_t i = 0; i < 7u; ++i) br_midi_channels[i] = i == off ? 0u : (uint8_t)(10u+i);
            assert(br_capture(&round) && br_restore(&round));
            for (uint32_t i = 0; i < 7u; ++i) assert(br_midi_channels[i] == (i == off ? 0u : 10u+i));
            for (uint32_t step=0; step<BR_STEPS; ++step) for (uint32_t lane=0; lane<BR_DRUM_LANES; ++lane)
                assert(br_seq_kit_retrig[step][lane] == ((step * BR_DRUM_LANES + lane) & 63u));
        }
    }
    old = s; old.kit_retrig[3][2] |= 0x80u; /* bit 29 reserved */
    assert(!br_session_v16_valid(&old) && !br_restore(&old));
    old = s; old.kit_retrig[3][3] |= 0x40u; /* OFF flag with a nonzero channel nibble */
    assert(!br_session_v16_valid(&old) && !br_restore(&old));
    old = s; old.kit_retrig[4][2] |= 0x80u; /* unused OFF flag bit */
    assert(!br_session_v16_valid(&old) && !br_restore(&old));
    old = s; old.kit_retrig[4][3] |= 0x40u; /* past the OFF flags */
    assert(!br_session_v16_valid(&old) && !br_restore(&old));
    old = s;
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) old.kit_retrig[step][lane] &= 63u;
    for (uint32_t version = 14; version <= 15; ++version) {
        old.v3.version = version;
        if (version == 14) for (uint32_t i = 0; i < BR_DRUM_LANES; ++i) old.v3.part[3].lane_level[i] &= 127u;
        assert(br_restore(&old));
        for (uint32_t i = 0; i < 7; ++i) assert(br_midi_channels[i] == i+1u);
        assert(!br_midi_clock_enabled);
    }
    old.v3.version = 13;
    memset(old.kit_note, 255, sizeof old.kit_note);
    assert(br_restore(&old));
    for (uint32_t i=0; i<7; ++i) assert(br_midi_channels[i] == i+1u);
    old.v3.version = 12;
    old.reserved_v10[0]=old.reserved_v10[1]=old.reserved_v10[2]=0;
    for (uint32_t p=0;p<3;++p) for (uint32_t step=0;step<BR_STEPS;++step) old.synth_length[p][step]&=0x7ffu;
    for (uint32_t step=0;step<BR_STEPS;++step) for (uint32_t lane=0;lane<BR_DRUM_LANES;++lane) old.kit_length[step][lane]&=0x7ffu;
    for (uint32_t p=0;p<BR_PARTS;++p) old.legacy_gate[p]=BR_GATE_OPEN;
    assert(br_session_v12_valid(&old) && br_restore(&old));
    for (uint32_t i=0; i<7; ++i) assert(br_midi_channels[i] == i+1u);
    assert(!settings_save(&s,sizeof s)); br_load_session();
    assert(!br_session_save_blocked() && br_midi_clock_enabled == 1);
    assert(br_midi_channels[0] == 16u);
    assert(!settings_save(&s,sizeof s - 1u)); br_load_session(); assert(br_session_save_blocked());
    s.v3.version = 17u; assert(!br_restore(&s));
    assert(!settings_save(&s,sizeof s)); br_load_session(); assert(br_session_save_blocked());
    puts("v16 MIDI session, retriggers, migration and protection passed");
    return 0;
}
