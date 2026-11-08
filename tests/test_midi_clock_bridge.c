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
    /* Once a lost clock is idle for more than the projection limit, it must
     * remain quiet rather than report an invalid event on every audio block. */
    setup();
    br_clock_experiment.follower.have_tick=0u;
    br_clock_experiment.follower.pending=0u;
    br_clock_experiment.follower.running=0u;
    br_clock_experiment.follower.now=100u;
    br_clock_experiment.render_frame=100u+0x01000000u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    uint32_t idle_invalid_before=br_clock_fail_count[0];
#endif
    br_clock_project(); br_clock_project();
    assert(!br_clock_experiment.invalid);
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    assert(br_clock_fail_count[0]==idle_invalid_before);
#endif
    br_clock_experiment.follower.pending=0xfau;
    br_clock_project();
    assert(br_clock_experiment.invalid);
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    assert(br_clock_fail_count[0]==idle_invalid_before+1u);
#endif
    setup();
    uint32_t next_tick=0u; uint64_t last_phase=0u; unsigned saw_start=0u;
    for (uint32_t b=0u;b<1800u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick <= now) {
            clock_event(0xf8u,next_tick);
            next_tick += b < 1000u ? 500000u : 600000u;
        }
        if (b==20u) clock_event(0xfau,now);
        block();
        assert(!br_clock_experiment.invalid);
        if (br_playing) {
            if (!saw_start) assert(br_arp_state[0].step_count == 1u);
            saw_start=1u;
            assert(br_clock_experiment.phase_q16 >= last_phase);
            last_phase=br_clock_experiment.phase_q16;
        }
    }
    assert(saw_start && br_arp_state[0].step_count > 30u);
    /* A clock gap triggers stop/wait rather than internal tempo fallback. */
    for (uint32_t b=0u;b<30u;++b) block();
    assert(!br_playing && br_clock_experiment.invalid);
    uint32_t stopped_arp_steps=br_arp_state[0].step_count;
    for (uint32_t b=0u;b<100u;++b) block();
    assert(br_arp_state[0].step_count == stopped_arp_steps && !br_arp_state[0].sounding);
    /* New pulses relock while stopped; Continue waits for a new clock. */
    uint32_t resume_tick=(uint32_t)((frame*24000000u)/FS)+100000u;
    for (uint32_t b=0u;b<300u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (resume_tick<=now) { clock_event(0xf8u,resume_tick); resume_tick+=600000u; }
        block();
    }
    assert(!br_playing && br_midi_clock_active);
    clock_event(0xfbu,(uint32_t)((frame*24000000u)/FS));
    block(); assert(!br_playing);
    for (uint32_t b=0u;b<20u && !br_playing;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (resume_tick<=now) { clock_event(0xf8u,resume_tick); resume_tick+=600000u; }
        block();
    }
    assert(br_playing && !br_clock_experiment.invalid);
    /* Local Stop shares MIDI pause semantics and freezes follower phase. */
    br_buttons(1u << BTN_PLAY); block();
    assert(!br_playing && br_midi_paused && !br_clock_experiment.follower.running);
    uint64_t frozen = br_clock_experiment.follower.phase_q16;
    block(); assert(br_clock_experiment.follower.phase_q16 == frozen);
    clock_event(0xfbu,(uint32_t)((frame*24000000u)/FS)); block();
    for (uint32_t b=0u;b<20u && !br_playing;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (resume_tick<=now) { clock_event(0xf8u,resume_tick); resume_tick+=600000u; }
        block();
    }
    assert(br_playing && !br_clock_experiment.invalid);
    /* Changing clock source must not leave the experimental phase frozen. */
    br_midi_clock_enabled=0u; block();
    assert(!br_playing && !br_clock_experiment.enabled);
    /* Start before any clock waits for lock and a quarter boundary.
     * At 60, 120 and 300 BPM it must never sound at provisional tempo. */
    const uint32_t pulse_ticks[] = {1000000u, 500000u, 200000u};
    for (uint32_t j=0u;j<3u;++j) {
        setup(); clock_event(0xfau,0u); block();
        assert(!br_playing);
        uint32_t nt=(uint32_t)((frame*24000000u)/FS), seen=0u;
        for (uint32_t b=0u;b<900u && !br_playing;++b) {
            uint32_t now=(uint32_t)((frame*24000000u)/FS);
            while (nt<=now) { clock_event(0xf8u,nt); nt+=pulse_ticks[j]; ++seen; }
            block();
            if (seen<4u) assert(!br_playing);
            assert(!br_clock_experiment.invalid);
        }
        assert(br_arp_state[0].step_count == 1u);
        if (!(br_playing && br_midi_clock_active && seen>=48u)) {
            fprintf(stderr,"startup rate %u: playing=%u active=%u seen=%u wait=%u phase=%llu target=%llu\n", j, br_playing, br_midi_clock_active, seen, br_clock_experiment.wait_transport, (unsigned long long)br_clock_experiment.phase_q16, (unsigned long long)br_clock_experiment.wait_boundary_q16);
            assert(0);
        }
    }
    /* Two clocks with an indistinguishable arrival time are not invented
     * into separate ticks: clock sync fails closed. */
    setup(); clock_event(0xfau,0u);
    clock_event(0xf8u,0u); clock_event(0xf8u,0u);
    block(); assert(!br_playing && br_clock_experiment.invalid);
    /* A full shared event queue invalidates sync, clears a pending Start,
     * and leaves no clock transport running. */
    setup();
    for (uint32_t i=0u;i<BR_EVENT_CAP;++i) clock_event(0xfau,0u);
    clock_event(0xfcu,0u); block();
    assert(!br_playing && br_clock_experiment.invalid &&
           !br_clock_experiment.follower.running && !br_clock_experiment.follower.pending);
    /* A sudden 2x tempo change suspends sound while the clock reacquires,
     * then resumes the frozen transport at the new detected rate. */
    setup(); next_tick=0u; uint32_t saw_pause=0u, resumed=0u;
    for (uint32_t b=0u;b<1000u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) {
            clock_event(0xf8u,next_tick);
            next_tick += b<500u ? 500000u : 250000u;
        }
        if (b==20u) clock_event(0xfau,now);
        block();
        assert(!br_clock_experiment.invalid);
        if (br_clock_experiment.suspect_pause) saw_pause=1u;
        if (saw_pause && br_playing && br_midi_clock_active) resumed=1u;
    }
    assert(saw_pause && resumed && br_playing);
    assert(br_midi_clock_bpm>=238u && br_midi_clock_bpm<=241u);
    /* Local Stop during tempo reacquisition cancels automatic resume. */
    setup(); next_tick=0u; uint32_t cancelled=0u;
    for (uint32_t b=0u;b<800u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) {
            clock_event(0xf8u,next_tick);
            next_tick += b<400u ? 500000u : 250000u;
        }
        if (b==20u) clock_event(0xfau,now);
        block();
        if (!cancelled && br_clock_experiment.suspect_pause) {
            br_buttons(1u << BTN_PLAY); block(); cancelled=1u;
        }
        if (cancelled) assert(!br_playing);
    }
    assert(cancelled && br_midi_clock_active && !br_clock_experiment.suspect_pause);
    /* Source changes invalidate queued transport but retain notes, and
     * discard old USB receipts that foreground drains after the toggle. */
    setup();
    br_engine_midi_transport(0xfau); br_engine_note_on(0u, 65u, 100u);
    uint32_t old_generation=br_midi_clock_generation;
    br_engine_clock_source_changed(1000u);
    uint32_t queued=br_event_write;
    br_engine_clock_midi_event(0xfau,999u,old_generation);
    br_engine_clock_midi_event(0xf8u,999u,old_generation);
    assert(br_event_write==queued);
    block(); assert(!br_playing && br_live_count[0] == 1u);
    uint32_t new_epoch_tick=(uint32_t)((frame*24000000u)/FS);
    clock_event(0xfau,new_epoch_tick);
    clock_event(0xf8u,new_epoch_tick); block();
    assert(!br_playing && br_clock_experiment.wait_transport == 0xfau);
    setup(); old_generation=br_midi_clock_generation;
    br_midi_clock_enabled=0u; br_engine_clock_source_changed(UINT32_MAX-100u);
    queued=br_event_write;
    br_engine_clock_midi_event(0xfau,UINT32_MAX-101u,old_generation);
    assert(br_event_write==queued);
    /* The current source epoch remains valid across arbitrarily long gaps
     * and timer wraps because classification uses receipt generation. */
    clock_event(0xfau,20u);
    assert(br_event_write==queued+1u);
    /* Different writable leads and Timer4 wrap preserve event ordering and
     * continuous phase. Absolute lead still requires device calibration. */
    for (uint32_t halves=0u; halves<=2u; halves+=2u) {
        setup_at(halves * BR_BLOCK, UINT32_MAX-1000000u);
        uint32_t next_offset=0u; uint64_t previous=0u; uint32_t started=0u;
        for (uint32_t b=0u;b<500u;++b) {
            uint32_t offset=(uint32_t)((frame*24000000u)/FS);
            while (next_offset<=offset) {
                clock_event(0xf8u,timer_origin+next_offset); next_offset+=500000u;
            }
            if (b==20u) clock_event(0xfau,timer_origin+offset);
            block(); assert(!br_clock_experiment.invalid);
            if (br_playing) {
                assert(br_clock_experiment.phase_q16>=previous);
                previous=br_clock_experiment.phase_q16; started=1u;
            }
        }
        assert(started && br_arp_state[0].step_count>5u);
    }
    /* Engine reinitialization must never make an old receipt generation
     * valid again after a project recall/diagnostic reinitialization. */
    setup(); old_generation=br_midi_clock_generation;
    br_engine_clock_source_changed(0u); br_engine_init();
    assert(br_midi_clock_generation != old_generation);
    queued=br_event_write;
    br_engine_clock_midi_event(0xfau,0u,old_generation);
    assert(br_event_write == queued);
    /* An unsupported SPP latch survives a DMA reanchor. */
    setup(); clock_event(0xf2u,0u); block();
    assert(br_clock_experiment.seek_unsupported);
    br_engine_clock_dma_invalid();
    uint32_t edge=(uint32_t)((frame*24000000u)/FS);
    br_engine_clock_dma_edge(edge,half,lead_frames);
    assert(br_clock_experiment.seek_unsupported);
    clock_event(0xfbu,edge); br_take_events();
    assert(!br_playing && br_clock_experiment.seek_unsupported);
    /* The first event after more than six minutes of silence remains valid,
     * even across multiple Timer4 wraps. Advance DMA edges without expensive
     * DSP rendering, then inject at the actual new writable horizon. */
    setup(); br_clock_source_prev=1u;
    for (uint32_t i=0u;i<70000u;++i) {
        frame += BR_BLOCK; half ^= 1u;
        uint32_t tick=(uint32_t)((frame*24000000u)/FS);
        br_engine_clock_dma_edge(tick,half,lead_frames);
        assert(br_clock_experiment.anchor.valid);
    }
    br_clock_experiment.render_frame=br_clock_experiment.anchor.edge_frame+lead_frames;
    clock_event(0xf8u,(uint32_t)((frame*24000000u)/FS));
    block(); assert(!br_clock_experiment.invalid && br_clock_experiment.follower.have_tick);
    /* A duplicate during tempo probing must clear all probe state so a
     * subsequent valid input can acquire from a clean follower. */
    setup();
    br_clock_experiment.follower.probe_active=1u;
    br_clock_experiment.follower.probe_intervals=1u;
    br_clock_experiment.follower.multiple_run=2u;
    br_clock_experiment.follower.have_tick=1u;
    br_clock_experiment.follower.running=1u;
    br_clock_experiment.follower.probe_tick=0u;
    clock_event(0xf8u,0u); block();
    assert(br_clock_experiment.invalid && !br_clock_experiment.follower.probe_active &&
           !br_clock_experiment.follower.probe_intervals && !br_clock_experiment.follower.multiple_run);
    clock_event(0xf8u,(uint32_t)((frame*24000000u)/FS)); block();
    assert(br_clock_experiment.follower.have_tick);
    /* Repeated Start while playing waits for F8 and resets the sequencer
     * and ARP note order at the new start. */
    setup(); next_tick=0u;
    for (uint32_t b=0u;b<250u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
        if (b==20u) clock_event(0xfau,now);
        block();
    }
    assert(br_playing && br_arp_state[0].step_count>1u);
    uint32_t restart=(uint32_t)((frame*24000000u)/FS);
    while (next_tick<=restart) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
    clock_event(0xfau,restart); block(); assert(!br_playing);
    for (uint32_t b=0u;b<20u && !br_playing;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
        block();
    }
    assert(br_playing && br_step_pos[0]==0u && br_arp_state[0].step_count==1u);
    /* Two source toggles between audio blocks still invalidate old sync. */
    assert(br_playing);
    br_engine_clock_source_changed(0u); br_midi_clock_enabled=0u;
    br_engine_clock_source_changed(0u); br_midi_clock_enabled=1u;
    block();
    assert(!br_playing && !br_clock_experiment.enabled && !br_midi_clock_active);
    /* Unsupported Song Position is explicit: stop, reject Continue, and
     * only a fresh Start can clear the seek latch. */
    setup(); next_tick=0u;
    for (uint32_t b=0u;b<200u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
        if (b==20u) clock_event(0xfau,now);
        block();
    }
    assert(br_playing);
    uint32_t seek_time=(uint32_t)((frame*24000000u)/FS);
    clock_event(0xf2u,seek_time); clock_event(0xfbu,seek_time); block();
    assert(!br_playing && br_clock_experiment.seek_unsupported);
    for (uint32_t b=0u;b<40u;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
        block(); assert(!br_playing);
    }
    uint32_t restart_now=(uint32_t)((frame*24000000u)/FS);
    while (next_tick<=restart_now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
    clock_event(0xfau,restart_now);
    for (uint32_t b=0u;b<300u && !br_playing;++b) {
        uint32_t now=(uint32_t)((frame*24000000u)/FS);
        while (next_tick<=now) { clock_event(0xf8u,next_tick); next_tick+=500000u; }
        block();
    }
    if (!(br_playing && !br_clock_experiment.seek_unsupported)) {
        fprintf(stderr,"SPP restart: playing=%u seek=%u invalid=%u active=%u pending=%u wait=%u follower_running=%u phase=%llu\n",br_playing,br_clock_experiment.seek_unsupported,br_clock_experiment.invalid,br_midi_clock_active,br_clock_experiment.follower.pending,br_clock_experiment.wait_transport,br_clock_experiment.follower.running,(unsigned long long)br_clock_experiment.phase_q16);
        assert(0);
    }
    /* Valid same-generation Start/clock received just before the first DMA
     * edge maps into the pre-roll, rather than a negative frame epoch. */
    br_app_init(); memset(&br_clock_experiment,0,sizeof br_clock_experiment);
    br_midi_clock_enabled=1u; br_engine_clock_source_changed(900u);
    clock_event(0xfau,950u); clock_event(0xf8u,960u);
    br_engine_clock_dma_edge(1000u,0u,BR_BLOCK);
    { int32_t out[BR_BLOCK*2u]={0}; assert(app_audio(out,BR_BLOCK)==FM1_AUDIO_RENDER_OK); }
    assert(!br_clock_experiment.invalid && br_clock_experiment.wait_transport==0xfau &&
           br_clock_experiment.follower.have_tick && !br_playing);
    puts("MIDI timestamp-to-engine host prototype checks passed");
    return 0;
}
