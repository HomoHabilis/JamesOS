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
static uint32_t random_state=42u;
static uint32_t rnd(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void run_case(uint32_t from, uint32_t to, uint32_t jitter, uint32_t drop_every, uint32_t extra_every)
{
    setup(); random_state=12345u;
    uint64_t ideal=3000000u, arrival=ideal;
    uint32_t pulse=0u, samples=0u, starts=0u; uint64_t sum=0u;
    uint64_t phase0=0u, frame0=0u; uint32_t phase_set=0u;
    for(uint32_t b=0u;b<4000u;++b) {
        uint64_t now=frame*24000000u/FS;
        while(arrival<=now) {
            if(!drop_every || pulse%drop_every) clock_event(0xf8u,(uint32_t)arrival);
            if(extra_every && pulse && pulse%extra_every==0u)
                clock_event(0xf8u,(uint32_t)arrival+60000u);
            ++pulse;
            uint32_t bpm=ideal < 96000000u ? from : to;
            ideal += 60000000u/bpm; /* 24MHz * 60 / 24 / BPM */
            int32_t noise=jitter ? (int32_t)(rnd()%(2u*jitter+1u))-(int32_t)jitter : 0;
            arrival=(uint64_t)((int64_t)ideal+noise);
        }
        if(b==50u) clock_event(0xfau,(uint32_t)now);
        block(); if (br_clock_experiment.invalid) { fprintf(stderr,"invalid %u->%u block=%u pulse=%u period=%llu active=%u pending=%u wait=%u anchor=%u\n",from,to,b,pulse,(unsigned long long)(br_clock_experiment.follower.period_q16>>16),br_midi_clock_active,br_clock_experiment.follower.pending,br_clock_experiment.wait_transport,br_clock_experiment.anchor.valid); assert(0); }
        if(br_playing) starts=1u;
        if(b==2000u){phase0=br_clock_experiment.phase_q16;frame0=frame;phase_set=1u;}
        if(b>2000u && br_playing && br_midi_clock_active){sum+=br_midi_clock_bpm;++samples;}
    }
    assert(starts && br_playing && br_midi_clock_active && samples>1800u && phase_set);
    double mean=(double)sum/samples;
    int64_t drift=(int64_t)(br_clock_experiment.phase_q16-phase0) - (int64_t)((frame-frame0)*(uint64_t)to*24u*65536u/(60u*FS));
    fprintf(stderr,"%u->%u jitter=%u drop=%u extra=%u mean=%.3f drift=%.3f pulses\n",from,to,jitter,drop_every,extra_every,mean,(double)drift/65536.0);
    assert(mean>(double)to*.97 && mean<(double)to*1.03);
    assert(drift>-(int64_t)(3u*65536u) && drift<(int64_t)(3u*65536u));
}
static void check_loss_and_no_auto_start(void)
{
    setup(); uint64_t next=0u;
    for (uint32_t b=0u;b<160u;++b) {
        uint64_t now=frame*24000000u/FS;
        while(next<=now) { clock_event(0xf8u,(uint32_t)next); next+=250000u; }
        block(); assert(!br_playing);
    }
    assert(br_midi_clock_active && !br_playing);
    for (uint32_t b=0u;b<40u;++b) block();
    assert(br_clock_experiment.invalid && !br_playing);
    next=frame*24000000u/FS + 250000u;
    for (uint32_t b=0u;b<160u;++b) {
        uint64_t now=frame*24000000u/FS;
        while(next<=now) { clock_event(0xf8u,(uint32_t)next); next+=250000u; }
        block(); assert(!br_playing);
    }
    assert(br_midi_clock_active && !br_playing);
}
int main(void)
{
    check_loss_and_no_auto_start();
    run_case(120u,240u,72000u,0u,0u);
    run_case(240u,120u,72000u,0u,0u);
    run_case(30u,30u,72000u,0u,0u);
    run_case(300u,300u,72000u,0u,0u);
    run_case(240u,240u,0u,48u,0u);
    run_case(240u,240u,0u,0u,48u);
    puts("MIDI clock transition/endpoints/drop engine checks passed");
}
