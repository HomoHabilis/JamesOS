/* Global tempo and swing must affect all tracks, including restored projects. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <string.h>
int main(void)
{
    br_app_init(); fm1_ms=2000u;
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=1u;
    assert(!strcmp(br_control(0).label,"STEPS"));
    assert(!strcmp(br_control(1).label,"SPEED"));
    assert(!strcmp(br_control(2).label,"QUANTIZE") && !br_control(3).label[0]);
    uint32_t tempo=br_bpm;
    br_control_turn(3u,15);
    assert(br_bpm==tempo && br_part[0].swing==0u);
    br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u; br_ui.part=2u;
    assert(!strcmp(br_control(1).label,"BPM"));
    br_control_t bpm_control = br_control(1u);
    assert(br_control_widget(1u, &bpm_control)==BR_WIDGET_BPM);
    {
        uint32_t counts[3] = {0u,0u,0u};
        const uint32_t levels[3] = {0u,64u,127u};
        for (uint32_t i=0;i<3u;++i) {
            cv_begin(60,60,BR_BACKGROUND); cv_oy=0; bw_bpm(0,0,60,levels[i],0u);
            for (uint32_t pixel=0;pixel<cv_w*cv_h;++pixel)
                if (cv_px[pixel]==swap16(BR_ACC)) ++counts[i];
        }
        assert(counts[0] < counts[1] && counts[1] < counts[2]);
        assert(cv_px[52u*cv_w+30u] != swap16(BR_ACC)); /* bottom gap stays open */
        {
            uint16_t frame[60u*60u];
            br_playing=0; assert(br_widget_phase(BR_WIDGET_BPM)==0u);
            br_playing=1; br_transport_phase=0u;
            cv_begin(60,60,BR_BACKGROUND); bw_bpm(0,0,60,64u,br_widget_phase(BR_WIDGET_BPM));
            memcpy(frame,cv_px,sizeof frame);
            br_transport_phase=64u;
            cv_begin(60,60,BR_BACKGROUND); bw_bpm(0,0,60,64u,br_widget_phase(BR_WIDGET_BPM));
            assert(memcmp(frame,cv_px,sizeof frame));
            br_playing=0;
        }
    }
    assert(!strcmp(br_control(2).label,"SWING"));
    br_control_t swing = br_control(2u);
    assert(br_control_widget(2u, &swing)==BR_WIDGET_SWING);
    cv_begin(60,60,BR_BACKGROUND); cv_oy=0; bw_swing(0,0,60,0);
    assert(cv_px[30u*cv_w+12u]==swap16(BR_ACC));
    assert(cv_px[30u*cv_w+30u]==swap16(BR_ACC));
    assert(cv_px[30u*cv_w+48u]==swap16(BR_ACC));
    cv_begin(60,60,BR_BACKGROUND); bw_swing(0,0,60,127);
    assert(cv_px[30u*cv_w+12u]==swap16(BR_ACC));
    assert(cv_px[30u*cv_w+30u]!=swap16(BR_ACC));
    assert(cv_px[30u*cv_w+36u]==swap16(BR_ACC));
    assert(cv_px[30u*cv_w+48u]==swap16(BR_ACC));
    for (uint32_t q=0; q<=127u; ++q) {
        uint32_t stem=(30u+q*60u/(127u*8u))&~1u;
        cv_begin(60,60,BR_BACKGROUND); bw_swing(0,0,60,q);
        assert(cv_px[30u*cv_w+stem]==swap16(BR_ACC));
        assert(cv_px[46u*cv_w+stem+1u]==swap16(BR_ACC));
        assert(cv_px[46u*cv_w+stem+2u]!=swap16(BR_ACC));
    }
    br_control_turn(1u,7); br_control_turn(2u,24);
    assert(br_bpm==tempo+7u && br_part[0].swing==24u);
    for (uint32_t p=0;p<BR_PARTS;++p) {
        br_ui.part=p;
        assert(br_control(2).value==24);
        br_part[p].division=1u;
        if (p) br_part[p].swing=63u; /* old per-track values cannot override global */
        assert(br_step_duration(p,0)==br_step_duration(0,0));
        assert(br_step_duration(p,1)==br_step_duration(0,1));
    }
    assert(br_step_duration(0,0)>br_step_duration(0,1));
    br_session_t saved; assert(br_capture(&saved));
    br_part[0].swing=0u; br_bpm=tempo;
    assert(br_restore(&saved));
    assert(br_part[0].swing==24u && br_bpm==tempo+7u);
    for (uint32_t p=0;p<BR_PARTS;++p) {
        assert(br_step_duration(p,0)==br_step_duration(0,0));
        assert(br_step_duration(p,1)==br_step_duration(0,1));
    }
    /* The global MIX swing control changes the shared setting. */
    br_ui.part=3u; br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u;
    assert(br_control(2).value==24); br_control_turn(2,1);
    assert(br_part[0].swing==25u && br_control(2).value==25);
    swing=br_control(2u); assert(br_control_widget(2u,&swing)==BR_WIDGET_SWING);
    {
        char text[24];
        br_part[0].swing=0u; swing=br_control(2u); br_control_text(2u,&swing,text);
        assert(!strcmp(text,"50%") && swing.high==BR_SWING_MAX);
        br_control_turn(2u,1000); swing=br_control(2u); br_control_text(2u,&swing,text);
        assert(!strcmp(text,"80%") && br_part[0].swing==BR_SWING_MAX);
        assert(br_step_duration_from_base(0,0,12800u)==20500u);
        assert(br_step_duration_from_base(0,1,12800u)==5100u);
        assert(br_capture(&saved) && br_session_v16_valid(&saved));
        br_part[0].swing=0; assert(br_restore(&saved) && br_part[0].swing==BR_SWING_MAX);
        br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u;
        swing=br_control(2u); br_control_text(2u,&swing,text);
        assert(!strcmp(text,"80%") && swing.high==BR_SWING_MAX);
    }
    {
        const uint32_t tempos[] = {60u, 120u, 200u, 240u};
        br_ui.page=BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX]=2u;
        bpm_control=br_control(1u);
        assert(bpm_control.low==60 && bpm_control.high==240);
        for (uint32_t i=0;i<sizeof tempos/sizeof tempos[0];++i) {
            br_bpm=tempos[i];
            assert(br_capture(&saved) && br_session_v16_valid(&saved));
            br_bpm=120u; assert(br_restore(&saved) && br_bpm==tempos[i]);
            assert(br_tempo_quarter_frames()==FS*60u/tempos[i]);
            assert(br_fx_delay_note_samples(br_fx_delay_note_store(5u),tempos[i])==
                   (FS*60u+tempos[i]/2u)/tempos[i]);
        }
        saved.reserved_v10[2]=0xd1u; saved.v3.bpm=24u; /* Old 280 BPM project. */
        assert(br_session_v16_valid(&saved) && br_restore(&saved) && br_bpm==240u);
        saved.reserved_v10[2]=0xd1u; saved.v3.bpm=25u; /* 281 BPM is invalid */
        assert(!br_session_v16_valid(&saved));
        br_bpm=120u; assert(br_capture(&saved)); saved.v3.bpm=40u;
        assert(br_session_v16_valid(&saved) && br_restore(&saved) && br_bpm==60u);
    }
    return 0;
}
