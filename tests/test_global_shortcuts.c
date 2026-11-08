/* Exercise deferred button gestures using the real input loop and SAVE UI. */
#define DINGHY_HOST 1
#define DINGHY_EMULATOR_SAVE 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void tick(void) { ++fm1_ms; br_app_tick(); }
static void down(uint32_t b) { fm1_in.buttons |= 1u<<b; host_pressed |= 1u<<b; tick(); }
static void up(uint32_t b) { fm1_in.buttons &= ~(1u<<b); host_released |= 1u<<b; tick(); }
static void turn(uint32_t e, int32_t n) { host_encoder[e] += n; tick(); }

/* Holding GLO on the global filter page (the press began there) snaps the filter to its landmarks. */
static void snap(uint32_t encoder, uint32_t start, int32_t direction, uint32_t expected)
{
    int8_t octave=br_ui.octave;
    br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=1u;
    br_master.fx.filter=start;
    down(BTN_GLO);
    assert(br_master.fx.filter==start && br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==1u);
    turn(encoder,direction);
    assert(br_master.fx.filter==expected && br_ui.octave==octave);
    up(BTN_GLO);
    assert(br_master.fx.filter==expected && br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==1u);  /* no tap on release */
}

int main(void)
{
    br_app_init(); fm1_ms=2000;
    /* Each global mixer knob selects its own track, including at a level limit. */
    br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=0u;
    for (uint32_t k=0; k<BR_PARTS; ++k) {
        br_ui.part=(k+1u)%BR_PARTS; br_part[k].sound.level=127u;
        turn(ENC_KNOB1+k,1); assert(br_ui.part==k);
        down(BTN_OCT_DOWN); up(BTN_OCT_DOWN); assert(br_part[k].sound.mute);
        down(BTN_OCT_DOWN); up(BTN_OCT_DOWN); assert(!br_part[k].sound.mute);
    }
    br_app_init(); fm1_ms=2000;
    /* Either chord order opens MIDI without octave/mute side effects. */
    br_ui.page=BR_PAGE_HOME; br_ui.octave=0;
    down(BTN_OCT_DOWN); down(BTN_GLO);
    assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==7u && br_ui.octave==0);
    up(BTN_GLO); up(BTN_OCT_DOWN);
    down(BTN_GLO); up(BTN_GLO); assert(br_ui.screen[BR_PAGE_MIX]==8u);
    down(BTN_GLO); up(BTN_GLO); assert(br_ui.screen[BR_PAGE_MIX]==7u);
    down(BTN_GLO); down(BTN_OCT_DOWN);
    assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==0u);
    up(BTN_OCT_DOWN); up(BTN_GLO);
    assert(!br_part[0].sound.mute && br_ui.octave==0);
    br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=0u;
    down(BTN_OCT_DOWN); down(BTN_GLO); up(BTN_GLO); up(BTN_OCT_DOWN);
    assert(br_ui.screen[BR_PAGE_MIX]==7u && !br_part[0].sound.mute);
    for (uint32_t k=0;k<3u;++k) {
        br_midi_channels[k]=1u; br_control_turn(k,99); assert(br_midi_channels[k]==16u);
        br_control_turn(k,-99); assert(br_midi_channels[k]==1u);
        char text[24]; br_control_t c=br_control(k); br_control_text(k,&c,text);
        assert(!strcmp(text,"1"));
    }
    br_midi_clock_enabled=0u;
    { br_control_t c=br_control(3u); assert(!strcmp(c.label,"CLOCK SOURCE") && !strcmp(c.text,"OFF")); }
    br_control_turn(3u,1);
    { br_control_t c=br_control(3u); assert(br_midi_clock_enabled && !strcmp(c.text,"USB/MIDI")); }
    br_ui.screen[BR_PAGE_MIX]=8u;
    /* Capture a clean MIDI 2 page and compare the incremental 1 -> 2
     * transition. Verify every lane label and knob badge has visible ink. */
    {
        static uint16_t clean[240u * 240u];
        br_draw_layout_old=0xffffffffu; br_ui.redraw=1u; br_draw();
        memcpy(clean,fb,sizeof clean);
        for (uint32_t col=0;col<4u;++col) {
            uint32_t label_ink=0u, knob_ink=0u;
            for (uint32_t y=160u;y<168u;++y) for (uint32_t x=col*60u;x<(col+1u)*60u;++x)
                label_ink += fb[y*240u+x] == BR_DIM;
            for (uint32_t y=76u;y<84u;++y) for (uint32_t x=col*60u;x<(col+1u)*60u;++x)
                knob_ink += fb[y*240u+x] == RGB(168,177,180);
            assert(label_ink && knob_ink);
        }
        br_ui.screen[BR_PAGE_MIX]=7u; br_ui.redraw=1u; br_draw();
        br_ui.screen[BR_PAGE_MIX]=8u; br_ui.redraw=1u; br_draw();
        assert(!memcmp(clean,fb,sizeof clean));
    }
    for (uint32_t k=0;k<4u;++k) {
        br_midi_channels[3u+k]=1u; br_control_turn(k,99); assert(br_midi_channels[3u+k]==16u);
    }
    br_ui.screen[BR_PAGE_MIX]=2u; br_bpm=120u; br_midi_clock_bpm=138u; br_midi_clock_active=1u;
    { br_control_t c=br_control(1u); assert(c.value==138 && !strcmp(c.label,"MIDI BPM")); }
    br_control_turn(1u,5); assert(br_bpm==120u);
    br_midi_clock_active=0u; br_control_turn(1u,5); assert(br_bpm==125u);
    br_app_init(); fm1_ms=2000;
    down(BTN_GLO); assert(br_ui.page==BR_PAGE_HOME);
    up(BTN_GLO); assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==0u);
    down(BTN_GLO); assert(br_ui.screen[BR_PAGE_MIX]==0u);
    up(BTN_GLO); assert(br_ui.screen[BR_PAGE_MIX]==1u);

    br_ui.page=BR_PAGE_HOME; br_master.fx.filter=50u;
    down(BTN_GLO); turn(ENC_SELECT,1);
    assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==1u && br_master.fx.filter==51u);
    up(BTN_GLO); assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==1u);

    /* A release and final encoder detent can arrive in the same scan. */
    br_ui.page=BR_PAGE_HOME; br_master.fx.filter=50u; down(BTN_GLO);
    fm1_in.buttons &= ~(1u<<BTN_GLO); host_released |= 1u<<BTN_GLO;
    host_encoder[ENC_SELECT]=1; tick();
    assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==1u && br_master.fx.filter==51u);

    /* Arriving by holding GLO and turning does not snap: only a fresh press made on the page does. */
    br_ui.page=BR_PAGE_HOME; br_master.fx.filter=50u; down(BTN_GLO); turn(ENC_SELECT,1); turn(ENC_SELECT,1);
    assert(br_ui.page==BR_PAGE_MIX && br_master.fx.filter==52u);
    up(BTN_GLO);
    for (uint32_t e=ENC_SELECT; e<=ENC_KNOB1; e+=ENC_KNOB1-ENC_SELECT) {
        snap(e,0,1,64); snap(e,20,1,64); snap(e,64,1,127); snap(e,127,1,127);
        snap(e,127,-1,64); snap(e,100,-1,64); snap(e,64,-1,0); snap(e,0,-1,0);
    }
    /* The OCT- shortcut is gone: OCT- changes the octave and the knob turns the filter normally. */
    br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=1u; br_ui.part=0u; br_ui.octave=0; br_master.fx.filter=20u;
    down(BTN_OCT_DOWN); assert(br_ui.octave==0);
    up(BTN_OCT_DOWN); assert(br_ui.octave==-1);
    turn(ENC_KNOB1,1); assert(br_master.fx.filter!=64u && br_master.fx.filter!=0u);
    assert(br_ui.octave==-1);

    /* All sends on every track snap directionally without an octave side effect. */
    for (uint32_t p=0; p<BR_PARTS; ++p) {
        br_ui.part=p; br_ui.page=BR_PAGE_FX; br_ui.screen[BR_PAGE_FX]=BR_FX_SENDS;
        br_ui.octave=0;
        for (uint32_t k=0; k<4; ++k) {
            for (uint32_t j=0;j<4;++j) br_control_turn(j,20-br_control(j).value);
            down(BTN_OCT_DOWN); assert(br_ui.octave==0);
            turn(ENC_KNOB1+k,1); assert(br_control(k).value==64);
            turn(ENC_KNOB1+k,1); assert(br_control(k).value==127);
            turn(ENC_KNOB1+k,1); assert(br_control(k).value==127);
            turn(ENC_KNOB1+k,-1); assert(br_control(k).value==64);
            turn(ENC_KNOB1+k,-1); assert(br_control(k).value==0);
            turn(ENC_KNOB1+k,-1); assert(br_control(k).value==0);
            up(BTN_OCT_DOWN); assert(br_ui.octave==0);
            for (uint32_t j=0;j<4;++j) if(j!=k) assert(br_control(j).value==20);
        }
    }
    br_ui.part=0; br_ui.octave=0;
    down(BTN_OCT_DOWN); assert(br_ui.octave==0);
    up(BTN_OCT_DOWN); assert(br_ui.octave==-1);
    br_control_turn(0,20-br_control(0).value); memset(br_encoder_speed,0,sizeof br_encoder_speed);
    turn(ENC_KNOB1,1); assert(br_control(0).value>20 && br_control(0).value<64);
    down(BTN_OCT_DOWN);
    fm1_in.buttons &= ~(1u<<BTN_OCT_DOWN); host_released |= 1u<<BTN_OCT_DOWN;
    host_encoder[ENC_KNOB1]=1; tick();
    assert(br_control(0).value==64 && br_ui.octave==-1);
    /* Holding OCT- then playing still transposes the note before note-on. */
    br_ui.octave=0; down(BTN_OCT_DOWN);
    fm1_in.notes |= 1u; host_note_pressed |= 1u; tick();
    assert(br_ui.octave==-1 && br_ui.held_note[0]==42u);
    fm1_in.notes &= ~1u; tick(); up(BTN_OCT_DOWN); assert(br_ui.octave==-1);
    down(BTN_OCT_DOWN); down(BTN_FX); up(BTN_FX); up(BTN_OCT_DOWN);
    assert(br_ui.screen[BR_PAGE_FX]==BR_FX_DRIVE && br_ui.octave==-1);

    for (uint32_t p=0;p<BR_PARTS;++p) for (uint32_t page=BR_FX_DRIVE;page<=BR_FX_REVERB;++page) {
        br_ui.part=p; br_ui.page=BR_PAGE_FX; br_ui.screen[BR_PAGE_FX]=page; br_ui.octave=0;
        br_control_turn(3,20-br_control(3).value);
        down(BTN_OCT_DOWN);
        turn(ENC_KNOB4,1); assert(br_control(3).value==64);
        turn(ENC_KNOB4,1); assert(br_control(3).value==127);
        turn(ENC_KNOB4,-1); assert(br_control(3).value==64);
        turn(ENC_KNOB4,-1); assert(br_control(3).value==0);
        /* Tone controls keep normal adjustment while the snap modifier is held. */
        uint32_t normal=page==BR_FX_DRIVE?1u:2u;
        br_control_turn(normal,20-br_control(normal).value);
        memset(br_encoder_speed,0,sizeof br_encoder_speed);
        turn(ENC_KNOB1+normal,1); assert(br_control(normal).value==21);
        up(BTN_OCT_DOWN); assert(br_ui.octave==0);
    }

    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0; br_ui.part=BR_DRUM_PART;
    br_load_preset(BR_DRUM_PART,BR_KIT_PLAITS);br_ui.lane=0;
    down(BTN_GLO); turn(ENC_SELECT,1);
    assert(br_ui.page==BR_PAGE_SEQ && br_ui.lane==1u);
    up(BTN_GLO); assert(br_ui.page==BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX]==0u);

    br_ui.page=BR_PAGE_HOME; es_open(); assert(es.mode);
    down(BTN_GLO); assert(es.mode);
    up(BTN_GLO); assert(!es.mode && br_ui.page==BR_PAGE_MIX);
    puts("PASS: GLO release, filter shortcut, GLO-hold snap, FX sends snap, sequencer and SAVE");
    return 0;
}
