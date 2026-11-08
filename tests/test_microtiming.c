/* Real sequencer scheduling, record quantization and session compatibility. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static void setup(void) {
    br_app_init(); fm1_ms=2000u;
    br_ui.part=0; br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0;
    br_load_preset(0,BR_FIRST_PLAITS);
    br_part[0].length=2u; br_part[0].division=1u; br_part[0].swing=0u;
    br_part[0].step[0]=BR_NOTE_STEP(60,100);
    br_part[0].step[1]=BR_NOTE_STEP(62,100);
    br_seq_synth_velocity[0][0]=br_seq_synth_velocity[0][1]=255u;
    br_seq_synth_length[0][0]=br_seq_synth_length[0][1]=16u;
    br_seq_synth_condition[0][0]=br_seq_synth_condition[0][1]=0u;
    br_seq_synth_micro[0][0]=br_seq_synth_micro[0][1]=0;
    br_snapshot(0); br_select_mode(0); br_build_effective(0);
    br_seq_frames[0]=br_seq_gate_frames[0]=0u; br_seq_note[0]=255u;
    br_seq_next[0]=br_seq_started[0]=br_seq_deferred_off[0]=0u;
    br_seq_cycle[0]=0u; memset(br_micro_event[0],0,sizeof br_micro_event[0]);
    br_playing=1u;
}
int main(void) {
    uint32_t duration, unit, step; int32_t micro;
    setup(); duration=br_step_base_duration(0); unit=duration/8u;
    /* Positive timing delays a trigger and a negative next step arrives early. */
    br_seq_synth_micro[0][0]=1; br_seq_synth_micro[0][1]=-1;
    br_seq_process(0,unit); assert(br_seq_note[0]==255u);
    br_seq_process(0,1u); assert(br_seq_note[0]==60u);
    br_seq_process(0,duration-2u*unit-1u); assert(br_seq_note[0]==60u);
    br_seq_process(0,1u); assert(br_seq_note[0]==62u);
    /* The negative step is not re-triggered at its normal boundary. */
    br_seq_process(0,unit); assert(br_seq_note[0]==62u);
    /* The real render loop processes events in BR_BLOCK chunks. Adjacent
     * 1/128 positions remain distinct, with at most one block of latency. */
    setup(); duration=br_step_base_duration(0); unit=duration/8u;
    br_seq_synth_micro[0][0]=1; uint32_t rendered=0u, onset=0xffffffffu;
    while (rendered < duration && onset == 0xffffffffu) {
        br_seq_process(0, BR_BLOCK);
        if (br_seq_note[0] == 60u) onset=rendered;
        rendered+=BR_BLOCK;
    }
    assert(onset != 0xffffffffu && onset <= unit && unit-onset < BR_BLOCK);
    /* Last-step FIRST evaluates against the correct cycle. */
    setup(); duration=br_step_base_duration(0);
    br_part[0].step[0]=0u;
    br_seq_synth_condition[0][1]=BR_STEP_COND_FIRST;
    br_seq_process(0,duration); assert(br_seq_note[0]==255u);
    br_seq_process(0,1u); assert(br_seq_note[0]==62u);
    /* Input quantization: nearest step, full 1/128, coarser grid, wrap. */
    br_part[0].length=16u; br_part[0].division=1u; br_part[0].swing=0u;
    br_record_quantize_event(0,0,300,800,100,800,BR_TIME_QUANT_OFF,&step,&micro);
    assert(step==0u && micro==3);
    br_record_quantize_event(0,0,500,800,100,800,BR_TIME_QUANT_OFF,&step,&micro);
    assert(step==1u && micro==-3);
    br_record_quantize_event(0,0,500,800,100,800,BR_TIME_QUANT_STEP,&step,&micro);
    assert(step==1u && micro==0);
    br_record_quantize_event(0,0,500,800,100,800,BR_TIME_QUANT_16,&step,&micro);
    assert(step==1u && micro==0);
    br_record_quantize_event(0,1,400,800,100,800,BR_TIME_QUANT_8,&step,&micro);
    assert(step==2u && micro==0);
    br_record_quantize_event(0,15,750,800,100,800,BR_TIME_QUANT_OFF,&step,&micro);
    assert(step==0u && micro==0);
    /* Setting knob changes the per-track record mode, not pitch quantization. */
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=1u; br_ui.part=0;
    assert(br_control(2).value==BR_TIME_QUANT_STEP);
    br_control_turn(2,1); assert(br_seq_record_quantize[0]==BR_TIME_QUANT_OFF);
    assert(br_part[0].mod.quantize==0u);
    br_ui.part=1; assert(br_seq_record_quantize[1]==BR_TIME_QUANT_STEP);
    /* Capture uses the audio transport snapshot rather than rounding to a step. */
    br_ui.part=0; br_ui.screen[BR_PAGE_SEQ]=0; br_part[0].length=16u;
    br_record_position[0].revision=0; br_record_position[0].step=0;
    br_record_position[0].duration=800u; br_record_position[0].elapsed=300u;
    br_record_position[0].unit=100u; br_part[0].division=1u;
    /* Use a matching real nominal duration in the snapshot. */
    uint32_t nominal=br_engine_step_duration(0,0), tick=nominal/8u;
    br_record_position[0].duration=nominal; br_record_position[0].elapsed=tick*3u;
    br_record_position[0].unit=tick; br_playing=1; br_transport_prev=1; br_recording=1;
    assert(br_record_note_begin(0,70,90,0,0,0));
    assert(br_step_micro_get(0,0,0)==3 && BR_STEP_NOTE(br_part[0].step[0])==70);
    br_recording=0; br_record_note_cancel_all();
    /* Current projects preserve all signed offsets without increasing size. */
    br_seq_synth_micro[0][0]=-4; br_seq_kit_micro[2][1]=7;
    br_seq_record_quantize[0]=BR_TIME_QUANT_OFF;
    br_seq_record_quantize[3]=BR_TIME_QUANT_64;
    br_session_t saved; assert(br_capture(&saved)); assert(saved.v3.version==16u);
    assert(br_session_v16_valid(&saved));
    br_seq_synth_micro[0][0]=0; br_seq_kit_micro[2][1]=0;
    br_seq_record_quantize[0]=br_seq_record_quantize[3]=0;
    assert(br_restore(&saved));
    assert(br_seq_synth_micro[0][0]==-4 && br_seq_kit_micro[2][1]==7);
    assert(br_seq_record_quantize[0]==BR_TIME_QUANT_OFF && br_seq_record_quantize[3]==BR_TIME_QUANT_64);
    /* A legacy v12 project starts with no micro offsets and step quantization. */
    for(uint32_t p=0;p<BR_SYNTH_PARTS;p++)for(uint32_t i=0;i<BR_STEPS;i++) saved.synth_length[p][i]&=0x7ffu;
    for(uint32_t i=0;i<BR_STEPS;i++)for(uint32_t j=0;j<BR_DRUM_LANES;j++) saved.kit_length[i][j]&=0x7ffu;
    memset(saved.kit_note,255,sizeof saved.kit_note);
    saved.reserved_v10[0]=saved.reserved_v10[1]=saved.reserved_v10[2]=0u; saved.v3.version=12u;
    assert(br_session_v12_valid(&saved) && br_restore(&saved));
    assert(br_seq_synth_micro[0][0]==0 && br_seq_kit_micro[2][1]==0);
    assert(br_seq_record_quantize[0]==BR_TIME_QUANT_STEP);
    /* Actual held-key plus OCT buttons changes only the held step. */
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0; br_ui.part=0;
    br_ui.step_bank=0; br_ui.previous_keys=0; br_ui.step_press_key=BR_STEP_NO_PRESS;
    br_playing=0; br_part[0].step[0]=BR_NOTE_STEP(60,100);
    fm1_in.notes=1u; host_note_pressed=1u; br_app_tick();
    assert(br_step_press_pending());
    fm1_in.buttons=1u << BTN_OCT_UP; host_pressed=1u << BTN_OCT_UP; br_app_tick();
    assert(br_step_micro_get(0,0,0)==1 && br_ui.step_edit_active && br_ui.step_bank==0u);
    fm1_in.buttons=0; host_released=1u << BTN_OCT_UP; br_app_tick();
    fm1_in.buttons=1u << BTN_OCT_DOWN; host_pressed=1u << BTN_OCT_DOWN; br_app_tick();
    assert(br_step_micro_get(0,0,0)==0 && br_ui.step_bank==0u);
    fm1_in.buttons=0; host_released=1u << BTN_OCT_DOWN; br_app_tick();
    fm1_in.notes=0; br_app_tick();
    assert(!br_step_press_pending() && br_step_active(0,0,0,0));
    /* Independent drum lanes schedule their own offsets. */
    br_ui.part=BR_DRUM_PART; br_load_preset(BR_DRUM_PART,BR_KIT_PLAITS);
    br_part[BR_DRUM_PART].length=2u; br_part[BR_DRUM_PART].division=1u;
    br_part[BR_DRUM_PART].step[0]=1u; br_part[BR_DRUM_PART].step[1]=2u;
    br_seq_kit_micro[0][0]=1; br_seq_kit_micro[1][1]=-1;
    br_seq_kit_length[0][0]=br_seq_kit_length[1][1]=16u;
    br_seq_kit_velocity[0][0]=br_seq_kit_velocity[1][1]=100u;
    br_seq_kit_condition[0][0]=br_seq_kit_condition[1][1]=0u;
    br_snapshot(BR_DRUM_PART); br_select_mode(BR_DRUM_PART); br_build_effective(BR_DRUM_PART);
    br_seq_frames[BR_DRUM_PART]=0u; br_seq_next[BR_DRUM_PART]=0u; br_seq_started[BR_DRUM_PART]=0u;
    memset(br_micro_event[BR_DRUM_PART],0,sizeof br_micro_event[BR_DRUM_PART]);
    memset(br_seq_lane_gate_frames[BR_DRUM_PART],0,sizeof br_seq_lane_gate_frames[BR_DRUM_PART]);
    br_playing=1; duration=br_step_base_duration(BR_DRUM_PART); unit=duration/8u;
    br_seq_process(BR_DRUM_PART,unit); assert(!br_seq_lane_gate_frames[BR_DRUM_PART][0]);
    br_seq_process(BR_DRUM_PART,1); assert(br_seq_lane_gate_frames[BR_DRUM_PART][0]);
    br_seq_process(BR_DRUM_PART,duration-2u*unit-1u); assert(!br_seq_lane_gate_frames[BR_DRUM_PART][1]);
    br_seq_process(BR_DRUM_PART,1); assert(br_seq_lane_gate_frames[BR_DRUM_PART][1]);
    /* Exercise the allowed range at every subdivision and common swing value. */
    br_ui.part=0; br_part[0].length=16u;
    static const uint32_t swing_values[] = {0u, 16u, 32u, 48u, 64u, BR_SWING_MAX};
    for (uint32_t div=0; div<3; ++div) for (uint32_t si=0; si<sizeof swing_values/sizeof swing_values[0]; ++si) {
        uint32_t swing = swing_values[si];
        br_part[0].division=div; br_part[0].swing=swing;
        uint32_t base=800u, tick=base/(16u>>div);
        for (uint32_t pos=0; pos<16; ++pos) {
            uint32_t dur=base*((pos&1u)?128u-swing:128u+swing)/128u;
            for (uint32_t elapsed=0; elapsed<dur; elapsed+=17u)
                for (uint32_t mode=0; mode<BR_TIME_QUANT_COUNT; ++mode) {
                    uint32_t out; int32_t offset, lo, hi;
                    br_record_quantize_event(0,pos,elapsed,dur,tick,base,mode,&out,&offset);
                    assert(out<16u && offset>=-16 && offset<=15);
                    br_micro_bounds(0,out,&lo,&hi);
                    assert(offset>=lo && offset<=hi);
                }
        }
    }
    /* Odd-length loops and non-divisible frame counts must also stay within
     * the displayed nudge bounds. */
    for (uint32_t len=1; len<=15; len+=2) for (uint32_t div=0; div<3; ++div) {
        br_part[0].length=len; br_part[0].division=div; br_part[0].swing=37;
        uint32_t base=779u, tick=base/(16u>>div);
        for (uint32_t pos=0;pos<len;++pos) {
            uint32_t dur=base*((pos&1u)?91u:165u)/128u;
            for (uint32_t elapsed=0;elapsed<dur;elapsed+=19u) {
                uint32_t out; int32_t offset,lo,hi;
                br_record_quantize_event(0,pos,elapsed,dur,tick,base,BR_TIME_QUANT_OFF,&out,&offset);
                assert(out<len); br_micro_bounds(0,out,&lo,&hi);
                assert(offset>=lo && offset<=hi);
            }
        }
    }
    puts("PASS: microtimed playback, recording grid and session migration");
    return 0;
}
