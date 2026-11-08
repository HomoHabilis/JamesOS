/* Host-only end-to-end prototype through the real audio MIDI/clock APIs. */
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static void clock_event(uint8_t status, uint32_t tick)
{ br_engine_clock_midi_event(status, tick, br_midi_clock_generation); }
static uint64_t frame;
static uint32_t half, lead_frames, timer_origin;
static void setup_at(uint32_t lead, uint32_t origin)
{
    br_app_init(); br_load_preset(0u, BR_FIRST_PLAITS);
    br_part[0].length=3u; br_part[0].division=1u; br_part[0].swing=77u;
    for (uint32_t i=0u;i<3u;++i) {
        br_part[0].step[i]=BR_NOTE_STEP(60u+i,100u);
        br_seq_synth_velocity[0][i]=255u; br_seq_synth_length[0][i]=16u;
        br_seq_synth_micro[0][i]=0;
    }
    if (!br_arp_on) br_arp_toggle();
    br_part[0].arp_rate=2u; br_arp_add(0u,60u,0u);
    memset(&br_clock_experiment,0,sizeof br_clock_experiment);
    br_midi_clock_enabled=1u; frame=0u; half=0u; lead_frames=lead; timer_origin=origin;
    br_engine_clock_dma_edge(origin,0u,lead);
}
static void setup(void) { setup_at(BR_BLOCK,0u); }
static void block(void)
{
    int32_t out[BR_BLOCK*2u]={0};
    assert(app_audio(out,BR_BLOCK)==FM1_AUDIO_RENDER_OK);
    frame+=BR_BLOCK; half^=1u;
    uint32_t observed=timer_origin+(uint32_t)((frame*24000000u)/FS);
    observed = half ? observed + 100u : observed - 100u;
    br_engine_clock_dma_edge(observed,half,lead_frames);
}
int main(void)
{
    setup(); uint32_t next_tick=0u, was_playing=0u;
    uint64_t stopped_frame=0u; uint32_t saw_stop=0u, saw_resume=0u;
    for (uint32_t b=0u;b<1200u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
        if (b==20u) clock_event(0xfau,now);
        if (b==710u) clock_event(0xfcu,now);  /* ~8 beats after Start */
        if (b==1055u) clock_event(0xfbu,now); /* ~4 beats of stopped clock */
        block();
        if (br_playing!=was_playing) {
            fprintf(stderr,"block=%u playing=%u paused=%u active=%u invalid=%u seek=%u follow_run=%u locked=%u pending=%u probe=%u wait=%u phase=%llu target=%llu anchor=%u\n",b,br_playing,br_midi_paused,br_midi_clock_active,br_clock_experiment.invalid,br_clock_experiment.seek_unsupported,br_clock_experiment.follower.running,br_clock_experiment.follower.locked,br_clock_experiment.follower.pending,br_clock_experiment.follower.probe_active,br_clock_experiment.wait_transport,(unsigned long long)br_clock_experiment.phase_q16,(unsigned long long)br_clock_experiment.track[0].target_phase_q16,br_clock_experiment.anchor.valid);
            was_playing=br_playing;
        }
        if (b>=710u && b<1055u && !br_playing) {
            if (!saw_stop) stopped_frame=br_seq_frames[0];
            saw_stop=1u;
            assert(br_seq_frames[0] == stopped_frame);
        }
        if (b>1055u && br_playing) saw_resume=1u;
        if (b==1054u) assert(saw_stop && !br_playing && br_midi_paused && br_midi_clock_active);
        if (b==1100u && !saw_resume) fprintf(stderr,"NO RESUME: invalid=%u seek=%u pending=%u locked=%u paused=%u follower=%u wait=%u anchor=%u drops=%u\n",br_clock_experiment.invalid,br_clock_experiment.seek_unsupported,br_clock_experiment.follower.pending,br_clock_experiment.follower.locked,br_midi_paused,br_clock_experiment.follower.running,br_clock_experiment.wait_transport,br_clock_experiment.anchor.valid,br_dropped_events);
    }
    assert(saw_stop && saw_resume); puts("long clocked Stop/Continue passed");
}
