/* Session v17: MIDI channels (including OFF) and the clock source have their
 * own GLOBAL bytes and round-trip; old (v16) records are rejected unchanged. */
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
    br_midi_clock_enabled = 1u;
    assert(br_capture(&s) && br_session_valid(&s));
    for (uint32_t i = 0; i < 7; ++i) assert(s.b[br_v17_wire_at(BR_V17_ROW(G, midi_ch_t1) + i, 0u, 0u)] == 16u - i);
    for (uint32_t i = 0; i < 7; ++i) br_midi_channels[i] = 1u;
    br_midi_clock_enabled = 0u;
    assert(br_restore(&s) && br_midi_clock_enabled == 1);
    { br_session_t same; assert(br_capture(&same)); assert(!memcmp(&same, &s, sizeof s)); }
    for (uint32_t i = 0; i < 7; ++i) assert(br_midi_channels[i] == 16u - i);
    for (uint32_t off = 0; off < 7u; ++off) {
        br_session_t round;
        for (uint32_t i = 0; i < 7u; ++i) br_midi_channels[i] = (uint8_t)(i == off ? 0u : 10u + i);
        assert(br_capture(&round) && br_restore(&round));
        for (uint32_t i = 0; i < 7u; ++i) assert(br_midi_channels[i] == (i == off ? 0u : 10u + i));
    }
    /* Out of range: clamped to 16. */
    old = s; old.b[br_v17_wire_at(BR_V17_ROW(G, midi_ch_t2), 0u, 0u)] = 40u;
    assert(br_restore(&old) && br_midi_channels[1] == 16u);
    /* An old v16 record (magic 0x42524431, version 16) is rejected; nothing changes. */
    {
        br_session_t before, after;
        memset(&old, 0, sizeof old);
        old.b[0] = 0x31; old.b[1] = 0x44; old.b[2] = 0x52; old.b[3] = 0x42; old.b[7] = 16u;
        assert(br_capture(&before));
        assert(!br_session_valid(&old) && !br_restore(&old));
        assert(br_capture(&after) && !memcmp(&before, &after, sizeof before));
    }
    assert(!settings_save(&s, sizeof s)); br_load_session();
    assert(!br_session_save_blocked() && br_midi_clock_enabled == 1 && br_midi_channels[0] == 16u);
    assert(!settings_save(&s, sizeof s - 1u)); br_load_session(); assert(br_session_save_blocked());
    s.b[4] = BR_V17_VERSION + 1u; assert(!br_restore(&s));
    assert(!settings_save(&s, sizeof s)); br_load_session(); assert(br_session_save_blocked());
    puts("v17 MIDI session, clock source, old-record rejection and protection passed");
    return 0;
}
