/* Session v17: kit retriggers and lane mutes have their own bytes and round-trip;
 * the settings sector reads only a complete v17 record. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

int main(void)
{
    br_session_t saved;
    br_app_init();
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            br_seq_kit_retrig[step][lane] = (uint8_t)((step + lane) & 63u);
    br_kit_mute_mask = 5u;
    br_kit_solo_mask = 2u;
    br_part[BR_DRUM_PART].lane_level[0] = 0u;
    br_part[BR_DRUM_PART].lane_level[2] = 127u;
    assert(br_capture(&saved) && sizeof saved == 3584u && saved.b[4] == BR_V17_VERSION && br_session_valid(&saved));
    assert(saved.b[br_v17_wire_at(BR_V17_ROW(K, lane_mute), 0u, 0u)] == 1u && saved.b[br_v17_wire_at(BR_V17_ROW(K, lane_mute), 0u, 1u)] == 0u);
    assert(saved.b[br_v17_wire_at(BR_V17_ROW(K, lane_level), 0u, 2u)] == 127u);
    br_kit_mute_mask = 0u;
    br_kit_solo_mask = 15u;
    memset((void *)br_seq_kit_retrig, 0, sizeof br_seq_kit_retrig);
    assert(br_restore(&saved));
    assert(br_seq_kit_note[0][0] == 255u);
    assert(br_kit_mute_mask == 5u && br_kit_solo_mask == 0u);   /* solo is transient */
    assert(br_part[BR_DRUM_PART].lane_level[0] == 0u && br_part[BR_DRUM_PART].lane_level[2] == 127u);
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            assert(br_seq_kit_retrig[step][lane] == ((step + lane) & 63u));
    /* High bits of a retrigger byte are out of range: cleared on restore. */
    {
        uint32_t at = BR_V17_TOTAL_LEN - 1u;   /* kit_retrig[63][3], the last byte */
        saved.b[at] = 0xc7u;
        assert(br_session_valid(&saved) && br_restore(&saved) && br_seq_kit_retrig[63][3] == 7u);
    }
    /* The settings sector: a complete v17 record loads; anything shorter is kept and protected. */
    br_seq_kit_retrig[0][0] = 63u;
    assert(br_capture(&saved));
    assert(!settings_save(&saved, sizeof saved));
    br_seq_kit_retrig[0][0] = 0u;
    br_load_session();
    assert(!br_session_save_blocked() && br_seq_kit_retrig[0][0] == 63u);
    assert(!settings_save(&saved, sizeof saved - 1u));
    br_load_session();
    assert(br_session_save_blocked());
    puts("session retrigger persistence passed");
    return 0;
}
