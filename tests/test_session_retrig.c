/* Session round trips and validation of packed kit retriggers. */
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
    assert(br_capture(&saved));
    assert(sizeof saved == 3484u && saved.v3.version == 16u);
    assert(br_session_v16_valid(&saved));
    assert(saved.v3.part[BR_DRUM_PART].lane_level[0] == 128u);
    assert(saved.v3.part[BR_DRUM_PART].lane_level[2] == 255u);
    br_kit_mute_mask = 0u;
    br_kit_solo_mask = 15u;
    memset((void *)br_seq_kit_retrig, 0, sizeof br_seq_kit_retrig);
    assert(br_restore(&saved));
    assert(br_seq_kit_note[0][0] == 255u);
    assert(br_kit_mute_mask == 5u && br_kit_solo_mask == 0u);
    assert(br_part[BR_DRUM_PART].lane_level[0] == 0u);
    assert(br_part[BR_DRUM_PART].lane_level[2] == 127u);
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            assert(br_seq_kit_retrig[step][lane] == ((step + lane) & 63u));
    saved.kit_retrig[BR_STEPS - 1u][BR_DRUM_LANES - 1u] |= 0x40u;
    assert(!br_session_v16_valid(&saved) && !br_restore(&saved));
    saved.kit_retrig[BR_STEPS - 1u][BR_DRUM_LANES - 1u] = 0x80u;
    assert(!br_session_v16_valid(&saved));
    /* v14 keeps retriggers but has no lane mutes. */
    saved.kit_retrig[BR_STEPS - 1u][BR_DRUM_LANES - 1u] = 7u;
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            saved.kit_retrig[step][lane] &= 63u;
    for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
        saved.v3.part[BR_DRUM_PART].lane_level[lane] &= 0x7fu;
    saved.v3.version = 14u;
    br_kit_mute_mask = br_kit_solo_mask = 15u;
    assert(br_session_v14_valid(&saved) && br_restore(&saved));
    assert(br_kit_mute_mask == 0u && br_kit_solo_mask == 0u);
    assert(br_seq_kit_retrig[BR_STEPS - 1u][BR_DRUM_LANES - 1u] == 7u);
    memset(saved.kit_note, 255, sizeof saved.kit_note);
    saved.kit_note[0][0] = 0u;
    saved.v3.version = 13u;
    assert(br_session_v13_valid(&saved) && br_restore(&saved));
    assert(br_seq_kit_note[0][0] == 0u);
    for (uint32_t step = 0; step < BR_STEPS; ++step)
        for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane)
            assert(br_seq_kit_retrig[step][lane] == 0u);
    /* The settings loader accepts the old prefix and defaults the new data. */
    assert(!settings_save(&saved, BR_SESSION_V13_SIZE));
    br_seq_kit_retrig[0][0] = 63u;
    br_load_session();
    assert(!br_session_save_blocked() && br_seq_kit_retrig[0][0] == 0u);
    br_seq_kit_retrig[0][0] = 63u;
    assert(br_capture(&saved));
    assert(!settings_save(&saved, sizeof saved));
    br_seq_kit_retrig[0][0] = 0u;
    br_load_session();
    assert(!br_session_save_blocked() && br_seq_kit_retrig[0][0] == 63u);
    /* A truncated current record must never be accepted as a legacy record. */
    assert(!settings_save(&saved, sizeof saved - 1u));
    br_load_session();
    assert(br_session_save_blocked());
    br_session_explicit_v12_upload_accepted(&saved);
    assert(!br_session_save_blocked());
    puts("session retrigger persistence passed");
    return 0;
}
