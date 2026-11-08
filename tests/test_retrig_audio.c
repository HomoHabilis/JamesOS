/* Per-lane retrigger scheduling and actual rendered audio. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static void setup(void) {
    br_app_init(); fm1_ms=2000u;
    br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    br_part[BR_DRUM_PART].length=16u; br_part[BR_DRUM_PART].division=1u;
    for (uint32_t i=0;i<BR_STEPS;i++) {
        br_part[BR_DRUM_PART].step[i]=0;
        for (uint32_t l=0;l<BR_DRUM_LANES;l++) {
            br_seq_kit_retrig[i][l]=0; br_seq_kit_micro[i][l]=0;
            br_seq_kit_condition[i][l]=0; br_seq_kit_velocity[i][l]=100;
            br_seq_kit_length[i][l]=16;
        }
    }
    br_bpm=240u; br_playing=1;
    br_snapshot(BR_DRUM_PART); br_select_mode(BR_DRUM_PART); br_build_effective(BR_DRUM_PART);
}
static void render(void) {
    int32_t out[BR_BLOCK*2u]={0}; assert(app_audio(out,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
}
int main(void) {
    setup(); br_part[BR_DRUM_PART].step[0]=3u;
    br_seq_kit_retrig[0][0]=3u; br_seq_kit_retrig[0][1]=2u | (1u<<3);
    render(); assert(br_retrig[0].count==3u && br_retrig[1].count==2u);
    uint32_t interval=br_tempo_quarter_frames()/32u;
    assert(br_retrig[0].interval==interval && br_retrig[1].interval==br_tempo_quarter_frames()/16u);
    assert(interval>BR_BLOCK);
    render(); assert(br_retrig[0].count==2u && br_retrig[1].count==2u);
    render(); assert(br_retrig[0].count==1u && br_retrig[1].count==1u);
    render(); render(); assert(br_retrig[0].count==0u);
    br_seq_kit_retrig[0][1]=0; render(); assert(br_retrig[1].count==0u);

    setup(); br_part[BR_DRUM_PART].step[0]=1u; br_seq_kit_retrig[0][0]=7u;
    br_seq_kit_condition[0][0]=BR_STEP_COND_NOT_FIRST;
    render(); assert(!br_retrig[0].count);
    setup(); br_part[BR_DRUM_PART].step[0]=1u; br_seq_kit_retrig[0][0]=7u;
    br_seq_kit_micro[0][0]=1; render(); assert(!br_retrig[0].count);
    for (uint32_t i=0;i<20 && !br_retrig[0].count;i++) render();
    assert(br_retrig[0].count); br_part[BR_DRUM_PART].step[0]=0; render();
    assert(!br_retrig[0].count);
    setup(); br_part[BR_DRUM_PART].step[0]=1u; br_seq_kit_retrig[0][0]=7u;
    render(); br_playing=0; render(); assert(!br_retrig[0].count);
    br_playing=1; br_part[BR_DRUM_PART].step[0]=0; render(); assert(!br_retrig[0].count);

    /* A later original in the same block replaces an already due repeat. */
    setup(); br_part[BR_DRUM_PART].step[0]=br_part[BR_DRUM_PART].step[1]=1u;
    br_seq_kit_retrig[0][0]=3u; br_seq_kit_retrig[1][0]=2u;
    br_seq_fire(BR_DRUM_PART,0,0,0); br_retrig[0].remaining=50u;
    br_seq_frames[BR_DRUM_PART]=1000u;
    br_micro_schedule(BR_DRUM_PART,1,0,100u,0);
    br_seq_process(BR_DRUM_PART,200u);
    assert(br_retrig[0].step==1u && br_retrig[0].count==2u && !br_retrig[0].pending);
    /* Engine reset clears runtime, preserving metadata loaded by the session. */
    br_engine_init(); assert(!br_retrig[0].count && br_seq_kit_retrig[1][0]==2u);

    /* Output must diverge only after the first scheduled repeat. */
    static int32_t baseline[6][BR_BLOCK*2u], repeated[6][BR_BLOCK*2u];
    static const uint32_t kits[]={BR_KIT_BRAIDS,BR_KIT_PLAITS,BR_KIT_808,BR_KIT_909};
    for(uint32_t i=0;i<4u;i++){
        setup(); br_load_preset(BR_DRUM_PART,kits[i]); br_part[BR_DRUM_PART].step[0]=1u;
        for (uint32_t b=0;b<6;b++) assert(app_audio(baseline[b],BR_BLOCK)==FM1_AUDIO_RENDER_OK);
        setup(); br_load_preset(BR_DRUM_PART,kits[i]); br_part[BR_DRUM_PART].step[0]=1u; br_seq_kit_retrig[0][0]=2u;
        for (uint32_t b=0;b<6;b++) assert(app_audio(repeated[b],BR_BLOCK)==FM1_AUDIO_RENDER_OK);
        assert(!memcmp(baseline[0], repeated[0], sizeof baseline[0]));
        assert(memcmp(baseline[3], repeated[3], sizeof baseline[3]));
    }
    puts("PASS: kit retrigger timing, lanes, cancellation and audio");
    return 0;
}
