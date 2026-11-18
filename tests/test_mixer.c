/* Mixer controls exercise the real UI, session and audio snapshot paths. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include "hd_look.h"

static void tick(void) { ++fm1_ms;br_app_tick(); }
static void tap(uint32_t button) {
    fm1_in.buttons|=1u<<button;host_pressed|=1u<<button;tick();
    fm1_in.buttons&=~(1u<<button);host_released|=1u<<button;tick();
}
static void refresh_audio(void) {
    for(uint32_t p=0;p<BR_PARTS;p++){br_snapshot(p);br_build_effective(p);}
}
/* The FM-1 reaches the mixer with GLO; on the SMK-37 GLO starts on PAN and FADER BANK opens the mixer. */
static void open_mixer(void) {
#if BR_PRODUCT_SMK37
    br_smk_fader_bank_select(0u); tick(); tick();
#else
    tap(BTN_GLO);
#endif
}
int main(void) {
    br_session_t saved;uint16_t strip[240*8],clock_strip[240*8],cue[240*24],frame[240*24];
    int32_t audio[2u*HALF_FRAMES];
    br_app_init();fm1_ms=2000;
    open_mixer();
    assert(br_ui.page==BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]);
    assert(!strcmp(br_header_title(),"MIXER"));
    assert(host_led_level_state[BTN_OCT_DOWN]==FM1_LED_BRIGHT && host_led_level_state[BTN_OCT_UP]==FM1_LED_BRIGHT);
    tap(BTN_OCT_DOWN);assert(br_part[0].sound.mute==1u);
    br_ui.part=1;tap(BTN_OCT_UP);refresh_audio();
    assert(br_part[0].sound.mute==1u && br_effective_sound[0].mute==1u && !br_effective_sound[1].mute);
    tap(BTN_HOME);assert(br_solo_mask==2u);open_mixer();assert(br_solo_mask==2u);
    tap(BTN_OCT_UP);refresh_audio();assert(br_part[0].sound.mute==1u && br_effective_sound[0].mute==1u && !br_effective_sound[1].mute);
    br_ui.part=0;
    tap(BTN_OCT_UP);assert(br_solo_mask==1u);refresh_audio();
    assert(br_effective_sound[0].mute==1u); /* Explicit mute still wins. */
    for(uint32_t p=1;p<BR_PARTS;p++)assert(br_effective_sound[p].mute==1u);
    tap(BTN_OCT_DOWN);refresh_audio();assert(br_effective_sound[0].mute==0u);
    br_ui.part=1;tap(BTN_OCT_UP);assert(br_solo_mask==3u);refresh_audio();
    assert(!br_effective_sound[0].mute && !br_effective_sound[1].mute && br_effective_sound[2].mute);
    tap(BTN_OCT_UP);br_ui.part=0;tap(BTN_OCT_UP);refresh_audio();assert(br_solo_mask==0u);
    for(uint32_t p=0;p<BR_PARTS;p++)assert(!br_effective_sound[p].mute);
    tap(BTN_OCT_DOWN);tap(BTN_OCT_UP);assert(br_capture(&saved));
    assert(br_restore(&saved) && br_solo_mask==0u && br_part[0].sound.mute==1u);
    br_ui.redraw=1;br_draw();memcpy(strip,fb+240*24,sizeof strip);
    br_part[0].step[0]^=BR_STEP_ON;br_ui.cursor=3;br_ui.redraw=0;br_draw();
    assert(!memcmp(strip,fb+240*24,sizeof strip));
    br_playing=1;br_global_beat=1;br_transport_phase=0;br_draw();memcpy(clock_strip,fb+240*24,sizeof clock_strip);
    br_global_beat=2;br_transport_phase=64;br_draw();assert(memcmp(clock_strip,fb+240*24,sizeof clock_strip));
    br_playing=0;memset(audio,0,sizeof audio);app_audio(audio,HALF_FRAMES);
    br_playing=1;
    for(uint32_t i=0;i<FS*60u/(br_bpm*HALF_FRAMES)+3u;i++) {
        memset(audio,0,sizeof audio);app_audio(audio,HALF_FRAMES);
    }
    assert(br_global_beat==1u);
    br_playing=0;br_ui.message[0]=0;br_ui.message_until=0;
    br_draw_cue_bar(1,1);memcpy(cue,fb+240*216,sizeof cue);
    br_message("PROJECT SAVED");fm1_ms+=80;br_draw_cue_bar(1,1);
    memcpy(frame,fb+240*216,sizeof frame);assert(memcmp(frame,cue,sizeof cue));
    fm1_ms+=240;br_draw_cue_bar(1,1);assert(memcmp(frame,fb+240*216,sizeof frame));
    memcpy(frame,fb+240*216,sizeof frame);
    br_bpm=40;fm1_ms=br_ui.message_until-240u;br_draw_cue_bar(1,1);
    assert(memcmp(frame,fb+240*216,sizeof frame)); /* Hold duration was fixed when the message began. */
    fm1_ms=br_ui.message_until;br_draw_cue_bar(1,1);assert(!memcmp(cue,fb+240*216,sizeof cue));
    br_ui.screen[BR_PAGE_MIX]=2u;
    assert(!strcmp(br_control(0).label,"BPM"));
    assert(!strcmp(br_control(1).label,"SWING"));
    assert(strcmp(br_control(2).label,"DRUM SNAP"));   /* POLY (or empty without polyphony) */
    assert(!strcmp(br_control(3).label,"MONITOR"));
    uint32_t mute_before=br_part[br_ui.part].sound.mute;
    br_control_turn(2,1);
    br_control_turn(3,1);
    assert(br_part[br_ui.part].sound.mute==mute_before);
    /* SELECT always edits the filter (the old stored assignment is gone). */
    for (uint32_t target=0;target<BR_SELECT_COUNT;++target) {
        uint32_t bpm=br_bpm, heat=br_master.fx.heat_type, level=br_part[br_ui.part].sound.level;
        br_master.fx.filter=64u;
        br_select_turn(1);
        assert(br_master.fx.filter==65u && br_bpm==bpm && br_master.fx.heat_type==heat);
        assert(br_part[br_ui.part].sound.level==level);
    }
    /* Pan marker reaches left, center and right without changing track colors. */
    /* HD: the marker is a rounded, shaded puck with a white-to-grey face, so find it from what is
     * drawn: the centroid of the near-white pixels (luma >= 900 of 1020) on the rail's rows. It sits
     * near the rail's left end (s/5), centre (s/2) and right end (4s/5) within 3 px, in order, and the
     * left and right positions mirror about the centre within 2 px. */
    {
        const uint32_t value[3]={0u,64u,127u};
        const int32_t s=60, cy=s/2, want[3]={s/5,s/2,s*4/5};
        int32_t x2[3]={0,0,0};   /* centroid, in half pixels */
        for (uint32_t i=0;i<3u;++i) {
            int32_t sum=0,n=0;
            cv_begin(s,s,BR_BACKGROUND);cv_oy=0;bw_track_pan(0,0,(uint32_t)s,value[i],0);
            for (int32_t y=cy-6;y<=cy+6;++y) for (int32_t x=0;x<s;++x)
                if (hd_luma(swap16(cv_px[y*cv_w+x]))>=900u) { sum+=x;++n; }
            HD_CHECK(n>0,"pan %u: a near-white marker is drawn on the rail",value[i]);
            if (!n) continue;
            x2[i]=(2*sum+n/2)/n;
            HD_CHECK(abs(x2[i]-2*want[i])<=6,"pan %u: marker centroid x=%d.%d, want %d +/- 3",
                     value[i],x2[i]/2,x2[i]%2*5,want[i]);
        }
        HD_CHECK(x2[0]<x2[1] && x2[1]<x2[2],"pan markers ordered left < centre < right (%d, %d, %d half px)",
                 x2[0],x2[1],x2[2]);
        HD_CHECK(abs((x2[1]-x2[0])-(x2[2]-x2[1]))<=4,"pan markers symmetric about the centre (%d, %d, %d half px)",
                 x2[0],x2[1],x2[2]);
    }
    if (hd_failures) { printf("%u of %u HD checks failed: mixer pan marker\n",hd_failures,hd_checks);return 1; }
    puts("PASS: mixer mute, multiple solos, saved mute, LEDs and global clock strip");return 0;
}
