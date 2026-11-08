/* Verify sequencer defaults and held-step locks with the real UI logic. */
#define DINGHY_HOST 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
int main(void)
{
    br_app_init(); fm1_ms=2000u;
    br_load_preset(0,BR_FIRST_PLAITS);
    br_ui.part=0; br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0;
    br_ui.cursor=7u;
    br_control_turn(0, 5); br_control_turn(1, 16);
    br_control_turn(2, -20); br_control_turn(3, 25);
    assert(br_control(0).value==65 && br_control(1).value==32);
    assert(br_control(2).value==80 && br_control(3).value==25);
    assert(!(br_part[0].step[7] & BR_STEP_ON));
    br_step_trigger_toggle(0, 2, 0, 0, 0);
    assert(br_ui.cursor==7u);
    assert(br_step_note_get(0,2,0)==65 && br_step_length_get(0,2,0)==32);
    assert(br_step_velocity_get(0,2,0)==80 && br_step_condition_get(0,2,0)==25);
    br_control_turn(0, 3); br_control_turn(2, 5);
    assert(br_step_note_get(0,2,0)==65 && br_step_velocity_get(0,2,0)==80);
    br_ui.step_press_key=0; br_ui.step_press_part=0; br_ui.step_press_step=2;
    br_ui.step_press_lane=0; br_ui.step_press_kit=0; br_ui.step_press_active=1;
    br_step_edit_begin();
    assert(br_control(0).value==65 && br_ui.cursor==7u);
    br_control_turn(0,-2); br_control_turn(1,3); br_control_turn(2,7); br_control_turn(3,1);
    assert(br_step_note_get(0,2,0)==63 && br_step_length_get(0,2,0)==35);
    assert(br_step_velocity_get(0,2,0)==87 && br_step_condition_get(0,2,0)==26);
    br_step_edit_end();
    assert(br_control(0).value==68 && br_control(1).value==32);
    assert(br_control(2).value==85 && br_control(3).value==25);
    /* Turning a knob while holding an empty step creates and locks a note. */
    br_ui.step_press_key=0; br_ui.step_press_part=0; br_ui.step_press_step=3;
    br_ui.step_press_lane=0; br_ui.step_press_kit=0; br_ui.step_press_active=0;
    br_step_edit_begin(); br_control_turn(0,1); br_step_edit_end();
    assert(br_step_note_get(0,3,0)==69 && br_step_length_get(0,3,0)==32);
    assert(br_control(0).value==68);
    /* Drum lane defaults are independent and survive edits to other lanes. */
    br_ui.part=BR_DRUM_PART; br_ui.lane=1u;
    br_control_turn(0,4); br_control_turn(1,20); br_control_turn(2,-10); br_control_turn(3,5);
    br_step_trigger_toggle(BR_DRUM_PART,4,1,1,0);
    assert(br_step_note_get(BR_DRUM_PART,4,1)==64);
    assert(br_step_length_get(BR_DRUM_PART,4,1)==20);
    assert(br_step_velocity_get(BR_DRUM_PART,4,1)==90);
    assert(br_step_condition_get(BR_DRUM_PART,4,1)==5);
    br_ui.lane=2u; assert(br_control(0).value==60 && br_control(1).value==0);
    assert(br_control(2).value==100 && br_control(3).value==0);
    /* The established session format persists every created/locked value. */
    br_session_t saved;
    assert(br_capture(&saved));
    br_step_trigger_clear(0,2,0); br_step_trigger_clear(BR_DRUM_PART,4,1);
    assert(br_restore(&saved));
    assert(br_step_note_get(0,2,0)==63 && br_step_length_get(0,2,0)==35);
    assert(br_step_velocity_get(0,2,0)==87 && br_step_condition_get(0,2,0)==26);
    assert(br_step_note_get(BR_DRUM_PART,4,1)==64 && br_step_length_get(BR_DRUM_PART,4,1)==20);
    assert(br_step_velocity_get(BR_DRUM_PART,4,1)==90 && br_step_condition_get(BR_DRUM_PART,4,1)==5);
    /* Exercise the scan loop: hold an empty step, turn a knob, release. */
    br_ui.part=0; br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0;
    br_ui.step_bank=0; br_ui.previous_keys=0; fm1_in.notes=1u << 9;
    host_note_pressed=1u << 9; br_app_tick();
    assert(br_step_press_pending() && !br_ui.step_press_active);
    host_encoder[ENC_KNOB1]=1; br_app_tick();
    assert(br_ui.step_edit_active && br_step_active(0,5,0,0));
    assert(br_step_note_get(0,5,0)==69);
    fm1_in.notes=0; br_app_tick();
    assert(!br_step_press_pending() && br_step_active(0,5,0,0));
    assert(br_control(0).value==68);
    /* A short tap on the existing step still removes it. */
    fm1_in.notes=1u << 9; host_note_pressed=1u << 9; br_app_tick();
    fm1_in.notes=0; br_app_tick();
    assert(!br_step_active(0,5,0,0));
    /* Octave keys navigate the visible 16-step bank and stop at each end. */
    br_ui.step_bank=0u; br_ui.cursor=3u; br_leds_mode(0);
    assert(host_led_level_state[BTN_OCT_DOWN]==FM1_LED_OFF);
    assert(host_led_level_state[BTN_OCT_UP]==FM1_LED_BRIGHT);
    br_buttons(1u << BTN_OCT_DOWN); assert(br_ui.step_bank==0u);
    br_buttons(1u << BTN_OCT_UP); assert(br_ui.step_bank==1u && br_ui.cursor==19u);
    br_leds_mode(0);
    assert(host_led_level_state[BTN_OCT_DOWN]==FM1_LED_BRIGHT);
    assert(host_led_level_state[BTN_OCT_UP]==FM1_LED_BRIGHT);
    br_buttons(1u << BTN_OCT_UP); br_buttons(1u << BTN_OCT_UP);
    assert(br_ui.step_bank==BR_BANKS-1u); br_leds_mode(0);
    assert(host_led_level_state[BTN_OCT_UP]==FM1_LED_OFF);
    br_buttons(1u << BTN_OCT_UP); assert(br_ui.step_bank==BR_BANKS-1u);
    br_buttons(1u << BTN_OCT_DOWN); assert(br_ui.step_bank==BR_BANKS-2u);
    br_seq_press(); br_seq_release(); assert(br_ui.screen[BR_PAGE_SEQ]==1u);
    br_seq_press(); br_seq_release(); assert(br_ui.screen[BR_PAGE_SEQ]==0u);
    /* SEQ + KNOB1 remains a length gesture and does not open settings. */
    br_seq_press(); br_length_turn(1); br_seq_release();
    assert(br_ui.screen[BR_PAGE_SEQ]==0u);
    /* Held-step copy/paste consumes REC and PLAY, including on an empty destination. */
    br_ui.part=0u; br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0u; br_ui.step_bank=0u;
    br_playing=0; br_recording=0;
    /* With nothing copied, PLAY is consumed and the empty step stays empty. */
    assert(!br_step_clipboard.valid);
    br_part[0].step[2]=0;
    fm1_in.notes=1u<<4; host_note_pressed=1u<<4; br_app_tick();
    fm1_in.buttons=1u<<BTN_PLAY; host_pressed=1u<<BTN_PLAY; br_app_tick();
    fm1_in.buttons=0; host_released=1u<<BTN_PLAY; fm1_in.notes=0; br_app_tick();
    assert(!br_playing && !br_step_active(0,2,0,0));
    br_part[0].step[0]=BR_NOTE_STEP(67,91)|BR_STEP_TIE;
    br_step_length_set(0,0,0,37); br_step_condition_set(0,0,0,23); br_step_micro_set(0,0,0,-3);
    fm1_in.notes=1u; host_note_pressed=1u; br_app_tick();
    fm1_in.buttons=1u<<BTN_REC; host_pressed=1u<<BTN_REC; br_app_tick();
    assert(!br_recording && br_step_clipboard.valid && !es.rec_pending);
    fm1_in.buttons=0; host_released=1u<<BTN_REC; fm1_in.notes=0; br_app_tick();
    assert((br_part[0].step[0] & BR_STEP_ON) && !br_recording && !es.rec_pending && !es.mode);
    br_ui.part=1; br_part[1].step[1]=0;
    fm1_in.notes=1u<<2; host_note_pressed=1u<<2; br_app_tick();
    fm1_in.buttons=1u<<BTN_PLAY; host_pressed=1u<<BTN_PLAY; br_app_tick();
    assert(!br_playing);
    fm1_in.buttons=0; host_released=1u<<BTN_PLAY; fm1_in.notes=0; br_app_tick();
    assert(br_part[1].step[1]==(BR_NOTE_STEP(67,91)|BR_STEP_TIE));
    assert(br_step_length_get(1,1,0)==37 && br_step_condition_get(1,1,0)==23);
    assert(br_step_micro_get(1,1,0)==-3);
    /* Drum paste changes just one lane, including retrigger metadata. */
    br_ui.part=BR_DRUM_PART; br_ui.lane=1;
    br_part[3].step[0]=(1u<<1)|(1u<<9)|(1u<<2); br_seq_kit_retrig[0][1]=7;
    br_step_length_set(3,0,1,11); br_step_velocity_set(3,0,1,99); br_step_micro_set(3,0,1,2);
    fm1_in.notes=1u; host_note_pressed=1u; br_app_tick();
    fm1_in.buttons=1u<<BTN_REC; host_pressed=1u<<BTN_REC; br_app_tick();
    fm1_in.buttons=0; host_released=1u<<BTN_REC; fm1_in.notes=0; br_app_tick();
    br_ui.lane=3; br_part[3].step[1]=(1u<<2);
    fm1_in.notes=1u<<2; host_note_pressed=1u<<2; br_app_tick();
    fm1_in.buttons=1u<<BTN_PLAY; host_pressed=1u<<BTN_PLAY; br_app_tick();
    fm1_in.buttons=0; host_released=1u<<BTN_PLAY; fm1_in.notes=0; br_app_tick();
    assert((br_part[3].step[1] & ((1u<<2)|(1u<<3)|(1u<<11)))==((1u<<2)|(1u<<3)|(1u<<11)));
    assert(br_seq_kit_retrig[1][3]==7 && br_step_length_get(3,1,3)==11);
    assert(br_step_velocity_get(3,1,3)==99 && br_step_micro_get(3,1,3)==2);
    /* An empty copy leaves the previous clipboard intact and does not create a step. */
    br_step_clipboard_t before = br_step_clipboard;
    br_ui.part=0; br_part[0].step[2]=0;
    fm1_in.notes=1u<<4; host_note_pressed=1u<<4; br_app_tick();
    fm1_in.buttons=1u<<BTN_REC; host_pressed=1u<<BTN_REC; br_app_tick();
    fm1_in.buttons=0; host_released=1u<<BTN_REC; fm1_in.notes=0; br_app_tick();
    assert(!br_step_active(0,2,0,0));
    assert(!memcmp(&before, &br_step_clipboard, sizeof before));
    /* A kit-to-synth paste is rejected without changing the destination. */
    br_part[0].step[2]=BR_NOTE_STEP(70,80);
    uint32_t destination = br_part[0].step[2];
    fm1_in.notes=1u<<4; host_note_pressed=1u<<4; br_app_tick();
    fm1_in.buttons=1u<<BTN_PLAY; host_pressed=1u<<BTN_PLAY; br_app_tick();
    fm1_in.buttons=0; host_released=1u<<BTN_PLAY; fm1_in.notes=0; br_app_tick();
    assert(br_part[0].step[2]==destination && !br_playing);
    return 0;
}
