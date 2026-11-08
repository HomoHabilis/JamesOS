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
static uint32_t next_random(void)
{
    random_state ^= random_state << 13; random_state ^= random_state >> 17;
    random_state ^= random_state << 5; return random_state;
}
static void check_seed(uint32_t seed)
{
    random_state=seed; setup();
    uint64_t ideal_tick=3000000u, next_arrival=ideal_tick;
    uint32_t pulse=0u, started=0u, samples=0u;
    uint64_t bpm_sum=0u, phase_at_start=0u, measure_frame=0u;
    for (uint32_t b=0u;b<4000u;++b) {
        uint64_t now=(frame*24000000u)/FS;
        while (next_arrival<=now) {
            clock_event(0xf8u,(uint32_t)next_arrival);
            ++pulse;
            ideal_tick += pulse<192u ? 500000u : 250000u;
            int32_t jitter=(int32_t)(next_random()%144001u)-72000;
            next_arrival=(uint64_t)((int64_t)ideal_tick+jitter);
        }
        if (b==50u) clock_event(0xfau,(uint32_t)now);
        block();
        assert(!br_clock_experiment.invalid);
        if (br_playing) started=1u;
        if (b==2000u) { phase_at_start=br_clock_experiment.phase_q16; measure_frame=frame; }
        if (b>2000u && br_playing && br_midi_clock_active) {
            bpm_sum+=br_midi_clock_bpm; ++samples;
        }
    }
    assert(started && br_playing && br_midi_clock_active && samples>1800u);
    double mean=(double)bpm_sum/samples;
    uint64_t expected=(frame-measure_frame)*96u*65536u/FS;
    int64_t actual=(int64_t)(br_clock_experiment.phase_q16-phase_at_start);
    int64_t err=actual-(int64_t)expected;
    fprintf(stderr,"mean BPM %.3f phase drift %.4f pulses over %.2fs\n", mean,
            (double)err/65536.0,(double)(frame-measure_frame)/FS);
    assert(mean>238.0 && mean<242.0);
    assert(err > -(int64_t)(2u*65536u) && err < (int64_t)(2u*65536u));
}
int main(void)
{
    check_seed(42u); check_seed(123u); check_seed(98765u); check_seed(777777u);
    puts("240 BPM jittered actual-engine drift checks passed");
    return 0;
}
