/* Host-only second stage for v16 conversion: old visible PATCH 1/2 were
 * track routes 0/1. Move them to the v18 per-track panel slots exactly once. */
#include "../firmware/src/instrument_session_v17.h"
#include "legacy_delay_map.h"
#include "legacy_lfo_rate_map.h"
#include "legacy_pitch_depth_map.h"
#include <stdio.h>
static uint8_t bytes[BR_V17_SIZE_LIMIT];
static uint8_t old_session[3484];
static br_session_v17_t session;
int main(int argc, char **argv)
{
    FILE *f;
    uint32_t clamps = 0;
    if (argc != 4 || !(f = fopen(argv[1], "rb"))) return 1;
    size_t n = fread(bytes, 1, sizeof bytes, f);
    if (fgetc(f) != EOF || ferror(f) || fclose(f) || n != BR_V17_TOTAL_LEN ||
        br_v17_rd(bytes + 4, 2u) != BR_V17_LEGACY_VERSION ||
        br_v17_decode(bytes, (uint32_t)n, &session, &clamps) != BR_V17_OK || clamps) {
        fputs("Invalid converted v17 session.\n", stderr); return 1;
    }
    if (!(f = fopen(argv[2], "rb"))) return 1;
    n = fread(old_session, 1, sizeof old_session, f);
    if (fgetc(f) != EOF || ferror(f) || fclose(f) || n != sizeof old_session ||
        old_session[0] != '1' || old_session[1] != 'D' ||
        old_session[2] != 'R' || old_session[3] != 'B' || old_session[7] != 16) {
        fputs("Invalid original v16 session.\n", stderr); return 1;
    }
    for (uint32_t p = 0; p < BR_V17_TRACKS; ++p) {
        br_v17_track_t *track = &session.track[p];
        int rate_mod = ((track->menv_target == 18 || track->menv_target == 19) && track->menv_amount) ||
                       ((track->lfo1_target == 18 || track->lfo1_target == 19) && track->lfo1_depth) ||
                       ((track->lfo2_target == 18 || track->lfo2_target == 19) && track->lfo2_depth);
        for (uint32_t slot = 0; slot < 8; ++slot)
            if ((track->route_target[slot] == 18 || track->route_target[slot] == 19) &&
                track->route_source[slot] && track->route_enabled[slot] && track->route_amount[slot])
                rate_mod = 1;
        if (rate_mod)
            printf("Warning: track %u modulates LFO rate; the speed response changed and should be auditioned.\n", p+1u);
        if (track->lfo1_target == 1) track->lfo1_depth = (int16_t)jamesos_v16_pitch_depth(track->lfo1_depth);
        if (track->lfo2_target == 1) track->lfo2_depth = (int16_t)jamesos_v16_pitch_depth(track->lfo2_depth);
        for (uint32_t slot = 0; slot < 8; ++slot)
            if (track->route_target[slot] == 1)
                track->route_amount[slot] = (int16_t)jamesos_v16_pitch_depth(track->route_amount[slot]);
    }
    for (uint32_t p = 0; p < BR_V17_TRACKS; ++p) for (uint32_t slot = 0; slot < 2; ++slot) {
        br_v17_track_t *track = &session.track[p];
        session.patch[p][slot][0] = track->route_source[slot];
        session.patch[p][slot][1] = track->route_target[slot];
        session.patch[p][slot][2] = track->route_amount[slot];
        session.patch[p][slot][3] = track->route_enabled[slot];
        track->route_source[slot] = track->route_target[slot] = 0;
        track->route_amount[slot] = track->route_enabled[slot] = 0;
    }
    for (uint32_t p = 0; p < BR_V17_TRACKS; ++p) {
        br_v17_track_t *track = &session.track[p];
        const uint8_t *control = old_session + 2824u + 5u * p + 2u;
        const uint8_t target[3] = {2u, 1u, 3u}; /* TIMBRE, PITCH, MORPH */
        track->timbre_mod = track->pitch_mod = track->morph_mod = 64; /* v19 neutral */
        for (uint32_t i = 0; i < 3; ++i) {
            uint32_t slot;
            int32_t depth;
            if (control[i] > 127u) { fputs("Invalid old modulation depth.\n", stderr); return 1; }
            if (control[i] == 64u) continue;
            if (i == 2u && p == 3u) {
                puts("Drum track: old HOME MORPH modulation was inactive and was not transferred.");
                continue;
            }
            for (slot = 0; slot < 8; ++slot)
                if (!track->route_source[slot] && !track->route_target[slot] &&
                    !track->route_amount[slot] && !track->route_enabled[slot]) break;
            if (slot == 8u) {
                fprintf(stderr, "Track %u has no free route for a legacy HOME modulation depth.\n", p+1u);
                return 1;
            }
            depth = control[i] < 64u ? -(int32_t)((64u-control[i])*127u/64u) :
                    (int32_t)((control[i]-64u)*127u/63u);
            if (i == 1u) depth = jamesos_v16_pitch_depth(depth);
            track->route_source[slot] = 5; /* LFO 1 */
            track->route_target[slot] = target[i];
            track->route_amount[slot] = (int16_t)depth;
            track->route_enabled[slot] = 1;
            printf("Track %u: restored legacy HOME %s as LFO 1 route %u.\n",
                   p+1u, i == 0u ? "TIMBRE" : i == 1u ? "PITCH" : "MORPH", slot+1u);
        }
        session.track[p].delay_time = (int16_t)jamesos_v16_delay_time((uint32_t)session.track[p].delay_time);
        session.track[p].lfo1_rate = (int16_t)jamesos_v16_lfo_rate((uint32_t)session.track[p].lfo1_rate);
        session.track[p].lfo2_rate = (int16_t)jamesos_v16_lfo_rate((uint32_t)session.track[p].lfo2_rate);
    }
    if (br_v17_encode(&session, bytes, sizeof bytes, &clamps) != BR_V17_TOTAL_LEN || clamps ||
        !(f = fopen(argv[3], "wb"))) return 1;
    if (fwrite(bytes, 1, BR_V17_TOTAL_LEN, f) != BR_V17_TOTAL_LEN || fclose(f)) return 1;
    puts("Moved each track's original PATCH 1 and PATCH 2 to its v18 panel slots.");
    puts("Mapped 1.0a delay times to the same musical divisions.");
    puts("Mapped 1.0a LFO rates to the closest current speeds.");
    puts("Mapped pitch LFO and patch depths to the closest current peak amounts.");
    return 0;
}
