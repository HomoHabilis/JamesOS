/* SPDX-License-Identifier: GPL-3.0-only */
#if !defined(BR_SPLIT_AUDIO) && (!defined(BR_PATTERN_SNAPSHOT_MONOLITHIC_TEST) || defined(FM1_DEVICE))
#error "pattern_snapshot.c requires shared split-audio globals (or the host monolithic test)"
#endif
#include "pattern_snapshot.h"
#include <string.h>

int br_pattern_snapshot_capture(br_pattern_snapshot *s)
{
    unsigned p, i, l;
    if (!s) return -1;
    memset(s, 0, sizeof *s);
    for (p=0; p<BR_PARTS; ++p) {
        s->length[p]=br_part[p].length; s->division[p]=br_part[p].division;
        for (i=0;i<BR_STEPS;++i) s->step[p][i]=br_part[p].step[i];
    }
    for (p=0;p<BR_SYNTH_PARTS;++p) for(i=0;i<BR_STEPS;++i) {
        s->synth_length[p][i]=br_seq_synth_length[p][i];
        s->synth_condition[p][i]=br_seq_synth_condition[p][i];
        s->synth_note[p][i]=br_seq_synth_note[p][i];
        s->synth_velocity[p][i]=br_seq_synth_velocity[p][i];
        s->synth_micro[p][i]=br_seq_synth_micro[p][i];
    }
    for(i=0;i<BR_STEPS;++i) for(l=0;l<BR_DRUM_LANES;++l) {
        s->kit_length[i][l]=br_seq_kit_length[i][l];
        s->kit_condition[i][l]=br_seq_kit_condition[i][l];
        s->kit_velocity[i][l]=br_seq_kit_velocity[i][l];
        s->kit_retrig[i][l]=br_seq_kit_retrig[i][l];
        s->kit_note[i][l]=br_seq_kit_note[i][l];
        s->kit_pitch[i][l]=br_seq_kit_pitch[i][l];
        s->kit_micro[i][l]=br_seq_kit_micro[i][l];
    }
    return 0;
}
int br_pattern_snapshot_apply(const br_pattern_snapshot *s)
{
    unsigned p, i, l;
    if (!s) return -1;
    for (p=0; p<BR_PARTS; ++p) {
        br_part[p].length=s->length[p]; br_part[p].division=s->division[p];
        for (i=0;i<BR_STEPS;++i) br_part[p].step[i]=s->step[p][i];
    }
    for (p=0;p<BR_SYNTH_PARTS;++p) for(i=0;i<BR_STEPS;++i) {
        br_seq_synth_length[p][i]=s->synth_length[p][i];
        br_seq_synth_condition[p][i]=s->synth_condition[p][i];
        br_seq_synth_note[p][i]=s->synth_note[p][i];
        br_seq_synth_velocity[p][i]=s->synth_velocity[p][i];
        br_seq_synth_micro[p][i]=s->synth_micro[p][i];
    }
    for(i=0;i<BR_STEPS;++i) for(l=0;l<BR_DRUM_LANES;++l) {
        br_seq_kit_length[i][l]=s->kit_length[i][l];
        br_seq_kit_condition[i][l]=s->kit_condition[i][l];
        br_seq_kit_velocity[i][l]=s->kit_velocity[i][l];
        br_seq_kit_retrig[i][l]=s->kit_retrig[i][l];
        br_seq_kit_note[i][l]=s->kit_note[i][l];
        br_seq_kit_pitch[i][l]=s->kit_pitch[i][l];
        br_seq_kit_micro[i][l]=s->kit_micro[i][l];
    }
    return 0;
}
